#include "pdf/Redaction.h"

#include <QImage>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {

namespace {

// 马赛克渲染分辨率：每点 2 像素。太高会白吃内存，太低块状化就没有意义。
constexpr double kMosaicRenderScale = 2.0;
// 马赛克块边长（渲染图上的像素）。缩小再放大形成的方块就是它。
constexpr int kMosaicBlockPixels = 8;
// 「与区域相交」的判定阈值：对象面积里只要有千分之一落在区域内就算相交。
// 擦除宁可多删——留着半截字形等于没擦。
constexpr double kOverlapThreshold = 0.001;
// 落实密文时的整页位图化分辨率。
constexpr double kApplyRasterScale = 2.0;

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

// 纯色小图：addImageLayer 会把它拉伸铺满目标矩形。
QImage solidImage(const QColor& color) {
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

// 一处标记解析之后的结果：页序号 + 已裁剪到页面盒子的矩形。
struct Target {
    int pageIndex = 0;
    QRectF rect;
};

// 把标记解析成「页 + 裁剪后的矩形」。
//
// 先全部校验再动手：半途失败会留下改了一半的页面，比整体失败更难收拾。
// 校验项：文档有效、区域非空、页序号在范围内、页面没有自带 /Rotate、
// 区域与页面盒子有交集（越界的会被裁剪到页面内）。
bool resolveTargets(const PdfDocument& document,
                    const QVector<RedactionMark>& marks,
                    QVector<Target>* targets,
                    QString* error) {
    if (!document.isValid()) {
        setError(error, QStringLiteral("文档无效，无法处理密文标记"));
        return false;
    }
    if (marks.isEmpty()) {
        setError(error, QStringLiteral("没有密文标记"));
        return false;
    }
    const int pageCount = document.pageCount();
    for (const RedactionMark& mark : marks) {
        if (mark.rect.isEmpty() || mark.rect.width() <= 0.0 || mark.rect.height() <= 0.0) {
            setError(error, QStringLiteral("密文标记区域为空"));
            return false;
        }
        QVector<int> pageIndexes;
        if (mark.pageIndex < 0) {
            for (int index = 0; index < pageCount; ++index) {
                pageIndexes.append(index);
            }
        } else {
            if (mark.pageIndex >= pageCount) {
                setError(error, QStringLiteral("密文标记指定的页序号 %1 超出文档页数 %2")
                                    .arg(mark.pageIndex)
                                    .arg(pageCount));
                return false;
            }
            pageIndexes.append(mark.pageIndex);
        }
        for (const int index : pageIndexes) {
            const auto page = document.page(index);
            if (!page.has_value()) {
                setError(error, QStringLiteral("取不到第 %1 页").arg(index + 1));
                return false;
            }
            // 页面自带 /Rotate 时，字符框是未旋转的用户空间坐标，而渲染图是旋转过的。
            // 这里不做旋转变换，免得盖歪——宁可明确失败，也不要默默画错位置。
            if (page->rotationDegrees() != 0) {
                setError(error, QStringLiteral("第 %1 页自带 /Rotate 旋转，"
                                              "密文坐标按未旋转的 PDF 用户空间给出，"
                                              "本次不做旋转变换")
                                    .arg(index + 1));
                return false;
            }
            const MediaBox box = page->contentBox();
            const QRectF pageRect(QPointF(box.left, box.bottom), QPointF(box.right, box.top));
            // 越界区域裁剪到页面内：用户拖出来的选区常常压到页边。
            const QRectF clipped = mark.rect.normalized().intersected(pageRect);
            if (clipped.isEmpty() || clipped.width() <= 0.0 || clipped.height() <= 0.0) {
                setError(error, QStringLiteral("密文标记区域不在第 %1 页范围内").arg(index + 1));
                return false;
            }
            Target target;
            target.pageIndex = index;
            target.rect = clipped;
            targets->append(target);
        }
    }
    return true;
}

// PDF user space 的矩形 → 渲染图上的像素矩形。
// 渲染图原点在左上角，PDF 用户空间原点在左下角，y 要翻过来。
QRect pixelRectFor(const MediaBox& box, const QRectF& rect, double scale, const QSize& imageSize) {
    const int x = static_cast<int>(std::floor((rect.left() - box.left) * scale));
    const int y = static_cast<int>(std::floor((box.top - rect.bottom()) * scale));
    const int width = static_cast<int>(std::ceil(rect.width() * scale));
    const int height = static_cast<int>(std::ceil(rect.height() * scale));
    const QRect pixelRect(x, y, width, height);
    return pixelRect.intersected(QRect(QPoint(0, 0), imageSize));
}

// 块状化：先缩到块大小（这一步做平均，所以每块是灰色而不是纯黑），
// 再用最近邻放大回去（这一步不加模糊，才看得见方块）。
QImage pixelate(const QImage& source) {
    if (source.isNull()) {
        return {};
    }
    const int width = source.width();
    const int height = source.height();
    int block = kMosaicBlockPixels;
    block = std::min(block, std::max(1, std::min(width, height) / 2));
    if (block <= 1) {
        return source;
    }
    const QImage small = source.scaled(std::max(1, width / block), std::max(1, height / block),
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return small.scaled(width, height, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

// 取出与区域相交的对象下标。文字层里能不能搜到，全看这些对象还在不在。
QVector<int> intersectingObjects(PdfPage& page, const QRectF& rect) {
    QVector<int> indexes;
    const QVector<PageObjectInfo> objects = page.objectsInRect(rect, kOverlapThreshold);
    indexes.reserve(objects.size());
    for (const PageObjectInfo& object : objects) {
        if (object.index >= 0) {
            indexes.append(object.index);
        }
    }
    return indexes;
}

}  // namespace

bool markRedactions(PdfDocument& document, const QVector<RedactionMark>& marks, QString* error) {
    QVector<Target> targets;
    if (!resolveTargets(document, marks, &targets, error)) {
        return false;
    }
    for (const Target& target : targets) {
        auto page = document.page(target.pageIndex);
        if (!page.has_value()) {
            setError(error, QStringLiteral("取不到第 %1 页").arg(target.pageIndex + 1));
            return false;
        }
        // 只盖黑块：内容一个字都不动，文字层照样能读到原文。
        if (!page->addImageLayer(target.rect, solidImage(Qt::black))) {
            setError(error, QStringLiteral("第 %1 页写入密文标记失败").arg(target.pageIndex + 1));
            return false;
        }
    }
    return true;
}

bool mosaicRedactions(PdfDocument& document, const QVector<RedactionMark>& marks, QString* error) {
    QVector<Target> targets;
    if (!resolveTargets(document, marks, &targets, error)) {
        return false;
    }
    for (const Target& target : targets) {
        auto page = document.page(target.pageIndex);
        if (!page.has_value()) {
            setError(error, QStringLiteral("取不到第 %1 页").arg(target.pageIndex + 1));
            return false;
        }
        // 先渲染再删：马赛克要的就是「原来那一片像素」。
        const QImage pageImage = page->render(kMosaicRenderScale);
        if (pageImage.isNull()) {
            setError(error, QStringLiteral("第 %1 页渲染失败，无法做马赛克").arg(target.pageIndex + 1));
            return false;
        }
        const MediaBox box = page->contentBox();
        const QRect pixelRect =
            pixelRectFor(box, target.rect, kMosaicRenderScale, pageImage.size());
        if (pixelRect.isEmpty()) {
            setError(error, QStringLiteral("第 %1 页的密文区域没有对应像素").arg(target.pageIndex + 1));
            return false;
        }
        const QImage mosaic = pixelate(pageImage.copy(pixelRect));
        if (mosaic.isNull()) {
            setError(error, QStringLiteral("第 %1 页生成马赛克失败").arg(target.pageIndex + 1));
            return false;
        }
        // 删掉与区域相交的对象：像素化只是「看不清」，不删对象的话
        // 文字层里还是能搜到原文，那就不是马赛克而是障眼法。
        const QVector<int> indexes = intersectingObjects(*page, target.rect);
        if (!indexes.isEmpty() && !page->removeObjects(indexes)) {
            setError(error, QStringLiteral("第 %1 页删除马赛克区域内的对象失败")
                                .arg(target.pageIndex + 1));
            return false;
        }
        // 只把马赛克贴回该区域，不整页贴：整页贴会把别处的矢量文字也毁掉。
        if (!page->addImageLayer(target.rect, mosaic)) {
            setError(error, QStringLiteral("第 %1 页写入马赛克失败").arg(target.pageIndex + 1));
            return false;
        }
    }
    return true;
}

bool eraseRedactions(PdfDocument& document, const QVector<RedactionMark>& marks, QString* error) {
    QVector<Target> targets;
    if (!resolveTargets(document, marks, &targets, error)) {
        return false;
    }
    for (const Target& target : targets) {
        auto page = document.page(target.pageIndex);
        if (!page.has_value()) {
            setError(error, QStringLiteral("取不到第 %1 页").arg(target.pageIndex + 1));
            return false;
        }
        const QVector<int> indexes = intersectingObjects(*page, target.rect);
        if (!indexes.isEmpty()) {
            if (!page->removeObjects(indexes)) {
                setError(error, QStringLiteral("第 %1 页删除密文区域内的对象失败")
                                    .arg(target.pageIndex + 1));
                return false;
            }
            // 补一块白底：字形被整个对象删掉时不会留半截，
            // 但共享的图形对象（表格线、下划线）可能还留着痕迹。
            if (!page->addImageLayer(target.rect, solidImage(Qt::white))) {
                setError(error, QStringLiteral("第 %1 页补白底失败").arg(target.pageIndex + 1));
                return false;
            }
        }
    }
    return true;
}

bool applyRedactions(PdfDocument& document, const QVector<RedactionMark>& marks, QString* error) {
    QVector<Target> targets;
    if (!resolveTargets(document, marks, &targets, error)) {
        return false;
    }
    for (const Target& target : targets) {
        auto page = document.page(target.pageIndex);
        if (!page.has_value()) {
            setError(error, QStringLiteral("取不到第 %1 页").arg(target.pageIndex + 1));
            return false;
        }
        // 落实标记：整页位图化 + 标记区域刷黑 + 删掉原对象与批注。
        // 代价是整页失去文字层——这正是它与「擦除」的区别。
        if (!page->redactByRasterizing(target.rect, kApplyRasterScale)) {
            setError(error, QStringLiteral("第 %1 页应用密文失败").arg(target.pageIndex + 1));
            return false;
        }
    }
    return true;
}

}  // namespace aipdf::pdf
