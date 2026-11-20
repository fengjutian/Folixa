#include "pdf/compliance/SemanticAltText.h"

#include "i18n/Translate.h"

#include <QHash>
#include <QSet>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>
#include <set>
#include <vector>
#endif

namespace aipdf::pdf {

namespace {

// 遍历护栏：标签树可能有环，层级也可能深得离谱。
// 超出就停手并报错，不把自己绕死在里面。
const int kMaxTreeDepth = 64;
const int kMaxTreeElements = 50000;

}  // namespace

int SemanticTreeIndex::indexOfObject(int objectId, int pageIndex) const {
    const QVector<int> candidates = elementsReferencing(objectId);
    for (int index : candidates) {
        if (pageIndex < 0) return index;
        if (elements.at(index).pageIndex == pageIndex) return index;
    }
    return -1;
}

QVector<int> SemanticTreeIndex::elementsReferencing(int objectId) const {
    QVector<int> result;
    for (int i = 0; i < elements.size(); ++i) {
        if (elements.at(i).referencedObjectIds.contains(objectId)) result.append(i);
    }
    return result;
}

#ifdef AIPDF_WITH_QPDF
namespace {

// 一条「结构元素 → 页面对象」的引用。
struct ObjectReference {
    // 被引用的对象号。
    int objectId = -1;
    // 引用自带 /Pg 时的页序号（-1 表示没写）。
    int pageIndex = -1;
    // 被引用对象的类型名（"/Image"、"/Widget"……）。
    QString typeName;
};

// 遍历标签树时用的上下文。
struct TreeContext {
    // 页面对象号 → 页序号（0 基）。
    QHash<int, int> pageIndexById;
    // 父对象号 → 子对象号（按 /Parent 反查出来的）。
    QHash<int, QVector<int>> childrenByParent;
    // 对象号 → 结构元素字典。
    QHash<int, QPDFObjectHandle> structElems;
    // 已访问过的元素（防环）。
    QSet<int> visited;
    // 收集到的元素（与 elements 同序）。
    QVector<QPDFObjectHandle> handles;
    QString error;
};

// 拿一个对象的字典：图像 XObject 是流，用 isDictionary() 判会被丢掉。
QPDFObjectHandle dictionaryOf(const QPDFObjectHandle& object) {
    return object.isStream() ? object.getDict() : object;
}

// 一个对象的类型名（/Subtype，没有就取 /Type）："/Image"、"/Widget"……
//
// 用途：对象号在 qpdf 写回后会变，类型不会——跨写回比较元素时靠它。
QString objectTypeName(const QPDFObjectHandle& object) {
    const QPDFObjectHandle dictionary = dictionaryOf(object);
    if (!dictionary.isDictionary()) return QString();
    const QPDFObjectHandle subtype = dictionary.getKey("/Subtype");
    if (subtype.isName()) return QString::fromLatin1(subtype.getName().c_str());
    const QPDFObjectHandle type = dictionary.getKey("/Type");
    if (type.isName()) return QString::fromLatin1(type.getName().c_str());
    return QString();
}

// 一个结构元素是不是「另一个结构元素」（也就是 /K 里的子节点）。
//
// 判据：带 /S（标签名，PDF 32000-1 §14.7.2 要求结构元素必须有 /S），
// 或者 /Type 是 /StructElem。
//
// 为什么要分清：/K 里既有子结构元素，也有指向页面对象的引用
// （/OBJR 字典、widget 注解），两种东西长得都是字典。
// widget 注解不能靠 /Parent 认——widget 的 /Parent 指的是**字段**字典
// （层级字段的惯例），不是父结构元素。
bool isChildStructElement(const QPDFObjectHandle& item) {
    const QPDFObjectHandle dictionary = dictionaryOf(item);
    if (!dictionary.isDictionary()) return false;
    if (dictionary.getKey("/S").isName() || dictionary.getKey("/S").isArray()) return true;
    const QPDFObjectHandle type = dictionary.getKey("/Type");
    if (type.isName() && type.getName() == "/StructElem") return true;
    return false;
}

// 从元素的 /K 里取出全部「指向页面对象」的引用。
//
// /K 有三种形态（PDF 32000-1 §14.7.2），这里都要看：
//   * 字典：可能是 /OBJR（指向图像 XObject），也可能是 widget 注解；
//   * 数组：上面两种混着来；
//   * 数字：内容流里的 MCID，不是对象引用，跳过。
QVector<ObjectReference> collectReferences(const QPDFObjectHandle& element,
                                           const TreeContext& context) {
    QVector<ObjectReference> references;
    const QPDFObjectHandle kids = element.getKey("/K");

    const auto handleItem = [&](const QPDFObjectHandle& item) {
        const QPDFObjectHandle dictionary = dictionaryOf(item);
        if (!dictionary.isDictionary()) return;
        const QPDFObjectHandle type = dictionary.getKey("/Type");
        if (type.isName() && type.getName() == "/OBJR") {
            // /OBJR：结构元素直接指向页面上的那个对象（图、注解）。
            const QPDFObjectHandle target = dictionary.getKey("/Obj");
            // 直接对象没有对象号，按对象号认不出来，只能跳过。
            if (!target.isIndirect()) return;
            ObjectReference reference;
            reference.objectId = target.getObjectID();
            reference.typeName = objectTypeName(target);
            const QPDFObjectHandle page = dictionary.getKey("/Pg");
            if (page.isDictionary() && page.isIndirect()) {
                reference.pageIndex = context.pageIndexById.value(page.getObjectID(), -1);
            }
            references.append(reference);
            return;
        }
        // 不是 /OBJR：只有间接对象才有对象号。
        if (!item.isIndirect()) return;
        // 子结构元素不是引用。
        if (isChildStructElement(item)) return;
        ObjectReference reference;
        reference.objectId = item.getObjectID();
        reference.typeName = objectTypeName(item);
        references.append(reference);
    };

    if (kids.isDictionary()) {
        handleItem(kids);
    } else if (kids.isArray()) {
        const std::vector<QPDFObjectHandle> items = kids.getArrayAsVector();
        for (const QPDFObjectHandle& item : items) handleItem(item);
    }
    return references;
}

// 遍历一个结构元素（以及它的子孙）。
void traverseElement(TreeContext& context, const QPDFObjectHandle& node, int depth,
                     int inheritedPage, QVector<SemanticElementInfo>* elements) {
    if (!node.isDictionary()) return;
    if (!context.error.isEmpty()) return;
    if (depth > kMaxTreeDepth) {
        context.error = QStringLiteral("标签树层级超过 %1 层，遍历已停止").arg(kMaxTreeDepth);
        return;
    }
    if (elements->size() >= kMaxTreeElements) {
        context.error = QStringLiteral("标签树元素超过 %1 个，遍历已停止").arg(kMaxTreeElements);
        return;
    }

    const bool indirect = node.isIndirect();
    const int id = indirect ? node.getObjectID() : 0;
    if (indirect) {
        if (context.visited.contains(id)) {
            context.error = QStringLiteral("标签树里有环：元素 %1 被重复引用，遍历已停止").arg(id);
            return;
        }
        context.visited.insert(id);
    }

    SemanticElementInfo info;
    info.objectId = indirect ? id : -1;

    const QPDFObjectHandle tag = node.getKey("/S");
    if (tag.isName()) {
        info.tag = QString::fromLatin1(tag.getName().c_str() + 1);
    } else if (tag.isArray()) {
        // 有些文件把 /S 写成数组（一个元素多个标签），取第一个。
        const std::vector<QPDFObjectHandle> items = tag.getArrayAsVector();
        for (const QPDFObjectHandle& item : items) {
            if (!item.isName()) continue;
            info.tag = QString::fromLatin1(item.getName().c_str() + 1);
            break;
        }
    }
    const QPDFObjectHandle alt = node.getKey("/Alt");
    if (alt.isString()) info.alt = QString::fromUtf8(alt.getUTF8Value().c_str());
    const QPDFObjectHandle title = node.getKey("/T");
    if (title.isString()) info.title = QString::fromUtf8(title.getUTF8Value().c_str());

    // 所属页：自己的 /Pg 优先，否则继承父元素的。
    int page = inheritedPage;
    const QPDFObjectHandle pageRef = node.getKey("/Pg");
    if (pageRef.isDictionary() && pageRef.isIndirect()) {
        const int found = context.pageIndexById.value(pageRef.getObjectID(), -1);
        if (found >= 0) page = found;
    }
    info.pageIndex = page;

    const QVector<ObjectReference> references = collectReferences(node, context);
    for (const ObjectReference& reference : references) {
        if (reference.objectId < 0) continue;
        info.referencedObjectIds.append(reference.objectId);
        info.referencedObjectTypes.append(reference.typeName);
        // 引用自带 /Pg 时，它比继承来的页更准（图在哪一页就在哪一页）。
        if (reference.pageIndex >= 0) info.pageIndex = reference.pageIndex;
    }

    elements->append(info);
    context.handles.append(node);

    // /K 里的子元素。
    const QPDFObjectHandle kids = node.getKey("/K");
    if (kids.isDictionary()) {
        if (!isChildStructElement(kids)) {
            // /K 是单个字典但不是子元素（例如单个 /OBJR）：没有子元素可走。
        } else {
            traverseElement(context, kids, depth + 1, page, elements);
        }
    } else if (kids.isArray()) {
        const std::vector<QPDFObjectHandle> items = kids.getArrayAsVector();
        for (const QPDFObjectHandle& item : items) {
            if (!isChildStructElement(item)) continue;
            traverseElement(context, item, depth + 1, page, elements);
        }
    }

    // 有些文件只靠 /Parent 串层级，/K 里是空的：按反向索引把子元素找回来。
    // 已经访问过的跳过（不当作环）——那样会把「/K 与 /Parent 都指着同一个孩子」
    // 的正常文件误判成环。
    if (!indirect) return;
    const QVector<int> childIds = context.childrenByParent.value(id);
    for (int childId : childIds) {
        if (context.visited.contains(childId)) continue;
        const QPDFObjectHandle child = context.structElems.value(childId);
        if (!child.isDictionary()) continue;
        traverseElement(context, child, depth + 1, page, elements);
    }
}

// 扫一遍文档里的标签树。
TreeContext scanTree(QPDF& pdf, QVector<SemanticElementInfo>* elements, bool* hasStructTree) {
    TreeContext context;
    *hasStructTree = false;

    const QPDFObjectHandle catalog = pdf.getRoot();
    if (!catalog.isDictionary()) {
        context.error = QStringLiteral("PDF 的根对象不是字典");
        return context;
    }

    const std::vector<QPDFObjectHandle> allPages = pdf.getAllPages();
    for (size_t i = 0; i < allPages.size(); ++i) {
        if (allPages.at(i).isIndirect()) {
            context.pageIndexById.insert(allPages.at(i).getObjectID(), static_cast<int>(i));
        }
    }

    const QPDFObjectHandle root = catalog.getKey("/StructTreeRoot");
    if (!root.isDictionary()) {
        // 没有标签树不算失败：调用方自己看 hasStructTree。
        return context;
    }
    *hasStructTree = true;

    // 反向索引：按 /Parent 找出每个元素的孩子。
    for (const QPDFObjectHandle& object : pdf.getAllObjects()) {
        if (!object.isDictionary() || !object.isIndirect()) continue;
        const QPDFObjectHandle type = object.getKey("/Type");
        const bool typedStructElem = type.isName() && type.getName() == "/StructElem";
        const bool looksLikeStructElem =
            !type.isName() && object.getKey("/S").isName() && object.getKey("/Parent").isDictionary();
        if (!typedStructElem && !looksLikeStructElem) continue;
        const int id = object.getObjectID();
        context.structElems.insert(id, object);
        const QPDFObjectHandle parent = object.getKey("/Parent");
        if (parent.isIndirect()) {
            context.childrenByParent[parent.getObjectID()].append(id);
        }
    }

    const QPDFObjectHandle kids = root.getKey("/K");
    if (kids.isDictionary()) {
        if (isChildStructElement(kids)) {
            traverseElement(context, kids, 0, -1, elements);
        }
    } else if (kids.isArray()) {
        const std::vector<QPDFObjectHandle> items = kids.getArrayAsVector();
        for (const QPDFObjectHandle& item : items) {
            if (!isChildStructElement(item)) continue;
            traverseElement(context, item, 0, -1, elements);
        }
    }
    return context;
}

// 一页的内容流（解压后的字节）。
QByteArray pageContentData(QPDFObjectHandle page) {
    QByteArray content;
    const std::vector<QPDFObjectHandle> contents = page.getPageContents();
    for (const QPDFObjectHandle& item : contents) {
        if (!item.isStream()) continue;
        QPDFObjectHandle stream = item;
        const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (!data) continue;
        content += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                               static_cast<qsizetype>(data->getSize()));
        content += '\n';
    }
    return content;
}

// 一个注解字典的结构描述：键 + 值（间接引用只记对象号，不把外观流展开）。
//
// 为什么这么记：注解里通常挂着外观流，直接展开会把整张图塞进内存；
// 而「哪个键指向哪个对象」已经足够判断注解有没有被动过。
QByteArray describeAnnotation(const QPDFObjectHandle& annotation) {
    QByteArray out;
    if (!annotation.isDictionary()) return out;
    const std::set<std::string> keys = annotation.getKeys();
    for (const std::string& key : keys) {
        out += QByteArray::fromStdString(key);
        out += ':';
        const QPDFObjectHandle value = annotation.getKey(key);
        if (value.isIndirect()) {
            out += QByteArray::number(value.getObjectID());
            out += ':';
            out += QByteArray::number(value.getGeneration());
        } else if (value.isStream()) {
            out += "<stream>";
        } else {
            const std::string text = value.unparse();
            out.append(text.data(), static_cast<qsizetype>(text.size()));
        }
        out += '\n';
    }
    return out;
}

// 每页的注解结构描述（按 /Annots 的次序）。
QByteArray pageAnnotationData(const QPDFObjectHandle& page) {
    QByteArray out;
    const QPDFObjectHandle annotations = page.getKey("/Annots");
    if (!annotations.isArray()) return out;
    const std::vector<QPDFObjectHandle> items = annotations.getArrayAsVector();
    for (const QPDFObjectHandle& item : items) {
        if (item.isIndirect()) {
            out += QByteArray::number(item.getObjectID());
            out += ':';
            out += QByteArray::number(item.getGeneration());
            out += ' ';
        }
        out += describeAnnotation(item);
        out += '\n';
    }
    return out;
}

// 文档的「页面内容 + 注解」指纹：用来确认改 /Alt 时没碰别的东西。
QVector<QByteArray> documentFingerprint(QPDF& pdf) {
    QVector<QByteArray> result;
    const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
    for (const QPDFObjectHandle& page : pages) {
        QByteArray entry;
        entry += "content:";
        entry += pageContentData(page);
        entry += "\nannots:";
        entry += pageAnnotationData(page);
        result.append(entry);
    }
    return result;
}

// 把一份打开的 PDF 写成字节。
QByteArray writePdf(QPDF& pdf, QString* error) {
    QPDFWriter writer(pdf);
    // 对象流保持原样（qpdf 的默认行为）：这份文件原来什么样就写成什么样，
    // 不顺手把交叉引用表换成别的形态。
    writer.setOutputMemory();
    writer.write();
    std::unique_ptr<Buffer> output(writer.getBuffer());
    if (!output || output->getSize() == 0) {
        if (error) *error = QStringLiteral("qpdf 写出的 PDF 为空");
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(output->getBuffer()),
                      static_cast<qsizetype>(output->getSize()));
}

}  // namespace
#endif  // AIPDF_WITH_QPDF

SemanticTreeIndex readSemanticTreeIndex(const QByteArray& pdfBytes) {
    SemanticTreeIndex index;
#ifdef AIPDF_WITH_QPDF
    if (pdfBytes.isEmpty()) {
        index.error = QStringLiteral("输入的 PDF 内容为空");
        return index;
    }
    try {
        QPDF pdf;
        pdf.processMemoryFile("semantic-alt-tree.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        bool hasStructTree = false;
        TreeContext context = scanTree(pdf, &index.elements, &hasStructTree);
        index.hasStructTree = hasStructTree;
        if (!context.error.isEmpty()) {
            index.error = context.error;
            index.elements.clear();
        }
    } catch (const std::exception& exception) {
        index.error = QStringLiteral("解析 PDF 失败：%1")
                          .arg(QString::fromUtf8(exception.what()));
        index.elements.clear();
    }
#else
    Q_UNUSED(pdfBytes)
    index.error = QStringLiteral("当前构建没有启用 qpdf，无法读取语义树");
#endif
    return index;
}

SemanticAltTextResult setSemanticAltText(const QByteArray& pdfBytes, int objectId,
                                         const QString& altText,
                                         const SemanticAltTextOptions& options) {
    SemanticAltTextResult result;
    if (pdfBytes.isEmpty()) {
        result.error = i18n::tr("aipdf::pdf::SemanticAltText", "输入的 PDF 内容为空");
        return result;
    }
    if (objectId < 0) {
        result.error = i18n::tr("aipdf::pdf::SemanticAltText", "目标对象号无效");
        return result;
    }

    // 替代文本首尾的空白没有意义（读屏软件不会替用户分辨），
    // 顺手去掉；全空白等同于没有文本。
    const QString alternative = altText.trimmed();
    if (alternative.isEmpty() && !options.allowClear) {
        result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                 "替代文本不能为空：清空替代文本会让读屏软件念出一段空白。"
                                 "确认要清空请显式打开 allowClear");
        return result;
    }

#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("semantic-alt.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));

        const QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText", "PDF 的根对象不是字典");
            return result;
        }
        if (!catalog.getKey("/StructTreeRoot").isDictionary()) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                     "这份文档没有语义标签树（根对象里没有 /StructTreeRoot），"
                                     "无法改写结构元素的替代文本");
            return result;
        }

        // 改之前的样子：留一份，写回后要拿它对照。
        QVector<SemanticElementInfo> before;
        bool hasStructTree = false;
        TreeContext context = scanTree(pdf, &before, &hasStructTree);
        if (!context.error.isEmpty()) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText", "读语义标签树失败：%1")
                               .arg(context.error);
            return result;
        }
        if (!hasStructTree) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                     "这份文档没有语义标签树（根对象里没有 /StructTreeRoot），"
                                     "无法改写结构元素的替代文本");
            return result;
        }
        const QVector<QByteArray> fingerprintBefore = documentFingerprint(pdf);

        // 找引用了目标对象的那些结构元素。
        QVector<int> candidates;
        for (int i = 0; i < before.size(); ++i) {
            if (before.at(i).referencedObjectIds.contains(objectId)) candidates.append(i);
        }
        if (candidates.isEmpty()) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                     "这份文档的语义树里没有引用该对象（对象号 %1）的结构元素，"
                                     "没有可改的替代文本")
                               .arg(objectId);
            return result;
        }
        if (options.pageIndex >= 0) {
            QVector<int> onPage;
            for (int index : candidates) {
                if (before.at(index).pageIndex == options.pageIndex) onPage.append(index);
            }
            candidates = onPage;
            if (candidates.isEmpty()) {
                result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                         "这份文档的语义树里没有引用该对象（对象号 %1）"
                                         "且位于第 %2 页的结构元素")
                                   .arg(objectId)
                                   .arg(options.pageIndex + 1);
                return result;
            }
        }
        if (candidates.size() > 1) {
            QStringList pages;
            for (int index : candidates) {
                const int page = before.at(index).pageIndex;
                pages.append(page >= 0 ? QStringLiteral("第 %1 页").arg(page + 1)
                                       : QStringLiteral("未知页"));
            }
            result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                     "这份文档里有 %1 个结构元素引用同一个对象（对象号 %2，"
                                     "分别在 %3），无法确定改哪一个")
                               .arg(candidates.size())
                               .arg(objectId)
                               .arg(pages.join(QStringLiteral("、")));
            return result;
        }

        const int target = candidates.first();
        // 这里要一份**可写**的句柄：qpdf 的 replaceKey/removeKey 不是 const 方法。
        // 句柄本身是共享的（底层对象同一个），改动会落到文件里的那个字典上。
        QPDFObjectHandle element = context.handles.at(target);
        if (!element.isDictionary()) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                     "目标结构元素不是字典，没法改写");
            return result;
        }

        result.elementObjectId = before.at(target).objectId;
        result.previousAlt = before.at(target).alt;
        if (alternative.isEmpty()) {
            // 清空 = 删掉 /Alt 键，不是写一个空字符串。
            element.removeKey("/Alt");
            result.cleared = true;
        } else {
            element.replaceKey("/Alt",
                               QPDFObjectHandle::newUnicodeString(alternative.toUtf8().toStdString()));
        }

        const QVector<QByteArray> fingerprintAfter = documentFingerprint(pdf);
        if (fingerprintBefore != fingerprintAfter) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                     "改写替代文本时页面内容或批注被改动了，已放弃这次改动");
            return result;
        }

        QString writeError;
        const QByteArray written = writePdf(pdf, &writeError);
        if (written.isEmpty()) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText", "写回 PDF 失败：%1")
                               .arg(writeError);
            return result;
        }

        // ---- 写回校验：重新解析一遍，确认改到了、别的没被动 ----
        const SemanticTreeIndex after = readSemanticTreeIndex(written);
        if (!after.ok()) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                     "写回的 PDF 重新解析失败：%1")
                               .arg(after.error);
            return result;
        }
        if (!after.hasStructTree) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                     "写回的 PDF 里语义标签树不见了");
            return result;
        }
        if (after.elements.size() != before.size()) {
            result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                     "写回前后结构元素的数量对不上（%1 → %2）")
                               .arg(before.size())
                               .arg(after.elements.size());
            return result;
        }
        const QString expected = result.cleared ? QString() : alternative;
        for (int i = 0; i < after.elements.size(); ++i) {
            if (i == target) {
                if (after.elements.at(i).alt != expected) {
                    result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                             "写回后目标结构元素的替代文本不对，"
                                             "已放弃这次改动");
                    return result;
                }
                continue;
            }
            if (after.elements.at(i).alt != before.at(i).alt) {
                result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                         "写回后别的结构元素的替代文本被改动了，已放弃这次改动");
                return result;
            }
            // 引用的**对象号**不能直接比：qpdf 写回时会重新编号。
            // 比的是引用的个数与类型——那两样不会随编号变。
            if (after.elements.at(i).referencedObjectIds.size() !=
                    before.at(i).referencedObjectIds.size() ||
                after.elements.at(i).referencedObjectTypes !=
                    before.at(i).referencedObjectTypes) {
                result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                                         "写回后结构元素引用的对象变了，已放弃这次改动");
                return result;
            }
        }

        result.bytes = written;
        result.succeeded = true;
        return result;
    } catch (const std::exception& exception) {
        result.error = i18n::tr("aipdf::pdf::SemanticAltText", "解析 PDF 失败：%1")
                           .arg(QString::fromUtf8(exception.what()));
        return result;
    }
#else
    Q_UNUSED(altText)
    Q_UNUSED(options)
    result.error = i18n::tr("aipdf::pdf::SemanticAltText",
                             "当前构建没有启用 qpdf，无法改写结构元素的替代文本");
    return result;
#endif
}

}  // namespace aipdf::pdf
