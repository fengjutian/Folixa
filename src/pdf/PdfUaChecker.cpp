#include "pdf/PdfUaChecker.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>
#endif

namespace aipdf::pdf {

namespace {

// 深度上限：坏文件里的标签树可能有环或极深，递归必须封顶，
// 否则一次检查就能把界面线程卡死。
constexpr int kMaxStructDepth = 64;

// ------------------------------------------------------------ 工具函数 ----

// PDF 里的名字对象（/Lang、/S 之类）转成不带斜杠的文本。
QString nameText(const std::string& name) {
    QString text = QString::fromUtf8(name.c_str());
    if (text.startsWith(QLatin1Char('/'))) text = text.mid(1);
    return text;
}

// 语言标记（BCP 47）是否像样。
//
// 为什么自己判而不查表：BCP 47 的完整注册表有几万个条目，不可能内嵌；
// 而 PDF/UA 真正关心的是「主语言子标签 + 可选子标签」这个形状对不对。
// 形状不对的语言标记在阅读器里会被当成未知语言，读屏软件就退化成默认语音。
bool looksLikeLanguageTag(const QString& tag) {
    if (tag.isEmpty()) return false;
    const QStringList parts = tag.split(QLatin1Char('-'));
    if (parts.size() > 8) return false;
    const QString primary = parts.first();
    if (primary.size() < 2 || primary.size() > 3) return false;
    for (const QChar ch : primary) {
        if (!ch.isLetter()) return false;
    }
    for (int i = 1; i < parts.size(); ++i) {
        const QString sub = parts.at(i);
        if (sub.size() < 2 || sub.size() > 8) return false;
        for (const QChar ch : sub) {
            if (!ch.isLetterOrNumber()) return false;
        }
    }
    return true;
}

// ------------------------------------------------------------ qpdf 部分 ----

#ifdef AIPDF_WITH_QPDF

// 一个字体是否内嵌了字体程序。
//
// 为什么查三个键：/FontFile 是 Type1，/FontFile2 是 TrueType/OpenType，
// /FontFile3 是 CFF/Type1C——字体程序挂在哪个键里取决于字体类型。
bool fontIsEmbedded(const QPDFObjectHandle& font) {
    QVector<QPDFObjectHandle> descriptors;
    const QPDFObjectHandle subtype = font.getKey("/Subtype");
    if (subtype.isName() && subtype.getName() == "/Type0") {
        // 复合字体的字体程序挂在 /DescendantFonts 里的每一个子字体上。
        const QPDFObjectHandle descendants = font.getKey("/DescendantFonts");
        if (descendants.isArray()) {
            for (const QPDFObjectHandle& item : descendants.getArrayAsVector()) {
                descriptors.append(item.getKey("/FontDescriptor"));
            }
        }
    } else {
        descriptors.append(font.getKey("/FontDescriptor"));
    }
    for (const QPDFObjectHandle& descriptor : descriptors) {
        if (!descriptor.isDictionary()) continue;
        if (descriptor.getKey("/FontFile").isStream()) return true;
        if (descriptor.getKey("/FontFile2").isStream()) return true;
        if (descriptor.getKey("/FontFile3").isStream()) return true;
    }
    return false;
}

// 字体在资源字典里的名字（资源名），出错时返回空串。
QString resourceName(const std::string& key) {
    QString name = QString::fromUtf8(key.c_str());
    if (name.startsWith(QLatin1Char('/'))) name = name.mid(1);
    return name;
}

// 收集一页用到的字体（资源名 + 字体字典）。
void collectFonts(const QPDFObjectHandle& resources,
                  QVector<QPair<QString, QPDFObjectHandle>>* fonts) {
    if (!resources.isDictionary()) return;
    const QPDFObjectHandle fontDict = resources.getKey("/Font");
    if (!fontDict.isDictionary()) return;
    for (const std::string& key : fontDict.getKeys()) {
        fonts->append(qMakePair(resourceName(key), fontDict.getKey(key)));
    }
}

// 字体在字典里的基名（/BaseFont），出错时给个占位名。
QString baseFontName(const QPDFObjectHandle& font) {
    const QPDFObjectHandle base = font.getKey("/BaseFont");
    if (!base.isName()) return QStringLiteral("（未命名字体）");
    QString name = nameText(base.getName());
    // 子集前缀（"ABCDEF+"）只是文件里对字体的编号，不是字体名。
    if (name.size() > 7 && name.at(6) == QLatin1Char('+')) {
        bool subsetTag = true;
        for (int i = 0; i < 6; ++i) {
            const QChar ch = name.at(i);
            if (!((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
                  (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')))) {
                subsetTag = false;
                break;
            }
        }
        if (subsetTag) name = name.mid(7);
    }
    return name;
}

// 页面内容流里出现过的 MCID 集合。
//
// 为什么扫原文而不解析内容流：MCID 只出现在 marked content 的属性表里
// （`/P <</MCID 0>> BDC`），扫属性名后面的整数就够，
// 完整的内容流解析器在这里是过度投入。
QSet<int> scanContentMcids(const QByteArray& content) {
    QSet<int> result;
    int index = 0;
    while (true) {
        const int at = content.indexOf("/MCID", index);
        if (at < 0) break;
        int cursor = at + 5;
        while (cursor < content.size() &&
               (content.at(cursor) == ' ' || content.at(cursor) == '\n' ||
                content.at(cursor) == '\r' || content.at(cursor) == '\t')) {
            ++cursor;
        }
        bool hasDigits = false;
        int value = 0;
        while (cursor < content.size() && content.at(cursor) >= '0' && content.at(cursor) <= '9') {
            value = value * 10 + (content.at(cursor) - '0');
            ++cursor;
            hasDigits = true;
        }
        if (hasDigits) result.insert(value);
        index = at + 5;
    }
    return result;
}

// 标签树遍历的中间结果。
struct StructScan {
    bool hasStructTreeRoot = false;
    bool rootIsDocument = false;
    bool hasParentTree = false;
    // 缺替代文本的结构元素（人可读的说明）。
    QStringList missingAlt;
    // 页序号 → 结构树引用到的 MCID。
    QHash<int, QSet<int>> referencedMcids;
};

// 递归走一个结构元素。
void walkStructElement(const QPDFObjectHandle& element, int inheritedPage, StructScan* scan,
                       std::set<std::pair<int, int>>* visited, int depth) {
    if (depth > kMaxStructDepth || !element.isDictionary()) return;
    // 坏文件里标签树可能成环：按对象号去重，走过就不再来一遍。
    if (element.isIndirect()) {
        const std::pair<int, int> key(element.getObjectID(), element.getGeneration());
        if (!visited->insert(key).second) return;
    }

    const QPDFObjectHandle type = element.getKey("/S");
    const QString structureType = type.isName() ? nameText(type.getName()) : QString();

    // 页归属：元素自己写了 /Pg 就用它，否则继承父元素的页。
    int page = inheritedPage;
    const QPDFObjectHandle pg = element.getKey("/Pg");
    if (pg.isDictionary() && pg.isIndirect()) {
        page = pg.getObjectID();
    }

    // 替代文本：图、公式、表头三种结构元素必须有 /Alt。
    if (structureType == QLatin1String("Figure") || structureType == QLatin1String("Formula") ||
        structureType == QLatin1String("TH")) {
        const QPDFObjectHandle alt = element.getKey("/Alt");
        const QString altText =
            alt.isString() ? QString::fromUtf8(alt.getUTF8Value().c_str()).trimmed() : QString();
        if (altText.isEmpty()) {
            scan->missingAlt.append(
                QStringLiteral("%1（对象 %2 0 R）").arg(structureType).arg(element.getObjectID()));
        }
    }

    const QPDFObjectHandle kids = element.getKey("/K");
    if (kids.isInteger()) {
        // /K 直接是整数：这个元素的内容就是该 MCID 标记的内容。
        if (page >= 0) scan->referencedMcids[page].insert(static_cast<int>(kids.getIntValue()));
        return;
    }
    if (kids.isArray()) {
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            if (kid.isInteger()) {
                if (page >= 0) scan->referencedMcids[page].insert(static_cast<int>(kid.getIntValue()));
            } else if (kid.isDictionary()) {
                const QPDFObjectHandle kidType = kid.getKey("/Type");
                if (kidType.isName() && kidType.getName() == "/MCR") {
                    // 标记内容引用：自带 /MCID，页可以自己指定。
                    const QPDFObjectHandle mcid = kid.getKey("/MCID");
                    const QPDFObjectHandle kidPage = kid.getKey("/Pg");
                    int target = page;
                    if (kidPage.isDictionary() && kidPage.isIndirect()) target = kidPage.getObjectID();
                    if (mcid.isInteger() && target >= 0) {
                        scan->referencedMcids[target].insert(static_cast<int>(mcid.getIntValue()));
                    }
                } else if (kid.getKey("/S").isName()) {
                    walkStructElement(kid, page, scan, visited, depth + 1);
                }
            }
        }
        return;
    }
    if (kids.isDictionary() && kids.getKey("/S").isName()) {
        walkStructElement(kids, page, scan, visited, depth + 1);
    }
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

QString PdfUaReport::summary() const {
    return QStringLiteral("PDF/UA 合规检查：共 %1 项，通过 %2 项、警告 %3 项、错误 %4 项；结论：%5")
        .arg(findings.size())
        .arg(passCount)
        .arg(warningCount)
        .arg(errorCount)
        .arg(compliant ? QStringLiteral("合规") : QStringLiteral("不合规"));
}

PdfUaReport checkPdfUa(const QByteArray& pdfBytes, QString* error) {
    PdfUaReport report;

    // 失败也要给一份看得懂的报告：调用方只看 compliant 与 error 就够，
    // 但报告里保留一条 Error 结论，界面上不必另外拼文案。
    const auto fail = [&report, error](const QString& reason, const QString& code) {
        report = PdfUaReport();
        PdfUaFinding finding;
        finding.code = code;
        finding.title = QStringLiteral("无法检查这份 PDF");
        finding.detail = reason;
        finding.suggestion = QStringLiteral("换成一份能正常打开的 PDF 再试一次。");
        finding.severity = PdfUaSeverity::Error;
        report.findings.append(finding);
        report.errorCount = 1;
        report.warningCount = 0;
        report.passCount = 0;
        report.compliant = false;
        if (error != nullptr) *error = reason;
        return report;
    };

    if (pdfBytes.isEmpty()) {
        return fail(QStringLiteral("内容是空的，没有可检查的 PDF 字节"),
                    QStringLiteral("ua-empty-document"));
    }
    // 先看文件头：不是 PDF 就别让解析器去猜（解析器对带前导垃圾的 PDF 很宽容，
    // 会把它「修好」再返回，那不是我们要的检查结果）。
    const int headerLimit = static_cast<int>(qMin<qsizetype>(pdfBytes.size(), 1024));
    if (!pdfBytes.left(headerLimit).contains("%PDF-")) {
        return fail(QStringLiteral("这段内容不是 PDF（文件头里没有 %PDF-）"),
                    QStringLiteral("ua-not-a-pdf"));
    }

#ifndef AIPDF_WITH_QPDF
    return fail(QStringLiteral("本次构建没有启用 qpdf，做不了 PDF/UA 合规检查"),
                QStringLiteral("ua-qpdf-unavailable"));
#else
    try {
        QPDF pdf;
        // qpdf 的警告不往 stderr 上倒：这里要的是自己的结论，不是解析器的抱怨。
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("pdf-ua-check.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));

        const QPDFObjectHandle root = pdf.getRoot();
        if (!root.isDictionary()) {
            return fail(QStringLiteral("这份 PDF 读不出根目录（文件可能已损坏）"),
                        QStringLiteral("ua-unreadable-document"));
        }

        // 页对象号 → 页序号（标签树里的 /Pg 指的是页对象）。
        QHash<int, int> pageIndexByObjectId;
        // 必须是可写的副本：qpdf 的 getPageContents() 是非 const 方法。
        std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        for (size_t i = 0; i < pages.size(); ++i) {
            if (pages.at(i).isIndirect()) {
                pageIndexByObjectId.insert(pages.at(i).getObjectID(), static_cast<int>(i));
            }
        }

        // ---------------------------------------------------------- 结构 ----
        const QPDFObjectHandle structTreeRoot = root.getKey("/StructTreeRoot");
        StructScan scan;
        scan.hasStructTreeRoot = structTreeRoot.isDictionary();
        if (scan.hasStructTreeRoot) {
            scan.hasParentTree = structTreeRoot.getKey("/ParentTree").isDictionary() ||
                                 structTreeRoot.getKey("/ParentTree").isArray();
            // 结构树的根元素必须是 /Document。
            QPDFObjectHandle rootElement = structTreeRoot.getKey("/K");
            if (rootElement.isArray()) {
                const std::vector<QPDFObjectHandle> items = rootElement.getArrayAsVector();
                if (!items.empty()) rootElement = items.front();
            }
            if (rootElement.isDictionary()) {
                const QPDFObjectHandle type = rootElement.getKey("/S");
                scan.rootIsDocument = type.isName() && type.getName() == "/Document";
                std::set<std::pair<int, int>> visited;
                if (rootElement.isArray()) {
                    for (const QPDFObjectHandle& item : rootElement.getArrayAsVector()) {
                        walkStructElement(item, -1, &scan, &visited, 0);
                    }
                } else {
                    walkStructElement(rootElement, -1, &scan, &visited, 0);
                }
            }
        }

        // 页码换算：标签树里存的是页对象号。
        QHash<int, QSet<int>> referencedByPage;
        for (auto it = scan.referencedMcids.constBegin(); it != scan.referencedMcids.constEnd(); ++it) {
            const auto pageIt = pageIndexByObjectId.constFind(it.key());
            if (pageIt == pageIndexByObjectId.constEnd()) continue;
            referencedByPage.insert(pageIt.value(), it.value());
        }

        // ---------------------------------------------------------- 语言 ----
        const QPDFObjectHandle lang = root.getKey("/Lang");
        const bool hasLang = lang.isString();
        const QString langText = hasLang ? QString::fromUtf8(lang.getUTF8Value().c_str()).trimmed() : QString();

        // ---------------------------------------------------------- 标题 ----
        const QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
        const QPDFObjectHandle title = info.isDictionary() ? info.getKey("/Title") : QPDFObjectHandle();
        const bool hasTitle = title.isString() &&
                              !QString::fromUtf8(title.getUTF8Value().c_str()).trimmed().isEmpty();
        const QPDFObjectHandle displayDocTitle = root.getKey("/DisplayDocTitle");
        const bool displaysDocTitle =
            displayDocTitle.isBool() && displayDocTitle.getBoolValue();

        // -------------------------------------------------------- 元数据 ----
        const QPDFObjectHandle metadata = root.getKey("/Metadata");
        QByteArray metadataBytes;
        if (metadata.isStream()) {
            // 拷一份可写的：qpdf 的 getStreamData() 是非 const 方法。
            QPDFObjectHandle stream = metadata;
            std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
            if (data) {
                metadataBytes = QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                           static_cast<int>(data->getSize()));
            }
        }
        const bool hasMetadata = !metadataBytes.isEmpty();
        const bool metadataDeclaresUa = metadataBytes.contains("pdfuaid");

        // ---------------------------------------------------------- 字体 ----
        QStringList notEmbeddedFonts;
        QSet<int> reportedFonts;
        for (size_t i = 0; i < pages.size(); ++i) {
            const QPDFObjectHandle resources =
                QPDFPageObjectHelper(pages.at(i)).getAttribute("/Resources", false);
            QVector<QPair<QString, QPDFObjectHandle>> fonts;
            collectFonts(resources, &fonts);
            for (const auto& font : fonts) {
                // fonts 里存的是「资源名 → 字体字典」。
                const QPDFObjectHandle& object = font.second;
                const int objectId = object.isIndirect() ? object.getObjectID() : -1;
                if (objectId >= 0 && reportedFonts.contains(objectId)) continue;
                if (objectId >= 0) reportedFonts.insert(objectId);
                if (fontIsEmbedded(object)) continue;
                notEmbeddedFonts.append(
                    QStringLiteral("%1（资源名 /%2，第 %3 页）")
                        .arg(baseFontName(object))
                        .arg(font.first)
                        .arg(static_cast<int>(i) + 1));
            }
        }

        // ------------------------------------------------------ Tab 顺序 ----
        bool hasReadingOrderTab = false;
        bool hasMissingTab = false;
        for (size_t i = 0; i < pages.size(); ++i) {
            const QPDFObjectHandle tabs = pages.at(i).getKey("/Tabs");
            if (!tabs.isName()) {
                hasMissingTab = true;
                continue;
            }
            if (tabs.getName() == "/R") hasReadingOrderTab = true;
        }

        // ---------------------------------------------------------- 内容 ----
        // 结构树引用的 MCID 必须能在对应页面的内容流里找到标记内容。
        QStringList missingMcids;
        for (size_t i = 0; i < pages.size(); ++i) {
            const int pageIndex = static_cast<int>(i);
            const QSet<int> referenced = referencedByPage.value(pageIndex);
            if (referenced.isEmpty()) continue;
            QByteArray content;
            for (QPDFObjectHandle& stream : pages.at(i).getPageContents()) {
                if (!stream.isStream()) continue;
                std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
                if (!data) continue;
                content.append(reinterpret_cast<const char*>(data->getBuffer()),
                               static_cast<int>(data->getSize()));
                content.append('\n');
            }
            const QSet<int> present = scanContentMcids(content);
            for (int mcid : referenced) {
                if (present.contains(mcid)) continue;
                missingMcids.append(QStringLiteral("第 %1 页的 MCID %2").arg(pageIndex + 1).arg(mcid));
            }
        }

        // ------------------------------------------------------ 生成结论 ----
        const auto add = [&report](const QString& code, const QString& title, const QString& detail,
                                   const QString& suggestion, PdfUaSeverity severity) {
            PdfUaFinding finding;
            finding.code = code;
            finding.title = title;
            finding.detail = detail;
            finding.suggestion = suggestion;
            finding.severity = severity;
            report.findings.append(finding);
        };

        // 1. 结构树根。
        if (scan.hasStructTreeRoot) {
            add(QStringLiteral("ua-struct-tree-root-present"), QStringLiteral("有标签结构树"),
                QStringLiteral("文档目录里有 /StructTreeRoot，文档已被标记为带标签的 PDF。"),
                QString(), PdfUaSeverity::Pass);
        } else {
            add(QStringLiteral("ua-missing-struct-tree-root"), QStringLiteral("没有标签结构树"),
                QStringLiteral("文档目录里没有 /StructTreeRoot，这份 PDF 完全没有标签结构，"
                               "读屏软件只能按几何位置猜阅读顺序。"),
                QStringLiteral("用带标签的 PDF，或给文档补一棵 /StructTreeRoot 标签树。"),
                PdfUaSeverity::Error);
        }

        // 2. 结构树根元素类型。
        if (scan.hasStructTreeRoot) {
            if (scan.rootIsDocument) {
                add(QStringLiteral("ua-struct-root-document"), QStringLiteral("结构树根是 Document"),
                    QStringLiteral("/StructTreeRoot 的根元素类型是 /Document，符合 PDF/UA 的要求。"),
                    QString(), PdfUaSeverity::Pass);
            } else {
                add(QStringLiteral("ua-struct-root-not-document"), QStringLiteral("结构树根不是 Document"),
                    QStringLiteral("/StructTreeRoot 的根元素类型不是 /Document，"
                                   "PDF/UA 要求整棵标签树只有一个 /Document 根。"),
                    QStringLiteral("把结构树的根元素改成 /Document，其下再挂各章节。"),
                    PdfUaSeverity::Error);
            }
        }

        // 3. ParentTree。
        if (scan.hasStructTreeRoot) {
            if (scan.hasParentTree) {
                add(QStringLiteral("ua-parent-tree-present"), QStringLiteral("有 /ParentTree"),
                    QStringLiteral("/StructTreeRoot 里有 /ParentTree，结构元素与页面内容的对应关系有据可查。"),
                    QString(), PdfUaSeverity::Pass);
            } else {
                add(QStringLiteral("ua-missing-parent-tree"), QStringLiteral("没有 /ParentTree"),
                    QStringLiteral("/StructTreeRoot 里没有 /ParentTree，"
                                   "读屏软件无法把结构元素对应回页面上的内容。"),
                    QStringLiteral("给 /StructTreeRoot 补一棵 /ParentTree 数字树。"),
                    PdfUaSeverity::Error);
            }
        }

        // 4. MarkInfo。
        const QPDFObjectHandle markInfo = root.getKey("/MarkInfo");
        const QPDFObjectHandle marked = markInfo.isDictionary() ? markInfo.getKey("/Marked") : QPDFObjectHandle();
        if (marked.isBool() && marked.getBoolValue()) {
            add(QStringLiteral("ua-mark-info-marked"), QStringLiteral("文档已标记为带标签"),
                QStringLiteral("文档目录里有 /MarkInfo <</Marked true>>，阅读器知道要按标签结构读。"),
                QString(), PdfUaSeverity::Pass);
        } else if (!markInfo.isDictionary()) {
            add(QStringLiteral("ua-missing-mark-info"), QStringLiteral("没有 /MarkInfo"),
                QStringLiteral("文档目录里没有 /MarkInfo，阅读器不会按标签结构来读这份文档。"),
                QStringLiteral("给文档目录加 /MarkInfo <</Marked true>>。"),
                PdfUaSeverity::Error);
        } else {
            add(QStringLiteral("ua-marked-not-true"), QStringLiteral("/Marked 不是 true"),
                QStringLiteral("文档目录里有 /MarkInfo，但 /Marked 的值不是 true。"),
                QStringLiteral("把 /Marked 改成 true。"),
                PdfUaSeverity::Error);
        }

        // 5. 语言。
        if (hasLang && looksLikeLanguageTag(langText)) {
            add(QStringLiteral("ua-lang-valid"), QStringLiteral("语言标记有效"),
                QStringLiteral("文档语言是 %1，读屏软件会按这个语言选语音。").arg(langText),
                QString(), PdfUaSeverity::Pass);
        } else if (!hasLang) {
            add(QStringLiteral("ua-missing-lang"), QStringLiteral("没有声明文档语言"),
                QStringLiteral("文档目录里没有 /Lang，读屏软件只能猜语言，"
                               "中文文档可能被英文语音念出来。"),
                QStringLiteral("给文档目录加 /Lang，例如 /Lang (zh-CN)。"),
                PdfUaSeverity::Error);
        } else {
            add(QStringLiteral("ua-invalid-lang"), QStringLiteral("语言标记不像 BCP 47"),
                QStringLiteral("文档语言是「%1」，不像 BCP 47 语言标记（形如 zh-CN、en）。").arg(langText),
                QStringLiteral("把 /Lang 改成规范的语言标记，例如 (zh-CN) 或 (en)。"),
                PdfUaSeverity::Error);
        }

        // 6. 标题。
        if (hasTitle) {
            add(QStringLiteral("ua-title-present"), QStringLiteral("有文档标题"),
                QStringLiteral("文档信息字典里有 /Title。"),
                QString(), PdfUaSeverity::Pass);
        } else {
            add(QStringLiteral("ua-missing-title"), QStringLiteral("没有文档标题"),
                QStringLiteral("文档信息字典里没有 /Title（或标题是空的），"
                               "读屏软件报不出这份文档叫什么。"),
                QStringLiteral("给文档信息字典加 /Title。"),
                PdfUaSeverity::Error);
        }

        // 7. 显示文档标题。
        if (displaysDocTitle) {
            add(QStringLiteral("ua-display-doc-title-ok"), QStringLiteral("阅读器会显示文档标题"),
                QStringLiteral("文档目录里有 /DisplayDocTitle true，阅读器窗口显示的是标题而不是文件名。"),
                QString(), PdfUaSeverity::Pass);
        } else {
            add(QStringLiteral("ua-missing-display-doc-title"), QStringLiteral("阅读器不会显示文档标题"),
                QStringLiteral("文档目录里没有 /DisplayDocTitle true，"
                               "阅读器会用文件名当标题，文件名往往没有信息量。"),
                QStringLiteral("给文档目录加 /DisplayDocTitle true。"),
                PdfUaSeverity::Error);
        }

        // 8. 替代文本。
        if (scan.hasStructTreeRoot) {
            if (scan.missingAlt.isEmpty()) {
                add(QStringLiteral("ua-alt-text-ok"), QStringLiteral("图与公式都有替代文本"),
                    QStringLiteral("结构树里的 /Figure、/Formula、/TH 元素都写了 /Alt。"),
                    QString(), PdfUaSeverity::Pass);
            } else {
                add(QStringLiteral("ua-missing-alt-text"), QStringLiteral("有结构元素缺替代文本"),
                    QStringLiteral("这些结构元素没有 /Alt：%1。").arg(scan.missingAlt.join(QStringLiteral("、"))),
                    QStringLiteral("给这些元素补 /Alt，用一句话说明图或公式讲的是什么。"),
                    PdfUaSeverity::Error);
            }
        }

        // 9. 字体嵌入。
        if (notEmbeddedFonts.isEmpty()) {
            add(QStringLiteral("ua-fonts-embedded"), QStringLiteral("字体都已内嵌"),
                QStringLiteral("页面用到的字体都带了字体程序，换一台机器打开版面不会走样。"),
                QString(), PdfUaSeverity::Pass);
        } else {
            add(QStringLiteral("ua-font-not-embedded"), QStringLiteral("有字体没有内嵌"),
                QStringLiteral("这些字体没有内嵌字体程序：%1。").arg(notEmbeddedFonts.join(QStringLiteral("、"))),
                QStringLiteral("导出 PDF 时勾选「嵌入字体」，或把字体转成轮廓。"),
                PdfUaSeverity::Error);
        }

        // 10. Tab 顺序。
        if (hasReadingOrderTab) {
            add(QStringLiteral("ua-tabs-reading-order"), QStringLiteral("有页面用阅读顺序做 Tab 顺序"),
                QStringLiteral("有页面的 /Tabs 是 /R（按行阅读顺序），"
                               "读屏软件按 Tab 走会跳开标签结构规定的顺序。"),
                QStringLiteral("把相关页面的 /Tabs 改成 /S（按标签结构顺序）。"),
                PdfUaSeverity::Error);
        } else if (hasMissingTab) {
            add(QStringLiteral("ua-missing-tabs-order"), QStringLiteral("有页面没声明 Tab 顺序"),
                QStringLiteral("有页面没写 /Tabs，按规范默认是 /R（阅读顺序），"
                               "与标签结构的顺序可能不一致。"),
                QStringLiteral("给这些页面的 /Tabs 写 /S。"),
                PdfUaSeverity::Warning);
        } else {
            add(QStringLiteral("ua-tabs-structure-order"), QStringLiteral("Tab 顺序按标签结构"),
                QStringLiteral("所有页面的 /Tabs 都是 /S，Tab 顺序与标签结构一致。"),
                QString(), PdfUaSeverity::Pass);
        }

        // 11. MCID 与内容流对得上。
        if (!referencedByPage.isEmpty()) {
            if (missingMcids.isEmpty()) {
                add(QStringLiteral("ua-mcid-content-ok"), QStringLiteral("标记内容与结构树对得上"),
                    QStringLiteral("结构树里引用的 MCID 都能在对应页面的内容流里找到。"),
                    QString(), PdfUaSeverity::Pass);
            } else {
                add(QStringLiteral("ua-mcid-missing-in-content"), QStringLiteral("结构树引用了不存在的内容"),
                    QStringLiteral("这些 MCID 在对应页面的内容流里找不到：%1。")
                        .arg(missingMcids.join(QStringLiteral("、"))),
                    QStringLiteral("重新生成标签树，或删掉指向不存在内容的结构元素。"),
                    PdfUaSeverity::Error);
            }
        }

        // 12. XMP 元数据与 PDF/UA 标识。
        if (!hasMetadata) {
            add(QStringLiteral("ua-missing-xmp-metadata"), QStringLiteral("没有 XMP 元数据"),
                QStringLiteral("文档目录里没有 /Metadata，缺少 PDF/UA 要求的 XMP 元数据。"),
                QStringLiteral("给文档加上 XMP 元数据流。"),
                PdfUaSeverity::Error);
        } else if (!metadataDeclaresUa) {
            add(QStringLiteral("ua-xmp-missing-pdfuaid"), QStringLiteral("XMP 里没有声明 PDF/UA"),
                QStringLiteral("XMP 元数据里没有 pdfuaid 标识，没有声明这份文档按哪一版 PDF/UA 制作。"),
                QStringLiteral("在 XMP 里加上 pdfuaid:part 与 pdfuaid:amd。"),
                PdfUaSeverity::Warning);
        } else {
            add(QStringLiteral("ua-xmp-metadata-ok"), QStringLiteral("XMP 元数据里有 PDF/UA 标识"),
                QStringLiteral("XMP 元数据里写了 pdfuaid，声明了 PDF/UA 一致性。"),
                QString(), PdfUaSeverity::Pass);
        }

        // 计数统一在最后按结论的实际级别重算一遍：
        // 边加结论边加计数很容易漏一处，重算则永远与结论一致。
        report.errorCount = 0;
        report.warningCount = 0;
        report.passCount = 0;
        for (const PdfUaFinding& finding : report.findings) {
            if (finding.severity == PdfUaSeverity::Error) ++report.errorCount;
            else if (finding.severity == PdfUaSeverity::Warning) ++report.warningCount;
            else ++report.passCount;
        }
        report.compliant = report.errorCount == 0;
        if (error != nullptr) error->clear();
        return report;
    } catch (const std::exception& exception) {
        return fail(QStringLiteral("解析这份 PDF 失败：%1")
                        .arg(QString::fromUtf8(exception.what())),
                    QStringLiteral("ua-unreadable-document"));
    }
#endif
}

}  // namespace aipdf::pdf
