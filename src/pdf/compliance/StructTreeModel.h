#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 标准结构类型（PDF 32000-1 表 333 的结构类型集合）。
//
// 为什么用枚举而不是到处写字符串：结构类型名是**协议**（PDF 里的 /S 名字），
// 拼错一个字母就是另一个类型，读屏软件的理解完全不同。
// 仓库里凡是判断「这个 /S 是不是标准类型」的地方都用这里的表，
// 表只有这一份（roleToName / nameToRole 一一对应）。
enum class StructRole {
    Unknown = 0,

    // 分组类
    Document,
    Part,
    Art,
    Sect,
    Div,
    BlockQuote,
    Caption,
    TOC,
    TOCI,
    Index,
    NonStruct,
    Private,

    // 块级类
    P,
    H,   // 泛用标题（PDF/UA 不推荐，弱结构模型要用 H1..H6）
    H1,
    H2,
    H3,
    H4,
    H5,
    H6,

    // 列表类
    L,
    LI,
    Lbl,
    LBody,

    // 表格类
    Table,
    TR,
    TH,
    TD,
    THead,
    TBody,
    TFoot,

    // 行内类
    Span,
    Quote,
    Note,
    Reference,
    BibEntry,
    Code,
    Link,
    Annot,
    Ruby,
    Warichu,

    // 插图类
    Figure,
    Formula,
    Form,
};

// 标准结构类型名（**不带**前导斜杠），例如 "Document"、"H1"。
//
// 为什么返回不带斜杠的名字：结构类型名会同时出现在
// 「给人看的说明」与「写进 PDF 的 /S 值」两处，
// 带斜杠的版本写进 PDF 时还得再拼一次，容易拼成 "//Document"。
inline QString roleToName(StructRole role) {
    switch (role) {
        case StructRole::Document: return QStringLiteral("Document");
        case StructRole::Part: return QStringLiteral("Part");
        case StructRole::Art: return QStringLiteral("Art");
        case StructRole::Sect: return QStringLiteral("Sect");
        case StructRole::Div: return QStringLiteral("Div");
        case StructRole::BlockQuote: return QStringLiteral("BlockQuote");
        case StructRole::Caption: return QStringLiteral("Caption");
        case StructRole::TOC: return QStringLiteral("TOC");
        case StructRole::TOCI: return QStringLiteral("TOCI");
        case StructRole::Index: return QStringLiteral("Index");
        case StructRole::NonStruct: return QStringLiteral("NonStruct");
        case StructRole::Private: return QStringLiteral("Private");
        case StructRole::P: return QStringLiteral("P");
        case StructRole::H: return QStringLiteral("H");
        case StructRole::H1: return QStringLiteral("H1");
        case StructRole::H2: return QStringLiteral("H2");
        case StructRole::H3: return QStringLiteral("H3");
        case StructRole::H4: return QStringLiteral("H4");
        case StructRole::H5: return QStringLiteral("H5");
        case StructRole::H6: return QStringLiteral("H6");
        case StructRole::L: return QStringLiteral("L");
        case StructRole::LI: return QStringLiteral("LI");
        case StructRole::Lbl: return QStringLiteral("Lbl");
        case StructRole::LBody: return QStringLiteral("LBody");
        case StructRole::Table: return QStringLiteral("Table");
        case StructRole::TR: return QStringLiteral("TR");
        case StructRole::TH: return QStringLiteral("TH");
        case StructRole::TD: return QStringLiteral("TD");
        case StructRole::THead: return QStringLiteral("THead");
        case StructRole::TBody: return QStringLiteral("TBody");
        case StructRole::TFoot: return QStringLiteral("TFoot");
        case StructRole::Span: return QStringLiteral("Span");
        case StructRole::Quote: return QStringLiteral("Quote");
        case StructRole::Note: return QStringLiteral("Note");
        case StructRole::Reference: return QStringLiteral("Reference");
        case StructRole::BibEntry: return QStringLiteral("BibEntry");
        case StructRole::Code: return QStringLiteral("Code");
        case StructRole::Link: return QStringLiteral("Link");
        case StructRole::Annot: return QStringLiteral("Annot");
        case StructRole::Ruby: return QStringLiteral("Ruby");
        case StructRole::Warichu: return QStringLiteral("Warichu");
        case StructRole::Figure: return QStringLiteral("Figure");
        case StructRole::Formula: return QStringLiteral("Formula");
        case StructRole::Form: return QStringLiteral("Form");
        case StructRole::Unknown: break;
    }
    return QString();
}

// 名字 → 结构类型。认不出来的（自定义类型）返回 StructRole::Unknown。
inline StructRole nameToRole(const QString& name) {
    if (name.isEmpty()) return StructRole::Unknown;
    static const QVector<QPair<QString, StructRole>> table = {
        {QStringLiteral("Document"), StructRole::Document},
        {QStringLiteral("Part"), StructRole::Part},
        {QStringLiteral("Art"), StructRole::Art},
        {QStringLiteral("Sect"), StructRole::Sect},
        {QStringLiteral("Div"), StructRole::Div},
        {QStringLiteral("BlockQuote"), StructRole::BlockQuote},
        {QStringLiteral("Caption"), StructRole::Caption},
        {QStringLiteral("TOC"), StructRole::TOC},
        {QStringLiteral("TOCI"), StructRole::TOCI},
        {QStringLiteral("Index"), StructRole::Index},
        {QStringLiteral("NonStruct"), StructRole::NonStruct},
        {QStringLiteral("Private"), StructRole::Private},
        {QStringLiteral("P"), StructRole::P},
        {QStringLiteral("H"), StructRole::H},
        {QStringLiteral("H1"), StructRole::H1},
        {QStringLiteral("H2"), StructRole::H2},
        {QStringLiteral("H3"), StructRole::H3},
        {QStringLiteral("H4"), StructRole::H4},
        {QStringLiteral("H5"), StructRole::H5},
        {QStringLiteral("H6"), StructRole::H6},
        {QStringLiteral("L"), StructRole::L},
        {QStringLiteral("LI"), StructRole::LI},
        {QStringLiteral("Lbl"), StructRole::Lbl},
        {QStringLiteral("LBody"), StructRole::LBody},
        {QStringLiteral("Table"), StructRole::Table},
        {QStringLiteral("TR"), StructRole::TR},
        {QStringLiteral("TH"), StructRole::TH},
        {QStringLiteral("TD"), StructRole::TD},
        {QStringLiteral("THead"), StructRole::THead},
        {QStringLiteral("TBody"), StructRole::TBody},
        {QStringLiteral("TFoot"), StructRole::TFoot},
        {QStringLiteral("Span"), StructRole::Span},
        {QStringLiteral("Quote"), StructRole::Quote},
        {QStringLiteral("Note"), StructRole::Note},
        {QStringLiteral("Reference"), StructRole::Reference},
        {QStringLiteral("BibEntry"), StructRole::BibEntry},
        {QStringLiteral("Code"), StructRole::Code},
        {QStringLiteral("Link"), StructRole::Link},
        {QStringLiteral("Annot"), StructRole::Annot},
        {QStringLiteral("Ruby"), StructRole::Ruby},
        {QStringLiteral("Warichu"), StructRole::Warichu},
        {QStringLiteral("Figure"), StructRole::Figure},
        {QStringLiteral("Formula"), StructRole::Formula},
        {QStringLiteral("Form"), StructRole::Form},
    };
    for (const auto& entry : table) {
        if (entry.first == name) return entry.second;
    }
    return StructRole::Unknown;
}

// 这个名字是不是标准结构类型。
//
// 为什么单独给一个函数而不是「nameToRole != Unknown」：
// Unknown 也表示「没解析出来」，而「不是标准类型」是一个**结论**，
// 检查器要拿它去判断「要不要 /RoleMap」。
inline bool isStandardRoleName(const QString& name) {
    return nameToRole(name) != StructRole::Unknown;
}

// 全部标准结构类型名（按枚举顺序）。
inline QStringList standardRoleNames() {
    QStringList names;
    for (int i = static_cast<int>(StructRole::Document);
         i <= static_cast<int>(StructRole::Form); ++i) {
        const QString name = roleToName(static_cast<StructRole>(i));
        if (!name.isEmpty()) names.append(name);
    }
    return names;
}

// 是不是标题类型（H 是泛用标题，PDF/UA 的弱结构模型不认它）。
inline bool isHeadingRole(StructRole role) {
    return role == StructRole::H1 || role == StructRole::H2 || role == StructRole::H3 ||
           role == StructRole::H4 || role == StructRole::H5 || role == StructRole::H6;
}

// 标题层级（1..6）。不是标题返回 0。
inline int headingLevel(StructRole role) {
    switch (role) {
        case StructRole::H1: return 1;
        case StructRole::H2: return 2;
        case StructRole::H3: return 3;
        case StructRole::H4: return 4;
        case StructRole::H5: return 5;
        case StructRole::H6: return 6;
        default: return 0;
    }
}

// 是不是块级结构类型（可以作为 Part / Sect / LBody / TD 的子节点）。
inline bool isBlockLevelRole(StructRole role) {
    switch (role) {
        case StructRole::Sect:
        case StructRole::Div:
        case StructRole::Art:
        case StructRole::BlockQuote:
        case StructRole::Caption:
        case StructRole::TOC:
        case StructRole::TOCI:
        case StructRole::Index:
        case StructRole::P:
        case StructRole::H:
        case StructRole::H1:
        case StructRole::H2:
        case StructRole::H3:
        case StructRole::H4:
        case StructRole::H5:
        case StructRole::H6:
        case StructRole::L:
        case StructRole::Table:
        case StructRole::Figure:
        case StructRole::Formula:
        case StructRole::Form:
        case StructRole::NonStruct:
        case StructRole::Private:
            return true;
        default:
            return false;
    }
}

// 是不是行内结构类型（可以作为 Span / TD 的子节点）。
inline bool isInlineLevelRole(StructRole role) {
    switch (role) {
        case StructRole::Span:
        case StructRole::Quote:
        case StructRole::Note:
        case StructRole::Reference:
        case StructRole::BibEntry:
        case StructRole::Code:
        case StructRole::Link:
        case StructRole::Annot:
        case StructRole::Ruby:
        case StructRole::Warichu:
            return true;
        default:
            return false;
    }
}

// 父子嵌套是否合法（PDF 32000-1 表 333 与 §14.8.4 的结构类型层级）。
//
// 为什么只管住几条最硬的关系：标准对每一类结构类型的子节点都有约束，
// 但把全部约束都编码进来，误报会多到没法用。
// 这里只管**读屏软件一定会读错**的几条：
//   * Document 的子节点是 Part（PDF/UA 的结构树要求）；
//   * L 的子节点是 LI；LI 的子节点是 Lbl / LBody；
//   * Table 的子节点是 TR / THead / TBody / TFoot；TR 的子节点是 TH / TD；
//   * THead / TBody / TFoot 的子节点是 TR；
//   * 块级容器的子节点是块级类型；行内容器的子节点是行内类型。
//
// 判不了的（父类型不在表里、或是 Unknown）一律放行：宁可漏报，不误伤。
inline bool isLegalChildRole(StructRole parent, StructRole child) {
    if (parent == StructRole::Unknown || child == StructRole::Unknown) return true;

    switch (parent) {
        case StructRole::Document:
            // Document 的子节点**不**在这里管。
            //
            // 为什么：「/Document 下面必须有 /Part」是 PDF/UA 的一条独立要求，
            // 检查器有专门的一条结论报它（ua-missing-part-element）。
            // 如果这里也管，「拆掉 /Part」就会同时报两条——用户看到两条错误，
            // 其实是同一件事。这里放开，让每条结论只对应一件事。
            return true;
        case StructRole::Part:
        case StructRole::Sect:
        case StructRole::Div:
        case StructRole::Art:
        case StructRole::BlockQuote:
        case StructRole::LBody:
        case StructRole::TH:
        case StructRole::TD:
            return isBlockLevelRole(child) || isInlineLevelRole(child);
        case StructRole::L:
            return child == StructRole::LI;
        case StructRole::LI:
            return child == StructRole::Lbl || child == StructRole::LBody;
        case StructRole::Lbl:
            return isInlineLevelRole(child);
        case StructRole::Table:
            return child == StructRole::TR || child == StructRole::THead ||
                   child == StructRole::TBody || child == StructRole::TFoot;
        case StructRole::TR:
            return child == StructRole::TH || child == StructRole::TD;
        case StructRole::THead:
        case StructRole::TBody:
        case StructRole::TFoot:
            return child == StructRole::TR;
        case StructRole::Span:
        case StructRole::Quote:
        case StructRole::Note:
        case StructRole::Reference:
        case StructRole::BibEntry:
        case StructRole::Code:
        case StructRole::Link:
        case StructRole::Ruby:
        case StructRole::Warichu:
            return isInlineLevelRole(child);
        default:
            return true;
    }
}

// 语义树里的一个节点。
//
// 为什么节点要带「归类是否可靠」：自动打标签是猜的，猜错了要**说出来**。
// 悄悄给出一棵看着整齐、其实归类错了的树，比不给树更坏——
// 用户会以为机器已经替他判断过了。
struct SemanticNode {
    // 这个节点的结构类型。
    StructRole role = StructRole::Unknown;
    // 标题（写进 PDF 的 /T）。
    QString title;
    // 替代文本（写进 PDF 的 /Alt）。
    QString alt;
    // 实际文字（写进 PDF 的 /ActualText）。
    QString actualText;
    // 语言（写进 PDF 的 /Lang）。空表示继承文档语言。
    QString language;
    // 所属页（0 基）；-1 表示不限定页。
    int pageIndex = -1;
    // 直接挂在这个节点上的 MCID。
    QVector<int> mcids;
    // 这个节点的文字（按 MCID 顺序拼起来）。
    QString text;
    // 这个节点的内容来自哪些「块」（块下标 = 传入 inferSemanticRoles 的块序号）。
    //
    // 为什么树里要记块：写标签树时要知道「这个结构元素的 /K 里放哪几个 MCID」，
    // 而 MCID 是从块里的文本运行来的。
    QVector<int> blockIndices;
    // 子节点在 SemanticTree::nodes 里的下标。
    QVector<int> childIndices;
    // 父节点在 SemanticTree::nodes 里的下标；-1 表示顶层。
    int parentIndex = -1;
    // 归类是否可靠。false 时 unreliableReason 说明为什么不可靠。
    bool reliable = true;
    QString unreliableReason;

    // 表格专用：这一格在第几行、第几列（0 基）；行列跨度。
    int tableRow = -1;
    int tableColumn = -1;
    int tableRowSpan = 1;
    int tableColumnSpan = 1;
    // 整张表有几行、几列（表头格与数据格都一样）。
    int tableRowCount = 0;
    int tableColumnCount = 0;
    // 是不是表头格（TH）。
    bool tableHeader = false;
};

// 一棵语义树。
struct SemanticTree {
    // 全部节点，按先序排列（父在子前）。
    QVector<SemanticNode> nodes;
    // 根节点的下标；-1 表示空树。
    int rootIndex = -1;
    // 归类过程里发现的问题（中文）：哪一块不可靠、为什么。
    QStringList issues;

    bool isEmpty() const { return rootIndex < 0 || nodes.isEmpty(); }
    int nodeCount() const { return nodes.size(); }
};

// 追加一个节点，返回它的下标。
int appendSemanticNode(SemanticTree* tree, const SemanticNode& node);

// 把 child 挂到 parent 下面（同时维护两边的父子下标）。
void appendSemanticChild(SemanticTree* tree, int parentIndex, int childIndex);

// 按先序展开整棵树，返回节点下标序列（读屏软件念的顺序）。
QVector<int> semanticReadingOrder(const SemanticTree& tree);

// 按树顺序把节点文字拼起来（用分隔符隔开）。
QString semanticReadingText(const SemanticTree& tree, const QString& separator = QStringLiteral(" "));

// 树里某个角色的节点下标（例如所有 /Form）。
QVector<int> semanticNodesWithRole(const SemanticTree& tree, StructRole role);

}  // namespace aipdf::pdf
