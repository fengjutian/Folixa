#pragma once

#include <QByteArray>
#include <QString>

#include "pdf/Annotation.h"

#include <memory>

namespace aipdf::doc {

class AnnotationModel;

// 可序列化的编辑命令。
//
// 技术设计要求编辑走命令、并进统一撤销栈：命令要能序列化，
// 才能落审计记录、才能在别处重放。
// 撤销与重做都只是「反向执行命令」，所以每条命令要能描述两种方向的差异。
class EditCommand {
public:
    virtual ~EditCommand() = default;

    // 类型标识（序列化用）："annotation.add" 之类。
    virtual QString typeName() const = 0;
    // 界面显示名：「添加高亮」。撤销菜单用它说明将要撤销什么。
    virtual QString displayName() const = 0;
    // 序列化。失败返回空字节数组。
    virtual QByteArray serialize() const = 0;
    // 反序列化：就地填充自己。失败返回 false，并通过 error 说明原因。
    virtual bool deserialize(const QByteArray& data, QString* error) = 0;

    // 执行（也是重做）。
    virtual bool apply(AnnotationModel* model) const = 0;
    // 撤销。
    virtual bool revert(AnnotationModel* model) const = 0;
};

// 添加一条批注。
class AddAnnotationCommand final : public EditCommand {
public:
    explicit AddAnnotationCommand(pdf::Annotation annotation = {});

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(AnnotationModel* model) const override;
    bool revert(AnnotationModel* model) const override;

    const pdf::Annotation& annotation() const;

private:
    pdf::Annotation annotation_;
};

// 删除一条批注。
//
// 命令里带着被删批注的完整快照：撤销时原样放回去，
// 不需要模型另外记一份「被删之前长什么样」。
class RemoveAnnotationCommand final : public EditCommand {
public:
    explicit RemoveAnnotationCommand(pdf::Annotation annotation = {});

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(AnnotationModel* model) const override;
    bool revert(AnnotationModel* model) const override;

    const pdf::Annotation& annotation() const;

private:
    pdf::Annotation annotation_;
};

// 修改一条批注（颜色、正文）。
//
// 带着修改前与修改后两份快照：撤销放回旧的，重做放回新的。
class UpdateAnnotationCommand final : public EditCommand {
public:
    UpdateAnnotationCommand(pdf::Annotation before = {}, pdf::Annotation after = {});

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(AnnotationModel* model) const override;
    bool revert(AnnotationModel* model) const override;

private:
    pdf::Annotation before_;
    pdf::Annotation after_;
};

// 按类型名新建一个空命令（反序列化工厂）。
std::unique_ptr<EditCommand> createEditCommand(const QString& typeName);

// 直接反序列化一条命令。
std::unique_ptr<EditCommand> deserializeEditCommand(const QByteArray& data, QString* error);

}  // namespace aipdf::doc
