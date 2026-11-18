#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace aipdf::doc::office {

// ---------------------------------------------------------------------------
// OOXML 包里的「资源通道」。
//
// 背景：DOCX / PPTX 都是 **zip 包**，图片不是写进正文 XML 里的，
// 而是作为独立部件（word/media/image1.png、ppt/media/image1.png）存在，
// 正文里用一个关系号（rId）指向它。没有这条通道，图片就一个都进不去。
//
// 这一层刻意**只认字节**：图片的解码与编码在 pdf 层做完（见
// pdf::PdfImage），doc 层只负责「给这堆字节起个名字、编号、登记关系」。
// 理由：doc 层一旦开始处理位图，就等于把编解码器拖进领域层；
// 而且导出器的输出必须是**确定的**（同样的输入 → 同样的字节），
// 交给 Qt 的图片保存器在 doc 层反而多一层不确定性。
//
// 图片之外，这里还放了两样「可以直接写进 OOXML 的图形结构」：
// 图片的落位（ImagePlacement）与表格的网格（TableGrid）。
// 它们和图片放在一起，是因为三者共用同一套坐标换算：
// **PDF 点 → 显示尺寸**，换错了图片就会飞出版面。
// ---------------------------------------------------------------------------

// 包里的一份媒体文件（目前只有图片）。
struct OfficeMedia {
    // 包内路径（全局唯一）："word/media/image1.png"。
    QString partName;
    // MIME 类型："image/png"。
    QString mimeType;
    // 文件字节。
    QByteArray bytes;
};

// 媒体集合：给整个包里的图片分配**全局连续**的编号与包内路径。
//
// 为什么编号要全局连续：word/media/ 与 ppt/media/ 共享包级的命名空间，
// 按页从 1 重来会撞名（两张页各放一张图，第二张就覆盖了第一张）。
class MediaSet {
public:
    // prefix 是包内媒体目录：DOCX 用 "word/media"，PPTX 用 "ppt/media"。
    //
    // 为什么要按格式分开：两个格式的媒体目录本来就是两套，混在一起
    // 会让 Word 找到 PPT 的图片（关系指向不存在的部件，Office 判文件损坏）。
    explicit MediaSet(const QString& prefix = QStringLiteral("media")) : prefix_(prefix) {}

    // 加一份媒体，返回它的序号（从 0 起，与 items() 下标一致）。
    int add(const QByteArray& bytes, const QString& mimeType = QStringLiteral("image/png"),
            const QString& extension = QStringLiteral("png")) {
        OfficeMedia media;
        media.partName = QStringLiteral("%1/image%2.%3").arg(prefix_).arg(nextNumber_).arg(extension);
        media.mimeType = mimeType;
        media.bytes = bytes;
        items_.append(media);
        // 编号必须递增：同名部件会互相覆盖（两张图只剩一张）。
        ++nextNumber_;
        return items_.size() - 1;
    }

    // 媒体份数。
    int count() const { return items_.size(); }
    bool isEmpty() const { return items_.isEmpty(); }

    // 第 index 份媒体。
    const OfficeMedia& at(int index) const { return items_.at(index); }
    const QVector<OfficeMedia>& items() const { return items_; }

    // [Content_Types].xml 里要补的 <Default> 项。
    //
    // 每项是「扩展名 → 内容类型」（png → image/png）。
    //
    // 少了这一项，Office 打开时会报「文件已损坏」：它按扩展名找内容类型，
    // 找不到就不知道 png 是什么。
    QVector<QPair<QString, QString>> contentTypeDefaults() const {
        QVector<QPair<QString, QString>> defaults;
        for (const OfficeMedia& media : items_) {
            const QString extension = media.partName.section(QLatin1Char('.'), -1);
            if (extension.isEmpty()) {
                continue;
            }
            bool seen = false;
            for (const QPair<QString, QString>& entry : defaults) {
                if (entry.first == extension) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                defaults.append(qMakePair(extension, media.mimeType));
            }
        }
        return defaults;
    }

private:
    QVector<OfficeMedia> items_;
    // 包内媒体目录（"word/media" / "ppt/media"）。
    QString prefix_;
    // 下一个编号（从 1 起；0 不用作编号）。
    int nextNumber_ = 1;
};

// 关系号：rId1、rId2……
//
// 编号在**一个部件内**连续：DOCX 的 document.xml.rels 与 PPTX 的
// slideN.xml.rels 各自从 1 开始，互不干扰。
inline QString relationshipId(int ordinal) { return QStringLiteral("rId%1").arg(ordinal + 1); }

// 一张图片的落位。
struct ImagePlacement {
    // 媒体集合里的序号（对应 MediaSet::at()）。
    int mediaIndex = -1;
    // 落位矩形（点，PDF user space，左下原点）。
    double leftPoints = 0.0;
    double bottomPoints = 0.0;
    // 显示尺寸（点，1/72 英寸）。
    //
    // 这两个数决定图片在文档里**显示多大**，与像素尺寸无关：
    // 像素尺寸只决定清晰度。把它当 EMU 用是这类导出最常见的错。
    double widthPoints = 0.0;
    double heightPoints = 0.0;
    // 属于页面上的哪一条文字带（双栏页面用它决定图片排在正文哪一段之后）。
    int bandIndex = 0;

    // 落位矩形的上边（PDF user space 里较大的 y，即页面上偏上的一边）。
    double upperEdgePoints() const { return bottomPoints + heightPoints; }
    // 落位矩形的下边（较小的 y）。
    double lowerEdgePoints() const { return bottomPoints; }
};

// 一个表格：矩形网格 + 单元格文字 + 列宽。
struct TableGrid {
    // 单元格文字（行 × 列）。缺格用空串占位。
    QVector<QVector<QString>> rows;
    // 每列的宽度（点）。列数与 rows 的列数一致。
    QVector<double> columnWidthsPoints;
    // 表格在页面上的位置与大小（点，PDF user space，左下原点）。
    double leftPoints = 0.0;
    double bottomPoints = 0.0;
    double widthPoints = 0.0;
    double heightPoints = 0.0;
    // 属于页面上的哪一条文字带。
    int bandIndex = 0;

    // 表格的上边（PDF user space 里较大的 y）。
    double topPoints() const { return bottomPoints + heightPoints; }

    int rowCount() const { return rows.size(); }
    int columnCount() const {
        int count = 0;
        for (const QVector<QString>& row : rows) {
            count = qMax(count, row.size());
        }
        return count;
    }
};

}  // namespace aipdf::doc::office
