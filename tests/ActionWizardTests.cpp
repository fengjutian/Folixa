#include "doc/ActionWizard.h"

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

using aipdf::doc::ActionWizard;
using aipdf::doc::WizardActionType;
using aipdf::doc::WizardFailurePolicy;
using aipdf::doc::WizardParam;
using aipdf::doc::WizardRunReport;
using aipdf::doc::WizardStep;
using aipdf::doc::WizardStepResult;
using aipdf::doc::deleteActionWizard;
using aipdf::doc::deserializeActionWizard;
using aipdf::doc::findActionWizardByName;
using aipdf::doc::listActionWizards;
using aipdf::doc::loadActionWizard;
using aipdf::doc::makeWizardStep;
using aipdf::doc::runActionWizard;
using aipdf::doc::saveActionWizard;
using aipdf::doc::serializeActionWizard;
using aipdf::doc::validateActionWizard;
using aipdf::doc::wizardActionTypeFromName;
using aipdf::doc::wizardActionTypeName;
using aipdf::doc::wizardBooleanParam;
using aipdf::doc::wizardIntegerParam;
using aipdf::doc::wizardNumberParam;
using aipdf::doc::wizardStringListParam;
using aipdf::doc::wizardStringParam;

namespace {

// 一份最小的合法向导：打开文件 → 结束。
ActionWizard makeWizard(const QString& name = QStringLiteral("测试向导")) {
    ActionWizard wizard;
    wizard.name = name;
    wizard.steps.append(makeWizardStep(WizardActionType::OpenFile,
                                       {wizardStringParam(QStringLiteral("path"),
                                                         QStringLiteral("D:/tmp/a.pdf"))}));
    wizard.steps.append(makeWizardStep(WizardActionType::End));
    return wizard;
}

// 执行回调的记录器：记下每一步的动作名，并按预设让某一步失败。
struct ExecLog {
    QStringList executed;
    QString failingAction;
    QString failureReason = QStringLiteral("模拟失败");
};

}  // namespace

class ActionWizardTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void minimalWizardIsValid();
    void emptyNameIsRejected();
    void emptyStepListIsRejected();
    void unknownActionTypeIsRejected();
    void missingRequiredParameterIsRejected();
    void wrongParameterTypeIsRejected();
    void unknownParameterIsRejected();
    void illegalParameterValueIsRejected();
    void parameterOutOfRangeIsRejected();
    void actionTypeNamesRoundTrip();
    void jsonRoundTripKeepsEverything();
    void deserializeRejectsBrokenJson();
    void deserializeRejectsWrongParamValueType();
    void savedWizardReadsBackIdentical();
    void listReturnsSavedWizards();
    void deleteRemovesWizard();
    void deletingUnknownIdReturnsFalse();
    void differentSettingsKeysAreIsolated();
    void duplicateNamesAreBothKept();
    void findByNameReturnsWizard();
    void findByNameFailsWhenMissing();
    void savingInvalidWizardFails();
    void loadingUnknownWizardFails();
    void runCallsEveryStepInOrder();
    void runRecordsFailedStepHonestly();
    void runStopsOnFirstFailure();
    void runContinuesOnFailure();
    void runRejectsInvalidWizardWithoutRunning();
    void runEndActionStopsFollowingSteps();
    void runWithoutExecutorFails();
    void summaryDescribesEveryStep();

private:
    // 设置存储的临时根目录：向导必须存在用户的真实设置之外。
    QTemporaryDir* m_storageRoot = nullptr;
};

void ActionWizardTests::initTestCase() {
    // 把设置存储整个指到临时目录：向导必须存在**用户的真实设置**之外，
    // 否则跑一次单元测试就往用户机器里塞一份向导。
    m_storageRoot = new QTemporaryDir();
    QVERIFY(m_storageRoot->isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_storageRoot->path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_storageRoot->path());
}

void ActionWizardTests::cleanupTestCase() {
    delete m_storageRoot;
    m_storageRoot = nullptr;
}

void ActionWizardTests::minimalWizardIsValid() {
    QString error;
    const ActionWizard wizard = makeWizard();
    QVERIFY2(validateActionWizard(wizard, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
}

void ActionWizardTests::emptyNameIsRejected() {
    ActionWizard wizard = makeWizard();
    wizard.name = QStringLiteral("   ");
    QString error;
    QVERIFY(!validateActionWizard(wizard, &error));
    QVERIFY(error.contains(QStringLiteral("名称")));
}

void ActionWizardTests::emptyStepListIsRejected() {
    ActionWizard wizard = makeWizard();
    wizard.steps.clear();
    QString error;
    QVERIFY(!validateActionWizard(wizard, &error));
    // 空步骤列表必须被明确拒绝，而不是当成「无事可做」。
    QVERIFY(error.contains(QStringLiteral("步骤")));
}

void ActionWizardTests::unknownActionTypeIsRejected() {
    ActionWizard wizard = makeWizard();
    WizardStep broken = makeWizardStep(WizardActionType::OpenFile,
                                       {wizardStringParam(QStringLiteral("path"),
                                                         QStringLiteral("D:/tmp/a.pdf"))});
    broken.type = WizardActionType::Unknown;
    broken.typeName = QStringLiteral("compressArchive");
    wizard.steps.append(broken);

    QString error;
    QVERIFY(!validateActionWizard(wizard, &error));
    // 报错必须写明是哪种动作不对。
    QVERIFY(error.contains(QStringLiteral("compressArchive")));
    QVERIFY(error.contains(QStringLiteral("不受支持")));
}

void ActionWizardTests::missingRequiredParameterIsRejected() {
    ActionWizard wizard = makeWizard();
    // 打开文件少了必填参数 path。
    wizard.steps[0] = makeWizardStep(WizardActionType::OpenFile);

    QString error;
    QVERIFY(!validateActionWizard(wizard, &error));
    // 报错必须写明缺哪个参数。
    QVERIFY(error.contains(QStringLiteral("path")));
    QVERIFY(error.contains(QStringLiteral("缺少必填参数")));
}

void ActionWizardTests::wrongParameterTypeIsRejected() {
    ActionWizard wizard = makeWizard();
    // path 应该是字符串，写成了整数。
    wizard.steps[0] = makeWizardStep(WizardActionType::OpenFile,
                                      {wizardIntegerParam(QStringLiteral("path"), 42)});

    QString error;
    QVERIFY(!validateActionWizard(wizard, &error));
    QVERIFY(error.contains(QStringLiteral("path")));
    QVERIFY(error.contains(QStringLiteral("类型")));
}

void ActionWizardTests::unknownParameterIsRejected() {
    ActionWizard wizard = makeWizard();
    wizard.steps[0] = makeWizardStep(
        WizardActionType::OpenFile,
        {wizardStringParam(QStringLiteral("path"), QStringLiteral("D:/tmp/a.pdf")),
         wizardStringParam(QStringLiteral("filePath"), QStringLiteral("D:/tmp/b.pdf"))});

    QString error;
    QVERIFY(!validateActionWizard(wizard, &error));
    QVERIFY(error.contains(QStringLiteral("filePath")));
}

void ActionWizardTests::illegalParameterValueIsRejected() {
    ActionWizard wizard = makeWizard();
    wizard.steps.append(makeWizardStep(WizardActionType::AddPageNumbers,
                                       {wizardStringParam(QStringLiteral("position"),
                                                         QStringLiteral("middle"))}));

    QString error;
    QVERIFY(!validateActionWizard(wizard, &error));
    QVERIFY(error.contains(QStringLiteral("position")));
}

void ActionWizardTests::parameterOutOfRangeIsRejected() {
    ActionWizard wizard = makeWizard();
    // 打印份数超出 1..99。
    wizard.steps.append(makeWizardStep(WizardActionType::Print,
                                       {wizardIntegerParam(QStringLiteral("copies"), 500)}));

    QString error;
    QVERIFY(!validateActionWizard(wizard, &error));
    QVERIFY(error.contains(QStringLiteral("copies")));
}

void ActionWizardTests::actionTypeNamesRoundTrip() {
    const WizardActionType types[] = {WizardActionType::OpenFile,
                                      WizardActionType::MergeDocuments,
                                      WizardActionType::SplitDocument,
                                      WizardActionType::AddBookmark,
                                      WizardActionType::AddPageNumbers,
                                      WizardActionType::AddWatermark,
                                      WizardActionType::Encrypt,
                                      WizardActionType::ConvertToFormat,
                                      WizardActionType::Print,
                                      WizardActionType::End};
    for (WizardActionType type : types) {
        const QString name = wizardActionTypeName(type);
        QVERIFY(!name.isEmpty());
        bool ok = false;
        const WizardActionType parsed = wizardActionTypeFromName(name, &ok);
        QVERIFY(ok);
        QCOMPARE(static_cast<int>(parsed), static_cast<int>(type));
    }

    bool ok = true;
    QCOMPARE(static_cast<int>(wizardActionTypeFromName(QStringLiteral("nope"), &ok)),
             static_cast<int>(WizardActionType::Unknown));
    QVERIFY(!ok);
}

void ActionWizardTests::jsonRoundTripKeepsEverything() {
    ActionWizard wizard = makeWizard(QStringLiteral("带全部动作的向导"));
    wizard.description = QStringLiteral("一条完整的编排");
    wizard.steps.append(makeWizardStep(WizardActionType::MergeDocuments,
                                       {wizardStringListParam(QStringLiteral("inputPaths"),
                                                             {QStringLiteral("a.pdf"),
                                                              QStringLiteral("b.pdf")}),
                                        wizardStringParam(QStringLiteral("outputPath"),
                                                          QStringLiteral("D:/tmp/out.pdf"))}));
    wizard.steps.append(makeWizardStep(WizardActionType::AddWatermark,
                                       {wizardStringParam(QStringLiteral("text"),
                                                         QStringLiteral("内部资料")),
                                        wizardNumberParam(QStringLiteral("opacity"), 0.25)}));
    wizard.steps.append(makeWizardStep(WizardActionType::Encrypt,
                                       {wizardStringParam(QStringLiteral("userPassword"),
                                                         QStringLiteral("u")),
                                        wizardStringParam(QStringLiteral("ownerPassword"),
                                                         QStringLiteral("o")),
                                        wizardBooleanParam(QStringLiteral("allowPrinting"), false)}));

    QString error;
    const QByteArray json = serializeActionWizard(wizard, &error);
    QVERIFY2(!json.isEmpty(), qPrintable(error));

    ActionWizard loaded;
    QVERIFY2(deserializeActionWizard(json, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.name, wizard.name);
    QCOMPARE(loaded.description, wizard.description);
    QCOMPARE(loaded.steps.size(), wizard.steps.size());
    for (int i = 0; i < wizard.steps.size(); ++i) {
        QCOMPARE(loaded.steps.at(i).type, wizard.steps.at(i).type);
        QCOMPARE(loaded.steps.at(i).typeName, wizard.steps.at(i).typeName);
        QCOMPARE(loaded.steps.at(i).params.size(), wizard.steps.at(i).params.size());
    }
    // 各类型的参数值都要原样回来（步骤顺序：打开文件 → 结束 → 合并 → 水印 → 加密）。
    QVERIFY(loaded.steps.at(2).params.at(0).listValue == wizard.steps.at(2).params.at(0).listValue);
    QCOMPARE(loaded.steps.at(3).params.at(1).numberValue, 0.25);
    QCOMPARE(loaded.steps.at(4).params.at(2).booleanValue, false);
}

void ActionWizardTests::deserializeRejectsBrokenJson() {
    ActionWizard loaded;
    QString error;
    QVERIFY(!deserializeActionWizard(QByteArray("{ 这不是 JSON"), &loaded, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!deserializeActionWizard(QByteArray(), &loaded, &error));
    QVERIFY(!error.isEmpty());
}

void ActionWizardTests::deserializeRejectsWrongParamValueType() {
    const QByteArray json = QByteArray(
        "{\"name\":\"x\",\"steps\":[{\"type\":\"openFile\",\"params\":"
        "[{\"name\":\"path\",\"type\":\"string\",\"value\":42}]}]}");
    ActionWizard loaded;
    QString error;
    QVERIFY(!deserializeActionWizard(json, &loaded, &error));
    QVERIFY(error.contains(QStringLiteral("path")));
}

void ActionWizardTests::savedWizardReadsBackIdentical() {
    const QString key = QStringLiteral("tests/readBack");
    ActionWizard wizard = makeWizard(QStringLiteral("存回来的向导"));
    wizard.description = QStringLiteral("备注");
    wizard.steps.append(makeWizardStep(WizardActionType::AddBookmark,
                                       {wizardStringParam(QStringLiteral("title"),
                                                         QStringLiteral("第一章")),
                                        wizardIntegerParam(QStringLiteral("pageIndex"), 1)}));

    QString error;
    QVERIFY2(saveActionWizard(&wizard, key, &error), qPrintable(error));
    QVERIFY(!wizard.id.isEmpty());

    ActionWizard loaded;
    QVERIFY2(loadActionWizard(wizard.id, key, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.id, wizard.id);
    QCOMPARE(loaded.name, wizard.name);
    QCOMPARE(loaded.description, wizard.description);
    QCOMPARE(loaded.steps.size(), wizard.steps.size());
    QCOMPARE(loaded.steps.at(2).params.at(1).integerValue, qint64(1));

    deleteActionWizard(wizard.id, key);
}

void ActionWizardTests::listReturnsSavedWizards() {
    const QString key = QStringLiteral("tests/list");
    ActionWizard first = makeWizard(QStringLiteral("向导一"));
    ActionWizard second = makeWizard(QStringLiteral("向导二"));
    QString error;
    QVERIFY2(saveActionWizard(&first, key, &error), qPrintable(error));
    QVERIFY2(saveActionWizard(&second, key, &error), qPrintable(error));

    const QVector<ActionWizard> wizards = listActionWizards(key);
    QCOMPARE(wizards.size(), 2);

    QStringList names;
    for (const ActionWizard& wizard : wizards) names.append(wizard.name);
    QVERIFY(names.contains(QStringLiteral("向导一")));
    QVERIFY(names.contains(QStringLiteral("向导二")));

    deleteActionWizard(first.id, key);
    deleteActionWizard(second.id, key);
}

void ActionWizardTests::deleteRemovesWizard() {
    const QString key = QStringLiteral("tests/delete");
    ActionWizard wizard = makeWizard(QStringLiteral("待删除的向导"));
    QString error;
    QVERIFY2(saveActionWizard(&wizard, key, &error), qPrintable(error));

    QVERIFY(deleteActionWizard(wizard.id, key));
    QCOMPARE(listActionWizards(key).size(), 0);
}

void ActionWizardTests::deletingUnknownIdReturnsFalse() {
    QVERIFY(!deleteActionWizard(QStringLiteral("never-existed"), QStringLiteral("tests/unknown")));
    QVERIFY(!deleteActionWizard(QString(), QStringLiteral("tests/unknown")));
}

void ActionWizardTests::differentSettingsKeysAreIsolated() {
    ActionWizard a = makeWizard(QStringLiteral("甲组向导"));
    ActionWizard b = makeWizard(QStringLiteral("乙组向导"));
    QString error;
    QVERIFY2(saveActionWizard(&a, QStringLiteral("tests/groupA"), &error), qPrintable(error));
    QVERIFY2(saveActionWizard(&b, QStringLiteral("tests/groupB"), &error), qPrintable(error));

    QCOMPARE(listActionWizards(QStringLiteral("tests/groupA")).size(), 1);
    QCOMPARE(listActionWizards(QStringLiteral("tests/groupB")).size(), 1);
    QCOMPARE(listActionWizards(QStringLiteral("tests/groupA")).first().name,
             QStringLiteral("甲组向导"));
    QCOMPARE(listActionWizards(QStringLiteral("tests/groupB")).first().name,
             QStringLiteral("乙组向导"));

    deleteActionWizard(a.id, QStringLiteral("tests/groupA"));
    deleteActionWizard(b.id, QStringLiteral("tests/groupB"));
}

void ActionWizardTests::duplicateNamesAreBothKept() {
    const QString key = QStringLiteral("tests/duplicate");
    ActionWizard first = makeWizard(QStringLiteral("重名的向导"));
    ActionWizard second = makeWizard(QStringLiteral("重名的向导"));
    QString error;
    QVERIFY2(saveActionWizard(&first, key, &error), qPrintable(error));
    QVERIFY2(saveActionWizard(&second, key, &error), qPrintable(error));

    // 同名策略：两份都留着（不覆盖、不报错），靠 id 区分。
    const QVector<ActionWizard> wizards = listActionWizards(key);
    QCOMPARE(wizards.size(), 2);
    QVERIFY(wizards.at(0).id != wizards.at(1).id);

    deleteActionWizard(first.id, key);
    deleteActionWizard(second.id, key);
}

void ActionWizardTests::findByNameReturnsWizard() {
    const QString key = QStringLiteral("tests/find");
    ActionWizard wizard = makeWizard(QStringLiteral("找得到的向导"));
    QString error;
    QVERIFY2(saveActionWizard(&wizard, key, &error), qPrintable(error));

    ActionWizard found;
    QVERIFY(findActionWizardByName(key, QStringLiteral("找得到的向导"), &found));
    QCOMPARE(found.id, wizard.id);

    deleteActionWizard(wizard.id, key);
}

void ActionWizardTests::findByNameFailsWhenMissing() {
    ActionWizard found;
    QVERIFY(!findActionWizardByName(QStringLiteral("tests/find"),
                                     QStringLiteral("根本没有这个向导"), &found));
    QVERIFY(!findActionWizardByName(QStringLiteral("tests/find"), QString(), &found));
}

void ActionWizardTests::savingInvalidWizardFails() {
    ActionWizard wizard = makeWizard();
    wizard.steps.clear();
    QString error;
    QVERIFY(!saveActionWizard(&wizard, QStringLiteral("tests/invalid"), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(listActionWizards(QStringLiteral("tests/invalid")).size(), 0);
}

void ActionWizardTests::loadingUnknownWizardFails() {
    ActionWizard loaded;
    QString error;
    QVERIFY(!loadActionWizard(QStringLiteral("no-such-wizard"), QStringLiteral("tests/unknown"),
                               &loaded, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!loadActionWizard(QString(), QStringLiteral("tests/unknown"), &loaded, &error));
    QVERIFY(!error.isEmpty());
}

void ActionWizardTests::runCallsEveryStepInOrder() {
    const ActionWizard wizard = makeWizard(QStringLiteral("按顺序跑"));
    ExecLog log;
    QString error;
    const WizardRunReport report = runActionWizard(
        wizard,
        [&log](const WizardStep& step, QString* reason) -> bool {
            Q_UNUSED(reason);
            log.executed.append(step.typeName);
            return true;
        },
        WizardFailurePolicy::StopOnFirstFailure, &error);

    QVERIFY(report.ok);
    QCOMPARE(report.totalSteps, 2);
    QCOMPARE(report.executedSteps, 2);
    QCOMPARE(report.succeededSteps, 2);
    QCOMPARE(report.failedSteps, 0);
    QCOMPARE(report.skippedSteps, 0);
    QCOMPARE(log.executed.size(), 2);
    QCOMPARE(log.executed.at(0), QStringLiteral("openFile"));
    QCOMPARE(log.executed.at(1), QStringLiteral("end"));
}

void ActionWizardTests::runRecordsFailedStepHonestly() {
    ActionWizard wizard = makeWizard(QStringLiteral("会失败"));
    ExecLog log;
    log.failingAction = QStringLiteral("openFile");
    log.failureReason = QStringLiteral("文件不存在");

    QString error;
    const WizardRunReport report = runActionWizard(
        wizard,
        [&log](const WizardStep& step, QString* reason) -> bool {
            log.executed.append(step.typeName);
            if (step.typeName != log.failingAction) return true;
            if (reason) *reason = log.failureReason;
            return false;
        },
        WizardFailurePolicy::ContinueOnFailure, &error);

    // 失败必须如实记成失败，不能假装成功。
    QVERIFY(!report.ok);
    QCOMPARE(report.failedSteps, 1);
    QCOMPARE(report.succeededSteps, 1);
    QCOMPARE(report.results.at(0).executed, true);
    QCOMPARE(report.results.at(0).succeeded, false);
    QCOMPARE(report.results.at(0).reason, QStringLiteral("文件不存在"));
    QVERIFY(report.summary.contains(QStringLiteral("文件不存在")));
}

void ActionWizardTests::runStopsOnFirstFailure() {
    ActionWizard wizard = makeWizard(QStringLiteral("失败就停"));
    ExecLog log;
    log.failingAction = QStringLiteral("openFile");

    const WizardRunReport report = runActionWizard(
        wizard,
        [&log](const WizardStep& step, QString* reason) -> bool {
            Q_UNUSED(reason);
            log.executed.append(step.typeName);
            return step.typeName != log.failingAction;
        },
        WizardFailurePolicy::StopOnFirstFailure, nullptr);

    QVERIFY(!report.ok);
    QCOMPARE(report.executedSteps, 1);
    QCOMPARE(report.failedSteps, 1);
    QCOMPARE(report.skippedSteps, 1);
    // 失败之后的步骤不能再跑。
    QCOMPARE(log.executed.size(), 1);
    QCOMPARE(report.results.at(1).executed, false);
    QCOMPARE(report.results.at(1).succeeded, false);
    QVERIFY(!report.results.at(1).reason.isEmpty());
}

void ActionWizardTests::runContinuesOnFailure() {
    ActionWizard wizard = makeWizard(QStringLiteral("失败也继续"));
    ExecLog log;
    log.failingAction = QStringLiteral("openFile");

    const WizardRunReport report = runActionWizard(
        wizard,
        [&log](const WizardStep& step, QString* reason) -> bool {
            Q_UNUSED(reason);
            log.executed.append(step.typeName);
            return step.typeName != log.failingAction;
        },
        WizardFailurePolicy::ContinueOnFailure, nullptr);

    // 策略是「失败也继续」：后面的步骤必须照跑，但失败的那一步仍然记成失败。
    QCOMPARE(log.executed.size(), 2);
    QCOMPARE(report.failedSteps, 1);
    QCOMPARE(report.succeededSteps, 1);
    QCOMPARE(report.skippedSteps, 0);
    QVERIFY(!report.ok);
}

void ActionWizardTests::runRejectsInvalidWizardWithoutRunning() {
    ActionWizard wizard = makeWizard();
    wizard.steps[0] = makeWizardStep(WizardActionType::OpenFile); // 缺 path

    int calls = 0;
    QString error;
    const WizardRunReport report = runActionWizard(
        wizard,
        [&calls](const WizardStep& step, QString* reason) -> bool {
            Q_UNUSED(step);
            Q_UNUSED(reason);
            ++calls;
            return true;
        },
        WizardFailurePolicy::StopOnFirstFailure, &error);

    QVERIFY(!report.ok);
    QCOMPARE(calls, 0);
    QCOMPARE(report.executedSteps, 0);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("path")));
}

void ActionWizardTests::runEndActionStopsFollowingSteps() {
    ActionWizard wizard = makeWizard(QStringLiteral("结束动作收尾"));
    wizard.steps.append(makeWizardStep(WizardActionType::Print,
                                       {wizardIntegerParam(QStringLiteral("copies"), 1)}));

    ExecLog log;
    const WizardRunReport report = runActionWizard(
        wizard,
        [&log](const WizardStep& step, QString* reason) -> bool {
            Q_UNUSED(reason);
            log.executed.append(step.typeName);
            return true;
        },
        WizardFailurePolicy::StopOnFirstFailure, nullptr);

    // 「结束」之后的步骤不再执行：它是向导自己声明的终点。
    QCOMPARE(log.executed.size(), 2);
    QCOMPARE(report.results.at(2).executed, false);
    QCOMPARE(report.skippedSteps, 1);
    QVERIFY(report.ok);
}

void ActionWizardTests::runWithoutExecutorFails() {
    const ActionWizard wizard = makeWizard();
    QString error;
    const WizardRunReport report =
        runActionWizard(wizard, nullptr, WizardFailurePolicy::StopOnFirstFailure, &error);
    QVERIFY(!report.ok);
    QCOMPARE(report.executedSteps, 0);
    QVERIFY(!error.isEmpty());
}

void ActionWizardTests::summaryDescribesEveryStep() {
    ActionWizard wizard = makeWizard(QStringLiteral("摘要"));
    ExecLog log;
    log.failingAction = QStringLiteral("openFile");

    const WizardRunReport report = runActionWizard(
        wizard,
        [&log](const WizardStep& step, QString* reason) -> bool {
            Q_UNUSED(reason);
            log.executed.append(step.typeName);
            return step.typeName != log.failingAction;
        },
        WizardFailurePolicy::ContinueOnFailure, nullptr);

    QVERIFY(!report.summary.isEmpty());
    QVERIFY(report.summary.contains(QStringLiteral("摘要")));
    QVERIFY(report.summary.contains(QStringLiteral("共 2 步")));
    QVERIFY(report.summary.contains(QStringLiteral("失败 1 步")));
    QVERIFY(report.summary.contains(QStringLiteral("第 1 步")));
    QVERIFY(report.summary.contains(QStringLiteral("第 2 步")));
}

QTEST_GUILESS_MAIN(ActionWizardTests)
#include "ActionWizardTests.moc"
