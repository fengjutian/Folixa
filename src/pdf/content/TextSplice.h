#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <Qt>

namespace aipdf::pdf {

// 一次替换的目标。
struct TextSpliceTarget {
    int pageIndex = 0;
    // 要替换的原文（按字体解码后的文字，不是内容流里的字节）。
    QString find;
    QString replace;
    Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
    // 这一页上的第几处（0 基）；-1 表示全部替换。
    int occurrence = 0;
};

struct TextSpliceReport {
    int pageIndex = 0;
    // 实际替换的处数。
    int occurrences = 0;
    int glyphsRemoved = 0;
    int glyphsInserted = 0;
    // 发生替换的字体资源名与 BaseFont（一次调用里只用一种字体）。
    QString fontResource;
    QString baseFont;
    QStringList removed;
    QStringList inserted;
    QStringList notes;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

struct TextSpliceResult {
    QByteArray bytes;
    TextSpliceReport report;

    bool ok() const { return report.ok() && !bytes.isEmpty(); }
};

// 在页内容流里做**字形级**替换：只改被选中的那几个字符，其余字节原样保留。
//
// 做法：定位显示操作里的字符串元素 -> 用页面字体把它解码成文字 ->
// 用**同一个字体**把新文字编码回字符码 -> 拼成 TJ 数组写回，
// 并在新旧文字宽度不同时插一个 TJ 位移量，让后面的字停在原来的位置。
//
// 边界（都是明确的失败，不静默降级）：
//   * 新文字里有原字体写不出的字 -> 报错并指出是哪个字（不做字体替换，
//     字体替换是另一条路径，不能在这里偷偷发生）；
//   * 匹配跨越多个字符串元素 -> 报错（跨操作数替换还没做）；
//   * 匹配落在 ' 与 " 操作符里 -> 报错（这两个还带换行语义，不能换成 TJ）；
//   * 匹配只在 Form XObject 里 -> 找不到（本层不递归 XObject）；
//   * 写回后会用同一套解码把整页文字重新读一遍，与期望值不一致就不写回。
TextSpliceResult splicePageText(const QByteArray& pdfBytes, const TextSpliceTarget& target);

}  // namespace aipdf::pdf
