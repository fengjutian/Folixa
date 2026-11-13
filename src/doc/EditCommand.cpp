#include "doc/EditCommand.h"

#include "doc/AnnotationModel.h"
#include "doc/PageCommand.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace aipdf::doc {

namespace {

constexpr auto kTypeAdd = "annotation.add";
constexpr auto kTypeRemove = "annotation.remove";
constexpr auto kTypeUpdate = "annotation.update";

constexpr auto kKeyType = "type";
constexpr auto kKeyAnnotation = "annotation";
// 删除与修改用**各自的数据键**：否则把一条「添加」命令的 type 改成
// 「删除」也能反序列化成功，命令的语义就被悄悄换掉了。
constexpr auto kKeyRemoved = "removed";
constexpr auto kKeyBefore = "before";
constexpr auto kKeyAfter = "after";

}  // namespace

// ------------------------------------------------------------ AddAnnotationCommand ----

AddAnnotationCommand::AddAnnotationCommand(pdf::Annotation annotation)
    : annotation_(std::move(annotation)) {}

QString AddAnnotationCommand::typeName() const {
    return QString::fromLatin1(kTypeAdd);
}

QString AddAnnotationCommand::displayName() const {
    return QStringLiteral("添加%1").arg(pdf::annotationTypeName(annotation_.type));
}

QByteArray AddAnnotationCommand::serialize() const {
    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyType), typeName());
    object.insert(QString::fromLatin1(kKeyAnnotation), pdf::annotationToJson(annotation_));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool AddAnnotationCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令不是合法的 JSON：%1").arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QString::fromLatin1(kKeyType)).toString() != typeName()) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令类型不匹配");
        }
        return false;
    }
    pdf::Annotation annotation;
    if (!pdf::annotationFromJson(object.value(QString::fromLatin1(kKeyAnnotation)).toObject(),
                                 &annotation)) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令里的批注内容不完整");
        }
        return false;
    }
    annotation_ = annotation;
    return true;
}

bool AddAnnotationCommand::apply(EditContext* context) const {
    AnnotationModel* model = (context != nullptr) ? context->annotationModel() : nullptr;
    if (model == nullptr) {
        return false;
    }
    return model->add(annotation_);
}

bool AddAnnotationCommand::revert(EditContext* context) const {
    AnnotationModel* model = (context != nullptr) ? context->annotationModel() : nullptr;
    if (model == nullptr) {
        return false;
    }
    return model->remove(annotation_.id);
}

const pdf::Annotation& AddAnnotationCommand::annotation() const {
    return annotation_;
}

// ---------------------------------------------------------- RemoveAnnotationCommand ----

RemoveAnnotationCommand::RemoveAnnotationCommand(pdf::Annotation annotation)
    : annotation_(std::move(annotation)) {}

QString RemoveAnnotationCommand::typeName() const {
    return QString::fromLatin1(kTypeRemove);
}

QString RemoveAnnotationCommand::displayName() const {
    return QStringLiteral("删除%1").arg(pdf::annotationTypeName(annotation_.type));
}

QByteArray RemoveAnnotationCommand::serialize() const {
    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyType), typeName());
    object.insert(QString::fromLatin1(kKeyRemoved), pdf::annotationToJson(annotation_));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool RemoveAnnotationCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令不是合法的 JSON：%1").arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QString::fromLatin1(kKeyType)).toString() != typeName()) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令类型不匹配");
        }
        return false;
    }
    pdf::Annotation annotation;
    if (!pdf::annotationFromJson(object.value(QString::fromLatin1(kKeyRemoved)).toObject(),
                                 &annotation)) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令里的批注内容不完整");
        }
        return false;
    }
    annotation_ = annotation;
    return true;
}

bool RemoveAnnotationCommand::apply(EditContext* context) const {
    AnnotationModel* model = (context != nullptr) ? context->annotationModel() : nullptr;
    if (model == nullptr) {
        return false;
    }
    return model->remove(annotation_.id);
}

bool RemoveAnnotationCommand::revert(EditContext* context) const {
    AnnotationModel* model = (context != nullptr) ? context->annotationModel() : nullptr;
    if (model == nullptr) {
        return false;
    }
    // 撤销删除：把快照原样放回去。
    return model->add(annotation_);
}

const pdf::Annotation& RemoveAnnotationCommand::annotation() const {
    return annotation_;
}

// ----------------------------------------------------------- UpdateAnnotationCommand ----

UpdateAnnotationCommand::UpdateAnnotationCommand(pdf::Annotation before, pdf::Annotation after)
    : before_(std::move(before)), after_(std::move(after)) {}

QString UpdateAnnotationCommand::typeName() const {
    return QString::fromLatin1(kTypeUpdate);
}

QString UpdateAnnotationCommand::displayName() const {
    return QStringLiteral("修改%1").arg(pdf::annotationTypeName(after_.type));
}

QByteArray UpdateAnnotationCommand::serialize() const {
    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyType), typeName());
    object.insert(QString::fromLatin1(kKeyBefore), pdf::annotationToJson(before_));
    object.insert(QString::fromLatin1(kKeyAfter), pdf::annotationToJson(after_));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool UpdateAnnotationCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令不是合法的 JSON：%1").arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QString::fromLatin1(kKeyType)).toString() != typeName()) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令类型不匹配");
        }
        return false;
    }
    pdf::Annotation before;
    pdf::Annotation after;
    if (!pdf::annotationFromJson(object.value(QString::fromLatin1(kKeyBefore)).toObject(), &before) ||
        !pdf::annotationFromJson(object.value(QString::fromLatin1(kKeyAfter)).toObject(), &after)) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令里的批注内容不完整");
        }
        return false;
    }
    if (before.id != after.id) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令里的前后批注不是同一条");
        }
        return false;
    }
    before_ = before;
    after_ = after;
    return true;
}

bool UpdateAnnotationCommand::apply(EditContext* context) const {
    AnnotationModel* model = (context != nullptr) ? context->annotationModel() : nullptr;
    if (model == nullptr) {
        return false;
    }
    return model->update(after_);
}

bool UpdateAnnotationCommand::revert(EditContext* context) const {
    AnnotationModel* model = (context != nullptr) ? context->annotationModel() : nullptr;
    if (model == nullptr) {
        return false;
    }
    return model->update(before_);
}

// ------------------------------------------------------------------- 工厂 ----

std::unique_ptr<EditCommand> createEditCommand(const QString& typeName) {
    if (typeName == QLatin1String(kTypeAdd)) {
        return std::make_unique<AddAnnotationCommand>();
    }
    if (typeName == QLatin1String(kTypeRemove)) {
        return std::make_unique<RemoveAnnotationCommand>();
    }
    if (typeName == QLatin1String(kTypeUpdate)) {
        return std::make_unique<UpdateAnnotationCommand>();
    }
    // 页面命令也进同一个工厂：恢复日志按类型名重建命令，
    // 不该分两套。
    if (typeName == QLatin1String("page.insert")) {
        return std::make_unique<InsertBlankPageCommand>();
    }
    if (typeName == QLatin1String("page.delete")) {
        return std::make_unique<DeletePageCommand>();
    }
    if (typeName == QLatin1String("page.rotate")) {
        return std::make_unique<RotatePageCommand>();
    }
    if (typeName == QLatin1String("page.move")) {
        return std::make_unique<MovePageCommand>();
    }
    return nullptr;
}

std::unique_ptr<EditCommand> deserializeEditCommand(const QByteArray& data, QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("批注命令不是合法的 JSON：%1").arg(parseError.errorString());
        }
        return nullptr;
    }
    const QString typeName = document.object().value(QString::fromLatin1(kKeyType)).toString();
    auto command = createEditCommand(typeName);
    if (command == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("未知的批注命令类型：%1").arg(typeName);
        }
        return nullptr;
    }
    if (!command->deserialize(data, error)) {
        return nullptr;
    }
    return command;
}

}  // namespace aipdf::doc
