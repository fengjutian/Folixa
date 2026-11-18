#include "pdf/document/PdfRevisionCounter.h"

#include <QByteArray>
#include <QSet>

namespace aipdf::pdf {

namespace {

// xref 链最多走多少节。真实文档几十节到几百节就够多了，
// 上限是防「/Prev 指回自己」这类坏文件把程序挂死。
constexpr int kMaxXrefChain = 4096;

bool isWhitespace(char c) {
    return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' || c == '\0';
}

// 字典/名字里算作分隔符的字符。
bool isDelimiter(char c) {
    return c == '/' || c == '<' || c == '>' || c == '[' || c == ']' || c == '(' ||
           c == ')' || c == '{' || c == '}';
}

void skipWhitespace(const QByteArray& bytes, int* pos) {
    while (*pos < bytes.size() && isWhitespace(bytes.at(*pos))) {
        ++(*pos);
    }
}

// 读一个裸词（到空白或分隔符为止）。
QByteArray readBareToken(const QByteArray& bytes, int* pos) {
    skipWhitespace(bytes, pos);
    const int start = *pos;
    while (*pos < bytes.size() && !isWhitespace(bytes.at(*pos)) && !isDelimiter(bytes.at(*pos))) {
        ++(*pos);
    }
    return bytes.mid(start, *pos - start);
}

// 读一个十进制整数。读不到数字返回 false（不消耗已读的空白）。
bool readInteger(const QByteArray& bytes, int* pos, qint64* value) {
    skipWhitespace(bytes, pos);
    bool negative = false;
    if (*pos < bytes.size() && (bytes.at(*pos) == '-' || bytes.at(*pos) == '+')) {
        negative = bytes.at(*pos) == '-';
        ++(*pos);
    }
    qint64 result = 0;
    bool anyDigit = false;
    while (*pos < bytes.size() && bytes.at(*pos) >= '0' && bytes.at(*pos) <= '9') {
        result = result * 10 + (bytes.at(*pos) - '0');
        anyDigit = true;
        ++(*pos);
    }
    if (!anyDigit) {
        return false;
    }
    *value = negative ? -result : result;
    return true;
}

// 跳过一段 (…) 字符串（含转义与嵌套）。pos 停在右括号之后。
void skipLiteralString(const QByteArray& bytes, int* pos) {
    ++(*pos);  // 跳过 '('
    int depth = 1;
    while (*pos < bytes.size() && depth > 0) {
        const char c = bytes.at(*pos);
        if (c == '\\') {
            *pos += 2;
            continue;
        }
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
        }
        ++(*pos);
    }
}

// 跳过一段 <…> 十六进制字符串。
void skipHexString(const QByteArray& bytes, int* pos) {
    ++(*pos);  // 跳过 '<'
    while (*pos < bytes.size() && bytes.at(*pos) != '>') {
        ++(*pos);
    }
    if (*pos < bytes.size()) {
        ++(*pos);
    }
}

// 找出字典文本的范围（<< … >> 的内部，不含那两个尖括号）。
//
// 嵌套字典按层数配对，遇到第一个 ">>" 就收尾会提前截断，
// 所以这里数层数。
bool dictRangeAt(const QByteArray& bytes, int offset, int* start, int* end) {
    int pos = offset;
    skipWhitespace(bytes, &pos);
    if (pos + 1 >= bytes.size() || bytes.at(pos) != '<' || bytes.at(pos + 1) != '<') {
        return false;
    }
    pos += 2;
    *start = pos;
    int depth = 1;
    while (pos < bytes.size()) {
        const char c = bytes.at(pos);
        if (c == '(') {
            skipLiteralString(bytes, &pos);
            continue;
        }
        if (c == '<' && pos + 1 < bytes.size() && bytes.at(pos + 1) == '<') {
            ++depth;
            pos += 2;
            continue;
        }
        if (c == '>' && pos + 1 < bytes.size() && bytes.at(pos + 1) == '>') {
            --depth;
            pos += 2;
            if (depth == 0) {
                *end = pos;
                return true;
            }
            continue;
        }
        if (c == '<') {
            skipHexString(bytes, &pos);
            continue;
        }
        ++pos;
    }
    return false;
}

// 在字典文本里找某个顶层键（只看键，不看值）。
//
// 只认**键**：字符串与十六进制字符串里的 "/Prev" 不算数
// （文档标题写成 "(/Prev 3)" 这种，不该把修订链数错）。
bool dictHasTopLevelKey(const QByteArray& dict, const char* key) {
    const QByteArray wanted(key);
    int pos = 0;
    while (pos < dict.size()) {
        const char c = dict.at(pos);
        if (c == '(') {
            skipLiteralString(dict, &pos);
            continue;
        }
        if (c == '<') {
            if (pos + 1 < dict.size() && dict.at(pos + 1) == '<') {
                pos += 2;
                continue;
            }
            skipHexString(dict, &pos);
            continue;
        }
        if (c == '/') {
            ++pos;
            const int start = pos;
            while (pos < dict.size() && !isWhitespace(dict.at(pos)) && !isDelimiter(dict.at(pos))) {
                ++pos;
            }
            if (dict.mid(start, pos - start) == wanted) {
                return true;
            }
            continue;
        }
        ++pos;
    }
    return false;
}

// 在字典文本里找 /Prev 的值。
bool dictPrevOffset(const QByteArray& dict, qint64* prev) {
    int pos = 0;
    while (pos < dict.size()) {
        const char c = dict.at(pos);
        if (c == '(') {
            skipLiteralString(dict, &pos);
            continue;
        }
        if (c == '<') {
            if (pos + 1 < dict.size() && dict.at(pos + 1) == '<') {
                pos += 2;
                continue;
            }
            skipHexString(dict, &pos);
            continue;
        }
        if (c == '/') {
            ++pos;
            const int start = pos;
            while (pos < dict.size() && !isWhitespace(dict.at(pos)) && !isDelimiter(dict.at(pos))) {
                ++pos;
            }
            if (dict.mid(start, pos - start) == QByteArrayLiteral("Prev")) {
                qint64 value = 0;
                int valuePos = pos;
                if (readInteger(dict, &valuePos, &value)) {
                    *prev = value;
                    return true;
                }
            }
            continue;
        }
        ++pos;
    }
    return false;
}

// 一节 xref 读出来的东西。
struct XrefSection {
    bool valid = false;      // 这一节能不能读出来
    qint64 prev = -1;        // 上一节的偏移（没有就是 -1）
    bool linearized = false; // 这一节的 trailer 带 /Linearized（线性化文件的首页 xref）
};

// 读出 offset 处的 xref 节，并给出它指向的上一节（没有就是 -1）。
//
// 支持两种写法：
//   - classic xref 表："xref" + 小节头 + 条目 + "trailer" + 字典；
//   - xref 流："N G obj" + 字典 + "stream"。
XrefSection readXrefSection(const QByteArray& bytes, int offset) {
    XrefSection section;
    if (offset < 0 || offset >= bytes.size()) {
        return section;
    }
    int pos = offset;
    skipWhitespace(bytes, &pos);
    const QByteArray token = readBareToken(bytes, &pos);

    if (token == QByteArrayLiteral("xref")) {
        // classic xref 表：小节头（起始号 + 条数）后跟固定长度的条目。
        while (true) {
            const int before = pos;
            qint64 start = 0;
            qint64 count = 0;
            if (!readInteger(bytes, &pos, &start) || !readInteger(bytes, &pos, &count)) {
                pos = before;
                break;
            }
            // 每个条目一行（10 位偏移 + 代号 + 类型 + 行尾）。
            // 按行跳过而不是按 20 字节跳：有的文件行尾不是两个字符。
            for (qint64 i = 0; i < count; ++i) {
                // 先把上一行留下的行尾与空白吃掉：小节头后面的换行
                // 不算一条条目（不先吃掉它，整张表会少跳一行）。
                while (pos < bytes.size() && (bytes.at(pos) == '\n' || bytes.at(pos) == '\r' ||
                                              bytes.at(pos) == ' ' || bytes.at(pos) == '\t')) {
                    ++pos;
                }
                // 跳到本行末尾。
                while (pos < bytes.size() && bytes.at(pos) != '\n' && bytes.at(pos) != '\r') {
                    ++pos;
                }
            }
        }
        if (readBareToken(bytes, &pos) != QByteArrayLiteral("trailer")) {
            return section;
        }
        int dictStart = 0;
        int dictEnd = 0;
        if (!dictRangeAt(bytes, pos, &dictStart, &dictEnd)) {
            return section;
        }
        const QByteArray dict = bytes.mid(dictStart, dictEnd - dictStart);
        // 没有 /Prev 是**正常情况**（这就是第一节）：字典解析出来就算这一节读到了。
        dictPrevOffset(dict, &section.prev);
        section.linearized = dictHasTopLevelKey(dict, "Linearized");
        section.valid = true;
        return section;
    }

    // xref 流：先读间接对象头。
    const int afterToken = pos;
    int numberPos = afterToken - token.size();
    qint64 objectNumber = 0;
    qint64 generation = 0;
    if (!readInteger(bytes, &numberPos, &objectNumber)) {
        return section;
    }
    if (!readInteger(bytes, &numberPos, &generation)) {
        return section;
    }
    if (readBareToken(bytes, &numberPos) != QByteArrayLiteral("obj")) {
        return section;
    }
    int dictStart = 0;
    int dictEnd = 0;
    if (!dictRangeAt(bytes, numberPos, &dictStart, &dictEnd)) {
        return section;
    }
    const QByteArray dict = bytes.mid(dictStart, dictEnd - dictStart);
    // 同上：没有 /Prev 只是说明这是第一节，不算读失败。
    dictPrevOffset(dict, &section.prev);
    section.linearized = dictHasTopLevelKey(dict, "Linearized");
    section.valid = true;
    return section;
}

}  // namespace

int countPdfRevisions(const QByteArray& bytes) {
    if (bytes.isEmpty()) {
        return 0;
    }
    // 文件末尾的 startxref 指向最后一节 xref。
    const int marker = bytes.lastIndexOf(QByteArrayLiteral("startxref"));
    if (marker < 0) {
        return 0;
    }
    int pos = marker + static_cast<int>(QByteArrayLiteral("startxref").size());
    qint64 offset = 0;
    if (!readInteger(bytes, &pos, &offset)) {
        return 0;
    }
    if (offset < 0 || offset >= bytes.size()) {
        return 0;
    }

    // 一节一节沿 /Prev 往回走。链断在哪一节，就只算到那一节为止。
    //
    // 走过的偏移要记住：坏文件的 /Prev 可能指回走过的位置（成环），
    // 不记的话会一直绕到上限，把修订数数成几千。
    QSet<qint64> visited;
    int sections = 0;
    qint64 current = offset;
    while (current >= 0 && current < bytes.size() && sections < kMaxXrefChain) {
        if (visited.contains(current)) {
            break;  // /Prev 指回走过的位置：坏文件，停在这里。
        }
        visited.insert(current);
        const XrefSection section = readXrefSection(bytes, static_cast<int>(current));
        if (!section.valid) {
            break;
        }
        if (section.linearized) {
            // 线性化文件的**首页 xref**：它不是「旧修订」，
            // 是同一版为了边下边读而多放的一张表。数进去会平白多报一截。
            break;
        }
        ++sections;
        current = section.prev;
    }
    return sections;
}

}  // namespace aipdf::pdf
