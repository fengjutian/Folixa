#include "doc/edit/PageCommand.h"

#include "doc/edit/AnnotationModel.h"
#include "doc/edit/EditContext.h"
#include "i18n/Translate.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace aipdf::doc {

namespace {

constexpr auto kTypeInsert = "page.insert";
constexpr auto kTypeDelete = "page.delete";
constexpr auto kTypeRotate = "page.rotate";
constexpr auto kTypeMove = "page.move";
constexpr auto kTypeSwap = "page.swap";

constexpr auto kKeyType = "type";
constexpr auto kKeyIndex = "index";
constexpr auto kKeyWidth = "width";
constexpr auto kKeyHeight = "height";
constexpr auto kKeyBefore = "before";
constexpr auto kKeyAfter = "after";
constexpr auto kKeyFrom = "from";
constexpr auto kKeyTo = "to";

// 解析命令 JSON。失败时通过 error 说明原因。
bool parseCommandJson(const QByteArray& data,
                      const QString& expectedType,
                      QJsonObject* object,
                      QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "页面命令不是合法的 JSON：%1")
                         .arg(parseError.errorString());
        }
        return false;
    }
    *object = document.object();
    if (object->value(QString::fromLatin1(kKeyType)).toString() != expectedType) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "页面命令类型不匹配");
        }
        return false;
    }
    return true;
}

// 页面结构没变时的映射（页序号原样不动）。
QVector<int> identityMapping(int count) {
    QVector<int> mapping;
    mapping.resize(count);
    for (int i = 0; i < count; ++i) {
        mapping[i] = i;
    }
    return mapping;
}

}  // namespace

// --------------------------------------------------- InsertBlankPageCommand ----

InsertBlankPageCommand::InsertBlankPageCommand(int index, double width, double height)
    : index_(index), width_(width), height_(height) {}

QString InsertBlankPageCommand::typeName() const {
    return QString::fromLatin1(kTypeInsert);
}

QString InsertBlankPageCommand::displayName() const {
    return i18n::tr("aipdf::doc::PageCommand", "插入空白页");
}

QByteArray InsertBlankPageCommand::serialize() const {
    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyType), typeName());
    object.insert(QString::fromLatin1(kKeyIndex), index_);
    object.insert(QString::fromLatin1(kKeyWidth), width_);
    object.insert(QString::fromLatin1(kKeyHeight), height_);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool InsertBlankPageCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonObject object;
    if (!parseCommandJson(data, typeName(), &object, error)) {
        return false;
    }
    if (!object.value(QString::fromLatin1(kKeyIndex)).isDouble() ||
        !object.value(QString::fromLatin1(kKeyWidth)).isDouble() ||
        !object.value(QString::fromLatin1(kKeyHeight)).isDouble()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "插入页命令缺少页序号或尺寸");
        }
        return false;
    }
    index_ = object.value(QString::fromLatin1(kKeyIndex)).toInt();
    width_ = object.value(QString::fromLatin1(kKeyWidth)).toDouble();
    height_ = object.value(QString::fromLatin1(kKeyHeight)).toDouble();
    if (index_ < 0 || width_ <= 0.0 || height_ <= 0.0) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "插入页命令的参数不合法");
        }
        return false;
    }
    return true;
}

bool InsertBlankPageCommand::apply(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr) {
        return false;
    }
    QVector<int> mapping;
    if (!pages->insertBlankPage(index_, width_, height_, &mapping)) {
        return false;
    }
    context->pageStructureChanged(mapping);
    return true;
}

bool InsertBlankPageCommand::revert(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr) {
        return false;
    }
    // 撤销插入就是删掉刚插入的那一页。
    QVector<int> mapping;
    if (!pages->deletePage(index_, &mapping)) {
        return false;
    }
    context->pageStructureChanged(mapping);
    return true;
}

int InsertBlankPageCommand::index() const {
    return index_;
}

double InsertBlankPageCommand::width() const {
    return width_;
}

double InsertBlankPageCommand::height() const {
    return height_;
}

// -------------------------------------------------------- DeletePageCommand ----

DeletePageCommand::DeletePageCommand(int index) : index_(index) {}

QString DeletePageCommand::typeName() const {
    return QString::fromLatin1(kTypeDelete);
}

QString DeletePageCommand::displayName() const {
    return i18n::tr("aipdf::doc::PageCommand", "删除页");
}

QByteArray DeletePageCommand::serialize() const {
    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyType), typeName());
    object.insert(QString::fromLatin1(kKeyIndex), index_);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool DeletePageCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonObject object;
    if (!parseCommandJson(data, typeName(), &object, error)) {
        return false;
    }
    if (!object.value(QString::fromLatin1(kKeyIndex)).isDouble()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "删除页命令缺少页序号");
        }
        return false;
    }
    index_ = object.value(QString::fromLatin1(kKeyIndex)).toInt();
    if (index_ < 0) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "删除页命令的页序号不合法");
        }
        return false;
    }
    return true;
}

bool DeletePageCommand::apply(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    AnnotationModel* model =
        (context != nullptr) ? context->annotationModel() : nullptr;
    if (pages == nullptr || model == nullptr) {
        return false;
    }
    // 先备份：删掉之后内容就没了，撤销要靠这份备份。
    if (backup_ == nullptr && !pages->backupPage(index_, &backup_)) {
        return false;
    }
    // 记下这页上的批注：删页时模型会把它们丢掉，撤销时要放回去。
    if (removedAnnotations_.isEmpty()) {
        removedAnnotations_ = model->forPage(index_);
    }
    QVector<int> mapping;
    if (!pages->deletePage(index_, &mapping)) {
        return false;
    }
    context->pageStructureChanged(mapping);
    return true;
}

bool DeletePageCommand::revert(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    AnnotationModel* model =
        (context != nullptr) ? context->annotationModel() : nullptr;
    if (pages == nullptr || model == nullptr) {
        return false;
    }
    if (backup_ == nullptr) {
        // 没有备份就恢复不了（例如从恢复日志重放出来的命令）。
        return false;
    }
    if (!pages->restorePage(index_, *backup_)) {
        return false;
    }
    // 恢复回来的页面带着原来的批注，批注模型里的页码也要跟着回去：
    // 原本在 index_ 之后的页整体往后挪一位。
    const int count = pages->pageCount();
    QVector<int> mapping;
    mapping.resize(count);
    for (int i = 0; i < count; ++i) {
        mapping[i] = (i < index_) ? i : i + 1;
    }
    context->pageStructureChanged(mapping);
    // 批注放回模型：页面回来了，批注也得回来。
    for (const pdf::Annotation& annotation : removedAnnotations_) {
        pdf::Annotation restored = annotation;
        restored.pageIndex = index_;
        model->add(restored);
    }
    return true;
}

int DeletePageCommand::index() const {
    return index_;
}

// -------------------------------------------------------- RotatePageCommand ----

RotatePageCommand::RotatePageCommand(int index, int beforeTurns, int afterTurns)
    : index_(index), beforeTurns_(beforeTurns), afterTurns_(afterTurns) {}

QString RotatePageCommand::typeName() const {
    return QString::fromLatin1(kTypeRotate);
}

QString RotatePageCommand::displayName() const {
    return i18n::tr("aipdf::doc::PageCommand", "旋转页");
}

QByteArray RotatePageCommand::serialize() const {
    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyType), typeName());
    object.insert(QString::fromLatin1(kKeyIndex), index_);
    object.insert(QString::fromLatin1(kKeyBefore), beforeTurns_);
    object.insert(QString::fromLatin1(kKeyAfter), afterTurns_);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool RotatePageCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonObject object;
    if (!parseCommandJson(data, typeName(), &object, error)) {
        return false;
    }
    if (!object.value(QString::fromLatin1(kKeyIndex)).isDouble() ||
        !object.value(QString::fromLatin1(kKeyBefore)).isDouble() ||
        !object.value(QString::fromLatin1(kKeyAfter)).isDouble()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "旋转页命令缺少页序号或角度");
        }
        return false;
    }
    index_ = object.value(QString::fromLatin1(kKeyIndex)).toInt();
    beforeTurns_ = object.value(QString::fromLatin1(kKeyBefore)).toInt();
    afterTurns_ = object.value(QString::fromLatin1(kKeyAfter)).toInt();
    if (index_ < 0) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "旋转页命令的页序号不合法");
        }
        return false;
    }
    return true;
}

bool RotatePageCommand::apply(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr) {
        return false;
    }
    if (!pages->setPageRotation(index_, afterTurns_)) {
        return false;
    }
    // 旋转不改页数与顺序，但页面几何变了，缓存要清、界面要重排。
    context->pageStructureChanged(identityMapping(pages->pageCount()));
    return true;
}

bool RotatePageCommand::revert(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr) {
        return false;
    }
    if (!pages->setPageRotation(index_, beforeTurns_)) {
        return false;
    }
    context->pageStructureChanged(identityMapping(pages->pageCount()));
    return true;
}

int RotatePageCommand::index() const {
    return index_;
}

int RotatePageCommand::beforeTurns() const {
    return beforeTurns_;
}

int RotatePageCommand::afterTurns() const {
    return afterTurns_;
}

// ---------------------------------------------------------- MovePageCommand ----

MovePageCommand::MovePageCommand(int from, int to) : from_(from), to_(to) {}

QString MovePageCommand::typeName() const {
    return QString::fromLatin1(kTypeMove);
}

QString MovePageCommand::displayName() const {
    return i18n::tr("aipdf::doc::PageCommand", "移动页");
}

QByteArray MovePageCommand::serialize() const {
    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyType), typeName());
    object.insert(QString::fromLatin1(kKeyFrom), from_);
    object.insert(QString::fromLatin1(kKeyTo), to_);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool MovePageCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonObject object;
    if (!parseCommandJson(data, typeName(), &object, error)) {
        return false;
    }
    if (!object.value(QString::fromLatin1(kKeyFrom)).isDouble() ||
        !object.value(QString::fromLatin1(kKeyTo)).isDouble()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "移动页命令缺少页序号");
        }
        return false;
    }
    from_ = object.value(QString::fromLatin1(kKeyFrom)).toInt();
    to_ = object.value(QString::fromLatin1(kKeyTo)).toInt();
    if (from_ < 0 || to_ < 0) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "移动页命令的页序号不合法");
        }
        return false;
    }
    return true;
}

bool MovePageCommand::apply(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr) {
        return false;
    }
    QVector<int> mapping;
    if (!pages->movePage(from_, to_, &mapping)) {
        return false;
    }
    context->pageStructureChanged(mapping);
    return true;
}

bool MovePageCommand::revert(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr) {
        return false;
    }
    // 撤销移动就是把这一页搬回原处。
    QVector<int> mapping;
    if (!pages->movePage(to_, from_, &mapping)) {
        return false;
    }
    context->pageStructureChanged(mapping);
    return true;
}

int MovePageCommand::from() const {
    return from_;
}

int MovePageCommand::to() const {
    return to_;
}


// ------------------------------------------------------ SwapPagesCommand ----

SwapPagesCommand::SwapPagesCommand(int first, int second) : first_(first), second_(second) {}

QString SwapPagesCommand::typeName() const {
    return QString::fromLatin1(kTypeSwap);
}

QString SwapPagesCommand::displayName() const {
    return i18n::tr("aipdf::doc::PageCommand", "交换页面");
}

QByteArray SwapPagesCommand::serialize() const {
    QJsonObject object;
    object.insert(QString::fromLatin1(kKeyType), typeName());
    object.insert(QString::fromLatin1(kKeyFrom), first_);
    object.insert(QString::fromLatin1(kKeyTo), second_);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool SwapPagesCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonObject object;
    if (!parseCommandJson(data, typeName(), &object, error)) {
        return false;
    }
    if (!object.value(QString::fromLatin1(kKeyFrom)).isDouble() ||
        !object.value(QString::fromLatin1(kKeyTo)).isDouble()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "交换页命令缺少页序号");
        }
        return false;
    }
    first_ = object.value(QString::fromLatin1(kKeyFrom)).toInt();
    second_ = object.value(QString::fromLatin1(kKeyTo)).toInt();
    if (first_ < 0 || second_ < 0 || first_ == second_) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::doc::PageCommand", "交换页命令的页序号不合法");
        }
        return false;
    }
    return true;
}

bool SwapPagesCommand::apply(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr) {
        return false;
    }
    const int count = pages->pageCount();
    if (first_ < 0 || first_ >= count || second_ < 0 || second_ >= count || first_ == second_) {
        return false;
    }
    // 交换 = 两次搬移：
    //   1) 把第二页搬到第一页的位置（第一页顺移到 first+1）；
    //   2) 把（此时在 first+1 的）第一页搬到 second。
    // 推导：原序列 [.., A(first), .., B(second), ..]
    //   搬 1 后 [.., B(first), A(first+1), ..]
    //   搬 2 把 A 从 first+1 挪到 second，中间那段自动补位
    //   → [.., B(first), .., A(second), ..] 正是交换后的结果。
    //
    // 第二步失败时把第一步退回去：不能让文档停在「只搬了一半」的状态。
    QVector<int> mapping;
    const int low = qMin(first_, second_);
    const int high = qMax(first_, second_);
    if (!pages->movePage(high, low, &mapping)) {
        return false;
    }
    // 搬完第一步：high 处的页到了 low，原来 low 的页到了 low+1。
    if (!pages->movePage(low + 1, high, &mapping)) {
        QVector<int> rollback;
        pages->movePage(low, low + 1, &rollback);
        return false;
    }
    context->pageStructureChanged(mapping);
    return true;
}

bool SwapPagesCommand::revert(EditContext* context) const {
    // 交换是自身的逆：换一次回去，再换一次就回来了。
    return apply(context);
}

int SwapPagesCommand::first() const {
    return first_;
}

int SwapPagesCommand::second() const {
    return second_;
}


// --------------------------------------------------- ReplacePageCommand ----

ReplacePageCommand::ReplacePageCommand(int index, std::shared_ptr<PageBackup> replacement)
    : index_(index), replacement_(std::move(replacement)) {}

QString ReplacePageCommand::typeName() const {
    return QStringLiteral("page.replace");
}

QString ReplacePageCommand::displayName() const {
    return i18n::tr("aipdf::doc::PageCommand", "替换页面");
}

QByteArray ReplacePageCommand::serialize() const {
    QJsonObject object;
    object.insert(QStringLiteral("type"), typeName());
    object.insert(QStringLiteral("index"), index_);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool ReplacePageCommand::deserialize(const QByteArray& data, QString* error) {
    QJsonObject object;
    if (!parseCommandJson(data, typeName(), &object, error)) {
        return false;
    }
    index_ = object.value(QStringLiteral("index")).toInt(-1);
    return index_ >= 0;
}

bool ReplacePageCommand::apply(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr || replacement_ == nullptr) {
        return false;
    }
    if (removed_ == nullptr && !pages->backupPage(index_, &removed_)) {
        return false;
    }
    QVector<int> mapping;
    if (!pages->deletePage(index_, &mapping)) {
        return false;
    }
    if (!pages->restorePage(index_, *replacement_)) {
        return false;
    }
    context->pageStructureChanged(identityMapping(pages->pageCount()));
    return true;
}

bool ReplacePageCommand::revert(EditContext* context) const {
    PageOperations* pages = (context != nullptr) ? context->pages() : nullptr;
    if (pages == nullptr || removed_ == nullptr) {
        return false;
    }
    QVector<int> mapping;
    if (!pages->deletePage(index_, &mapping)) {
        return false;
    }
    if (!pages->restorePage(index_, *removed_)) {
        return false;
    }
    context->pageStructureChanged(identityMapping(pages->pageCount()));
    return true;
}

}  // namespace aipdf::doc
