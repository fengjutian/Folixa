#pragma once

#include <QChar>
#include <QString>

namespace aipdf::doc {

// XML 文本转义（& < > " '）。
//
// 为什么单独放一个头文件：docx / ofd / pptx / xlsx 四种包都要转义，
// 各写一份就会出现「四份实现慢慢分叉」的事。
//
// 除了五个保留字符，还做两件事，两者都是“不做就把文件写坏”：
//   * 丢掉 XML 1.0 不允许的控制字符（除 \t \n \r 外的 0x00-0x1F）；
//   * 丢掉**落单的**代理项，但保留合法代理对（emoji、CJK 扩展 B）。
//     以前按 `0xD800 <= code <= 0xDFFF` 一刀切，会把合法代理对整对丢掉，
//     导出的 docx/xlsx/pptx 里 emoji 静默消失。
inline QString escapeXmlText(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const ushort code = ch.unicode();

        if (QChar::isHighSurrogate(code)) {
            if (i + 1 < text.size() && QChar::isLowSurrogate(text.at(i + 1).unicode())) {
                out.append(ch);
                out.append(text.at(i + 1));
                ++i;
            }
            // 落单的高代理项：写进 XML 就是非法字符，直接丢掉。
            continue;
        }
        if (QChar::isLowSurrogate(code)) {
            continue;  // 落单的低代理项。
        }

        switch (code) {
        case '&':
            out.append(QStringLiteral("&amp;"));
            break;
        case '<':
            out.append(QStringLiteral("&lt;"));
            break;
        case '>':
            out.append(QStringLiteral("&gt;"));
            break;
        case '"':
            out.append(QStringLiteral("&quot;"));
            break;
        case '\'':
            out.append(QStringLiteral("&apos;"));
            break;
        default:
            if (code == 0x09 || code == 0x0A || code == 0x0D) {
                out.append(ch);
            } else if (code < 0x20 || code == 0xFFFE || code == 0xFFFF) {
                // XML 1.0 不允许：控制字符与两个非字符。
                continue;
            } else {
                out.append(ch);
            }
            break;
        }
    }
    return out;
}

// OOXML 用的版本：在 escapeXmlText 之上**把回车写成字符引用 `&#xD;`**。
//
// XML 解析器会把元素正文里的字面回车规范化成换行，字段值里的回车会被悄悄
// 改掉；写成字符引用就不会被规范化（字符引用不受行尾规范化影响）。
inline QString escapeOoxmlText(const QString& text) {
    QString escaped = escapeXmlText(text);
    escaped.replace(QLatin1Char('\r'), QStringLiteral("&#xD;"));
    return escaped;
}

}  // namespace aipdf::doc
