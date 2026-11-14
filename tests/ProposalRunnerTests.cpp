// v0.14.0：提案 → 命令的翻译测试，以及确认框的默认按钮。
//
// 守两件事：
//   1. 翻译出来的命令顺序正确（删多页必须从后往前，否则第二次删错位置）；
//   2. 确认框的默认按钮是「取消」——回车不等于同意。
#include "app/BlankPageDetector.h"
#include "app/EditProposalDialog.h"
#include "app/ProposalRunner.h"

#include "doc/PageCommand.h"
#include "tools/ProposalValidator.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

using namespace aipdf::app;
using namespace aipdf::tools;

namespace {

// 造一份已通过校验的提案：直接走 validateProposal，保证与生产路径一致。
Proposal makeProposal(const QJsonArray& raw, int pageCount = 10) {
    return validateProposal(raw, DocumentSnapshot{pageCount, 1});
}

QJsonObject op(const QString& tool, const QJsonObject& args) {
    return QJsonObject{{QStringLiteral("tool"), tool}, {QStringLiteral("args"), args}};
}

}  // namespace

class ProposalRunnerTests : public QObject {
    Q_OBJECT

private slots:
    void deletesPagesFromTheEnd();
    void convertsPageNumbersToZeroBased();
    void insertsBlankPageWithDefaultSize();
    void mapsNoteAnnotation();
    void unsupportedToolIsReported();
    void dialogDefaultButtonIsCancel();
    void dialogListsEveryOperation();
    void blankPageIsDetectedByInkRatio();
    void pageWithTextIsNotEmpty();
    void failedRenderIsNotBlank();
};

void ProposalRunnerTests::deletesPagesFromTheEnd() {
    QJsonArray raw;
    raw.append(op(QStringLiteral("doc.delete_pages"),
                  QJsonObject{{QStringLiteral("pages"), QJsonArray{2, 5, 9}}}));
    const Proposal proposal = makeProposal(raw);
    QVERIFY(proposal.ok());

    ProposalRunner::CommandList commands;
    QString error;
    QVERIFY2(ProposalRunner::buildCommands(proposal, &commands, &error), qPrintable(error));
    QCOMPARE(static_cast<int>(commands.size()), 3);

    // 必须是 9 → 5 → 2（降序）：先删小的会让后面的下标整体前移。
    QCOMPARE(static_cast<aipdf::doc::DeletePageCommand*>(commands.at(0).get())->index(), 8);
    QCOMPARE(static_cast<aipdf::doc::DeletePageCommand*>(commands.at(1).get())->index(), 4);
    QCOMPARE(static_cast<aipdf::doc::DeletePageCommand*>(commands.at(2).get())->index(), 1);
}

void ProposalRunnerTests::convertsPageNumbersToZeroBased() {
    QJsonArray raw;
    raw.append(op(QStringLiteral("doc.move_page"),
                  QJsonObject{{QStringLiteral("from"), 1}, {QStringLiteral("to"), 4}}));
    const Proposal proposal = makeProposal(raw);
    ProposalRunner::CommandList commands;
    QString error;
    QVERIFY(ProposalRunner::buildCommands(proposal, &commands, &error));
    auto* move = static_cast<aipdf::doc::MovePageCommand*>(commands.front().get());
    QCOMPARE(move->from(), 0);
    QCOMPARE(move->to(), 3);
}

void ProposalRunnerTests::insertsBlankPageWithDefaultSize() {
    QJsonArray raw;
    raw.append(
        op(QStringLiteral("doc.insert_blank_page"), QJsonObject{{QStringLiteral("index"), 3}}));
    const Proposal proposal = makeProposal(raw);
    ProposalRunner::CommandList commands;
    QString error;
    QVERIFY(ProposalRunner::buildCommands(proposal, &commands, &error));
    auto* insert = static_cast<aipdf::doc::InsertBlankPageCommand*>(commands.front().get());
    QCOMPARE(insert->index(), 2);
    // 没给尺寸就按 A4，不能是 0×0。
    QVERIFY(insert->width() > 500.0);
    QVERIFY(insert->height() > 700.0);
}

void ProposalRunnerTests::mapsNoteAnnotation() {
    QJsonArray raw;
    raw.append(op(QStringLiteral("annotation.add_note"),
                  QJsonObject{{QStringLiteral("page"), 2},
                              {QStringLiteral("rect"), QJsonArray{10.0, 20.0, 30.0, 40.0}},
                              {QStringLiteral("text"), QStringLiteral("这里需要复核")}}));
    const Proposal proposal = makeProposal(raw);
    ProposalRunner::CommandList commands;
    QString error;
    QVERIFY2(ProposalRunner::buildCommands(proposal, &commands, &error), qPrintable(error));
    auto* add = static_cast<aipdf::doc::AddAnnotationCommand*>(commands.front().get());
    const aipdf::pdf::Annotation annotation = add->annotation();
    QCOMPARE(annotation.pageIndex, 1);
    QCOMPARE(annotation.contents, QStringLiteral("这里需要复核"));
    QCOMPARE(annotation.rect, QRectF(10.0, 20.0, 30.0, 40.0));
    QVERIFY(!annotation.author.isEmpty());
}

void ProposalRunnerTests::unsupportedToolIsReported() {
    // 直接构造一份「绕过校验」的提案：翻译层也必须拒绝不认识的工具。
    Proposal proposal;
    ProposedOperation operation;
    operation.toolId = QStringLiteral("doc.do_magic");
    operation.displayName = QStringLiteral("魔法");
    proposal.operations.append(operation);

    ProposalRunner::CommandList commands;
    QString error;
    QVERIFY(!ProposalRunner::buildCommands(proposal, &commands, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(commands.empty());
}

void ProposalRunnerTests::dialogDefaultButtonIsCancel() {
    QJsonArray raw;
    raw.append(op(QStringLiteral("doc.delete_pages"),
                  QJsonObject{{QStringLiteral("pages"), QJsonArray{1}}}));
    const Proposal proposal = makeProposal(raw);

    EditProposalDialog dialog(proposal);
    auto* cancel = dialog.findChild<QPushButton*>(QStringLiteral("editProposalCancel"));
    auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("editProposalApply"));
    QVERIFY(cancel != nullptr);
    QVERIFY(apply != nullptr);
    // 手快回车时不该把文档改了。
    QVERIFY(cancel->isDefault());
    QVERIFY(!apply->isDefault());
}

void ProposalRunnerTests::dialogListsEveryOperation() {
    QJsonArray raw;
    raw.append(op(QStringLiteral("doc.delete_pages"),
                  QJsonObject{{QStringLiteral("pages"), QJsonArray{2}}}));
    raw.append(op(QStringLiteral("doc.move_page"),
                  QJsonObject{{QStringLiteral("from"), 1}, {QStringLiteral("to"), 3}}));
    const Proposal proposal = makeProposal(raw);

    EditProposalDialog dialog(proposal);
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("editProposalList"));
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("删除")));
    QVERIFY(list->item(1)->text().contains(QStringLiteral("移动")));
}

void ProposalRunnerTests::blankPageIsDetectedByInkRatio() {
    QImage white(200, 280, QImage::Format_Grayscale8);
    white.fill(255);
    QVERIFY(aipdf::app::looksBlank(white));
    QCOMPARE(aipdf::app::inkRatio(white), 0.0);

    // 一点点扫描噪声不该把空白页判成有内容。
    QImage noisy = white;
    for (int i = 0; i < 20; ++i) {
        noisy.setPixel(i * 3, i * 2, 235);
    }
    QVERIFY(aipdf::app::looksBlank(noisy));
}

void ProposalRunnerTests::pageWithTextIsNotEmpty() {
    QImage page(200, 280, QImage::Format_Grayscale8);
    page.fill(255);
    // 画一块 40×10 的「字」：占比 200/56000 ≈ 0.36%，明显高于空白阈值。
    for (int y = 40; y < 50; ++y) {
        for (int x = 20; x < 60; ++x) {
            page.setPixel(x, y, 10);
        }
    }
    QVERIFY(aipdf::app::inkRatio(page) > aipdf::app::kBlankInkRatio);
    QVERIFY(!aipdf::app::looksBlank(page));
}

void ProposalRunnerTests::failedRenderIsNotBlank() {
    // 渲染失败（空图）不能当成空白页：那会删掉用户真正的内容页。
    QVERIFY(!aipdf::app::looksBlank(QImage()));
}

QTEST_MAIN(ProposalRunnerTests)
#include "ProposalRunnerTests.moc"
