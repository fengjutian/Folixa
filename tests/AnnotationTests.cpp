// 批注模型、命令序列化与统一撤销栈的单元测试。
//
// 这一层不碰 PDFium：批注是纯数据，撤销逻辑也是纯逻辑，
// 都能脱离界面与文档单独验证。

#include "doc/AnnotationModel.h"
#include "doc/EditCommand.h"
#include "doc/UndoStack.h"
#include "pdf/Annotation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

using aipdf::doc::AddAnnotationCommand;
using aipdf::doc::AnnotationModel;
using aipdf::doc::EditCommand;
using aipdf::doc::RemoveAnnotationCommand;
using aipdf::doc::UndoStack;
using aipdf::doc::UpdateAnnotationCommand;
using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;

namespace {

Annotation makeHighlight(quint64 id = 1, int pageIndex = 0) {
    Annotation annotation;
    annotation.id = id;
    annotation.pageIndex = pageIndex;
    annotation.type = AnnotationType::Highlight;
    annotation.rect = QRectF(10.0, 20.0, 100.0, 12.0);
    annotation.quads.append(QRectF(10.0, 20.0, 100.0, 12.0));
    annotation.color = QColor(255, 214, 0);
    annotation.contents = QStringLiteral("重点");
    annotation.author = QStringLiteral("Folixa");
    return annotation;
}

Annotation makeInk(quint64 id = 2, int pageIndex = 1) {
    Annotation annotation;
    annotation.id = id;
    annotation.pageIndex = pageIndex;
    annotation.type = AnnotationType::Ink;
    annotation.strokes.append({QPointF(1.0, 2.0), QPointF(3.0, 4.0), QPointF(5.0, 6.0)});
    annotation.rect = QRectF(1.0, 2.0, 4.0, 4.0);
    annotation.color = QColor(226, 62, 62);
    return annotation;
}

}  // namespace

class AnnotationTests : public QObject {
    Q_OBJECT

private slots:
    void modelRejectsDuplicateId();
    void modelRejectsZeroId();
    void modelKeepsInsertionOrder();
    void modelCountsPerPage();
    void modelUpdateKeepsIdentity();
    void modelEmitsChangedForAffectedPage();

    void annotationJsonRoundTrip();
    void annotationJsonRoundTripKeepsStrokes();
    void annotationJsonRejectsBrokenRect();

    void addCommandSerializesAndDeserializes();
    void removeCommandSerializesAndDeserializes();
    void updateCommandSerializesAndDeserializes();
    void commandDeserializeRejectsGarbage();
    void commandDeserializeRejectsMismatchedType();
    void unknownCommandTypeIsRejected();

    void undoRestoresPreviousState();
    void redoReappliesCommand();
    void newCommandClearsRedoStack();
    void undoStackRespectsDepthLimit();
    void undoTextDescribesCommand();
    void failedApplyIsNotPushed();
};

void AnnotationTests::modelRejectsDuplicateId() {
    AnnotationModel model;
    QVERIFY(model.add(makeHighlight(1)));
    QVERIFY(!model.add(makeHighlight(1)));
    QCOMPARE(model.count(), 1);
}

void AnnotationTests::modelRejectsZeroId() {
    AnnotationModel model;
    // id 为 0 表示「没有身份」，模型不接受：撤销栈靠 id 认批注。
    QVERIFY(!model.add(makeHighlight(0)));
    QCOMPARE(model.count(), 0);
}

void AnnotationTests::modelKeepsInsertionOrder() {
    AnnotationModel model;
    QVERIFY(model.add(makeHighlight(7)));
    QVERIFY(model.add(makeInk(3)));
    const QVector<Annotation> all = model.all();
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.at(0).id, quint64(7));
    QCOMPARE(all.at(1).id, quint64(3));
}

void AnnotationTests::modelCountsPerPage() {
    AnnotationModel model;
    QVERIFY(model.add(makeHighlight(1, 0)));
    QVERIFY(model.add(makeHighlight(2, 0)));
    QVERIFY(model.add(makeInk(3, 1)));
    QCOMPARE(model.count(), 3);
    QCOMPARE(model.countForPage(0), 2);
    QCOMPARE(model.countForPage(1), 1);
    QCOMPARE(model.countForPage(2), 0);
    QCOMPARE(model.forPage(1).size(), 1);
    QCOMPARE(model.forPage(1).first().id, quint64(3));
}

void AnnotationTests::modelUpdateKeepsIdentity() {
    AnnotationModel model;
    QVERIFY(model.add(makeHighlight(1)));
    Annotation updated = makeHighlight(1);
    updated.color = QColor(10, 20, 30);
    updated.contents = QStringLiteral("改过的备注");
    QVERIFY(model.update(updated));
    const Annotation* found = model.find(1);
    QVERIFY(found != nullptr);
    QCOMPARE(found->color, QColor(10, 20, 30));
    QCOMPARE(found->contents, QStringLiteral("改过的备注"));
}

void AnnotationTests::modelEmitsChangedForAffectedPage() {
    AnnotationModel model;
    QSignalSpy spy(&model, &AnnotationModel::changed);
    QVERIFY(model.add(makeHighlight(1, 3)));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 3);

    QVERIFY(model.remove(1));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), 3);
}

void AnnotationTests::annotationJsonRoundTrip() {
    const Annotation original = makeHighlight(42, 2);
    const QJsonObject object = aipdf::pdf::annotationToJson(original);
    Annotation restored;
    QVERIFY(aipdf::pdf::annotationFromJson(object, &restored));
    QCOMPARE(restored.id, original.id);
    QCOMPARE(restored.pageIndex, original.pageIndex);
    QCOMPARE(restored.type, original.type);
    QCOMPARE(restored.rect, original.rect);
    QCOMPARE(restored.quads.size(), original.quads.size());
    QCOMPARE(restored.quads.first(), original.quads.first());
    QCOMPARE(restored.color, original.color);
    QCOMPARE(restored.contents, original.contents);
    QCOMPARE(restored.author, original.author);
}

void AnnotationTests::annotationJsonRoundTripKeepsStrokes() {
    const Annotation original = makeInk();
    const QJsonObject object = aipdf::pdf::annotationToJson(original);
    Annotation restored;
    QVERIFY(aipdf::pdf::annotationFromJson(object, &restored));
    QCOMPARE(restored.strokes.size(), 1);
    QCOMPARE(restored.strokes.first().size(), 3);
    QCOMPARE(restored.strokes.first().at(2), QPointF(5.0, 6.0));
}

void AnnotationTests::annotationJsonRejectsBrokenRect() {
    QJsonObject object = aipdf::pdf::annotationToJson(makeHighlight());
    QJsonArray broken;
    broken.append(1.0);
    broken.append(2.0);
    object.insert(QStringLiteral("rect"), broken);
    Annotation restored;
    QVERIFY(!aipdf::pdf::annotationFromJson(object, &restored));
}

void AnnotationTests::addCommandSerializesAndDeserializes() {
    AddAnnotationCommand command(makeHighlight(9, 1));
    const QByteArray data = command.serialize();
    QVERIFY(!data.isEmpty());

    QString error;
    auto restored = aipdf::doc::deserializeEditCommand(data, &error);
    QVERIFY2(restored != nullptr, qPrintable(error));
    QCOMPARE(restored->typeName(), QStringLiteral("annotation.add"));
    QCOMPARE(restored->displayName(), QStringLiteral("添加高亮"));

    auto* add = dynamic_cast<AddAnnotationCommand*>(restored.get());
    QVERIFY(add != nullptr);
    QCOMPARE(add->annotation().id, quint64(9));
    QCOMPARE(add->annotation().pageIndex, 1);
}

void AnnotationTests::removeCommandSerializesAndDeserializes() {
    RemoveAnnotationCommand command(makeHighlight(5, 0));
    QString error;
    auto restored = aipdf::doc::deserializeEditCommand(command.serialize(), &error);
    QVERIFY2(restored != nullptr, qPrintable(error));
    auto* remove = dynamic_cast<RemoveAnnotationCommand*>(restored.get());
    QVERIFY(remove != nullptr);
    QCOMPARE(remove->annotation().id, quint64(5));
    QCOMPARE(restored->displayName(), QStringLiteral("删除高亮"));
}

void AnnotationTests::updateCommandSerializesAndDeserializes() {
    Annotation before = makeHighlight(11);
    Annotation after = before;
    after.color = QColor(1, 2, 3);
    UpdateAnnotationCommand command(before, after);

    QString error;
    auto restored = aipdf::doc::deserializeEditCommand(command.serialize(), &error);
    QVERIFY2(restored != nullptr, qPrintable(error));
    QCOMPARE(restored->typeName(), QStringLiteral("annotation.update"));

    // 反序列化后再执行，结果应当与修改后的批注一致。
    AnnotationModel model;
    QVERIFY(model.add(before));
    QVERIFY(restored->apply(&model));
    const Annotation* found = model.find(11);
    QVERIFY(found != nullptr);
    QCOMPARE(found->color, QColor(1, 2, 3));
}

void AnnotationTests::commandDeserializeRejectsGarbage() {
    QString error;
    QVERIFY(aipdf::doc::deserializeEditCommand("not json at all", &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

void AnnotationTests::commandDeserializeRejectsMismatchedType() {
    AddAnnotationCommand command(makeHighlight());
    // 把类型改成别的命令：反序列化必须拒绝，而不是照单全收。
    QJsonObject object = QJsonDocument::fromJson(command.serialize()).object();
    object.insert(QStringLiteral("type"), QStringLiteral("annotation.remove"));
    const QByteArray tampered = QJsonDocument(object).toJson(QJsonDocument::Compact);

    QString error;
    QVERIFY(aipdf::doc::deserializeEditCommand(tampered, &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

void AnnotationTests::unknownCommandTypeIsRejected() {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("annotation.explode"));
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact);

    QString error;
    QVERIFY(aipdf::doc::deserializeEditCommand(data, &error) == nullptr);
    QVERIFY(!error.isEmpty());
    QVERIFY(aipdf::doc::createEditCommand(QStringLiteral("annotation.explode")) == nullptr);
}

void AnnotationTests::undoRestoresPreviousState() {
    AnnotationModel model;
    UndoStack stack;

    QVERIFY(stack.push(std::make_unique<AddAnnotationCommand>(makeHighlight(1)), &model));
    QCOMPARE(model.count(), 1);

    QVERIFY(stack.undo(&model));
    QCOMPARE(model.count(), 0);
    QVERIFY(model.find(1) == nullptr);
}

void AnnotationTests::redoReappliesCommand() {
    AnnotationModel model;
    UndoStack stack;

    QVERIFY(stack.push(std::make_unique<AddAnnotationCommand>(makeHighlight(1)), &model));
    QVERIFY(stack.undo(&model));
    QCOMPARE(model.count(), 0);

    QVERIFY(stack.redo(&model));
    QCOMPARE(model.count(), 1);
    QVERIFY(model.find(1) != nullptr);
}

void AnnotationTests::newCommandClearsRedoStack() {
    AnnotationModel model;
    UndoStack stack;

    QVERIFY(stack.push(std::make_unique<AddAnnotationCommand>(makeHighlight(1)), &model));
    QVERIFY(stack.undo(&model));
    QVERIFY(stack.canRedo());

    // 在旧状态上分叉：重做栈必须失效，否则会重做到另一条分支上。
    QVERIFY(stack.push(std::make_unique<AddAnnotationCommand>(makeHighlight(2)), &model));
    QVERIFY(!stack.canRedo());
    QCOMPARE(model.count(), 1);
}

void AnnotationTests::undoStackRespectsDepthLimit() {
    AnnotationModel model;
    UndoStack stack;

    constexpr int extra = 25;
    for (int index = 0; index < UndoStack::kMaxDepth + extra; ++index) {
        const quint64 id = static_cast<quint64>(index) + 1;
        QVERIFY(stack.push(std::make_unique<AddAnnotationCommand>(makeHighlight(id)), &model));
    }
    QCOMPARE(stack.undoCount(), UndoStack::kMaxDepth);

    // 全部撤销后，被挤出撤销历史的那 extra 条批注仍在模型里：
    // 深度上限的语义就是「再早的操作撤不了」，而不是「把批注也撤了」。
    while (stack.canUndo()) {
        QVERIFY(stack.undo(&model));
    }
    QCOMPARE(model.count(), extra);
    QVERIFY(model.find(1) != nullptr);   // 最老的一条已无命令可撤
    QVERIFY(model.find(quint64(extra + 1)) == nullptr);  // 最新的一条撤干净了
}

void AnnotationTests::undoTextDescribesCommand() {
    AnnotationModel model;
    UndoStack stack;

    QVERIFY(stack.undoText().isEmpty());
    QVERIFY(stack.push(std::make_unique<AddAnnotationCommand>(makeHighlight()), &model));
    QCOMPARE(stack.undoText(), QStringLiteral("撤销 添加高亮"));
    QCOMPARE(stack.redoText(), QString());

    QVERIFY(stack.undo(&model));
    QCOMPARE(stack.redoText(), QStringLiteral("重做 添加高亮"));
}

void AnnotationTests::failedApplyIsNotPushed() {
    AnnotationModel model;
    UndoStack stack;

    // 重复 id：apply 会失败，命令不该进撤销历史。
    QVERIFY(model.add(makeHighlight(1)));
    QVERIFY(!stack.push(std::make_unique<AddAnnotationCommand>(makeHighlight(1)), &model));
    QCOMPARE(stack.undoCount(), 0);
    QVERIFY(!stack.canUndo());
}

QTEST_MAIN(AnnotationTests)
#include "AnnotationTests.moc"
