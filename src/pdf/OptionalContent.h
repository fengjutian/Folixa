#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 一个可选内容组（OCG，也就是阅读器里的「图层」）。
struct OptionalContentGroup {
    // 图层名（OCG 字典的 /Name，文本串）。
    QString name;
    // 是否可见。
    //
    // 这个状态是按默认配置（/OCProperties /D）算出来的：
    // /BaseState 定基调，/ON 与 /OFF 数组是例外。
    bool visible = true;
    // /Intent（View / Design 之类）。文件里没写就是空串。
    QString intent;
    // 在 /Order 里的次序（0 基）。不在 /Order 里是 -1。
    int orderIndex = -1;
};

// 页面级的一条可选内容命令（OCMD，可选内容成员字典）。
struct OptionalContentCommand {
    // 可见性策略：AnyOn / AllOn / AnyOff / AllOff（默认 AnyOn）。
    QString policy = QStringLiteral("AnyOn");
    // 这条命令涉及的图层名。
    QStringList groupNames;
    // /VE 可见性表达式的原文（空串表示没有）。
    QString visibilityExpression;
};

// 页面 /Resources /Properties 里的一项：属性键名 → 图层名。
struct OptionalContentProperty {
    // 属性键名（带前导斜杠，例如 "/OC1"）。
    QString key;
    // 该键指向的图层名。
    QString groupName;
};

// 一页上的可选内容信息。
struct OptionalContentPage {
    // 页码（0 基）。
    int pageIndex = -1;
    // 页面 /OC 数组里的命令。
    QVector<OptionalContentCommand> commands;
    // 页面 /Resources /Properties 里指向图层的项。
    QVector<OptionalContentProperty> properties;
};

// 读出来的可选内容总览。
struct OptionalContentSummary {
    // 文件里到底有没有 /OCProperties。
    //
    // 「只有文本、没有图层」是绝大多数 PDF 的常态：这时它是 false，
    // 而不是错误——只有文件本身坏了才会写 error。
    bool hasOptionalContent = false;
    // 全部图层（按 /OCGs 数组的顺序）。
    QVector<OptionalContentGroup> groups;
    // 带可选内容设置的页（只有带 /OC 或带 /Properties 的页才在这里出现）。
    QVector<OptionalContentPage> pages;
    // 默认配置里的 /BaseState 与 /ListMode（没写就是空串）。
    QString baseState;
    QString listMode;
};

// 写：一个图层的定义。
struct OptionalContentLayer {
    // 图层名。不能为空，同一份文档里不能重名。
    QString name;
    // 默认是否可见（写进 /D 默认配置，同时在 OCG 字典里写一份 /ON 或 /OFF 镜像）。
    bool defaultVisible = true;
    // /Intent（可选，例如 View / Design）。
    QString intent;
};

// 写：把某段内容流标记到某个图层上。
struct OptionalContentMark {
    // 页码（0 基）。
    int pageIndex = -1;
    // 图层名（必须是方案里定义过的图层）。
    QString layerName;
    // 标记哪一条内容流：-1 表示该页的全部内容流；否则是内容流下标（0 基）。
    int contentStreamIndex = -1;
};

// 写：页面级覆盖（页面 /OC 数组里的一条 OCMD）。
struct OptionalContentPageCommand {
    // 页码（0 基）。
    int pageIndex = -1;
    // 这条命令涉及的图层名。
    QStringList groupNames;
    // 可见性策略：AnyOn / AllOn / AnyOff / AllOff（默认 AnyOn）。
    QString policy = QStringLiteral("AnyOn");
};

// 写：一份完整的图层方案。
struct OptionalContentPlan {
    // 图层定义。顺序就是图层面板里的顺序。
    QVector<OptionalContentLayer> layers;
    // 内容流标记。
    QVector<OptionalContentMark> marks;
    // 页面级覆盖。
    QVector<OptionalContentPageCommand> pageCommands;
    // 写完之后把 catalog 的 /PageMode 设成 /UseOC（打开文件就显示图层面板）。
    bool openLayerPanel = false;
};

// 读：解析 catalog 的 /OCProperties。
//
// 没有 /OCProperties 时返回空结果（hasOptionalContent 为 false，error 为空）：
// 绝大多数 PDF 都没有图层，这不是错误。
OptionalContentSummary readOptionalContent(const QByteArray& pdfBytes, QString* error = nullptr);

// 写：按方案产出一份新的 PDF 字节；失败返回空字节并写明中文原因。
//
// 写进去的东西：
//   - catalog 的 /OCProperties：/OCGs 数组（每个 OCG 一个间接对象）、
//     /D 默认配置（/BaseState + /ON + /OFF 数组 + /Order + /ListMode）；
//   - 被标记的内容流：整段裹上 `/OC /<属性键名> BDC … EMC`；
//   - 页面 /Resources /Properties：属性键名 → 该图层的 OCG 字典；
//   - 页面 /OC 数组：可选内容成员字典（OCMD），表达这一页的可见性覆盖。
//
// 关于属性键名：按 ISO 32000-1 §8.11.3，BDC 的操作数是一个**名字对象**，
// 它必须在页面 /Resources /Properties 里有一项与之对应，值是该图层的 OCG 字典；
// 图层名本身是 OCG 字典里的文本串，不能直接当名字对象用。
// 所以这里用图层名（当它只含字母数字下划线连字符时）或 "/OC<序号>" 作为属性键名，
// 两边解析出来是同一个名字，对得上。
QByteArray writeOptionalContent(const QByteArray& pdfBytes, const OptionalContentPlan& plan,
                                 QString* error = nullptr);

}  // namespace aipdf::pdf
