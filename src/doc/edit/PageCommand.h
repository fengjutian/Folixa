#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "doc/edit/EditCommand.h"
#include "pdf/annot/Annotation.h"

#include <memory>

namespace aipdf::doc {

// 页面结构的编辑命令。
//
// 与批注命令一样，页面命令也进统一撤销栈：
// 撤销栈只认「命令」，不认命令是哪种编辑。
//
// 页面命令通过上下文拿页面操作入口（doc::PageOperations），
// 改完结构后把「旧页序号 → 新页序号」的映射交回上下文，
// 让上下文把批注的页码一起搬过去。
//
// 每条命令都要能序列化：恢复日志靠它重放。
// 但**撤销**需要的数据不一定能序列化（例如删除页时备份的那一页），
// 这种数据只在运行时保留——恢复日志只重放「向前」的过程，不需要撤销。

// 插入一张空白页。
class InsertBlankPageCommand final : public EditCommand {
public:
    InsertBlankPageCommand() = default;
    InsertBlankPageCommand(int index, double width, double height);

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(EditContext* context) const override;
    bool revert(EditContext* context) const override;

    int index() const;
    double width() const;
    double height() const;

private:
    int index_ = 0;
    double width_ = 0.0;
    double height_ = 0.0;
};

// 删除一页。
//
// 撤销要靠备份：删掉之前先把这一页复制到一份临时文档里，
// 撤销时再复制回来。备份是运行时数据，不进序列化。
class DeletePageCommand final : public EditCommand {
public:
    DeletePageCommand() = default;
    explicit DeletePageCommand(int index);

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(EditContext* context) const override;
    bool revert(EditContext* context) const override;

    int index() const;

private:
    int index_ = 0;
    // 被删页的备份。mutable：apply 是 const，但备份要就地生成。
    mutable std::shared_ptr<PageBackup> backup_;
    // 被删页上的批注：删页时模型会丢掉它们，撤销时要放回去，
    // 否则页面回来了、批注没了。
    mutable QVector<pdf::Annotation> removedAnnotations_;
};

// 旋转一页（写进文档的 /Rotate，与界面上「旋转视图」不是一回事）。
//
// 命令里带着旋转前后的角度：撤销要把角度改回去。
class RotatePageCommand final : public EditCommand {
public:
    RotatePageCommand() = default;
    RotatePageCommand(int index, int beforeTurns, int afterTurns);

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(EditContext* context) const override;
    bool revert(EditContext* context) const override;

    int index() const;
    int beforeTurns() const;
    int afterTurns() const;

private:
    int index_ = 0;
    int beforeTurns_ = 0;
    int afterTurns_ = 0;
};

// 把一页移动到另一个位置。
//
// 撤销就是把这一页移回原处（再搬一次）。
class MovePageCommand final : public EditCommand {
public:
    MovePageCommand() = default;
    MovePageCommand(int from, int to);

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(EditContext* context) const override;
    bool revert(EditContext* context) const override;

    int from() const;
    int to() const;

private:
    int from_ = 0;
    int to_ = 0;
};

// 交换任意两页。
//
// 为什么单独一条命令，而不是界面连着调两次「移动页」：
// 1. **一步撤销**。连着搬两次会留下两条撤销记录，用户按一次 Ctrl+Z 只回到中间态
//    （两页换了一半），得按两次才真的换回来——这在用户看来就是「撤销坏了」。
// 2. **失败不留半截**。交换是两次搬移：第二步失败时必须把第一步退回去，
//    否则文档停在「只搬了一半」的状态。做成一条命令，这个回滚写在命令里，
//    界面层不用知道。
//
// 交换是自身的逆：把第 i 页和第 j 页换一次，再换一次就回去了，
// 所以撤销就是再做一次同样的交换（见 PageCommand.cpp 里的推导）。
class SwapPagesCommand final : public EditCommand {
public:
    SwapPagesCommand() = default;
    SwapPagesCommand(int first, int second);

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(EditContext* context) const override;
    bool revert(EditContext* context) const override;

    int first() const;
    int second() const;

private:
    int first_ = 0;
    int second_ = 0;
};

// 用另一页替换当前页。
//
// 替换 = 备份原页 + 删除 + 恢复新页；撤销做反向三步。
// 新页来自另一份文档，备份是运行时数据，不参与序列化。
class ReplacePageCommand final : public EditCommand {
public:
    ReplacePageCommand() = default;
    ReplacePageCommand(int index, std::shared_ptr<PageBackup> replacement);

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(EditContext* context) const override;
    bool revert(EditContext* context) const override;

private:
    int index_ = 0;
    std::shared_ptr<PageBackup> replacement_;
    mutable std::shared_ptr<PageBackup> removed_;
};

}  // namespace aipdf::doc
