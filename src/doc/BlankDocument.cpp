#include "doc/BlankDocument.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtMath>

namespace aipdf::doc {

namespace {

// 页数上限。
//
// 为什么有上限：每一页都要写进页面树，页数是用户随手填的字段，
// 填错一个零就可能生成一个几百万页的文件。这里把明显不合理的输入挡掉。
constexpr int kMaxPageCount = 1000;

// 自定义页面上限：14400 点 = 200 英寸。
//
// 更大的尺寸没有实际意义（打印机根本吐不出这种纸），留着只会让
// 后续按纸张尺寸缩放时算出荒唐的比例。
constexpr double kMaxCustomDimensionPoints = 14400.0;

// 把页面尺寸写成 PDF 里的数字。
//
// 整数就写整数（595 而不是 595.0000），带小数时保留 4 位：
// 0.0001 点约为 0.000014 英寸，肉眼看不出来，但足够精确。
QString formatPoints(double value) {
    const double rounded = qRound64(value * 10000.0) / 10000.0;
    const qlonglong asInteger = qRound64(rounded);
    if (qAbs(rounded - static_cast<double>(asInteger)) < 1e-9) {
        return QString::number(asInteger);
    }
    return QString::number(rounded, 'f', 4);
}

// 拼 PDF 时的中间状态：正文 + 每个对象的起始偏移。
//
// 为什么要记偏移：PDF 的交叉引用表里写的是每个对象的**字节位置**，
// 边写边记偏移，否则偏移对不上，别的阅读器会认为文件损坏。
struct PdfObjectStream {
    QByteArray bytes;
    // 下标即对象号；值为该对象 "N 0 obj" 起始处的字节偏移。
    QVector<qint64> offsets;
};

// 写一个间接对象。
void appendObject(PdfObjectStream& stream, int number, const QString& body) {
    stream.offsets[number] = stream.bytes.size();
    stream.bytes.append(QStringLiteral("%1 0 obj\n").arg(number).toUtf8());
    stream.bytes.append(body.toUtf8());
    stream.bytes.append("\nendobj\n");
}

// 拼一份只含空白页的最小 PDF。
//
// 结构：1 个 Catalog、1 个 Pages 树、N 个页面对象。
//
// 为什么页面不写 /Contents：空白页本来就没有内容，而一个空的
// 内容流会让"内容流长度对不对"变成另一处可能出错的地方。
// PDF 规范里页面的 /Contents 是可选的，缺省就是空白页。
QByteArray buildBlankPdf(int pageCount, double widthPt, double heightPt) {
    // 对象号：1 = Catalog，2 = Pages，第 i 页（0 基）是第 3 + i 号对象。
    const int objectCount = 2 + pageCount;
    PdfObjectStream stream;
    stream.offsets.resize(objectCount + 1);

    stream.bytes.append("%PDF-1.7\n");
    // 二进制标记行：告诉别的程序这个文件里有非 ASCII 字节，
    // 免得有人用文本模式搬运时把它改坏。
    stream.bytes.append(QByteArrayLiteral("%\xE2\xE3\xCF\xD3\n"));

    appendObject(stream, 1, QStringLiteral("<< /Type /Catalog /Pages 2 0 R >>"));

    QStringList kids;
    kids.reserve(pageCount);
    for (int i = 0; i < pageCount; ++i) {
        kids.append(QStringLiteral("%1 0 R").arg(3 + i));
    }
    appendObject(stream, 2,
                 QStringLiteral("<< /Type /Pages /Kids [%1] /Count %2 >>")
                     .arg(kids.join(QLatin1Char(' ')))
                     .arg(pageCount));

    const QString mediaBox =
        QStringLiteral("[0 0 %1 %2]").arg(formatPoints(widthPt), formatPoints(heightPt));
    for (int i = 0; i < pageCount; ++i) {
        appendObject(stream, 3 + i,
                     QStringLiteral("<< /Type /Page /Parent 2 0 R /MediaBox %1 /Resources << >> >>")
                         .arg(mediaBox));
    }

    // 交叉引用表。每个条目固定 20 字节（10 位对象号、空格、5 位版本号、
    // 空格、类型标记、两字节行尾），这是 PDF 规范写死的格式。
    const qint64 xrefOffset = stream.bytes.size();
    QString xref = QStringLiteral("xref\n0 %1\n").arg(objectCount + 1);
    xref += QStringLiteral("0000000000 65535 f \n");
    for (int number = 1; number <= objectCount; ++number) {
        xref += QStringLiteral("%1 00000 n \n")
                    .arg(stream.offsets[number], 10, 10, QLatin1Char('0'));
    }
    stream.bytes.append(xref.toUtf8());
    stream.bytes.append(QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
                            .arg(objectCount + 1)
                            .arg(xrefOffset)
                            .toUtf8());
    return stream.bytes;
}

}  // namespace

QSizeF pageSizeInPoints(PageSize size, PageOrientation orientation) {
    QSizeF portrait;
    switch (size) {
    case PageSize::A4:
        portrait = QSizeF(595.0, 842.0);
        break;
    case PageSize::A3:
        portrait = QSizeF(842.0, 1191.0);
        break;
    case PageSize::A5:
        portrait = QSizeF(420.0, 595.0);
        break;
    case PageSize::Letter:
        portrait = QSizeF(612.0, 792.0);
        break;
    case PageSize::Legal:
        portrait = QSizeF(612.0, 1008.0);
        break;
    case PageSize::Custom:
        // 自定义宽高随调用而定，这张表里没有：返回无效尺寸（isValid() 为假）。
        return QSizeF();
    }

    if (orientation == PageOrientation::Landscape) {
        return QSizeF(portrait.height(), portrait.width());
    }
    return portrait;
}

QByteArray createBlankDocument(const BlankDocumentOptions& options, QString* error) {
    const auto fail = [error](const QString& reason) {
        if (error != nullptr) {
            *error = reason;
        }
        return QByteArray();
    };

    if (options.pageCount <= 0) {
        return fail(QStringLiteral("新建文档失败：页数必须大于 0，当前为 %1").arg(options.pageCount));
    }
    // 超过上限不报错：用户填 1001 页与填 1000 页没有本质区别，
    // 直接夹到上限比整份文档创建失败更好用。
    const int pageCount = qMin(options.pageCount, kMaxPageCount);

    double widthPt = 0.0;
    double heightPt = 0.0;
    if (options.size == PageSize::Custom) {
        // 用 `> 0` 判断而不是 `== 0`：NaN 也要挡掉（NaN 落到后面的
        // 尺寸比较里会一路静默通过，最后生成一份谁也打不开的文件）。
        if (!(options.widthPt > 0.0) || !(options.heightPt > 0.0)) {
            return fail(QStringLiteral("新建文档失败：自定义页面宽高必须是正数，当前为 %1 × %2 点")
                            .arg(options.widthPt, 0, 'f', 2)
                            .arg(options.heightPt, 0, 'f', 2));
        }
        if (options.widthPt > kMaxCustomDimensionPoints ||
            options.heightPt > kMaxCustomDimensionPoints) {
            return fail(QStringLiteral("新建文档失败：自定义页面尺寸不能超过 %1 点（200 英寸）")
                            .arg(formatPoints(kMaxCustomDimensionPoints)));
        }
        widthPt = options.widthPt;
        heightPt = options.heightPt;
    } else {
        const QSizeF size = pageSizeInPoints(options.size, options.orientation);
        widthPt = size.width();
        heightPt = size.height();
    }

    if (error != nullptr) {
        error->clear();
    }
    return buildBlankPdf(pageCount, widthPt, heightPt);
}

}  // namespace aipdf::doc
