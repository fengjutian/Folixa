#include "pdf/compliance/PdfUaChecker.h"

#include "pdf/compliance/StructTreeModel.h"

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
    // 页序号 → MCID → 引用它的元素链（从根到直接引用它的那个元素）。
    //
    // 为什么记「元素链」而不是只记元素：/ParentTree 的条目按规范指向
    // 「包含这段标记内容的结构元素」——直接装 MCID 的那个元素算，
    // 它的祖先也算。记下整条链，两边都能对得上，不必猜。
    QHash<int, QHash<int, QVector<int>>> mcidChains;
    // 结构树里用到的非标准结构类型（RoleMap 也没把它映射到标准类型）。
    QStringList unmappedTypes;
    // 父子嵌套非法的地方（人可读的说明）。
    QStringList nestingViolations;
    // 有没有 /Part 元素（PDF/UA 的结构树要求 Document 下面有 Part）。
    int partCount = 0;
    // 有没有 /Document 元素。
    bool hasDocumentElement = false;
    // 结构树里用到的全部原始 /S 名字。
    QStringList usedTypeNames;
};

// 把一个结构类型名解析成标准类型名。
//
// 规则（PDF 32000-1 §14.7.4.4 的 /RoleMap）：
//   * 本身就是标准类型 → 原样返回；
//   * 是自定义类型 → 顺着 /RoleMap 映射下去，映射结果仍是标准类型才算数；
//   * 映射不存在的、映射到另一个自定义类型的、绕成环的 → 返回空串（不合规）。
QString resolveRoleName(const QString& name, const QHash<QString, QString>& roleMap, int depth) {
    if (name.isEmpty()) return QString();
    if (isStandardRoleName(name)) return name;
    if (depth > 8) return QString();  // RoleMap 里绕成环
    const auto found = roleMap.constFind(name);
    if (found == roleMap.constEnd()) return QString();
    return resolveRoleName(found.value(), roleMap, depth + 1);
}

// 读 /RoleMap：自定义结构类型名 → 它映射到的类型名。
QHash<QString, QString> readRoleMap(const QPDFObjectHandle& structTreeRoot) {
    QHash<QString, QString> result;
    const QPDFObjectHandle roleMap = structTreeRoot.getKey("/RoleMap");
    if (!roleMap.isDictionary()) return result;
    for (const std::string& key : roleMap.getKeys()) {
        const QPDFObjectHandle value = roleMap.getKey(key);
        if (!value.isName()) continue;
        QString name = QString::fromUtf8(key.c_str());
        if (name.startsWith(QLatin1Char('/'))) name = name.mid(1);
        result.insert(name, nameText(value.getName()));
    }
    return result;
}

// 把一棵数字树摊平成「键 → 值」。
//
// 键是页的 /StructParents，值是那一页的数组。既认顶层 /Nums，
// 也认 /Kids 分层的样子——摊平之后语义不变。
void collectNumberTree(const QPDFObjectHandle& node, int depth, QSet<int>* visited,
                        QHash<int, QPDFObjectHandle>* out) {
    if (!node.isDictionary() || depth > kMaxStructDepth) return;
    if (node.isIndirect()) {
        const int id = node.getObjectID();
        if (visited->contains(id)) return;
        visited->insert(id);
    }
    const QPDFObjectHandle nums = node.getKey("/Nums");
    if (nums.isArray()) {
        const std::vector<QPDFObjectHandle> items = nums.getArrayAsVector();
        for (size_t i = 0; i + 1 < items.size(); i += 2) {
            const QPDFObjectHandle key = items.at(i);
            const QPDFObjectHandle value = items.at(i + 1);
            if (!key.isInteger()) continue;
            if (value.isDictionary()) {
                collectNumberTree(value, depth + 1, visited, out);
                continue;
            }
            out->insert(static_cast<int>(key.getIntValue()), value);
        }
    }
    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            collectNumberTree(kid, depth + 1, visited, out);
        }
    }
}

// 递归走一个结构元素。
//
// 父子角色关系也在这里判：/S 的标准类型决定这个元素能装什么
// （例如 /TH 必须在 /TR 里面，/LI 必须在 /L 里面）。
void walkStructElement(const QPDFObjectHandle& element, int inheritedPage, StructRole parentRole,
                       QVector<int> chain, StructScan* scan,
                       const QHash<QString, QString>& roleMap,
                       std::set<std::pair<int, int>>* visited, int depth) {
    if (depth > kMaxStructDepth || !element.isDictionary()) return;
    // 坏文件里标签树可能成环：按对象号去重，走过就不再来一遍。
    if (element.isIndirect()) {
        const std::pair<int, int> key(element.getObjectID(), element.getGeneration());
        if (!visited->insert(key).second) return;
    }

    const QPDFObjectHandle type = element.getKey("/S");
    const QString rawType = type.isName() ? nameText(type.getName()) : QString();
    const QString resolvedType = resolveRoleName(rawType, roleMap, 0);
    const StructRole role = nameToRole(resolvedType);

    if (!rawType.isEmpty() && !scan->usedTypeNames.contains(rawType)) {
        scan->usedTypeNames.append(rawType);
    }
    // 非标准类型且 /RoleMap 也没把它映射到标准类型：读屏软件不知道这是什么。
    if (!rawType.isEmpty() && resolvedType.isEmpty()) {
        if (!scan->unmappedTypes.contains(rawType)) scan->unmappedTypes.append(rawType);
    }
    if (rawType == QLatin1String("Document")) scan->hasDocumentElement = true;
    if (rawType == QLatin1String("Part")) ++scan->partCount;

    const int elementId = element.isIndirect() ? element.getObjectID() : -1;
    chain.append(elementId);

    // 页归属：元素自己写了 /Pg 就用它，否则继承父元素的页。
    int page = inheritedPage;
    const QPDFObjectHandle pg = element.getKey("/Pg");
    if (pg.isDictionary() && pg.isIndirect()) {
        page = pg.getObjectID();
    }

    // 替代文本：图、公式、表头三种结构元素必须有 /Alt。
    //
    // 用「解析后的类型」判断：自定义类型经 /RoleMap 映射成 /Figure 时，
    // 同样要写 /Alt（PDF/UA-1 §7.3.6）。
    if (resolvedType == QLatin1String("Figure") || resolvedType == QLatin1String("Formula") ||
        resolvedType == QLatin1String("TH")) {
        const QPDFObjectHandle alt = element.getKey("/Alt");
        const QString altText =
            alt.isString() ? QString::fromUtf8(alt.getUTF8Value().c_str()).trimmed() : QString();
        if (altText.isEmpty()) {
            scan->missingAlt.append(
                QStringLiteral("%1（对象 %2 0 R）")
                    .arg(rawType)
                    .arg(element.isIndirect() ? element.getObjectID() : 0));
        }
    }

    // 父子嵌套：父元素的类型决定子元素能是什么。
    //
    // 只有父子两个类型都解析得出标准类型时才判：自定义类型（或 /RoleMap
    // 没映射好的类型）本身就是另一条错误，这里再报一次嵌套就重复了。
    if (parentRole != StructRole::Unknown && role != StructRole::Unknown &&
        !isLegalChildRole(parentRole, role)) {
        scan->nestingViolations.append(QStringLiteral("%1 里出现了 %2（文件里写的是 %3）")
                                            .arg(roleToName(parentRole), roleToName(role), rawType));
    }

    // 记录这个元素引用到的 MCID（连同引用它的元素链）。
    const auto recordMcid = [&scan, &chain](int targetPage, int mcid) {
        if (targetPage < 0 || mcid < 0) return;
        scan->referencedMcids[targetPage].insert(mcid);
        scan->mcidChains[targetPage][mcid] = chain;
    };

    const QPDFObjectHandle kids = element.getKey("/K");
    if (kids.isInteger()) {
        // /K 直接是整数：这个元素的内容就是该 MCID 标记的内容。
        recordMcid(page, static_cast<int>(kids.getIntValue()));
        return;
    }
    if (kids.isArray()) {
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            if (kid.isInteger()) {
                recordMcid(page, static_cast<int>(kid.getIntValue()));
            } else if (kid.isDictionary()) {
                const QPDFObjectHandle kidType = kid.getKey("/Type");
                if (kidType.isName() && kidType.getName() == "/MCR") {
                    // 标记内容引用：自带 /MCID，页可以自己指定。
                    const QPDFObjectHandle mcid = kid.getKey("/MCID");
                    const QPDFObjectHandle kidPage = kid.getKey("/Pg");
                    int target = page;
                    if (kidPage.isDictionary() && kidPage.isIndirect()) target = kidPage.getObjectID();
                    if (mcid.isInteger()) {
                        recordMcid(target, static_cast<int>(mcid.getIntValue()));
                    }
                } else if (kid.getKey("/S").isName()) {
                    walkStructElement(kid, page, role, chain, scan, roleMap, visited, depth + 1);
                }
            }
        }
        return;
    }
    if (kids.isDictionary() && kids.getKey("/S").isName()) {
        walkStructElement(kids, page, role, chain, scan, roleMap, visited, depth + 1);
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
        // /RoleMap：自定义结构类型 → 标准结构类型的映射表。
        QHash<QString, QString> roleMap;
        // ParentTree 摊平后的「页的 /StructParents 键 → 那一页的数组」。
        QHash<int, QPDFObjectHandle> parentTreeEntries;
        if (scan.hasStructTreeRoot) {
            scan.hasParentTree = structTreeRoot.getKey("/ParentTree").isDictionary() ||
                                 structTreeRoot.getKey("/ParentTree").isArray();
            roleMap = readRoleMap(structTreeRoot);
            if (scan.hasParentTree) {
                QSet<int> visitedNumbers;
                collectNumberTree(structTreeRoot.getKey("/ParentTree"), 0, &visitedNumbers,
                                  &parentTreeEntries);
            }
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
                        walkStructElement(item, -1, StructRole::Unknown, QVector<int>(), &scan,
                                          roleMap, &visited, 0);
                    }
                } else {
                    walkStructElement(rootElement, -1, StructRole::Unknown, QVector<int>(), &scan,
                                      roleMap, &visited, 0);
                }
            }
        }

        // 页码换算：标签树里存的是页对象号。
        QHash<int, QSet<int>> referencedByPage;
        // 页序号 → MCID → 引用它的元素链。
        QHash<int, QHash<int, QVector<int>>> chainsByPage;
        for (auto it = scan.referencedMcids.constBegin(); it != scan.referencedMcids.constEnd(); ++it) {
            const auto pageIt = pageIndexByObjectId.constFind(it.key());
            if (pageIt == pageIndexByObjectId.constEnd()) continue;
            referencedByPage.insert(pageIt.value(), it.value());
            const auto chainIt = scan.mcidChains.constFind(it.key());
            if (chainIt != scan.mcidChains.constEnd()) {
                chainsByPage.insert(pageIt.value(), chainIt.value());
            }
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
        // 每页的内容流里出现的 MCID（页序号 → MCID 集合）。
        QHash<int, QSet<int>> contentMcidsByPage;
        // 有内容却没有被打标的页。
        QStringList untaggedPages;
        // 页缺 /StructParents。
        QStringList pagesMissingStructParents;
        // /ParentTree 与 MCID 对不上的地方。
        QStringList parentTreeMismatches;
        // 页的 /StructParents 键（页序号 → 键）；-1 表示页上没有这个键。
        QVector<int> pageStructParentKey(static_cast<int>(pages.size()), -1);

        for (size_t i = 0; i < pages.size(); ++i) {
            const int pageIndex = static_cast<int>(i);
            const QSet<int> referenced = referencedByPage.value(pageIndex);
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
            contentMcidsByPage.insert(pageIndex, present);

            for (int mcid : referenced) {
                if (present.contains(mcid)) continue;
                missingMcids.append(QStringLiteral("第 %1 页的 MCID %2").arg(pageIndex + 1).arg(mcid));
            }

            // 这一页的 /StructParents：它是这一页在 /ParentTree 里的键。
            const QPDFObjectHandle structParents = pages.at(i).getKey("/StructParents");
            if (structParents.isInteger()) {
                pageStructParentKey[pageIndex] = static_cast<int>(structParents.getIntValue());
            }

            // 有内容却没有被打标：这页要么一个标记内容都没有，
            // 要么标记内容里的 MCID 没有一个被结构树引用（成了孤儿）。
            const bool hasPageContent = !content.trimmed().isEmpty();
            if (hasPageContent) {
                bool anyReferenced = false;
                for (int mcid : present) {
                    if (referenced.contains(mcid)) {
                        anyReferenced = true;
                        break;
                    }
                }
                if (!anyReferenced) {
                    untaggedPages.append(QStringLiteral("第 %1 页").arg(pageIndex + 1));
                }
            }

            // 有内容（或者结构树里有这一页的元素）却没有 /StructParents：
            // 读屏软件没法把这一页的内容接回结构树。
            if ((hasPageContent || !referenced.isEmpty()) && pageStructParentKey[pageIndex] < 0) {
                pagesMissingStructParents.append(QStringLiteral("第 %1 页").arg(pageIndex + 1));
            }

            // /ParentTree 与 MCID 的对应关系：结构树引用了哪个 MCID，
            // /ParentTree 里就必须有一条指向「装着这段标记内容的结构元素」的记录。
            if (referenced.isEmpty()) continue;
            const int key = pageStructParentKey[pageIndex];
            if (key < 0) continue;  // 缺 /StructParents 已经单独报过了
            const auto entry = parentTreeEntries.constFind(key);
            if (entry == parentTreeEntries.constEnd()) {
                parentTreeMismatches.append(
                    QStringLiteral("第 %1 页的 /ParentTree 里没有键 %2 的条目").arg(pageIndex + 1).arg(key));
                continue;
            }
            const QPDFObjectHandle array = entry.value();
            if (!array.isArray()) {
                parentTreeMismatches.append(
                    QStringLiteral("第 %1 页的 /ParentTree 条目不是数组").arg(pageIndex + 1));
                continue;
            }
            const std::vector<QPDFObjectHandle> items = array.getArrayAsVector();
            const QHash<int, QVector<int>>& chains = chainsByPage.value(pageIndex);
            for (int mcid : referenced) {
                if (mcid < 0 || mcid >= static_cast<int>(items.size())) {
                    parentTreeMismatches.append(
                        QStringLiteral("第 %1 页的 MCID %2 在 /ParentTree 里没有条目")
                            .arg(pageIndex + 1)
                            .arg(mcid));
                    continue;
                }
                const QPDFObjectHandle target = items.at(static_cast<size_t>(mcid));
                if (!target.isDictionary()) {
                    parentTreeMismatches.append(
                        QStringLiteral("第 %1 页的 MCID %2 在 /ParentTree 里没有指向结构元素")
                            .arg(pageIndex + 1)
                            .arg(mcid));
                    continue;
                }
                // 条目指向的元素必须真的装着这段标记内容。
                // 元素链里有直接对象时无法按对象号比对，这时只判「是个结构元素」。
                const QVector<int>& chain = chains.value(mcid);
                bool comparable = target.isIndirect() && !chain.isEmpty();
                for (int id : chain) {
                    if (id < 0) comparable = false;
                }
                if (!comparable) continue;
                const int targetId = target.getObjectID();
                if (!chain.contains(targetId)) {
                    parentTreeMismatches.append(
                        QStringLiteral("第 %1 页的 MCID %2 在 /ParentTree 里指向的元素里没有这段内容")
                            .arg(pageIndex + 1)
                            .arg(mcid));
                }
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
                PdfUaSeverity::Error);
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

        // 13. Part 元素：PDF/UA 的结构树要求 /Document 下面有 /Part。
        //
        // 只在确实有 /Document 元素时才判：根元素不是 /Document 的文件
        // 已经由第 2 条报了，不必在这里重复一遍。
        if (scan.hasDocumentElement) {
            if (scan.partCount > 0) {
                add(QStringLiteral("ua-part-element-present"), QStringLiteral("结构树里有 Part 元素"),
                    QStringLiteral("结构树里有 %1 个 /Part 元素，"
                                   "文档内容按「部分」分组，符合 PDF/UA 的结构要求。")
                        .arg(scan.partCount),
                    QString(), PdfUaSeverity::Pass);
            } else {
                add(QStringLiteral("ua-missing-part-element"), QStringLiteral("结构树里没有 Part 元素"),
                    QStringLiteral("结构树的 /Document 元素下面一个 /Part 都没有，"
                                   "PDF/UA 要求文档内容按 /Part 分组。"),
                    QStringLiteral("在 /Document 与各章节之间加一层 /Part 元素。"),
                    PdfUaSeverity::Error);
            }
        }

        // 14. /RoleMap：自定义结构类型必须映射到标准类型。
        if (scan.hasStructTreeRoot && !scan.unmappedTypes.isEmpty()) {
            if (roleMap.isEmpty()) {
                add(QStringLiteral("ua-missing-role-map"), QStringLiteral("用了自定义结构类型但没有 /RoleMap"),
                    QStringLiteral("结构树里用了这些非标准结构类型：%1，"
                                   "但 /StructTreeRoot 里没有 /RoleMap 映射表。")
                        .arg(scan.unmappedTypes.join(QStringLiteral("、"))),
                    QStringLiteral("在 /StructTreeRoot 里加 /RoleMap，把这些类型映射到标准类型。"),
                    PdfUaSeverity::Error);
            } else {
                add(QStringLiteral("ua-non-standard-structure-type"),
                    QStringLiteral("有结构类型没有映射到标准类型"),
                    QStringLiteral("这些结构类型不是标准类型，/RoleMap 也没有把它们映射到标准类型：%1。")
                        .arg(scan.unmappedTypes.join(QStringLiteral("、"))),
                    QStringLiteral("在 /RoleMap 里把这些类型映射到标准类型（例如 /P、/H1）。"),
                    PdfUaSeverity::Error);
            }
        } else if (scan.hasStructTreeRoot) {
            add(QStringLiteral("ua-role-map-ok"), QStringLiteral("结构类型都是标准类型"),
                QStringLiteral("结构树里用到的结构类型都是 PDF 里的标准类型，不需要 /RoleMap 映射。"),
                QString(), PdfUaSeverity::Pass);
        }

        // 15. 父子嵌套是否合法。
        if (scan.hasStructTreeRoot) {
            if (scan.nestingViolations.isEmpty()) {
                add(QStringLiteral("ua-structure-nesting-valid"), QStringLiteral("结构元素的嵌套合法"),
                    QStringLiteral("结构元素的父子关系符合 PDF 里的结构类型层级"
                                   "（例如 /TH 在 /TR 里、/LI 在 /L 里）。"),
                    QString(), PdfUaSeverity::Pass);
            } else {
                add(QStringLiteral("ua-illegal-structure-nesting"), QStringLiteral("结构元素的嵌套不合法"),
                    QStringLiteral("这些父子关系不符合 PDF 里的结构类型层级：%1。")
                        .arg(scan.nestingViolations.join(QStringLiteral("、"))),
                    QStringLiteral("按结构类型的层级重排这些元素（例如把 /TH 放进 /TR）。"),
                    PdfUaSeverity::Error);
            }
        }

        // 16. 页的 /StructParents。
        if (scan.hasStructTreeRoot && !pages.empty()) {
            if (pagesMissingStructParents.isEmpty()) {
                add(QStringLiteral("ua-page-struct-parents-present"), QStringLiteral("每页都写了 /StructParents"),
                    QStringLiteral("每个有内容的页都写了 /StructParents，"
                                   "读屏软件能按它在这一页的 /ParentTree 里找到对应关系。"),
                    QString(), PdfUaSeverity::Pass);
            } else {
                add(QStringLiteral("ua-page-missing-struct-parents"), QStringLiteral("有页面没写 /StructParents"),
                    QStringLiteral("这些页没有 /StructParents：%1。"
                                   "没有它，读屏软件找不到这一页在 /ParentTree 里的位置。")
                        .arg(pagesMissingStructParents.join(QStringLiteral("、"))),
                    QStringLiteral("给这些页加上 /StructParents，键要与 /ParentTree 里的键一致。"),
                    PdfUaSeverity::Error);
            }
        }

        // 17. /ParentTree 与 MCID 的对应关系。
        if (scan.hasStructTreeRoot && scan.hasParentTree && !referencedByPage.isEmpty()) {
            if (parentTreeMismatches.isEmpty()) {
                add(QStringLiteral("ua-parent-tree-aligned"), QStringLiteral("/ParentTree 与标记内容对得上"),
                    QStringLiteral("结构树引用到的每个 MCID，在 /ParentTree 里都有一条指向"
                                   "装着这段内容的结构元素的记录。"),
                    QString(), PdfUaSeverity::Pass);
            } else {
                add(QStringLiteral("ua-parent-tree-mismatch"), QStringLiteral("/ParentTree 与标记内容对不上"),
                    QStringLiteral("这些地方对不上：%1。").arg(parentTreeMismatches.join(QStringLiteral("、"))),
                    QStringLiteral("按结构树里引用的 MCID 重建 /ParentTree 的对应关系。"),
                    PdfUaSeverity::Error);
            }
        }

        // 18. 每页的内容都被打标。
        if (scan.hasStructTreeRoot && !pages.empty()) {
            if (untaggedPages.isEmpty()) {
                add(QStringLiteral("ua-page-content-tagged"), QStringLiteral("每页的内容都打了标签"),
                    QStringLiteral("每一页的内容都进了结构树，没有游离在标签结构之外的内容。"),
                    QString(), PdfUaSeverity::Pass);
            } else {
                add(QStringLiteral("ua-untagged-page-content"), QStringLiteral("有页面的内容没有打标签"),
                    QStringLiteral("这些页的内容没有进结构树：%1。"
                                   "读屏软件只能按几何位置猜它们怎么读。")
                        .arg(untaggedPages.join(QStringLiteral("、"))),
                    QStringLiteral("给这些页的内容补上标记内容与对应的结构元素。"),
                    PdfUaSeverity::Error);
            }
        }

        // 19. 表单域的 widget 有没有 /StructParent。
        //
        // PDF/UA-1 §7.18.4：widget 注解必须是 /Form 结构元素的后代。
        // 挂不上的话（没有 /StructParent），读屏软件在结构树里找不到这个域，
        // Tab 键也跳不过去。
        if (!pages.empty()) {
            QStringList widgetsWithoutParent;
            int widgetCount = 0;
            for (size_t i = 0; i < pages.size(); ++i) {
                const QPDFObjectHandle annots = pages.at(i).getKey("/Annots");
                if (!annots.isArray()) continue;
                for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
                    if (!annot.isDictionary()) continue;
                    const QPDFObjectHandle subtype = annot.getKey("/Subtype");
                    if (!subtype.isName() || subtype.getName() != "/Widget") continue;
                    ++widgetCount;
                    const QPDFObjectHandle structParent = annot.getKey("/StructParent");
                    if (structParent.isInteger()) continue;
                    widgetsWithoutParent.append(
                        QStringLiteral("第 %1 页的一个表单域").arg(static_cast<int>(i) + 1));
                }
            }
            if (widgetCount > 0) {
                if (widgetsWithoutParent.isEmpty()) {
                    add(QStringLiteral("ua-widget-struct-parent-present"),
                        QStringLiteral("表单域都挂进了标签结构"),
                        QStringLiteral("页面上的 %1 个表单域都有 /StructParent，"
                                       "读屏软件能在结构树里找到它们。")
                            .arg(widgetCount),
                        QString(), PdfUaSeverity::Pass);
                } else {
                    add(QStringLiteral("ua-widget-missing-struct-parent"),
                        QStringLiteral("有表单域没挂进标签结构"),
                        QStringLiteral("这些表单域没有 /StructParent：%1。"
                                       "读屏软件在标签结构里找不到它们。")
                            .arg(widgetsWithoutParent.join(QStringLiteral("、"))),
                        QStringLiteral("给这些 widget 注解补上 /StructParent，"
                                       "并在 /ParentTree 里放上对应的结构元素。"),
                        PdfUaSeverity::Error);
                }
            }
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
