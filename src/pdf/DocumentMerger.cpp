#include "pdf/DocumentMerger.h"

#include <QVector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>
#include <string>
#include <vector>
#endif

namespace aipdf::pdf {

int mergedPageIndex(const QVector<int>& pageOffsets, int sourceIndex, int sourcePage) {
    return mergedPageIndex(pageOffsets, sourceIndex, sourcePage, -1);
}

int mergedPageIndex(const QVector<int>& pageOffsets,
                    int sourceIndex,
                    int sourcePage,
                    int totalPages) {
    if (sourceIndex < 0 || sourceIndex >= pageOffsets.size()) return -1;
    if (sourcePage < 0) return -1;
    const int base = pageOffsets.at(sourceIndex);
    if (base < 0) return -1;
    const int target = base + sourcePage;
    // 页码不能越过下一份文件的起点：越过去说明这个页号根本不属于这份源文件。
    if (sourceIndex + 1 < pageOffsets.size() && target >= pageOffsets.at(sourceIndex + 1)) return -1;
    // 知道总页数时，最后一份文件的越界也判得出来。
    if (totalPages >= 0 && target >= totalPages) return -1;
    return target;
}

#ifdef AIPDF_WITH_QPDF

namespace {

// 大纲递归深度与条目数的上限。
//
// 为什么要有上限：坏文件里的大纲可能有环（/First 指回祖先），
// 不设限就会把自己绕死在一个死循环里。
constexpr int kMaxOutlineDepth = 64;
constexpr int kMaxOutlineNodes = 20000;

// 一棵大纲节点（源文件里的一条书签）。
struct OutlineNode {
    QString title;
    int level = 0;        // 层级：0 = 顶层
    int sourcePage = -1;    // 指向源文件里的第几页（0 基），-1 = 解析不出来
    QString uri;            // 外部跳转（URI 动作）的目标
    QVector<int> children;  // 子节点在 OutlineTree::nodes 里的下标
    // 目的地数组里页对象之外的部分（视图参数）。
    QVector<QPDFObjectHandle> destTail;
};

// 一份源文件的大纲树。
struct OutlineTree {
    QVector<OutlineNode> nodes;
    QVector<int> topLevel;  // 顶层节点在 nodes 里的下标
};

// 合并结果里的大纲节点。
struct MergedNode {
    QString title;
    int targetPage = -1;  // 合并结果里的页号（0 基），-1 = 没有可用目标
    QString uri;
    int parent = -1;  // 父节点在 MergedNode 数组里的下标，-1 = 顶层
    QVector<int> children;
    QPDFObjectHandle object;  // 目标文档里的间接对象
    // 目的地数组里页对象之外的部分（视图参数：/Fit、/XYZ left top zoom……）。
    QVector<QPDFObjectHandle> destTail;
};

// 目的地数组的第一个元素是页对象时，返回它在 pages 里的下标；找不到返回 -1。
//
// 为什么按对象号（而不是按内容）比对：页对象在源文件里是间接对象，
// 引用它的地方也是同一个对象号；按内容比没有意义。
int pageIndexOfDestination(const QPDFObjectHandle& destination,
                            const std::vector<QPDFObjectHandle>& pages) {
    if (!destination.isArray()) return -1;
    const std::vector<QPDFObjectHandle> items = destination.getArrayAsVector();
    if (items.empty()) return -1;
    const QPDFObjectHandle first = items.front();
    if (!first.isDictionary() || !first.isIndirect()) return -1;
    const int id = first.getObjectID();
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].isIndirect() && pages[i].getObjectID() == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// 在名字树（/Names /Dests）里找一个名字目的地。
int pageIndexInNameTree(const QPDFObjectHandle& node,
                        const QString& name,
                        const std::vector<QPDFObjectHandle>& pages,
                        int depth) {
    if (depth > 32 || !node.isDictionary()) return -1;
    const QPDFObjectHandle names = node.getKey("/Names");
    if (names.isArray()) {
        const std::vector<QPDFObjectHandle> items = names.getArrayAsVector();
        // 名字树的 /Names 是「键 值 键 值……」的交替序列。
        for (size_t i = 0; i + 1 < items.size(); i += 2) {
            const QPDFObjectHandle key = items[i];
            if (!key.isString()) continue;
            if (QString::fromUtf8(key.getUTF8Value().c_str()) != name) continue;
            return pageIndexOfDestination(items[i + 1], pages);
        }
    }
    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            const int found = pageIndexInNameTree(kid, name, pages, depth + 1);
            if (found >= 0) return found;
        }
    }
    return -1;
}

// 名字目的地（/Dest (chapter1) 这种写法）：先看 catalog 的 /Dests，再看名字树。
int pageIndexOfNamedDestination(QPDF& pdf,
                                const QString& name,
                                const std::vector<QPDFObjectHandle>& pages) {
    if (name.isEmpty()) return -1;
    const QPDFObjectHandle dests = pdf.getRoot().getKey("/Dests");
    if (dests.isDictionary()) {
        const QPDFObjectHandle found = dests.getKey(name.toUtf8().constData());
        if (found.isArray()) return pageIndexOfDestination(found, pages);
    }
    const QPDFObjectHandle names = pdf.getRoot().getKey("/Names");
    if (names.isDictionary()) {
        return pageIndexInNameTree(names.getKey("/Dests"), name, pages, 0);
    }
    return -1;
}

// 解析一条书签的目标：返回源文件里的页号；解析不出来返回 -1。
//
// 两种写法都要认：条目直接写 /Dest，或写 /A 动作（/GoTo）。
// 外部跳转（/A /URI）写进 uriOut，页号没有意义。
int resolveItemDestination(QPDF& pdf,
                           const QPDFObjectHandle& item,
                           const std::vector<QPDFObjectHandle>& pages,
                           QString* uriOut) {
    const QPDFObjectHandle dest = item.getKey("/Dest");
    if (dest.isArray()) return pageIndexOfDestination(dest, pages);
    if (dest.isString()) {
        return pageIndexOfNamedDestination(pdf, QString::fromUtf8(dest.getUTF8Value().c_str()), pages);
    }
    const QPDFObjectHandle action = item.getKey("/A");
    if (!action.isDictionary()) return -1;
    const QPDFObjectHandle kind = action.getKey("/S");
    if (!kind.isName()) return -1;
    const std::string actionName = kind.getName();
    if (actionName == "/URI") {
        const QPDFObjectHandle uri = action.getKey("/URI");
        if (uri.isString() && uriOut != nullptr) {
            *uriOut = QString::fromUtf8(uri.getUTF8Value().c_str());
        }
        return -1;
    }
    if (actionName == "/GoTo") {
        const QPDFObjectHandle target = action.getKey("/D");
        if (target.isArray()) return pageIndexOfDestination(target, pages);
        if (target.isString()) {
            return pageIndexOfNamedDestination(pdf, QString::fromUtf8(target.getUTF8Value().c_str()), pages);
        }
    }
    return -1;
}

// 目的地数组里页对象之外的部分（视图参数：/Fit、/XYZ left top zoom……）。
//
// 只搬「直接值」：间接引用属于源文件，搬进合并结果就成了悬空引用。
QVector<QPDFObjectHandle> destinationTail(const QPDFObjectHandle& destination) {
    QVector<QPDFObjectHandle> tail;
    if (!destination.isArray()) return tail;
    const std::vector<QPDFObjectHandle> items = destination.getArrayAsVector();
    for (size_t i = 1; i < items.size(); ++i) {
        const QPDFObjectHandle& item = items[i];
        if (item.isIndirect()) continue;
        if (item.isNumber() || item.isName() || item.isNull()) tail.append(item);
    }
    return tail;
}

// 造一个新的目的地数组：第一个元素换成目标页，其余（视图参数）照旧。
QPDFObjectHandle buildDestination(const QPDFObjectHandle& page,
                                  const QVector<QPDFObjectHandle>& tail) {
    std::vector<QPDFObjectHandle> items;
    items.push_back(page);
    for (const QPDFObjectHandle& part : tail) items.push_back(part);
    // 没有视图参数时补一个 /Fit：只写页码的 destinations 也是合法的。
    if (items.size() == 1) items.push_back(QPDFObjectHandle::newName("/Fit"));
    return QPDFObjectHandle::newArray(items);
}

// 条目的目标数组：/Dest 直接给数组时用它；写的是 /A /GoTo 时用动作里的 /D。
QPDFObjectHandle destinationOfItem(const QPDFObjectHandle& item) {
    const QPDFObjectHandle dest = item.getKey("/Dest");
    if (dest.isArray()) return dest;
    const QPDFObjectHandle action = item.getKey("/A");
    if (!action.isDictionary()) return QPDFObjectHandle();
    const QPDFObjectHandle kind = action.getKey("/S");
    if (!kind.isName() || kind.getName() != "/GoTo") return QPDFObjectHandle();
    const QPDFObjectHandle target = action.getKey("/D");
    return target.isArray() ? target : QPDFObjectHandle();
}

// 读一棵子树，返回这棵子树的顶层节点下标。
//
// 只记父子关系的下标，不急着填 /Parent 之类的引用：
// 那些引用要等目标文档里的对象都建好之后才能回填。
QVector<int> readOutlineChildren(QPDF& pdf,
                                 const QPDFObjectHandle& first,
                                 const std::vector<QPDFObjectHandle>& pages,
                                 int level,
                                 OutlineTree* tree) {
    QVector<int> result;
    QPDFObjectHandle item = first;
    int guard = 0;
    while (item.isDictionary() && item.isIndirect() && guard < kMaxOutlineNodes) {
        ++guard;
        if (tree->nodes.size() >= kMaxOutlineNodes) break;
        OutlineNode node;
        node.level = level;
        const QPDFObjectHandle title = item.getKey("/Title");
        if (title.isString()) node.title = QString::fromUtf8(title.getUTF8Value().c_str());
        node.sourcePage = resolveItemDestination(pdf, item, pages, &node.uri);
        node.destTail = destinationTail(destinationOfItem(item));
        const int index = tree->nodes.size();
        tree->nodes.append(node);
        const QPDFObjectHandle childFirst = item.getKey("/First");
        if (childFirst.isDictionary() && level + 1 < kMaxOutlineDepth) {
            tree->nodes[index].children =
                readOutlineChildren(pdf, childFirst, pages, level + 1, tree);
        }
        result.append(index);
        item = item.getKey("/Next");
    }
    return result;
}

// 读一份文件的大纲树。
OutlineTree readOutlineTree(QPDF& pdf, const std::vector<QPDFObjectHandle>& pages) {
    OutlineTree tree;
    const QPDFObjectHandle root = pdf.getRoot().getKey("/Outlines");
    if (!root.isDictionary()) return tree;
    const QPDFObjectHandle first = root.getKey("/First");
    if (!first.isDictionary()) return tree;
    tree.topLevel = readOutlineChildren(pdf, first, pages, 0, &tree);
    return tree;
}

// 一个条目下面的后代条数（PDF 规范里 /Count 的含义）。
int descendantCount(const QVector<MergedNode>& nodes, int index) {
    int total = 0;
    for (int child : nodes.at(index).children) {
        total += 1 + descendantCount(nodes, child);
    }
    return total;
}

// 在目标文档里建一棵合并后的书签树。
//
// 为什么要自己建，而不是把源文件的 /Outlines 整个拷过来：
// 源文件的大纲指向的是**源文件自己的页对象**，拷过来就是悬空引用
// （那些页对象在合并结果里要么不存在、要么已经换了位置）。
// 自己重建，才能把每一条书签指向合并结果里真正的那一页。
//
// 层级怎么拼：每份文件自己的层级照搬（子条目仍然是子条目），
// 各份文件的**顶层**条目在合并结果里并排成一条链。
// 不额外加一层「文件名」节点——标题前缀已经说明了来源，
// 再套一层会让书签多出一层没必要的缩进。
void buildMergedOutline(QPDF& target,
                        const QVector<OutlineTree>& trees,
                        const QVector<int>& pageOffsets,
                        const std::vector<QPDFObjectHandle>& targetPages,
                        const MergeOptions& options,
                        const QVector<QString>& sourceNames) {
    QVector<MergedNode> nodes;
    QVector<int> topLevel;

    for (int s = 0; s < trees.size(); ++s) {
        const OutlineTree& tree = trees.at(s);
        const int offset = pageOffsets.at(s);
        const QString sourceName = s < sourceNames.size() ? sourceNames.at(s) : QString();
        // 源节点下标 → 合并节点下标。
        QVector<int> mapping(tree.nodes.size(), -1);
        for (int i = 0; i < tree.nodes.size(); ++i) {
            const OutlineNode& source = tree.nodes.at(i);
            MergedNode node;
            node.title = source.title;
            if (options.outlinePrefix && !sourceName.isEmpty()) {
                node.title = sourceName + QStringLiteral(" · ") + source.title;
            }
            node.uri = source.uri;
            node.destTail = source.destTail;
            if (source.sourcePage >= 0) {
                const int mergedPage = offset + source.sourcePage;
                if (mergedPage >= 0 && mergedPage < static_cast<int>(targetPages.size())) {
                    node.targetPage = mergedPage;
                }
            }
            mapping[i] = nodes.size();
            nodes.append(node);
        }
        // 父子关系按映射搬过来。
        for (int i = 0; i < tree.nodes.size(); ++i) {
            const int mergedIndex = mapping.at(i);
            if (mergedIndex < 0) continue;
            for (int child : tree.nodes.at(i).children) {
                if (child < 0 || child >= mapping.size()) continue;
                const int mergedChild = mapping.at(child);
                if (mergedChild < 0) continue;
                nodes[mergedIndex].children.append(mergedChild);
                nodes[mergedChild].parent = mergedIndex;
            }
        }
        for (int sourceIndex : tree.topLevel) {
            if (sourceIndex < 0 || sourceIndex >= mapping.size()) continue;
            const int mergedIndex = mapping.at(sourceIndex);
            if (mergedIndex >= 0) topLevel.append(mergedIndex);
        }
    }

    if (nodes.isEmpty()) {
        target.getRoot().removeKey("/Outlines");
        return;
    }

    // 大纲根节点要先建出来：条目的 /Parent 要指回它。
    QPDFObjectHandle rootDict = QPDFObjectHandle::newDictionary();
    rootDict.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
    // 注意：qpdf 的对象句柄只有非 const 版本能改（replaceKey 是非 const 方法）。
    QPDFObjectHandle rootRef = target.makeIndirectObject(rootDict);

    // 每个条目建一个间接对象。/Parent /Prev /Next 互相指向，
    // 只能等对象都建好之后再回填。
    for (MergedNode& node : nodes) {
        QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
        dict.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(node.title.toUtf8().constData()));
        if (node.targetPage >= 0) {
            dict.replaceKey("/Dest", buildDestination(targetPages.at(node.targetPage), node.destTail));
        } else if (!node.uri.isEmpty()) {
            QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
            action.replaceKey("/S", QPDFObjectHandle::newName("/URI"));
            action.replaceKey("/URI", QPDFObjectHandle::newString(node.uri.toUtf8().constData()));
            dict.replaceKey("/A", action);
        }
        node.object = target.makeIndirectObject(dict);
    }

    // 回填父子与兄弟关系。
    for (int i = 0; i < nodes.size(); ++i) {
        MergedNode& node = nodes[i];
        const QVector<int>& siblings = node.parent >= 0 ? nodes.at(node.parent).children : topLevel;
        const int position = siblings.indexOf(i);
        node.object.replaceKey("/Parent", node.parent >= 0 ? nodes.at(node.parent).object : rootRef);
        if (position > 0) {
            node.object.replaceKey("/Prev", nodes.at(siblings.at(position - 1)).object);
        }
        if (position >= 0 && position + 1 < siblings.size()) {
            node.object.replaceKey("/Next", nodes.at(siblings.at(position + 1)).object);
        }
        if (!node.children.isEmpty()) {
            node.object.replaceKey("/First", nodes.at(node.children.first()).object);
            node.object.replaceKey("/Last", nodes.at(node.children.last()).object);
            node.object.replaceKey("/Count",
                                   QPDFObjectHandle::newInteger(descendantCount(nodes, i)));
        }
    }

    rootRef.replaceKey("/First", nodes.at(topLevel.first()).object);
    rootRef.replaceKey("/Last", nodes.at(topLevel.last()).object);
    rootRef.replaceKey("/Count", QPDFObjectHandle::newInteger(nodes.size()));
    target.getRoot().replaceKey("/Outlines", rootRef);
}

// 读回一条批注的目标页对象号（-1 = 没有可解析的目标页）。
int destinationPageId(const QPDFObjectHandle& annot) {
    const QPDFObjectHandle destination = destinationOfItem(annot);
    if (!destination.isArray()) return -1;
    const std::vector<QPDFObjectHandle> items = destination.getArrayAsVector();
    if (items.empty()) return -1;
    const QPDFObjectHandle& first = items.front();
    if (!first.isIndirect()) return -1;
    return first.getObjectID();
}

// 把源文件里的内部跳转改指到合并结果里的页号。
//
// 为什么必须改：源文件里的 /Dest 指向的是**源文件自己的页对象**。
// 合并之后那些页对象要么不存在、要么换了位置——不改的话，
// 点「跳到第 2 页」会跳到错的地方，或者干脆跳不动。
//
// 读的是源文件里的目标，写的是合并结果里的批注：
// 两边靠「批注在 /Annots 里的下标」对齐（拷贝是原样搬的，顺序不变）。
int fixInternalLinks(const std::vector<QPDFObjectHandle>& sourcePages,
                     const std::vector<QPDFObjectHandle>& targetPages,
                     int offset) {
    int fixed = 0;
    for (size_t p = 0; p < sourcePages.size(); ++p) {
        const int targetIndex = offset + static_cast<int>(p);
        if (targetIndex < 0 || targetIndex >= static_cast<int>(targetPages.size())) continue;
        const QPDFObjectHandle sourceAnnots = sourcePages[p].getKey("/Annots");
        const QPDFObjectHandle targetAnnots = targetPages[targetIndex].getKey("/Annots");
        if (!sourceAnnots.isArray() || !targetAnnots.isArray()) continue;
        const std::vector<QPDFObjectHandle> sourceItems = sourceAnnots.getArrayAsVector();
        const std::vector<QPDFObjectHandle> targetItems = targetAnnots.getArrayAsVector();
        // 两边的批注应当一一对应；数量对不上就别动（宁可不改也不要改错位置）。
        if (sourceItems.size() != targetItems.size()) continue;
        for (size_t k = 0; k < sourceItems.size(); ++k) {
            const QPDFObjectHandle& sourceAnnot = sourceItems[k];
            // 目标批注要按值取：qpdf 的写接口只认非 const 句柄。
            QPDFObjectHandle targetAnnot = targetItems[k];
            if (!sourceAnnot.isDictionary()) continue;
            const QPDFObjectHandle subtype = sourceAnnot.getKey("/Subtype");
            if (!subtype.isName() || subtype.getName() != "/Link") continue;
            const QPDFObjectHandle destination = destinationOfItem(sourceAnnot);
            if (!destination.isArray()) continue;
            const int sourcePage = pageIndexOfDestination(destination, sourcePages);
            if (sourcePage < 0) continue;
            const int mergedPage = offset + sourcePage;
            if (mergedPage < 0 || mergedPage >= static_cast<int>(targetPages.size())) continue;
            const QPDFObjectHandle newDestination =
                buildDestination(targetPages[mergedPage], destinationTail(destination));
            try {
                if (sourceAnnot.getKey("/A").isDictionary()) {
                    // 走 /A 动作的：整条动作换掉。动作字典通常直接写在批注里，
                    // 直接改它不安全，换一条新的更稳。
                    QPDFObjectHandle goTo = QPDFObjectHandle::newDictionary();
                    goTo.replaceKey("/S", QPDFObjectHandle::newName("/GoTo"));
                    goTo.replaceKey("/D", newDestination);
                    targetAnnot.replaceKey("/A", goTo);
                } else {
                    targetAnnot.replaceKey("/Dest", newDestination);
                }
            } catch (const std::exception&) {
                // 批注是直接对象之类的坏情况：跳过这一条，不影响整份合并。
                continue;
            }
            // 写完读回来核对：写进去了才算修好（qpdf 对直接对象的写入
            // 未必落到真正的那份字节上）。
            if (destinationPageId(targetAnnot) == targetPages[mergedPage].getObjectID()) {
                ++fixed;
            }
        }
    }
    return fixed;
}

}  // namespace

#endif  // AIPDF_WITH_QPDF

bool mergeDocuments(const QVector<MergeInput>& inputs,
                    const MergeOptions& options,
                    MergeOutcome* outcome,
                    QString* error) {
    MergeOutcome result;
    const auto fail = [&result, outcome, error](const QString& message) {
        result = MergeOutcome();
        result.error = message;
        if (error != nullptr) *error = message;
        if (outcome != nullptr) *outcome = result;
        return false;
    };

    if (inputs.isEmpty()) {
        return fail(QStringLiteral("没有可合并的文件"));
    }
    for (int i = 0; i < inputs.size(); ++i) {
        if (inputs.at(i).bytes.isEmpty()) {
            return fail(QStringLiteral("第 %1 个文件是空的").arg(i + 1));
        }
    }

#ifndef AIPDF_WITH_QPDF
    Q_UNUSED(options);
    return fail(QStringLiteral("本次构建没有启用 qpdf，多文件合并不可用"));
#else
    try {
        // 每份源文件都要先能打开：有一份坏掉就整份失败，
        // 不能合到一半才报错（那样会产出半成品）。
        std::vector<std::unique_ptr<QPDF>> sources;
        sources.reserve(static_cast<size_t>(inputs.size()));
        for (int i = 0; i < inputs.size(); ++i) {
            auto source = std::make_unique<QPDF>();
            // 源文件里的警告由本层自己翻译成中文错误，
            // 让 qpdf 直接写到 stderr 只会把界面日志弄脏。
            source->setSuppressWarnings(true);
            const QByteArray& bytes = inputs.at(i).bytes;
            const QByteArray name = QStringLiteral("source-%1.pdf").arg(i + 1).toUtf8();
            source->processMemoryFile(name.constData(),
                                      bytes.constData(),
                                      static_cast<size_t>(bytes.size()));
            // 坏文件有时能被 qpdf「修复」成一份空文档。
            // 与其合出一份少页的文件，不如直接说清楚哪一份是空的。
            if (source->getAllPages().empty()) {
                return fail(QStringLiteral("第 %1 个文件里没有页面").arg(i + 1));
            }
            sources.push_back(std::move(source));
        }

        // 第一份文件当底：它的页已经在里面了，后面几份只需要把页追加进去
        // （用 qpdf 官方的页面拷贝，不自己拼内容流）。
        QPDF target;
        target.setSuppressWarnings(true);
        {
            const QByteArray& first = inputs.first().bytes;
            target.processMemoryFile("merged.pdf",
                                     first.constData(),
                                     static_cast<size_t>(first.size()));
        }

        // 页码偏移：每份文件在合并结果里的起始页。
        QVector<int> offsets;
        offsets.append(0);
        int running = static_cast<int>(target.getAllPages().size());
        for (size_t s = 1; s < sources.size(); ++s) {
            QPDF& source = *sources[s];
            const std::vector<QPDFObjectHandle>& sourcePages = source.getAllPages();
            QPDFPageDocumentHelper& helper = QPDFPageDocumentHelper::get(target);
            for (const QPDFObjectHandle& page : sourcePages) {
                helper.addPage(QPDFPageObjectHelper(page), false);
            }
            offsets.append(running);
            running += static_cast<int>(sourcePages.size());
        }

        const std::vector<QPDFObjectHandle> targetPages = target.getAllPages();
        const int expectedPages = static_cast<int>(targetPages.size());
        if (expectedPages <= 0) {
            return fail(QStringLiteral("合并后没有页面"));
        }

        // 读各份文件的大纲，再在目标文档里重建一棵合并的大纲。
        QVector<OutlineTree> trees;
        QVector<QString> sourceNames;
        for (int i = 0; i < inputs.size(); ++i) {
            const std::vector<QPDFObjectHandle>& sourcePages = sources[static_cast<size_t>(i)]->getAllPages();
            trees.append(readOutlineTree(*sources[static_cast<size_t>(i)], sourcePages));
            sourceNames.append(inputs.at(i).sourceName);
        }
        if (options.copyOutlines) {
            buildMergedOutline(target, trees, offsets, targetPages, options, sourceNames);
        } else {
            // 不要源书签时，第一份文件带来的那份也要摘掉：
            // 留着它就是「书签还指着老页号」，比没有书签更糟。
            target.getRoot().removeKey("/Outlines");
        }

        // 重定向源文件里的内部跳转。
        // 修好的条数只作自检用（写完读回来核对过才算数），不对外报告。
        if (options.fixInternalLinks) {
            for (int i = 0; i < inputs.size(); ++i) {
                const size_t index = static_cast<size_t>(i);
                fixInternalLinks(sources[index]->getAllPages(),
                                 targetPages,
                                 offsets.at(i));
            }
        }

        QPDFWriter writer(target);
        writer.setOutputMemory();
        // 关掉对象流：交叉引用表保持传统形态，出问题时人眼能直接看。
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            return fail(QStringLiteral("qpdf 没有写出合并结果"));
        }
        const QByteArray merged(reinterpret_cast<const char*>(buffer->getBuffer()),
                                static_cast<int>(buffer->getSize()));

        // 自检：写出来的文件必须能重新打开，而且页数与预期一致。
        // 「少了页却报告成功」是最糟的失败方式。
        QPDF check;
        check.processMemoryFile("check.pdf", merged.constData(), static_cast<size_t>(merged.size()));
        const int checkPages = static_cast<int>(check.getAllPages().size());
        if (checkPages != expectedPages) {
            return fail(QStringLiteral("合并结果的页数不对：期望 %1 页，实际 %2 页")
                            .arg(expectedPages)
                            .arg(checkPages));
        }

        result.bytes = merged;
        result.pageOffsets = offsets;
        result.pageCount = expectedPages;
        result.error.clear();
        if (error != nullptr) error->clear();
        if (outcome != nullptr) *outcome = result;
        return true;
    } catch (const std::exception& exception) {
        return fail(QStringLiteral("合并失败：%1").arg(QString::fromUtf8(exception.what())));
    }
#endif
}

QVector<SourceOutlineEntry> readOutlineEntries(const QByteArray& bytes, QString* error) {
    QVector<SourceOutlineEntry> entries;
    if (bytes.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("没有可读的内容");
        return entries;
    }
#ifndef AIPDF_WITH_QPDF
    if (error != nullptr) *error = QStringLiteral("本次构建没有启用 qpdf，读不出大纲");
    return entries;
#else
    try {
        QPDF pdf;
        pdf.processMemoryFile("outline.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        const OutlineTree tree = readOutlineTree(pdf, pages);
        entries.reserve(tree.nodes.size());
        for (const OutlineNode& node : tree.nodes) {
            SourceOutlineEntry entry;
            entry.level = node.level;
            entry.title = node.title;
            entry.sourcePage = node.sourcePage;
            entry.uri = node.uri;
            entries.append(entry);
        }
        if (error != nullptr) error->clear();
    } catch (const std::exception& exception) {
        entries.clear();
        if (error != nullptr) {
            *error = QStringLiteral("读大纲失败：%1").arg(QString::fromUtf8(exception.what()));
        }
    }
    return entries;
#endif
}

}  // namespace aipdf::pdf
