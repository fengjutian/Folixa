#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::pdf {

// 文档属性（对应 PDF 的 /Info 字典）。
//
// 纯数据：这一层不认识界面，也不放用户可见文案（文案由 app 层拼）。
// 为什么单独一个结构体：标题、作者这些值既要能从 PDF 里读出来，
// 也要能填进界面再写回去，读写用同一个结构体才不会漏字段。
struct DocumentProperties {
    // 标题（/Title）。
    QString title;
    // 作者（/Author）。
    QString author;
    // 主题（/Subject）。
    QString subject;
    // 关键词（/Keywords）。
    QString keywords;
    // 创建程序（/Creator）。
    QString creator;
    // 生成程序（/Producer）。
    QString producer;
    // 创建时间（/CreationDate）：原样保留 PDF 里的字符串。
    //
    // 为什么原样存字符串：PDF 的日期有多种写法（D:20260101120000+08'00'、
    // D:20260101120000Z、只有 D:2026 这种残缺写法都有），
    // 自己转一遍格式既会丢时区，也会把残缺写法判成非法。
    QString creationDate;
    // 修改时间（/ModDate）：同样是原样保留。
    QString modificationDate;

    // 八个字段全空。
    bool isEmpty() const;
};

// 读 /Info。
//
// 没有 /Info 的 PDF 不算错：返回 true，字段全空。
// qpdf 不可用（本次构建没有编 AIPDF_WITH_QPDF）时返回 false，并给出中文错误。
bool readDocumentProperties(const QByteArray& pdfBytes,
                             DocumentProperties* out,
                             QString* error = nullptr);

// 写 /Info：存在的键改写，不存在的键补上。
//
// 空字符串的键**不写入**：否则一次只想改标题的保存，会把作者、主题全清成空。
// 整个文件重写（不是增量写）。
// qpdf 不可用时返回空 QByteArray，并给出中文错误。
QByteArray writeDocumentProperties(const QByteArray& pdfBytes,
                                    const DocumentProperties& properties,
                                    QString* error = nullptr);

}  // namespace aipdf::pdf
