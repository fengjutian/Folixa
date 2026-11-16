#include "pdf/BarcodeEncoder.h"

#include <QChar>
#include <QStringList>

namespace aipdf::pdf {

namespace {

// Code 128 的 107 个符号图案（ISO/IEC 15417 标准表）。
//
// 为什么"下标 = 符号值"：Code 128 里每个符号值唯一对应一个条空图案；同一个图案在
// Code A/B/C 下含义不同（113141 在 Code B 里是"切到 Code C"，在 Code C 里是数字对
// "99"），但图案本身只由符号值决定。这样起始符、切换码、数据、校验位都能统一走
// "值 -> 图案"查表，不必为每类符号各写一份宽度串，也就不会写串行。
//
// 0..95 是 Code B 的 ASCII 32..127；96..102 是控制符（SHIFT / FNC / 切换码）；
// 103..106 是 Start A / Start B / Start C / Stop（Stop 是 13 模块，末尾 2 模块是结束条）。
const char* const kPatterns[107] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312", "132212",
    "221213", "221312", "231212", "112232", "122132", "122231", "113222", "123122", "123221",
    "223211", "221132", "221231", "213212", "223112", "312131", "311222", "321122", "321221",
    "312212", "322112", "322211", "212123", "212321", "232121", "111323", "131123", "131321",
    "112313", "132113", "132311", "211313", "231113", "231311", "112133", "112331", "132131",
    "113123", "113321", "133121", "313121", "211331", "231131", "213113", "213311", "213131",
    "311123", "311321", "331121", "312113", "312311", "332111", "314111", "221411", "431111",
    "111224", "111422", "121124", "121421", "141122", "141221", "112214", "112412", "122114",
    "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111", "111242",
    "121142", "121241", "114212", "124112", "124211", "411212", "421112", "421211", "212141",
    "214121", "412121", "111143", "111341", "131141", "114113", "114311", "411113", "411311",
    "113141", "114131", "311141", "411131", "211412", "211214", "211232", "2331112",
};

// 常用符号值。校验位要把起始符和切换码的值一起算进去，所以写死并保持和标准表一致。
constexpr int kCodeC = 99;    // 切换到 Code C
constexpr int kCodeB = 100;   // 切换到 Code B
constexpr int kStartB = 104;  // Start B
constexpr int kStartC = 105;  // Start C
constexpr int kStop = 106;    // Stop

// 码集分段：一段要么整体用 Code B（一个字符一个符号），要么整体用 Code C（两个数字一个符号）。
struct Segment {
    bool codeC = false;
    int begin = 0;   // 在原文中的起始下标
    int length = 0;  // 字符数（Code C 段必然是偶数）
};

bool isDigit(QChar ch) {
    return ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
}

// 把一个符号的宽度串展开成模块（条/空交替），并把相邻同色的合并成一个。
void appendSymbol(QVector<BarcodeModule>& modules, int value) {
    const char* digits = kPatterns[value];
    bool bar = true;  // 图案永远是"条、空、条、空……"这样交替
    for (const char* p = digits; *p != '\0'; ++p) {
        const int width = *p - '0';
        if (!modules.isEmpty() && modules.last().bar == bar) {
            modules.last().width += width;
        } else {
            modules.append(BarcodeModule{width, bar});
        }
        bar = !bar;
    }
}

}  // namespace

BarcodeCode128 encodeCode128(const QString& text) {
    BarcodeCode128 result;

    if (text.isEmpty()) {
        result.error = QStringLiteral("条码内容为空，无法生成条码");
        return result;
    }

    // 只接受 Code 128 B 能表示的 ASCII 32..127。
    // 这里不用 latin1 兜底替换：替换出来的条码扫出来是另一个值，用户只会看到"扫不出来"，
    // 完全看不出是编码阶段就失败了。宁可当场报错。
    for (const QChar ch : text) {
        const ushort code = static_cast<ushort>(ch.unicode());
        if (code < 32 || code > 127) {
            result.error =
                QStringLiteral("条码内容含有非 ASCII 字符，Code 128 只能编码 ASCII 32~127 的字符");
            return result;
        }
    }

    // 分段：偶数长度且长度 >= 4 的纯数字串走 Code C（两个数字一个符号，比 Code B 窄一半），
    // 其余走 Code B。奇数长度的数字串留在 Code B 里：切一半过去要多付一个切换符，
    // 省下的宽度补不回切换符的 11 模块。
    QVector<Segment> segments;
    const int textLength = text.size();
    int i = 0;
    while (i < textLength) {
        const bool digits = isDigit(text.at(i));
        int j = i + 1;
        while (j < textLength && isDigit(text.at(j)) == digits) {
            ++j;
        }
        const int count = j - i;
        const bool codeC = digits && (count % 2 == 0) && count >= 4;
        if (!segments.isEmpty() && segments.last().codeC == codeC) {
            // 相邻同码集并成一段，省掉一个切换符
            segments.last().length += count;
        } else {
            segments.append(Segment{codeC, i, count});
        }
        i = j;
    }

    // 生成符号值序列：起始符 ->（段与段之间插切换码）数据 -> 校验位 -> 终止符。
    QVector<int> symbols;
    symbols.reserve(segments.size() * 2 + 4);

    // 校验位按 Mod-103 算：起始符的值直接加上，之后每个符号按位置 1、2、3… 加权。
    // 起始符不加权、切换码按位置加权，这两处最容易写错，而写错条码不会报错，
    // 只会安静地生成一张扫不出来的图，所以这里刻意分开写。
    int checksum = 0;
    int weight = 0;
    bool first = true;
    for (const Segment& segment : segments) {
        const int head =
            segment.codeC ? (first ? kStartC : kCodeC) : (first ? kStartB : kCodeB);
        symbols.append(head);
        if (first) {
            checksum += head;
        } else {
            checksum += ++weight * head;
        }
        if (segment.codeC) {
            for (int k = 0; k < segment.length; k += 2) {
                const int pair = (text.at(segment.begin + k).unicode() - '0') * 10 +
                                 (text.at(segment.begin + k + 1).unicode() - '0');
                symbols.append(pair);
                checksum += ++weight * pair;
            }
        } else {
            for (int k = 0; k < segment.length; ++k) {
                const int value = text.at(segment.begin + k).unicode() - 32;
                symbols.append(value);
                checksum += ++weight * value;
            }
        }
        first = false;
    }
    symbols.append(checksum % 103);  // 校验位，紧挨在终止符前面
    symbols.append(kStop);

    for (const int value : symbols) {
        appendSymbol(result.modules, value);
    }

    // 码集只用于诊断与测试：记录实际用到哪些码集，按首次出现的顺序拼接。
    QStringList sets;
    for (const Segment& segment : segments) {
        const QString name = segment.codeC ? QStringLiteral("C") : QStringLiteral("B");
        if (!sets.contains(name)) {
            sets.append(name);
        }
    }
    result.codeSet = sets.join(QLatin1Char('+'));
    result.ok = true;
    return result;
}

}  // namespace aipdf::pdf
