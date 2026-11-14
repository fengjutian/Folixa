// v0.14.0：工具注册表与提案校验的单元测试。
//
// 守的是「AI 无法绕过确认执行变更」这条底线：
// 未知工具、被禁止的工具、只读工具混进提案、参数越界、文档已改，
// 都必须在**执行之前**被判失败。
#include "tools/ProposalValidator.h"
#include "tools/ToolRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace aipdf::tools;

class ToolRegistryTests : public QObject {
    Q_OBJECT

private slots:
    void registryExposesBuiltinTools();
    void forbiddenToolsAreRejectedByName();
    void readonlyToolInProposalIsRejected();
    void unknownToolIsRejected();
    void validProposalPasses();
    void missingRequiredArgumentIsRejected();
    void wrongArgumentTypeIsRejected();
    void unknownArgumentIsRejected();
    void pageOutOfRangeIsRejected();
    void tooManyOperationsAreRejected();
    void revisionMismatchIsRejected();
    void emptyProposalIsRejected();
};

void ToolRegistryTests::registryExposesBuiltinTools() {
    const ToolRegistry& registry = ToolRegistry::instance();
    QVERIFY(registry.find(QStringLiteral("doc.delete_pages")) != nullptr);
    QVERIFY(registry.find(QStringLiteral("doc.list_blank_pages")) != nullptr);
    QCOMPARE(registry.find(QStringLiteral("doc.delete_pages"))->permission,
             ToolPermission::ProposeOnly);
    QCOMPARE(registry.find(QStringLiteral("doc.list_blank_pages"))->permission,
             ToolPermission::ReadOnly);
}

void ToolRegistryTests::forbiddenToolsAreRejectedByName() {
    QVERIFY(ToolRegistry::isForbidden(QStringLiteral("shell.exec")));
    QVERIFY(ToolRegistry::isForbidden(QStringLiteral("file.write")));
    QVERIFY(!ToolRegistry::isForbidden(QStringLiteral("doc.delete_pages")));

    QJsonArray operations;
    operations.append(QJsonObject{{QStringLiteral("tool"), QStringLiteral("shell.exec")},
                                  {QStringLiteral("args"), QJsonObject{}}});
    const Proposal proposal = validateProposal(operations, DocumentSnapshot{5, 1});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("禁止")));
}

void ToolRegistryTests::readonlyToolInProposalIsRejected() {
    // 只读工具可以单独调用，但不能混进「要执行的变更」里。
    QJsonArray operations;
    operations.append(QJsonObject{{QStringLiteral("tool"), QStringLiteral("doc.list_blank_pages")},
                                  {QStringLiteral("args"), QJsonObject{}}});
    const Proposal proposal = validateProposal(operations, DocumentSnapshot{5, 1});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("只读")));
}

void ToolRegistryTests::unknownToolIsRejected() {
    QJsonArray operations;
    operations.append(QJsonObject{{QStringLiteral("tool"), QStringLiteral("doc.do_magic")},
                                  {QStringLiteral("args"), QJsonObject{}}});
    const Proposal proposal = validateProposal(operations, DocumentSnapshot{5, 1});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("未知工具")));
}

void ToolRegistryTests::validProposalPasses() {
    QJsonArray operations;
    operations.append(QJsonObject{
        {QStringLiteral("tool"), QStringLiteral("doc.delete_pages")},
        {QStringLiteral("args"), QJsonObject{{QStringLiteral("pages"), QJsonArray{2, 5}}}},
        {QStringLiteral("docRevision"), 7}});
    operations.append(QJsonObject{
        {QStringLiteral("tool"), QStringLiteral("annotation.add_note")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("page"), 3},
                     {QStringLiteral("rect"), QJsonArray{10.0, 20.0, 100.0, 12.0}},
                     {QStringLiteral("text"), QStringLiteral("这里需要复核")}}}});

    const Proposal proposal = validateProposal(operations, DocumentSnapshot{10, 7});
    QVERIFY(proposal.ok());
    QCOMPARE(proposal.operations.size(), 2);
    QCOMPARE(proposal.operations.at(0).summary, QStringLiteral("删除第 2、5 页"));
    QVERIFY(proposal.operations.at(1).summary.contains(QStringLiteral("这里需要复核")));
}

void ToolRegistryTests::missingRequiredArgumentIsRejected() {
    QJsonArray operations;
    operations.append(QJsonObject{{QStringLiteral("tool"), QStringLiteral("doc.delete_pages")},
                                  {QStringLiteral("args"), QJsonObject{}}});
    const Proposal proposal = validateProposal(operations, DocumentSnapshot{5, 1});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("缺少必填参数")));
}

void ToolRegistryTests::wrongArgumentTypeIsRejected() {
    QJsonArray operations;
    operations.append(QJsonObject{
        {QStringLiteral("tool"), QStringLiteral("doc.delete_pages")},
        {QStringLiteral("args"), QJsonObject{{QStringLiteral("pages"), QStringLiteral("2,5")}}}});
    const Proposal proposal = validateProposal(operations, DocumentSnapshot{5, 1});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("类型")));
}

void ToolRegistryTests::unknownArgumentIsRejected() {
    // 多余参数说明模型在猜 schema，宁可直接拒掉，也不要静默忽略。
    QJsonArray operations;
    operations.append(QJsonObject{
        {QStringLiteral("tool"), QStringLiteral("doc.delete_pages")},
        {QStringLiteral("args"),
         QJsonObject{{QStringLiteral("pages"), QJsonArray{1}}, {QStringLiteral("force"), true}}}});
    const Proposal proposal = validateProposal(operations, DocumentSnapshot{5, 1});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("不接受参数")));
}

void ToolRegistryTests::pageOutOfRangeIsRejected() {
    QJsonArray operations;
    operations.append(QJsonObject{
        {QStringLiteral("tool"), QStringLiteral("doc.delete_pages")},
        {QStringLiteral("args"), QJsonObject{{QStringLiteral("pages"), QJsonArray{9}}}}});
    const Proposal proposal = validateProposal(operations, DocumentSnapshot{5, 1});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("超出范围")));
}

void ToolRegistryTests::tooManyOperationsAreRejected() {
    QJsonArray operations;
    for (int i = 0; i < 21; ++i) {
        operations.append(QJsonObject{
            {QStringLiteral("tool"), QStringLiteral("doc.delete_pages")},
            {QStringLiteral("args"), QJsonObject{{QStringLiteral("pages"), QJsonArray{1}}}}});
    }
    const Proposal proposal = validateProposal(operations, DocumentSnapshot{5, 1});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("最多")));
}

void ToolRegistryTests::revisionMismatchIsRejected() {
    QJsonArray operations;
    operations.append(QJsonObject{
        {QStringLiteral("tool"), QStringLiteral("doc.delete_pages")},
        {QStringLiteral("args"), QJsonObject{{QStringLiteral("pages"), QJsonArray{1}}}},
        {QStringLiteral("docRevision"), 3}});
    // 文档已经改到第 4 版：提案基于旧版本，必须重来。
    const Proposal proposal = validateProposal(operations, DocumentSnapshot{5, 4});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("已被修改")));
}

void ToolRegistryTests::emptyProposalIsRejected() {
    const Proposal proposal = validateProposal(QJsonArray{}, DocumentSnapshot{5, 1});
    QVERIFY(!proposal.ok());
    QVERIFY(proposal.issues.first().message.contains(QStringLiteral("没有任何操作")));
}

QTEST_MAIN(ToolRegistryTests)
#include "ToolRegistryTests.moc"
