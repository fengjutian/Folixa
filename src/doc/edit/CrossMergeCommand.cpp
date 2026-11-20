#include "doc/edit/CrossMergeCommand.h"

#include "doc/edit/EditContext.h"
#include "i18n/Translate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace aipdf::doc {

namespace {

constexpr auto kType = "page.crossMerge";

constexpr auto kKeyType = "type";
constexpr auto kKeyRounds = "rounds";
constexpr auto kKeySlots = "slots";
constexpr auto kKeySlotSource = "source";
constexpr auto kKeySlotPage = "page";
constexpr auto kKeyTaken = "taken";

}  // namespace

CrossMergeCommand::CrossMergeCommand(pdf::CrossMergePlan plan,
                                     std::shared_ptr<CrossMergePageSource> source)
    : plan_(std::move(plan)), source_(std::move(source)) {}

QString CrossMergeCommand::typeName() const {
    return QString::fromLatin1(kType);
}

QString CrossMergeCommand::displayName() const {
    return i18n::tr("aipdf::doc::CrossMergeCommand", "交叉合并");
}

QByteArray CrossMergeCommand::serialize() const {
    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyType), typeName());
    object.insert(QString::fromLatin1(kKeyRounds), plan_.rounds);
    QJsonArray slotArray;
    for (const pdf::CrossMergeSlot& slot : plan_.pageSlots) {
        QJsonArray entry;
        entry.append(slot.sourceId);
        entry.append(slot.sourcePage);
        slotArray.append(entry);
    }
    object.insert(QString::fromLatin1(kKeySlots), slotArray);
    QJsonArray taken;
    for (const int count : plan_.takenPerSource) {
        taken.append(count);
    }
    object.insert(QString::fromLatin1(kKeyTaken), taken);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool CrossMergeCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::CrossMergeCommand", "交叉合并命令不是合法的 JSON：%1")
                         .arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QString::fromLatin1(kKeyType)).toString() != typeName()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::CrossMergeCommand", "交叉合并命令类型不匹配");
        }
        return false;
    }
    const QJsonArray slotArray = object.value(QString::fromLatin1(kKeySlots)).toArray();
    pdf::CrossMergePlan plan;
    plan.rounds = object.value(QString::fromLatin1(kKeyRounds)).toInt();
    for (const QJsonValue& value : slotArray) {
        const QJsonArray entry = value.toArray();
        if (entry.size() != 2) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::doc::CrossMergeCommand", "交叉合并命令的页序项不合法");
            }
            return false;
        }
        pdf::CrossMergeSlot slot;
        slot.sourceId = entry.at(0).toInt();
        slot.sourcePage = entry.at(1).toInt();
        plan.pageSlots.append(slot);
    }
    const QJsonArray taken = object.value(QString::fromLatin1(kKeyTaken)).toArray();
    for (const QJsonValue& value : taken) {
        plan.takenPerSource.append(value.toInt());
    }
    plan_ = plan;
    // 外部来源的取页入口与页面备份都不在序列化里（它们是运行时数据）：
    // 从恢复日志重放出来的命令没有来源可取页，执行不了——这一点与
    // ReplacePageCommand 的替换页备份是同一个处境。
    return true;
}

bool CrossMergeCommand::apply(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr) {
        return false;
    }
    if (plan_.pageSlots.isEmpty()) {
        // 没有要装的页：没什么可交叉的。
        return false;
    }
    // 1. 拍下交叉前的页面（撤销要靠它）。已经拍过就不重拍：
    //    重做时再拍一次，拍到的会是合并后的页面。
    if (!captured_ && !captureOriginalPages(pages)) {
        return false;
    }
    // 2. 清空当前文档的页面：交叉后的页序从头排起。
    if (!clearPages(pages)) {
        return false;
    }
    // 3. 按计划装页。
    if (!installPlannedPages(pages)) {
        // 装到一半失败：把交叉前的页面放回去。
        // 不能停在「只交叉了一半」的状态——那比不做还糟。
        clearPages(pages);
        restoreOriginalPages(pages);
        return false;
    }
    // 4. 页码重映射：交叉前的第 i 页搬到了新页序里的第几页。
    //    批注的页码由应用层按这张表搬（命令不重复搬一次）。
    context->pageStructureChanged(mappingFromOriginalToPlan());
    return true;
}

bool CrossMergeCommand::revert(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr) {
        return false;
    }
    if (!captured_) {
        // 没有备份就恢复不了（例如从恢复日志重放出来的命令）。
        return false;
    }
    // 映射要在清页之前算：它按「合并后的页号」索引。
    const QVector<int> mapping = mappingFromPlanToOriginal(pages->pageCount());
    if (!clearPages(pages)) {
        return false;
    }
    if (!restoreOriginalPages(pages)) {
        return false;
    }
    context->pageStructureChanged(mapping);
    return true;
}

const pdf::CrossMergePlan& CrossMergeCommand::plan() const {
    return plan_;
}

int CrossMergeCommand::originalPageCount() const {
    return originalPages_.size();
}

bool CrossMergeCommand::captureOriginalPages(PageOperations* pages) const {
    const int count = pages->pageCount();
    originalPages_.clear();
    originalPages_.reserve(count);
    for (int index = 0; index < count; ++index) {
        std::shared_ptr<PageBackup> backup;
        if (!pages->backupPage(index, &backup) || backup == nullptr) {
            originalPages_.clear();
            captured_ = false;
            return false;
        }
        originalPages_.append(backup);
    }
    captured_ = true;
    return true;
}

bool CrossMergeCommand::clearPages(PageOperations* pages) const {
    // 从后往前删：前面的页号不会因为后面被删而变。
    for (int index = pages->pageCount() - 1; index >= 0; --index) {
        QVector<int> mapping;
        if (!pages->deletePage(index, &mapping)) {
            return false;
        }
    }
    return true;
}

bool CrossMergeCommand::installPlannedPages(PageOperations* pages) const {
    for (const pdf::CrossMergeSlot& slot : plan_.pageSlots) {
        std::shared_ptr<PageBackup> backup;
        if (slot.sourceId == pdf::kCrossMergeCurrentSource) {
            // 当前文档的页：用的是交叉前拍下的那份备份。
            if (slot.sourcePage < 0 || slot.sourcePage >= originalPages_.size()) {
                return false;
            }
            backup = originalPages_.at(slot.sourcePage);
        } else {
            // 外部来源的页：让应用层去取。
            if (source_ == nullptr) {
                return false;
            }
            if (!source_->backupPage(slot.sourceId, slot.sourcePage, &backup) || backup == nullptr) {
                return false;
            }
        }
        QVector<int> mapping;
        // 追加到末尾：页序由计划的顺序决定。
        if (!pages->restorePage(pages->pageCount(), *backup)) {
            return false;
        }
    }
    return true;
}

bool CrossMergeCommand::restoreOriginalPages(PageOperations* pages) const {
    for (int index = 0; index < originalPages_.size(); ++index) {
        QVector<int> mapping;
        if (!pages->restorePage(index, *originalPages_.at(index))) {
            return false;
        }
    }
    return true;
}

QVector<int> CrossMergeCommand::mappingFromOriginalToPlan() const {
    QVector<int> mapping;
    mapping.resize(originalPages_.size());
    for (int& value : mapping) {
        value = -1;
    }
    for (int position = 0; position < plan_.pageSlots.size(); ++position) {
        const pdf::CrossMergeSlot& slot = plan_.pageSlots.at(position);
        if (slot.sourceId != pdf::kCrossMergeCurrentSource) {
            continue;
        }
        if (slot.sourcePage >= 0 && slot.sourcePage < mapping.size()) {
            mapping[slot.sourcePage] = position;
        }
    }
    return mapping;
}

QVector<int> CrossMergeCommand::mappingFromPlanToOriginal(int mergedCount) const {
    QVector<int> mapping;
    mapping.resize(std::max(0, mergedCount));
    for (int& value : mapping) {
        value = -1;
    }
    const int limit = std::min(mapping.size(), plan_.pageSlots.size());
    for (int position = 0; position < limit; ++position) {
        const pdf::CrossMergeSlot& slot = plan_.pageSlots.at(position);
        if (slot.sourceId == pdf::kCrossMergeCurrentSource && slot.sourcePage >= 0) {
            mapping[position] = slot.sourcePage;
        }
    }
    return mapping;
}

}  // namespace aipdf::doc
