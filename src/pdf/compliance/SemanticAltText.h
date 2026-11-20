#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 语义树里的一个结构元素，以及它引用到的页面对象。
//
// 为什么单独建一个结构：pdf::StructElement（读侧 StructTreeReader 用的那个）
// 只有标签名、文字这些「给人看」的信息，没有对象号，也没有「这个元素指向
// 页面上的哪个对象」这一层。改替代文本要按对象找元素，缺这两样就找不回来。
struct SemanticElementInfo {
    // 结构元素自己的对象号（间接对象）。直接写进父字典里的元素是 -1。
    int objectId = -1;
    // 标签名（/S）。
    QString tag;
    // 标题（/T）。
    QString title;
    // 替代文本（/Alt）。
    QString alt;
    // 所属页（0 基）。-1 表示文件里没写、也没能推出来。
    int pageIndex = -1;
    // 这个元素引用到的页面对象号（图像 XObject、表单域 widget 注解）。
    //
    // 注意：qpdf 写回时会重新编号对象，所以这些对象号**只在写回之前**有意义。
    // 要跨写回比较两个元素，请比 referencedObjectTypes（那是不随编号变的）。
    QVector<int> referencedObjectIds;
    // 每个被引用对象的类型（/Subtype，没有就取 /Type）："/Image"、"/Widget"……
    //
    // 为什么单独留一份：调用方要判断「这个元素指的是图还是表单域」，
    // 而对象号在写回后会变，类型不会。
    QVector<QString> referencedObjectTypes;
};

// 一份文档的语义树索引（读侧）。
struct SemanticTreeIndex {
    // 文档里有没有 /StructTreeRoot。
    bool hasStructTree = false;
    // 全部结构元素，按树的先序排列（父在子前）。
    QVector<SemanticElementInfo> elements;
    // 失败原因（中文）。解析不了 PDF 时非空。
    QString error;

    bool ok() const { return error.isEmpty(); }

    // 引用了 objectId 的元素下标。一个都没有时返回 -1。
    //
    // pageIndex >= 0 时只在那一页的元素里找：同一个图像 XObject 被多页复用时
    // （页眉页脚那种 logo）树里会有好几个元素指向它，这时必须靠页码区分。
    int indexOfObject(int objectId, int pageIndex = -1) const;
    // 引用了 objectId 的全部元素下标。
    QVector<int> elementsReferencing(int objectId) const;
};

// 读一份 PDF 的语义树索引：每个结构元素的 /Alt + 它引用了哪些对象号。
//
// 判定「结构元素引用了某个对象」的规则（PDF 32000-1 §14.8.4.3）：
//   * 元素的 /K 里有 /Type /OBJR 的字典：看它的 /Obj 指向哪个对象号；
//     本仓库写出的图像元素走的就是这一条。
//   * 元素的 /K 里直接挂着 widget 注解字典（本仓库写出的表单域元素、
//     以及 FormAutoTag 写出的 /Form 元素走这一条）：看这个注解自己的对象号。
//   * /K 里的 MCID 数字不算对象引用（那是内容流里的标记内容编号）。
//
// 没有语义树的文档不算失败：hasStructTree 为假。
SemanticTreeIndex readSemanticTreeIndex(const QByteArray& pdfBytes);

// 改替代文本的选项。
struct SemanticAltTextOptions {
    // 允许把替代文本清空。
    //
    // 默认 false：空文本直接报错。理由是「误按回车把说明抹掉」比
    // 「少说一句说明」更伤——读屏用户会得到一个空标签。确实要清空时
    // 调用方显式打开这个开关（清空 = 删掉元素上的 /Alt 键）。
    bool allowClear = false;
    // 限定在某一页里找元素（0 基）。-1 表示不限。
    //
    // 什么时候需要：同一个图像 XObject 被多页复用时（页眉页脚那种 logo），
    // 树里有好几个元素指向同一个对象号，不给页码就没法确定改哪一个。
    int pageIndex = -1;
};

// 改替代文本的结果。
struct SemanticAltTextResult {
    // 是否成功。失败时 error 里有中文原因。
    bool succeeded = false;
    // 失败原因（中文）。
    QString error;
    // 改好的 PDF 字节。失败时为空（绝不产出改了一半的文件）。
    QByteArray bytes;
    // 改的是哪个结构元素（它的对象号）。失败时为 -1。
    int elementObjectId = -1;
    // 改之前的 /Alt。
    QString previousAlt;
    // 是不是把 /Alt 清掉了（而不是改成了新值）。
    bool cleared = false;

    bool ok() const { return succeeded; }
};

// 改**已经存在**的结构元素的替代文本（/Alt）。
//
// 给的是 PDF 对象号（页面上的图像 XObject，或表单域的 widget 注解），
// 不是页内下标：结构树里挂的是对象号，只有对象号才对得上。
//
// 找不到对应结构元素时**明确报错**，不静默成功，也不凭空造一个结构元素：
// 没有语义树的文档同样报错，不新建一棵标签树。
//
// 写回用 qpdf 重新序列化整份文件：页面内容、批注、其它元素的 /Alt 都不会动，
// 但字节布局会变（这是 qpdf 的正常行为，与本仓库写标签树那条路一致）。
//
// **对象号会变**：qpdf 写回时重新给对象编号。调用方拿到的 bytes 里，
// 结构树引用的对象号已经不是原来那几个了——要继续按对象号操作，
// 必须拿新文件重新查一遍（readSemanticTreeIndex）。
//
// 改完会重新解析校验一遍：目标元素的 /Alt 是新值、其它元素的 /Alt 没变、
// 页面内容与批注没动，才算成功。
SemanticAltTextResult setSemanticAltText(const QByteArray& pdfBytes,
                                          int objectId,
                                          const QString& altText,
                                          const SemanticAltTextOptions& options = SemanticAltTextOptions());

}  // namespace aipdf::pdf
