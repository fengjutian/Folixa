#pragma once

#include <QString>

namespace aipdf::doc {

// XML 文本转义（& < > " '）。
//
// 为什么单独放一个头文件：docx / ofd / pptx / xlsx 四种包都要转义，
// 各写一份就会出现「四份实现慢慢分叉」的事。
inline QString escapeXmlText(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        switch (ch.unicode()) {
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
            out.append(ch);
            break;
        }
    }
    return out;
}

// OOXML 用的版本：比 escapeXmlText 多做两件事。
//
// 1. **丢掉 XML 1.0 不允许的控制字符**（除 \t \n \r 外的 0x00-0x1F）
//    与落单的代理项（U+D800-U+DFFF）。留着它们，写出来的包不是「格式不对」，
//    而是**Excel / PowerPoint 直接判定文件损坏**——字段值里混进一个 0x01
//    就足以让整份工作簿打不开。
// 2. **把回车写成字符引用 `&#xD;`**。XML 解析器会把元素正文里的字面回车
//    规范化成换行，字段值里的回车会被悄悄改掉；写成字符引用就不会被规范化
//    （字符引用不受行尾规范化影响）。
inline QString escapeOoxmlText(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code == 0x09 || code == 0x0A) {
            out.append(ch);
            continue;
        }
        if (code == 0x0D) {
            out.append(QStringLiteral("&#xD;"));
            continue;
        }
        if (code < 0x20) {
            continue;  // 其它控制字符：XML 1.0 不允许，直接丢掉。
        }
        if (code >= 0xD800 && code <= 0xDFFF) {
            continue;  // 落单的代理项：写进 XML 就是损坏。
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
            out.append(ch);
            break;
        }
    }
    return out;
}

}  // namespace aipdf::doc
