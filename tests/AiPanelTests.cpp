// v0.11.0 AI 面板整篇问答交互的单元测试。
//
// 重点守「范围切换」这条链路。
//
// 它曾经只听 QButtonGroup::buttonClicked：而程序化 toggle（界面自动化的
// TogglePattern 走的就是这条路）**只发 toggled**，不发 clicked ——
// 于是范围切不过去，提问还是按当前页走。界面上完全看不出来，
// 只有自动化点按钮才暴露，而那一轮 CI 要跑十七分钟。
//
// 所以这条链路必须有本地就能跑的守卫。
#include "app/AiPanel.h"
#include "app/UploadPreviewDialog.h"
#include "ai/UploadPreview.h"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QtTest>

using namespace aipdf;
using namespace aipdf::app;

namespace {

// 按对象名找面板里的控件（控件都是 private 的，只能靠 findChild 拿）。
//
// 不叫 findChild：QObject 自带一个同名成员函数，
// 类里的非限定调用会先找到它，命名空间作用域的同名模板就被遮蔽了。
template <typename T>
T* findWidget(QWidget* panel, const QString& objectName) {
    return panel->findChild<T*>(objectName);
}

}  // namespace

class AiPanelTests : public QObject {
    Q_OBJECT

private slots:
    void scopeStartsOnCurrentPage();
    void clickingDocumentScopeSwitchesScope();
    void programmaticToggleAlsoSwitchesScope();
    void scopeLabelTellsUserWhichHalf();
    void citationListStartsEmpty();
    void documentQaWithoutWorkerReportsError();

    // 上传预览（v0.12.0）：发送前把「发什么、发到哪、多少」念给用户看。
    void uploadPreviewShowsTargetSizeAndPages();
    void uploadPreviewCancelIsTheDefaultButton();
};

void AiPanelTests::scopeStartsOnCurrentPage() {
    AiPanel panel;
    QCOMPARE(panel.scope(), AiPanel::Scope::CurrentPage);
}

void AiPanelTests::clickingDocumentScopeSwitchesScope() {
    AiPanel panel;
    QAbstractButton* document = findWidget<QAbstractButton>(&panel,
                                                           QStringLiteral("aiScopeDocumentButton"));
    QAbstractButton* page = findWidget<QAbstractButton>(&panel,
                                                        QStringLiteral("aiScopePageButton"));
    QVERIFY(document != nullptr);
    QVERIFY(page != nullptr);

    document->click();
    QCOMPARE(panel.scope(), AiPanel::Scope::WholeDocument);

    page->click();
    QCOMPARE(panel.scope(), AiPanel::Scope::CurrentPage);
}

void AiPanelTests::programmaticToggleAlsoSwitchesScope() {
    // 程序化 toggle 只发 toggled，不发 clicked。
    // 面板必须照样切换——不然界面自动化切了范围，提问还是按当前页走。
    AiPanel panel;
    QAbstractButton* document = findWidget<QAbstractButton>(&panel,
                                                           QStringLiteral("aiScopeDocumentButton"));
    QAbstractButton* page = findWidget<QAbstractButton>(&panel,
                                                        QStringLiteral("aiScopePageButton"));
    QVERIFY(document != nullptr);
    QVERIFY(page != nullptr);

    document->setChecked(true);
    QCOMPARE(panel.scope(), AiPanel::Scope::WholeDocument);

    // 切回「当前页」要去选另一个按钮，而不是取消勾选 document：
    // 两个按钮在同一个互斥 QButtonGroup 里，唯一被选中的那个
    // setChecked(false) 会被 Qt 直接忽略（单选按钮本来就不允许取消选中）。
    // 界面自动化的 TogglePattern 也是这个语义：切范围 = 选中另一个。
    page->setChecked(true);
    QCOMPARE(panel.scope(), AiPanel::Scope::CurrentPage);
}

void AiPanelTests::scopeLabelTellsUserWhichHalf() {
    // 范围标签是「本次送了什么」的可见说明，也是验收脚本判断
    // 范围切换真的生效的依据，不能空着。
    AiPanel panel;
    QLabel* label = findWidget<QLabel>(&panel, QStringLiteral("aiScopeLabel"));
    QVERIFY(label != nullptr);
    QVERIFY(label->text().isEmpty());

    findWidget<QAbstractButton>(&panel, QStringLiteral("aiScopeDocumentButton"))->click();
    QVERIFY(!label->text().isEmpty());
    QVERIFY(label->text().contains(QStringLiteral("整篇文档")));
}

void AiPanelTests::citationListStartsEmpty() {
    AiPanel panel;
    QListWidget* list = findWidget<QListWidget>(&panel, QStringLiteral("aiCitationList"));
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 0);
    // 还没有问答过，引用核验报告是空的。
    QCOMPARE(panel.lastCitationReport().validCount, 0);
    QCOMPARE(panel.lastCitationReport().invalidCount, 0);
    QVERIFY(panel.lastContext().isEmpty());
    QVERIFY(panel.citationStatusText().isEmpty());
}

void AiPanelTests::documentQaWithoutWorkerReportsError() {
    // 没有 Worker 时不能崩：给一句看得见的提示。
    AiPanel panel;
    QTextBrowser* conversation = findWidget<QTextBrowser>(&panel, QStringLiteral("aiConversation"));
    QVERIFY(conversation != nullptr);

    panel.requestDocumentQa(QStringLiteral("这份文档讲了什么？"));
    QVERIFY(conversation->toPlainText().contains(QStringLiteral("还没连上")));
}

void AiPanelTests::uploadPreviewShowsTargetSizeAndPages() {
    ai::UploadPreview preview;
    preview.mode = ai::DataMode::SelectedPages;
    preview.host = QStringLiteral("api.openai.com");
    preview.chunkCount = 3;
    preview.charCount = 1200;
    preview.pages = {2, 5};
    preview.decision.allowed = true;
    UploadPreviewDialog dialog(preview);

    QLabel* target = findWidget<QLabel>(&dialog, QStringLiteral("uploadPreviewTarget"));
    QVERIFY(target != nullptr);
    QVERIFY2(target->text().contains(QStringLiteral("api.openai.com")),
             qPrintable(target->text()));

    QLabel* content = findWidget<QLabel>(&dialog, QStringLiteral("uploadPreviewContent"));
    QVERIFY(content != nullptr);
    QVERIFY2(content->text().contains(QStringLiteral("3")), qPrintable(content->text()));
    QVERIFY2(content->text().contains(QStringLiteral("1200")), qPrintable(content->text()));

    QLabel* pages = findWidget<QLabel>(&dialog, QStringLiteral("uploadPreviewPages"));
    QVERIFY(pages != nullptr);
    QVERIFY2(pages->text().contains(QStringLiteral("2、5")), qPrintable(pages->text()));
}

void AiPanelTests::uploadPreviewCancelIsTheDefaultButton() {
    // 手快回车时不该把文档发出去：默认按钮必须是「取消」，不是「发送」。
    ai::UploadPreview preview;
    preview.host = QStringLiteral("api.openai.com");
    preview.chunkCount = 1;
    preview.charCount = 100;
    preview.decision.allowed = true;
    UploadPreviewDialog dialog(preview);

    auto* cancel = findWidget<QPushButton>(&dialog, QStringLiteral("uploadPreviewCancel"));
    auto* send = findWidget<QPushButton>(&dialog, QStringLiteral("uploadPreviewSend"));
    QVERIFY(cancel != nullptr);
    QVERIFY(send != nullptr);
    QVERIFY(cancel->isDefault());
    QVERIFY(!send->isDefault());
}

QTEST_MAIN(AiPanelTests)
#include "AiPanelTests.moc"
