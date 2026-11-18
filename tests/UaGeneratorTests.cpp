#include "pdf/compliance/PdfUaChecker.h"
#include "pdf/compliance/StructTreeModel.h"
#include "pdf/compliance/UaGenerator.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

#include <functional>
#include <memory>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

using aipdf::pdf::checkPdfUa;
using aipdf::pdf::PdfUaFinding;
using aipdf::pdf::PdfUaReport;
using aipdf::pdf::PdfUaSeverity;
using aipdf::pdf::UaGeneratorOptions;
using aipdf::pdf::UaGeneratorResult;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 把对象列表拼成一份带正确 xref 的 PDF。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects, int rootId) {
    QByteArray out("%PDF-1.7\n");
    QHash<int, int> offsets;
    int maxId = 0;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        maxId = qMax(maxId, object.first);
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (offsets.contains(id)) {
            out += QStringLiteral("%1 00000 n \n")
                       .arg(offsets.value(id), 10, 10, QLatin1Char('0'))
                       .toLatin1();
        } else {
            out += QByteArray("0000000000 65535 f \n");
        }
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root %2 0 R >>\nstartxref\n%3\n%%EOF\n")
               .arg(count)
               .arg(rootId)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 一个流对象。
QByteArray streamObject(const QByteArray& data) {
    QByteArray out = "<< ";
    out += "/Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
    return out;
}

// 内嵌字体的字体资源（PDF/UA 要求字体都内嵌：换一台机器打开版式不走样）。
//
// 为什么夹具里要有它：生成器**不**负责内嵌字体（那是另一件事），
// 源文档字体没内嵌的话，产物的检查器会报「字体没有内嵌」——
// 那是源文档的问题，不是一键生成的锅。
QVector<QPair<int, QByteArray>> appendFont(QVector<QPair<int, QByteArray>> objects, int fontId) {
    objects.append(qMakePair(fontId, QStringLiteral("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                                    "/FontDescriptor %1 0 R >>")
                                 .arg(fontId + 1)
                                 .toLatin1()));
    objects.append(qMakePair(fontId + 1, QStringLiteral("<< /Type /FontDescriptor /FontName /Helvetica "
                                                        "/Flags 32 /FontFile %1 0 R >>")
                                     .arg(fontId + 2)
                                     .toLatin1()));
    objects.append(qMakePair(fontId + 2, streamObject(QByteArray(64, '\0'))));
    return objects;
}

// 源文档：标题 + 两行正文 + 两个列表项 + 一张图（带说明文字）。
QByteArray buildTextPdf() {
    const QByteArray content(
        "BT /F1 20 Tf 72 700 Td (Report Title) Tj ET\n"
        "BT /F1 12 Tf 72 670 Td (Body paragraph line one.) Tj ET\n"
        "BT /F1 12 Tf 72 655 Td (Body paragraph line two.) Tj ET\n"
        "BT /F1 12 Tf 72 640 Td (- first item) Tj ET\n"
        "BT /F1 12 Tf 72 625 Td (- second item) Tj ET\n"
        "q 200 0 0 100 72 400 cm /Im1 Do Q\n"
        "BT /F1 10 Tf 72 380 Td (Figure 1 Company logo) Tj ET\n");

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                                           "/Resources << /Font << /F1 5 0 R >> "
                                           "/XObject << /Im1 8 0 R >> >> /Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject(content)));
    objects = appendFont(objects, 5);
    // 1×1 的灰度图像：1 个字节就是 1 个像素，/Length 必须与实际字节数一致，
    // 否则 qpdf 读流时会报「expected endstream」并自己猜长度（猜出来的流是坏的）。
    const QByteArray imageData(1, '\0');
    const QByteArray image =
        "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray "
        "/BitsPerComponent 8 /Length " + QByteArray::number(imageData.size()) + " >>\nstream\n" +
        imageData + "\nendstream";
    objects.append(qMakePair(8, image));
    return buildPdf(objects, 1);
}

// 源文档：标题 + 两个表单域。
QByteArray buildFormPdf() {
    const QByteArray content("BT /F1 20 Tf 72 700 Td (Report Title) Tj ET\n");
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R "
                                           "/AcroForm << /Fields [8 0 R 9 0 R] "
                                           "/DA (/Helv 0 Tf 0 g) >> >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                                           "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R "
                                           "/Annots [8 0 R 9 0 R] >>")));
    objects.append(qMakePair(4, streamObject(content)));
    objects = appendFont(objects, 5);
    objects.append(qMakePair(8, QByteArray("<< /Type /Annot /Subtype /Widget /FT /Tx /T (firstName) "
                                           "/TU (First name) /Rect [72 500 200 520] /P 3 0 R "
                                           "/DA (/Helv 0 Tf 0 g) >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /Annot /Subtype /Widget /FT /Tx /T (lastName) "
                                           "/TU (Last name) /Rect [72 470 200 490] /P 3 0 R "
                                           "/DA (/Helv 0 Tf 0 g) >>")));
    return buildPdf(objects, 1);
}

// 报告里有没有某一条结论。
const PdfUaFinding* findFinding(const PdfUaReport& report, const QString& code) {
    for (const PdfUaFinding& finding : report.findings) {
        if (finding.code == code) return &finding;
    }
    return nullptr;
}

#ifdef AIPDF_WITH_QPDF

// ------------------------------------------------------------ 独立复算 ----
//
// 这一段**不复用生成器的任何数据结构**：自己用 qpdf 把产物重新读一遍，
// 自己拼出结构树、/ParentTree 与 MCID 的对应关系。
// 生成器与检查器同源，「生成器的产物过了检查器」是同义反复——
// 只有绕开它们自己算一遍，才知道产物到底对不对。

// 独立读出来的一个结构元素。
struct RawElement {
    QString type;
    QString title;
    QString alt;
    QVector<int> mcids;
    QVector<int> children;
    int parent = -1;
    int pageIndex = -1;
    // /K 里的 widget 注解（对象号）。
    QVector<int> widgetObjectIds;
};

// 独立读出来的结构树。
struct RawTree {
    QVector<RawElement> elements;
    int rootIndex = -1;
    // 页序号 → 页的 /StructParents。
    QVector<int> pageStructParents;
    // /ParentTree：键 → 该页的数组（条目是元素下标，-1 表示空条目）。
    QHash<int, QVector<int>> parentTree;
    // 页序号 → 内容流里出现过的 MCID。
    QHash<int, QSet<int>> contentMcids;
    // 页序号 → 该页 /Annots 里的 widget（对象号 → /StructParent）。
    QHash<int, int> widgetStructParents;
};

// 数字树摊平。
void collectNumbers(const QPDFObjectHandle& node, QHash<int, QPDFObjectHandle>* out) {
    if (!node.isDictionary()) return;
    const QPDFObjectHandle nums = node.getKey("/Nums");
    if (nums.isArray()) {
        const std::vector<QPDFObjectHandle> items = nums.getArrayAsVector();
        for (size_t i = 0; i + 1 < items.size(); i += 2) {
            if (!items.at(i).isInteger()) continue;
            out->insert(static_cast<int>(items.at(i).getIntValue()), items.at(i + 1));
        }
    }
    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            collectNumbers(kid, out);
        }
    }
}

// 内容流里出现的 MCID。
QSet<int> mcidsInContent(const QByteArray& content) {
    QSet<int> result;
    static const QRegularExpression pattern(QStringLiteral("/MCID\\s+(\\d+)"));
    QRegularExpressionMatchIterator iterator = pattern.globalMatch(QString::fromLatin1(content));
    while (iterator.hasNext()) {
        result.insert(iterator.next().captured(1).toInt());
    }
    return result;
}

// 页的内容流（拼起来）。
QByteArray pageContentOf(const std::vector<QPDFObjectHandle>& pages, int pageIndex) {
    QByteArray content;
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return content;
    // 注意：getPageContents() 是非 const 方法，所以这里先拿一份可改的页句柄副本。
    QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));
    std::vector<QPDFObjectHandle> contents = page.getPageContents();
    for (QPDFObjectHandle& stream : contents) {
        if (!stream.isStream()) continue;
        std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (!data) continue;
        content.append(reinterpret_cast<const char*>(data->getBuffer()),
                       static_cast<int>(data->getSize()));
        content.append('\n');
    }
    return content;
}

// 独立读产物。
RawTree readTreeIndependently(const QByteArray& bytes) {
    RawTree tree;
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("independent.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    const QPDFObjectHandle catalog = pdf.getRoot();
    const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();

    // 页的 /StructParents 与 /Annots。
    for (size_t i = 0; i < pages.size(); ++i) {
        const QPDFObjectHandle page = pages.at(i);
        int key = -1;
        if (page.getKey("/StructParents").isInteger()) {
            key = static_cast<int>(page.getKey("/StructParents").getIntValue());
        }
        tree.pageStructParents.append(key);
        const QPDFObjectHandle annots = page.getKey("/Annots");
        if (annots.isArray()) {
            for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
                if (!annot.isDictionary() || !annot.isIndirect()) continue;
                const QPDFObjectHandle subtype = annot.getKey("/Subtype");
                if (!subtype.isName() || subtype.getName() != "/Widget") continue;
                int structParent = -1;
                if (annot.getKey("/StructParent").isInteger()) {
                    structParent = static_cast<int>(annot.getKey("/StructParent").getIntValue());
                }
                tree.widgetStructParents.insert(annot.getObjectID(), structParent);
            }
        }
        tree.contentMcids.insert(static_cast<int>(i), mcidsInContent(pageContentOf(pages, static_cast<int>(i))));
    }

    const QPDFObjectHandle root = catalog.getKey("/StructTreeRoot");
    if (!root.isDictionary()) return tree;

    QHash<int, QPDFObjectHandle> numbers;
    collectNumbers(root.getKey("/ParentTree"), &numbers);
    for (auto it = numbers.constBegin(); it != numbers.constEnd(); ++it) {
        QVector<int> entries;
        if (it.value().isArray()) {
            for (const QPDFObjectHandle& entry : it.value().getArrayAsVector()) {
                entries.append(entry.isIndirect() ? entry.getObjectID() : -1);
            }
        }
        tree.parentTree.insert(it.key(), entries);
    }

    // 结构树：按先序展开。
    std::function<void(const QPDFObjectHandle&, int, int, int)> walk =
        [&](const QPDFObjectHandle& element, int parent, int page, int depth) {
            if (depth > 64) return;
            RawElement record;
            record.parent = parent;
            record.pageIndex = page;
            const QPDFObjectHandle type = element.getKey("/S");
            record.type = type.isName()
                              ? QString::fromStdString(type.getName()).mid(1)
                              : QString();
            const QPDFObjectHandle title = element.getKey("/T");
            if (title.isString()) record.title = QString::fromUtf8(title.getUTF8Value().c_str());
            const QPDFObjectHandle alt = element.getKey("/Alt");
            if (alt.isString()) record.alt = QString::fromUtf8(alt.getUTF8Value().c_str());
            const QPDFObjectHandle pg = element.getKey("/Pg");
            if (pg.isIndirect()) {
                for (size_t i = 0; i < pages.size(); ++i) {
                    if (pages.at(i).isIndirect() && pages.at(i).getObjectID() == pg.getObjectID()) {
                        record.pageIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
            const QPDFObjectHandle kids = element.getKey("/K");
            std::vector<QPDFObjectHandle> kidList;
            if (kids.isInteger()) {
                record.mcids.append(static_cast<int>(kids.getIntValue()));
            } else if (kids.isArray()) {
                for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
                    if (kid.isInteger()) {
                        record.mcids.append(static_cast<int>(kid.getIntValue()));
                    } else if (kid.isDictionary()) {
                        kidList.push_back(kid);
                    }
                }
            } else if (kids.isDictionary()) {
                kidList.push_back(kids);
            }
            const int index = tree.elements.size();
            tree.elements.append(record);

            for (const QPDFObjectHandle& kid : kidList) {
                const QPDFObjectHandle kidType = kid.getKey("/S");
                if (kidType.isName()) {
                    walk(kid, index, record.pageIndex, depth + 1);
                } else if (kid.isIndirect()) {
                    // widget 注解：记下对象号。
                    tree.elements[index].widgetObjectIds.append(kid.getObjectID());
                }
            }
            // 子元素的下标：刚才追加的那些。
            for (int i = index + 1; i < tree.elements.size(); ++i) {
                if (tree.elements.at(i).parent == index) tree.elements[index].children.append(i);
            }
        };

    QPDFObjectHandle rootElement = root.getKey("/K");
    if (rootElement.isArray()) {
        const std::vector<QPDFObjectHandle> items = rootElement.getArrayAsVector();
        if (!items.empty()) rootElement = items.front();
    }
    if (rootElement.isDictionary()) {
        walk(rootElement, -1, -1, 0);
        tree.rootIndex = 0;
    }
    return tree;
}

// 树序：先序展开的元素下标。
QVector<int> treeOrder(const RawTree& tree) {
    QVector<int> order;
    if (tree.rootIndex < 0) return order;
    QVector<int> stack{tree.rootIndex};
    while (!stack.isEmpty()) {
        const int index = stack.takeFirst();
        order.append(index);
        const QVector<int> children = tree.elements.at(index).children;
        for (int i = children.size() - 1; i >= 0; --i) stack.prepend(children.at(i));
    }
    return order;
}

// 从内容流里抽出「MCID → 文字」。
QHash<int, QString> extractTextByMcid(const QByteArray& content) {
    QHash<int, QString> result;
    static const QRegularExpression mcidPattern(QStringLiteral("<</MCID\\s+(\\d+)>>"));
    static const QRegularExpression textPattern(QStringLiteral("\\(([^)]*)\\)\\s*Tj"));
    int position = 0;
    while (true) {
        const int at = content.indexOf("/Tag", position);
        if (at < 0) break;
        const int end = content.indexOf("EMC", at);
        const QByteArray chunk = content.mid(at, (end < 0 ? content.size() : end) - at);
        const QString text = QString::fromLatin1(chunk);
        const QRegularExpressionMatch mcidMatch = mcidPattern.match(text);
        const QRegularExpressionMatch textMatch = textPattern.match(text);
        if (mcidMatch.hasMatch() && textMatch.hasMatch()) {
            result.insert(mcidMatch.captured(1).toInt(), textMatch.captured(1));
        }
        if (end < 0) break;
        position = end;
    }
    return result;
}

// 按树序把文字拼起来。
QString readingTextOf(const RawTree& tree, const QByteArray& content) {
    const QHash<int, QString> textOfMcid = extractTextByMcid(content);
    QStringList pieces;
    for (int index : treeOrder(tree)) {
        for (int mcid : tree.elements.at(index).mcids) {
            const QString text = textOfMcid.value(mcid);
            if (!text.isEmpty()) pieces.append(text);
        }
    }
    return pieces.join(QStringLiteral(" "));
}

// 改坏产物（用 qpdf 直接改，不经过生成器）。
QByteArray mutateBytes(const QByteArray& bytes, const std::function<void(QPDF&)>& mutator) {
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("mutate.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    mutator(pdf);
    QPDFWriter writer(pdf);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setOutputMemory();
    writer.write();
    std::unique_ptr<Buffer> output(writer.getBuffer());
    if (!output) return {};
    return QByteArray(reinterpret_cast<const char*>(output->getBuffer()),
                      static_cast<qsizetype>(output->getSize()));
}

// 找到第一个类型是给定名字的结构元素。
QPDFObjectHandle findElementOfType(const QPDFObjectHandle& element, const char* type,
                                   int depth = 0) {
    if (depth > 64 || !element.isDictionary()) return QPDFObjectHandle();
    const QPDFObjectHandle own = element.getKey("/S");
    if (own.isName() && own.getName() == type) return element;
    const QPDFObjectHandle kids = element.getKey("/K");
    std::vector<QPDFObjectHandle> kidList;
    if (kids.isArray()) kidList = kids.getArrayAsVector();
    else if (kids.isDictionary()) kidList.push_back(kids);
    for (const QPDFObjectHandle& kid : kidList) {
        if (!kid.isDictionary() || !kid.getKey("/S").isName()) continue;
        const QPDFObjectHandle found = findElementOfType(kid, type, depth + 1);
        if (found.isDictionary()) return found;
    }
    return QPDFObjectHandle();
}

// 结构树根元素。
QPDFObjectHandle rootElementOf(QPDF& pdf) {
    QPDFObjectHandle root = pdf.getRoot().getKey("/StructTreeRoot");
    QPDFObjectHandle element = root.getKey("/K");
    if (element.isArray()) {
        const std::vector<QPDFObjectHandle> items = element.getArrayAsVector();
        if (!items.empty()) element = items.front();
    }
    return element;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

class UaGeneratorTests : public QObject {
    Q_OBJECT

private slots:
    void generatedDocumentPassesChecker();
    void generatedDocumentHasSemanticShape();
    void parentTreeIsBijectiveWithMarkedContent();
    void pageStructParentsMatchParentTreeKeys();
    void widgetStructParentPointsToFormElement();
    void formFieldsCarryRoleAndTitle();
    void readingOrderTextMatchesExpected();
    void formFieldOrderMatchesExpected();
    void checkerResultIsReproducible();
    void removingAltMakesCheckerFail();
    void pointingKAtMissingMcidMakesCheckerFail();
    void removingPartMakesCheckerFail();
    void removingWidgetStructParentMakesCheckerFail();
    void disclaimerDoesNotClaimCertification();
    void emptyInputFailsWithChineseReason();
};

// 一键生成：产物必须过了本项目检查器的全部检查。
void UaGeneratorTests::generatedDocumentPassesChecker() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible report");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildTextPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));
    QVERIFY2(result.ok, qPrintable(result.summaryText));
    QCOMPARE(result.report.errorCount, 0);
    // 摘要说的是「过了本项目的检查」，不是「通过了 PDF/UA 校验」。
    QVERIFY(result.summaryText.contains(QStringLiteral("本项目检查器")));
    QVERIFY(!result.summaryText.contains(QStringLiteral("通过 PDF/UA")));
}

// 独立复算：产物的结构树形状必须是 Document → Part → Sect → …。
void UaGeneratorTests::generatedDocumentHasSemanticShape() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible report");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildTextPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));

    const RawTree tree = readTreeIndependently(result.bytes);
    QVERIFY(tree.rootIndex >= 0);
    QCOMPARE(tree.elements.at(tree.rootIndex).type, QStringLiteral("Document"));

    // Document 下面只有一个 Part。
    const RawElement& document = tree.elements.at(tree.rootIndex);
    QCOMPARE(document.children.size(), 1);
    const RawElement& part = tree.elements.at(document.children.at(0));
    QCOMPARE(part.type, QStringLiteral("Part"));

    // Part 下面是 Sect；Sect 下面是标题 / 段落 / 列表。
    QCOMPARE(part.children.size(), 1);
    const RawElement& section = tree.elements.at(part.children.at(0));
    QCOMPARE(section.type, QStringLiteral("Sect"));

    QStringList types;
    for (int child : section.children) types.append(tree.elements.at(child).type);
    QVERIFY(types.contains(QStringLiteral("H1")));
    QVERIFY(types.contains(QStringLiteral("P")));
    QVERIFY(types.contains(QStringLiteral("L")));

    // 列表下面是列表项。
    for (int child : section.children) {
        if (tree.elements.at(child).type != QStringLiteral("L")) continue;
        const RawElement& list = tree.elements.at(child);
        QCOMPARE(list.children.size(), 2);
        QCOMPARE(tree.elements.at(list.children.at(0)).type, QStringLiteral("LI"));
        QCOMPARE(tree.elements.at(list.children.at(1)).type, QStringLiteral("LI"));
    }
}

// 独立复算：/ParentTree 与 MCID 一一对应。
void UaGeneratorTests::parentTreeIsBijectiveWithMarkedContent() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible report");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildTextPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));

    const RawTree tree = readTreeIndependently(result.bytes);
    QCOMPARE(tree.pageStructParents.size(), 1);
    const int key = tree.pageStructParents.at(0);
    QVERIFY(key >= 0);
    QVERIFY(tree.parentTree.contains(key));

    const QVector<int> entries = tree.parentTree.value(key);
    const QSet<int> contentMcids = tree.contentMcids.value(0);
    QVERIFY(!contentMcids.isEmpty());

    // 内容流里的每个 MCID，/ParentTree 里都要有一条，而且指向一个真的装着它的元素。
    for (int mcid : contentMcids) {
        QVERIFY2(mcid < entries.size(), qPrintable(QStringLiteral("MCID %1 没有条目").arg(mcid)));
        const int objectId = entries.at(mcid);
        QVERIFY(objectId >= 0);
        // 找到这个对象号对应的元素下标。
        int found = -1;
        for (int i = 0; i < tree.elements.size(); ++i) {
            // 元素本身没有对象号，这里用「装了这个 MCID」来判断它是不是那条记录。
            if (tree.elements.at(i).mcids.contains(mcid)) {
                found = i;
                break;
            }
        }
        QVERIFY2(found >= 0, qPrintable(QStringLiteral("MCID %1 没有结构元素装着它").arg(mcid)));
    }
    // 结构树引用的 MCID 都要在内容流里。
    for (const RawElement& element : tree.elements) {
        for (int mcid : element.mcids) {
            QVERIFY(contentMcids.contains(mcid));
        }
    }
}

// 独立复算：页的 /StructParents 与 /ParentTree 的键一致。
void UaGeneratorTests::pageStructParentsMatchParentTreeKeys() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible report");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildTextPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));

    const RawTree tree = readTreeIndependently(result.bytes);
    for (int key : tree.pageStructParents) {
        QVERIFY(key >= 0);
        QVERIFY2(tree.parentTree.contains(key),
                 qPrintable(QStringLiteral("键 %1 在 /ParentTree 里没有条目").arg(key)));
    }
    QSet<int> pageKeys;
    for (int key : tree.pageStructParents) pageKeys.insert(key);
    for (auto it = tree.parentTree.constBegin(); it != tree.parentTree.constEnd(); ++it) {
        QVERIFY2(pageKeys.contains(it.key()),
                 qPrintable(QStringLiteral("/ParentTree 里有键 %1，但页上没有").arg(it.key())));
    }
}

// 独立复算：widget 的 /StructParent → /ParentTree 条目 → /Form 结构元素。
void UaGeneratorTests::widgetStructParentPointsToFormElement() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible form");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildFormPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));

    const RawTree tree = readTreeIndependently(result.bytes);
    QVERIFY(!tree.widgetStructParents.isEmpty());
    const int key = tree.pageStructParents.value(0);
    QVERIFY(tree.parentTree.contains(key));
    const QVector<int> entries = tree.parentTree.value(key);

    // 至少有一个 widget 挂在 /Form 元素上。
    int formCount = 0;
    for (const RawElement& element : tree.elements) {
        if (element.type == QStringLiteral("Form")) ++formCount;
    }
    QVERIFY(formCount >= 1);

    // 每个 widget 的 /StructParent 都指向一个存在的条目。
    for (auto it = tree.widgetStructParents.constBegin(); it != tree.widgetStructParents.constEnd(); ++it) {
        QVERIFY2(it.value() >= 0, "widget 没有 /StructParent");
        QVERIFY2(it.value() < entries.size(), "widget 的 /StructParent 越界");
        QVERIFY(entries.at(it.value()) >= 0);
    }
}

// 独立复算：表单域在语义树里带正确的角色、层级与非空的名字。
void UaGeneratorTests::formFieldsCarryRoleAndTitle() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible form");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildFormPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));

    const RawTree tree = readTreeIndependently(result.bytes);
    QStringList formTitles;
    for (const RawElement& element : tree.elements) {
        if (element.type != QStringLiteral("Form")) continue;
        formTitles.append(element.title);
        // 字段名不能空，替代文本也不能空（读屏要念得出字段名）。
        QVERIFY2(!element.title.isEmpty(), "表单域没有 /T");
        QVERIFY2(!element.alt.isEmpty(), "表单域没有 /Alt");
        // 表单域必须挂在 Part 下面（不能平铺在 Document 末尾）。
        QVERIFY(element.parent >= 0);
        const QString parentType = tree.elements.at(element.parent).type;
        QVERIFY2(parentType == QStringLiteral("Part") || parentType == QStringLiteral("Sect"),
                 qPrintable(QStringLiteral("表单域挂在 %1 下面").arg(parentType)));
    }
    QCOMPARE(formTitles.size(), 2);
}

// 朗读序列：按树顺序拼起来的文字要等于预期串。
void UaGeneratorTests::readingOrderTextMatchesExpected() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible report");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildTextPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));

    // 独立读一遍内容流（不复用生成器的扫描结果）。
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("reading.pdf", result.bytes.constData(),
                          static_cast<size_t>(result.bytes.size()));
    const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
    QVERIFY(!pages.empty());

    const RawTree tree = readTreeIndependently(result.bytes);
    const QString text = readingTextOf(tree, pageContentOf(pages, 0));
    QCOMPARE(text, QStringLiteral("Report Title Body paragraph line one. "
                                  "Body paragraph line two. - first item - second item "
                                  "Figure 1 Company logo"));
}

// 表单域的朗读顺序：树序里 /Form 的 /T 序列 == 预期字段顺序。
void UaGeneratorTests::formFieldOrderMatchesExpected() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible form");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildFormPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));

    const RawTree tree = readTreeIndependently(result.bytes);
    QStringList formTitles;
    for (int index : treeOrder(tree)) {
        const RawElement& element = tree.elements.at(index);
        if (element.type != QStringLiteral("Form")) continue;
        formTitles.append(element.title);
        QVERIFY2(!element.alt.isEmpty(), "表单域没有替代文本");
    }
    QCOMPARE(formTitles, QStringList({QStringLiteral("firstName"), QStringLiteral("lastName")}));
}

// 复检结果可重复：生成器自己复检的结论 == 独立再跑一次 checkPdfUa。
void UaGeneratorTests::checkerResultIsReproducible() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible report");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildTextPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));

    const PdfUaReport independent = checkPdfUa(result.bytes);
    QCOMPARE(independent.findings.size(), result.report.findings.size());
    for (int i = 0; i < independent.findings.size(); ++i) {
        QCOMPARE(independent.findings.at(i).code, result.report.findings.at(i).code);
        QCOMPARE(independent.findings.at(i).severity, result.report.findings.at(i).severity);
    }
    QCOMPARE(independent.errorCount, result.report.errorCount);
    QCOMPARE(independent.passCount, result.report.passCount);
    QCOMPARE(independent.compliant, result.report.compliant);
}

// ---- 变异测试：改坏产物，检查器必须变红 ----
//
// 为什么这一组最关键：生成器与检查器同源，「生成器的产物过了检查器」
// 证明不了任何事。把产物按指定方式改坏，检查器必须**报出对应的错误**——
// 这才说明检查器真的在查那件事。

// 变异 1：删掉一个 /Alt。
void UaGeneratorTests::removingAltMakesCheckerFail() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible report");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildTextPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));
    QVERIFY(result.ok);

    const QByteArray mutated = mutateBytes(result.bytes, [](QPDF& pdf) {
        QPDFObjectHandle element = findElementOfType(rootElementOf(pdf), "/Figure");
        if (!element.isDictionary()) element = findElementOfType(rootElementOf(pdf), "/H1");
        element.removeKey("/Alt");
    });
    QVERIFY(!mutated.isEmpty());

    const PdfUaReport report = checkPdfUa(mutated);
    QVERIFY(!report.compliant);
    QVERIFY2(findFinding(report, QStringLiteral("ua-missing-alt-text")) != nullptr,
             "删掉 /Alt 之后检查器没有报出来");
}

// 变异 2：把一个 /K 的 MCID 改成不存在的值。
void UaGeneratorTests::pointingKAtMissingMcidMakesCheckerFail() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible report");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildTextPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));
    QVERIFY(result.ok);

    const QByteArray mutated = mutateBytes(result.bytes, [](QPDF& pdf) {
        QPDFObjectHandle element = findElementOfType(rootElementOf(pdf), "/H1");
        if (!element.isDictionary()) return;
        std::vector<QPDFObjectHandle> mcids;
        mcids.push_back(QPDFObjectHandle::newInteger(9999));
        element.replaceKey("/K", QPDFObjectHandle::newArray(mcids));
    });
    QVERIFY(!mutated.isEmpty());

    const PdfUaReport report = checkPdfUa(mutated);
    QVERIFY(!report.compliant);
    QVERIFY2(findFinding(report, QStringLiteral("ua-mcid-missing-in-content")) != nullptr,
             "MCID 指向不存在的内容时检查器没有报出来");
}

// 变异 3：拆掉 /Part。
void UaGeneratorTests::removingPartMakesCheckerFail() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible report");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildTextPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));
    QVERIFY(result.ok);

    const QByteArray mutated = mutateBytes(result.bytes, [](QPDF& pdf) {
        QPDFObjectHandle document = rootElementOf(pdf);
        const QPDFObjectHandle kids = document.getKey("/K");
        if (!kids.isArray()) return;
        const std::vector<QPDFObjectHandle> parts = kids.getArrayAsVector();
        if (parts.empty()) return;
        // Part 的子节点（Sect）直接挂到 Document 下面：/Part 这一层没了。
        const QPDFObjectHandle part = parts.front();
        const QPDFObjectHandle partKids = part.getKey("/K");
        std::vector<QPDFObjectHandle> sections;
        if (partKids.isArray()) sections = partKids.getArrayAsVector();
        if (sections.empty()) return;
        document.replaceKey("/K", QPDFObjectHandle::newArray(sections));
    });
    QVERIFY(!mutated.isEmpty());

    const PdfUaReport report = checkPdfUa(mutated);
    QVERIFY(!report.compliant);
    QVERIFY2(findFinding(report, QStringLiteral("ua-missing-part-element")) != nullptr,
             "拆掉 /Part 之后检查器没有报出来");
}

// 变异 4：去掉一个 widget 的 /StructParent。
void UaGeneratorTests::removingWidgetStructParentMakesCheckerFail() {
    REQUIRE_QPDF();
    UaGeneratorOptions options;
    options.title = QStringLiteral("Generated accessible form");
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(buildFormPdf(), options);
    QVERIFY2(!result.bytes.isEmpty(), qPrintable(result.error));

    const QByteArray mutated = mutateBytes(result.bytes, [](QPDF& pdf) {
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        for (const QPDFObjectHandle& page : pages) {
            const QPDFObjectHandle annots = page.getKey("/Annots");
            if (!annots.isArray()) continue;
            for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
                if (!annot.isDictionary()) continue;
                const QPDFObjectHandle subtype = annot.getKey("/Subtype");
                if (!subtype.isName() || subtype.getName() != "/Widget") continue;
                // 拷一份可改的：qpdf 的句柄只有非 const 版本能改。
                QPDFObjectHandle widget = annot;
                widget.removeKey("/StructParent");
                return;
            }
        }
    });
    QVERIFY(!mutated.isEmpty());

    const PdfUaReport report = checkPdfUa(mutated);
    QVERIFY(!report.compliant);
    QVERIFY2(findFinding(report, QStringLiteral("ua-widget-missing-struct-parent")) != nullptr,
             "去掉 widget 的 /StructParent 之后检查器没有报出来");
}

// 免责说明不能宣称「已通过 PDF/UA 校验」。
void UaGeneratorTests::disclaimerDoesNotClaimCertification() {
    const QString disclaimer = aipdf::pdf::uaGenerationDisclaimer();
    QVERIFY(!disclaimer.isEmpty());
    QVERIFY(disclaimer.contains(QStringLiteral("不是")));
    QVERIFY(disclaimer.contains(QStringLiteral("veraPDF")));
    QVERIFY(!disclaimer.contains(QStringLiteral("已通过 PDF/UA")));
}

// 空输入：失败也要给中文原因。
void UaGeneratorTests::emptyInputFailsWithChineseReason() {
    const UaGeneratorResult result = aipdf::pdf::generateUaPdf(QByteArray());
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("空")));
}

QTEST_GUILESS_MAIN(UaGeneratorTests)
#include "UaGeneratorTests.moc"
