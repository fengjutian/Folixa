#include "pdf/forms/FormAutoTag.h"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <memory>
#include <vector>

#ifdef AIPDF_WITH_QPDF

#include <qpdf/Buffer.hh>
#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#endif

namespace aipdf::pdf {
namespace {

// 结构树遍历的防环与规模上限。
//
// 为什么必须设：坏文件里的结构树可能有环（/K 指回祖先，或 /Parent 指回祖先）。
// 不设限就会把自己绕死在一个死循环里——用户看到的是「程序卡住了」。
constexpr int kMaxTreeDepth = 64;
constexpr int kMaxTreeElements = 100000;

#ifdef AIPDF_WITH_QPDF

// 字典里某个键的字符串值；不是字符串就当没有。
QString stringValue(const QPDFObjectHandle& dict, const char* key) {
    const QPDFObjectHandle value = dict.getKey(key);
    if (!value.isString()) return {};
    return QString::fromUtf8(value.getUTF8Value().c_str());
}

// 字典里某个键的名字值（去掉前导斜杠）；不是名字就当没有。
QString nameValue(const QPDFObjectHandle& dict, const char* key) {
    const QPDFObjectHandle value = dict.getKey(key);
    if (!value.isName()) return {};
    const std::string name = value.getName();
    if (name.empty()) return {};
    return QString::fromLatin1(name.c_str() + 1);
}

// 两个句柄是不是同一个对象（只认间接对象：直接对象没有身份）。
bool sameObject(const QPDFObjectHandle& left, const QPDFObjectHandle& right) {
    if (!left.isIndirect() || !right.isIndirect()) return false;
    return left.getObjectID() == right.getObjectID() && left.getGeneration() == right.getGeneration();
}

// /K 的子项（/K 可以是一个元素，也可以是数组）。
QVector<QPDFObjectHandle> kidList(const QPDFObjectHandle& kids) {
    QVector<QPDFObjectHandle> result;
    if (kids.isDictionary()) {
        result.append(kids);
    } else if (kids.isArray()) {
        for (const QPDFObjectHandle& item : kids.getArrayAsVector()) result.append(item);
    }
    return result;
}

// 数一数结构树里既有的结构元素（按标签统计）。
//
// 为什么不把 /Form 算进来：/Form 元素正是这次要替换掉的，
// 算进去的话「保留了多少」就变成了「替换了多少」，口径会骗人。
//
// 为什么从树根开始数：文档元素本身也是既有结构元素，它同样被保留下来了。
void collectPreservedTags(const QPDFObjectHandle& parent, int depth, QSet<int>* visited,
                          QMap<QString, int>* counts) {
    if (!parent.isDictionary() || depth > kMaxTreeDepth) return;
    if (parent.isIndirect()) {
        const int id = parent.getObjectID();
        if (visited->contains(id)) return;
        visited->insert(id);
    }
    for (const QPDFObjectHandle& kid : kidList(parent.getKey("/K"))) {
        if (!kid.isDictionary()) continue;
        const QString tag = nameValue(kid, "/S");
        if (tag == "Form") continue;  // 这一类被替换掉，不算保留
        (*counts)[tag] += 1;
        collectPreservedTags(kid, depth + 1, visited, counts);
    }
}

// 在整棵树里找文档元素（/S /Document）。
//
// 为什么不能只看根的下一层：有的文件（某些 Word 导出、Acrobat 的输出）
// 把文档元素放在章节元素下面。只看根的下一层就会漏掉它，然后另建一个——
// 同一份文件里出现两棵文档分支，读屏软件按哪棵走都不对。
bool findDocumentElement(const QPDFObjectHandle& parent, int depth, QSet<int>* visited,
                         QPDFObjectHandle* found) {
    if (!parent.isDictionary() || depth > kMaxTreeDepth) return false;
    if (parent.isIndirect()) {
        const int id = parent.getObjectID();
        if (visited->contains(id)) return false;
        visited->insert(id);
    }
    for (const QPDFObjectHandle& kid : kidList(parent.getKey("/K"))) {
        if (!kid.isDictionary()) continue;
        if (nameValue(kid, "/S") == "Document") {
            *found = kid;
            return true;
        }
        if (findDocumentElement(kid, depth + 1, visited, found)) return true;
    }
    return false;
}

// 一个表单域（AcroForm 里的一个终端字段）。
struct FormFieldInfo {
    QString name;                       // 字段名（/T，含继承）
    QString tooltip;                    // 提示文字（/TU，含继承）
    QString type;                       // 字段类型（/FT，含继承）
    int pageIndex = -1;                 // 所在页（0 基）；-1 表示没找到
    QVector<QPDFObjectHandle> widgets;  // 这个域的 widget 注解
    // 阅读顺序用的位置：页面上从上到下、从左到右念。
    //
    // 为什么自己算而不是用 widget 的 /Rect：/Rect 的两个 y 值是「下边、上边」
    // （PDF 坐标，y 越大越靠上），跟界面上的「上边」不是一个东西。
    // 这里统一成「readingTop 越大越靠上、readingLeft 越小越靠左」。
    double readingTop = 0.0;
    double readingLeft = 0.0;
    bool hasPosition = false;  // 拿不到位置时，排在同页有位置的域后面
};

// widget 注解在页面上的位置（读屏顺序按它排）。
//
// 拿不到 /Rect 就返回 false：这时只能按收集顺序排，不能编一个位置出来。
bool readingPositionOfWidget(const QPDFObjectHandle& widget, double* top, double* left) {
    const QPDFObjectHandle rect = widget.getKey("/Rect");
    if (!rect.isArray()) return false;
    const std::vector<QPDFObjectHandle> items = rect.getArrayAsVector();
    if (items.size() != 4) return false;
    double values[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        if (!items.at(static_cast<size_t>(i)).isNumber()) return false;
        values[i] = items.at(static_cast<size_t>(i)).getNumericValue();
    }
    // /Rect 是 [左 下 右 上]：下边与上边哪个大不一定（有些文件写反了），
    // 所以取两个 y 的较大值当「上边」，取两个 x 的较小值当「左边」。
    *top = std::max(values[1], values[3]);
    *left = std::min(values[0], values[2]);
    return true;
}

// 从 AcroForm 的字段树里收集终端域。
//
// 域的形状有三种，都要认：
//   * 字段本身就是 widget（最常见）：/FT、/T、/TU 都在字段字典上；
//   * 字段带 /Kids，kids 是 widget（单选组）：字段名与类型在字段上，widget 只有位置；
//   * 字段带 /Kids，kids 还是字段（嵌套域）：名字要拼成「父名.子名」。
//
// /T、/TU、/FT 都可以从父字段继承（PDF 32000-1 12.7.3.2）。
void collectFields(const QPDFObjectHandle& node, const QString& inheritedName,
                   const QString& inheritedTooltip, const QString& inheritedType, int depth,
                   QSet<int>* visited, QVector<FormFieldInfo>* fields) {
    if (!node.isDictionary() || depth > kMaxTreeDepth) return;
    if (fields->size() >= kMaxTreeElements) return;
    if (node.isIndirect()) {
        const int id = node.getObjectID();
        // 同一个字段对象被引用两次：多半是文件里有环，停手。
        if (visited->contains(id)) return;
        visited->insert(id);
    }

    const QString ownName = stringValue(node, "/T");
    const QString ownTooltip = stringValue(node, "/TU");
    const QString ownType = nameValue(node, "/FT");

    QString fullName = inheritedName;
    if (!ownName.isEmpty()) {
        fullName = inheritedName.isEmpty() ? ownName : inheritedName + QLatin1Char('.') + ownName;
    }
    const QString tooltip = ownTooltip.isEmpty() ? inheritedTooltip : ownTooltip;
    const QString type = ownType.isEmpty() ? inheritedType : ownType;

    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        QVector<QPDFObjectHandle> widgetKids;
        QVector<QPDFObjectHandle> fieldKids;
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            if (!kid.isDictionary()) continue;
            if (nameValue(kid, "/Subtype") == "Widget") widgetKids.append(kid);
            else fieldKids.append(kid);
        }
        if (!widgetKids.isEmpty()) {
            FormFieldInfo info;
            info.name = fullName;
            info.tooltip = tooltip;
            info.type = type;
            info.widgets = widgetKids;
            fields->append(info);
        } else if (fieldKids.isEmpty()) {
            // 有字段却没有 widget：域存在，但没有可挂标签的对象。
            FormFieldInfo info;
            info.name = fullName;
            info.tooltip = tooltip;
            info.type = type;
            fields->append(info);
        }
        for (const QPDFObjectHandle& kid : fieldKids) {
            collectFields(kid, fullName, tooltip, type, depth + 1, visited, fields);
        }
        return;
    }

    if (nameValue(node, "/Subtype") == "Widget") {
        FormFieldInfo info;
        info.name = fullName;
        info.tooltip = tooltip;
        info.type = type;
        info.widgets.append(node);
        fields->append(info);
        return;
    }

    // 是个字段，但没有 widget 注解：域是存在的，只是没有可挂标签的对象。
    // 照样收进来（结构树里要有一个对应的元素），并由调用方记一条问题。
    FormFieldInfo info;
    info.name = fullName;
    info.tooltip = tooltip;
    info.type = type;
    fields->append(info);
}

// 页序号 → 该页 /Annots 里的 widget 对象号。
QHash<int, int> widgetPageMap(const std::vector<QPDFObjectHandle>& pages) {
    QHash<int, int> map;
    for (size_t i = 0; i < pages.size(); ++i) {
        QPDFObjectHandle page = pages.at(i);
        if (!page.isDictionary()) continue;
        const QPDFObjectHandle annots = page.getKey("/Annots");
        if (!annots.isArray()) continue;
        for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
            if (!annot.isDictionary() || !annot.isIndirect()) continue;
            if (nameValue(annot, "/Subtype") != "Widget") continue;
            map.insert(annot.getObjectID(), static_cast<int>(i));
        }
    }
    return map;
}

// widget 在第几页。
//
// 先按对象号查（widget 一般是间接对象），查不到再退化到 /P 指向的页面。
int pageOfWidget(const QPDFObjectHandle& widget, const QHash<int, int>& byObjectId,
                 const std::vector<QPDFObjectHandle>& pages) {
    if (widget.isIndirect()) {
        const auto found = byObjectId.constFind(widget.getObjectID());
        if (found != byObjectId.constEnd()) return found.value();
    }
    const QPDFObjectHandle owner = widget.getKey("/P");
    if (owner.isIndirect()) {
        for (size_t i = 0; i < pages.size(); ++i) {
            if (!pages.at(i).isIndirect()) continue;
            if (pages.at(i).getObjectID() == owner.getObjectID()) return static_cast<int>(i);
        }
    }
    return -1;
}

// 把页 /Annots 里没进 AcroForm 的 widget 也算成表单域。
//
// 有些文件只在一头写（页上有 widget，AcroForm 里没有，或反过来）。
// 只认 AcroForm 会漏掉前者，只认页又会漏掉后者——两边都收，用对象号去重。
//
// used 是已经从 AcroForm 收走的 widget 对象号。
void collectLooseWidgets(const std::vector<QPDFObjectHandle>& pages, QSet<int> used,
                         QVector<FormFieldInfo>* fields) {
    for (size_t i = 0; i < pages.size(); ++i) {
        QPDFObjectHandle page = pages.at(i);
        if (!page.isDictionary()) continue;
        const QPDFObjectHandle annots = page.getKey("/Annots");
        if (!annots.isArray()) continue;
        for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
            if (!annot.isDictionary()) continue;
            if (nameValue(annot, "/Subtype") != "Widget") continue;
            if (annot.isIndirect() && used.contains(annot.getObjectID())) continue;
            FormFieldInfo info;
            info.name = stringValue(annot, "/T");
            info.tooltip = stringValue(annot, "/TU");
            info.type = nameValue(annot, "/FT");
            info.pageIndex = static_cast<int>(i);
            info.widgets.append(annot);
            fields->append(info);
            if (annot.isIndirect()) used.insert(annot.getObjectID());
        }
    }
}

// 摘掉一棵结构元素下面的 /Form 子元素（重跑时用来替换上一轮的标签）。
//
// 返回被摘掉的元素个数。摘掉的元素记在 removed 里——
// ParentTree 里指向它们的条目要一起清掉，不能留着一个指向空壳的下标。
//
// 句柄按值传：qpdf 的修改接口不是 const 方法，
// 而 QPDFObjectHandle 本身是共享句柄——副本改的是同一个对象。
int stripFormElements(QPDFObjectHandle parent, int depth, QSet<int>* visited,
                      QVector<QPDFObjectHandle>* removed) {
    if (!parent.isDictionary() || depth > kMaxTreeDepth) return 0;
    if (parent.isIndirect()) {
        const int id = parent.getObjectID();
        if (visited->contains(id)) return 0;
        visited->insert(id);
    }
    const QPDFObjectHandle kids = parent.getKey("/K");
    if (kids.isArray()) {
        std::vector<QPDFObjectHandle> kept;
        int count = 0;
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            if (!kid.isDictionary()) {
                kept.push_back(kid);
                continue;
            }
            if (nameValue(kid, "/S") == "Form") {
                removed->append(kid);
                ++count;
                continue;
            }
            count += stripFormElements(kid, depth + 1, visited, removed);
            kept.push_back(kid);
        }
        if (count > 0) parent.replaceKey("/K", QPDFObjectHandle::newArray(kept));
        return count;
    }
    if (kids.isDictionary()) {
        if (nameValue(kids, "/S") == "Form") {
            removed->append(kids);
            parent.replaceKey("/K", QPDFObjectHandle::newNull());
            return 1;
        }
        return stripFormElements(kids, depth + 1, visited, removed);
    }
    return 0;
}

// 树外面挂着的 /Form 元素（只能靠 /Parent 串起来的那种）按 /Parent 反查摘掉。
//
// 防环：父链上遇到已经访问过的对象就停手——
// 文件里的 /Parent 完全可能指回祖先（甚至两个元素互指）。
bool removeOrphanFormElement(QPDFObjectHandle element, int depth, QSet<int>* visited) {
    if (depth > kMaxTreeDepth) return false;
    QPDFObjectHandle parent = element.getKey("/Parent");
    if (!parent.isDictionary()) return false;
    if (parent.isIndirect()) {
        const int id = parent.getObjectID();
        if (visited->contains(id)) return false;
        visited->insert(id);
    }
    const QPDFObjectHandle kids = parent.getKey("/K");
    if (kids.isArray()) {
        std::vector<QPDFObjectHandle> kept;
        bool found = false;
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            if (sameObject(kid, element)) {
                found = true;
                continue;
            }
            kept.push_back(kid);
        }
        if (found) {
            parent.replaceKey("/K", QPDFObjectHandle::newArray(kept));
            return true;
        }
    } else if (kids.isDictionary() && sameObject(kids, element)) {
        parent.replaceKey("/K", QPDFObjectHandle::newNull());
        return true;
    }
    // 父元素的 /K 里没有它：再往上找一层（父链有可能是断的）。
    return removeOrphanFormElement(parent, depth + 1, visited);
}

// 把一棵数字树摊平成「键 → 值」。
//
// 键是页的 /StructParents，值是那一页的数组。
// 既认顶层 /Nums，也认 /Kids 分层的样子；摊平之后再按 /Nums 写回去，
// 语义不变（数字树的分层只是索引方式）。
void collectNumberTree(const QPDFObjectHandle& node, int depth, QSet<int>* visited,
                        QMap<int, QPDFObjectHandle>* out) {
    if (!node.isDictionary() || depth > kMaxTreeDepth) return;
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
                // 值是一棵子树：继续往下摊平。
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

// 在文档元素下面找「管着这一页」的 /Part 元素。
//
// 为什么按 /Pg 认页：/Part 元素自己写 /Pg，指的是它管的那些页。
// 不写 /Pg 的 /Part 认不出页（页只能从父元素继承，而父元素是文档元素，
// 文档元素本身没有 /Pg）——那种 /Part 不能当成这一页的部件。
//
// 找的是整棵子树：不只看文档元素的直接子节点。用户自己排好的树里，
// /Part 可能在章节下面；找到了就复用，不再另建一个。
QPDFObjectHandle findPartElement(const QPDFObjectHandle& parent, const QPDFObjectHandle& page,
                                 int depth, QSet<int>* visited) {
    if (!parent.isDictionary() || depth > kMaxTreeDepth) return QPDFObjectHandle();
    if (parent.isIndirect()) {
        const int id = parent.getObjectID();
        if (visited->contains(id)) return QPDFObjectHandle();
        visited->insert(id);
    }
    for (const QPDFObjectHandle& kid : kidList(parent.getKey("/K"))) {
        if (!kid.isDictionary()) continue;
        if (nameValue(kid, "/S") == "Part") {
            const QPDFObjectHandle pg = kid.getKey("/Pg");
            if (pg.isDictionary() && sameObject(pg, page)) return kid;
        }
        const QPDFObjectHandle found = findPartElement(kid, page, depth + 1, visited);
        if (found.isDictionary()) return found;
    }
    return QPDFObjectHandle();
}

// 新建一个 /Part 元素，挂到文档元素下面。
//
// 为什么 /Form 要挂在 /Part 下面而不是平铺在 /Document 下面：
// PDF/UA-1 的结构树是「文档 → 部件 → 页面内容」三层（§7.2、§7.3）。
// 表单域属于它所在的那一页，所以它该挂在那一页的 /Part 里；
// 平铺在 /Document 下，读屏软件就分不清这个域属于哪一页。
QPDFObjectHandle createPartElement(QPDF& pdf, QPDFObjectHandle documentElement,
                                    const QPDFObjectHandle& page) {
    QPDFObjectHandle part = QPDFObjectHandle::newDictionary();
    part.replaceKey("/Type", QPDFObjectHandle::newName("/StructElem"));
    part.replaceKey("/S", QPDFObjectHandle::newName("/Part"));
    part.replaceKey("/Pg", page);
    part.replaceKey("/P", documentElement);
    part = pdf.makeIndirectObject(part);

    std::vector<QPDFObjectHandle> kids;
    for (const QPDFObjectHandle& kid : kidList(documentElement.getKey("/K"))) kids.push_back(kid);
    kids.push_back(part);
    documentElement.replaceKey("/K", QPDFObjectHandle::newArray(kids));
    return part;
}

// 把结构元素挂到某个父元素的 /K 末尾（父元素已有的子节点原样保留）。
void appendChild(QPDFObjectHandle parent, const QVector<QPDFObjectHandle>& children) {
    if (children.isEmpty()) return;
    std::vector<QPDFObjectHandle> kids;
    for (const QPDFObjectHandle& kid : kidList(parent.getKey("/K"))) kids.push_back(kid);
    for (const QPDFObjectHandle& child : children) kids.push_back(child);
    parent.replaceKey("/K", QPDFObjectHandle::newArray(kids));
}

// 数一数结构树里的 /Form 元素（写完重开校验时用）。
int countFormElements(const QPDFObjectHandle& parent, int depth, QSet<int>* visited) {
    if (!parent.isDictionary() || depth > kMaxTreeDepth) return 0;
    if (parent.isIndirect()) {
        const int id = parent.getObjectID();
        if (visited->contains(id)) return 0;
        visited->insert(id);
    }
    int count = 0;
    for (const QPDFObjectHandle& kid : kidList(parent.getKey("/K"))) {
        if (!kid.isDictionary()) continue;
        if (nameValue(kid, "/S") == "Form") {
            ++count;
            continue;
        }
        count += countFormElements(kid, depth + 1, visited);
    }
    return count;
}

// 写完重开校验：结构树得在、/Form 元素数量得对得上、qpdf 也不能报警告。
//
// 为什么要回读校验：qpdf 对「操作被忽略」（例如往流对象上 replaceKey）只报警告，
// 不报的话就会把一份结构坏掉的文件当成成功交出去。
bool verifyWritten(const QByteArray& bytes, int expectedForms, QString* error) {
    try {
        QPDF check;
        check.processMemoryFile("form-auto-tag-check.pdf", bytes.constData(),
                                static_cast<size_t>(bytes.size()));
        if (check.anyWarnings()) {
            const std::vector<QPDFExc> warnings = check.getWarnings();
            if (error) {
                *error = QStringLiteral("写出的 PDF 结构有问题：%1")
                             .arg(warnings.empty() ? QStringLiteral("qpdf 没有给出细节")
                                                  : QString::fromStdString(warnings.front().what()));
            }
            return false;
        }
        const QPDFObjectHandle catalog = check.getRoot();
        const QPDFObjectHandle root = catalog.isDictionary() ? catalog.getKey("/StructTreeRoot")
                                                             : QPDFObjectHandle();
        if (!root.isDictionary()) {
            if (error) *error = QStringLiteral("写出的 PDF 里没有 /StructTreeRoot");
            return false;
        }
        QSet<int> visited;
        const int forms = countFormElements(root, 0, &visited);
        if (forms != expectedForms) {
            if (error) {
                *error = QStringLiteral("写出的 PDF 里 /Form 结构元素有 %1 个，与域数 %2 对不上")
                             .arg(forms)
                             .arg(expectedForms);
            }
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            *error = QStringLiteral("写出的 PDF 无法重新打开：%1").arg(QString::fromUtf8(exception.what()));
        }
        return false;
    }
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

FormAutoTagResult autoTagFormFields(const QByteArray& pdfBytes) {
    FormAutoTagResult result;
    if (pdfBytes.isEmpty()) {
        result.error = QStringLiteral("输入的 PDF 内容为空");
        return result;
    }

#ifndef AIPDF_WITH_QPDF
    result.error = QStringLiteral("当前构建没有启用 qpdf，无法给表单域写标签");
    return result;
#else
    try {
        QPDF pdf;
        pdf.processMemoryFile("form-auto-tag.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        result.pageCount = static_cast<int>(pages.size());
        if (pages.empty()) {
            result.error = QStringLiteral("PDF 里没有页面");
            return result;
        }
        QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            result.error = QStringLiteral("PDF 的根对象不是字典");
            return result;
        }

        // ---- 1. 读出表单域 ----
        QVector<FormFieldInfo> fields;
        {
            const QPDFObjectHandle acroForm = catalog.getKey("/AcroForm");
            if (acroForm.isDictionary()) {
                const QPDFObjectHandle list = acroForm.getKey("/Fields");
                if (list.isArray()) {
                    QSet<int> visited;
                    for (const QPDFObjectHandle& field : list.getArrayAsVector()) {
                        collectFields(field, QString(), QString(), QString(), 0, &visited, &fields);
                    }
                }
            }
        }
        QSet<int> usedWidgetIds;
        for (const FormFieldInfo& field : fields) {
            for (const QPDFObjectHandle& widget : field.widgets) {
                if (widget.isIndirect()) usedWidgetIds.insert(widget.getObjectID());
            }
        }
        collectLooseWidgets(pages, usedWidgetIds, &fields);

        if (fields.isEmpty()) {
            // 一个域都没有：写一棵空标签树没有意义（读屏软件读不到任何东西），
            // 直接失败并说清楚为什么。
            result.error = QStringLiteral(
                "这份 PDF 里没有表单域（没有 AcroForm 字段，页面上也没有 widget 注解），没有可打标签的对象");
            return result;
        }

        const QHash<int, int> pageByWidget = widgetPageMap(pages);
        for (FormFieldInfo& field : fields) {
            if (field.pageIndex >= 0) continue;
            for (const QPDFObjectHandle& widget : field.widgets) {
                const int pageIndex = pageOfWidget(widget, pageByWidget, pages);
                if (pageIndex >= 0) {
                    field.pageIndex = pageIndex;
                    break;
                }
            }
        }

        // 阅读顺序：先按页，再按页面上从上到下、从左到右。
        //
        // 为什么要在这一步排：读屏软件按结构树里的次序念，
        // 域的次序必须跟人眼睛扫过去的次序一致，否则填表的人会填错行。
        // 域在 AcroForm 里的次序是**表单定义**的次序，跟页面上的位置没关系。
        for (FormFieldInfo& field : fields) {
            for (const QPDFObjectHandle& widget : field.widgets) {
                double top = 0.0;
                double left = 0.0;
                if (!readingPositionOfWidget(widget, &top, &left)) continue;
                field.readingTop = top;
                field.readingLeft = left;
                field.hasPosition = true;
                break;
            }
        }
        std::stable_sort(fields.begin(), fields.end(), [](const FormFieldInfo& left,
                                                         const FormFieldInfo& right) {
            if (left.pageIndex != right.pageIndex) return left.pageIndex < right.pageIndex;
            // 找不到页的域排在最后：它们挂不到任何一页的 /Part 下面。
            if (left.pageIndex < 0) return false;
            if (right.pageIndex < 0) return true;
            if (left.hasPosition != right.hasPosition) return left.hasPosition;
            if (!left.hasPosition) return false;
            if (std::abs(left.readingTop - right.readingTop) > 0.5) {
                return left.readingTop > right.readingTop;  // y 越大越靠上
            }
            return left.readingLeft < right.readingLeft;
        });

        // ---- 2. 准备标签树（合并语义）----
        //
        // 文档本来就有标签树 → 合并：树里原有的其它结构元素（/P、/H1、/Span 等）
        // 与既有阅读顺序原样保留，只把与表单域相关的 /Form 元素换成新的。
        // 文档本来没有标签树 → 新建一棵（老行为）。
        const QPDFObjectHandle existingRoot = catalog.getKey("/StructTreeRoot");
        const bool hadExistingTree = existingRoot.isDictionary();
        result.mergedIntoExistingTree = hadExistingTree;

        QPDFObjectHandle root = existingRoot;
        if (!root.isDictionary()) {
            root = QPDFObjectHandle::newDictionary();
            root.replaceKey("/Type", QPDFObjectHandle::newName("/StructTreeRoot"));
        }
        if (!root.isIndirect()) {
            root = pdf.makeIndirectObject(root);
        }
        catalog.replaceKey("/StructTreeRoot", root);

        // 摘掉上一轮（或别的工具）写下的 /Form 元素：重跑不能叠加。
        QVector<QPDFObjectHandle> removedForms;
        QSet<int> visitedTree;
        {
            const QPDFObjectHandle rootKids = root.getKey("/K");
            std::vector<QPDFObjectHandle> kept;
            bool rootChanged = false;
            if (rootKids.isArray()) {
                for (const QPDFObjectHandle& kid : rootKids.getArrayAsVector()) {
                    if (!kid.isDictionary()) {
                        kept.push_back(kid);
                        continue;
                    }
                    if (nameValue(kid, "/S") == "Form") {
                        removedForms.append(kid);
                        rootChanged = true;
                        continue;
                    }
                    stripFormElements(kid, 0, &visitedTree, &removedForms);
                    kept.push_back(kid);
                }
            } else if (rootKids.isDictionary()) {
                if (nameValue(rootKids, "/S") == "Form") {
                    removedForms.append(rootKids);
                    rootChanged = true;
                } else {
                    stripFormElements(rootKids, 0, &visitedTree, &removedForms);
                    kept.push_back(rootKids);
                }
            }
            if (rootChanged) root.replaceKey("/K", QPDFObjectHandle::newArray(kept));
        }

        // 树外面挂着的 /Form 元素：按 /Parent 反查摘掉（父链有环也不怕）。
        {
            QSet<int> removedIds;
            for (const QPDFObjectHandle& element : removedForms) {
                if (element.isIndirect()) removedIds.insert(element.getObjectID());
            }
            for (const QPDFObjectHandle& object : pdf.getAllObjects()) {
                if (!object.isDictionary() || !object.isIndirect()) continue;
                if (nameValue(object, "/S") != "Form") continue;
                if (removedIds.contains(object.getObjectID())) continue;
                if (visitedTree.contains(object.getObjectID())) continue;
                removeOrphanFormElement(object, 0, &visitedTree);
                removedForms.append(object);
            }
        }

        // Document 元素：表单域的结构元素挂在它下面。
        // 整棵树里找（不只看根的下一层）：有的文件把文档元素放在章节下面，
        // 漏掉它就会在同一份文件里造出两棵文档分支。
        QPDFObjectHandle documentElement;
        {
            QSet<int> findVisited;
            QPDFObjectHandle found;
            if (findDocumentElement(root, 0, &findVisited, &found)) documentElement = found;
        }
        bool createdDocumentElement = false;
        if (!documentElement.isDictionary()) {
            createdDocumentElement = true;
            documentElement = QPDFObjectHandle::newDictionary();
            documentElement.replaceKey("/Type", QPDFObjectHandle::newName("/StructElem"));
            documentElement.replaceKey("/S", QPDFObjectHandle::newName("/Document"));
            documentElement.replaceKey("/P", root);
            documentElement = pdf.makeIndirectObject(documentElement);
            std::vector<QPDFObjectHandle> rootKids;
            for (const QPDFObjectHandle& kid : kidList(root.getKey("/K"))) rootKids.push_back(kid);
            rootKids.push_back(documentElement);
            root.replaceKey("/K", QPDFObjectHandle::newArray(rootKids));
        }

        // ---- 3. 每个域写一个 /Form 元素，挂到它所在那一页的 /Part 下面 ----
        //
        // 先给「有表单域的页」准备 /Part：有的复用（用户自己排好的），
        // 有的新建（文档本来没有这一层的）。
        QMap<int, QPDFObjectHandle> partOfPage;
        int createdPartCount = 0;
        {
            QVector<int> pagesWithFields;
            for (const FormFieldInfo& field : fields) {
                if (field.pageIndex < 0 || field.pageIndex >= static_cast<int>(pages.size())) continue;
                if (!pagesWithFields.contains(field.pageIndex)) pagesWithFields.append(field.pageIndex);
            }
            for (int pageIndex : pagesWithFields) {
                const QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));
                QSet<int> findVisited;
                QPDFObjectHandle part = findPartElement(documentElement, page, 0, &findVisited);
                if (!part.isDictionary()) {
                    part = createPartElement(pdf, documentElement, page);
                    ++createdPartCount;
                }
                partOfPage.insert(pageIndex, part);
            }
        }

        std::vector<QPDFObjectHandle> formRefs;
        // 页序号 → 这一页的 /Form 元素（按阅读顺序）。
        QMap<int, QVector<QPDFObjectHandle>> formsByPage;
        // 找不到所在页的域：它们的结构元素挂在文档元素下面。
        QVector<QPDFObjectHandle> orphanFormRefs;
        struct Pending {
            int pageIndex = -1;
            QPDFObjectHandle widget;
            QPDFObjectHandle element;
        };
        QVector<Pending> pending;

        for (const FormFieldInfo& field : fields) {
            QString alt;
            if (!field.tooltip.isEmpty()) {
                alt = field.tooltip;
            } else if (!field.name.isEmpty()) {
                alt = field.name;
            } else {
                // 两者都没有：没有可用的替代文本。如实记下来，不替它编一句。
                const QString label = field.pageIndex >= 0
                                         ? QStringLiteral("第 %1 页的表单域").arg(field.pageIndex + 1)
                                         : QStringLiteral("未命名的表单域");
                result.issues.append({label,
                                      QStringLiteral("这个表单域没有可用的替代文本：既没有提示文字（/TU）"
                                                     "也没有字段名（/T），没有替它编一句")});
            }

            QPDFObjectHandle element = QPDFObjectHandle::newDictionary();
            element.replaceKey("/Type", QPDFObjectHandle::newName("/StructElem"));
            element.replaceKey("/S", QPDFObjectHandle::newName("/Form"));
            if (!field.name.isEmpty()) {
                element.replaceKey("/T",
                                   QPDFObjectHandle::newUnicodeString(field.name.toUtf8().toStdString()));
            }
            if (!alt.isEmpty()) {
                element.replaceKey("/Alt",
                                   QPDFObjectHandle::newUnicodeString(alt.toUtf8().toStdString()));
            }
            if (field.pageIndex >= 0 && field.pageIndex < static_cast<int>(pages.size())) {
                element.replaceKey("/Pg", pages.at(static_cast<size_t>(field.pageIndex)));
            }
            // /K 是 widget 注解：规范要求表单域结构元素的子节点就是 widget。
            std::vector<QPDFObjectHandle> widgetRefs;
            for (const QPDFObjectHandle& widget : field.widgets) widgetRefs.push_back(widget);
            element.replaceKey("/K", QPDFObjectHandle::newArray(widgetRefs));

            // 挂在所属页的 /Part 下面（PDF/UA-1 §7.3：部件元素装着它管的页的内容）。
            // 找不到所在页的域退回到文档元素下面——它不属于任何一页，
            // 但结构树上还得有个落脚点，不能凭空消失。
            const QPDFObjectHandle parent = partOfPage.value(field.pageIndex);
            element.replaceKey("/P", parent.isDictionary() ? parent : documentElement);

            const QPDFObjectHandle elementRef = pdf.makeIndirectObject(element);
            formRefs.push_back(elementRef);
            if (parent.isDictionary()) {
                formsByPage[field.pageIndex].append(elementRef);
            } else {
                orphanFormRefs.append(elementRef);
            }
            for (const QPDFObjectHandle& widget : field.widgets) {
                pending.append({field.pageIndex, widget, elementRef});
            }

            if (field.widgets.isEmpty()) {
                // 域存在却没有 widget：结构元素写了，但读屏软件没有可指向的对象。
                const QString label = field.name.isEmpty() ? QStringLiteral("未命名的表单域")
                                                          : field.name;
                result.issues.append({label,
                                      QStringLiteral("这个表单域没有 widget 注解，没有可挂结构标签的对象，"
                                                     "只写了一个空的表单域结构元素")});
            } else if (field.pageIndex < 0) {
                // 找不到所在页：没法确定它属于哪一页的父树，/StructParent 挂不上。
                const QString label = field.name.isEmpty() ? QStringLiteral("未命名的表单域")
                                                          : field.name;
                result.issues.append({label,
                                      QStringLiteral("这个表单域的 widget 注解找不到所在页面，"
                                                     "没能挂上 /StructParent（读屏软件可能找不到它）")});
            }
        }

        // ---- 4. 把 widget 挂进页的 ParentTree ----
        //
        // widget 的 /StructParent 是「这一页的 ParentTree 数组里的下标」，
        // 那个下标对应的元素就是它的结构元素。数组里已有的内容（别的工具写的
        // MCID 条目）要原样保留，所以新下标只能排在它们后面。
        QPDFObjectHandle parentTree = root.getKey("/ParentTree");
        if (!parentTree.isDictionary()) parentTree = QPDFObjectHandle::newDictionary();
        if (!parentTree.isIndirect()) parentTree = pdf.makeIndirectObject(parentTree);
        root.replaceKey("/ParentTree", parentTree);

        QMap<int, QPDFObjectHandle> entries;
        {
            QSet<int> visitedNumbers;
            collectNumberTree(parentTree, 0, &visitedNumbers, &entries);
        }

        // 每页的 ParentTree 键：沿用页上已有的 /StructParents，没有就另分配一个。
        QVector<int> pageKey(pages.size(), -1);
        {
            QSet<int> treeKeys;
            for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) treeKeys.insert(it.key());
            QSet<int> claimed;
            for (size_t i = 0; i < pages.size(); ++i) {
                QPDFObjectHandle page = pages.at(i);
                int key = -1;
                if (page.isDictionary()) {
                    const QPDFObjectHandle structParents = page.getKey("/StructParents");
                    if (structParents.isInteger()) {
                        const int candidate = static_cast<int>(structParents.getIntValue());
                        // 两个页抢同一个键是坏文件：后一个另分配。
                        if (!claimed.contains(candidate)) key = candidate;
                    }
                }
                if (key < 0) {
                    key = 0;
                    while (treeKeys.contains(key) || claimed.contains(key)) ++key;
                }
                claimed.insert(key);
                pageKey[static_cast<int>(i)] = key;
                if (page.isDictionary()) {
                    page.replaceKey("/StructParents", QPDFObjectHandle::newInteger(key));
                }
            }
        }

        QSet<int> removedIds;
        for (const QPDFObjectHandle& element : removedForms) {
            if (element.isIndirect()) removedIds.insert(element.getObjectID());
        }

        QMap<int, QVector<QPDFObjectHandle>> arrays;
        QSet<int> loadedKeys;
        for (Pending& item : pending) {
            if (item.pageIndex < 0 || item.pageIndex >= static_cast<int>(pages.size())) continue;
            const int key = pageKey[item.pageIndex];
            if (!loadedKeys.contains(key)) {
                loadedKeys.insert(key);
                const auto existing = entries.constFind(key);
                if (existing != entries.constEnd() && existing.value().isArray()) {
                    for (const QPDFObjectHandle& item2 : existing.value().getArrayAsVector()) {
                        arrays[key].append(item2);
                    }
                }
            }
            QVector<QPDFObjectHandle>& array = arrays[key];

            // 复用 widget 原有的下标（那是上一轮写的），别去动别人的条目。
            int structParent = -1;
            const QPDFObjectHandle oldValue = item.widget.getKey("/StructParent");
            if (oldValue.isInteger()) {
                const int candidate = static_cast<int>(oldValue.getIntValue());
                if (candidate >= 0) {
                    if (candidate >= array.size()) {
                        while (array.size() <= candidate) array.append(QPDFObjectHandle::newNull());
                        structParent = candidate;
                    } else {
                        const QPDFObjectHandle entry = array.at(candidate);
                        const bool reusable = entry.isNull() ||
                                             (entry.isIndirect() && removedIds.contains(entry.getObjectID()));
                        if (reusable) structParent = candidate;
                    }
                }
            }
            if (structParent < 0) {
                structParent = array.size();
                array.append(QPDFObjectHandle::newNull());
            }
            array[structParent] = item.element;
            item.widget.replaceKey("/StructParent", QPDFObjectHandle::newInteger(structParent));
        }

        for (auto it = arrays.constBegin(); it != arrays.constEnd(); ++it) {
            std::vector<QPDFObjectHandle> items;
            for (const QPDFObjectHandle& item : it.value()) items.push_back(item);
            entries.insert(it.key(), QPDFObjectHandle::newArray(items));
        }

        std::vector<QPDFObjectHandle> nums;
        int maxKey = -1;
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            nums.push_back(QPDFObjectHandle::newInteger(it.key()));
            nums.push_back(it.value());
            maxKey = std::max(maxKey, it.key());
        }
        parentTree.replaceKey("/Nums", QPDFObjectHandle::newArray(nums));
        // 摊平之后不再有分层：留着 /Kids 会让 qpdf 按旧结构再解释一遍。
        parentTree.replaceKey("/Kids", QPDFObjectHandle::newNull());
        root.replaceKey("/ParentTreeNextKey", QPDFObjectHandle::newInteger(maxKey + 1));

        // 表单域的结构元素挂到所属页的 /Part 下面（旧的 /Form 已经被摘掉了）。
        //
        // 挂载顺序 = 阅读顺序：fields 已经按页面上从上到下排好，
        // 所以同一页的 /Form 元素按这个次序追加，读屏念的次序就跟人看的一致。
        for (auto it = formsByPage.constBegin(); it != formsByPage.constEnd(); ++it) {
            appendChild(partOfPage.value(it.key()), it.value());
        }
        // 找不到所在页的域：挂在文档元素下面（它不属于任何一页）。
        appendChild(documentElement, orphanFormRefs);

        // 合并完成之后**再数一遍**既有结构元素，报给调用方的是最终那棵树里的实数。
        //
        // 为什么不在合并前就把数字报出去：那样等于让实现自己给自己背书——
        // 就算真把树重建了，它也照样报「保留了 N 个」。合并后数，
        // 实现做错了数字会跟着错，测试当场就能看出来。
        if (hadExistingTree) {
            QMap<QString, int> preservedCounts;
            QSet<int> countVisited;
            collectPreservedTags(root, 0, &countVisited, &preservedCounts);
            if (createdDocumentElement) {
                // 新建的文档元素不是「既有元素」，不算进去。
                const auto document = preservedCounts.find(QStringLiteral("Document"));
                if (document != preservedCounts.constEnd() && document.value() > 0) {
                    preservedCounts[QStringLiteral("Document")] = document.value() - 1;
                }
            }
            if (createdPartCount > 0) {
                // 新建的 /Part 元素同样不是「既有元素」：它是这次为了挂表单域才补的。
                const auto part = preservedCounts.find(QStringLiteral("Part"));
                if (part != preservedCounts.constEnd()) {
                    preservedCounts[QStringLiteral("Part")] =
                        std::max(0, part.value() - createdPartCount);
                }
            }
            for (auto it = preservedCounts.constBegin(); it != preservedCounts.constEnd(); ++it) {
                if (it.value() <= 0) continue;
                result.preservedTags.append({it.key(), it.value()});
                result.preservedElementCount += it.value();
            }
        }

        // 文档是「带标签」的：读屏软件据此决定要不要走结构树。
        {
            QPDFObjectHandle markInfo = catalog.getKey("/MarkInfo");
            if (!markInfo.isDictionary()) markInfo = QPDFObjectHandle::newDictionary();
            markInfo.replaceKey("/Marked", QPDFObjectHandle::newBool(true));
            catalog.replaceKey("/MarkInfo", markInfo);
        }

        // ---- 5. 写出并回读校验 ----
        QPDFWriter writer(pdf);
        // 关掉对象流：交叉引用表保持传统形态，出问题时人眼能直接看。
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> output(writer.getBuffer());
        if (!output || output->getSize() == 0) {
            result.error = QStringLiteral("qpdf 写出的 PDF 为空");
            return result;
        }
        const QByteArray written(reinterpret_cast<const char*>(output->getBuffer()),
                                  static_cast<qsizetype>(output->getSize()));

        QString verifyError;
        if (!verifyWritten(written, static_cast<int>(formRefs.size()), &verifyError)) {
            result.error = verifyError;
            return result;
        }

        result.bytes = written;
        result.taggedFieldCount = static_cast<int>(formRefs.size());
        result.ok = true;
        return result;
    } catch (const std::exception& exception) {
        result.error = QStringLiteral("解析 PDF 失败：%1").arg(QString::fromUtf8(exception.what()));
        return result;
    }
#endif
}

}  // namespace aipdf::pdf
