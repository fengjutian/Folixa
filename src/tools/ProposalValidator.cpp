#include "tools/ProposalValidator.h"

#include "tools/ToolRegistry.h"

#include <QJsonValue>
#include <QStringList>

#include <cmath>

namespace aipdf::tools {

namespace {

constexpr int kMaxOperations = 20;

bool typeMatches(const QJsonValue& value, const QString& type) {
    if (type == QLatin1String("integer")) {
        if (!value.isDouble()) {
            return false;
        }
        const double number = value.toDouble();
        return std::floor(number) == number;
    }
    if (type == QLatin1String("number")) {
        return value.isDouble();
    }
    if (type == QLatin1String("string")) {
        return value.isString();
    }
    if (type == QLatin1String("boolean")) {
        return value.isBool();
    }
    if (type == QLatin1String("array")) {
        return value.isArray();
    }
    return false;
}

// 校验一个参数值是否符合字段 schema。返回空串表示通过。
QString checkField(const QString& name, const QJsonValue& value, const QJsonObject& fieldSchema) {
    const QString type = fieldSchema.value(QStringLiteral("type")).toString();
    if (!type.isEmpty() && !typeMatches(value, type)) {
        return QStringLiteral("参数 %1 类型应为 %2").arg(name, type);
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (fieldSchema.contains(QStringLiteral("minimum")) &&
            number < fieldSchema.value(QStringLiteral("minimum")).toDouble()) {
            return QStringLiteral("参数 %1 小于下限 %2")
                .arg(name)
                .arg(fieldSchema.value(QStringLiteral("minimum")).toDouble());
        }
        if (fieldSchema.contains(QStringLiteral("maximum")) &&
            number > fieldSchema.value(QStringLiteral("maximum")).toDouble()) {
            return QStringLiteral("参数 %1 超过上限 %2")
                .arg(name)
                .arg(fieldSchema.value(QStringLiteral("maximum")).toDouble());
        }
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (fieldSchema.contains(QStringLiteral("minItems")) &&
            array.size() < fieldSchema.value(QStringLiteral("minItems")).toInt()) {
            return QStringLiteral("参数 %1 元素太少").arg(name);
        }
        if (fieldSchema.contains(QStringLiteral("maxItems")) &&
            array.size() > fieldSchema.value(QStringLiteral("maxItems")).toInt()) {
            return QStringLiteral("参数 %1 元素太多").arg(name);
        }
        const QJsonObject items = fieldSchema.value(QStringLiteral("items")).toObject();
        if (!items.isEmpty()) {
            for (const QJsonValue& item : array) {
                const QString issue = checkField(name, item, items);
                if (!issue.isEmpty()) {
                    return issue;
                }
            }
        }
    }
    return {};
}

QString summaryFor(const QString& toolId, const QJsonObject& args) {
    if (toolId == QLatin1String("doc.delete_pages")) {
        QStringList pages;
        for (const QJsonValue& value : args.value(QStringLiteral("pages")).toArray()) {
            pages.append(QString::number(value.toInt()));
        }
        return QStringLiteral("删除第 %1 页").arg(pages.join(QStringLiteral("、")));
    }
    if (toolId == QLatin1String("doc.insert_blank_page")) {
        return QStringLiteral("在第 %1 页前插入空白页")
            .arg(args.value(QStringLiteral("index")).toInt());
    }
    if (toolId == QLatin1String("doc.move_page")) {
        return QStringLiteral("把第 %1 页移动到第 %2 位")
            .arg(args.value(QStringLiteral("from")).toInt())
            .arg(args.value(QStringLiteral("to")).toInt());
    }
    if (toolId == QLatin1String("annotation.add_note")) {
        return QStringLiteral("在第 %1 页添加批注：%2")
            .arg(args.value(QStringLiteral("page")).toInt())
            .arg(args.value(QStringLiteral("text")).toString());
    }
    return toolId;
}

// 页码必须落在文档范围内：模型很可能按「看起来的页码」给数值。
QString checkPageBounds(const QString& toolId, const QJsonObject& args, int pageCount) {
    const auto inRange = [pageCount](int page) { return page >= 1 && page <= pageCount; };

    if (toolId == QLatin1String("doc.delete_pages")) {
        for (const QJsonValue& value : args.value(QStringLiteral("pages")).toArray()) {
            if (!inRange(value.toInt())) {
                return QStringLiteral("页码 %1 超出范围（1–%2）").arg(value.toInt()).arg(pageCount);
            }
        }
    }
    if (toolId == QLatin1String("doc.insert_blank_page")) {
        const int index = args.value(QStringLiteral("index")).toInt();
        if (index < 1 || index > pageCount + 1) {
            return QStringLiteral("插入位置 %1 超出范围（1–%2）").arg(index).arg(pageCount + 1);
        }
    }
    if (toolId == QLatin1String("doc.move_page")) {
        const int from = args.value(QStringLiteral("from")).toInt();
        const int to = args.value(QStringLiteral("to")).toInt();
        if (!inRange(from) || !inRange(to)) {
            return QStringLiteral("页码超出范围（1–%1）").arg(pageCount);
        }
    }
    if (toolId == QLatin1String("annotation.add_note")) {
        const int page = args.value(QStringLiteral("page")).toInt();
        if (!inRange(page)) {
            return QStringLiteral("页码 %1 超出范围（1–%2）").arg(page).arg(pageCount);
        }
    }
    return {};
}

}  // namespace

Proposal validateProposal(const QJsonArray& rawOperations, const DocumentSnapshot& document) {
    Proposal proposal;
    if (rawOperations.isEmpty()) {
        proposal.issues.append({-1, QStringLiteral("提案里没有任何操作")});
        return proposal;
    }
    if (rawOperations.size() > kMaxOperations) {
        proposal.issues.append(
            {-1, QStringLiteral("一次最多 %1 条操作").arg(kMaxOperations)});
        return proposal;
    }

    const ToolRegistry& registry = ToolRegistry::instance();
    for (int index = 0; index < rawOperations.size(); ++index) {
        const QJsonValue raw = rawOperations.at(index);
        if (!raw.isObject()) {
            proposal.issues.append({index, QStringLiteral("操作必须是对象")});
            continue;
        }
        const QJsonObject object = raw.toObject();

        if (object.contains(QStringLiteral("docRevision"))) {
            const int revision = object.value(QStringLiteral("docRevision")).toInt();
            if (revision != document.revision) {
                proposal.issues.append(
                    {index, QStringLiteral("文档已被修改，请重新生成提案后再确认")});
                continue;
            }
        }

        const QString toolId = object.value(QStringLiteral("tool")).toString();
        if (toolId.isEmpty()) {
            proposal.issues.append({index, QStringLiteral("缺少 tool 字段")});
            continue;
        }
        if (ToolRegistry::isForbidden(toolId)) {
            proposal.issues.append(
                {index, QStringLiteral("工具 %1 被禁止：AI 不能执行文件、Shell 或网络操作").arg(toolId)});
            continue;
        }
        const ToolSpec* spec = registry.find(toolId);
        if (spec == nullptr) {
            proposal.issues.append({index, QStringLiteral("未知工具：%1").arg(toolId)});
            continue;
        }
        if (spec->permission == ToolPermission::ReadOnly) {
            proposal.issues.append(
                {index, QStringLiteral("%1 是只读工具，不能出现在变更提案里").arg(toolId)});
            continue;
        }

        const QJsonValue argsValue = object.value(QStringLiteral("args"));
        if (!argsValue.isUndefined() && !argsValue.isObject()) {
            proposal.issues.append({index, QStringLiteral("args 必须是对象")});
            continue;
        }
        const QJsonObject args = argsValue.toObject();

        const QJsonObject properties = spec->schema.value(QStringLiteral("properties")).toObject();
        bool fieldIssue = false;
        for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
            if (!properties.contains(it.key())) {
                proposal.issues.append(
                    {index, QStringLiteral("%1 不接受参数 %2").arg(toolId, it.key())});
                fieldIssue = true;
                break;
            }
        }
        if (fieldIssue) {
            continue;
        }

        for (const QJsonValue& required :
             spec->schema.value(QStringLiteral("required")).toArray()) {
            const QString name = required.toString();
            if (!args.contains(name)) {
                proposal.issues.append(
                    {index, QStringLiteral("%1 缺少必填参数 %2").arg(toolId, name)});
                fieldIssue = true;
                break;
            }
        }
        if (fieldIssue) {
            continue;
        }

        for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
            const QString issue =
                checkField(it.key(), it.value(), properties.value(it.key()).toObject());
            if (!issue.isEmpty()) {
                proposal.issues.append({index, issue});
                fieldIssue = true;
                break;
            }
        }
        if (fieldIssue) {
            continue;
        }

        const QString boundsIssue = checkPageBounds(toolId, args, document.pageCount);
        if (!boundsIssue.isEmpty()) {
            proposal.issues.append({index, boundsIssue});
            continue;
        }

        ProposedOperation operation;
        operation.toolId = toolId;
        operation.displayName = spec->displayName;
        operation.args = args;
        operation.summary = summaryFor(toolId, args);
        proposal.operations.append(operation);
    }

    return proposal;
}

}  // namespace aipdf::tools
