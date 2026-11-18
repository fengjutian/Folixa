#include "doc/batch/BatchProcessor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTest>
#include <QUuid>

using aipdf::doc::BatchItemResult;
using aipdf::doc::BatchReport;
using aipdf::doc::BatchStep;
using aipdf::doc::BatchTransform;
using aipdf::doc::runBatch;

namespace {

// 写一个测试夹具文件。
void writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(contents);
    file.close();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// 一个确定性的变换：在内容后面补一个字符。
BatchTransform appendMarker() {
    return [](const QByteArray& input, QString* error) -> QByteArray {
        if (error != nullptr) {
            error->clear();
        }
        return input + QByteArrayLiteral("!");
    };
}

// 一个会失败的变换：统一报同一个中文原因，方便断言。
BatchTransform alwaysFail(const QString& reason) {
    return [reason](const QByteArray&, QString* error) -> QByteArray {
        if (error != nullptr) {
            *error = reason;
        }
        return {};
    };
}

// 只有内容里带 "bad" 的文件才失败：用来把某几个文件挑出来。
BatchTransform failOnMarker() {
    return [](const QByteArray& input, QString* error) -> QByteArray {
        if (error != nullptr) {
            error->clear();
        }
        if (input.contains("bad")) {
            if (error != nullptr) {
                *error = QStringLiteral("内容里带 bad，按规则拒绝");
            }
            return {};
        }
        return input + QByteArrayLiteral("+ok");
    };
}

QStringList fileListOf(const QString& directory) {
    return QDir(directory).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

}  // namespace

class BatchProcessorTests final : public QObject {
    Q_OBJECT

private:
    // 每个用例自己的临时目录（建在系统临时目录下，用完就删）。
    QString m_tempDir;

private slots:
    void init();
    void cleanup();

    void twoInputsWithOneStepBothSucceed();
    void failedStepMarksItemFailedAndStopsLaterSteps();
    void continueOnErrorDisabledStopsWholeBatch();
    void jsonRoundTripKeepsEveryField();
    void brokenJsonIsRejected();
    void wrongFieldTypeIsRejected();
    void writesOutputsAndCreatesOutputDirectory();
    void noOutputDirectoryWritesNothing();
    void emptyInputListGivesEmptyReport();
    void emptyStepsMarkEveryItemSkipped();
    void missingInputFileIsReportedAsFailure();
    void stepWithoutTransformPassesBytesThrough();
    void usesInputFileNameWhenOutputPathMissing();
    void jsonMissingFieldIsRejected();
    void countsAlwaysMatchItems();
};

// 每个用例一个独立子目录，跑完就删。
void BatchProcessorTests::init() {
    const QString unique = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_tempDir = QDir(QDir::tempPath()).filePath(QStringLiteral("aipdf-batch-") + unique);
    QDir().mkpath(m_tempDir);
}

void BatchProcessorTests::cleanup() {
    if (!m_tempDir.isEmpty()) {
        QDir(m_tempDir).removeRecursively();
    }
    m_tempDir.clear();
}

// 两个文件各跑一步变换：两条都成功，字节数与耗时说得通。
void BatchProcessorTests::twoInputsWithOneStepBothSucceed() {
    const QString first = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    const QString second = QDir(m_tempDir).filePath(QStringLiteral("b.pdf"));
    writeFile(first, QByteArrayLiteral("hello"));
    writeFile(second, QByteArrayLiteral("world!"));

    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("append"), appendMarker()});

    const BatchReport report = runBatch({first, second}, {}, steps);

    QCOMPARE(report.items.size(), 2);
    QCOMPARE(report.succeededCount, 2);
    QCOMPARE(report.failedCount, 0);
    QCOMPARE(report.skippedCount, 0);
    QVERIFY(!report.aborted);
    QCOMPARE(report.items.at(0).inputPath, first);
    QCOMPARE(report.items.at(1).inputPath, second);
    QVERIFY(report.items.at(0).succeeded);
    QVERIFY(report.items.at(1).succeeded);
    QVERIFY(report.items.at(0).error.isEmpty());
    // 输入 5 字节、输出 6 字节（补了一个感叹号）。
    QCOMPARE(report.items.at(0).inputBytes, qint64(5));
    QCOMPARE(report.items.at(0).outputBytes, qint64(6));
    QCOMPARE(report.items.at(1).inputBytes, qint64(6));
    QCOMPARE(report.items.at(1).outputBytes, qint64(7));
    QVERIFY(report.items.at(0).durationMs >= 0);
    QVERIFY(report.totalDurationMs >= 0);
}

// 中间一步失败：这个文件记失败，后面的步骤不再执行。
void BatchProcessorTests::failedStepMarksItemFailedAndStopsLaterSteps() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    writeFile(input, QByteArrayLiteral("hello"));

    int laterCalls = 0;
    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("first"), appendMarker()});
    steps.append(BatchStep{QStringLiteral("boom"), alwaysFail(QStringLiteral("磁盘没空间了"))});
    steps.append(BatchStep{QStringLiteral("later"),
                           [&laterCalls](const QByteArray& inputBytes, QString*) -> QByteArray {
                               ++laterCalls;
                               return inputBytes;
                           }});

    const BatchReport report = runBatch({input}, {}, steps);

    QCOMPARE(report.items.size(), 1);
    const BatchItemResult& item = report.items.at(0);
    QVERIFY(!item.succeeded);
    QVERIFY(!item.skipped);
    QVERIFY(!item.error.isEmpty());
    // 失败原因里要能看出是哪一步坏了。
    QVERIFY2(item.error.contains(QStringLiteral("boom")), qPrintable(item.error));
    QVERIFY2(item.error.contains(QStringLiteral("磁盘没空间了")), qPrintable(item.error));
    // 关键断言：失败之后的那一步根本没被调用。
    QCOMPARE(laterCalls, 0);
    QCOMPARE(report.failedCount, 1);
    QCOMPARE(report.succeededCount, 0);
}

// continueOnError=false：遇到失败就停下，后面的文件不再处理。
void BatchProcessorTests::continueOnErrorDisabledStopsWholeBatch() {
    const QString bad = QDir(m_tempDir).filePath(QStringLiteral("bad.pdf"));
    const QString good = QDir(m_tempDir).filePath(QStringLiteral("good.pdf"));
    writeFile(bad, QByteArrayLiteral("bad content"));
    writeFile(good, QByteArrayLiteral("fine content"));

    int calls = 0;
    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("rule"),
                           [&calls](const QByteArray& inputBytes, QString* error) -> QByteArray {
                               ++calls;
                               return failOnMarker()(inputBytes, error);
                           }});

    const BatchReport report = runBatch({bad, good}, {}, steps, false);

    QCOMPARE(report.items.size(), 1);
    QCOMPARE(report.items.at(0).inputPath, bad);
    QVERIFY(!report.items.at(0).succeeded);
    QVERIFY(!report.items.at(0).error.isEmpty());
    QVERIFY(report.aborted);
    QCOMPARE(report.failedCount, 1);
    // 第二个文件根本没被碰过。
    QCOMPARE(calls, 1);
}

// 报告存盘再读回来：条目与关键字段必须一模一样。
void BatchProcessorTests::jsonRoundTripKeepsEveryField() {
    const QString ok = QDir(m_tempDir).filePath(QStringLiteral("ok.pdf"));
    const QString bad = QDir(m_tempDir).filePath(QStringLiteral("bad.pdf"));
    writeFile(ok, QByteArrayLiteral("fine"));
    writeFile(bad, QByteArrayLiteral("bad"));

    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("rule"), failOnMarker()});
    const BatchReport original = runBatch({ok, bad}, {}, steps);

    const QByteArray json = original.toJson();
    QVERIFY(!json.isEmpty());
    // 缩进 2 空格（Qt 默认给的是 4 空格，这里换过）。
    QVERIFY2(json.contains("\n  \"items\": ["), json.constData());
    // 中文不转义：报告是给人看的，转义成 \uXXXX 就没法直接读了。
    QVERIFY2(json.contains(QStringLiteral("按规则拒绝").toUtf8()), json.constData());

    BatchReport restored;
    QString error;
    QVERIFY2(BatchReport::fromJson(json, &restored, &error), qPrintable(error));

    QCOMPARE(restored.items.size(), original.items.size());
    QCOMPARE(restored.totalDurationMs, original.totalDurationMs);
    QCOMPARE(restored.succeededCount, original.succeededCount);
    QCOMPARE(restored.failedCount, original.failedCount);
    QCOMPARE(restored.skippedCount, original.skippedCount);
    QCOMPARE(restored.aborted, original.aborted);
    for (int index = 0; index < original.items.size(); ++index) {
        const BatchItemResult& before = original.items.at(index);
        const BatchItemResult& after = restored.items.at(index);
        QCOMPARE(after.inputPath, before.inputPath);
        QCOMPARE(after.outputPath, before.outputPath);
        QCOMPARE(after.succeeded, before.succeeded);
        QCOMPARE(after.skipped, before.skipped);
        QCOMPARE(after.durationMs, before.durationMs);
        QCOMPARE(after.error, before.error);
        QCOMPARE(after.inputBytes, before.inputBytes);
        QCOMPARE(after.outputBytes, before.outputBytes);
    }
}

// 坏 JSON 必须报错，不能吞成一份空报告。
void BatchProcessorTests::brokenJsonIsRejected() {
    BatchReport report;
    report.items.append(BatchItemResult());
    report.items[0].inputPath = QStringLiteral("keep-me");
    const int before = report.items.size();

    QString error;
    QVERIFY(!BatchReport::fromJson(QByteArrayLiteral("{ not json at all"), &report, &error));
    QVERIFY(!error.isEmpty());
    // 失败不能把调用方手里那份报告换掉。
    QCOMPARE(report.items.size(), before);
    QCOMPARE(report.items.at(0).inputPath, QStringLiteral("keep-me"));
}

// 字段类型不对（items 是字符串）同样要报错。
void BatchProcessorTests::wrongFieldTypeIsRejected() {
    BatchReport report;
    QString error;
    QVERIFY(!BatchReport::fromJson(QByteArrayLiteral("{\"items\": \"not-an-array\"}"), &report, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(report.items.isEmpty());

    // 顶层不是对象（比如给了一个数组）也要报错。
    QVERIFY(!BatchReport::fromJson(QByteArrayLiteral("[1, 2, 3]"), &report, &error));
    QVERIFY(!error.isEmpty());

    // 条目里的数字字段写成字符串也不行。
    QVERIFY(!BatchReport::fromJson(
        QByteArrayLiteral("{\"items\": [{\"inputPath\": \"a\", \"outputPath\": \"\", \"succeeded\": true,"
                               " \"skipped\": false, \"durationMs\": \"12\", \"error\": \"\","
                               " \"inputBytes\": 1, \"outputBytes\": 2}], \"totalDurationMs\": 1}"),
        &report,
        &error));
    QVERIFY(!error.isEmpty());
}

// 给输出目录：真的落盘，目录不存在也会被创建。
void BatchProcessorTests::writesOutputsAndCreatesOutputDirectory() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    writeFile(input, QByteArrayLiteral("hello"));
    const QString outputDirectory = QDir(m_tempDir).filePath(QStringLiteral("out/deep/nested"));
    QVERIFY(!QFileInfo::exists(outputDirectory));

    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("append"), appendMarker()});

    const BatchReport report =
        runBatch({input}, {QStringLiteral("a-out.pdf")}, steps, true, outputDirectory);

    QCOMPARE(report.items.size(), 1);
    QVERIFY(report.items.at(0).succeeded);
    // 目录被自动建出来了。
    QVERIFY(QFileInfo::exists(outputDirectory));
    const QString expected = QDir(outputDirectory).filePath(QStringLiteral("a-out.pdf"));
    QCOMPARE(report.items.at(0).outputPath, expected);
    QVERIFY(QFileInfo::exists(expected));
    QCOMPARE(readFile(expected), QByteArrayLiteral("hello!"));
    QCOMPARE(report.items.at(0).outputBytes, qint64(6));
}

// 不给输出目录：一个文件都不许产生。
void BatchProcessorTests::noOutputDirectoryWritesNothing() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    writeFile(input, QByteArrayLiteral("hello"));
    const QStringList before = fileListOf(m_tempDir);

    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("append"), appendMarker()});

    const BatchReport report = runBatch({input}, {}, steps);

    QVERIFY(report.items.at(0).succeeded);
    QVERIFY(report.items.at(0).outputPath.isEmpty());
    QCOMPARE(report.items.at(0).outputBytes, qint64(6));
    QCOMPARE(fileListOf(m_tempDir), before);
}

// 空输入列表：空报告，不崩。
void BatchProcessorTests::emptyInputListGivesEmptyReport() {
    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("append"), appendMarker()});

    const BatchReport report = runBatch({}, {}, steps);

    QVERIFY(report.items.isEmpty());
    QCOMPARE(report.succeededCount, 0);
    QCOMPARE(report.failedCount, 0);
    QCOMPARE(report.skippedCount, 0);
    QVERIFY(!report.aborted);

    BatchReport restored;
    QString error;
    QVERIFY2(BatchReport::fromJson(report.toJson(), &restored, &error), qPrintable(error));
    QVERIFY(restored.items.isEmpty());
}

// 步骤为空：每条都记成跳过。
void BatchProcessorTests::emptyStepsMarkEveryItemSkipped() {
    const QString first = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    const QString second = QDir(m_tempDir).filePath(QStringLiteral("b.pdf"));
    writeFile(first, QByteArrayLiteral("hello"));
    writeFile(second, QByteArrayLiteral("world"));
    const QString outputDirectory = QDir(m_tempDir).filePath(QStringLiteral("out"));

    const BatchReport report = runBatch({first, second}, {}, {}, true, outputDirectory);

    QCOMPARE(report.items.size(), 2);
    QCOMPARE(report.skippedCount, 2);
    QCOMPARE(report.succeededCount, 0);
    QCOMPARE(report.failedCount, 0);
    for (const BatchItemResult& item : report.items) {
        QVERIFY(item.skipped);
        QVERIFY(!item.succeeded);
        QVERIFY(item.error.isEmpty());
    }
    // 整批没活干，就不该在磁盘上留任何东西。
    QVERIFY(!QFileInfo::exists(outputDirectory));
}

// 输入文件不存在：记成失败，原因说清楚是哪个文件。
void BatchProcessorTests::missingInputFileIsReportedAsFailure() {
    const QString missing = QDir(m_tempDir).filePath(QStringLiteral("nope.pdf"));

    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("append"), appendMarker()});

    const BatchReport report = runBatch({missing}, {}, steps);

    QCOMPARE(report.items.size(), 1);
    QVERIFY(!report.items.at(0).succeeded);
    QVERIFY(!report.items.at(0).error.isEmpty());
    QVERIFY2(report.items.at(0).error.contains(QStringLiteral("nope.pdf")),
             qPrintable(report.items.at(0).error));
    QCOMPARE(report.failedCount, 1);
}

// 空变换（没有 transform）= 这一步跳过，字节原样传下去。
void BatchProcessorTests::stepWithoutTransformPassesBytesThrough() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    writeFile(input, QByteArrayLiteral("hello"));
    const QString outputDirectory = QDir(m_tempDir).filePath(QStringLiteral("out"));

    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("empty"), BatchTransform()});
    steps.append(BatchStep{QStringLiteral("identity"),
                           [](const QByteArray& inputBytes, QString* error) -> QByteArray {
                               if (error != nullptr) {
                                   error->clear();
                               }
                               return inputBytes;
                           }});

    const BatchReport report =
        runBatch({input}, {QStringLiteral("a-out.pdf")}, steps, true, outputDirectory);

    QVERIFY(report.items.at(0).succeeded);
    QCOMPARE(report.items.at(0).outputBytes, qint64(5));
    const QString expected = QDir(outputDirectory).filePath(QStringLiteral("a-out.pdf"));
    QCOMPARE(readFile(expected), QByteArrayLiteral("hello"));
}

// 没给输出路径时，用输入文件名兜底。
void BatchProcessorTests::usesInputFileNameWhenOutputPathMissing() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("report.pdf"));
    writeFile(input, QByteArrayLiteral("hello"));
    const QString outputDirectory = QDir(m_tempDir).filePath(QStringLiteral("out"));

    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("append"), appendMarker()});

    const BatchReport report = runBatch({input}, {}, steps, true, outputDirectory);

    const QString expected = QDir(outputDirectory).filePath(QStringLiteral("report.pdf"));
    QCOMPARE(report.items.at(0).outputPath, expected);
    QVERIFY(QFileInfo::exists(expected));
}

// 少一个字段也要报错：字段缺了就没法还原出报告。
void BatchProcessorTests::jsonMissingFieldIsRejected() {
    BatchReport report;
    QString error;
    const QByteArray json = QByteArrayLiteral(
        "{\"items\": [{\"inputPath\": \"a\", \"outputPath\": \"\", \"succeeded\": true,"
        " \"skipped\": false, \"error\": \"\", \"inputBytes\": 1, \"outputBytes\": 2}],"
        " \"totalDurationMs\": 1}");
    QVERIFY(!BatchReport::fromJson(json, &report, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(report.items.isEmpty());
}

// 计数必须跟条目对得上（报告里的数字不能自己瞎报）。
void BatchProcessorTests::countsAlwaysMatchItems() {
    const QString ok = QDir(m_tempDir).filePath(QStringLiteral("ok.pdf"));
    const QString bad = QDir(m_tempDir).filePath(QStringLiteral("bad.pdf"));
    writeFile(ok, QByteArrayLiteral("fine"));
    writeFile(bad, QByteArrayLiteral("bad"));

    QVector<BatchStep> steps;
    steps.append(BatchStep{QStringLiteral("rule"), failOnMarker()});
    const BatchReport report = runBatch({ok, bad}, {}, steps);

    QCOMPARE(report.items.size(), 2);
    QCOMPARE(report.succeededCount, 1);
    QCOMPARE(report.failedCount, 1);
    QCOMPARE(report.skippedCount, 0);

    BatchReport restored;
    QString error;
    QVERIFY2(BatchReport::fromJson(report.toJson(), &restored, &error), qPrintable(error));
    QCOMPARE(restored.succeededCount, report.succeededCount);
    QCOMPARE(restored.failedCount, report.failedCount);
    QCOMPARE(restored.skippedCount, report.skippedCount);
}

QTEST_GUILESS_MAIN(BatchProcessorTests)
#include "BatchProcessorTests.moc"
