#include "doc/UndoStack.h"

#include "doc/EditCommand.h"
#include "doc/EditContext.h"

namespace aipdf::doc {

bool UndoStack::push(std::unique_ptr<EditCommand> command, EditContext* context) {
    if (command == nullptr || context == nullptr) {
        return false;
    }
    // 先执行再入栈：执行不成功的命令不该出现在撤销历史里。
    if (!command->apply(context)) {
        return false;
    }
    undoStack_.append(std::shared_ptr<EditCommand>(std::move(command)));
    // 超过上限就丢最老的一条。
    while (undoStack_.size() > kMaxDepth) {
        undoStack_.removeFirst();
    }
    // 新的编辑动作让重做栈失效：历史分叉之后不能重做到另一条分支。
    redoStack_.clear();
    return true;
}

bool UndoStack::undo(EditContext* context) {
    if (context == nullptr || undoStack_.isEmpty()) {
        return false;
    }
    auto command = undoStack_.last();
    if (!command->revert(context)) {
        return false;
    }
    undoStack_.removeLast();
    redoStack_.append(command);
    return true;
}

bool UndoStack::redo(EditContext* context) {
    if (context == nullptr || redoStack_.isEmpty()) {
        return false;
    }
    auto command = redoStack_.last();
    if (!command->apply(context)) {
        return false;
    }
    redoStack_.removeLast();
    undoStack_.append(command);
    return true;
}

bool UndoStack::canUndo() const {
    return !undoStack_.isEmpty();
}

bool UndoStack::canRedo() const {
    return !redoStack_.isEmpty();
}

QString UndoStack::undoText() const {
    if (undoStack_.isEmpty()) {
        return {};
    }
    return QStringLiteral("撤销 %1").arg(undoStack_.last()->displayName());
}

QString UndoStack::redoText() const {
    if (redoStack_.isEmpty()) {
        return {};
    }
    return QStringLiteral("重做 %1").arg(redoStack_.last()->displayName());
}

int UndoStack::undoCount() const {
    return static_cast<int>(undoStack_.size());
}

int UndoStack::redoCount() const {
    return static_cast<int>(redoStack_.size());
}

void UndoStack::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

QVector<QByteArray> UndoStack::serializedHistory() const {
    QVector<QByteArray> result;
    result.reserve(undoStack_.size());
    for (const auto& command : undoStack_) {
        if (command == nullptr) {
            continue;
        }
        const QByteArray data = command->serialize();
        if (!data.isEmpty()) {
            result.append(data);
        }
    }
    return result;
}

}  // namespace aipdf::doc
