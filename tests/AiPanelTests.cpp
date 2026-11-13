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

#include <QAbstractButton>
#include <QLabel>
#include <QListWidget>
#include <QTextBrowser>
#include <QtTest>

using namespace aipdf::app;

namespace {

// 按对象名找面板里的控件（控件都是 private 的，只能靠 findChild 拿）。
//
// 不叫 findChild：QObject 自带一个同名成员函数，
// 类里的非限定调用会先找到它，命名空间作用域的同名模板就被遮蔽了。
template <typename T>
T* findWidget(AiPanel* panel, const QString& objectName) {
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
    QVERIFY(document != nullptr);

    document->setChecked(true);
    QCOMPARE(panel.scope(), AiPanel::Scope::WholeDocument);

    document->setChecked(false);
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

QTEST_MAIN(AiPanelTests)
#include "AiPanelTests.moc"
