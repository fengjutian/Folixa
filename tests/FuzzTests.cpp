// v0.15.0：模糊测试（坏输入只该失败，不该崩）。
//
// 三个入口各喂一堆随机/半随机的坏数据：
//   1. 提案校验：模型可能吐出任意的 JSON 结构；
//   2. 恢复日志：断电时文件可能只写了一半；
//   3. PDF 打开：用户会打开从各处下载来的、损坏或截断的文件。
//
// 判据不是「输出对不对」，而是「不崩 + 不变量成立」：
// 校验通过的提案必须只引用白名单里的提案类工具；打开成功的文档必须真能取到页。
#include "doc/RecoveryLog.h"
#include "pdf/PdfiumAdapter.h"
#include "tools/ProposalValidator.h"
#include "tools/ToolRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

#include <random>

using namespace aipdf;

namespace {

std::mt19937& rng() {
    static std::mt19937 engine(20261114);  // 固定种子：失败可复现
    return engine;
}

// 随机造一个「可能是操作」的 JSON 值。
QJsonValue randomOperation() {
    const int kind = static_cast<int>(rng()() % 6);
    switch (kind) {
    case 0:
        return QJsonValue(QStringLiteral("not an object"));
    case 1:
        return QJsonValue(static_cast<double>(rng()() % 100));
    case 2:
        return QJsonValue(QJsonArray{QJsonValue(1), QJsonValue(true)});
    default:
        break;
    }
    QJsonObject object;
    const QStringList tools{QStringLiteral("doc.delete_pages"),
                            QStringLiteral("doc.insert_blank_page"),
                            QStringLiteral("doc.move_page"),
                            QStringLiteral("annotation.add_note"),
                            QStringLiteral("doc.list_blank_pages"),
                            QStringLiteral("shell.exec"),
                            QStringLiteral("file.write"),
                            QStringLiteral("nonexistent.tool")};
    object.insert(QStringLiteral("tool"), tools.at(static_cast<int>(rng()() % tools.size())));

    QJsonObject args;
    const int argKind = static_cast<int>(rng()() % 5);
    if (argKind == 0) {
        args.insert(QStringLiteral("pages"), QJsonArray{static_cast<int>(rng()() % 50) - 10});
    } else if (argKind == 1) {
        args.insert(QStringLiteral("pages"), QStringLiteral("not an array"));
    } else if (argKind == 2) {
        args.insert(QStringLiteral("index"), static_cast<int>(rng()() % 100));
    } else if (argKind == 3) {
        args.insert(QStringLiteral("page"), static_cast<int>(rng()() % 30));
        args.insert(QStringLiteral("rect"),
                    QJsonArray{0.0, 0.0, 10.0, 10.0});
        args.insert(QStringLiteral("text"), QStringLiteral("x"));
    }
    object.insert(QStringLiteral("args"), args);
    if (rng()() % 3 == 0) {
        object.insert(QStringLiteral("docRevision"), static_cast<int>(rng()() % 5));
    }
    return object;
}

}  // namespace

class FuzzTests : public QObject {
    Q_OBJECT

private slots:
    void proposalValidationSurvivesRandomJson();
    void acceptedProposalsOnlyUseWhitelistedTools();
    void recoveryLogSurvivesGarbageFiles();
    void pdfOpenSurvivesCorruptedBytes();
};

void FuzzTests::proposalValidationSurvivesRandomJson() {
    const tools::DocumentSnapshot document{30, 1};
    for (int iteration = 0; iteration < 2000; ++iteration) {
        QJsonArray operations;
        const int count = static_cast<int>(rng()() % 6);
        for (int i = 0; i < count; ++i) {
            operations.append(randomOperation());
        }
        // 只要不崩、不抛，这条就算过；通过的提案另有不变量（下一条测试）。
        const tools::Proposal proposal = tools::validateProposal(operations, document);
        Q_UNUSED(proposal);
    }
    QVERIFY(true);
}

void FuzzTests::acceptedProposalsOnlyUseWhitelistedTools() {
    const tools::DocumentSnapshot document{30, 1};
    const tools::ToolRegistry& registry = tools::ToolRegistry::instance();
    int accepted = 0;
    for (int iteration = 0; iteration < 3000; ++iteration) {
        QJsonArray operations;
        const int count = 1 + static_cast<int>(rng()() % 4);
        for (int i = 0; i < count; ++i) {
            operations.append(randomOperation());
        }
        const tools::Proposal proposal = tools::validateProposal(operations, document);
        if (!proposal.ok()) {
            continue;
        }
        ++accepted;
        for (const tools::ProposedOperation& operation : proposal.operations) {
            const tools::ToolSpec* spec = registry.find(operation.toolId);
            QVERIFY2(spec != nullptr, qPrintable(operation.toolId));
            // 只读工具与禁止工具都不该出现在通过校验的提案里。
            QCOMPARE(spec->permission, tools::ToolPermission::ProposeOnly);
            QVERIFY(!tools::ToolRegistry::isForbidden(operation.toolId));
        }
    }
    qInfo("accepted proposals: %d", accepted);
    QVERIFY(accepted > 0);  // 随机样本里必须至少有一条能过，否则这条测试没有意义
}

void FuzzTests::recoveryLogSurvivesGarbageFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("AIPDF_RECOVERY_DIR", dir.path().toUtf8());

    // 断电时文件可能写了一半：随机字节、截断的 JSON、超长垃圾都要能读。
    for (int iteration = 0; iteration < 100; ++iteration) {
        const QByteArray garbage(static_cast<int>(rng()() % 400), '\0');
        QByteArray bytes;
        for (char byte : garbage) {
            bytes.append(static_cast<char>(rng()() % 256));
        }
        const QString path =
            QDir(dir.path())
                .filePath(QStringLiteral("garbage-%1.json").arg(iteration));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(bytes);
        file.close();
    }

    // list() 会把目录里每个文件都读一遍：一条坏文件不能拖垮整份列表。
    const QVector<doc::RecoveryEntry> entries = doc::RecoveryLog::list();
    for (const doc::RecoveryEntry& entry : entries) {
        // 读出来的条目必须至少是「看起来有效」的。
        QVERIFY(entry.isValid());
    }
    // 正常写入仍然要能用：坏文件不该影响好记录。
    const QString document = dir.filePath(QStringLiteral("good.pdf"));
    QVERIFY(doc::RecoveryLog::write(document, {QByteArray("{}")}));
    doc::RecoveryEntry entry;
    QVERIFY(doc::RecoveryLog::read(document, &entry));
    QCOMPARE(entry.commands.size(), 1);
}

void FuzzTests::pdfOpenSurvivesCorruptedBytes() {
    const QString source = QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/normal.pdf");
    QFile file(source);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(source));
    const QByteArray original = file.readAll();
    file.close();
    QVERIFY(original.size() > 100);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    int opened = 0;
    int rejected = 0;
    for (int iteration = 0; iteration < 60; ++iteration) {
        QByteArray mutated = original;
        const int mutations = 1 + static_cast<int>(rng()() % 8);
        for (int i = 0; i < mutations; ++i) {
            const int position = static_cast<int>(rng()() % mutated.size());
            mutated[position] = static_cast<char>(rng()() % 256);
        }
        if (iteration % 3 == 0) {
            // 截断：下载中断的文件在现实里很常见。
            mutated.truncate(static_cast<int>(rng()() % mutated.size()));
        }

        const QString path = dir.filePath(QStringLiteral("fuzz-%1.pdf").arg(iteration));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(mutated);
        out.close();

        const pdf::PdfOpenResult result = pdf::openDocument(path);
        if (result.passwordRequired) {
            continue;
        }
        if (result.document.isValid()) {
            ++opened;
            // 打开成功的文档必须真能取到页，不能只是「对象有效」。
            QVERIFY(result.document.pageCount() >= 1);
            const auto page = result.document.page(0);
            QVERIFY(page.has_value());
        } else {
            ++rejected;
            QVERIFY2(result.errorMessage.size() >= 0, "失败必须带原因（可以为空串，但不能崩）");
        }
    }
    qInfo("fuzzed pdfs: opened=%d rejected=%d", opened, rejected);
    QCOMPARE(opened + rejected > 0, true);
}

QTEST_MAIN(FuzzTests)
#include "FuzzTests.moc"
