#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdf_transformpage.h>
#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace aipdf::pdf {

// ============================================================================
// 页面对象模型：这个模块回答什么
// ============================================================================
//
// 整篇重排必然改版面（页数会变、位置会变），所以「重排之后内容还在不在」
// 没法靠肉眼比对，只能靠**逐类计数**：重排前数一遍，重排后再数一遍，
// 两边对不上就说明有东西没搬过去。
//
// 没有这一层，重排就只是「把能抄的抄过去」——抄漏了什么没人知道，
// 而页数一变，用户根本发现不了漏掉的是哪一块。
//
// 计数单位（关键，决定了计数有没有意义）：
//   * 文字：一个**块**（= 一段正文），不是一行。段落被拆成几行，仍然只算一块。
//   * 图片：一个图片对象算一张。
//   * 矢量：一个路径对象算一个。
//   * 表格：按网格识别出来的一张表算一个；表里的单元格文字归到表上，
//     不再单独算文字块（否则源文档与重排结果的表格会被数两遍）。
//   * 页眉 / 页脚：各算一个。
//
// 「什么算一块」由**文档自己的结构**决定，不靠几何猜测：
//   1. 页面对象上带着重排写的标记（AipdfReflowBlock）→ 按标记里的块号分组；
//   2. 否则用标签树给的归属索引：按 MCID 找到它所属的**块级结构元素**
//      （P、H1、TD…），同一个块级元素里的文字算一块；
//   3. 再不行（既没有标记也没有标签树）→ 每个文字对象自成一块。
//
// 为什么第 2 条要「块级祖先」而不是「最细的元素」：标签树里 P 下面常挂 Span，
// 一个段落会被拆成好几个 Span。按最细的元素算，源文档会把一段正文数成好几块，
// 而重排结果里它仍然是一段（一个块），两边一对不上，门禁就会误报。
//
// 实现方式：**头文件实现（header-only）**。
// 为什么不是 .cpp：pdf::reflowTaggedDocument 在 tests/cmake/DocumentReflowTests.cmake
// 那个目标里编译，那个目标只编 DocumentReflow.cpp 与它的几个依赖，
// 不编本模块的 .cpp。实现放在头文件里，那个目标不用改一行 CMake 就能链接。
// （PageObjectModel.cpp 与 ReflowPlan.cpp 仍然存在，作为这两个模块的翻译单元。）

// ---------------------------------------------------------------------------
// 种类与角色
// ---------------------------------------------------------------------------

// 页面对象的种类。
enum class PageObjectKind {
    Unknown,
    Text,    // 正文文字块（一段）
    Image,   // 图片
    Vector,  // 矢量图形（路径）
    Table,   // 表格
    Header,  // 页眉
    Footer,  // 页脚
};

// 文字块的角色：正文 / 页眉 / 页脚。
enum class PageTextRole {
    Body = 0,
    Header = 1,
    Footer = 2,
};

// 机器可读的种类键（拼错误信息、做比较用；不是给人看的文案）。
inline QString pageObjectKindKey(PageObjectKind kind) {
    switch (kind) {
    case PageObjectKind::Text:
        return QStringLiteral("text");
    case PageObjectKind::Image:
        return QStringLiteral("image");
    case PageObjectKind::Vector:
        return QStringLiteral("vector");
    case PageObjectKind::Table:
        return QStringLiteral("table");
    case PageObjectKind::Header:
        return QStringLiteral("header");
    case PageObjectKind::Footer:
        return QStringLiteral("footer");
    default:
        return QStringLiteral("unknown");
    }
}

// 重排结果里用的标记名与参数名。
//
// 为什么用标记（marked content）而不是只靠位置：重排后页数变了、位置变了，
// 「这段字原来在第几页第几行」这种位置信息在重排结果里已经对不上号了。
// 标记是写进文件里的身份信息，重新打开还在——门禁正是靠它把重排结果的
// 文字块和源文档的文字块对上的。
inline constexpr auto kReflowBlockMark = "AipdfReflowBlock";
inline constexpr auto kReflowBlockParam = "block";
inline constexpr auto kReflowRoleParam = "role";

// ---------------------------------------------------------------------------
// 计数
// ---------------------------------------------------------------------------

// 一篇文档里各类对象的数量。
struct PageObjectCounts {
    int textBlocks = 0;  // 正文文字块（段落级）
    int images = 0;      // 图片
    int vectors = 0;     // 矢量图形
    int tables = 0;      // 表格
    int headers = 0;     // 页眉
    int footers = 0;     // 页脚

    int total() const {
        return textBlocks + images + vectors + tables + headers + footers;
    }

    // 某一类的数量。
    int count(PageObjectKind kind) const {
        switch (kind) {
        case PageObjectKind::Text:
            return textBlocks;
        case PageObjectKind::Image:
            return images;
        case PageObjectKind::Vector:
            return vectors;
        case PageObjectKind::Table:
            return tables;
        case PageObjectKind::Header:
            return headers;
        case PageObjectKind::Footer:
            return footers;
        default:
            return 0;
        }
    }

    // 与另一份计数逐项比较，返回「对方少了哪几类、各少几个」。
    //
    // 只报「少的」：重排后对象变多不算问题（表格边框是重画的，矢量会变多），
    // 变少才是「有东西没搬过去」，那必须被门禁拦住。
    //
    // **页眉页脚不按个数比**：它们按「每页重复」的语义写出去，
    // 输出页数与源页数不一致时个数本来就对不上
    // （源文档两页各有一个页眉，重排成一页后页眉只写一份）。
    // 页眉页脚改按「文字还在不在」把关，见 pdf/ops/DocumentReflow.cpp 里的门禁。
    QVector<QPair<PageObjectKind, int>> missingComparedWith(const PageObjectCounts& other) const {
        QVector<QPair<PageObjectKind, int>> missing;
        static const PageObjectKind kinds[] = {
            PageObjectKind::Text,  PageObjectKind::Image,
            PageObjectKind::Vector, PageObjectKind::Table};
        for (const PageObjectKind kind : kinds) {
            const int difference = count(kind) - other.count(kind);
            if (difference > 0) missing.append(qMakePair(kind, difference));
        }
        return missing;
    }
};

// ---------------------------------------------------------------------------
// 对象
// ---------------------------------------------------------------------------

// 表格的一个单元格。
struct PageTableCell {
    // 行号 / 列号（0 基）。
    int row = 0;
    int column = 0;
    // 单元格里的文字。
    QString text;
    // 单元格在原页面上的位置。
    QRectF bounds;
};

// 页面上的一个对象（文字块 / 图片 / 矢量 / 表格 / 页眉 / 页脚）。
struct PageObject {
    // 稳定身份：由「种类 + 内容指纹 + 几何 + 序号」派生（见 derivePageObjectId）。
    //
    // 为什么不用「页面对象数组的下标」当下标：PDFium 的下标只在**当前这一版**
    // 页面里稳，删掉一个对象、加一个对象，后面所有对象的下标都会漂移。
    // 拿它当锚点，锚点就会指向别的东西。
    QString id;
    // 种类。
    PageObjectKind kind = PageObjectKind::Unknown;
    // 所在页（0 基）。
    int pageIndex = -1;
    // 位置与大小（PDF user space，左下原点）。
    QRectF bounds;
    // 文字（文字块 / 表格单元格的汇总文字；图片与矢量为空）。
    QString text;
    // 内容指纹（文字的规范化摘要；图片是像素数据摘要；其它为空）。
    QString fingerprint;
    // 表格的行列数（非表格为 0）。
    int rowCount = 0;
    int columnCount = 0;
    // 表格的单元格（表格才有）。
    QVector<PageTableCell> cells;
    // 图片按「原尺寸与 DPI 换算」得到的自然尺寸（点）。非图片为空。
    QSizeF naturalSize;
    // 这个对象由几个页面对象合成（表格 = 单元格个数；其它为 1）。
    int memberCount = 1;
    // 分组用的块号：重排标记里的块号，或标签树里的内容块下标。
    //
    // **不是稳定身份**：它只在「同一次统计里」用来把页面对象归到块上，
    // 跨文档、跨版本没有意义。跨修订要认对象请用 id。
    int sourceBlockKey = -1;
    // **不是稳定身份**：页面对象在本页对象数组里的下标。
    //
    // 只用于「同一次调用内」从模型找回 PDFium 对象句柄（重排搬运时要用）。
    // 跨调用、跨版本一律无效。
    int sourceObjectIndex = -1;
};

// 一篇文档的页面对象模型。
struct DocumentObjectModel {
    // 所有对象（按页、按页内次序）。
    QVector<PageObject> objects;
    // 各类对象的数量。
    PageObjectCounts counts;
    // 页数。
    int pageCount = 0;
    // 每页的盒子（PDF user space）。
    QVector<QRectF> pageSizes;
    // 读模型失败的原因（中文）。为空表示成功。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 某一类对象里出现过的**不同文字**（按第一次出现的顺序）。
//
// 为什么要有它：页眉页脚按「每页重复」的语义写出去，个数对不上是常事；
// 能不能说「没丢」要看文字还在不在，而不是看有几个。
// 正文／图片／表格不走这条路：它们按个数逐类比对（见 PageObjectCounts::missingComparedWith）。
inline QStringList distinctTextsOfKind(const DocumentObjectModel& model, PageObjectKind kind) {
    QStringList texts;
    for (const PageObject& object : model.objects) {
        if (object.kind != kind) continue;
        const QString text = object.text.trimmed();
        if (text.isEmpty()) continue;
        if (!texts.contains(text)) texts.append(text);
    }
    return texts;
}

// ---------------------------------------------------------------------------
// 文字块的归属索引（由标签树整理而来）
// ---------------------------------------------------------------------------

// 「哪个 MCID 属于哪一块」。
//
// 本模块不自己读标签树：读标签树是 compliance 层的事，
// kernel 不反向依赖它。由调用方（ops 层）读好转成这个索引传进来。
struct PageTextBlockIndex {
    // 「页号|MCID」→ 块号。
    QHash<QString, int> mcidToBlock;
    // 块号 → 角色（正文 / 页眉 / 页脚）。
    QVector<PageTextRole> blockRoles;
    // 块号 → 该块的文字（用来算内容指纹）。
    QStringList blockTexts;
    // 块号 → 该块的结构标签（例如 P、H1、Head）。
    QStringList blockTags;

    static QString mcidKey(int pageIndex, int mcid) {
        return QStringLiteral("%1|%2").arg(pageIndex).arg(mcid);
    }

    bool isEmpty() const { return mcidToBlock.isEmpty(); }
};

// ---------------------------------------------------------------------------
// 稳定 id 的派生
// ---------------------------------------------------------------------------

// 文字的内容指纹：规范化空白后取摘要。
//
// 为什么规范化：同一段文字在标签树里和在页面上读出来的空白常常不一样
// （换行、空格数量），不规范化就会算成两个不同的东西。
inline QString pageTextFingerprint(const QString& text) {
    QString normalized = text;
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    normalized = normalized.trimmed();
    if (normalized.isEmpty()) return QString();
    const QByteArray digest =
        QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(digest.left(16));
}

// 几何指纹：把坐标量化到 0.01 点再拼成字符串。
inline QString pageGeometryFingerprint(const QRectF& bounds) {
    return QStringLiteral("%1,%2,%3,%4")
        .arg(qRound(bounds.left() * 100.0), 0, 10)
        .arg(qRound(bounds.top() * 100.0), 0, 10)
        .arg(qRound(bounds.width() * 100.0), 0, 10)
        .arg(qRound(bounds.height() * 100.0), 0, 10);
}

// 派生对象的稳定身份。
//
// 组成：种类 + 内容指纹 + 几何 + 序号。
//   * 种类与内容指纹：认「这是什么」；
//   * 几何：认「它原来在哪儿」；
//   * 序号：同一页里长得完全一样的两个对象（比如两处一模一样的图）靠它区分。
//     序号只在「前面插进了同样的东西」时才会漂移——那种情况本来就无法
//     靠内容区分，如实说明即可。
inline QString derivePageObjectId(PageObjectKind kind, const QString& fingerprint,
                                  const QRectF& bounds, int ordinal) {
    const QString canonical = QStringLiteral("%1|%2|%3|%4")
                                  .arg(pageObjectKindKey(kind), fingerprint,
                                       pageGeometryFingerprint(bounds))
                                  .arg(ordinal);
    const QByteArray digest =
        QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(digest.left(16));
}

// ---------------------------------------------------------------------------
// 内部工具
// ---------------------------------------------------------------------------

namespace detail {

// PDFium 的库初始化：进程内只做一次。
//
// 与 pdf::PdfiumAdapter 的做法一致（那边也是 call_once，且都不销毁库）。
inline void ensurePageObjectModelPdfiumReady() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        FPDF_LIBRARY_CONFIG config = {};
        config.version = 2;
        FPDF_InitLibraryWithConfig(&config);
    });
}

// PDFium 的进程级锁。
//
// 为什么需要：PDFium 的全局状态不是线程安全的，仓库里踩过「随机堆损坏」。
// 注意：pdf::PdfiumAdapter 里另有一把锁，两把锁不是同一把——这是已知限制，
// 调用方应当避免在渲染线程正忙的时候跑整篇重排。
inline std::recursive_mutex& pageObjectModelPdfiumMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

// 页面上的一段文字（还没分组）。
struct PageTextRun {
    int pageIndex = -1;
    int mcid = -1;
    QRectF bounds;
    QString text;
    double fontSize = 0.0;
    // 分组用的键：同一个键的文字属于同一块。
    int blockKey = -1;
    PageTextRole role = PageTextRole::Body;
    // 页内次序（从上往下）。
    int ordinal = 0;
};

// 分好组的一块文字。
struct PageTextBlock {
    int key = -1;
    PageTextRole role = PageTextRole::Body;
    QVector<int> runIndexes;
    QRectF bounds;
    QString text;
};

// 页面上识别出的一张表。
struct PageTableGrid {
    int rowCount = 0;
    int columnCount = 0;
    QRectF bounds;
    QStringList cellTexts;
    QVector<PageTableCell> cells;
    QVector<int> runIndexes;
};

inline double medianOf(QVector<double> values) {
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const int middle = values.size() / 2;
    return values.size() % 2 ? values.at(middle)
                             : (values.at(middle - 1) + values.at(middle)) * 0.5;
}

// 读一个文字对象的文字。
inline QString readTextObjectText(FPDF_PAGEOBJECT object, FPDF_TEXTPAGE textPage) {
    if (textPage == nullptr) return QString();
    const unsigned long bytes = FPDFTextObj_GetText(object, textPage, nullptr, 0);
    if (bytes < 2 || bytes > 1024 * 1024) return QString();
    QVector<FPDF_WCHAR> buffer(static_cast<qsizetype>(bytes / sizeof(FPDF_WCHAR)));
    if (!FPDFTextObj_GetText(object, textPage, buffer.data(), bytes)) return QString();
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.constData()))
        .remove(QChar::Null);
}

// 读一个图片对象的内容指纹：像素数据摘要。
inline QString readImageFingerprint(FPDF_PAGEOBJECT object) {
    unsigned int width = 0;
    unsigned int height = 0;
    if (!FPDFImageObj_GetImagePixelSize(object, &width, &height)) return QString();
    const QByteArray size = QStringLiteral("%1x%2").arg(width).arg(height).toLatin1();
    // 原始数据可能很大（未压缩的位图），只取前 4 MiB 参与摘要。
    const unsigned long needed = FPDFImageObj_GetImageDataRaw(object, nullptr, 0);
    if (needed == 0 || needed > 4UL * 1024 * 1024) {
        const QByteArray digest =
            QCryptographicHash::hash(size, QCryptographicHash::Sha1).toHex();
        return QString::fromLatin1(digest.left(16));
    }
    QByteArray raw(static_cast<qsizetype>(needed), Qt::Uninitialized);
    const unsigned long read = FPDFImageObj_GetImageDataRaw(object, raw.data(), needed);
    if (read == 0) {
        const QByteArray digest =
            QCryptographicHash::hash(size, QCryptographicHash::Sha1).toHex();
        return QString::fromLatin1(digest.left(16));
    }
    raw.truncate(static_cast<qsizetype>(read));
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(size);
    hash.addData(raw);
    return QString::fromLatin1(hash.result().toHex().left(16));
}

// 读一个图片对象的自然尺寸：像素数按 DPI 换算成点。
//
// 「原尺寸与 DPI 换算」就是这里：一张 300×200 像素、96 DPI 的图，
// 自然尺寸是 300×72/96 = 225 磅宽。重排时按这个尺寸放，放不下才等比缩小。
inline QSizeF readImageNaturalSize(FPDF_PAGEOBJECT object, FPDF_PAGE page) {
    FPDF_IMAGEOBJ_METADATA metadata = {};
    if (!FPDFImageObj_GetImageMetadata(object, page, &metadata)) return QSizeF();
    if (metadata.width == 0 || metadata.height == 0) return QSizeF();
    const double horizontal = metadata.horizontal_dpi > 1.0 ? metadata.horizontal_dpi : 72.0;
    const double vertical = metadata.vertical_dpi > 1.0 ? metadata.vertical_dpi : 72.0;
    return QSizeF(metadata.width * 72.0 / horizontal, metadata.height * 72.0 / vertical);
}

// 读一个页面对象上的重排标记（块号与角色）。
inline bool readReflowMark(FPDF_PAGEOBJECT object, int* blockKey, PageTextRole* role) {
    const int markCount = FPDFPageObj_CountMarks(object);
    for (int index = 0; index < markCount; ++index) {
        FPDF_PAGEOBJECTMARK mark = FPDFPageObj_GetMark(object, static_cast<unsigned long>(index));
        if (mark == nullptr) continue;
        unsigned long nameBytes = 0;
        if (!FPDFPageObjMark_GetName(mark, nullptr, 0, &nameBytes)) continue;
        if (nameBytes < 2 || nameBytes > 4096) continue;
        QByteArray name(static_cast<qsizetype>(nameBytes), Qt::Uninitialized);
        if (!FPDFPageObjMark_GetName(mark, reinterpret_cast<FPDF_WCHAR*>(name.data()), nameBytes,
                                     &nameBytes)) {
            continue;
        }
        const QString markName = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(name.constData())).remove(QChar::Null);
        if (markName != QLatin1String(kReflowBlockMark)) continue;
        int block = -1;
        if (!FPDFPageObjMark_GetParamIntValue(mark, kReflowBlockParam, &block)) continue;
        int roleValue = static_cast<int>(PageTextRole::Body);
        FPDFPageObjMark_GetParamIntValue(mark, kReflowRoleParam, &roleValue);
        if (blockKey != nullptr) *blockKey = block;
        if (role != nullptr) {
            *role = roleValue == static_cast<int>(PageTextRole::Header)
                        ? PageTextRole::Header
                        : (roleValue == static_cast<int>(PageTextRole::Footer)
                               ? PageTextRole::Footer
                               : PageTextRole::Body);
        }
        return true;
    }
    return false;
}

// 网格识别：把一页里的文字段认成表格。
//
// 口径与仓库里 pdf::TableStructure::detect 一致（同样的行聚类、同样的
// 列对齐容差、同样的 0.70 置信度门槛），但这里自己写一份：
// 本模块是头文件实现，不能依赖 TableStructure.cpp（那个翻译单元不在
// DocumentReflowTests 目标里）。
inline QVector<PageTableGrid> detectPageTableGrids(const QVector<PageTextRun>& runs) {
    QVector<PageTableGrid> grids;
    QVector<int> usable;
    QVector<double> heights;
    for (int index = 0; index < runs.size(); ++index) {
        const PageTextRun& run = runs.at(index);
        if (run.text.trimmed().isEmpty() || run.bounds.isEmpty()) continue;
        usable.append(index);
        heights.append(run.bounds.height());
    }
    if (usable.size() < 4) return grids;

    const double rowTolerance = qMax(2.0, medianOf(heights) * 0.65);

    // 按行聚类：从上往下（y 大在上）。
    QVector<int> order = usable;
    std::sort(order.begin(), order.end(), [&runs](int left, int right) {
        const double leftY = runs.at(left).bounds.center().y();
        const double rightY = runs.at(right).bounds.center().y();
        if (!qFuzzyCompare(leftY + 1.0, rightY + 1.0)) return leftY > rightY;
        return runs.at(left).bounds.left() < runs.at(right).bounds.left();
    });

    QVector<QVector<int>> rows;
    QVector<double> rowCenters;
    for (const int index : order) {
        const double center = runs.at(index).bounds.center().y();
        int target = -1;
        for (int row = 0; row < rows.size(); ++row) {
            if (std::abs(rowCenters.at(row) - center) <= rowTolerance) {
                target = row;
                break;
            }
        }
        if (target < 0) {
            rows.append(QVector<int>{index});
            rowCenters.append(center);
        } else {
            rows[target].append(index);
            rowCenters[target] =
                (rowCenters.at(target) * rows.at(target).size() + center) /
                (rows.at(target).size() + 1);
        }
    }

    // 一行里只有一个对象的，多半是正文而不是表格，整行丢掉。
    QVector<QVector<int>> keptRows;
    for (const QVector<int>& row : rows) {
        if (row.size() >= 2) keptRows.append(row);
    }
    if (keptRows.size() < 2) return grids;

    int columns = 0;
    for (QVector<int>& row : keptRows) {
        std::sort(row.begin(), row.end(), [&runs](int left, int right) {
            return runs.at(left).bounds.center().x() < runs.at(right).bounds.center().x();
        });
        columns = qMax(columns, row.size());
    }
    if (columns < 2) return grids;

    QVector<double> columnCenters(columns, 0.0);
    QVector<int> columnSamples(columns, 0);
    for (const QVector<int>& row : keptRows) {
        if (row.size() != columns) continue;
        for (int column = 0; column < columns; ++column) {
            columnCenters[column] += runs.at(row.at(column)).bounds.center().x();
            ++columnSamples[column];
        }
    }
    for (int column = 0; column < columns; ++column) {
        if (columnSamples.at(column) == 0) return grids;
        columnCenters[column] /= columnSamples.at(column);
    }

    PageTableGrid grid;
    grid.rowCount = keptRows.size();
    grid.columnCount = columns;
    int aligned = 0;
    int total = 0;
    for (int rowIndex = 0; rowIndex < keptRows.size(); ++rowIndex) {
        const QVector<int>& row = keptRows.at(rowIndex);
        for (const int index : row) {
            const PageTextRun& run = runs.at(index);
            int bestColumn = 0;
            double bestDistance = std::numeric_limits<double>::max();
            for (int column = 0; column < columns; ++column) {
                const double distance =
                    std::abs(run.bounds.center().x() - columnCenters.at(column));
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestColumn = column;
                }
            }
            const double tolerance = qMax(4.0, run.bounds.width() * 0.75);
            if (bestDistance <= tolerance) ++aligned;
            ++total;
            PageTableCell cell;
            cell.row = rowIndex;
            cell.column = bestColumn;
            cell.text = run.text.trimmed();
            cell.bounds = run.bounds;
            grid.cells.append(cell);
            grid.runIndexes.append(index);
            grid.cellTexts.append(run.text.trimmed());
            grid.bounds = grid.bounds.isNull() ? run.bounds : grid.bounds.united(run.bounds);
        }
    }
    const double confidence = total > 0 ? static_cast<double>(aligned) / total : 0.0;
    if (confidence < 0.70) return grids;
    grids.append(grid);
    return grids;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// 读整篇文档的页面对象模型
// ---------------------------------------------------------------------------

// 读一份 PDF 的页面对象模型。
//
// blocks 是「文字块归属索引」（由标签树整理而来，可以为空）。
// 为空时：没有重排标记、也没有标签树归属，每个文字对象自成一块。
inline DocumentObjectModel readPageObjectModel(const QByteArray& pdfBytes,
                                               const PageTextBlockIndex& blocks = {},
                                               QString* error = nullptr) {
    DocumentObjectModel model;
    if (pdfBytes.isEmpty()) {
        model.error = QStringLiteral("没有可统计的 PDF 内容");
        if (error != nullptr) *error = model.error;
        return model;
    }

    detail::ensurePageObjectModelPdfiumReady();
    std::lock_guard<std::recursive_mutex> lock(detail::pageObjectModelPdfiumMutex());

    FPDF_DOCUMENT document = FPDF_LoadMemDocument(pdfBytes.constData(),
                                                  static_cast<int>(pdfBytes.size()), nullptr);
    if (document == nullptr) {
        model.error = QStringLiteral("PDFium 打不开这份 PDF，统计不了页面对象");
        if (error != nullptr) *error = model.error;
        return model;
    }

    const int pageCount = FPDF_GetPageCount(document);
    model.pageCount = pageCount;

    QVector<detail::PageTextRun> runs;
    // 图片与矢量：只记位置与指纹，搬运由调用方做。
    struct NonTextObject {
        PageObjectKind kind = PageObjectKind::Unknown;
        int pageIndex = -1;
        int objectIndex = -1;
        QRectF bounds;
        QString fingerprint;
        QSizeF naturalSize;
    };
    QVector<NonTextObject> nonText;

    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        FPDF_PAGE page = FPDF_LoadPage(document, pageIndex);
        if (page == nullptr) continue;

        float left = 0.0F;
        float bottom = 0.0F;
        float right = 0.0F;
        float top = 0.0F;
        if (FPDFPage_GetMediaBox(page, &left, &bottom, &right, &top)) {
            model.pageSizes.append(
                QRectF(left, bottom, right - left, top - bottom));
        } else {
            model.pageSizes.append(QRectF());
        }

        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
        const int objectCount = FPDFPage_CountObjects(page);
        for (int objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
            FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, objectIndex);
            if (object == nullptr) continue;
            float objectLeft = 0.0F;
            float objectBottom = 0.0F;
            float objectRight = 0.0F;
            float objectTop = 0.0F;
            const bool hasBounds =
                FPDFPageObj_GetBounds(object, &objectLeft, &objectBottom, &objectRight, &objectTop);
            const QRectF bounds(objectLeft, objectBottom, objectRight - objectLeft,
                                objectTop - objectBottom);
            const int type = FPDFPageObj_GetType(object);

            if (type == FPDF_PAGEOBJ_TEXT) {
                detail::PageTextRun run;
                run.pageIndex = pageIndex;
                run.mcid = FPDFPageObj_GetMarkedContentID(object);
                run.bounds = bounds;
                run.text = detail::readTextObjectText(object, textPage);
                float fontSize = 0.0F;
                if (FPDFTextObj_GetFontSize(object, &fontSize)) run.fontSize = fontSize;

                int markBlock = -1;
                PageTextRole markRole = PageTextRole::Body;
                if (detail::readReflowMark(object, &markBlock, &markRole)) {
                    // 重排标记最可信：它就是重排时写下的块身份。
                    run.blockKey = markBlock;
                    run.role = markRole;
                } else if (run.mcid >= 0) {
                    const QString key = PageTextBlockIndex::mcidKey(pageIndex, run.mcid);
                    const auto found = blocks.mcidToBlock.constFind(key);
                    if (found != blocks.mcidToBlock.constEnd()) {
                        run.blockKey = found.value();
                        if (found.value() < blocks.blockRoles.size()) {
                            run.role = blocks.blockRoles.at(found.value());
                        }
                    }
                }
                if (run.blockKey < 0) {
                    // 没有归属信息：这个文字对象自成一块（用负数区间的键，
                    // 不会和标记／索引的块号撞上）。
                    run.blockKey = -1000000 - runs.size();
                }
                run.ordinal = runs.size();
                runs.append(run);
                continue;
            }

            NonTextObject item;
            item.pageIndex = pageIndex;
            item.objectIndex = objectIndex;
            item.bounds = bounds;
            if (type == FPDF_PAGEOBJ_IMAGE) {
                item.kind = PageObjectKind::Image;
                item.fingerprint = detail::readImageFingerprint(object);
                item.naturalSize = detail::readImageNaturalSize(object, page);
            } else if (type == FPDF_PAGEOBJ_PATH) {
                item.kind = PageObjectKind::Vector;
            } else if (type == FPDF_PAGEOBJ_SHADING) {
                item.kind = PageObjectKind::Vector;
            } else {
                // 表单 XObject 之类的容器：算矢量图形（它是页面上的可见图形），
                // 但内容指纹为空，只能靠几何区分。
                item.kind = PageObjectKind::Vector;
                item.fingerprint = QStringLiteral("container");
            }
            if (hasBounds) nonText.append(item);
        }
        if (textPage != nullptr) FPDFText_ClosePage(textPage);
        FPDF_ClosePage(page);
    }
    FPDF_CloseDocument(document);

    // ---- 分组：同一块号的文字属于同一块 ----
    //
    // 正文按**块号**分组：一个段落就算排到了好几页上，仍然是一块
    // （否则源文档里一段、输出里两段，计数对不上，门禁会误报）。
    //
    // 页眉页脚按**页号 + 块号**分组：页眉是「每页一份」的，
    // 同一个块号在好几页上出现时那是好几份页眉，不能并成一块——
    // 并成一块的话，块里的文字会被拼成「Running head Running head …」，
    // 页眉还在，却再也认不出它是什么。
    QVector<detail::PageTextBlock> grouped;
    QHash<QString, int> blockIndexByKey;
    for (int runIndex = 0; runIndex < runs.size(); ++runIndex) {
        const detail::PageTextRun& run = runs.at(runIndex);
        const bool perPage =
            run.role == PageTextRole::Header || run.role == PageTextRole::Footer;
        const QString groupKey =
            perPage ? QStringLiteral("%1|%2").arg(run.pageIndex).arg(run.blockKey)
                    : QString::number(run.blockKey);
        const auto found = blockIndexByKey.constFind(groupKey);
        int blockIndex = -1;
        if (found != blockIndexByKey.constEnd()) {
            blockIndex = found.value();
        } else {
            blockIndex = grouped.size();
            blockIndexByKey.insert(groupKey, blockIndex);
            detail::PageTextBlock block;
            block.key = run.blockKey;
            block.role = run.role;
            grouped.append(block);
        }
        detail::PageTextBlock& block = grouped[blockIndex];
        block.runIndexes.append(runIndex);
        block.bounds = block.bounds.isNull() ? run.bounds : block.bounds.united(run.bounds);
        if (block.role == PageTextRole::Body && run.role != PageTextRole::Body) {
            block.role = run.role;
        }
    }
    // 块内的文字按页、从上往下拼（顺序要对，否则指纹会随页内次序乱跳）。
    for (detail::PageTextBlock& block : grouped) {
        QVector<int> ordered = block.runIndexes;
        std::sort(ordered.begin(), ordered.end(), [&runs](int left, int right) {
            const detail::PageTextRun& leftRun = runs.at(left);
            const detail::PageTextRun& rightRun = runs.at(right);
            if (leftRun.pageIndex != rightRun.pageIndex) return leftRun.pageIndex < rightRun.pageIndex;
            if (!qFuzzyCompare(leftRun.bounds.top() + 1.0, rightRun.bounds.top() + 1.0)) {
                return leftRun.bounds.top() > rightRun.bounds.top();
            }
            return leftRun.bounds.left() < rightRun.bounds.left();
        });
        QStringList pieces;
        for (const int runIndex : ordered) {
            const QString piece = runs.at(runIndex).text.trimmed();
            if (!piece.isEmpty()) pieces.append(piece);
        }
        block.text = pieces.join(QLatin1Char(' '));
        block.runIndexes = ordered;
    }

    // ---- 表格识别：把落在表格区域里的文字块收进表格 ----
    QVector<detail::PageTableGrid> grids;
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        QVector<detail::PageTextRun> pageRuns;
        QVector<int> pageRunIndexes;
        for (int runIndex = 0; runIndex < runs.size(); ++runIndex) {
            if (runs.at(runIndex).pageIndex != pageIndex) continue;
            pageRuns.append(runs.at(runIndex));
            pageRunIndexes.append(runIndex);
        }
        if (pageRuns.isEmpty()) continue;
        const QVector<detail::PageTableGrid> pageGrids =
            detail::detectPageTableGrids(pageRuns);
        for (detail::PageTableGrid grid : pageGrids) {
            QVector<int> globalIndexes;
            for (const int localIndex : grid.runIndexes) {
                globalIndexes.append(pageRunIndexes.at(localIndex));
            }
            grid.runIndexes = globalIndexes;
            grids.append(grid);
        }
    }

    // 判断一个文字块是不是落在某张表里。
    auto insideTable = [&grids](const QRectF& bounds) {
        for (const detail::PageTableGrid& grid : grids) {
            if (grid.bounds.isEmpty()) continue;
            const QRectF overlap = grid.bounds.intersected(bounds);
            if (overlap.isEmpty()) continue;
            const double area = bounds.width() * bounds.height();
            if (area <= 0.0) continue;
            if (overlap.width() * overlap.height() >= area * 0.6) return true;
        }
        return false;
    };

    // ---- 汇总成对象 ----
    QHash<QString, int> ordinalByKey;
    auto nextOrdinal = [&ordinalByKey](const QString& key) {
        const int seen = ordinalByKey.value(key, 0);
        ordinalByKey.insert(key, seen + 1);
        return seen;
    };

    QVector<PageObject> objects;
    // 表格先算（它的成员是单元格文字块，文字块不能再单独算一次）。
    for (const detail::PageTableGrid& grid : grids) {
        PageObject object;
        object.kind = PageObjectKind::Table;
        object.bounds = grid.bounds;
        object.rowCount = grid.rowCount;
        object.columnCount = grid.columnCount;
        object.cells = grid.cells;
        object.text = grid.cellTexts.join(QLatin1Char(' '));
        object.memberCount = grid.runIndexes.size();
        object.fingerprint = pageTextFingerprint(object.text);
        int pageIndex = -1;
        for (const int runIndex : grid.runIndexes) {
            pageIndex = qMax(pageIndex, runs.at(runIndex).pageIndex);
        }
        object.pageIndex = pageIndex;
        const QString ordinalKey = QStringLiteral("%1|%2")
                                       .arg(pageObjectKindKey(object.kind), object.fingerprint);
        object.id = derivePageObjectId(object.kind, object.fingerprint, object.bounds,
                                       nextOrdinal(ordinalKey));
        objects.append(object);
    }

    for (const detail::PageTextBlock& block : grouped) {
        if (insideTable(block.bounds)) continue;  // 已经算进表格里了
        PageObject object;
        object.kind = block.role == PageTextRole::Header ? PageObjectKind::Header
                      : block.role == PageTextRole::Footer ? PageObjectKind::Footer
                                                           : PageObjectKind::Text;
        object.bounds = block.bounds;
        object.text = block.text;
        object.fingerprint = pageTextFingerprint(block.text);
        object.memberCount = block.runIndexes.size();
        object.sourceBlockKey = block.key;
        object.pageIndex = block.runIndexes.isEmpty()
                               ? -1
                               : runs.at(block.runIndexes.first()).pageIndex;
        const QString ordinalKey = QStringLiteral("%1|%2")
                                       .arg(pageObjectKindKey(object.kind), object.fingerprint);
        object.id = derivePageObjectId(object.kind, object.fingerprint, object.bounds,
                                       nextOrdinal(ordinalKey));
        objects.append(object);
    }

    for (const NonTextObject& item : nonText) {
        PageObject object;
        object.kind = item.kind;
        object.pageIndex = item.pageIndex;
        object.bounds = item.bounds;
        object.fingerprint = item.fingerprint;
        object.naturalSize = item.naturalSize;
        object.sourceObjectIndex = item.objectIndex;
        const QString ordinalKey = QStringLiteral("%1|%2|%3")
                                       .arg(pageObjectKindKey(object.kind), item.fingerprint,
                                            pageGeometryFingerprint(item.bounds));
        object.id = derivePageObjectId(object.kind, item.fingerprint, item.bounds,
                                       nextOrdinal(ordinalKey));
        objects.append(object);
    }

    model.objects = objects;
    for (const PageObject& object : objects) {
        switch (object.kind) {
        case PageObjectKind::Text:
            ++model.counts.textBlocks;
            break;
        case PageObjectKind::Image:
            ++model.counts.images;
            break;
        case PageObjectKind::Vector:
            ++model.counts.vectors;
            break;
        case PageObjectKind::Table:
            ++model.counts.tables;
            break;
        case PageObjectKind::Header:
            ++model.counts.headers;
            break;
        case PageObjectKind::Footer:
            ++model.counts.footers;
            break;
        default:
            break;
        }
    }
    return model;
}

}  // namespace aipdf::pdf
