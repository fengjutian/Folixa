// 电子签章「签署流程 / 签署报告 / 签署记录」的专项单测。
//
// 这一层是纯数据层：不依赖 PDFium，也不依赖 qpdf，
// 所以目标只链接 QtCore 与 QtTest。
//
// 测试的立场：签署记录是给事后追责用的流水。
// 所以负例（非法状态迁移、重复签署、没名字就拒签）比正例更重要——
// 这些地方一旦松了，流水就不再可信。

#include <QtTest>

#include "pdf/security/SignatureWorkflow.h"

using namespace aipdf::pdf;

namespace {

// 造一位签署人。
Signatory makeSignatory(const QString& name, int order) {
    Signatory s;
    s.name = name;
    s.order = order;
    return s;
}

// 造一个已经建好流程、已经加好签署人、已经开始签署的流程。
// 返回的流程处于 InProgress，方便直接测 recordXxx 系列。
SignatureWorkflow makeStartedWorkflow(const QStringList& names) {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("采购合同会签"), QStringLiteral("王发起"));
    int order = 1;
    for (const QString& name : names) {
        workflow.addSignatory(makeSignatory(name, order));
        ++order;
    }
    workflow.startSigning();
    return workflow;
}

}  // namespace

class SignatureWorkflowTests : public QObject {
    Q_OBJECT

private slots:
    void createWorkflowStartsInDraft();
    void createWorkflowRejectsEmptyTitle();
    void createWorkflowRejectsEmptyInitiator();
    void createWorkflowRejectsSecondCall();
    void addSignatoryAppendsEvent();
    void addSignatoryRejectsEmptyName();
    void addSignatoryRejectsDuplicateName();
    void addSignatoryRejectsDuplicateOrder();
    void addSignatoryAutoAssignsOrderWhenUnspecified();
    void addSignatoryIgnoresIncomingStatus();
    void addSignatoryRejectedAfterStart();
    void removeSignatoryRemovesPendingSignatory();
    void removeSignatoryRejectsUnknownName();
    void removeSignatoryRejectsAfterSigningStarted();
    void removeSignatoryRejectsOnCompletedWorkflow();
    void removeSignatoryRecordsEvent();
    void startSigningRequiresSignatories();
    void startSigningRejectsSecondCall();
    void recordSignedMarksSignatory();
    void recordSignedRejectsBeforeStart();
    void recordSignedRejectsDuplicate();
    void recordSignedRejectsUnknownName();
    void recordRejectedKeepsReason();
    void recordRejectedRequiresReason();
    void skipSignatoryMarksSkipped();
    void completeWorkflowListsPendingNames();
    void completeWorkflowSucceedsWhenAllHandled();
    void completedWorkflowRejectsFurtherChanges();
    void abortWorkflowLocksFlow();
    void abortWorkflowRequiresReason();
    void reportCountsEachOutcome();
    void reportTextContainsTitleAndNames();
    void eventsKeepInsertionOrder();
    void nextPendingSignatoryFollowsOrder();
};

void SignatureWorkflowTests::createWorkflowStartsInDraft() {
    SignatureWorkflow workflow;
    const QString id = workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));

    QVERIFY(!id.isEmpty());
    QVERIFY(id.startsWith(QStringLiteral("WF-")));
    QCOMPARE(workflow.state(), WorkflowState::Draft);
    QCOMPARE(workflow.title(), QStringLiteral("合同会签"));
    QCOMPARE(workflow.initiator(), QStringLiteral("王发起"));

    // 建流程本身就要留一条记录，否则流水里看不出流程是谁建的。
    QCOMPARE(workflow.events().size(), 1);
    QCOMPARE(workflow.events().first().action, QStringLiteral("created"));
}

void SignatureWorkflowTests::createWorkflowRejectsEmptyTitle() {
    SignatureWorkflow workflow;
    QString error;
    const QString id = workflow.createWorkflow(QStringLiteral("   "), QStringLiteral("王发起"), &error);

    QVERIFY(id.isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.state(), WorkflowState::Draft);
    QCOMPARE(workflow.events().size(), 0);
}

void SignatureWorkflowTests::createWorkflowRejectsEmptyInitiator() {
    SignatureWorkflow workflow;
    QString error;
    const QString id = workflow.createWorkflow(QStringLiteral("合同会签"), QString(), &error);

    QVERIFY(id.isEmpty());
    QVERIFY(!error.isEmpty());
}

void SignatureWorkflowTests::createWorkflowRejectsSecondCall() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));
    QString error;
    const QString second = workflow.createWorkflow(QStringLiteral("另一个流程"), QStringLiteral("王发起"), &error);

    QVERIFY(second.isEmpty());
    QVERIFY(!error.isEmpty());
    // 第二次创建不能把原来的流水冲掉。
    QCOMPARE(workflow.title(), QStringLiteral("合同会签"));
}

void SignatureWorkflowTests::addSignatoryAppendsEvent() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));
    QString error;
    workflow.addSignatory(makeSignatory(QStringLiteral("张三"), 1), &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(workflow.signatories().size(), 1);
    QCOMPARE(workflow.signatories().first().name, QStringLiteral("张三"));
    QCOMPARE(workflow.signatories().first().order, 1);

    bool sawEvent = false;
    for (const WorkflowEvent& event : workflow.events()) {
        if (event.action == QStringLiteral("signatory_added")) {
            sawEvent = true;
            QVERIFY(event.detail.contains(QStringLiteral("张三")));
        }
    }
    QVERIFY(sawEvent);
}

void SignatureWorkflowTests::addSignatoryRejectsEmptyName() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));
    QString error;
    workflow.addSignatory(makeSignatory(QStringLiteral("  "), 1), &error);

    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.signatories().size(), 0);
}

void SignatureWorkflowTests::addSignatoryRejectsDuplicateName() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));
    workflow.addSignatory(makeSignatory(QStringLiteral("张三"), 1));
    QString error;
    workflow.addSignatory(makeSignatory(QStringLiteral("张三"), 2), &error);

    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.signatories().size(), 1);
}

void SignatureWorkflowTests::addSignatoryRejectsDuplicateOrder() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));
    workflow.addSignatory(makeSignatory(QStringLiteral("张三"), 1));
    QString error;
    workflow.addSignatory(makeSignatory(QStringLiteral("李四"), 1), &error);

    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.signatories().size(), 1);
}

void SignatureWorkflowTests::addSignatoryAutoAssignsOrderWhenUnspecified() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));
    workflow.addSignatory(makeSignatory(QStringLiteral("张三"), 1));
    workflow.addSignatory(makeSignatory(QStringLiteral("李四"), 0));

    QCOMPARE(workflow.signatories().size(), 2);
    QCOMPARE(workflow.signatories().at(1).name, QStringLiteral("李四"));
    QCOMPARE(workflow.signatories().at(1).order, 2);
}

void SignatureWorkflowTests::addSignatoryIgnoresIncomingStatus() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));

    Signatory s = makeSignatory(QStringLiteral("张三"), 1);
    s.status = SignStepStatus::Signed;
    s.signedAt = QDateTime::currentDateTime();
    workflow.addSignatory(s);

    // 外部传进来的状态一律不认：否则调用方能凭一个 struct 把流程直接推到「已签」。
    QCOMPARE(workflow.signatories().first().status, SignStepStatus::Pending);
    QVERIFY(!workflow.signatories().first().signedAt.isValid());
}

void SignatureWorkflowTests::addSignatoryRejectedAfterStart() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    QString error;
    workflow.addSignatory(makeSignatory(QStringLiteral("李四"), 2), &error);

    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.signatories().size(), 1);
}

void SignatureWorkflowTests::startSigningRequiresSignatories() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));
    QString error;
    const bool ok = workflow.startSigning(&error);

    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.state(), WorkflowState::Draft);
}

void SignatureWorkflowTests::startSigningRejectsSecondCall() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    QString error;
    const bool ok = workflow.startSigning(&error);

    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.state(), WorkflowState::InProgress);
}

void SignatureWorkflowTests::recordSignedMarksSignatory() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    const QDateTime when(QDate(2026, 3, 1), QTime(10, 30));
    QString error;
    const bool ok = workflow.recordSigned(QStringLiteral("张三"), when, &error);

    QVERIFY2(ok, qPrintable(error));
    QCOMPARE(workflow.signatories().first().status, SignStepStatus::Signed);
    QCOMPARE(workflow.signatories().first().signedAt, when);
}

void SignatureWorkflowTests::recordSignedRejectsBeforeStart() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("合同会签"), QStringLiteral("王发起"));
    workflow.addSignatory(makeSignatory(QStringLiteral("张三"), 1));

    QString error;
    const bool ok = workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime(), &error);

    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.signatories().first().status, SignStepStatus::Pending);
}

void SignatureWorkflowTests::recordSignedRejectsDuplicate() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());

    QString error;
    const bool ok = workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime(), &error);

    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    // 重复签署不能改写第一次的时间。
    const QDateTime first = workflow.signatories().first().signedAt;
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());
    QCOMPARE(workflow.signatories().first().signedAt, first);
}

void SignatureWorkflowTests::recordSignedRejectsUnknownName() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    QString error;
    const bool ok = workflow.recordSigned(QStringLiteral("查无此人"), QDateTime::currentDateTime(), &error);

    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("查无此人")));
}

void SignatureWorkflowTests::recordRejectedKeepsReason() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    QString error;
    const bool ok = workflow.recordRejected(QStringLiteral("张三"), QStringLiteral("金额不对"),
                                            QDateTime::currentDateTime(), &error);

    QVERIFY2(ok, qPrintable(error));
    QCOMPARE(workflow.signatories().first().status, SignStepStatus::Rejected);
    QCOMPARE(workflow.signatories().first().rejectReason, QStringLiteral("金额不对"));
    QVERIFY(!workflow.signatories().first().signedAt.isValid());
}

void SignatureWorkflowTests::recordRejectedRequiresReason() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    QString error;
    const bool ok = workflow.recordRejected(QStringLiteral("张三"), QStringLiteral("  "),
                                            QDateTime::currentDateTime(), &error);

    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.signatories().first().status, SignStepStatus::Pending);
}

void SignatureWorkflowTests::skipSignatoryMarksSkipped() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    QString error;
    const bool ok = workflow.skipSignatory(QStringLiteral("张三"), &error);

    QVERIFY2(ok, qPrintable(error));
    QCOMPARE(workflow.signatories().first().status, SignStepStatus::Skipped);
}

void SignatureWorkflowTests::completeWorkflowListsPendingNames() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三"), QStringLiteral("李四")});
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());

    QString error;
    const bool ok = workflow.completeWorkflow(&error);

    QVERIFY(!ok);
    // 错误里必须点名还差谁，否则调用方只能自己再遍历一遍名单。
    QVERIFY(error.contains(QStringLiteral("李四")));
    QVERIFY(!error.contains(QStringLiteral("张三")));
    QCOMPARE(workflow.state(), WorkflowState::InProgress);
}

void SignatureWorkflowTests::completeWorkflowSucceedsWhenAllHandled() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三"), QStringLiteral("李四")});
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());
    workflow.skipSignatory(QStringLiteral("李四"));

    QString error;
    const bool ok = workflow.completeWorkflow(&error);

    QVERIFY2(ok, qPrintable(error));
    QCOMPARE(workflow.state(), WorkflowState::Completed);
}

void SignatureWorkflowTests::completedWorkflowRejectsFurtherChanges() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());
    workflow.completeWorkflow();

    QString error;
    const bool ok = workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime(), &error);

    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.state(), WorkflowState::Completed);
}

void SignatureWorkflowTests::abortWorkflowLocksFlow() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    QString error;
    const bool ok = workflow.abortWorkflow(QStringLiteral("项目取消"), &error);

    QVERIFY2(ok, qPrintable(error));
    QCOMPARE(workflow.state(), WorkflowState::Aborted);

    // 终态之后任何修改都要被拒绝：不然流水就不再可信。
    const bool signedOk = workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime(), &error);
    QVERIFY(!signedOk);
    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.signatories().first().status, SignStepStatus::Pending);
}

void SignatureWorkflowTests::abortWorkflowRequiresReason() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    QString error;
    const bool ok = workflow.abortWorkflow(QStringLiteral("   "), &error);

    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.state(), WorkflowState::InProgress);
}

void SignatureWorkflowTests::reportCountsEachOutcome() {
    SignatureWorkflow workflow = makeStartedWorkflow(
        {QStringLiteral("张三"), QStringLiteral("李四"), QStringLiteral("王五"), QStringLiteral("赵六")});
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());
    workflow.recordRejected(QStringLiteral("李四"), QStringLiteral("不同意"),
                            QDateTime::currentDateTime());
    workflow.skipSignatory(QStringLiteral("王五"));

    const SignatureWorkflow::Report report = workflow.buildReport();
    QCOMPARE(report.signedCount, 1);
    QCOMPARE(report.rejectedCount, 1);
    QCOMPARE(report.skippedCount, 1);
    QCOMPARE(report.pendingCount, 1);
    QCOMPARE(report.signatories.size(), 4);
    QCOMPARE(report.state, WorkflowState::InProgress);
    QCOMPARE(report.title, QStringLiteral("采购合同会签"));
    QVERIFY(!report.id.isEmpty());
}

void SignatureWorkflowTests::reportTextContainsTitleAndNames() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三"), QStringLiteral("李四")});
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());

    const QString text = workflow.buildReport().toPlainText();
    QVERIFY(text.contains(QStringLiteral("采购合同会签")));
    QVERIFY(text.contains(QStringLiteral("张三")));
    QVERIFY(text.contains(QStringLiteral("李四")));
    QVERIFY(text.contains(QStringLiteral("已签署")));
    // 报告里要能看出流程处在哪一步。
    QVERIFY(text.contains(QStringLiteral("进行中")));
}

void SignatureWorkflowTests::eventsKeepInsertionOrder() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());
    workflow.completeWorkflow();

    QStringList actions;
    for (const WorkflowEvent& event : workflow.events()) {
        actions << event.action;
    }
    // 顺序必须是 created → signatory_added → started → signed → completed。
    QCOMPARE(actions, QStringList({QStringLiteral("created"), QStringLiteral("signatory_added"),
                                   QStringLiteral("started"), QStringLiteral("signed"),
                                   QStringLiteral("completed")}));
    // 每条事件都要有时间戳，否则事后追责时对不上时间线。
    for (const WorkflowEvent& event : workflow.events()) {
        QVERIFY(event.at.isValid());
    }
}

void SignatureWorkflowTests::nextPendingSignatoryFollowsOrder() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三"), QStringLiteral("李四")});

    QCOMPARE(workflow.nextPendingSignatory(), QStringLiteral("张三"));
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());
    QCOMPARE(workflow.nextPendingSignatory(), QStringLiteral("李四"));
    workflow.recordSigned(QStringLiteral("李四"), QDateTime::currentDateTime());
    QVERIFY(workflow.nextPendingSignatory().isEmpty());
}


void SignatureWorkflowTests::removeSignatoryRemovesPendingSignatory() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("采购合同会签"), QStringLiteral("王发起"));
    workflow.addSignatory(makeSignatory(QStringLiteral("张三"), 1));
    workflow.addSignatory(makeSignatory(QStringLiteral("李四"), 2));

    QString error;
    QVERIFY2(workflow.removeSignatory(QStringLiteral("张三"), &error), qPrintable(error));
    QCOMPARE(workflow.signatories().size(), 1);
    QCOMPARE(workflow.signatories().first().name, QStringLiteral("李四"));
    // 报告的「签署人」名单里不能再有他（workflow.signatories() 已经是唯一真相），
    // 但「签署记录」里必须留着这次移除：审计流水只增不改，
    // 事后要能看出「名单上曾经有谁、什么时候被移走的」。
    QVERIFY(workflow.buildReport().toPlainText().contains(QStringLiteral("移除签署人：张三")));
}

void SignatureWorkflowTests::removeSignatoryRejectsUnknownName() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("采购合同会签"), QStringLiteral("王发起"));
    workflow.addSignatory(makeSignatory(QStringLiteral("张三"), 1));

    QString error;
    QVERIFY(!workflow.removeSignatory(QStringLiteral("不存在"), &error));
    QVERIFY(error.contains(QStringLiteral("找不到签署人")));
    // 删除失败不能动名单。
    QCOMPARE(workflow.signatories().size(), 1);
}

void SignatureWorkflowTests::removeSignatoryRejectsAfterSigningStarted() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三"), QStringLiteral("李四")});

    QString error;
    QVERIFY(!workflow.removeSignatory(QStringLiteral("张三"), &error));
    QVERIFY(error.contains(QStringLiteral("不能再删除签署人")));
    QCOMPARE(workflow.signatories().size(), 2);
}

void SignatureWorkflowTests::removeSignatoryRejectsOnCompletedWorkflow() {
    SignatureWorkflow workflow = makeStartedWorkflow({QStringLiteral("张三")});
    workflow.recordSigned(QStringLiteral("张三"), QDateTime::currentDateTime());
    QVERIFY(workflow.completeWorkflow());

    QString error;
    QVERIFY(!workflow.removeSignatory(QStringLiteral("张三"), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(workflow.signatories().size(), 1);
}

void SignatureWorkflowTests::removeSignatoryRecordsEvent() {
    SignatureWorkflow workflow;
    workflow.createWorkflow(QStringLiteral("采购合同会签"), QStringLiteral("王发起"));
    workflow.addSignatory(makeSignatory(QStringLiteral("张三"), 1));

    QVERIFY(workflow.removeSignatory(QStringLiteral("张三")));
    QStringList actions;
    for (const WorkflowEvent& event : workflow.events()) {
        actions << event.action;
    }
    QCOMPARE(actions, QStringList({QStringLiteral("created"), QStringLiteral("signatory_added"),
                                   QStringLiteral("signatory_removed")}));
}

QTEST_GUILESS_MAIN(SignatureWorkflowTests)

#include "SignatureWorkflowTests.moc"
