#include "app/ProposalRunner.h"

#include "doc/PageCommand.h"
#include "doc/UndoStack.h"
#include "pdf/Annotation.h"

#include <QJsonArray>

#include <algorithm>

namespace aipdf::app {

namespace {

// 页码：提案里从 1 开始（给人看），命令里从 0 开始。
int toIndex(int pageNumber) {
    return pageNumber - 1;
}

QString describeFailure(const tools::ProposedOperation& operation, const QString& reason) {
    return QStringLiteral("第 %1 条操作（%2）无法执行：%3")
        .arg(operation.displayName, operation.toolId, reason);
}

}  // namespace

bool ProposalRunner::buildCommands(const tools::Proposal& proposal, CommandList* commands,
                                   QString* error) {
    if (commands == nullptr) {
        return false;
    }
    commands->clear();

    for (const tools::ProposedOperation& operation : proposal.operations) {
        if (operation.toolId == QLatin1String("doc.delete_pages")) {
            // 删多页必须从后往前：先删第 2 页，原来的第 5 页会变成第 4 页。
            QVector<int> pages;
            for (const QJsonValue& value : operation.args.value(QStringLiteral("pages")).toArray()) {
                pages.append(toIndex(value.toInt()));
            }
            std::sort(pages.begin(), pages.end(), std::greater<int>());
            for (const int page : pages) {
                commands->push_back(std::make_unique<doc::DeletePageCommand>(page));
            }
            continue;
        }
        if (operation.toolId == QLatin1String("doc.insert_blank_page")) {
            const int index = toIndex(operation.args.value(QStringLiteral("index")).toInt());
            // 没给尺寸就按 A4：插入空白页总得有个大小。
            const double width = operation.args.contains(QStringLiteral("width"))
                                     ? operation.args.value(QStringLiteral("width")).toDouble()
                                     : 595.0;
            const double height = operation.args.contains(QStringLiteral("height"))
                                      ? operation.args.value(QStringLiteral("height")).toDouble()
                                      : 842.0;
            commands->push_back(std::make_unique<doc::InsertBlankPageCommand>(index, width, height));
            continue;
        }
        if (operation.toolId == QLatin1String("doc.move_page")) {
            const int from = toIndex(operation.args.value(QStringLiteral("from")).toInt());
            const int to = toIndex(operation.args.value(QStringLiteral("to")).toInt());
            commands->push_back(std::make_unique<doc::MovePageCommand>(from, to));
            continue;
        }
        if (operation.toolId == QLatin1String("annotation.add_note")) {
            pdf::Annotation annotation;
            annotation.pageIndex = toIndex(operation.args.value(QStringLiteral("page")).toInt());
            annotation.type = pdf::AnnotationType::Note;
            const QJsonArray rect = operation.args.value(QStringLiteral("rect")).toArray();
            annotation.rect = QRectF(rect.at(0).toDouble(), rect.at(1).toDouble(),
                                     rect.at(2).toDouble(), rect.at(3).toDouble());
            annotation.contents = operation.args.value(QStringLiteral("text")).toString();
            annotation.author = QStringLiteral("AI 草稿");
            annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Note);
            commands->push_back(std::make_unique<doc::AddAnnotationCommand>(annotation));
            continue;
        }

        if (error != nullptr) {
            *error = describeFailure(operation, QStringLiteral("没有对应的编辑命令"));
        }
        commands->clear();
        return false;
    }

    if (commands->empty()) {
        if (error != nullptr) {
            *error = QStringLiteral("提案里没有可执行的操作");
        }
        return false;
    }
    return true;
}

int ProposalRunner::execute(CommandList* commands, doc::EditContext* context,
                            doc::UndoStack* stack, QString* error) {
    if (commands == nullptr) {
        return 0;
    }
    if (context == nullptr || stack == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("没有可用的编辑上下文");
        }
        return 0;
    }
    int applied = 0;
    for (std::unique_ptr<doc::EditCommand>& command : *commands) {
        if (command == nullptr) {
            continue;
        }
        // 名字要先取出来：push 之后 command 已经为空。
        const QString name = command->displayName();
        // push 会执行并入栈：失败时不会留下半条命令。
        if (!stack->push(std::move(command), context)) {
            if (error != nullptr) {
                *error = QStringLiteral("第 %1 条「%2」执行失败").arg(applied + 1).arg(name);
            }
            return applied;
        }
        ++applied;
    }
    return applied;
}

}  // namespace aipdf::app
