#include "app/controllers/SafeDocumentController.h"
#include "app/dialogs/SafeDocumentViewerDialog.h"
#include "ipc/IpcProtocol.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QLocalSocket>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
        return requestWithVersion(operation, payload, aipdf::ipc::kProtocolVersion, timeoutMs);
    }

    // 显式指定协议版本：用来验证「旧 Worker 收到新版本请求要明确拒绝」。
    QJsonObject requestWithVersion(const QString& operation,
                                   const QJsonObject& payload,
                                   int protocolVersion,
                                   int timeoutMs = 8000) {
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject frame = aipdf::ipc::makeRequest(requestId, operation, payload);
        frame.insert(QStringLiteral("protocol_version"), protocolVersion);
        socket_.write(aipdf::ipc::encodeFrame(frame));
        socket_.waitForBytesWritten(2000);
        const QByteArray reply = readFrame(timeoutMs);
        if (reply.isEmpty()) {
            return {};
        }
        return QJsonDocument::fromJson(reply).object();
    }

    // 只发不等：用来在保存进行到一半时把解析进程杀掉。
    void sendRequest(const QString& operation, const QJsonObject& payload = {}) {
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        socket_.write(aipdf::ipc::encodeFrame(
            aipdf::ipc::makeRequest(requestId, operation, payload)));
        socket_.waitForBytesWritten(2000);
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

// 仓库根目录：编译期 __FILE__ 就在 tests/ 下面，往上退一级就是。
QString repoRootPath() {
    const QFileInfo info(QString::fromLocal8Bit(__FILE__));
    return QDir(info.absolutePath()).filePath(QStringLiteral(".."));
}

// qpdf 可执行文件。仓库里带了 tools/qpdf，找不到就返回空串（相关用例跳过）。
QString qpdfExecutablePath() {
    const QString candidate =
        QDir(repoRootPath()).filePath(QStringLiteral("tools/qpdf/bin/qpdf.exe"));
    return QFileInfo::exists(candidate) ? candidate : QString();
}

// 跑一次 qpdf，拿它的标准输出。退出码通过 exitCode 带出来。
QString runQpdf(const QStringList& arguments, int* exitCode = nullptr) {
    const QString executable = qpdfExecutablePath();
    if (executable.isEmpty()) {
        return {};
    }
    QProcess process;
    process.start(executable, arguments);
    if (!process.waitForStarted(5000)) {
        return {};
    }
    process.waitForFinished(20000);
    if (exitCode != nullptr) {
        *exitCode = process.exitCode();
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput());
}

// 读一个文件里有没有某个 PDF 记号（用来判断签名还在不在）。
bool fileContainsMarker(const QString& path, const QByteArray& marker) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return bytes.contains(marker);
}

// 这个文件里的数字签名还有效吗？
//
// 签名靠 /ByteRange [a b c d] 说明「签的是哪两段字节」：前一段是 [a, b)，
// 后一段是 [c, a+…)。文件一旦被重写，这四个偏移就对不上新文件了——
// 签名数据还在，但**不再算数**。判据：a 必须是 0，且 c 必须正好是「文件末尾往前 d」。
bool signatureStillValid(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    const int rangePos = bytes.indexOf("/ByteRange");
    if (rangePos < 0) {
        return false;
    }
    const int open = bytes.indexOf('[', rangePos);
    const int close = bytes.indexOf(']', open);
    if (open < 0 || close < 0) {
        return false;
    }
    const QList<QByteArray> parts =
        bytes.mid(open + 1, close - open - 1).simplified().split(' ');
    if (parts.size() != 4) {
        return false;
    }
    bool okA = false;
    bool okB = false;
    bool okC = false;
    bool okD = false;
    const qint64 a = parts[0].toLongLong(&okA);
    const qint64 b = parts[1].toLongLong(&okB);
    const qint64 c = parts[2].toLongLong(&okC);
    const qint64 d = parts[3].toLongLong(&okD);
    if (!okA || !okB || !okC || !okD) {
        return false;
    }
    if (a != 0 || b >= c) {
        return false;
    }
    return c == static_cast<qint64>(bytes.size()) - d;
}

// 数一个文件里有几份修订（%%EOF 的个数）：
// 一份 = 只有原始版本；两份以上 = 上面叠过增量更新。
int countRevisionMarkers(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    int count = 0;
    int position = 0;
    while ((position = bytes.indexOf("%%EOF", position)) >= 0) {
        ++count;
        position += 5;
    }
    return count;
}

// 把系统的定时器精度调到 1 ms（Windows 默认 15.6 ms）。
//
// 用途：轮询「文件刚被打开、内容还没写完」那个窗口时，精度不够就会错过。
// timeBeginPeriod 在 winmm.dll 里，动态加载，免得测试目标多一个链接依赖。
class TimerResolutionGuard {
public:
    TimerResolutionGuard() {
        library_.setFileName(QStringLiteral("winmm"));
        if (!library_.load()) {
            return;
        }
        begin_ = reinterpret_cast<void (*)(unsigned int)>(library_.resolve("timeBeginPeriod"));
        end_ = reinterpret_cast<void (*)(unsigned int)>(library_.resolve("timeEndPeriod"));
        if (begin_ != nullptr) {
            begin_(1);
        }
    }
    ~TimerResolutionGuard() {
        if (begin_ != nullptr && end_ != nullptr) {
            end_(1);
        }
    }

private:
    QLibrary library_;
    void (*begin_)(unsigned int) = nullptr;
    void (*end_)(unsigned int) = nullptr;
};

// 解析进程写盘时会先在目标目录里留下一个临时文件（原子替换的实现）。
// 看到这个临时文件、或者看到目标文件被直接打开，就说明保存真的开始了。
bool saveHasStarted(const QString& directory, const QString& target) {
    if (QFileInfo::exists(target)) {
        return true;
    }
    const QDir dir(directory);
    const QStringList entries = dir.entryList(QDir::Files | QDir::Hidden);
    for (const QString& entry : entries) {
        if (entry.contains(QStringLiteral("aipdf-tmp"))) {
            return true;
        }
    }
    return false;
}

// 把目录里解析进程留下的临时文件清掉（上一轮被杀死的进程留下的）。
void removeLeftoverTempFiles(const QString& directory) {
    QDir dir(directory);
    const QStringList entries = dir.entryList(QDir::Files | QDir::Hidden);
    for (const QString& entry : entries) {
        if (entry.contains(QStringLiteral("aipdf-tmp"))) {
            QFile::remove(dir.filePath(entry));
        }
    }
}

// 从表单域清单里取某个字段的当前值。
QString fieldValue(const QJsonArray& fields, const QString& name) {
    for (const QJsonValue& value : fields) {
        const QJsonObject field = value.toObject();
        if (field.value(QStringLiteral("name")).toString() == name) {
            return field.value(QStringLiteral("value")).toString();
        }
    }
    return {};
}

// 点掉当前模态框上的「确定」：保存前那个确认框不能把测试卡死。
void acceptActiveModalDialog() {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) {
        return;
    }
    const QList<QAbstractButton*> buttons = box->buttons();
    for (QAbstractButton* button : buttons) {
        if (box->buttonRole(button) == QMessageBox::AcceptRole) {
            button->click();
            return;
        }
    }
    box->reject();
}

// 兜底：确认框要是没被点掉，三秒后强制关掉它，测试不能卡死。
void forceCloseModalDialog() {
    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        box->reject();
    }
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

    // 可编辑：填表单域、加批注、保存回磁盘。
    void openReportsFormFields();
    void editChangesFormFieldValueInWorker();
    void controllerEditChangesFormFieldValue();
    void editAddsAnnotationThatSurvivesSave();
    void saveWritesFileThatOpensAgain();
    void saveOfEncryptedDocumentReportsWhatIsLost();
    void saveOfSignedDocumentDropsTheSignature();
    void failedEditIsRejectedAndBlocksSave();
    void protocolVersionMismatchIsRejected();
    void viewerDialogShowsFormFields();
    void viewerDialogSavesDocumentAndTellsWhatIsLost();
    void saveInPlaceReplacesTheFileAndKeepsDocumentOpen();
    void editChangesCheckboxAndChoiceValues();
    void failedSaveDoesNotProduceHalfWrittenFile();
    void savePreservesDocumentContent();
    void mainProcessNeverHoldsDocumentBytes();
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
    // 文档现在能编辑了，横幅里不能再写「只读」——那是不实的话。
    QVERIFY2(!dialog.bannerText().contains(QStringLiteral("只读")),
             qPrintable(dialog.bannerText()));
    // 安全前提要写在界面上：主进程不碰这个 PDF 的字节。
    QVERIFY2(dialog.bannerText().contains(QStringLiteral("不持有它的字节")),
             qPrintable(dialog.bannerText()));

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

// ---------------------------------------------------------- 可编辑（新增） ----

void SafeDocumentTests::openReportsFormFields() {
    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-fields")),
             "解析进程没有接受连接");

    QJsonObject payload;
    payload.insert(QStringLiteral("path"), fixturePath(QStringLiteral("form.pdf")));
    const QJsonObject response = worker.request("pdf.open", payload);
    QVERIFY(response.value("ok").toBool());

    const QJsonArray fields =
        response.value("payload").toObject().value("fields").toArray();
    QVERIFY2(!fields.isEmpty(), "表单文档应当报出表单域清单");

    QStringList names;
    for (const QJsonValue& value : fields) {
        names.append(value.toObject().value("name").toString());
    }
    QVERIFY2(names.contains(QStringLiteral("fullName")), qPrintable(names.join(',')));
    QVERIFY2(names.contains(QStringLiteral("country")), qPrintable(names.join(',')));

    // 字段清单要带类型和当前值，主进程才知道该渲染什么控件。
    for (const QJsonValue& value : fields) {
        const QJsonObject field = value.toObject();
        if (field.value("name").toString() != QStringLiteral("fullName")) {
            continue;
        }
        QCOMPARE(field.value("type").toString(), QString("text"));
        QVERIFY(field.contains("value"));
        QVERIFY(field.contains("readOnly"));
    }

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::editChangesFormFieldValueInWorker() {
    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-edit")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), fixturePath(QStringLiteral("form.pdf")));
    const QJsonObject opened = worker.request("pdf.open", openPayload);
    QVERIFY(opened.value("ok").toBool());

    const QJsonArray before =
        opened.value("payload").toObject().value("fields").toArray();
    QVERIFY2(fieldValue(before, "fullName") != QStringLiteral("隔离进程改的"),
             "测试前提：字段原来的值不是我们要写的那个");

    QJsonObject instruction;
    instruction.insert(QStringLiteral("type"), QStringLiteral("setFormFieldValue"));
    instruction.insert(QStringLiteral("field"), QStringLiteral("fullName"));
    instruction.insert(QStringLiteral("value"), QStringLiteral("隔离进程改的"));

    QJsonObject editPayload;
    editPayload.insert(QStringLiteral("instructions"), QJsonArray{instruction});
    const QJsonObject edited = worker.request("pdf.edit", editPayload);
    QVERIFY2(edited.value("ok").toBool(),
             qPrintable(edited.value("error").toObject().value("message").toString()));

    // 改完再读回：值必须真的变了。
    const QJsonArray after =
        edited.value("payload").toObject().value("fields").toArray();
    QCOMPARE(fieldValue(after, "fullName"), QString("隔离进程改的"));

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::controllerEditChangesFormFieldValue() {
    aipdf::app::SafeDocumentController controller;
    controller.setWorkerExecutable(parseWorkerExecutablePath());

    QString error;
    QVERIFY2(controller.open(fixturePath(QStringLiteral("form.pdf")), QString(), &error),
             qPrintable(error));

    QVERIFY2(controller.setFormFieldValue(QStringLiteral("fullName"),
                                          QStringLiteral("主进程发过去的值"),
                                          &error),
             qPrintable(error));

    QCOMPARE(fieldValue(controller.formFields(), QStringLiteral("fullName")),
             QString("主进程发过去的值"));

    // 不存在的字段必须明确报错，不能悄悄成功。
    QVERIFY(!controller.setFormFieldValue(QStringLiteral("根本没有这个域"),
                                          QStringLiteral("x"),
                                          &error));
    QVERIFY2(!error.isEmpty(), "失败必须带原因");

    controller.close();
}

void SafeDocumentTests::editAddsAnnotationThatSurvivesSave() {
    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    const QString target = sandbox.filePath(QStringLiteral("annotated.pdf"));

    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-annot")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), fixturePath(QStringLiteral("normal.pdf")));
    const QJsonObject opened = worker.request("pdf.open", openPayload);
    QVERIFY(opened.value("ok").toBool());
    const QJsonObject page =
        opened.value("payload").toObject().value("pages").toArray().first().toObject();
    QCOMPARE(page.value("annotationCount").toInt(), 0);

    QJsonObject instruction;
    instruction.insert(QStringLiteral("type"), QStringLiteral("addAnnotation"));
    instruction.insert(QStringLiteral("pageIndex"), 0);
    instruction.insert(QStringLiteral("subtype"), QStringLiteral("text"));
    instruction.insert(QStringLiteral("rect"), QJsonArray{20.0, 20.0, 60.0, 40.0});
    instruction.insert(QStringLiteral("text"), QStringLiteral("隔离进程加的便签"));

    QJsonObject editPayload;
    editPayload.insert(QStringLiteral("instructions"), QJsonArray{instruction});
    const QJsonObject edited = worker.request("pdf.edit", editPayload);
    QVERIFY2(edited.value("ok").toBool(),
             qPrintable(edited.value("error").toObject().value("message").toString()));
    QCOMPARE(edited.value("payload").toObject().value("annotationCount").toInt(), 1);

    QJsonObject savePayload;
    savePayload.insert(QStringLiteral("path"), target);
    const QJsonObject saved = worker.request("pdf.save", savePayload);
    QVERIFY2(saved.value("ok").toBool(),
             qPrintable(saved.value("error").toObject().value("message").toString()));
    QVERIFY(QFileInfo::exists(target));

    // 换一个进程重新打开：批注必须还在文件里。
    ParseWorkerProcess reopened;
    QVERIFY2(reopened.start(uniqueServerName("aipdf-parse-annot2")),
             "解析进程没有接受连接");
    QJsonObject reopenPayload;
    reopenPayload.insert(QStringLiteral("path"), target);
    const QJsonObject reopenResponse = reopened.request("pdf.open", reopenPayload);
    QVERIFY(reopenResponse.value("ok").toBool());
    const QJsonObject reopenedPage =
        reopenResponse.value("payload").toObject().value("pages").toArray().first().toObject();
    QCOMPARE(reopenedPage.value("annotationCount").toInt(), 1);

    reopened.process().kill();
    reopened.process().waitForFinished(3000);
    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::saveWritesFileThatOpensAgain() {
    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    const QString target = sandbox.filePath(QStringLiteral("saved.pdf"));

    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-save")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(worker.request("pdf.open", openPayload).value("ok").toBool());

    QJsonObject savePayload;
    savePayload.insert(QStringLiteral("path"), target);
    const QJsonObject saved = worker.request("pdf.save", savePayload);
    QVERIFY2(saved.value("ok").toBool(),
             qPrintable(saved.value("error").toObject().value("message").toString()));

    const QJsonObject result = saved.value("payload").toObject();
    QCOMPARE(result.value("saved").toBool(), true);
    QVERIFY(QFileInfo::exists(target));
    QVERIFY(QFileInfo(target).size() > 0);
    QCOMPARE(result.value("path").toString(), QFileInfo(target).absoluteFilePath());

    // 落盘的文件必须能被重新打开（qpdf 也要认它是个合法 PDF）。
    ParseWorkerProcess reopened;
    QVERIFY2(reopened.start(uniqueServerName("aipdf-parse-save2")),
             "解析进程没有接受连接");
    QJsonObject reopenPayload;
    reopenPayload.insert(QStringLiteral("path"), target);
    const QJsonObject reopenResponse = reopened.request("pdf.open", reopenPayload);
    QVERIFY2(reopenResponse.value("ok").toBool(),
             qPrintable(reopenResponse.value("error").toObject().value("message").toString()));
    QCOMPARE(reopenResponse.value("payload").toObject().value("pageCount").toInt(), 1);
    reopened.process().kill();
    reopened.process().waitForFinished(3000);

    if (!qpdfExecutablePath().isEmpty()) {
        int exitCode = -1;
        const QString check =
            runQpdf({QStringLiteral("--check"), target}, &exitCode);
        QVERIFY2(exitCode == 0, qPrintable(check));
        QVERIFY2(check.contains(QStringLiteral("No syntax or stream encoding errors")),
                 qPrintable(check));
    }

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::saveOfEncryptedDocumentReportsWhatIsLost() {
    const QString encrypted = fixturePath(QStringLiteral("encrypted.pdf"));
    if (!QFileInfo::exists(encrypted)) {
        QSKIP("没有加密夹具，跳过");
    }

    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    const QString target = sandbox.filePath(QStringLiteral("encrypted-saved.pdf"));

    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-encsave")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), encrypted);
    openPayload.insert(QStringLiteral("password"), QStringLiteral("secret"));
    const QJsonObject opened = worker.request("pdf.open", openPayload);
    QVERIFY2(opened.value("ok").toBool(),
             qPrintable(opened.value("error").toObject().value("message").toString()));

    QJsonObject savePayload;
    savePayload.insert(QStringLiteral("path"), target);
    const QJsonObject saved = worker.request("pdf.save", savePayload);
    QVERIFY2(saved.value("ok").toBool(),
             qPrintable(saved.value("error").toObject().value("message").toString()));

    const QJsonObject result = saved.value("payload").toObject();
    QVERIFY(QFileInfo::exists(target));

    // 实测结论（这个仓库固定的 PDFium 构建）：**加密被保留**。
    // 源文件要口令，保存后的文件也要口令，而且**同一个口令仍然有效**。
    // （这条是实测钉住的，不是推测：qpdf --show-encryption 显示 R/V/P 三项
    //   与源文件完全一致，qpdf --password=secret --check 也能过。）
    QCOMPARE(result.value("sourceEncrypted").toBool(), true);
    QCOMPARE(result.value("encrypted").toBool(), true);
    QCOMPARE(result.value("samePasswordWorks").toBool(), true);
    const QJsonArray warnings = result.value("warnings").toArray();
    QVERIFY2(!warnings.contains(QStringLiteral("encryption_dropped")),
             "加密其实保住了，不该报「加密丢了」");
    QVERIFY(warnings.contains(QStringLiteral("signatures_invalidated")));

    // 保存后的文件：不给口令打不开，给同一个口令能打开。
    ParseWorkerProcess reopened;
    QVERIFY2(reopened.start(uniqueServerName("aipdf-parse-encsave2")),
             "解析进程没有接受连接");
    QJsonObject reopenPayload;
    reopenPayload.insert(QStringLiteral("path"), target);
    const QJsonObject reopenResponse = reopened.request("pdf.open", reopenPayload);
    QCOMPARE(reopenResponse.value("type").toString(), QString("error"));
    QCOMPARE(reopenResponse.value("error").toObject().value("code").toString(),
             QString("password_required"));

    QJsonObject reopenWithPassword;
    reopenWithPassword.insert(QStringLiteral("path"), target);
    reopenWithPassword.insert(QStringLiteral("password"), QStringLiteral("secret"));
    const QJsonObject reopenWithPasswordResponse =
        reopened.request("pdf.open", reopenWithPassword);
    QVERIFY2(reopenWithPasswordResponse.value("ok").toBool(),
             qPrintable(reopenWithPasswordResponse.value("error")
                            .toObject()
                            .value("message")
                            .toString()));

    reopened.process().kill();
    reopened.process().waitForFinished(3000);

    if (!qpdfExecutablePath().isEmpty()) {
        int exitCode = -1;
        const QString encryption = runQpdf({QStringLiteral("--password=secret"),
                                            QStringLiteral("--show-encryption"),
                                            target},
                                           &exitCode);
        QVERIFY2(exitCode == 0, qPrintable(encryption));
        QVERIFY2(encryption.contains(QStringLiteral("R = 2")),
                 qPrintable(encryption));
        QVERIFY2(encryption.contains(QStringLiteral("User password = secret")),
                 qPrintable(encryption));

        int checkExitCode = -1;
        const QString check =
            runQpdf({QStringLiteral("--password=secret"), QStringLiteral("--check"), target},
                    &checkExitCode);
        QVERIFY2(checkExitCode == 0, qPrintable(check));
        QVERIFY2(check.contains(QStringLiteral("No syntax or stream encoding errors")),
                 qPrintable(check));
    }

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::saveOfSignedDocumentDropsTheSignature() {
    const QString signedPath =
        QDir(repoRootPath()).filePath(QStringLiteral("tests/fixtures/signed/signed-sample.pdf"));
    if (!QFileInfo::exists(signedPath)) {
        QSKIP("没有签名夹具，跳过");
    }
    QVERIFY2(fileContainsMarker(signedPath, QByteArray("/ByteRange")),
             "夹具本身应当是签过名的");

    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    const QString target = sandbox.filePath(QStringLiteral("signed-saved.pdf"));

    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-sign")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), signedPath);
    const QJsonObject opened = worker.request("pdf.open", openPayload);
    QVERIFY2(opened.value("ok").toBool(),
             qPrintable(opened.value("error").toObject().value("message").toString()));

    QJsonObject savePayload;
    savePayload.insert(QStringLiteral("path"), target);
    const QJsonObject saved = worker.request("pdf.save", savePayload);
    QVERIFY2(saved.value("ok").toBool(),
             qPrintable(saved.value("error").toObject().value("message").toString()));

    // 实测结论：解析器重写整个文件之后，签名域与 /ByteRange 作为数据还留在文件里，
    // 但字节范围已经对不上新文件 —— 签名**不再有效**。
    // 这正是要如实告诉用户的那件事：不是「签名被删了」，是「签名不算数了」。
    QVERIFY2(signatureStillValid(signedPath), "夹具本身的签名应当是有效的");
    QVERIFY2(!signatureStillValid(target), "保存后的签名应当已经失效");
    QVERIFY(saved.value("payload").toObject().value("warnings").toArray().contains(
        QStringLiteral("signatures_invalidated")));

    // 实测结论：增量更新的历史版本被合并成一份。
    // 签过名的文件本来是「原文件 + 一份增量」（两个 %%EOF），
    // 保存之后只剩一个 %%EOF —— 历史版本没有保留下来。
    QCOMPARE(countRevisionMarkers(signedPath), 2);
    QCOMPARE(countRevisionMarkers(target), 1);
    QVERIFY(saved.value("payload").toObject().value("warnings").toArray().contains(
        QStringLiteral("incremental_history_dropped")));

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::failedEditIsRejectedAndBlocksSave() {
    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    const QString target = sandbox.filePath(QStringLiteral("must-not-exist.pdf"));

    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-failed-edit")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), fixturePath(QStringLiteral("form.pdf")));
    QVERIFY(worker.request("pdf.open", openPayload).value("ok").toBool());

    // 字段不存在：必须明确报错。
    QJsonObject instruction;
    instruction.insert(QStringLiteral("type"), QStringLiteral("setFormFieldValue"));
    instruction.insert(QStringLiteral("field"), QStringLiteral("查无此域"));
    instruction.insert(QStringLiteral("value"), QStringLiteral("x"));
    QJsonObject editPayload;
    editPayload.insert(QStringLiteral("instructions"), QJsonArray{instruction});
    const QJsonObject edited = worker.request("pdf.edit", editPayload);
    QCOMPARE(edited.value("type").toString(), QString("error"));
    QCOMPARE(edited.value("error").toObject().value("code").toString(),
             QString("field_not_found"));
    QVERIFY(!edited.value("error").toObject().value("message").toString().isEmpty());

    // 指令不合法：同样必须明确报错。
    QJsonObject badInstruction;
    badInstruction.insert(QStringLiteral("type"), QStringLiteral("炸掉这个文档"));
    QJsonObject badPayload;
    badPayload.insert(QStringLiteral("instructions"), QJsonArray{badInstruction});
    const QJsonObject bad = worker.request("pdf.edit", badPayload);
    QCOMPARE(bad.value("type").toString(), QString("error"));
    QCOMPARE(bad.value("error").toObject().value("code").toString(),
             QString("invalid_instruction"));

    // 编辑失败之后不许保存：否则就是「产出半改的文件」。
    QJsonObject savePayload;
    savePayload.insert(QStringLiteral("path"), target);
    const QJsonObject saved = worker.request("pdf.save", savePayload);
    QCOMPARE(saved.value("type").toString(), QString("error"));
    QCOMPARE(saved.value("error").toObject().value("code").toString(),
             QString("save_blocked_by_failed_edit"));
    QVERIFY2(!QFileInfo::exists(target), "编辑失败之后不该产出任何文件");

    // 进程没崩，还能继续回答。
    QVERIFY(worker.isRunning());
    QVERIFY(worker.request("system.health").value("ok").toBool());

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::protocolVersionMismatchIsRejected() {
    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-version")),
             "解析进程没有接受连接");

    // 旧版本号的请求（协议已经是 4 了）必须被明确拒绝，不能被当成新协议处理。
    const QJsonObject response =
        worker.requestWithVersion("pdf.open", {}, 3);
    QCOMPARE(response.value("type").toString(), QString("error"));
    QCOMPARE(response.value("error").toObject().value("code").toString(),
             QString("protocol_version_unsupported"));
    QVERIFY(!response.value("error").toObject().value("message").toString().isEmpty());

    // 拒绝之后进程还好好的：不能因为版本对不上就崩掉。
    QVERIFY(worker.isRunning());
    const QJsonObject health = worker.request("system.health");
    QVERIFY(health.value("ok").toBool());
    QCOMPARE(health.value("payload").toObject().value("protocol_version").toInt(),
             aipdf::ipc::kProtocolVersion);

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::viewerDialogShowsFormFields() {
    aipdf::app::SafeDocumentViewerDialog dialog(fixturePath(QStringLiteral("form.pdf")));
    // 界面测试不许抢桌面：这个窗口永远不映射到屏幕上。
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.setWorkerExecutable(parseWorkerExecutablePath());

    QVERIFY2(dialog.openDocument(), qPrintable(dialog.lastError()));
    QVERIFY(dialog.documentOpened());

    // 表单域清单要变成界面上的控件。
    const QJsonArray fields = dialog.formFields();
    QVERIFY2(!fields.isEmpty(), "表单文档应当有表单域");
    QCOMPARE(dialog.formFieldValue(QStringLiteral("fullName")), QString(""));
    QCOMPARE(dialog.formFieldValue(QStringLiteral("country")), QString("China"));
    // 只读字段照样显示当前值，但改不了。
    QCOMPARE(dialog.formFieldValue(QStringLiteral("issuedBy")), QString("Legal"));
}

void SafeDocumentTests::viewerDialogSavesDocumentAndTellsWhatIsLost() {
    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    // 复制一份夹具：保存会覆盖文件，不能把仓库里的那份改掉。
    const QString copy = sandbox.filePath(QStringLiteral("copy.pdf"));
    QVERIFY(QFile::copy(fixturePath(QStringLiteral("normal.pdf")), copy));

    aipdf::app::SafeDocumentViewerDialog dialog(copy);
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.setWorkerExecutable(parseWorkerExecutablePath());
    QVERIFY2(dialog.openDocument(), qPrintable(dialog.lastError()));

    auto* saveButton = dialog.findChild<QPushButton*>(QStringLiteral("safeDocument.save"));
    QVERIFY2(saveButton != nullptr, "保存按钮不存在");

    const qint64 before = QFileInfo(copy).size();
    QTimer::singleShot(0, &acceptActiveModalDialog);
    QTimer::singleShot(3000, &forceCloseModalDialog);
    saveButton->click();

    // 保存成功：状态栏要说清楚写到了哪里、丢了什么。
    QVERIFY2(dialog.statusText().contains(QFileInfo(copy).absoluteFilePath()),
             qPrintable(dialog.statusText()));
    QVERIFY2(dialog.statusText().contains(QStringLiteral("数字签名")),
             qPrintable(dialog.statusText()));
    QVERIFY2(dialog.statusText().contains(QStringLiteral("增量更新")),
             qPrintable(dialog.statusText()));
    QVERIFY(QFileInfo::exists(copy));
    QVERIFY(QFileInfo(copy).size() > 0);
    QVERIFY2(QFileInfo(copy).size() != before || QFileInfo(copy).size() > 0,
             "保存后的文件应当存在");
}

void SafeDocumentTests::saveInPlaceReplacesTheFileAndKeepsDocumentOpen() {
    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    const QString target = sandbox.filePath(QStringLiteral("inplace.pdf"));
    QVERIFY(QFile::copy(fixturePath(QStringLiteral("normal.pdf")), target));
    const QByteArray before = [&] {
        QFile file(target);
        file.open(QIODevice::ReadOnly);
        return file.readAll();
    }();

    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-inplace")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), target);
    QVERIFY(worker.request("pdf.open", openPayload).value("ok").toBool());

    // 先加一条批注，这样保存后的文件一定和原来不一样。
    QJsonObject instruction;
    instruction.insert(QStringLiteral("type"), QStringLiteral("addAnnotation"));
    instruction.insert(QStringLiteral("pageIndex"), 0);
    instruction.insert(QStringLiteral("subtype"), QStringLiteral("text"));
    instruction.insert(QStringLiteral("rect"), QJsonArray{20.0, 20.0, 60.0, 40.0});
    instruction.insert(QStringLiteral("text"), QStringLiteral("覆盖保存前的便签"));
    QJsonObject editPayload;
    editPayload.insert(QStringLiteral("instructions"), QJsonArray{instruction});
    QVERIFY(worker.request("pdf.edit", editPayload).value("ok").toBool());

    // 覆盖保存：目标路径就是当前打开的那个文件。
    QJsonObject savePayload;
    savePayload.insert(QStringLiteral("path"), target);
    const QJsonObject saved = worker.request("pdf.save", savePayload);
    QVERIFY2(saved.value("ok").toBool(),
             qPrintable(saved.value("error").toObject().value("message").toString()));
    // 覆盖保存之后解析进程要能接着编辑：文档必须重新打开成功。
    QCOMPARE(saved.value("payload").toObject().value("documentOpen").toBool(), true);

    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray after = file.readAll();
    file.close();
    QVERIFY2(after != before, "覆盖保存后文件内容应当变了");
    QVERIFY(fileContainsMarker(target, QByteArray("/Annots")));

    // 重新打开还是好的。
    ParseWorkerProcess reopened;
    QVERIFY2(reopened.start(uniqueServerName("aipdf-parse-inplace2")),
             "解析进程没有接受连接");
    QJsonObject reopenPayload;
    reopenPayload.insert(QStringLiteral("path"), target);
    const QJsonObject reopenResponse = reopened.request("pdf.open", reopenPayload);
    QVERIFY2(reopenResponse.value("ok").toBool(),
             qPrintable(reopenResponse.value("error").toObject().value("message").toString()));
    const QJsonObject page =
        reopenResponse.value("payload").toObject().value("pages").toArray().first().toObject();
    QCOMPARE(page.value("annotationCount").toInt(), 1);

    reopened.process().kill();
    reopened.process().waitForFinished(3000);
    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::editChangesCheckboxAndChoiceValues() {
    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-cb")), "解析进程没有接受连接");
    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), fixturePath(QStringLiteral("form.pdf")));
    const QJsonObject opened = worker.request("pdf.open", openPayload);
    QVERIFY(opened.value("ok").toBool());
    const QJsonArray before = opened.value("payload").toObject().value("fields").toArray();
    QCOMPARE(fieldValue(before, "agree"), QString("Off"));
    QCOMPARE(fieldValue(before, "country"), QString("China"));

    // 复选框：勾上。
    QJsonObject instruction;
    instruction.insert(QStringLiteral("type"), QStringLiteral("setFormFieldValue"));
    instruction.insert(QStringLiteral("field"), QStringLiteral("agree"));
    instruction.insert(QStringLiteral("value"), QStringLiteral("Yes"));
    QJsonObject editPayload;
    editPayload.insert(QStringLiteral("instructions"), QJsonArray{instruction});
    const QJsonObject edited = worker.request("pdf.edit", editPayload);
    QVERIFY2(edited.value("ok").toBool(),
             qPrintable(edited.value("error").toObject().value("message").toString()));
    QCOMPARE(fieldValue(edited.value("payload").toObject().value("fields").toArray(),
                        "agree"),
             QString("Yes"));

    // 下拉框：换一项。
    QJsonObject comboInstruction;
    comboInstruction.insert(QStringLiteral("type"), QStringLiteral("setFormFieldValue"));
    comboInstruction.insert(QStringLiteral("field"), QStringLiteral("country"));
    comboInstruction.insert(QStringLiteral("value"), QStringLiteral("Other"));
    QJsonObject comboPayload;
    comboPayload.insert(QStringLiteral("instructions"), QJsonArray{comboInstruction});
    const QJsonObject comboEdited = worker.request("pdf.edit", comboPayload);
    QVERIFY(comboEdited.value("ok").toBool());
    QCOMPARE(fieldValue(comboEdited.value("payload").toObject().value("fields").toArray(),
                        "country"),
             QString("Other"));

    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::failedSaveDoesNotProduceHalfWrittenFile() {
    // 「失败不产出半改的文件」：保存写到一半时把解析进程杀掉，
    // 目标文件必须**根本没出现**（写的是临时文件），或者是一个完整的文件。
    // 绝不能是半截的。
    //
    // 这条是「原子替换」的意义所在：先写临时文件、写成了再替换目标。
    // 如果改成直接截断写目标文件，杀进程就会在目标文件上留下半截内容。
    const QString source = fixturePath(QStringLiteral("cjk.pdf"));
    if (!QFileInfo::exists(source)) {
        QSKIP("没有大一点的夹具，跳过");
    }

    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    const QString opened = sandbox.filePath(QStringLiteral("opened.pdf"));
    const QString target = sandbox.filePath(QStringLiteral("saved.pdf"));

#ifdef Q_OS_WIN
    // Windows 默认的定时器精度是 15.6 ms：轮询会错过「文件刚被打开、内容还没写完」
    // 那个窗口。把精度调到 1 ms，才能在写盘过程中把进程杀掉。
    TimerResolutionGuard timerResolution;
#endif

    // 多个时点都试一遍：进程被杀的时机是不确定的，
    // 但「目标文件永远不是半截的」这条在任何时机都得成立。
    for (const int delayMs : {0, 1, 3, 7}) {
        if (QFileInfo::exists(opened)) {
            QVERIFY(QFile::remove(opened));
        }
        if (QFileInfo::exists(target)) {
            QVERIFY(QFile::remove(target));
        }
        removeLeftoverTempFiles(sandbox.path());
        QVERIFY(QFile::copy(source, opened));

        ParseWorkerProcess worker;
        QVERIFY2(worker.start(uniqueServerName("aipdf-parse-kill")),
                 "解析进程没有接受连接");

        QJsonObject openPayload;
        openPayload.insert(QStringLiteral("path"), opened);
        QVERIFY(worker.request("pdf.open", openPayload).value("ok").toBool());

        // 发起保存，不等回执：回执要等写完才有。
        QJsonObject savePayload;
        savePayload.insert(QStringLiteral("path"), target);
        worker.sendRequest("pdf.save", savePayload);

        // 等保存真的开始（临时文件出现，或者目标文件被直接打开），
        // 然后立刻把进程杀掉：这时候文件正写到一半。
        QElapsedTimer timer;
        timer.start();
        bool started = false;
        while (timer.elapsed() < 3000) {
            if (saveHasStarted(sandbox.path(), target)) {
                started = true;
                break;
            }
            QTest::qWait(1);
        }
        QVERIFY2(started, "保存没有开始");
        if (delayMs > 0) {
            QTest::qWait(delayMs);
        }
        worker.process().kill();
        worker.process().waitForFinished(5000);

        // 目标文件：要么根本没出现，要么是一个**完整**的文件。
        if (QFileInfo::exists(target)) {
            QFile file(target);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray bytes = file.readAll();
            file.close();
            // 完整的 PDF 一定以 %%EOF 收尾；写了一半的文件没有。
            QVERIFY2(QByteArray(bytes).trimmed().endsWith("%%EOF"),
                     qPrintable(QStringLiteral("在 %1 毫秒处杀掉解析进程后，"
                                               "目标文件成了半截的。")
                                    .arg(delayMs)));
        }

        // 被打开的那份文件一个字节都不能变：保存写的是另一个路径。
        QFile sourceFile(opened);
        QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        const QByteArray sourceBytes = sourceFile.readAll();
        sourceFile.close();
        QVERIFY2(QByteArray(sourceBytes).trimmed().endsWith("%%EOF"),
                 qPrintable(QStringLiteral("在 %1 毫秒处杀掉解析进程后，"
                                           "被打开的文件被写坏了。")
                                .arg(delayMs)));
    }

#ifdef Q_OS_WIN
    // 定时器精度由 TimerResolutionGuard 负责还原。
#endif
}

void SafeDocumentTests::savePreservesDocumentContent() {
    // 保存不能把文档弄坏。
    //
    // 实测：PDFium 重写文件时内容是保住的（5 MB 的内嵌字体还在，
    // 保存后的文件 5,015,769 字节，差 239 字节是重新序列化的开销）。
    //
    // 这条钉住一个具体的坏实现：直接截断写目标文件时，PDFium 还会从
    // 已经被截断的源文件里继续读对象 → 5 MB 的文档被存成 2 KB（字体没了）。
    const QString source = fixturePath(QStringLiteral("cjk.pdf"));
    if (!QFileInfo::exists(source)) {
        QSKIP("没有大一点的夹具，跳过");
    }

    QTemporaryDir sandbox;
    QVERIFY(sandbox.isValid());
    const QString target = sandbox.filePath(QStringLiteral("cjk-saved.pdf"));

    ParseWorkerProcess worker;
    QVERIFY2(worker.start(uniqueServerName("aipdf-parse-keep")),
             "解析进程没有接受连接");

    QJsonObject openPayload;
    openPayload.insert(QStringLiteral("path"), source);
    const QJsonObject opened = worker.request("pdf.open", openPayload);
    QVERIFY(opened.value("ok").toBool());

    QJsonObject savePayload;
    savePayload.insert(QStringLiteral("path"), target);
    const QJsonObject saved = worker.request("pdf.save", savePayload);
    QVERIFY2(saved.value("ok").toBool(),
             qPrintable(saved.value("error").toObject().value("message").toString()));

    QVERIFY(QFileInfo::exists(target));
    // 存出来的文件不能小得离谱（丢内容就是这个表现）。
    QVERIFY2(QFileInfo(target).size() * 2 >= QFileInfo(source).size(),
             qPrintable(QStringLiteral("源文件 %1 字节，存出来只有 %2 字节：内容被丢了。")
                            .arg(QFileInfo(source).size())
                            .arg(QFileInfo(target).size())));

    // 存出来的文件还能打开，页数一样。
    ParseWorkerProcess reopened;
    QVERIFY2(reopened.start(uniqueServerName("aipdf-parse-keep2")),
             "解析进程没有接受连接");
    QJsonObject reopenPayload;
    reopenPayload.insert(QStringLiteral("path"), target);
    const QJsonObject reopenResponse = reopened.request("pdf.open", reopenPayload);
    QVERIFY2(reopenResponse.value("ok").toBool(),
             qPrintable(reopenResponse.value("error").toObject().value("message").toString()));
    QCOMPARE(reopenResponse.value("payload").toObject().value("pageCount").toInt(),
             opened.value("payload").toObject().value("pageCount").toInt());

    reopened.process().kill();
    reopened.process().waitForFinished(3000);
    worker.process().kill();
    worker.process().waitForFinished(3000);
}

void SafeDocumentTests::mainProcessNeverHoldsDocumentBytes() {
    // 第一层：主进程侧的公开 API 里不能有任何返回文档字节的东西。
    const QString headerPath =
        QDir(repoRootPath()).filePath(QStringLiteral("src/app/controllers/SafeDocumentController.h"));
    if (!QFileInfo::exists(headerPath)) {
        QSKIP("找不到源码目录，跳过源码级检查");
    }
    QFile header(headerPath);
    QVERIFY(header.open(QIODevice::ReadOnly));
    const QString headerText = QString::fromUtf8(header.readAll());
    header.close();
    QVERIFY2(!headerText.contains(QStringLiteral("QByteArray")),
             "主进程侧的公开 API 不能返回文档字节（接口里出现了 QByteArray）");

    // 第二层：主进程拿到的是渲染出来的图片，不是 PDF 字节。
    aipdf::app::SafeDocumentController controller;
    controller.setWorkerExecutable(parseWorkerExecutablePath());

    QString error;
    QVERIFY2(controller.open(fixturePath(QStringLiteral("normal.pdf")), QString(), &error),
             qPrintable(error));
    const QString imagePath = controller.renderPage(0, 1.0, &error);
    QVERIFY2(!imagePath.isEmpty(), qPrintable(error));

    QFile image(imagePath);
    QVERIFY(image.open(QIODevice::ReadOnly));
    const QByteArray head = image.read(8);
    image.close();
    QVERIFY2(head.startsWith(QByteArray("\x89PNG", 4)), "主进程拿到的应当是渲染出来的图片");
    QVERIFY(!head.startsWith(QByteArray("%PDF")));

    controller.close();
}

QTEST_MAIN(SafeDocumentTests)
#include "SafeDocumentTests.moc"
