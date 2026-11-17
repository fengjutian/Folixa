#include "pdf/InvisibleTextLayer.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>
#include <string>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

// ---------------------------------------------------------------------------
// 编码与字宽
// ---------------------------------------------------------------------------

// WinAnsi 编码里 0x80-0x9F 这一段与 Latin-1 不同，单独列出来。
struct WinAnsiSpecial {
    int code;
    ushort unicode;
};

const WinAnsiSpecial kWinAnsiSpecials[] = {
    {0x80, 0x20AC}, {0x82, 0x201A}, {0x83, 0x0192}, {0x84, 0x201E}, {0x85, 0x2026},
    {0x86, 0x2020}, {0x87, 0x2021}, {0x88, 0x02C6}, {0x89, 0x2030}, {0x8A, 0x0160},
    {0x8B, 0x2039}, {0x8C, 0x0152}, {0x8E, 0x017D}, {0x91, 0x2018}, {0x92, 0x2019},
    {0x93, 0x201C}, {0x94, 0x201D}, {0x95, 0x2022}, {0x96, 0x2013}, {0x97, 0x2014},
    {0x98, 0x02DC}, {0x99, 0x2122}, {0x9A, 0x0161}, {0x9B, 0x203A}, {0x9C, 0x0153},
    {0x9E, 0x017E}, {0x9F, 0x0178},
};

// Unicode 码点 → WinAnsi 字节；返回 -1 表示这个字符 WinAnsi 表示不了。
int winAnsiByte(ushort unicode) {
    if (unicode >= 0x20 && unicode <= 0x7E) return unicode;
    if (unicode >= 0xA0 && unicode <= 0xFF) return unicode;
    for (const WinAnsiSpecial& special : kWinAnsiSpecials) {
        if (special.unicode == unicode) return special.code;
    }
    return -1;
}

// PDF 标准 14 号字体 Helvetica 的字宽（单位 1/1000 em），覆盖 ASCII 0x20-0x7E。
//
// 为什么自己带字宽表而不是查系统字体：
// 写进 PDF 的是 Helvetica，读者用的也是 Helvetica 的标准字宽；
// 查系统字体反而会拿到 Arial/DejaVu 的宽度，与文件里写的对不上。
const short kHelveticaWidths[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584
};

// Helvetica 里非 ASCII 字符的字宽：用一个常见的平均值估算。
constexpr int kDefaultHelveticaWidth = 556;

// 「空字形」的宽度：Helvetica 里没有的字符（例如中文）落到 .notdef 上，
// 它的宽度各阅读器不完全一致，这里按 PDFium 的取值（500/1000 em）估。
constexpr int kNotdefWidth = 500;

// 字符写进内容流时用哪种字节编码。
enum class ByteMode {
    // 单字节，WinAnsi 字节码（配标准 14 号字体 Helvetica）。
    WinAnsi,
    // 单字节，自定义码（配一张 /ToUnicode 把码映射回 Unicode）。
    OneByteCode,
    // 双字节，Unicode 码点（Identity-H 的写法，配调用方注入的字体）。
    IdentityH
};

// 一个字符在当前编码下的字节宽度（1/1000 em）。
int glyphWidth(ByteMode mode, int code) {
    if (mode != ByteMode::WinAnsi) return kNotdefWidth;
    if (code >= 32 && code <= 126) return kHelveticaWidths[code - 32];
    return kDefaultHelveticaWidth;
}

// ---------------------------------------------------------------------------
// 文字分段
// ---------------------------------------------------------------------------

// 一段连续的文字：一次 Tm + 一次 Tj 写出去。
struct OcrChar {
    QChar ch;
    double x = 0.0;
    double right = 0.0;
    double fontSize = 0.0;
};

struct OcrRun {
    QVector<OcrChar> chars;
    double x = 0.0;         // 第一个字符的 x
    double baseline = 0.0;
    double width = 0.0;     // 最后一个字符的右边界 - 第一个字符的 x
    double fontSize = 0.0;  // 第一个字符的字号

    QString text() const {
        QString out;
        for (const OcrChar& character : chars) out += character.ch;
        return out;
    }
};

// 把一页的字符切成若干段。
//
// 为什么切段而不是一个字符一次 Tj：
// 相邻字符写在同一个字符串里时，读者不会在它们之间插空格；
// 一个字符一个 Tj 时，读者会按「位置上的空隙」判断要不要补空格，
// 于是搜「你好」会变成搜「你 好」。
QVector<OcrRun> buildRuns(const TextPage& page, const InvisibleTextLayerOptions& options) {
    QVector<OcrRun> runs;
    for (const TextChar& character : page.chars) {
        if (character.text.isEmpty()) continue;
        const double x = character.box.left();
        // 字符框的下边就是基线（PDF user space 原点在左下）。
        const double baseline = character.box.top();
        const double height = character.box.height();
        double fontSize = height;
        if (fontSize <= 0.5) fontSize = options.defaultFontSize;
        if (fontSize <= 0.5) fontSize = options.minimumFontSize > 0.0 ? options.minimumFontSize : 1.0;
        if (options.minimumFontSize > 0.0 && fontSize < options.minimumFontSize) {
            fontSize = options.minimumFontSize;
        }
        const double right = character.box.right();

        bool canExtend = false;
        if (!runs.isEmpty()) {
            const OcrRun& last = runs.last();
            const double reference = qMax(fontSize, last.fontSize);
            const bool sameLine = std::fabs(baseline - last.baseline) <= 0.5 * reference;
            const bool adjacent = x <= last.chars.last().right + 0.6 * reference;
            const bool sameSize = std::fabs(fontSize - last.fontSize) <= 0.5 * reference;
            canExtend = sameLine && adjacent && sameSize;
        }

        OcrChar item;
        item.ch = character.text.at(0);
        item.x = x;
        item.right = right;
        item.fontSize = fontSize;

        if (canExtend) {
            OcrRun& run = runs.last();
            run.chars.append(item);
        } else {
            OcrRun run;
            run.chars.append(item);
            run.x = x;
            run.baseline = baseline;
            run.width = right - x;
            run.fontSize = fontSize;
            runs.append(run);
        }
    }
    // 每段的右边界记在最后一个字符上。
    for (OcrRun& run : runs) {
        if (!run.chars.isEmpty()) {
            run.width = run.chars.last().right - run.x;
        }
    }
    return runs;
}

// 把跨了字体槽位的一段拆成几段。
//
// 为什么需要拆：一个简单字体的 /Differences 最多只能放 256 个码，
// 所以自定义码模式下 255 个码就要换一个字体资源；
// 一段文字如果正好跨过这个界线，就必须分开写，
// 否则后半段的字符会用错字体（写出来的字节对不上它的码）。
QVector<OcrRun> splitRunsBySlot(const QVector<OcrRun>& runs, ByteMode mode,
                                 const QHash<ushort, int>& slotOf) {
    if (mode != ByteMode::OneByteCode) return runs;
    QVector<OcrRun> result;
    for (const OcrRun& run : runs) {
        OcrRun current;
        int currentSlot = -1;
        for (const OcrChar& character : run.chars) {
            const int slot = qMax(0, slotOf.value(character.ch.unicode(), 0));
            if (currentSlot >= 0 && slot != currentSlot) {
                result.append(current);
                current = OcrRun();
            }
            // 新的那一段要从当前字符重新起头：位置、基线、字号都得跟着走。
            if (current.chars.isEmpty()) {
                current.x = character.x;
                current.baseline = run.baseline;
                current.fontSize = character.fontSize;
            }
            currentSlot = slot;
            current.chars.append(character);
        }
        if (!current.chars.isEmpty()) {
            current.width = current.chars.last().right - current.x;
            result.append(current);
        }
    }
    return result;
}

// 一段文字按字体字宽算出来的自然宽度。
double naturalRunWidth(const OcrRun& run, ByteMode mode, const QHash<ushort, int>& codeOf) {
    double total = 0.0;
    for (const OcrChar& character : run.chars) {
        int code = 0;
        if (mode == ByteMode::WinAnsi) {
            code = winAnsiByte(character.ch.unicode());
            if (code < 0) code = ' ';
        } else {
            code = codeOf.value(character.ch.unicode(), 0);
        }
        total += glyphWidth(mode, code) / 1000.0 * character.fontSize;
    }
    return total;
}

// 一段文字写进内容流时的字节。
QByteArray runBytes(const OcrRun& run, ByteMode mode, const QHash<ushort, int>& codeOf) {
    QByteArray out;
    for (const OcrChar& character : run.chars) {
        if (mode == ByteMode::IdentityH) {
            // Identity-H：Unicode 码点拆成高、低两个字节。
            const ushort unit = character.ch.unicode();
            out += static_cast<char>((unit >> 8) & 0xFF);
            out += static_cast<char>(unit & 0xFF);
            continue;
        }
        int code = 0;
        if (mode == ByteMode::WinAnsi) {
            code = winAnsiByte(character.ch.unicode());
            if (code < 0) code = '?';
        } else {
            code = codeOf.value(character.ch.unicode(), 0x20);
        }
        out += static_cast<char>(code);
    }
    return out;
}

// 一段文字里的字符落在哪个字体槽位（自定义码模式下才需要区分）。
//
// 注意：字节码是**槽位内**的编号（1..255），不能拿它反推槽位——
// 槽位号要单独记。
int runSlot(const OcrRun& run, ByteMode mode, const QHash<ushort, int>& slotOf) {
    if (mode != ByteMode::OneByteCode || run.chars.isEmpty()) return 0;
    return qMax(0, slotOf.value(run.chars.first().ch.unicode(), 0));
}

// ---------------------------------------------------------------------------
// 内容流
// ---------------------------------------------------------------------------

// 字节串转成 PDF 字面量字符串（带括号与转义）。
QByteArray pdfLiteralString(const QByteArray& raw) {
    QByteArray out("(");
    for (const char byte : raw) {
        switch (byte) {
        case '(':
            out += "\\(";
            break;
        case ')':
            out += "\\)";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default: {
            const unsigned char value = static_cast<unsigned char>(byte);
            if (value < 32 || value > 126) {
                // 八进制转义必须三位，否则会被后面的字符吃掉。
                out += '\\';
                out += QByteArray::number(value, 8).rightJustified(3, '0');
            } else {
                out += byte;
            }
            break;
        }
        }
    }
    out += ')';
    return out;
}

QByteArray formatNumber(double value) {
    QString text = QString::number(value, 'f', 3);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    if (text == QStringLiteral("-0")) text = QStringLiteral("0");
    return text.toLatin1();
}

}  // namespace

#ifdef AIPDF_WITH_QPDF
namespace {

// 生成一张 ToUnicode CMap（简单字体：一个字节就是一个码）。
QByteArray buildToUnicodeCMap(const QHash<int, ushort>& codes) {
    QByteArray out;
    out += "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n";
    out += "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n";
    out += "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n";
    out += "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n";
    QList<int> sorted = codes.keys();
    std::sort(sorted.begin(), sorted.end());
    out += QByteArray::number(sorted.size()) + " beginbfchar\n";
    for (int code : sorted) {
        const ushort unicode = codes.value(code);
        out += '<' + QByteArray::number(code, 16).rightJustified(2, '0') + "> <";
        out += QByteArray::number(unicode, 16).rightJustified(4, '0');
        out += ">\n";
    }
    out += "endbfchar\nendcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    return out;
}

// 建一个「假字形名」的 Differences 数组。
//
// 为什么用假字形名：文字是不可见的，读者不会去画这些字形，
// 字形名只需要唯一且合法；真正决定「搜出来是什么字」的是 ToUnicode。
std::vector<QPDFObjectHandle> buildDifferences(const QHash<int, ushort>& codes) {
    std::vector<QPDFObjectHandle> items;
    QList<int> sorted = codes.keys();
    std::sort(sorted.begin(), sorted.end());
    for (int code : sorted) {
        items.push_back(QPDFObjectHandle::newInteger(code));
        items.push_back(QPDFObjectHandle::newName(
            ("/g" + QString::number(code)).toLatin1().constData()));
    }
    return items;
}

// 不可见文字层在内容流里的起止标记。
//
// 为什么要有标记：给同一页写第二遍时，如果只是把新的一层追加到末尾，
// 两层不可见文字会画在同一个坐标上，PDFium 只保留一层 ——
// 第二遍的文字一个字都取不到，而函数还返回 ok=true。
// 有了标记，下次写之前就能把上一层准确地摘掉，再写新的（替换，不是叠加）。
//
// 用的是 PDF 内容流的 `%` 注释：渲染器会整行忽略，不影响任何东西。
const char kInvisibleLayerBegin[] = "%AIPDF-OCR-LAYER-BEGIN";
const char kInvisibleLayerEnd[] = "%AIPDF-OCR-LAYER-END";

// 把内容流里已有的不可见文字层摘掉。
//
// 找不到标记就原样返回（内容流不是本程序写的，或者已经被人手工改过）。
QByteArray removePreviousInvisibleLayer(const QByteArray& content) {
    const QByteArray begin(kInvisibleLayerBegin);
    const QByteArray end(kInvisibleLayerEnd);
    const int start = content.indexOf(begin);
    if (start < 0) {
        return content;
    }
    int stop = content.indexOf(end, start);
    if (stop < 0) {
        // 有起标记没有止标记：说明内容流被改坏了，别乱删，原样返回。
        return content;
    }
    stop += end.size();
    // 连同后面的换行一起去掉，免得留下空行。
    while (stop < content.size() && (content.at(stop) == '\n' || content.at(stop) == '\r')) {
        ++stop;
    }
    return content.left(start) + content.mid(stop);
}

// 在字体资源字典里挑一个没被占用的名字。
//
// 注意参数是「当前这一页**现在**的字体字典」，
// 而不是「写这一层之前页面原有的字体字典」——
// newFonts 是从原有字体复制出来、并且在分配过程中不断加入新名字的，
// 所以拿它来查，才能同时避开「原有的」与「本次已经分配出去的」。
//
// 只看原有字典是不够的：同一份 PDF 上做第二次 OCR 回写时，
// 两个槽位会挑到同一个名字，后一个把前一个覆盖掉 ——
// 结果是函数照样返回 ok=true，但前半段文字被按另一张字的映射解出来，静默写错。
QByteArray pickFontResourceName(const QPDFObjectHandle& fonts, int index) {
    for (int i = index; i < index + 1000; ++i) {
        const QByteArray candidate = QStringLiteral("/AipdfOcrF%1").arg(i + 1).toLatin1();
        if (!fonts.isDictionary() || !fonts.hasKey(candidate.constData())) {
            return candidate;
        }
    }
    return QStringLiteral("/AipdfOcrF1").toLatin1();
}

}  // namespace
#endif  // AIPDF_WITH_QPDF

InvisibleTextLayerResult writeInvisibleTextLayer(const QByteArray& pdfBytes,
                                                 const QVector<TextPage>& pages,
                                                 const InvisibleTextLayerOptions& options) {
    InvisibleTextLayerResult result;
    if (pdfBytes.isEmpty()) {
        result.error = QStringLiteral("输入的 PDF 内容为空");
        return result;
    }

#ifndef AIPDF_WITH_QPDF
    result.error = QStringLiteral("当前构建没有启用 qpdf，无法写入不可见文字层");
    return result;
#else
    try {
        QPDF pdf;
        pdf.processMemoryFile("invisible-text-layer.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> allPages = pdf.getAllPages();
        const int pageCount = static_cast<int>(allPages.size());
        if (pageCount <= 0) {
            result.error = QStringLiteral("PDF 里没有页面");
            return result;
        }
        result.pageCount = pageCount;

        // 文字页数必须与 PDF 页数一致：多写少写都会让文字落到别的页上。
        if (static_cast<int>(pages.size()) != pageCount) {
            const int difference = static_cast<int>(pages.size()) - pageCount;
            result.error = QStringLiteral("文字页数与 PDF 页数不一致：PDF 有 %1 页，提供了 %2 页文字（差 %3 页）")
                               .arg(pageCount)
                               .arg(pages.size())
                               .arg(qAbs(difference));
            return result;
        }
        for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
            const int declared = pages.at(i).pageIndex;
            if (declared >= 0 && declared != i) {
                result.error = QStringLiteral("第 %1 段文字标注的页序号是 %2，与它在列表里的位置不一致")
                                   .arg(i + 1)
                                   .arg(declared);
                return result;
            }
        }

        // 一页文字都没有 → 原样返回：没有东西可写，这不是错误。
        int totalChars = 0;
        for (const TextPage& page : pages) totalChars += page.charCount();
        if (totalChars == 0) {
            result.bytes = pdfBytes;
            return result;
        }

        // 决定字节编码方式。
        //
        // 注入字体时按 Identity-H 写（Unicode 码点拆两字节），这是调用方
        // 自己的字体能接收的写法。
        // 没注入字体时：全部字符都能用 WinAnsi 表示就用标准 14 号字体；
        // 否则加一张 ToUnicode——直接用 Helvetica 写中文等于把识别出来的字丢掉。
        ByteMode mode = ByteMode::WinAnsi;
        const bool injected = !options.fontResourceName.isEmpty();
        if (injected) {
            mode = ByteMode::IdentityH;
        } else {
            for (const TextPage& page : pages) {
                for (const TextChar& character : page.chars) {
                    for (int i = 0; i < character.text.size(); ++i) {
                        if (winAnsiByte(character.text.at(i).unicode()) < 0) {
                            mode = ByteMode::OneByteCode;
                            break;
                        }
                    }
                    if (mode == ByteMode::OneByteCode) break;
                }
                if (mode == ByteMode::OneByteCode) break;
            }
        }
        result.unicodeFontUsed = (mode == ByteMode::OneByteCode);

        // 字符 → 字节码（自定义码模式下要给每个不同的字符发一个码）。
        //
        // 字节码是**槽位内**的编号（1..255）：一个简单字体的 /Differences
        // 最多只能放 256 个码，所以每 255 个不同的字符就要换一个字体资源。
        // 槽位号单独记一份，不能拿字节码反推。
        QHash<ushort, int> codeOf;
        QHash<ushort, int> slotOf;
        QVector<QHash<int, ushort>> slotCodes;
        if (mode == ByteMode::OneByteCode) {
            for (const TextPage& page : pages) {
                for (const TextChar& character : page.chars) {
                    for (int i = 0; i < character.text.size(); ++i) {
                        const ushort unit = character.text.at(i).unicode();
                        if (codeOf.contains(unit)) continue;
                        const int index = codeOf.size();
                        const int slot = index / 255;
                        const int code = (index % 255) + 1;
                        codeOf.insert(unit, code);
                        slotOf.insert(unit, slot);
                        while (slotCodes.size() <= slot) slotCodes.append(QHash<int, ushort>());
                        slotCodes[slot].insert(code, unit);
                    }
                }
            }
        }

        // 建字体对象（每个槽位一个）。
        //
        // WinAnsi 路线只有一个字体（标准 14 号 Helvetica）；
        // 自定义码路线每个槽位一个（一个槽位最多 255 个码）；
        // 注入字体的路线一个都不用建——那是调用方自己的字体。
        QVector<QPDFObjectHandle> fontRefs;
        const int fontCount = injected ? 0
                                       : (mode == ByteMode::OneByteCode
                                              ? static_cast<int>(slotCodes.size())
                                              : 1);
        for (int slot = 0; slot < fontCount; ++slot) {
            QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
            font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
            font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
            // 基字体用标准 14 号字体：任何阅读器都认得，不依赖文档里已有的字体。
            font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
            if (mode == ByteMode::OneByteCode) {
                QPDFObjectHandle encoding = QPDFObjectHandle::newDictionary();
                encoding.replaceKey("/Type", QPDFObjectHandle::newName("/Encoding"));
                encoding.replaceKey("/Differences",
                                    QPDFObjectHandle::newArray(buildDifferences(slotCodes.at(slot))));
                font.replaceKey("/Encoding", encoding);
                const QByteArray cmap = buildToUnicodeCMap(slotCodes.at(slot));
                QPDFObjectHandle cmapStream =
                    QPDFObjectHandle::newStream(&pdf, std::string(cmap.constData(), cmap.size()));
                font.replaceKey("/ToUnicode", pdf.makeIndirectObject(cmapStream));
            } else {
                font.replaceKey("/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
            }
            fontRefs.append(pdf.makeIndirectObject(font));
        }

        // 逐页写。
        for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
            const TextPage& source = pages.at(pageIndex);
            const QVector<OcrRun> runs = splitRunsBySlot(buildRuns(source, options), mode, slotOf);
            if (runs.isEmpty()) continue;

            QPDFObjectHandle page = allPages.at(static_cast<size_t>(pageIndex));

            // 复制一份资源字典：不直接改共享的 /Resources，
            // 否则一页加字体会连带改到所有共用这份资源的页。
            QPDFObjectHandle resources = page.getKey("/Resources");
            QPDFObjectHandle newResources = QPDFObjectHandle::newDictionary();
            QPDFObjectHandle existingFonts;
            if (resources.isDictionary()) {
                for (const std::string& key : resources.getKeys()) {
                    newResources.replaceKey(key, resources.getKey(key));
                }
                existingFonts = resources.getKey("/Font");
            }
            QPDFObjectHandle newFonts = QPDFObjectHandle::newDictionary();
            if (existingFonts.isDictionary()) {
                for (const std::string& key : existingFonts.getKeys()) {
                    newFonts.replaceKey(key, existingFonts.getKey(key));
                }
            }

            // 这一页用到哪些槽位。
            QSet<int> usedSlots;
            usedSlots.insert(0);
            if (mode == ByteMode::OneByteCode) {
                for (const OcrRun& run : runs) {
                    usedSlots.insert(runSlot(run, mode, slotOf));
                }
            }

            // 给槽位分配页面里的资源名（要避开页面已有的名字）。
            QHash<int, QByteArray> slotName;
            for (int slot : usedSlots) {
                QByteArray name;
                if (injected) {
                    name = QByteArray("/") + options.fontResourceName.toLatin1();
                } else {
                    // 用 newFonts 查占用：它同时含有页面原有的字体
                    // 与本次调用里已经分配出去的名字。
                    name = pickFontResourceName(newFonts, slot);
                    newFonts.replaceKey(name.constData(), fontRefs.at(slot));
                }
                slotName.insert(slot, name);
            }
            newResources.replaceKey("/Font", pdf.makeIndirectObject(newFonts));
            page.replaceKey("/Resources", pdf.makeIndirectObject(newResources));

            // 内容流：不可见文字加在页面内容流的末尾。
            //
            // 为什么加在末尾：不可见文字是渲染模式 3，本来就看不见，
            // 但如果插在中间，后面的可见内容会盖在它上面——
            // 那样虽然还是看不见，选取高亮却会被后画的图形挡住。
            // 加在末尾则它总是最上面一层，高亮不会被遮。
            QByteArray ops;
            ops += kInvisibleLayerBegin;
            ops += "\nq\nBT\n3 Tr\n";
            QByteArray currentFont;
            double currentSize = -1.0;
            double currentScale = -1.0;
            for (const OcrRun& run : runs) {
                const int slot = runSlot(run, mode, slotOf);
                const QByteArray fontName = slotName.value(slot, slotName.value(0));
                if (fontName != currentFont || !qFuzzyCompare(currentSize + 1.0, run.fontSize + 1.0)) {
                    currentFont = fontName;
                    currentSize = run.fontSize;
                    ops += fontName + ' ' + formatNumber(run.fontSize) + " Tf\n";
                }
                // 横向拉伸：让文字宽度对上 OCR 给的框宽，
                // 这样选中高亮的位置与页面上的图像对得上。
                const double natural = naturalRunWidth(run, mode, codeOf);
                double scale = 100.0;
                if (natural > 0.01 && run.width > 0.01) {
                    scale = qBound(1.0, 100.0 * run.width / natural, 1000.0);
                }
                if (std::fabs(scale - currentScale) > 0.01) {
                    currentScale = scale;
                    ops += formatNumber(scale) + " Tz\n";
                }
                ops += "1 0 0 1 " + formatNumber(run.x) + ' ' + formatNumber(run.baseline) + " Tm\n";
                ops += pdfLiteralString(runBytes(run, mode, codeOf)) + " Tj\n";
                result.runCount += 1;
                result.charCount += run.chars.size();
            }
            ops += "ET\nQ\n";
            ops += kInvisibleLayerEnd;
            ops += "\n";

            // 把原内容流与新内容流接在一起。
            page.coalesceContentStreams();
            const std::vector<QPDFObjectHandle> contents = page.getPageContents();
            QByteArray combined;
            for (const QPDFObjectHandle& content : contents) {
                if (!content.isStream()) continue;
                // qpdf 的取流数据接口不是 const 方法，这里按值取一份句柄。
                QPDFObjectHandle stream = content;
                const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
                if (!data) continue;
                combined += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                       static_cast<qsizetype>(data->getSize()));
                combined += '\n';
            }
            // 先把上一次写的不可见文字层摘掉：新的一层是**替换**旧的一层。
            // 不摘的话两层会画在同一坐标上，PDFium 只保留一层，
            // 第二遍的文字一个字都取不到，而这里还照样返回 ok=true。
            combined = removePreviousInvisibleLayer(combined);
            combined += ops;
            QPDFObjectHandle stream =
                QPDFObjectHandle::newStream(&pdf, std::string(combined.constData(), combined.size()));
            page.replaceKey("/Contents", pdf.makeIndirectObject(stream));
        }

        QPDFWriter writer(pdf);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> output(writer.getBuffer());
        if (!output || output->getSize() == 0) {
            result.error = QStringLiteral("写出的 PDF 内容为空");
            return result;
        }
        result.bytes = QByteArray(reinterpret_cast<const char*>(output->getBuffer()),
                                  static_cast<qsizetype>(output->getSize()));
        return result;
    } catch (const std::exception& failure) {
        result.error = QStringLiteral("写入不可见文字层失败：%1")
                           .arg(QString::fromUtf8(failure.what()));
        return result;
    }
#endif
}

}  // namespace aipdf::pdf
