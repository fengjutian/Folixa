#include "pdf/compliance/SemanticReadingOrder.h"

#include "i18n/Translate.h"

#include "pdf/compliance/StructTreeModel.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/compliance/TagTreeWriter.h"
#include "pdf/content/ContentStream.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <limits>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <memory>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

// ---------------------------------------------------------------------------
// 角色与容器
// ---------------------------------------------------------------------------

// 结构容器：只负责分组，本身不是可排序的内容。
//
// 为什么把 Sect 算容器而不是内容：Sect 是「一节」的壳，用户心智里的
// 「一段 / 一个标题 / 一张表 / 一张图」都在它下面；重排容器没有意义。
bool isContainerTag(const QString& tag) {
    static const QSet<QString> kContainers = {
        QStringLiteral("Document"), QStringLiteral("Part"),     QStringLiteral("Sect"),
        QStringLiteral("Div"),     QStringLiteral("Art"),      QStringLiteral("BlockQuote"),
        QStringLiteral("TOC"),     QStringLiteral("TOCI"),     QStringLiteral("Index"),
        QStringLiteral("NonStruct"), QStringLiteral("Private"),
    };
    return kContainers.contains(tag);
}

// 文字预览的长度上限。
//
// 为什么截断：结构树里可能有整页那么长的段落，全塞进列表会让界面没法看。
constexpr int kPreviewLimit = 120;

// 「这个位置上没有单元」用的哨兵值（比任何真实位置都大）。
constexpr int kNoPosition = std::numeric_limits<int>::max();

QString previewText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.size() <= kPreviewLimit) return trimmed;
    return trimmed.left(kPreviewLimit) + QStringLiteral("…");
}

// ---------------------------------------------------------------------------
// 结构树视图
// ---------------------------------------------------------------------------

// 从读回的结构树里整理出来的东西：父子关系、每页的可排序单元。
struct TreeView {
    // 元素下标 → 子元素下标（树序）。
    QVector<QVector<int>> children;
    // 元素下标 → 它是不是可排序单元。
    QSet<int> unitSet;
    // 元素下标 → 它是不是「引用元素」（没有 /S 的伪元素：/OBJR 或表单域 widget）。
    QSet<int> referenceSet;
    // 出错时的中文原因。
    QString error;
};

// 一棵结构树的视图。
//
// 什么叫「可排序单元」：父元素是结构容器（Part / Sect / Div…）的那个内容元素。
// 更深的子结构（LI、TR、TD…）跟着它的父单元走——表格整张是一个单元，
// 把表头格单独拎出来重排，表格就散了。
TreeView buildTreeView(const StructTreeReport& report) {
    TreeView view;
    view.children.resize(report.elements.size());
    for (int i = 0; i < report.elements.size(); ++i) {
        const int parent = report.elements.at(i).parentIndex;
        if (parent >= 0 && parent < view.children.size()) {
            view.children[parent].append(i);
        }
    }

    for (int i = 0; i < report.elements.size(); ++i) {
        const StructElement& element = report.elements.at(i);
        if (element.tag.isEmpty()) {
            // 没有 /S 的元素是「引用」：图像的 /OBJR 字典、或者表单域的 widget 注解。
            //
            // 为什么跳过而不是整篇拒绝：这两类东西本来就**不是**内容元素，
            // 它们是挂在某个元素 /K 上的引用。整篇拒绝会让**含图或含表单域的文档
            // 一页都调不了顺序**——那不是「不支持表单域」，那是「不支持有图的文件」。
            //
            // 它们的引用会被原样搬进新写的结构树（见 collectContentRefs）。
            view.referenceSet.insert(i);
            continue;
        }
        if (isContainerTag(element.tag)) continue;
        const int parent = element.parentIndex;
        const bool parentIsContainer =
            parent < 0 || (parent < report.elements.size() &&
                           isContainerTag(report.elements.at(parent).tag));
        if (!parentIsContainer) continue;  // 单元内部的子结构，跟着单元走
        if (element.pageIndex < 0) continue;  // 不挂在任何页上的内容不参与排序

        view.unitSet.insert(i);
    }
    return view;
}

// 一个单元（含它的子结构）的文字。
QString subtreeText(int index, const QVector<QVector<int>>& children,
                     const QVector<StructElement>& elements, int depth = 0) {
    if (index < 0 || index >= elements.size() || depth > 512) return QString();
    QString text = elements.at(index).text;
    for (int child : children.at(index)) {
        text += subtreeText(child, children, elements, depth + 1);
    }
    return text;
}

// 找到结构树的根节点。
//
// 返回 -1 表示没有根（-2 表示有多个根）。
//
// 为什么这么严：结构树有多个根时，「阅读顺序」根本没有唯一定义——
// 每一棵子树都是一棵独立的树，读屏软件念哪一棵都不算错。
constexpr int kNoRoot = -1;
constexpr int kMultipleRoots = -2;

int findRootElement(const StructTreeReport& report) {
    int root = kNoRoot;
    for (int i = 0; i < report.elements.size(); ++i) {
        if (report.elements.at(i).parentIndex >= 0) continue;
        if (root != kNoRoot) return kMultipleRoots;
        root = i;
    }
    return root;
}

// 单元在树里的先序（读屏软件念的顺序）。
//
// 为什么不用「按页分组再拼」：跨页移动之后，某一页的元素在阅读顺序里
// 会与别的页交错，按页分组再拼起来就还原不出原来的顺序。
QVector<int> unitPreOrder(const StructTreeReport& report, const TreeView& view, int root) {
    QVector<int> result;
    if (root < 0) return result;
    QVector<int> stack;
    stack.append(root);
    while (!stack.isEmpty()) {
        const int current = stack.takeLast();
        if (view.unitSet.contains(current)) result.append(current);
        const QVector<int>& children = view.children.at(current);
        for (int i = children.size() - 1; i >= 0; --i) stack.append(children.at(i));
    }
    return result;
}

// ---------------------------------------------------------------------------
// 内容流里的标记内容
// ---------------------------------------------------------------------------

int previousSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index - 1; i >= 0; --i) {
        if (tokens.at(i).type == ContentTokenType::Whitespace) continue;
        if (tokens.at(i).type == ContentTokenType::Comment) continue;
        return i;
    }
    return -1;
}

// 在 token 区间里找字典的某个键对应的值记号。
int findDictValue(const QVector<ContentToken>& tokens, int start, int end, const QByteArray& key) {
    for (int i = start; i < end; ++i) {
        if (tokens.at(i).type != ContentTokenType::Name) continue;
        if (tokens.at(i).raw != key) continue;
        for (int j = i + 1; j < end; ++j) {
            if (tokens.at(j).type == ContentTokenType::Whitespace) continue;
            if (tokens.at(j).type == ContentTokenType::Comment) continue;
            return j;
        }
        return -1;
    }
    return -1;
}

// 一页的内容流里，MCID → 它包住的显示操作符下标。
//
// 为什么按「显示操作符下标」对齐而不是按文字对齐：
// 内容流里的标记内容与扫描出来的文本运行是**同一批显示操作符**
// （顺序一致、数量一致），按下标对齐就不依赖两套解码器给出同样的文字——
// 解码有差异时也不会把文字配错元素。按文字对齐会在字体编码特殊时错位。
QHash<int, QVector<int>> scanMarkedContentRuns(const QByteArray& content, int* displayOpCount,
                                                QString* error) {
    QHash<int, QVector<int>> result;
    const ContentParseResult parsed = ContentStream::tokenize(content);
    if (!parsed.isValid()) {
        if (error) *error = parsed.error;
        return result;
    }
    const QVector<ContentToken>& tokens = parsed.tokens;

    struct Frame {
        int mcid = -1;
        QVector<int> ops;
    };
    QVector<Frame> stack;
    int opCount = 0;

    for (int i = 0; i < tokens.size(); ++i) {
        const ContentToken& token = tokens.at(i);
        if (token.type != ContentTokenType::Operator) continue;
        const QByteArray& op = token.raw;

        if (op == "BDC" || op == "BMC") {
            int mcid = -1;
            if (op == "BDC") {
                const int propsIndex = previousSignificant(tokens, i);
                if (propsIndex >= 0 && tokens.at(propsIndex).type == ContentTokenType::DictionaryEnd) {
                    int depth = 0;
                    int dictStart = -1;
                    for (int j = propsIndex; j >= 0; --j) {
                        if (tokens.at(j).type == ContentTokenType::DictionaryEnd) {
                            ++depth;
                        } else if (tokens.at(j).type == ContentTokenType::DictionaryStart) {
                            --depth;
                            if (depth == 0) {
                                dictStart = j;
                                break;
                            }
                        }
                    }
                    if (dictStart >= 0) {
                        const int mcidIndex =
                            findDictValue(tokens, dictStart + 1, propsIndex, "/MCID");
                        if (mcidIndex >= 0 && tokens.at(mcidIndex).type == ContentTokenType::Number) {
                            mcid = tokens.at(mcidIndex).raw.toInt();
                        }
                    }
                }
            }
            stack.append({mcid, QVector<int>()});
            continue;
        }

        if (op == "EMC") {
            if (stack.isEmpty()) continue;
            const Frame frame = stack.takeLast();
            if (frame.mcid < 0) continue;
            if (result.contains(frame.mcid)) {
                if (error && error->isEmpty()) {
                    *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                      "内容流里 MCID %1 出现了两次，无法确定它对应哪段文字。")
                                 .arg(frame.mcid);
                }
                continue;
            }
            result.insert(frame.mcid, frame.ops);
            continue;
        }

        if (op == "Tj" || op == "TJ" || op == "'" || op == "\"") {
            const int index = opCount++;
            // 嵌套的标记内容：里层的显示操作符也属于外层。
            for (Frame& frame : stack) frame.ops.append(index);
        }
    }
    if (displayOpCount) *displayOpCount = opCount;
    return result;
}

#ifdef AIPDF_WITH_QPDF
// 每一页的内容流（按页顺序拼起来）。
QVector<QByteArray> pageContentStreams(const QByteArray& pdfBytes, QString* error) {
    QVector<QByteArray> result;
    try {
        QPDF pdf;
        pdf.processMemoryFile("semantic-order-content.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        for (const QPDFObjectHandle& pageHandle : pages) {
            QByteArray content;
            // qpdf 的取内容流接口不是 const 方法，所以句柄要按值取一份可改的。
            QPDFObjectHandle page = pageHandle;
            const std::vector<QPDFObjectHandle> streams = page.getPageContents();
            for (const QPDFObjectHandle& streamHandle : streams) {
                QPDFObjectHandle stream = streamHandle;
                if (!stream.isStream()) continue;
                const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
                if (!data) continue;
                content += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                      static_cast<qsizetype>(data->getSize()));
                content += '\n';
            }
            result.append(content);
        }
    } catch (const std::exception& failure) {
        if (error) *error = QString::fromUtf8(failure.what());
        return {};
    }
    return result;
}
#endif

// ---------------------------------------------------------------------------
// 图像（/OBJR）与表单域 widget 的引用
// ---------------------------------------------------------------------------
//
// 语义树里的「非文字内容」是挂在元素 /K 上的两类引用：
//   * 图像：/K 里放 << /Type /OBJR /Obj x 0 R /Pg p 0 R >>；
//   * 表单域：/K 里放 widget 注解的字典。
//
// 共用的读结构树件只带角色与 MCID，看不见这两样；而写手要靠它们把图像与
// 表单域挂回结构树——不复制就会在重写时**悄悄丢掉**图像与表单域。
//
// 走一遍结构树的结果：每个元素的「非文字内容」引用。
struct ContentRefs {
    // 元素下标 → 图像引用（/OBJR 指向的对象）。
    QVector<QVector<ObjectRef>> objectRefs;
    // 元素下标 → 表单域 widget 注解。
    QVector<QVector<ObjectRef>> annotations;
    // 走树时看到的元素（用来核对顺序）。
    QVector<QString> tags;
    QVector<int> parentIndexes;
    QVector<QVector<int>> mcids;
    // 看到了引用、但拿不到对象号（写不回去）。
    bool unresolved = false;
};

#ifdef AIPDF_WITH_QPDF

// 为什么自己再走一遍树：要把「哪个元素的 /K 里有哪些引用」对上号，只有走一遍
// 树才知道。这里完全照抄共用读件那套遍历规则（/K 的三种形态、/Parent 反查、
// 防环），元素顺序因此与它一致；走完还会逐条核对（角色 / 父下标 / MCID），
// 对不上就不写——宁可不做，也不能写出一份丢图像的树。

struct RefWalkContext {
    QHash<int, int> pageIndexById;                       // 页面对象号 → 页码
    QHash<int, QVector<int>> childrenByParent;            // 父对象号 → 子对象号
    QHash<int, QPDFObjectHandle> structElems;             // 对象号 → 元素
    QSet<int> visited;                                    // 已访问过的元素（防环）
    ContentRefs* refs = nullptr;
};

bool isObjectReference(const QPDFObjectHandle& node) {
    if (!node.isDictionary()) return false;
    const QPDFObjectHandle type = node.getKey("/Type");
    return type.isName() && type.getName() == "/OBJR";
}

bool isWidgetAnnotation(const QPDFObjectHandle& node) {
    if (!node.isDictionary()) return false;
    const QPDFObjectHandle subtype = node.getKey("/Subtype");
    return subtype.isName() && subtype.getName() == "/Widget";
}

// 这个孩子是引用的话，把引用记到它爸身上。
void collectChildReference(RefWalkContext& context, const QPDFObjectHandle& child, int parentIndex,
                           int inheritedPage) {
    if (isObjectReference(child)) {
        ObjectRef reference;
        const QPDFObjectHandle object = child.getKey("/Obj");
        if (object.isIndirect()) {
            reference.objectId = object.getObjectID();
            reference.generation = object.getGeneration();
        }
        int page = inheritedPage;
        const QPDFObjectHandle pageRef = child.getKey("/Pg");
        if (pageRef.isDictionary() && pageRef.isIndirect()) {
            const int found = context.pageIndexById.value(pageRef.getObjectID(), -1);
            if (found >= 0) page = found;
        }
        reference.pageIndex = page;
        if (reference.objectId < 0) {
            context.refs->unresolved = true;
            return;
        }
        context.refs->objectRefs[parentIndex].append(reference);
        return;
    }
    if (isWidgetAnnotation(child)) {
        ObjectRef reference;
        if (child.isIndirect()) {
            reference.objectId = child.getObjectID();
            reference.generation = child.getGeneration();
        }
        reference.pageIndex = inheritedPage;
        if (reference.objectId < 0) {
            context.refs->unresolved = true;
            return;
        }
        context.refs->annotations[parentIndex].append(reference);
        return;
    }
    // 子结构元素（带 /S）：不是引用，交给遍历处理。
    const QPDFObjectHandle tag = child.getKey("/S");
    if (tag.isName() || tag.isArray()) return;
    // 认不出来的引用（例如可选内容组 OCG）：写手抄不回去，
    // 悄悄丢掉就是丢内容——宁可不做。
    context.refs->unresolved = true;
}

void traverseReferenceElement(RefWalkContext& context, const QPDFObjectHandle& node, int parentIndex,
                              int depth, int inheritedPage) {
    if (!node.isDictionary()) return;
    if (depth > 512) return;
    if (context.refs->tags.size() >= 200000) return;

    const bool indirect = node.isIndirect();
    const int id = indirect ? node.getObjectID() : 0;
    if (indirect) {
        if (context.visited.contains(id)) return;
        context.visited.insert(id);
    }

    QString tag;
    const QPDFObjectHandle tagValue = node.getKey("/S");
    if (tagValue.isName()) {
        tag = QString::fromLatin1(tagValue.getName().c_str() + 1);
    } else if (tagValue.isArray()) {
        for (const QPDFObjectHandle& item : tagValue.getArrayAsVector()) {
            if (!item.isName()) continue;
            tag = QString::fromLatin1(item.getName().c_str() + 1);
            break;
        }
    }

    int page = inheritedPage;
    const QPDFObjectHandle pageRef = node.getKey("/Pg");
    if (pageRef.isDictionary() && pageRef.isIndirect()) {
        const int found = context.pageIndexById.value(pageRef.getObjectID(), -1);
        if (found >= 0) page = found;
    }

    const int index = context.refs->tags.size();
    context.refs->tags.append(tag);
    context.refs->parentIndexes.append(parentIndex);
    context.refs->mcids.append(QVector<int>());
    context.refs->objectRefs.append(QVector<ObjectRef>());
    context.refs->annotations.append(QVector<ObjectRef>());

    const QPDFObjectHandle kids = node.getKey("/K");
    bool hasKids = false;
    if (kids.isDictionary()) {
        hasKids = true;
        collectChildReference(context, kids, index, page);
        traverseReferenceElement(context, kids, index, depth + 1, page);
    } else if (kids.isNumber()) {
        hasKids = true;
        context.refs->mcids[index].append(static_cast<int>(kids.getIntValue()));
    } else if (kids.isArray()) {
        hasKids = true;
        for (const QPDFObjectHandle& item : kids.getArrayAsVector()) {
            if (item.isDictionary()) {
                collectChildReference(context, item, index, page);
                traverseReferenceElement(context, item, index, depth + 1, page);
            } else if (item.isNumber()) {
                context.refs->mcids[index].append(static_cast<int>(item.getIntValue()));
            }
        }
    }

    // 没有 /K 的元素：有些文件只靠 /Parent 串层级，用反向索引把它的孩子找回来。
    if (!hasKids && indirect) {
        const QVector<int> childIds = context.childrenByParent.value(id);
        for (int childId : childIds) {
            const QPDFObjectHandle child = context.structElems.value(childId);
            if (!child.isDictionary()) continue;
            traverseReferenceElement(context, child, index, depth + 1, page);
        }
    }
}

// 读出每个元素的图像 / 表单域引用。
//
// 返回 false 表示读不了（这份文档不该被改写）。
bool collectContentRefs(const QByteArray& pdfBytes, const StructTreeReport& report, ContentRefs* refs,
                        QString* error) {
    try {
        QPDF pdf;
        pdf.processMemoryFile("semantic-order-refs.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            if (error) *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "PDF 的根对象不是字典");
            return false;
        }
        const QPDFObjectHandle root = catalog.getKey("/StructTreeRoot");
        if (!root.isDictionary()) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "这份文档没有标签树根对象");
            }
            return false;
        }

        RefWalkContext context;
        context.refs = refs;
        const std::vector<QPDFObjectHandle> allPages = pdf.getAllPages();
        for (size_t i = 0; i < allPages.size(); ++i) {
            if (allPages.at(i).isIndirect()) {
                context.pageIndexById.insert(allPages.at(i).getObjectID(), static_cast<int>(i));
            }
        }
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
            traverseReferenceElement(context, kids, -1, 0, -1);
        } else if (kids.isArray()) {
            for (const QPDFObjectHandle& item : kids.getArrayAsVector()) {
                if (!item.isDictionary()) continue;
                traverseReferenceElement(context, item, -1, 0, -1);
            }
        }

        // 核对：走出来的顺序必须与共用读件给的一致，否则说明两边的遍历规则
        // 在这份文档上分叉了——这时引用可能挂错元素，宁可不做。
        const int elementCount = static_cast<int>(report.elements.size());
        if (static_cast<int>(refs->tags.size()) != elementCount) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                  "读结构树时元素顺序对不上，无法安全重排，文档没有修改。");
            }
            return false;
        }
        for (int i = 0; i < elementCount; ++i) {
            const StructElement& element = report.elements.at(i);
            if (refs->tags.at(i) != element.tag ||
                refs->parentIndexes.at(i) != element.parentIndex ||
                refs->mcids.at(i) != element.mcids) {
                if (error) {
                    *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                      "读结构树时元素顺序对不上，无法安全重排，文档没有修改。");
                }
                return false;
            }
        }
        return true;
    } catch (const std::exception& failure) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "读取结构树引用失败：%1")
                         .arg(QString::fromUtf8(failure.what()));
        }
        return false;
    }
}
#endif  // AIPDF_WITH_QPDF

// ---------------------------------------------------------------------------
// 顺序的对号与校验
// ---------------------------------------------------------------------------

// 把传入的槽位摊平成一条全局序列，并校验它与文档里的单元一一对应。
//
// 口径（比「每页个数相等」宽，但一样严）：
//   * 文档里每个单元在序列里**恰好出现一次**——少一个就是丢一段内容，
//     多一个就是凭空多出一段；
//   * 序列里不能有文档里没有的元素；
//   * 元素**可以来自别的槽位**（跨页移动是合法的）：
//     槽位是「阅读位置」，元素的内容仍然在它自己的物理页上。
bool collectSequence(const QVector<SemanticOrderSlot>& order, const TreeView& view,
                     const StructTreeReport& report, QVector<int>* sequence,
                     QHash<int, int>* slotOfUnit, QString* error) {
    sequence->clear();
    slotOfUnit->clear();
    QSet<int> used;
    for (int slot = 0; slot < order.size(); ++slot) {
        for (const SemanticOrderEntry& entry : order.at(slot).entries) {
            if (entry.nodeIndex < 0 || entry.nodeIndex >= report.elements.size() ||
                !view.unitSet.contains(entry.nodeIndex)) {
                if (error) {
                    *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                      "传入的顺序里有一个不属于这份文档的语义元素，文档没有修改。");
                }
                return false;
            }
            if (used.contains(entry.nodeIndex)) {
                if (error) {
                    *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                      "传入的顺序里有重复的语义元素，文档没有修改。");
                }
                return false;
            }
            used.insert(entry.nodeIndex);
            sequence->append(entry.nodeIndex);
            slotOfUnit->insert(entry.nodeIndex, slot);
        }
    }
    for (int unit : view.unitSet) {
        if (used.contains(unit)) continue;
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "语义元素的数量对不上（文档里 %1 个，传入 %2 个），文档没有修改。")
                         .arg(view.unitSet.size())
                         .arg(sequence->size());
        }
        return false;
    }
    return true;
}

// 每个元素的「角色 + 文字」签名（用来比写前写后有没有变）。
QStringList elementSignatures(const StructTreeReport& report) {
    QStringList signatures;
    signatures.reserve(report.elements.size());
    for (const StructElement& element : report.elements) {
        signatures.append(element.tag + QLatin1Char('\t') + element.text);
    }
    signatures.sort();
    return signatures;
}

// ---------------------------------------------------------------------------
// 目标树：单元进哪个容器、容器里的子节点怎么排
// ---------------------------------------------------------------------------

struct TargetTree {
    // 元素下标 → 新的父元素下标（只有搬了家的单元会变）。
    QHash<int, int> parentOf;
    // 新的子节点清单。
    QVector<QVector<int>> children;
};

// 按目标顺序重排结构树。
//
// 规则（跨页移动的核心）：
//   * 单元的**目标父容器**由它所在的槽位决定——槽位对应哪个容器，就进那个容器；
//   * 容器按「它底下第一个单元在全局序列里的位置」排，
//     容器内部的单元按全局序列排——这样树里的先序就等于用户要的顺序；
//   * 容器本身不搬家（搬容器等于改结构）。
TargetTree buildTargetTree(const StructTreeReport& report, const TreeView& view,
                            const QVector<SemanticOrderSlot>& order, const QVector<int>& sequence,
                            const QHash<int, int>& slotOfUnit) {
    TargetTree tree;
    tree.children = view.children;

    // 每个槽位的容器：槽位自己记着（见 SemanticOrderSlot::containerNodeIndex）。
    // 槽位没记（调用方自己拼的）时，退回「第一个单元原来的父」。
    QVector<int> containerOfSlot(order.size(), -1);
    for (int slot = 0; slot < order.size(); ++slot) {
        const int container = order.at(slot).containerNodeIndex;
        if (container >= 0 && container < report.elements.size()) containerOfSlot[slot] = container;
    }
    for (int slot = 0; slot < order.size(); ++slot) {
        if (containerOfSlot.at(slot) >= 0) continue;
        for (const SemanticOrderEntry& entry : order.at(slot).entries) {
            if (entry.nodeIndex < 0 || entry.nodeIndex >= report.elements.size()) continue;
            const int parent = report.elements.at(entry.nodeIndex).parentIndex;
            if (parent < 0 || parent >= report.elements.size()) continue;
            containerOfSlot[slot] = parent;
            break;
        }
    }

    QHash<int, int> globalPos;
    for (int i = 0; i < sequence.size(); ++i) globalPos.insert(sequence.at(i), i);

    // 每个容器底下的目标单元（按全局序列顺序）。
    QVector<QVector<int>> unitsOfContainer(report.elements.size());
    for (int unit : sequence) {
        const int slot = slotOfUnit.value(unit, -1);
        int container = -1;
        if (slot >= 0 && slot < containerOfSlot.size()) container = containerOfSlot.at(slot);
        if (container < 0) container = report.elements.at(unit).parentIndex;
        if (container < 0 || container >= report.elements.size()) continue;
        tree.parentOf.insert(unit, container);
        unitsOfContainer[container].append(unit);
    }

    // 容器下面的最小全局位置（用来把容器与单元按位置归并）。
    QVector<int> minPos(report.elements.size(), kNoPosition);
    for (int e = 0; e < report.elements.size(); ++e) {
        for (int unit : unitsOfContainer.at(e)) {
            minPos[e] = qMin(minPos[e], globalPos.value(unit, kNoPosition));
        }
    }
    // 原来在某个元素底下的单元的最小位置：单元搬家后，容器还留在原来的相对位置。
    QVector<int> originalMinPos(report.elements.size(), kNoPosition);
    for (int i = report.elements.size() - 1; i >= 0; --i) {
        int best = kNoPosition;
        if (view.unitSet.contains(i)) best = globalPos.value(i, kNoPosition);
        for (int child : view.children.at(i)) best = qMin(best, originalMinPos.at(child));
        originalMinPos[i] = best;
    }
    // 子容器把位置往上传。
    for (int i = report.elements.size() - 1; i >= 0; --i) {
        for (int child : view.children.at(i)) {
            if (view.unitSet.contains(child)) continue;
            if (view.referenceSet.contains(child)) continue;
            minPos[i] = qMin(minPos[i], minPos.at(child));
        }
    }

    // 重建每个元素的子节点清单：它自己的单元 + 原来的子容器，按全局位置归并。
    for (int e = 0; e < report.elements.size(); ++e) {
        QVector<QPair<int, int>> items;
        for (int unit : unitsOfContainer.at(e)) {
            items.append({globalPos.value(unit, kNoPosition), unit});
        }
        for (int child : view.children.at(e)) {
            if (view.unitSet.contains(child)) continue;
            if (view.referenceSet.contains(child)) continue;
            const int key = minPos.at(child) != kNoPosition ? minPos.at(child)
                                                            : originalMinPos.at(child);
            items.append({key, child});
        }
        std::stable_sort(items.begin(), items.end(),
                         [](const QPair<int, int>& left, const QPair<int, int>& right) {
                             return left.first < right.first;
                         });
        QVector<int> children;
        children.reserve(items.size());
        for (const QPair<int, int>& item : items) children.append(item.second);
        tree.children[e] = children;
    }
    return tree;
}

}  // namespace

// ---------------------------------------------------------------------------
// 读
// ---------------------------------------------------------------------------

QVector<SemanticOrderSlot> readSemanticReadingOrder(const QByteArray& input, QString* error) {
    if (error) error->clear();
    if (input.isEmpty()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "输入的 PDF 内容为空");
        }
        return {};
    }

    const StructTreeReport report = readStructTree(input);
    if (!report.error.isEmpty()) {
        if (error) *error = report.error;
        return {};
    }
    if (!report.hasStructTree) {
        // 没有语义树就说清楚为什么——不静默退化成行级顺序。
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "这份文档还没有语义标签树；先运行「自动添加文档标签」生成语义树");
        }
        return {};
    }

    const TreeView view = buildTreeView(report);
    if (!view.error.isEmpty()) {
        if (error) *error = view.error;
        return {};
    }

    // 槽位 = 结构树里的容器（Part / Sect）：一个容器底下的单元是一段连续的
    // 阅读位置。跨页移动之后，槽位里的元素可以来自别的页——
    // 但它内容所在的物理页不变（元素自己的 pageIndex）。
    const int root = findRootElement(report);
    if (root == kMultipleRoots) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "这份文档的语义树有多个根节点，无法确定阅读顺序。");
        }
        return {};
    }
    if (root == kNoRoot) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "这份文档的语义树没有根节点，无法确定阅读顺序。");
        }
        return {};
    }

    QVector<SemanticOrderSlot> positions;
    QHash<int, int> slotOfContainer;
    for (int unit : unitPreOrder(report, view, root)) {
        const int container = report.elements.at(unit).parentIndex;
        if (container < 0 || container >= report.elements.size()) continue;
        int slot = -1;
        const auto found = slotOfContainer.constFind(container);
        if (found == slotOfContainer.constEnd()) {
            slot = positions.size();
            SemanticOrderSlot created;
            created.pageIndex = report.elements.at(container).pageIndex;
            if (created.pageIndex < 0) created.pageIndex = report.elements.at(unit).pageIndex;
            created.containerNodeIndex = container;
            positions.append(created);
            slotOfContainer.insert(container, slot);
        } else {
            slot = found.value();
        }

        const StructElement& element = report.elements.at(unit);
        SemanticOrderEntry entry;
        entry.pageIndex = element.pageIndex;
        entry.role = element.tag;
        entry.text = previewText(subtreeText(unit, view.children, report.elements));
        entry.mcids = element.mcids;
        entry.nodeIndex = unit;
        positions[slot].entries.append(entry);
    }
    return positions;
}

// ---------------------------------------------------------------------------
// 写
// ---------------------------------------------------------------------------

QByteArray writeSemanticReadingOrder(const QByteArray& input,
                                      const QVector<SemanticOrderSlot>& order,
                                      QString* error) {
    if (error) error->clear();
    if (input.isEmpty()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "输入的 PDF 内容为空");
        }
        return {};
    }

    const StructTreeReport report = readStructTree(input);
    if (!report.error.isEmpty()) {
        if (error) *error = report.error;
        return {};
    }
    if (!report.hasStructTree) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "这份文档还没有语义标签树；先运行「自动添加文档标签」生成语义树");
        }
        return {};
    }

    const TreeView view = buildTreeView(report);
    if (!view.error.isEmpty()) {
        if (error) *error = view.error;
        return {};
    }

    // ---- 0. 图像与表单域的引用：重写时一起搬走 ----
    //
    // 为什么必须先取：写手是**重建**整棵结构树的，
    // 不把 /OBJR 与 widget 的引用抄进计划，图像与表单域就会被悄悄丢掉。
#ifdef AIPDF_WITH_QPDF
    ContentRefs refs;
    QString refsError;
    if (!collectContentRefs(input, report, &refs, &refsError)) {
        if (error) *error = refsError;
        return {};
    }
    if (refs.unresolved) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "语义树里的图像或表单域引用找不到对应的对象，"
                              "无法安全重排，文档没有修改。");
        }
        return {};
    }
#else
    ContentRefs refs;
    refs.objectRefs.resize(report.elements.size());
    refs.annotations.resize(report.elements.size());
#endif

    // ---- 1. 对号：把传入的顺序与当前的单元对上 ----
    QVector<int> sequence;
    QHash<int, int> slotOfUnit;
    if (!collectSequence(order, view, report, &sequence, &slotOfUnit, error)) {
        return {};
    }

    // ---- 2. 目标树：单元进哪个容器、容器里怎么排 ----
    const TargetTree target = buildTargetTree(report, view, order, sequence, slotOfUnit);

    // ---- 3. 根节点 ----
    const int rootElement = findRootElement(report);
    if (rootElement == kMultipleRoots) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "这份文档的语义树有多个根节点，无法安全重排，文档没有修改。");
        }
        return {};
    }
    if (rootElement == kNoRoot) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "这份文档的语义树没有根节点，无法安全重排，文档没有修改。");
        }
        return {};
    }

    // ---- 4. MCID → 文本运行 ----
    //
    // 写手是按「块里的文本运行」发 MCID 的，所以要知道每个 MCID 对应哪些运行，
    // 才能保证「顺序变了、文字还在原来的元素上」。
    ScannedDocument scan;
    QString scanError;
    if (!scanDocumentContent(input, &scan, &scanError)) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "扫描文档内容失败：%1")
                         .arg(scanError);
        }
        return {};
    }

    QVector<QHash<int, QVector<int>>> runsOfMcid;
#ifdef AIPDF_WITH_QPDF
    QString contentError;
    const QVector<QByteArray> contents = pageContentStreams(input, &contentError);
    if (contents.isEmpty() && !contentError.isEmpty()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder", "读取页面内容流失败：%1")
                         .arg(contentError);
        }
        return {};
    }
    runsOfMcid.resize(contents.size());
    for (int pageIndex = 0; pageIndex < contents.size(); ++pageIndex) {
        int opCount = 0;
        QString markedError;
        const QHash<int, QVector<int>> marked =
            scanMarkedContentRuns(contents.at(pageIndex), &opCount, &markedError);
        if (!markedError.isEmpty()) {
            if (error) *error = markedError;
            return {};
        }
        const int runCount = pageIndex < scan.pages.size() ? scan.pages.at(pageIndex).runs.size() : 0;
        if (opCount != runCount) {
            // 数量对不上说明内容流里有扫描器没认出来的显示操作符，
            // 这时按下标配对会把文字配错元素——宁可不做。
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                  "第 %1 页的内容流与扫描结果对不上（%2 个显示操作符 / %3 个文本运行），"
                                  "无法安全重排，文档没有修改。")
                             .arg(pageIndex + 1)
                             .arg(opCount)
                             .arg(runCount);
            }
            return {};
        }
        runsOfMcid[pageIndex] = marked;
    }
#else
    if (error) {
        *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                          "当前构建没有启用 qpdf，无法重排语义阅读顺序");
    }
    return {};
#endif

    // ---- 5. 组装写手计划 ----
    StructWritePlan plan;
    plan.rootIndex = 0;
    plan.documentLanguage = report.hasDocumentLanguage ? report.documentLanguage : QString();
    plan.displayDocTitle = report.hasDisplayDocTitle ? report.displayDocTitle : false;
    // 语义树与 Tab 顺序必须一致（PDF/UA-1 §7.4），写的时候一起更新。
    plan.writeTabsStructureOrder = true;

    const int pageCount = static_cast<int>(std::max(runsOfMcid.size(), scan.pages.size()));
    plan.pageBlockRuns.resize(std::max(pageCount, 1));

    QVector<int> planIndexOfElement(report.elements.size(), -1);
    // 每一页被语义元素认领走的文本运行。
    QVector<QSet<int>> claimedRuns(static_cast<int>(plan.pageBlockRuns.size()));

    struct Frame {
        int element = -1;
        int cursor = 0;
    };
    QVector<Frame> stack;
    stack.append({rootElement, 0});

    while (!stack.isEmpty()) {
        const int current = stack.last().element;
        const bool entering = stack.last().cursor == 0;
        if (entering) {
            const StructElement& element = report.elements.at(current);
            StructElementPlan planElement;
            planElement.type = element.tag;
            planElement.title = element.title;
            planElement.alt = element.alt;
            planElement.actualText = element.actualText;
            planElement.language = element.language;
            // /Pg 与 MCID 都按**物理页**取：跨页移动只改阅读位置，
            // 元素的内容仍然留在它原来的那一页上。
            planElement.pageIndex = element.pageIndex;
            if (element.tag == QLatin1String("TH")) {
                // 表头格按列覆盖（与共用写手对表头格的默认处理一致）。
                planElement.scope = QStringLiteral("Column");
            }
            const int newParent = target.parentOf.value(current, element.parentIndex);
            if (newParent >= 0 && newParent < report.elements.size()) {
                planElement.parentIndex = planIndexOfElement.at(newParent);
            }
            // 图像（/OBJR）与表单域 widget：引用一起搬走，重写后还在。
            if (current < refs.objectRefs.size()) {
                planElement.objectRefs = refs.objectRefs.at(current);
                planElement.annotations = refs.annotations.at(current);
            }

            // 内容块：元素自己的 MCID → 文本运行。
            if (!element.mcids.isEmpty()) {
                if (element.pageIndex < 0 || element.pageIndex >= runsOfMcid.size() ||
                    element.pageIndex >= plan.pageBlockRuns.size()) {
                    if (error) {
                        *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                          "语义树里有元素没有页码，无法安全重排，文档没有修改。");
                    }
                    return {};
                }
                const QHash<int, QVector<int>>& marked = runsOfMcid.at(element.pageIndex);
                QVector<int> runs;
                for (int mcid : element.mcids) {
                    const auto found = marked.constFind(mcid);
                    if (found == marked.constEnd()) {
                        if (error) {
                            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                              "第 %1 页的 MCID %2 在内容流里找不到对应的标记内容，"
                                              "无法安全重排，文档没有修改。")
                                         .arg(element.pageIndex + 1)
                                         .arg(mcid);
                        }
                        return {};
                    }
                    runs += found.value();
                }
                const int local = plan.pageBlockRuns.at(element.pageIndex).size();
                plan.pageBlockRuns[element.pageIndex].append(runs);
                planElement.blockIndices.append(local);
                for (int run : runs) {
                    claimedRuns[element.pageIndex].insert(run);
                }
            }

            planIndexOfElement[current] = plan.elements.size();
            plan.elements.append(planElement);
        }

        const QVector<int>& children = target.children.at(current);
        if (stack.last().cursor < children.size()) {
            const int child = children.at(stack.last().cursor);
            ++stack.last().cursor;
            stack.append({child, 0});
            continue;
        }
        stack.removeLast();
    }

    // 每一处文字都要有归属：没被任何语义元素认领的运行，写手会给它发一个
    // 却没有结构元素的 MCID，/ParentTree 里那一格是空的——qpdf 写不出这种
    // 对象图。与其让写手报一句看不懂的错，不如这里说清楚是哪一页对不上。
    for (int pageIndex = 0; pageIndex < scan.pages.size(); ++pageIndex) {
        const int total = scan.pages.at(pageIndex).runs.size();
        const int claimed = pageIndex < claimedRuns.size() ? claimedRuns.at(pageIndex).size() : 0;
        if (claimed == total) continue;
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "第 %1 页有 %2 处文字不归属任何语义元素（结构树与内容流对不上），"
                              "无法安全重排，文档没有修改。")
                         .arg(pageIndex + 1)
                         .arg(total - claimed);
        }
        return {};
    }

    // 子节点清单要等子元素建好之后才准（写手按它写 /K），这里按新顺序补一次。
    {
        QVector<Frame> walk;
        walk.append({rootElement, 0});
        while (!walk.isEmpty()) {
            const int current = walk.last().element;
            const QVector<int>& children = target.children.at(current);
            if (walk.last().cursor < children.size()) {
                const int child = children.at(walk.last().cursor);
                ++walk.last().cursor;
                const int parentPlan = planIndexOfElement.value(current, -1);
                const int childPlan = planIndexOfElement.value(child, -1);
                if (parentPlan >= 0 && childPlan >= 0) {
                    plan.elements[parentPlan].childIndices.append(childPlan);
                }
                walk.append({child, 0});
                continue;
            }
            walk.removeLast();
        }
    }

    // ---- 6. 写 ----
    QString writeError;
    const QByteArray written = writeStructPlan(input, plan, &writeError);
    if (written.isEmpty()) {
        if (error) {
            *error = writeError.isEmpty()
                         ? i18n::tr("aipdf::pdf::SemanticReadingOrder", "写语义标签树失败，文档没有修改。")
                         : writeError;
        }
        return {};
    }

    // ---- 7. 读回校验：角色与文字必须与写之前一致，只是顺序变了 ----
    const StructTreeReport after = readStructTree(written);
    if (!after.error.isEmpty()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "写回后的语义树读不出来（%1），文档没有修改。")
                         .arg(after.error);
        }
        return {};
    }
    if (after.elements.size() != report.elements.size()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "写回后的语义元素数量对不上（写之前 %1 个，写之后 %2 个），"
                              "文档没有修改。")
                         .arg(report.elements.size())
                         .arg(after.elements.size());
        }
        return {};
    }
    if (elementSignatures(after) != elementSignatures(report)) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "写回后有语义元素的角色或文字变了，文档没有修改。");
        }
        return {};
    }

    // 读回后的**全局顺序**必须与要求的一致。
    //
    // 为什么不再按物理页分页比对：跨页移动之后，元素的内容还在原来的物理页上，
    // 但它在阅读顺序里的位置换了——按页比对会把「位置」当成「页」，
    // 于是每一次合法的跨页移动都会被判成失败。
    const TreeView afterView = buildTreeView(after);
    if (!afterView.error.isEmpty()) {
        if (error) *error = afterView.error;
        return {};
    }
    const QVector<int> afterUnits = unitPreOrder(after, afterView, findRootElement(after));
    if (afterUnits.size() != sequence.size()) {
        if (error) {
            *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                              "写回后的语义元素数量对不上（要求 %1 个，实际 %2 个），文档没有修改。")
                         .arg(sequence.size())
                         .arg(afterUnits.size());
        }
        return {};
    }
    for (int i = 0; i < sequence.size(); ++i) {
        const StructElement& expected = report.elements.at(sequence.at(i));
        const StructElement& got = after.elements.at(afterUnits.at(i));
        if (expected.tag != got.tag ||
            subtreeText(sequence.at(i), view.children, report.elements) !=
                subtreeText(afterUnits.at(i), afterView.children, after.elements)) {
            if (error) {
                *error = i18n::tr("aipdf::pdf::SemanticReadingOrder",
                                  "写回后的语义顺序与要求的不一致，文档没有修改。");
            }
            return {};
        }
    }

    return written;
}

}  // namespace aipdf::pdf
