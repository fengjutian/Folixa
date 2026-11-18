#include "pdf/ops/OptionalContent.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

namespace aipdf::pdf {
namespace {

// 失败统一走这里：把中文原因写进 error，返回 false。
void setError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

#ifdef AIPDF_WITH_QPDF

// 取名字对象的值（带前导斜杠，例如 "/AnyOn"）。不是名字就返回空串。
QString nameOf(const QPDFObjectHandle& handle) {
    if (!handle.isName()) return QString();
    return QString::fromStdString(handle.getName());
}

// 取文本串的值（UTF-8）。不是文本串就返回空串。
QString textOf(const QPDFObjectHandle& handle) {
    if (!handle.isString()) return QString();
    return QString::fromUtf8(handle.getUTF8Value().c_str());
}

// 收集一个数组里所有对象的 id（用来判断某个 OCG 在不在 /ON、/OFF 数组里）。
void collectIds(const QPDFObjectHandle& handle, std::set<int>* ids) {
    if (!handle.isArray()) return;
    for (const QPDFObjectHandle& item : handle.getArrayAsVector()) {
        if (!item.isIndirect()) continue;
        ids->insert(item.getObjectID());
    }
}

// 算出某个 OCG 的可见性。
//
// 规则（ISO 32000-1 §8.11.4.3）：/BaseState 定基调，/ON 与 /OFF 数组是例外。
// 所以先按 /BaseState 定，再让数组里的例外覆盖它。
bool resolveVisibility(const QPDFObjectHandle& ocg, const std::set<int>& onIds,
                       const std::set<int>& offIds, const QString& baseState) {
    const int id = ocg.isIndirect() ? ocg.getObjectID() : -1;
    if (id >= 0 && onIds.count(id) > 0) return true;
    if (id >= 0 && offIds.count(id) > 0) return false;
    // 没有默认配置时，看 OCG 字典自己有没有 /ON 或 /OFF。
    if (baseState.isEmpty()) {
        if (ocg.getKey("/OFF").isBool()) return !ocg.getKey("/OFF").getBoolValue();
        if (ocg.getKey("/ON").isBool()) return ocg.getKey("/ON").getBoolValue();
        return true;
    }
    if (baseState == QLatin1String("/OFF")) return false;
    return true;
}

// 走一遍 /Order，给每个 OCG 记下它在图层面板里的次序。
//
// /Order 里可能是 OCG 字典、名字（只是分组标题）、或者嵌套数组（子图层）。
void collectOrder(const QPDFObjectHandle& node, std::map<int, int>* orderById, int* counter,
                  int depth) {
    if (depth > 8) return; // 坏文件里的 /Order 可能套很深，不设限会把自己栈爆掉
    if (node.isArray()) {
        for (const QPDFObjectHandle& item : node.getArrayAsVector()) {
            collectOrder(item, orderById, counter, depth + 1);
        }
        return;
    }
    if (!node.isDictionary()) return; // 名字（分组标题）不参与次序编号
    if (!node.isIndirect()) return;
    (*orderById)[node.getObjectID()] = *counter;
    *counter += 1;
}

// 页面自己的内容流（不合并，合并会把多条流并成一条，下标就失去意义了）。
std::vector<QPDFObjectHandle> pageContentStreams(const QPDFObjectHandle& page) {
    std::vector<QPDFObjectHandle> result;
    const QPDFObjectHandle contents = page.getKey("/Contents");
    if (contents.isArray()) {
        for (const QPDFObjectHandle& item : contents.getArrayAsVector()) result.push_back(item);
    } else if (contents.isStream()) {
        result.push_back(contents);
    }
    return result;
}

// 属性键名：图层名只含字母数字下划线连字符时直接用它，否则用 "/OC<序号>"。
//
// 为什么不用图层名本身当名字对象：名字对象在 PDF 里只能是字节串，
// 图层名是给人看的文本串，中文塞进名字对象要么被转义成一串 #xx（人眼读不了），
// 要么碰上不认的阅读器。键名与图层名的对应关系由页面 /Resources /Properties 负责。
QString propertyKeyFor(const QString& layerName, int layerIndex) {
    bool safe = !layerName.isEmpty();
    for (const QChar ch : layerName) {
        const ushort code = ch.unicode();
        const bool allowed = (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z') ||
                             (code >= '0' && code <= '9') || code == '_' || code == '-';
        if (!allowed) {
            safe = false;
            break;
        }
    }
    if (safe) return QLatin1Char('/') + layerName;
    return QStringLiteral("/OC%1").arg(layerIndex + 1);
}

// 在页面 /Resources /Properties 里加一项：属性键名 → OCG 字典。
void registerProperty(QPDFObjectHandle& page, const QString& key, const QPDFObjectHandle& value) {
    QPDFObjectHandle resources = page.getKey("/Resources");
    if (!resources.isDictionary()) {
        resources = QPDFObjectHandle::newDictionary();
        page.replaceKey("/Resources", resources);
    }
    QPDFObjectHandle properties = resources.getKey("/Properties");
    if (!properties.isDictionary()) {
        properties = QPDFObjectHandle::newDictionary();
        resources.replaceKey("/Properties", properties);
    }
    properties.replaceKey(key.toStdString(), value);
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

OptionalContentSummary readOptionalContent(const QByteArray& pdfBytes, QString* error) {
    OptionalContentSummary summary;
    if (error != nullptr) error->clear();
    if (pdfBytes.isEmpty()) {
        setError(error, QStringLiteral("没有可读取的 PDF 内容"));
        return summary;
    }
#ifndef AIPDF_WITH_QPDF
    setError(error, QStringLiteral("本次构建没有启用 qpdf，可选内容图层不可用"));
    return summary;
#else
    try {
        QPDF pdf;
        pdf.processMemoryFile("optional-content-read.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            setError(error, QStringLiteral("PDF 的根对象不是字典"));
            return summary;
        }

        const QPDFObjectHandle ocProperties = catalog.getKey("/OCProperties");
        // 没有 /OCProperties 是常态（绝大多数文档都没有图层）：不是错误。
        if (ocProperties.isNull()) return summary;
        if (!ocProperties.isDictionary()) {
            setError(error, QStringLiteral("PDF 的 /OCProperties 不是字典"));
            return summary;
        }
        summary.hasOptionalContent = true;

        // ---- 默认配置（/D）----
        const QPDFObjectHandle config = ocProperties.getKey("/D");
        std::set<int> onIds;
        std::set<int> offIds;
        if (config.isDictionary()) {
            summary.baseState = nameOf(config.getKey("/BaseState"));
            summary.listMode = nameOf(config.getKey("/ListMode"));
            collectIds(config.getKey("/ON"), &onIds);
            collectIds(config.getKey("/OFF"), &offIds);
        }

        // ---- 图层（/OCGs）----
        std::vector<QPDFObjectHandle> ocgList;
        const QPDFObjectHandle ocgs = ocProperties.getKey("/OCGs");
        if (ocgs.isArray()) ocgList = ocgs.getArrayAsVector();

        std::map<int, int> orderById;
        if (config.isDictionary()) {
            int counter = 0;
            collectOrder(config.getKey("/Order"), &orderById, &counter, 0);
        }

        for (const QPDFObjectHandle& ocg : ocgList) {
            if (!ocg.isDictionary()) continue;
            OptionalContentGroup group;
            group.name = textOf(ocg.getKey("/Name"));
            group.intent = nameOf(ocg.getKey("/Intent"));
            if (group.intent.startsWith(QLatin1Char('/'))) group.intent = group.intent.mid(1);
            group.visible = resolveVisibility(ocg, onIds, offIds, summary.baseState);
            if (ocg.isIndirect()) {
                const auto found = orderById.find(ocg.getObjectID());
                if (found != orderById.end()) group.orderIndex = found->second;
            }
            summary.groups.append(group);
        }

        // ---- 页面级设置 ----
        const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
        for (size_t i = 0; i < pages.size(); ++i) {
            const QPDFObjectHandle page = pages.at(i);
            OptionalContentPage info;
            info.pageIndex = static_cast<int>(i);
            bool any = false;

            const QPDFObjectHandle oc = page.getKey("/OC");
            if (oc.isArray()) {
                for (const QPDFObjectHandle& item : oc.getArrayAsVector()) {
                    if (!item.isDictionary()) continue;
                    OptionalContentCommand command;
                    const QString policy = nameOf(item.getKey("/P"));
                    command.policy = policy.isEmpty() ? QStringLiteral("AnyOn")
                                                      : policy.mid(policy.startsWith(QLatin1Char('/'))
                                                                   ? 1
                                                                   : 0);
                    const QPDFObjectHandle groups = item.getKey("/OCGs");
                    if (groups.isArray()) {
                        for (const QPDFObjectHandle& group : groups.getArrayAsVector()) {
                            if (!group.isDictionary()) continue;
                            command.groupNames.append(textOf(group.getKey("/Name")));
                        }
                    }
                    const QPDFObjectHandle expression = item.getKey("/VE");
                    if (!expression.isNull()) {
                        command.visibilityExpression = QString::fromStdString(expression.unparse());
                    }
                    info.commands.append(command);
                    any = true;
                }
            }

            const QPDFObjectHandle resources = page.getKey("/Resources");
            if (resources.isDictionary()) {
                const QPDFObjectHandle properties = resources.getKey("/Properties");
                if (properties.isDictionary()) {
                    for (const std::string& key : properties.getKeys()) {
                        const QPDFObjectHandle value = properties.getKey(key);
                        if (!value.isDictionary()) continue;
                        const QString groupName = textOf(value.getKey("/Name"));
                        if (groupName.isEmpty()) continue;
                        OptionalContentProperty property;
                        property.key = QString::fromStdString(key);
                        property.groupName = groupName;
                        info.properties.append(property);
                        any = true;
                    }
                }
            }

            if (any) summary.pages.append(info);
        }
        return summary;
    } catch (const std::exception& exception) {
        setError(error, QStringLiteral("读取可选内容图层失败：%1")
                       .arg(QString::fromUtf8(exception.what())));
        return summary;
    }
#endif
}

QByteArray writeOptionalContent(const QByteArray& pdfBytes, const OptionalContentPlan& plan,
                                 QString* error) {
    if (error != nullptr) error->clear();
    if (pdfBytes.isEmpty()) {
        setError(error, QStringLiteral("没有可写入的 PDF 内容"));
        return {};
    }
#ifndef AIPDF_WITH_QPDF
    setError(error, QStringLiteral("本次构建没有启用 qpdf，可选内容图层不可用"));
    return {};
#else
    try {
        if (plan.layers.isEmpty()) {
            setError(error, QStringLiteral("没有要写入的图层"));
            return {};
        }

        // 图层名不能为空、不能重名：属性键名与图层名是一一对应的，重名就分不清了。
        QHash<QString, int> layerIndexByName;
        QHash<QString, QString> propertyKeyByName;
        QSet<QString> usedKeys;
        for (int i = 0; i < plan.layers.size(); ++i) {
            const QString name = plan.layers.at(i).name.trimmed();
            if (name.isEmpty()) {
                setError(error, QStringLiteral("第 %1 个图层没有名字").arg(i + 1));
                return {};
            }
            if (layerIndexByName.contains(name)) {
                setError(error, QStringLiteral("图层名「%1」重复").arg(name));
                return {};
            }
            layerIndexByName.insert(name, i);
            const QString key = propertyKeyFor(name, i);
            if (usedKeys.contains(key)) {
                setError(error, QStringLiteral("图层「%1」的属性键名「%2」与别的图层撞了")
                                   .arg(name, key));
                return {};
            }
            usedKeys.insert(key);
            propertyKeyByName.insert(name, key);
        }

        QPDF pdf;
        pdf.processMemoryFile("optional-content.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            setError(error, QStringLiteral("PDF 的根对象不是字典"));
            return {};
        }
        const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
        if (pages.empty()) {
            setError(error, QStringLiteral("PDF 里没有页面"));
            return {};
        }

        // ---- 建 OCG 字典 ----
        std::vector<QPDFObjectHandle> ocgRefs;
        QHash<QString, QPDFObjectHandle> ocgByName;
        for (const OptionalContentLayer& layer : plan.layers) {
            QPDFObjectHandle ocg = QPDFObjectHandle::newDictionary();
            ocg.replaceKey("/Type", QPDFObjectHandle::newName("/OCG"));
            ocg.replaceKey("/Name", QPDFObjectHandle::newUnicodeString(layer.name.toUtf8().constData()));
            const QString intent = layer.intent.trimmed();
            if (!intent.isEmpty()) {
                const QString withSlash = intent.startsWith(QLatin1Char('/')) ? intent : QLatin1Char('/') + intent;
                ocg.replaceKey("/Intent", QPDFObjectHandle::newName(withSlash.toUtf8().constData()));
            }
            // OCG 字典里的 /ON /OFF 镜像：真正管用的是下面的 /D 默认配置，
            // 这里写一份是为了让只看 OCG 字典的工具也能读出默认可见性。
            if (layer.defaultVisible) {
                ocg.replaceKey("/ON", QPDFObjectHandle::newBool(true));
            } else {
                ocg.replaceKey("/OFF", QPDFObjectHandle::newBool(true));
            }
            const QPDFObjectHandle reference = pdf.makeIndirectObject(ocg);
            ocgRefs.push_back(reference);
            ocgByName.insert(layer.name, reference);
        }

        // ---- 默认配置（/D）----
        std::vector<QPDFObjectHandle> onRefs;
        std::vector<QPDFObjectHandle> offRefs;
        for (int i = 0; i < plan.layers.size(); ++i) {
            if (plan.layers.at(i).defaultVisible) {
                onRefs.push_back(ocgRefs.at(static_cast<size_t>(i)));
            } else {
                offRefs.push_back(ocgRefs.at(static_cast<size_t>(i)));
            }
        }
        QPDFObjectHandle config = QPDFObjectHandle::newDictionary();
        config.replaceKey("/BaseState", QPDFObjectHandle::newName("/ON"));
        config.replaceKey("/ON", QPDFObjectHandle::newArray(onRefs));
        config.replaceKey("/OFF", QPDFObjectHandle::newArray(offRefs));
        // /Order 就是图层面板里的顺序：按写入的顺序排。
        config.replaceKey("/Order", QPDFObjectHandle::newArray(ocgRefs));
        config.replaceKey("/ListMode", QPDFObjectHandle::newName("/AllPages"));

        QPDFObjectHandle ocProperties = QPDFObjectHandle::newDictionary();
        ocProperties.replaceKey("/OCGs", QPDFObjectHandle::newArray(ocgRefs));
        ocProperties.replaceKey("/D", pdf.makeIndirectObject(config));
        catalog.replaceKey("/OCProperties", pdf.makeIndirectObject(ocProperties));
        if (plan.openLayerPanel) {
            catalog.replaceKey("/PageMode", QPDFObjectHandle::newName("/UseOC"));
        }

        // ---- 内容流标记 ----
        // 同一页只允许一个「整页」标记：一页的内容不可能同时属于两个图层。
        QSet<int> wholePageMarks;
        for (const OptionalContentMark& mark : plan.marks) {
            if (mark.pageIndex < 0 || mark.pageIndex >= static_cast<int>(pages.size())) {
                setError(error, QStringLiteral("内容标记的页码超出范围（文档共 %1 页）")
                                   .arg(pages.size()));
                return {};
            }
            if (!ocgByName.contains(mark.layerName)) {
                setError(error, QStringLiteral("内容标记引用了没有定义的图层「%1」")
                                   .arg(mark.layerName));
                return {};
            }
            if (mark.contentStreamIndex < 0) {
                if (wholePageMarks.contains(mark.pageIndex)) {
                    setError(error,
                             QStringLiteral("第 %1 页有多个「整页」标记，一页的内容不能同时属于多个图层")
                                 .arg(mark.pageIndex + 1));
                    return {};
                }
                wholePageMarks.insert(mark.pageIndex);
            }
        }

        for (const OptionalContentMark& mark : plan.marks) {
            QPDFObjectHandle page = pages.at(static_cast<size_t>(mark.pageIndex));
            const std::vector<QPDFObjectHandle> contents = pageContentStreams(page);
            if (contents.empty()) {
                // 空白页没有内容流：标记无处可放，必须报错而不是悄悄跳过。
                setError(error, QStringLiteral("第 %1 页没有内容流，无法标记图层")
                                   .arg(mark.pageIndex + 1));
                return {};
            }
            const QString propertyKey = propertyKeyByName.value(mark.layerName);

            // 属性登记：页面 /Resources /Properties 里放「键名 → OCG 字典」。
            registerProperty(page, propertyKey, ocgByName.value(mark.layerName));

            std::vector<int> targets;
            if (mark.contentStreamIndex < 0) {
                for (size_t s = 0; s < contents.size(); ++s) {
                    targets.push_back(static_cast<int>(s));
                }
            } else if (mark.contentStreamIndex < static_cast<int>(contents.size())) {
                targets.push_back(mark.contentStreamIndex);
            } else {
                setError(error, QStringLiteral("第 %1 页没有第 %2 条内容流")
                                   .arg(mark.pageIndex + 1)
                                   .arg(mark.contentStreamIndex + 1));
                return {};
            }

            for (int target : targets) {
                QPDFObjectHandle stream = contents.at(static_cast<size_t>(target));
                if (!stream.isStream()) continue;
                const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
                if (!data) {
                    setError(error, QStringLiteral("第 %1 页的第 %2 条内容流读不出来")
                                       .arg(mark.pageIndex + 1)
                                       .arg(target + 1));
                    return {};
                }
                const QByteArray raw(reinterpret_cast<const char*>(data->getBuffer()),
                                     static_cast<qsizetype>(data->getSize()));
                // 标记内容：/OC /<属性键名> BDC … EMC
                const QByteArray wrapped = QByteArray("/OC ") + propertyKey.toLatin1() +
                                          QByteArray(" BDC ") + raw + QByteArray(" EMC");
                if (stream.isIndirect()) {
                    stream.replaceStreamData(wrapped.toStdString(), QPDFObjectHandle::newNull(),
                                             QPDFObjectHandle::newNull());
                } else {
                    // 直接对象的内容流不能就地改：换成一条新的间接流，再把整页的内容流重写一遍。
                    QPDFObjectHandle replacement =
                        QPDFObjectHandle::newStream(&pdf, wrapped.toStdString());
                    std::vector<QPDFObjectHandle> rebuilt;
                    for (size_t s = 0; s < contents.size(); ++s) {
                        if (static_cast<int>(s) == target) {
                            rebuilt.push_back(replacement);
                        } else {
                            rebuilt.push_back(contents.at(s));
                        }
                    }
                    page.replaceKey("/Contents", QPDFObjectHandle::newArray(rebuilt));
                }
            }
        }

        // ---- 页面级覆盖（/OC 数组里的 OCMD）----
        for (const OptionalContentPageCommand& command : plan.pageCommands) {
            if (command.pageIndex < 0 || command.pageIndex >= static_cast<int>(pages.size())) {
                setError(error, QStringLiteral("页面覆盖的页码超出范围（文档共 %1 页）")
                                   .arg(pages.size()));
                return {};
            }
            const QString policy = command.policy.trimmed().isEmpty() ? QStringLiteral("AnyOn")
                                                                      : command.policy.trimmed();
            if (policy != QLatin1String("AnyOn") && policy != QLatin1String("AllOn") &&
                policy != QLatin1String("AnyOff") && policy != QLatin1String("AllOff")) {
                setError(error, QStringLiteral("可选内容策略「%1」不认识").arg(policy));
                return {};
            }
            std::vector<QPDFObjectHandle> groups;
            for (const QString& name : command.groupNames) {
                if (!ocgByName.contains(name)) {
                    setError(error, QStringLiteral("页面覆盖引用了没有定义的图层「%1」").arg(name));
                    return {};
                }
                groups.push_back(ocgByName.value(name));
            }
            QPDFObjectHandle membership = QPDFObjectHandle::newDictionary();
            membership.replaceKey("/Type", QPDFObjectHandle::newName("/OCMD"));
            membership.replaceKey("/OCGs", QPDFObjectHandle::newArray(groups));
            membership.replaceKey("/P", QPDFObjectHandle::newName(("/" + policy).toUtf8().constData()));

            QPDFObjectHandle page = pages.at(static_cast<size_t>(command.pageIndex));
            std::vector<QPDFObjectHandle> array;
            const QPDFObjectHandle existing = page.getKey("/OC");
            if (existing.isArray()) array = existing.getArrayAsVector();
            array.push_back(pdf.makeIndirectObject(membership));
            page.replaceKey("/OC", QPDFObjectHandle::newArray(array));
        }

        QPDFWriter writer(pdf);
        // 关掉对象流：交叉引用表保持传统形态，出问题时人眼能直接看。
        writer.setObjectStreamMode(qpdf_o_disable);
        // 可选内容是 PDF 1.5 才有的特性：版本不够高的话阅读器根本不认图层。
        writer.setMinimumPDFVersion("1.5");
        writer.setOutputMemory();
        writer.write();
        const std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            setError(error, QStringLiteral("qpdf 没有写出可选内容图层"));
            return {};
        }
        const QByteArray written(reinterpret_cast<const char*>(buffer->getBuffer()),
                                  static_cast<qsizetype>(buffer->getSize()));

        // 自检：写完必须真的读得回来，而且图层名与可见性都要对得上。
        // 这一步挡住的是「写出了字节、但图层其实没进去」的静默失败。
        const OptionalContentSummary check = readOptionalContent(written, nullptr);
        if (!check.hasOptionalContent || check.groups.size() != plan.layers.size()) {
            setError(error, QStringLiteral("写出的图层读不回来"));
            return {};
        }
        for (const OptionalContentLayer& layer : plan.layers) {
            bool found = false;
            for (const OptionalContentGroup& group : check.groups) {
                if (group.name != layer.name) continue;
                found = true;
                if (group.visible != layer.defaultVisible) {
                    setError(error, QStringLiteral("写出的图层「%1」可见性与设定不一致").arg(layer.name));
                    return {};
                }
            }
            if (!found) {
                setError(error, QStringLiteral("写出的图层「%1」读不回来").arg(layer.name));
                return {};
            }
        }
        return written;
    } catch (const std::exception& exception) {
        setError(error, QStringLiteral("写入可选内容图层失败：%1")
                       .arg(QString::fromUtf8(exception.what())));
        return {};
    }
#endif
}

}  // namespace aipdf::pdf
