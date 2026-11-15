#include "app/AnnotationController.h"
#include "pdf/SoundFormat.h"

#include "app/PdfPageOperations.h"
#include "doc/EditCommand.h"
#include "doc/PageCommand.h"

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
    pageOperations_ = std::make_unique<PdfPageOperations>(document_);
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
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3")
              .arg(pdf::annotationTypeKey(type))
              .arg(pageIndex)
              .arg(id));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addLine(pdf::AnnotationType type, int pageIndex, const QPointF& from,
                                      const QPointF& to) {
    if (type != pdf::AnnotationType::Line && type != pdf::AnnotationType::Arrow) {
        return 0;
    }
    if (QLineF(from, to).length() < 0.5) {
        return 0;  // 手抖点一下不产生"零长度直线"
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = type;
    annotation.vertices = { from, to };
    annotation.rect = QRectF(from, to).normalized();
    // 线的矩形不能是零高/零宽：水平线的 bounding box 高度为 0，外观流会算不出 bbox。
    if (annotation.rect.width() < 1.0) {
        annotation.rect.setWidth(1.0);
    }
    if (annotation.rect.height() < 1.0) {
        annotation.rect.setHeight(1.0);
    }
    annotation.color = pdf::defaultAnnotationColor(type);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3")
              .arg(pdf::annotationTypeKey(type))
              .arg(pageIndex)
              .arg(id));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addPolygon(pdf::AnnotationType type, int pageIndex,
                                         const QVector<QPointF>& vertices) {
    if (type != pdf::AnnotationType::Polygon && type != pdf::AnnotationType::Cloud) {
        return 0;
    }
    if (vertices.size() < 3) {
        return 0;
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = type;
    annotation.vertices = vertices;
    QRectF rect = QRectF(vertices.first(), vertices.first());
    for (const QPointF& vertex : vertices) {
        rect = rect.united(QRectF(vertex, QSizeF(0.0, 0.0)));
    }
    // 留出线宽与扇贝的余量，否则外观流 bbox 会把边缘切掉。
    annotation.rect = rect.adjusted(-3.0, -3.0, 3.0, 3.0);
    annotation.color = pdf::defaultAnnotationColor(type);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3")
              .arg(pdf::annotationTypeKey(type))
              .arg(pageIndex)
              .arg(id));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addMeasureShape(MeasureMode mode, int pageIndex,
                                              const QVector<QPointF>& vertices, double scale) {
    if (vertices.size() < 2) {
        return 0;
    }
    if (mode == MeasureMode::Area && vertices.size() < 3) {
        return 0;
    }
    const double factor = (scale > 0.0) ? scale : 1.0;
    const QString label = formatMeasure(mode, vertices, factor);
    if (label.isEmpty()) {
        return 0;
    }

    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::Measure;
    annotation.vertices = vertices;
    annotation.measureScale = factor;
    annotation.contents = label;
    QRectF rect = QRectF(vertices.first(), vertices.first());
    for (const QPointF& vertex : vertices) {
        rect = rect.united(QRectF(vertex, QSizeF(0.0, 0.0)));
    }
    annotation.rect = rect.adjusted(-2.0, -2.0, 2.0, 2.0);
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Measure);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 measure=%4")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::Measure))
              .arg(pageIndex)
              .arg(id)
              .arg(label));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addAttachment(int pageIndex, const QPointF& point,
                                           const QString& fileName, const QByteArray& data) {
    if (fileName.isEmpty() || data.isEmpty()) {
        return 0;
    }
    constexpr double kSize = 20.0;
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::FileAttachment;
    annotation.rect = QRectF(point.x() - kSize / 2.0, point.y() - kSize / 2.0, kSize, kSize);
    annotation.contents = fileName;
    annotation.attachmentName = fileName;
    annotation.attachmentData = data;
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::FileAttachment);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 size=%4")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::FileAttachment))
              .arg(pageIndex)
              .arg(id)
              .arg(data.size()));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addMeasure(int pageIndex, const QPointF& from, const QPointF& to,
                                        double scale) {
    if (QLineF(from, to).length() < 0.5) {
        return 0;
    }
    const double factor = (scale > 0.0) ? scale : 1.0;
    const double length = QLineF(from, to).length() * factor;
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::Measure;
    annotation.vertices = { from, to };
    annotation.measureScale = factor;
    annotation.contents = QStringLiteral("%1 pt").arg(length, 0, 'f', 1);
    annotation.rect = QRectF(from, to).normalized();
    if (annotation.rect.width() < 1.0) {
        annotation.rect.setWidth(1.0);
    }
    if (annotation.rect.height() < 1.0) {
        annotation.rect.setHeight(1.0);
    }
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Measure);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 length=%4")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::Measure))
              .arg(pageIndex)
              .arg(id)
              .arg(length, 0, 'f', 2));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addStamp(int pageIndex, const QRectF& rect, const QString& label) {
    if (rect.width() <= 0.0 || rect.height() <= 0.0) {
        return 0;
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::Stamp;
    annotation.rect = rect;
    annotation.contents = label.trimmed().isEmpty() ? QStringLiteral("APPROVED") : label.trimmed();
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Stamp);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::Stamp))
              .arg(pageIndex)
              .arg(id));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addImageStamp(int pageIndex, const QRectF& rect,
                                            const QByteArray& imageBytes, const QString& label) {
    if (imageBytes.isEmpty() || rect.width() <= 0.0 || rect.height() <= 0.0) {
        return 0;
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::Stamp;
    annotation.rect = rect;
    annotation.imageData = imageBytes;
    annotation.contents = label;
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Stamp);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 image=%4")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::Stamp))
              .arg(pageIndex)
              .arg(id)
              .arg(imageBytes.size()));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addSoundFromWav(int pageIndex, const QRectF& rect,
                                              const QString& name, const QByteArray& wav) {
    if (wav.isEmpty() || rect.width() <= 0.0 || rect.height() <= 0.0) {
        return 0;
    }
    // 原生 /Sound 注释：PDFium 建不出来，所以它**只存在于模型里**，
    // 保存时由 DocumentWriter 把字典插进页面 /Annots（见 PageAnnotsInjector）。
    const pdf::SoundPayload payload = pdf::parseWav(wav);
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::Sound;
    annotation.rect = rect;
    annotation.soundName = name;
    annotation.contents = name;
    if (payload.isValid()) {
        annotation.soundData = payload.pcm;
        annotation.soundRate = payload.rate;
        annotation.soundChannels = payload.channels;
        annotation.soundBits = payload.bits;
    }
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Sound);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 wav=%4")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::FileAttachment))
              .arg(pageIndex)
              .arg(id)
              .arg(wav.size()));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addSound(int pageIndex, const QRectF& rect, const QString& name,
                                       const QByteArray& pcm, int rate, int channels, int bits) {
    if (pcm.isEmpty() || rect.width() <= 0.0 || rect.height() <= 0.0) {
        return 0;
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    // PDFium 这个版本没有创建 /Sound 注释的接口（FPDFPage_CreateAnnot 对
    // FPDF_ANNOT_SOUND 返回空），所以音频以"文件附件"的形式随文档保存：
    // 阅读器里点开附件就能播放，数据一个字节都不少。等哪天能写 /Sound 注释，
    // 这里换回来即可，模型字段已经留好。
    annotation.type = pdf::AnnotationType::FileAttachment;
    annotation.rect = rect;
    annotation.soundData = pcm;
    annotation.soundName = name;
    annotation.soundRate = rate > 0 ? rate : 8000;
    annotation.soundChannels = channels > 0 ? channels : 1;
    annotation.soundBits = bits > 0 ? bits : 8;
    annotation.attachmentName = name;
    annotation.attachmentData = pcm;
    // 附件名会显示在阅读器的批注里，跟着界面语言走。
    annotation.contents = tr("声音: %1").arg(name);
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::Sound);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 pcm=%4")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::Sound))
              .arg(pageIndex)
              .arg(id)
              .arg(pcm.size()));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addFreeText(int pageIndex, const QRectF& rect, const QString& text,
                                         double fontSize) {
    if (rect.width() <= 0.0 || rect.height() <= 0.0 || text.trimmed().isEmpty()) {
        return 0;
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = pdf::AnnotationType::FreeText;
    annotation.rect = rect;
    annotation.contents = text;
    annotation.fontSize = (fontSize > 0.0) ? fontSize : 12.0;
    annotation.color = pdf::defaultAnnotationColor(pdf::AnnotationType::FreeText);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::FreeText))
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
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 chars=%4")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::Note))
              .arg(pageIndex)
              .arg(id)
              .arg(contents.size()));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addSquare(int pageIndex, const QRectF& rect) {
    return addShape(pdf::AnnotationType::Square, pageIndex, rect);
}

quint64 AnnotationController::addShape(pdf::AnnotationType type, int pageIndex, const QRectF& rect) {
    if (rect.width() <= 0.0 || rect.height() <= 0.0) {
        return 0;
    }
    if (type != pdf::AnnotationType::Square && type != pdf::AnnotationType::Ellipse) {
        return 0;  // 只有以矩形为界的图形走这条路
    }
    pdf::Annotation annotation;
    annotation.id = model_.allocateId();
    annotation.pageIndex = pageIndex;
    annotation.type = type;
    annotation.rect = rect;
    annotation.color = pdf::defaultAnnotationColor(type);

    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    const quint64 id = annotation.id;
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3")
              .arg(pdf::annotationTypeKey(type))
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
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation add type=%1 page=%2 id=%3 points=%4")
              .arg(pdf::annotationTypeKey(pdf::AnnotationType::Ink))
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
    if (!undoStack_.push(std::move(command), this)) {
        return false;
    }
    trace(QStringLiteral("annotation remove id=%1").arg(id));
    emit undoStateChanged();
    return true;
}

quint64 AnnotationController::importAnnotation(const pdf::Annotation& source) {
    pdf::Annotation annotation = source;
    annotation.id = model_.allocateId();
    if (annotation.createdAt.isNull()) {
        annotation.createdAt = QDateTime::currentDateTimeUtc();
    }
    const quint64 id = annotation.id;
    auto command = std::make_unique<doc::AddAnnotationCommand>(annotation);
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation import type=%1 page=%2 id=%3")
              .arg(static_cast<int>(annotation.type))
              .arg(annotation.pageIndex)
              .arg(id));
    emit undoStateChanged();
    return id;
}

quint64 AnnotationController::addReply(quint64 parentId, const QString& text, const QString& author) {
    const pdf::Annotation* parent = model_.find(parentId);
    if (parent == nullptr || text.trimmed().isEmpty()) {
        return 0;
    }
    pdf::Annotation reply;
    reply.id = model_.allocateId();
    reply.pageIndex = parent->pageIndex;
    reply.type = pdf::AnnotationType::Note;
    reply.replyToId = parentId;
    reply.contents = text.trimmed();
    reply.author = author;
    reply.createdAt = QDateTime::currentDateTimeUtc();
    reply.color = parent->color;
    // 回复贴在父批注右侧，避免和父批注完全重叠。
    reply.rect = parent->rect.translated(parent->rect.width() + 6.0, 0.0);

    const quint64 id = reply.id;
    auto command = std::make_unique<doc::AddAnnotationCommand>(reply);
    if (!undoStack_.push(std::move(command), this)) {
        return 0;
    }
    trace(QStringLiteral("annotation reply parent=%1 id=%2").arg(parentId).arg(id));
    emit undoStateChanged();
    return id;
}

bool AnnotationController::updateAppearance(const QVector<quint64>& ids, const QColor& color,
                                            double lineWidth, double opacity) {
    if (ids.isEmpty()) {
        return false;
    }
    // 一条批注一个命令：撤销时逐条回滚，符合"我改了几条就撤几次"的直觉。
    int changed = 0;
    for (quint64 id : ids) {
        const pdf::Annotation* annotation = model_.find(id);
        if (annotation == nullptr) {
            continue;
        }
        pdf::Annotation before = *annotation;
        pdf::Annotation after = before;
        if (color.isValid()) {
            after.color = color;
        }
        if (lineWidth > 0.0) {
            after.lineWidth = lineWidth;
        }
        if (opacity > 0.0 && opacity <= 1.0) {
            after.opacity = opacity;
        }
        if (after.color == before.color && qFuzzyCompare(after.lineWidth + 1.0, before.lineWidth + 1.0) &&
            qFuzzyCompare(after.opacity + 1.0, before.opacity + 1.0)) {
            continue;
        }
        auto command = std::make_unique<doc::UpdateAnnotationCommand>(before, after);
        if (undoStack_.push(std::move(command), this)) {
            ++changed;
        }
    }
    if (changed == 0) {
        return false;
    }
    trace(QStringLiteral("annotation appearance updated count=%1").arg(changed));
    emit undoStateChanged();
    return true;
}

bool AnnotationController::setReviewState(quint64 id, const QString& state) {
    const pdf::Annotation* annotation = model_.find(id);
    if (annotation == nullptr) {
        return false;
    }
    pdf::Annotation before = *annotation;
    pdf::Annotation after = before;
    after.reviewState = state;
    auto command = std::make_unique<doc::UpdateAnnotationCommand>(before, after);
    if (!undoStack_.push(std::move(command), this)) {
        return false;
    }
    trace(QStringLiteral("annotation state id=%1 state=%2").arg(id).arg(state));
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
    if (!undoStack_.push(std::move(command), this)) {
        return false;
    }
    trace(QStringLiteral("annotation update id=%1").arg(id));
    emit undoStateChanged();
    return true;
}

bool AnnotationController::undo() {
    if (!undoStack_.undo(this)) {
        return false;
    }
    trace(QStringLiteral("annotation undo count=%1 pages=%2")
              .arg(undoStack_.undoCount())
              .arg(pageCount()));
    emit undoStateChanged();
    return true;
}

bool AnnotationController::redo() {
    if (!undoStack_.redo(this)) {
        return false;
    }
    trace(QStringLiteral("annotation redo count=%1 pages=%2")
              .arg(undoStack_.redoCount())
              .arg(pageCount()));
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

QHash<quint64, QPair<QString, QByteArray>> AnnotationController::attachmentPayloads() const {
    QHash<quint64, QPair<QString, QByteArray>> payloads;
    for (const pdf::Annotation& annotation : model_.all()) {
        if (annotation.type == pdf::AnnotationType::FileAttachment &&
            !annotation.attachmentData.isEmpty()) {
            payloads.insert(annotation.id,
                            { annotation.attachmentName, annotation.attachmentData });
        }
    }
    return payloads;
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

// ------------------------------------------------------- 页面结构操作 ----

bool AnnotationController::insertBlankPage(int index, double width, double height) {
    if (!document_.isValid() || index < 0 || index > document_.pageCount()) {
        return false;
    }
    auto command = std::make_unique<doc::InsertBlankPageCommand>(index, width, height);
    if (!undoStack_.push(std::move(command), this)) {
        return false;
    }
    trace(QStringLiteral("page insert index=%1 pages=%2").arg(index).arg(pageCount()));
    emit undoStateChanged();
    return true;
}

bool AnnotationController::deletePage(int index) {
    if (!document_.isValid() || index < 0 || index >= document_.pageCount()) {
        return false;
    }
    auto command = std::make_unique<doc::DeletePageCommand>(index);
    if (!undoStack_.push(std::move(command), this)) {
        return false;
    }
    trace(QStringLiteral("page delete index=%1 pages=%2").arg(index).arg(pageCount()));
    emit undoStateChanged();
    return true;
}

bool AnnotationController::rotatePage(int index, int quarterTurns) {
    if (!document_.isValid() || index < 0 || index >= document_.pageCount()) {
        return false;
    }
    // 撤销要把角度改回去，所以命令里要带着旋转前的角度。
    const int before = pageRotation(index);
    auto command = std::make_unique<doc::RotatePageCommand>(index, before, quarterTurns);
    if (!undoStack_.push(std::move(command), this)) {
        return false;
    }
    trace(QStringLiteral("page rotate index=%1 turns=%2 pages=%3").arg(index).arg(quarterTurns).arg(pageCount()));
    emit undoStateChanged();
    return true;
}


bool AnnotationController::replacePageWithBackup(int index,
                                                std::shared_ptr<doc::PageBackup> backup) {
    if (!document_.isValid() || backup == nullptr || index < 0 ||
        index >= document_.pageCount()) {
        return false;
    }
    auto command = std::make_unique<doc::ReplacePageCommand>(index, std::move(backup));
    if (!undoStack_.push(std::move(command), this)) {
        return false;
    }
    emit undoStateChanged();
    return true;
}

bool AnnotationController::movePage(int from, int to) {
    if (!document_.isValid()) {
        return false;
    }
    const int count = document_.pageCount();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return false;
    }
    auto command = std::make_unique<doc::MovePageCommand>(from, to);
    if (!undoStack_.push(std::move(command), this)) {
        return false;
    }
    trace(QStringLiteral("page move from=%1 to=%2 pages=%3").arg(from).arg(to).arg(pageCount()));
    emit undoStateChanged();
    return true;
}

int AnnotationController::pageCount() const {
    return document_.pageCount();
}

double AnnotationController::pageWidth(int index) const {
    const auto page = document_.page(index);
    return page.has_value() ? page->displayWidthPoints() : 0.0;
}

double AnnotationController::pageHeight(int index) const {
    const auto page = document_.page(index);
    return page.has_value() ? page->displayHeightPoints() : 0.0;
}

int AnnotationController::pageRotation(int index) const {
    const auto page = document_.page(index);
    if (!page.has_value()) {
        return 0;
    }
    // 页面的 /Rotate 以四分之一圈计。
    return page->rotationDegrees() / 90;
}

// ---------------------------------------------------------- 编辑上下文 ----

doc::AnnotationModel* AnnotationController::annotationModel() {
    return &model_;
}

doc::PageOperations* AnnotationController::pages() {
    return pageOperations_.get();
}

void AnnotationController::pageStructureChanged(const QVector<int>& pageMapping) {
    // 页面挪了位置：之前记下「哪些页投影过」的页号已经作废。
    // 适配层按页维护着批注记账，页面操作时已经跟着重排过，
    // 这里不需要再按旧页号重投影。
    syncedPages_.clear();
    // 几何缓存作废：页数、顺序、某页的旋转都可能变了。
    geometry_.clear();
    // 批注的页码跟着页面走。
    model_.remapPages(pageMapping);
    emit documentStructureChanged();
}

QVector<QByteArray> AnnotationController::serializedHistory() const {
    return undoStack_.serializedHistory();
}

bool AnnotationController::applyCommand(std::unique_ptr<doc::EditCommand> command) {
    if (command == nullptr) {
        return false;
    }
    if (!undoStack_.push(std::move(command), this)) {
        return false;
    }
    emit undoStateChanged();
    return true;
}

}  // namespace aipdf::app
