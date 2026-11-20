#include "app/controllers/SafeDocumentController.h"
#include "app/dialogs/SafeDocumentViewerDialog.h"
#include "ipc/IpcProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

namespace {

QString parseWorkerExecutablePath() {
    return QDir::fromNativeSeparators(QString::fromLocal8Bit(AIPDF_PARSE_WORKER_EXE));
}

// 夹具目录：优先用编译期给的目录，找不到再退到仓库里共享的那份。
QString fixturePath(const QString& name) {
    const QString primary = QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
    if (QFileInfo::exists(primary)) {
        return primary;
    }
    const QString shared = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("../../pdf-fixtures"));
    const QString fallback = QDir(shared).filePath(name);
    return QFileInfo::exists(fallback) ? fallback : primary;
}

QString uniqueServerName(const QString& prefix) {
    return prefix + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// 起一个真的解析进程，并用本地 socket 连上去。
class ParseWorkerProcess {
public:
    bool start(const QString& serverName) {
        process_.start(parseWorkerExecutablePath(), {"--server-name", serverName});
        if (!process_.waitForStarted(5000)) {
            return false;
        }
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 8000) {
            socket_.abort();
            socket_.connectToServer(serverName);
            if (socket_.waitForConnected(200)) {
                return true;
            }
            QTest::qWait(50);
        }
        return false;
    }

    QJsonObject request(const QString& operation,
                        const QJsonObject& payload = {},
                        int timeoutMs = 8000) {
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        socket_.write(aipdf::ipc::encodeFrame(
            aipdf::ipc::makeRequest(requestId, operation, payload)));
        socket_.waitForBytesWritten(2000);
        const QByteArray frame = readFrame(timeoutMs);
        if (frame.isEmpty()) {
            return {};
        }
        return QJsonDocument::fromJson(frame).object();
    }

    bool isRunning() const { return process_.state() != QProcess::NotRunning; }
    QProcess& process() { return process_; }

private:
    QByteArray readFrame(int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            buffer_.append(socket_.readAll());
            const int newline = buffer_.indexOf('\n');
            if (newline >= 0) {
                const QByteArray frame = buffer_.left(newline);
                buffer_.remove(0, newline + 1);
                return frame;
            }
            if (!socket_.waitForReadyRead(200)) {
                continue;
            }
        }
        return {};
    }

    QProcess process_;
    QLocalSocket socket_;
    QByteArray buffer_;
};

// 造一个不是 PDF 的坏文件：解析器必须明确拒绝，而不是崩掉。
QString writeCorruptFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write("this is definitely not a pdf file, just random bytes \x01\x02\x03\x04");
    file.close();
    return path;
}

}  // namespace

class SafeDocumentTests final : public QObject {
    Q_OBJECT

private slots:
    void workerStartsAndAnswersHealthCheck();
    void openReportsPageCountAndPageSize();
    void renderWritesReadablePng();
    void missingFileIsRejectedWithReason();
    void corruptFileIsRejectedWithoutCrash();
    void closeStopsTheWorkerProcess();
    void workerExitsWhenParentProcessIsGone();
    void encryptedDocumentDistinguishesPasswordErrors();
    void controllerOpensDocumentAndRendersPage();
    void controllerReportsFailureForMissingFile();
    void viewerDialogExplainsIsolationAndFailure();
    void viewerDialogOpensDocumentInIsolatedProcess();
};

void SafeDocumentTests::workerStartsAndAnswersHealthCheck() {
    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-health")),
             "解析进程没有接受连接");

    const QJsonObject response = worker.request("system.health");
    QVERIFY(!response.isEmpty());
    QCOMPARE(response.value("type").toString(), QString("result"));
    QVERIFY(response.value("ok").toBool());
    QCOMPARE(response.value("protocol_version").toInt(), aipdf::ipc::kProtocolVersion);
    QCOMPARE(response.value("payload").toObject().value("status").toString(),
             QString("ready"));

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::openReportsPageCountAndPageSize() {
    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-open")),
             "解析进程没有接受连接");

    QJsonObject payload;
    payload.insert(QStringLiteral("path"), fixturePath(QStringLiteral("normal.pdf")));

    const QJsonObject response = worker.request("pdf.open", payload);
    QVERIFY(!response.isEmpty());
    QCOMPARE(response.value("type").toString(), QString("result"));
    QVERIFY(response.value("ok").toBool());

    const QJsonObject result = response.value("payload").toObject();
    const int pageCount = result.value("pageCount").toInt();
    QVERIFY2(pageCount > 0, "页数应该是正数");
    QCOMPARE(pageCount, 1);

    const QJsonArray pages = result.value("pages").toArray();
    QCOMPARE(pages.size(), 1);
    const QJsonObject page = pages.first().toObject();
    QVERIFY(page.value("widthPt").toDouble() > 0.0);
    QVERIFY(page.value("heightPt").toDouble() > 0.0);
    // normal.pdf 的 MediaBox 是 200x100。
    QVERIFY(qAbs(page.value("widthPt").toDouble() - 200.0) < 1.0);
    QVERIFY(qAbs(page.value("heightPt").toDouble() - 100.0) < 1.0);
    QCOMPARE(page.value("rotation").toInt(), 0);

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::renderWritesReadablePng() {
    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-render")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), fixturePath(QStringLiteral("normal.pdf")));
    const QJsonObject opened = worker.request("pdf.open", openPayload);
    QVERIFY(opened.value("ok").toBool());

    QJsonObject renderPayload;
    renderPayload.insert(QStringLiteral("pageIndex"), 0);
    renderPayload.insert(QStringLiteral("scale"), 1.5);

    const QJsonObject response = worker.request("pdf.render", renderPayload);
    QVERIFY(!response.isEmpty());
    QCOMPARE(response.value("type").toString(), QString("result"));
    QVERIFY(response.value("ok").toBool());

    const QJsonObject result = response.value("payload").toObject();
    const QString imagePath = result.value("imagePath").toString();
    QVERIFY2(!imagePath.isEmpty(), "渲染结果没有给出图片路径");
    QVERIFY2(QFileInfo::exists(imagePath), qPrintable(QStringLiteral("图片不存在：%1").arg(imagePath)));

    // 图片必须真的能读出来，不能只是个空文件。
    QImage image(imagePath);
    QVERIFY2(!image.isNull(), "渲染出来的 PNG 读不出来");
    QVERIFY(image.width() > 0);
    QVERIFY(image.height() > 0);
    QCOMPARE(image.width(), static_cast<int>(result.value("widthPx").toInt()));
    QCOMPARE(image.height(), static_cast<int>(result.value("heightPx").toInt()));

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::missingFileIsRejectedWithReason() {
    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-missing")),
             "解析进程没有接受连接");

    QJsonObject payload;
    payload.insert(QStringLiteral("path"),
                   QDir(QDir::tempPath()).filePath(QStringLiteral("aipdf-not-here.pdf")));

    const QJsonObject response = worker.request("pdf.open", payload);
    QVERIFY(!response.isEmpty());
    QCOMPARE(response.value("type").toString(), QString("error"));
    QVERIFY(!response.value("ok").toBool());

    const QJsonObject error = response.value("error").toObject();
    const QString code = error.value("code").toString();
    const QString message = error.value("message").toString();
    QVERIFY2(!message.isEmpty(), "失败必须带原因");
    QVERIFY2(code == QLatin1String("file_not_found") || code == QLatin1String("open_failed"),
             qPrintable(code));

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::corruptFileIsRejectedWithoutCrash() {
    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    const QString corruptPath = writeCorruptFile(sandbox.filePath(QStringLiteral("broken.pdf")));
    QVERIFY(!corruptPath.isEmpty());

    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-corrupt")),
             "解析进程没有接受连接");

    QJsonObject payload;
    payload.insert(QStringLiteral("path"), corruptPath);

    const QJsonObject response = worker.request("pdf.open", payload);
    QVERIFY(!response.isEmpty());
    QCOMPARE(response.value("type").toString(), QString("error"));
    QVERIFY(!response.value("ok").toBool());
    QVERIFY(!response.value("error").toObject().value("message").toString().isEmpty());

    // 坏文件不能把解析进程带崩：它还得能继续回答。
    QVERIFY(worker.isRunning());
    const QJsonObject health = worker.request("system.health");
    QVERIFY(!health.isEmpty());
    QVERIFY(health.value("ok").toBool());

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::closeStopsTheWorkerProcess() {
    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-close")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(worker.request("pdf.open", openPayload).value("ok").toBool());

    const QJsonObject closed = worker.request("pdf.close");
    QVERIFY(!closed.isEmpty());
    QCOMPARE(closed.value("type").toString(), QString("result"));
    QVERIFY(closed.value("ok").toBool());
    QVERIFY(closed.value("payload").toObject().value("closed").toBool());

    // 关掉之后进程必须自己退出：不留孤儿进程占着 pdfium.dll。
    QTRY_VERIFY_WITH_TIMEOUT(!worker.isRunning(), 8000);
}

void SafeDocumentTests::workerExitsWhenParentProcessIsGone() {
    // 借一个刚退出的短命进程的 PID：当成「父进程」传给解析进程，
    // 它应当立刻发现父进程没了并退出。
    QProcess helper;
    helper.start("cmd.exe", {"/c", "exit", "0"});
    QVERIFY(helper.waitForStarted(5000));
    // PID 要在进程还活着的时候取：进程结束后 processId() 会变成 0。
    const qint64 deadParentPid = helper.processId();
    QVERIFY(deadParentPid > 0);
    QVERIFY(helper.waitForFinished(5000));

    QProcess worker;
    worker.start(parseWorkerExecutablePath(),
                 {"--server-name", uniqueServerName("aipdf-parse-orphan"),
                  "--parent-pid", QString::number(deadParentPid)});
    QVERIFY(worker.waitForStarted(5000));
    QVERIFY2(worker.waitForFinished(8000), "父进程没了之后解析进程没有退出");
    QCOMPARE(worker.exitCode(), 0);
}

void SafeDocumentTests::encryptedDocumentDistinguishesPasswordErrors() {
    const QString encrypted = fixturePath(QStringLiteral("encrypted.pdf"));
    if (!QFileInfo::exists(encrypted)) {
        QSKIP("没有加密夹具，跳过");
    }

    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-password")),
             "解析进程没有接受连接");

    // 不给密码：必须说「要密码」。
    QJsonObject noPassword;
    noPassword.insert(QStringLiteral("path"), encrypted);
    const QJsonObject required = worker.request("pdf.open", noPassword);
    QCOMPARE(required.value("type").toString(), QString("error"));
    QCOMPARE(required.value("error").toObject().value("code").toString(),
             QString("password_required"));
    QVERIFY(required.value("error").toObject().value("message").toString().contains(
        QStringLiteral("密码")));

    // 口令不对：必须说「口令不对」，不能和「要密码」混成一回事。
    QJsonObject wrongPassword;
    wrongPassword.insert(QStringLiteral("path"), encrypted);
    wrongPassword.insert(QStringLiteral("password"), QStringLiteral("definitely-wrong"));
    const QJsonObject incorrect = worker.request("pdf.open", wrongPassword);
    QCOMPARE(incorrect.value("type").toString(), QString("error"));
    QCOMPARE(incorrect.value("error").toObject().value("code").toString(),
             QString("password_incorrect"));

    // 错误消息里绝不能出现口令本身。
    QVERIFY(!incorrect.value("error").toObject().value("message").toString().contains(
        QStringLiteral("definitely-wrong")));

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::controllerOpensDocumentAndRendersPage() {
    aipdf::app::SafeDocumentController controller;
    controller.setWorkerExecutable(parseWorkerExecutablePath());

    QString error;
    QVERIFY2(controller.open(fixturePath(QStringLiteral("normal.pdf")), QString(), &error),
             qPrintable(error));
    QVERIFY(controller.isOpen());
    QCOMPARE(controller.pageCount(), 1);

    const QString imagePath = controller.renderPage(0, 1.0, &error);
    QVERIFY2(!imagePath.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo::exists(imagePath));
    QImage image(imagePath);
    QVERIFY(!image.isNull());
    QVERIFY(image.width() > 0);

    controller.close();
    QVERIFY(!controller.isOpen());
}

void SafeDocumentTests::controllerReportsFailureForMissingFile() {
    aipdf::app::SafeDocumentController controller;
    controller.setWorkerExecutable(parseWorkerExecutablePath());

    QString error;
    const QString missing = QDir(QDir::tempPath()).filePath(QStringLiteral("aipdf-missing.pdf"));
    QVERIFY(!controller.open(missing, QString(), &error));
    QVERIFY(!controller.isOpen());
    QVERIFY2(!error.isEmpty(), "失败必须带原因");
}

void SafeDocumentTests::viewerDialogExplainsIsolationAndFailure() {
    aipdf::app::SafeDocumentViewerDialog dialog(
        QDir(QDir::tempPath()).filePath(QStringLiteral("aipdf-missing.pdf")));
    // 界面测试不许抢桌面：这个窗口永远不映射到屏幕上。
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.setWorkerExecutable(parseWorkerExecutablePath());

    QVERIFY2(dialog.bannerText().contains(QStringLiteral("隔离进程")),
             qPrintable(dialog.bannerText()));
    QVERIFY(dialog.bannerText().contains(QStringLiteral("只读")));

    QVERIFY(!dialog.openDocument());
    QVERIFY2(!dialog.lastError().isEmpty(), "打开失败必须显示具体原因");
}

void SafeDocumentTests::viewerDialogOpensDocumentInIsolatedProcess() {
    aipdf::app::SafeDocumentViewerDialog dialog(fixturePath(QStringLiteral("normal.pdf")));
    // 界面测试不许抢桌面：这个窗口永远不映射到屏幕上。
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.setWorkerExecutable(parseWorkerExecutablePath());

    QVERIFY2(dialog.openDocument(), qPrintable(dialog.lastError()));
    QVERIFY(dialog.documentOpened());
    QCOMPARE(dialog.pageCount(), 1);
    QCOMPARE(dialog.currentPage(), 0);
}

QTEST_MAIN(SafeDocumentTests)
#include "SafeDocumentTests.moc"
