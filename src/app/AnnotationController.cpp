#include "app/AnnotationController.h"

#include "doc/EditCommand.h"

#include <QDateTime>
#include <QFile>

namespace aipdf::app {

namespace {

// 验收探针用的轨迹文件：批注的创建与撤销要能被脚本看到。
void trace(const QString& message) {
    const QString path = qEnvironmentVariable("AIPDF_TRACE_FILE");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " + message + "\n").toUtf8());
    }
}

// 批注作者。批注正文一律按**纯文本**处理：
// 不解释富文本、不执行脚本、不加载外部内容。
QString annotationAuthor() {
    return QStringLiteral("Folixa");
}

}  // namespace

AnnotationController::AnnotationController(pdf::PdfDocument document, QObject* parent)
    : QObject(parent), document_(std::move(document)) {
    connect(&model_, &doc::AnnotationModel::changed, this, &AnnotationController::handleModelChanged);
}

AnnotationController::~AnnotationController() = default;

void AnnotationController::setUserRotation(int degrees) {
    int normalized = ((degrees % 360) + 360) % 360;
    normalized = (normalized / 90) * 90;
    if (normalized == userRotation_) {
        return;
    }
    userRotation_ = normalized;
    // 旋转变了，页面几何缓存要清：变换的角度变了。
    geometry_.clear();
    emit annotationsChanged(-1);
}

int AnnotationController::userRotation() const {
    return userRotation_;
}

void AnnotationController::setTool(Tool tool) {
    if (tool == tool_) {
        return;
    }
    tool_ = tool;
    trace(QStringLiteral("annotation tool=%1").arg(static_cast<int>(tool)));
    emit toolChanged();
}

AnnotationController::Tool AnnotationController::tool() const {
    return tool_;
}

quint64 AnnotationController::addMarkAnnotation(pdf::AnnotationType type,
                                                int pageIndex,
                                                const QVector<QRectF>& quads) {
    if (quads.isEmpty()) {
        return 0;
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = type;
    annotation.quads = quads;
    annotation.rect = quads.first();
    for (const QRectF& quad : quads) {
        annotation.rect = annotation.rect.united(quad);
    }
    annotation.color = pdf::defaultAnnotationColor(type);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), &model_)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3")
              .arg(static_cast<int>(type))
              .arg(pageIndex)
              .arg(id));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addNote(int pageIndex, const QRectF& rect, const QString& contents) {
    if (rect.width() <= 0.0 || rect.height() <= 0.0) {
        return 0;
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::Note;
    annotation.rect = rect;
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Note);
    annotation.contents = contents;
    annotation.author = annotationAuthor();

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), &model_)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 chars=%4")
              .arg(static_cast<int>(pdf::AnnotationType::Note))
              .arg(pageIndex)
              .arg(id)
              .arg(contents.size()));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addSquare(int pageIndex, const QRectF& rect) {
    if (rect.width() <= 0.0 || rect.height() <= 0.0) {
        return 0;
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::Square;
    annotation.rect = rect;
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Square);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), &model_)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3")
              .arg(static_cast<int>(pdf::AnnotationType::Square))
              .arg(pageIndex)
              .arg(id));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addInk(int pageIndex, const QVector<QPointF>& stroke) {
    if (stroke.size() < 2) {
        // 只有一个点的笔画没有意义，忽略。
        return 0;
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::Ink;
    annotation.strokes.append(stroke);
    annotation.rect = QRectF(stroke.first(), stroke.last()).normalized();
    for (const QPointF& point : stroke) {
        annotation.rect = annotation.rect.united(QRectF(point, point));
    }
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Ink);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), &model_)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 points=%4")
              .arg(static_cast<int>(pdf::AnnotationType::Ink))
              .arg(pageIndex)
              .arg(id)
              .arg(stroke.size()));
    emit undoStateChanged();
    return id;
}

bool AnnotationController::removeAnnotation(quint64 id) {
    const pdf::Annotation* annotation = model_.find(id);
    if (annotation == nullptr) {
        return false;
    }
    auto command = std::make_unique<doc::RemoveAnnotationCommand>(*annotation);
    if (!undoStack_.push(std::move(command), &model_)) {
        return false;
    }
    trace(QStringLiteral("annotation remove id=%1").arg(id));
    emit undoStateChanged();
    return true;
}

bool AnnotationController::updateAnnotation(quint64 id,
                                            const QColor& color,
                                            const QString& contents) {
    const pdf::Annotation* annotation = model_.find(id);
    if (annotation == nullptr) {
        return false;
    }
    pdf::Annotation before = *annotation;
    pdf::Annotation after = before;
    if (color.isValid()) {
        after.color = color;
    }
    after.contents = contents;

    auto command = std::make_unique<doc::UpdateAnnotationCommand>(before, after);
    if (!undoStack_.push(std::move(command), &model_)) {
        return false;
    }
    trace(QStringLiteral("annotation update id=%1").arg(id));
    emit undoStateChanged();
    return true;
}

bool AnnotationController::undo() {
    if (!undoStack_.undo(&model_)) {
        return false;
    }
    trace(QStringLiteral("annotation undo count=%1").arg(undoStack_.undoCount()));
    emit undoStateChanged();
    return true;
}

bool AnnotationController::redo() {
    if (!undoStack_.redo(&model_)) {
        return false;
    }
    trace(QStringLiteral("annotation redo count=%1").arg(undoStack_.redoCount()));
    emit undoStateChanged();
    return true;
}

bool AnnotationController::canUndo() const {
    return undoStack_.canUndo();
}

bool AnnotationController::canRedo() const {
    return undoStack_.canRedo();
}

QString AnnotationController::undoText() const {
    return undoStack_.undoText();
}

QString AnnotationController::redoText() const {
    return undoStack_.redoText();
}

QVector<pdf::Annotation> AnnotationController::annotationsForPage(int pageIndex) const {
    return model_.forPage(pageIndex);
}

QVector<pdf::Annotation> AnnotationController::allAnnotations() const {
    return model_.all();
}

int AnnotationController::annotationCount() const {
    return model_.count();
}

int AnnotationController::annotationCountForPage(int pageIndex) const {
    return model_.countForPage(pageIndex);
}

bool AnnotationController::transformForPage(int pageIndex, PageTransform* transform) const {
    if (transform == nullptr) {
        return false;
    }
    PageGeometry geometry;
    if (!pageGeometry(pageIndex, &geometry)) {
        return false;
    }
    const pdf::MediaBox& box = geometry.contentBox;
    const double left = qMin(box.left, box.right);
    const double right = qMax(box.left, box.right);
    const double bottom = qMin(box.bottom, box.top);
    const double top = qMax(box.bottom, box.top);
    transform->setPage(left, bottom, right - left, top - bottom);
    // 总旋转 = 页面自带的 /Rotate + 用户额外旋转（都是顺时针）。
    transform->setRotationDegrees(geometry.rotationDegrees + userRotation_);
    return true;
}

const doc::AnnotationModel* AnnotationController::model() const {
    return &model_;
}

QVector<QRectF> AnnotationController::toUserSpaceRects(int pageIndex,
                                                        const QVector<QRectF>& displayRects) const {
    QVector<QRectF> result;
    PageTransform transform;
    if (!transformForPage(pageIndex, &transform)) {
        return result;
    }
    result.reserve(displayRects.size());
    for (const QRectF& rect : displayRects) {
        // 旋转页上，一个显示空间的矩形在用户空间里会换向：
        // 四个角点逐个换算，再取包围盒。
        const QPointF a = transform.toUserSpace(rect.topLeft());
        const QPointF b = transform.toUserSpace(rect.bottomRight());
        const QPointF c = transform.toUserSpace(rect.topRight());
        const QPointF d = transform.toUserSpace(rect.bottomLeft());
        const double left = qMin(qMin(a.x(), b.x()), qMin(c.x(), d.x()));
        const double right = qMax(qMax(a.x(), b.x()), qMax(c.x(), d.x()));
        const double lower = qMin(qMin(a.y(), b.y()), qMin(c.y(), d.y()));
        const double upper = qMax(qMax(a.y(), b.y()), qMax(c.y(), d.y()));
        result.append(QRectF(QPointF(left, lower), QPointF(right, upper)).normalized());
    }
    return result;
}

void AnnotationController::syncPageToDocument(int pageIndex) {
    if (!document_.isValid()) {
        return;
    }
    auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return;
    }
    // 单向投影：文档里的这页批注 = 模型里的这页批注。
    const QVector<pdf::Annotation> items = model_.forPage(pageIndex);
    page->setAnnotations(items);
    syncedPages_.insert(pageIndex);
    // 投影结果记进轨迹：验收脚本据此确认批注真的进了文档。
    trace(QStringLiteral("annotation doc sync page=%1 count=%2").arg(pageIndex).arg(items.size()));
}

bool AnnotationController::pageGeometry(int pageIndex, PageGeometry* geometry) const {
    if (geometry == nullptr) {
        return false;
    }
    const auto it = geometry_.constFind(pageIndex);
    if (it != geometry_.constEnd()) {
        *geometry = it.value();
        return true;
    }
    const auto page = document_.page(pageIndex);
    if (!page.has_value()) {
        return false;
    }
    PageGeometry result;
    result.contentBox = page->contentBox();
    result.rotationDegrees = page->rotationDegrees();
    geometry_.insert(pageIndex, result);
    *geometry = result;
    return true;
}

void AnnotationController::handleModelChanged(int pageIndex) {
    if (pageIndex < 0) {
        // 整体重置：把写过的页全部重新投影一遍（此时模型是空的，等于全撤）。
        for (int index : syncedPages_) {
            syncPageToDocument(index);
        }
    } else {
        syncPageToDocument(pageIndex);
    }
    emit annotationsChanged(pageIndex);
}

}  // namespace aipdf::app
