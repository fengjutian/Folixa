#pragma once

#include <QString>
#include <QVector>

#include <memory>

namespace aipdf::doc {

class AnnotationModel;
class EditCommand;

// 统一撤销栈：所有编辑命令都进这里。
//
// 一个栈管所有类型的编辑，撤销菜单与快捷键才有一个统一入口，
// 不用给每种编辑各做一套撤销逻辑。
//
// 语义与常见的编辑器一致：
// - 新命令进来时，重做栈清空（在旧状态上分叉之后不能重做到另一条分支）；
// - 撤销栈有深度上限，太深的历史没有实际意义，还会一直占着内存。
class UndoStack {
public:
    // 栈深上限。
    static constexpr int kMaxDepth = 200;

    // 执行命令并入栈。执行失败不入栈，返回 false。
    bool push(std::unique_ptr<EditCommand> command, AnnotationModel* model);
    // 撤销一步。栈空时返回 false。
    bool undo(AnnotationModel* model);
    // 重做一步。栈空时返回 false。
    bool redo(AnnotationModel* model);

    bool canUndo() const;
    bool canRedo() const;
    // 撤销菜单的文案：「撤销 添加高亮」。没有可撤销时返回空串。
    QString undoText() const;
    // 重做菜单的文案：「重做 添加高亮」。
    QString redoText() const;

    int undoCount() const;
    int redoCount() const;
    void clear();

private:
    QVector<std::shared_ptr<EditCommand>> undoStack_;
    QVector<std::shared_ptr<EditCommand>> redoStack_;
};

}  // namespace aipdf::doc
