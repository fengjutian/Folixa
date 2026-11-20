#include "PdfParseServer.h"

#include "ipc/IpcProtocol.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include <filesystem>
#include <system_error>
#include <utility>

namespace aipdf::worker::parse {
namespace {

// 单页渲染的像素上限：畸形页面尺寸（例如 1e9 pt 的 MediaBox）不能把内存吃光。
constexpr int kMaxPixelDimension = 4096;
// 单页渲染的最长耗时。PDFium 的渲染是同步调用、没法抢占，
// 所以只能在渲染结束后判定：超时就当失败回给主进程，而不是假装成功。
constexpr qint64 kRenderTimeoutMs = 10'000;
// 允许的缩放范围。
constexpr double kMinScale = 0.05;
constexpr double kMaxScale = 8.0;
// 渲染文件名模板：page-0001.png（第 1 页）。
constexpr auto kRenderFileTemplate = "page-%1.png";
// 读 PDFium 的 UTF-16 字符串时的缓冲区上限（单位是 UTF-16 码元）。
constexpr int kMaxWideUnits = 1024;
// 探测「这个文件是不是还要口令」时用的假口令。
// 用一个绝不会有人用的口令去试：能打开说明根本不需要口令。
constexpr auto kEncryptionProbePassword = "aipdf-encryption-probe";

// PDFium 的 FPDF_FILEWRITE 结构里**没有**自定义数据字段，
// 只能用一个进程级指针把写入目标带进去。
// 这个进程只有一个线程、一次只保存一份文档，所以这样是安全的。
QFile* g_saveTarget = nullptr;

int writeToFile(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
    Q_UNUSED(self);
    if (g_saveTarget == nullptr) {
        return 0;
    }
    const qint64 written = g_saveTarget->write(static_cast<const char*>(data),
                                               static_cast<qint64>(size));
    return written == static_cast<qint64>(size) ? 1 : 0;
}

// 读 PDFium 的 UTF-16LE 字符串（按「需要的字节数」裁剪缓冲区，
// 不能直接拿返回值当长度，否则超长字符串会越界读栈）。
QString readWideString(const std::function<unsigned long(unsigned short*, unsigned long)>& reader) {
    unsigned short buffer[kMaxWideUnits] = {0};
    const unsigned long length = reader(buffer, static_cast<unsigned long>(sizeof(buffer)));
    if (length <= 2) {
        // 返回值 2 表示 "\0\0"：空字符串。
        return {};
    }
    const int available = static_cast<int>(length / 2) - 1;
    const int count = qBound(0, available, kMaxWideUnits - 1);
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer), count);
}

// 表单域类型 → 稳定的字符串名（主进程按名字渲染控件，别写死数字）。
QString formFieldTypeName(int type) {
    switch (type) {
        case FPDF_FORMFIELD_PUSHBUTTON:
            return QStringLiteral("button");
        case FPDF_FORMFIELD_CHECKBOX:
            return QStringLiteral("checkbox");
        case FPDF_FORMFIELD_RADIOBUTTON:
            return QStringLiteral("radio");
        case FPDF_FORMFIELD_COMBOBOX:
            return QStringLiteral("combo");
        case FPDF_FORMFIELD_LISTBOX:
            return QStringLiteral("list");
        case FPDF_FORMFIELD_TEXTFIELD:
            return QStringLiteral("text");
        case FPDF_FORMFIELD_SIGNATURE:
            return QStringLiteral("signature");
        default:
            return QStringLiteral("unknown");
    }
}

// 探一下这个文件要不要口令才能打开，以及给的口令对不对。
enum class EncryptionProbe {
    NotEncrypted,    // 不需要口令就能打开
    PasswordWorks,   // 需要口令，给的口令是对的
    PasswordWrong,   // 需要口令，给的口令不对
};

bool canOpenWith(const QString& path, const QByteArray& password) {
    const QByteArray pathUtf8 = path.toUtf8();
    FPDF_DOCUMENT probe = FPDF_LoadDocument(pathUtf8.constData(), password.constData());
    if (probe == nullptr) {
        return false;
    }
    FPDF_CloseDocument(probe);
    return true;
}

// 探加密：先用给的口令试，再用假口令试。
// 两次都能打开 → 根本不需要口令；只有给的口令能开 → 口令是对的；
// 两次都开不了 → 文件确实要口令，而给的口令不对。
EncryptionProbe probeEncryption(const QString& path, const QString& password) {
    if (canOpenWith(path, password.toUtf8())) {
        if (canOpenWith(path, kEncryptionProbePassword)) {
            return EncryptionProbe::NotEncrypted;
        }
        return EncryptionProbe::PasswordWorks;
    }
    if (!canOpenWith(path, kEncryptionProbePassword)) {
        return EncryptionProbe::PasswordWrong;
    }
    return EncryptionProbe::NotEncrypted;
}

// 原子替换：同目录 rename 覆盖。
// 中途失败时目标文件还是原来那个，不会卡在「写了一半」的状态。
bool replaceFile(const QString& tempPath, const QString& targetPath, QString* error) {
    std::error_code code;
    std::filesystem::rename(std::filesystem::path(tempPath.toStdWString()),
                            std::filesystem::path(targetPath.toStdWString()),
                            code);
    if (!code) {
        return true;
    }
    *error = QString::fromStdString(code.message().empty()
                                        ? std::string("无法替换目标文件。")
                                        : code.message());
    return false;
}

}  // namespace

PdfParseServer::PdfParseServer(QString serverName, QObject* parent)
    : QObject(parent), serverName_(std::move(serverName)) {
    connect(&server_, &QLocalServer::newConnection, this, &PdfParseServer::acceptConnection);
}

PdfParseServer::~PdfParseServer() {
    closeDocument();
    if (server_.isListening()) {
        server_.close();
    }
    if (pdfiumInitialized_) {
        FPDF_DestroyLibrary();
        pdfiumInitialized_ = false;
    }
}

bool PdfParseServer::start() {
    if (!pdfiumInitialized_) {
        FPDF_InitLibrary();
        pdfiumInitialized_ = true;
    }
    // 上一次异常退出可能留下同名的 socket 文件：先清掉再监听。
    QLocalServer::removeServer(serverName_);
    return server_.listen(serverName_);
}

void PdfParseServer::acceptConnection() {
    while (server_.hasPendingConnections()) {
        QLocalSocket* candidate = server_.nextPendingConnection();
        if (client_ != nullptr) {
            // 一次只服务一个客户端：多出来的直接踢掉，免得两套请求互相踩。
            candidate->disconnectFromServer();
            candidate->deleteLater();
            continue;
        }
        client_ = candidate;
        connect(client_, &QLocalSocket::readyRead, this, &PdfParseServer::readFrames);
        connect(client_, &QLocalSocket::disconnected, this, [this] {
            // 客户端没了就收尾：文档句柄与临时图片不能一直挂着。
            closeDocument();
            client_->deleteLater();
            client_ = nullptr;
            receiveBuffer_.clear();
        });
    }
}

void PdfParseServer::readFrames() {
    if (client_ == nullptr) {
        return;
    }
    receiveBuffer_.append(client_->readAll());
    if (receiveBuffer_.size() > ipc::kMaximumFrameBytes) {
        receiveBuffer_.clear();
        client_->write(ipc::encodeFrame(ipc::makeError(
            {}, "frame_too_large", "IPC 帧超过 1 MiB 上限。")));
        client_->disconnectFromServer();
        return;
    }

    qsizetype newline = -1;
    while ((newline = receiveBuffer_.indexOf('\n')) >= 0) {
        const QByteArray frame = receiveBuffer_.left(newline);
        receiveBuffer_.remove(0, newline + 1);
        if (frame.isEmpty()) {
            continue;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(frame, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            client_->write(ipc::encodeFrame(ipc::makeError(
                {}, "invalid_json", "请求必须是 JSON 对象。")));
            continue;
        }
        handleMessage(client_, document.object());
    }
}

void PdfParseServer::writeFrame(const QJsonObject& frame) {
    if (client_ == nullptr) {
        return;
    }
    client_->write(ipc::encodeFrame(frame));
}

void PdfParseServer::handleMessage(QLocalSocket* socket, const QJsonObject& request) {
    const QString requestId = request.value("request_id").toString();
    if (request.value("protocol_version").toInt(-1) != ipc::kProtocolVersion) {
        socket->write(ipc::encodeFrame(ipc::makeError(
            requestId, "protocol_version_unsupported", "不支持的 IPC 协议版本。")));
        return;
    }

    const QString operation = request.value("operation").toString();
    const QJsonObject payload = request.value("payload").toObject();

    if (operation == "system.health") {
        socket->write(ipc::encodeFrame(ipc::makeResult(
            requestId,
            {{"status", "ready"},
             {"protocol_version", ipc::kProtocolVersion},
             {"worker_version", QCoreApplication::applicationVersion()},
             {"document_open", document_ != nullptr}})));
        return;
    }
    if (operation == "pdf.open") {
        handleOpen(requestId, payload);
        return;
    }
    if (operation == "pdf.render") {
        handleRender(requestId, payload);
        return;
    }
    if (operation == "pdf.edit") {
        handleEdit(requestId, payload);
        return;
    }
    if (operation == "pdf.save") {
        handleSave(requestId, payload);
        return;
    }
    if (operation == "pdf.close") {
        handleClose(requestId);
        return;
    }
    if (operation == "system.shutdown") {
        socket->write(ipc::encodeFrame(ipc::makeResult(requestId, {{"status", "stopping"}})));
        socket->flush();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        return;
    }
    socket->write(ipc::encodeFrame(ipc::makeError(
        requestId, "operation_not_supported", "不支持的操作。")));
}

void PdfParseServer::handleOpen(const QString& requestId, const QJsonObject& payload) {
    const QString path = payload.value("path").toString();
    // 口令只在这一行被读出来用：它不会进日志，也不会进任何错误消息。
    const QString password = payload.value("password").toString();

    if (path.isEmpty()) {
        writeFrame(ipc::makeError(requestId, "invalid_request", "缺少文件路径。"));
        return;
    }

    // 已经有文档了：先关掉旧的，免得句柄和临时图片越积越多。
    closeDocument();

    const QByteArray pathUtf8 = path.toUtf8();
    const QByteArray passwordUtf8 = password.toUtf8();
    FPDF_DOCUMENT document = FPDF_LoadDocument(pathUtf8.constData(), passwordUtf8.constData());
    if (document == nullptr) {
        // 失败原因必须在这里分清楚：要密码 / 口令不对 / 文件坏了，
        // 主进程要靠这个告诉用户下一步做什么。
        const unsigned long pdfiumError = FPDF_GetLastError();
        writeFrame(ipc::makeError(requestId,
                                  openErrorCode(pdfiumError, !password.isEmpty()),
                                  openErrorMessage(pdfiumError, !password.isEmpty())));
        return;
    }
    document_ = document;
    // 覆盖保存之后要把这份文档重新打开，所以得记住路径与口令。
    sourcePath_ = path;
    sourcePassword_ = password;
    editState_ = EditState::Clean;

    rebuildPageMeta();

    writeFrame(ipc::makeResult(requestId,
                                {{"pageCount", pageCount_},
                                 {"pages", pageMeta_},
                                 // 表单域清单：主进程据此渲染表单控件。
                                 {"fields", collectFormFields()},
                                 {"openedWithPassword", !password.isEmpty()}}));
}

void PdfParseServer::handleRender(const QString& requestId, const QJsonObject& payload) {
    if (document_ == nullptr) {
        writeFrame(ipc::makeError(requestId, "not_open", "还没有打开文档，无法渲染。"));
        return;
    }

    const int pageIndex = payload.value("pageIndex").toInt(-1);
    double scale = payload.value("scale").toDouble(1.0);
    if (scale < kMinScale || scale > kMaxScale) {
        writeFrame(ipc::makeError(
            requestId, "invalid_scale",
            QStringLiteral("缩放倍率必须在 %1 到 %2 之间。")
                .arg(kMinScale, 0, 'f', 2)
                .arg(kMaxScale, 0, 'f', 2)));
        return;
    }

    const int pageCount = FPDF_GetPageCount(document_);
    if (pageIndex < 0 || pageIndex >= pageCount) {
        writeFrame(ipc::makeError(requestId, "page_out_of_range", "页码超出文档范围。"));
        return;
    }

    FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
    if (page == nullptr) {
        writeFrame(ipc::makeError(requestId, "page_load_failed", "这一页打不开。"));
        return;
    }

    const double widthPt = FPDF_GetPageWidthF(page);
    const double heightPt = FPDF_GetPageHeightF(page);
    int pixelWidth = static_cast<int>(widthPt * scale + 0.5);
    int pixelHeight = static_cast<int>(heightPt * scale + 0.5);
    if (pixelWidth <= 0 || pixelHeight <= 0) {
        FPDF_ClosePage(page);
        writeFrame(ipc::makeError(requestId, "render_failed", "页面尺寸算不出来，无法渲染。"));
        return;
    }
    if (pixelWidth > kMaxPixelDimension || pixelHeight > kMaxPixelDimension) {
        FPDF_ClosePage(page);
        writeFrame(ipc::makeError(
            requestId, "render_too_large",
            QStringLiteral("渲染尺寸 %1×%2 像素超过上限，请调小缩放倍率。")
                .arg(pixelWidth)
                .arg(pixelHeight)));
        return;
    }

    QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB32);
    image.fill(Qt::white);
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(pixelWidth,
                                              pixelHeight,
                                              FPDFBitmap_BGRA,
                                              reinterpret_cast<unsigned char*>(image.bits()),
                                              pixelWidth * 4);
    if (bitmap == nullptr) {
        FPDF_ClosePage(page);
        writeFrame(ipc::makeError(requestId, "render_failed", "位图创建失败，无法渲染。"));
        return;
    }
    FPDFBitmap_FillRect(bitmap, 0, 0, pixelWidth, pixelHeight, 0xFFFFFFFF);

    QElapsedTimer timer;
    timer.start();
    // rotate=0：页面自带的 /Rotate 已经套在尺寸上，这里不能再转一次。
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, pixelWidth, pixelHeight, 0, FPDF_ANNOT);
    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);

    if (timer.elapsed() > kRenderTimeoutMs) {
        writeFrame(ipc::makeError(
            requestId, "render_timeout",
            QStringLiteral("渲染耗时 %1 毫秒，超过上限，已放弃这一页。").arg(timer.elapsed())));
        return;
    }

    // 图片绝不进 IPC 帧（单帧上限 1 MiB）：写成 PNG 文件，只把路径回给主进程。
    const QString fileName = QString::fromLatin1(kRenderFileTemplate)
                                 .arg(pageIndex + 1, 4, 10, QLatin1Char('0'));
    const QString target = QDir(renderDirectory()).filePath(fileName);
    if (!image.save(target, "PNG")) {
        writeFrame(ipc::makeError(requestId, "write_failed", "渲染结果写盘失败。"));
        return;
    }

    writeFrame(ipc::makeResult(requestId,
                               {{"imagePath", target},
                                {"widthPx", pixelWidth},
                                {"heightPx", pixelHeight}}));
}

void PdfParseServer::handleEdit(const QString& requestId, const QJsonObject& payload) {
    if (document_ == nullptr) {
        writeFrame(ipc::makeError(requestId, "not_open", "还没有打开文档，无法编辑。"));
        return;
    }

    const QJsonValue instructionsValue = payload.value("instructions");
    if (!instructionsValue.isArray()) {
        markEditInconsistent();
        writeFrame(ipc::makeError(requestId, "invalid_request", "编辑指令必须是数组（instructions）。"));
        return;
    }
    const QJsonArray instructions = instructionsValue.toArray();
    if (instructions.isEmpty()) {
        writeFrame(ipc::makeError(requestId, "invalid_request", "没有要执行的编辑指令。"));
        return;
    }

    // 第一步：把全部指令校验一遍。任何一条不合法就**一条都不做**——
    // 半改的文档既没法保存也没法说清楚，不能让它发生。
    QList<EditInstruction> plans;
    for (const QJsonValue& value : instructions) {
        if (!value.isObject()) {
            markEditInconsistent();
            writeFrame(ipc::makeError(requestId, "invalid_instruction", "编辑指令必须是 JSON 对象。"));
            return;
        }
        EditInstruction plan;
        QString reason;
        if (!parseInstruction(value.toObject(), &plan, &reason)) {
            markEditInconsistent();
            writeFrame(ipc::makeError(requestId, "invalid_instruction", reason));
            return;
        }
        if (plan.kind == EditInstruction::Kind::SetFormFieldValue && !hasFormField(plan.fieldName)) {
            markEditInconsistent();
            writeFrame(ipc::makeError(requestId, "field_not_found",
                                      QStringLiteral("找不到名为 %1 的表单域。").arg(plan.fieldName)));
            return;
        }
        plans.append(plan);
    }

    // 第二步：校验过了，一条一条执行。
    for (const EditInstruction& plan : plans) {
        QString reason;
        if (!applyInstruction(plan, &reason)) {
            markEditInconsistent();
            writeFrame(ipc::makeError(requestId, "edit_failed", reason));
            return;
        }
    }

    editState_ = EditState::Dirty;
    writeFrame(ipc::makeResult(requestId,
                               {{"applied", plans.size()},
                                // 文档里现在有多少条批注：确认「批注真的加上了」。
                                {"annotationCount", totalAnnotationCount()},
                                // 回一份最新的字段清单：主进程据此刷新界面。
                                {"fields", collectFormFields()}}));
}

void PdfParseServer::handleSave(const QString& requestId, const QJsonObject& payload) {
    if (document_ == nullptr) {
        writeFrame(ipc::makeError(requestId, "not_open", "还没有打开文档，无法保存。"));
        return;
    }
    if (editState_ == EditState::Inconsistent) {
        // 上一次编辑没有成功：文档状态不可信。
        // 这时候写盘就是「产出半改的文件」，直接拒绝。
        writeFrame(ipc::makeError(
            requestId, "save_blocked_by_failed_edit",
            "上一次编辑没有成功，文档状态不可信，为避免产出半改的文件已拒绝保存。"
            "请重新打开文档后再试。"));
        return;
    }

    const QString target = payload.value("path").toString();
    if (target.isEmpty()) {
        writeFrame(ipc::makeError(requestId, "invalid_request", "缺少保存目标路径。"));
        return;
    }
    const QFileInfo targetInfo(target);
    const QString targetPath = targetInfo.absoluteFilePath();
    const QString directory = targetInfo.absolutePath();
    if (!QDir(directory).exists()) {
        writeFrame(ipc::makeError(requestId, "invalid_request", "保存目标目录不存在。"));
        return;
    }
    const bool inPlace = !sourcePath_.isEmpty() &&
                         QFileInfo(sourcePath_).absoluteFilePath().compare(
                             targetPath, Qt::CaseInsensitive) == 0;

    // 第一步：先写同目录的临时文件。写坏了也只是临时文件坏了，目标文件一点没动。
    const QString tempPath =
        QDir(directory).filePath(QStringLiteral(".%1.aipdf-tmp-%2")
                                     .arg(targetInfo.fileName(),
                                          QUuid::createUuid().toString(QUuid::WithoutBraces)));
    // 上一次保存如果半路被杀死了，目标目录里会留下临时文件：
    // 十分钟以前的那种清掉，别让目录越积越脏。
    // （十分钟这个门槛是为了不误删别的进程正在写的临时文件。）
    const QString tempPrefix = QStringLiteral(".%1.aipdf-tmp-").arg(targetInfo.fileName());
    const QDateTime staleBefore = QDateTime::currentDateTime().addSecs(-600);
    const QStringList existing = QDir(directory).entryList(QDir::Files | QDir::Hidden);
    for (const QString& entry : existing) {
        if (!entry.startsWith(tempPrefix)) {
            continue;
        }
        const QFileInfo info(QDir(directory).filePath(entry));
        if (info.lastModified() < staleBefore) {
            QFile::remove(info.absoluteFilePath());
        }
    }
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        writeFrame(ipc::makeError(requestId, "write_failed",
                                  "写不了临时文件：" + tempFile.errorString()));
        return;
    }
    g_saveTarget = &tempFile;
    FPDF_FILEWRITE fileWrite = {};
    fileWrite.version = 1;
    fileWrite.WriteBlock = &writeToFile;
    const FPDF_BOOL saved = FPDF_SaveAsCopy(document_, &fileWrite, 0);
    g_saveTarget = nullptr;
    tempFile.flush();
    const qint64 bytes = tempFile.size();
    const QString writeError = tempFile.errorString();
    tempFile.close();
    if (!saved) {
        QFile::remove(tempPath);
        writeFrame(ipc::makeError(requestId, "write_failed",
                                  "解析器保存失败：" + writeError));
        return;
    }

    // 第二步：探一下保存出来的文件还要不要口令（加密还在不在）。
    // 用**原来那个口令**去探：这样才能知道「同一个口令还能不能打开」。
    const EncryptionProbe savedProbe = probeEncryption(tempPath, sourcePassword_);
    // 源文件也要探一次（替换之后就探不到「原来」的状态了）。
    // 另存到别处时源文件没被碰过，探出来的结果照样成立。
    const EncryptionProbe sourceProbe = probeEncryption(sourcePath_, sourcePassword_);
    const bool sourceEncrypted = sourceProbe != EncryptionProbe::NotEncrypted;
    const bool stillEncrypted = savedProbe != EncryptionProbe::NotEncrypted;
    const bool samePasswordWorks = savedProbe == EncryptionProbe::PasswordWorks;

    // 第三步：覆盖原文件之前，必须先让 Windows 放开这个文件的句柄——
    // PDFium 还开着它的时候，这个文件换不掉。
    if (inPlace) {
        releaseDocument();
    }

    // 第四步：原子替换。中途失败时目标文件还是原来那个。
    QString replaceError;
    if (!replaceFile(tempPath, targetPath, &replaceError)) {
        QFile::remove(tempPath);
        if (inPlace) {
            // 替换失败说明源文件没被动过：把文档重新打开，恢复可编辑状态。
            reopenDocument(sourcePath_, sourcePassword_);
        }
        writeFrame(ipc::makeError(requestId, "write_failed",
                                  "写回目标文件失败：" + replaceError));
        return;
    }

    // 第五步：覆盖保存之后要把文档重新打开：改动还在里面，才能接着改。
    bool documentOpen = true;
    if (inPlace) {
        documentOpen = reopenDocument(targetPath, sourcePassword_);
        if (!documentOpen) {
            // 口令不对（保存后的文件可能已经不需要口令了）：再不带口令试一次。
            documentOpen = reopenDocument(targetPath, QString());
        }
    }
    editState_ = EditState::Clean;

    QJsonArray warnings;
    if (sourceEncrypted && !stillEncrypted) {
        // 实测结论（这个 PDFium 构建）：加密字典会被保留；但万一哪次构建不保留，
        // 这里如实报出来，不能让用户以为加密还在。
        warnings.append("encryption_dropped");
    }
    if (sourceEncrypted && stillEncrypted && !samePasswordWorks) {
        warnings.append("encryption_password_changed");
    }
    if (sourceEncrypted && stillEncrypted && samePasswordWorks) {
        // 实测结论：这个 PDFium 构建保留加密字典，同一个口令仍然有效。
        warnings.append("encryption_preserved");
    }
    // 实测结论：重写文件后，签名域与 /ByteRange 还在文件里，但字节范围已经
    // 对不上新文件——签名不再有效（不是「被删掉」，是「不再算数」）。
    warnings.append("signatures_invalidated");
    // 实测结论：增量更新的历史版本被合并成一份（原来 2 份 %%EOF 的文件
    // 保存后只剩 1 份）。
    warnings.append("incremental_history_dropped");
    // 实测结论：整个文件被重新序列化（对象编号、结构都可能变）。
    warnings.append("structure_rewritten");

    writeFrame(ipc::makeResult(requestId,
                               {{"saved", true},
                                {"path", targetPath},
                                {"bytes", bytes},
                                {"documentOpen", documentOpen},
                                {"encrypted", stillEncrypted},
                                {"sourceEncrypted", sourceEncrypted},
                                {"samePasswordWorks", samePasswordWorks},
                                {"warnings", warnings}}));
}

void PdfParseServer::handleClose(const QString& requestId) {
    closeDocument();
    writeFrame(ipc::makeResult(requestId, {{"closed", true}}));
    // 回执发出去之后本进程就没有存在的必要了：
    // 留着它只会让用户看到「还有一个解析进程在跑」，却什么也干不了。
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
}

void PdfParseServer::rebuildPageMeta() {
    QJsonArray pages;
    const int pageCount = FPDF_GetPageCount(document_);
    for (int index = 0; index < pageCount; ++index) {
        FPDF_PAGE page = FPDF_LoadPage(document_, index);
        if (page == nullptr) {
            continue;
        }
        // 宽高是**已经套用页面 /Rotate 之后**的显示尺寸：
        // 主进程按它算像素尺寸才对得上。
        const double widthPt = FPDF_GetPageWidthF(page);
        const double heightPt = FPDF_GetPageHeightF(page);
        const int rotation = FPDFPage_GetRotation(page) * 90;
        // 这一页上有多少批注：主进程与测试据此确认「批注真的加上了」。
        const int annotationCount = FPDFPage_GetAnnotCount(page);
        FPDF_ClosePage(page);
        pages.append(QJsonObject{
            {"widthPt", widthPt},
            {"heightPt", heightPt},
            {"rotation", rotation},
            {"annotationCount", annotationCount},
        });
    }
    pageMeta_ = pages;
    pageCount_ = pageCount;
}

void PdfParseServer::closeDocument() {
    releaseDocument();
    pageMeta_ = QJsonArray();
    pageCount_ = 0;
    sourcePath_.clear();
    sourcePassword_.clear();
    editState_ = EditState::Clean;
    // 已渲染的 PNG 没有保留价值：清掉，别让临时目录越滚越大。
    if (renderDir_.isValid()) {
        const QString pages = QDir(renderDir_.path()).filePath(QStringLiteral("pages"));
        QDir(pages).removeRecursively();
        QDir().mkpath(pages);
    }
}

void PdfParseServer::releaseDocument() {
    if (document_ != nullptr) {
        FPDF_CloseDocument(document_);
        document_ = nullptr;
    }
    // 表单环境跟着文档一起作废。这个 PDFium 构建在退出表单环境时会崩，
    // 所以这里只丢指针、不调用 FPDFDOC_ExitFormFillEnvironment（每个文档泄漏一个）。
    form_ = nullptr;
}

bool PdfParseServer::reopenDocument(const QString& path, const QString& password) {
    releaseDocument();
    const QByteArray pathUtf8 = path.toUtf8();
    const QByteArray passwordUtf8 = password.toUtf8();
    FPDF_DOCUMENT document = FPDF_LoadDocument(pathUtf8.constData(), passwordUtf8.constData());
    if (document == nullptr) {
        return false;
    }
    document_ = document;
    sourcePath_ = path;
    sourcePassword_ = password;
    editState_ = EditState::Clean;
    rebuildPageMeta();
    return true;
}

void PdfParseServer::markEditInconsistent() {
    // 编辑失败之后文档状态不可信：禁止保存，直到重新打开。
    editState_ = EditState::Inconsistent;
}

QString PdfParseServer::renderDirectory() const {
    if (!renderDir_.isValid()) {
        return {};
    }
    const QString pages = QDir(renderDir_.path()).filePath(QStringLiteral("pages"));
    QDir().mkpath(pages);
    return pages;
}

FPDF_FORMHANDLE PdfParseServer::formHandle() const {
    if (form_ != nullptr || document_ == nullptr) {
        return form_;
    }
    formInfo_ = FPDF_FORMFILLINFO{};
    formInfo_.version = 2;
    form_ = FPDFDOC_InitFormFillEnvironment(document_, &formInfo_);
    return form_;
}

void PdfParseServer::forEachFormField(
    const std::function<void(FPDF_ANNOTATION, int, int, const QString&)>& visit) const {
    if (document_ == nullptr) {
        return;
    }
    FPDF_FORMHANDLE form = formHandle();
    if (form == nullptr) {
        return;
    }
    const int pageCount = FPDF_GetPageCount(document_);
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
        if (page == nullptr) {
            continue;
        }
        const int annotCount = FPDFPage_GetAnnotCount(page);
        for (int index = 0; index < annotCount; ++index) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, index);
            if (annot == nullptr) {
                continue;
            }
            const int type = FPDFAnnot_GetFormFieldType(form, annot);
            if (type >= 0) {
                const QString name = readWideString([&](unsigned short* buffer, unsigned long size) {
                    return FPDFAnnot_GetFormFieldName(form, annot, buffer, size);
                });
                visit(annot, pageIndex, type, name);
            }
            FPDFPage_CloseAnnot(annot);
        }
        FPDF_ClosePage(page);
    }
}

bool PdfParseServer::hasFormField(const QString& name) const {
    bool found = false;
    forEachFormField([&](FPDF_ANNOTATION, int, int, const QString& fieldName) {
        if (fieldName == name) {
            found = true;
        }
    });
    return found;
}

int PdfParseServer::totalAnnotationCount() const {
    if (document_ == nullptr) {
        return 0;
    }
    int total = 0;
    const int pageCount = FPDF_GetPageCount(document_);
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
        if (page == nullptr) {
            continue;
        }
        total += FPDFPage_GetAnnotCount(page);
        FPDF_ClosePage(page);
    }
    return total;
}

QJsonArray PdfParseServer::collectFormFields() const {
    QJsonArray fields;
    FPDF_FORMHANDLE form = formHandle();
    if (form == nullptr) {
        return fields;
    }
    QSet<QString> seen;
    forEachFormField([&](FPDF_ANNOTATION annot, int pageIndex, int type, const QString& name) {
        if (name.isEmpty() || seen.contains(name)) {
            // 单选按钮的每个选项都是同一个字段名：只留一份。
            return;
        }
        seen.insert(name);

        QJsonArray options;
        const int optionCount = FPDFAnnot_GetOptionCount(form, annot);
        for (int index = 0; index < optionCount; ++index) {
            unsigned short buffer[256] = {0};
            const unsigned long length =
                FPDFAnnot_GetOptionLabel(form, annot, index, buffer, sizeof(buffer));
            if (length <= 2) {
                continue;
            }
            const int available = static_cast<int>(length / 2) - 1;
            const int count = qBound(0, available, 255);
            options.append(QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer), count));
        }

        const int flags = FPDFAnnot_GetFormFieldFlags(form, annot);
        QJsonObject field;
        field.insert("name", name);
        field.insert("type", formFieldTypeName(type));
        field.insert("value", readWideString([&](unsigned short* buffer, unsigned long size) {
            return FPDFAnnot_GetFormFieldValue(form, annot, buffer, size);
        }));
        field.insert("readOnly", (flags & FPDF_FORMFLAG_READONLY) != 0);
        field.insert("pageIndex", pageIndex);
        field.insert("options", options);
        fields.append(field);
    });
    return fields;
}

bool PdfParseServer::parseInstruction(const QJsonObject& instruction,
                                      EditInstruction* out,
                                      QString* error) {
    const QString type = instruction.value("type").toString();
    if (type == "setFormFieldValue") {
        out->kind = EditInstruction::Kind::SetFormFieldValue;
        const QString name = instruction.value("field").toString();
        if (name.isEmpty()) {
            *error = QStringLiteral("填表单域的指令没有给字段名。");
            return false;
        }
        out->fieldName = name;
        if (!instruction.contains("value")) {
            *error = QStringLiteral("填表单域的指令没有给新值。");
            return false;
        }
        if (!instruction.value("value").isString()) {
            *error = QStringLiteral("表单域的新值必须是字符串。");
            return false;
        }
        out->value = instruction.value("value").toString();
        return true;
    }

    if (type == "addAnnotation") {
        out->kind = EditInstruction::Kind::AddAnnotation;
        out->pageIndex = instruction.value("pageIndex").toInt(-1);
        const int pageCount = FPDF_GetPageCount(document_);
        if (out->pageIndex < 0 || out->pageIndex >= pageCount) {
            *error = QStringLiteral("加批注的指令里的页码超出文档范围。");
            return false;
        }
        const QString subtype = instruction.value("subtype").toString(QStringLiteral("text"));
        if (subtype == "text") {
            out->highlight = false;
        } else if (subtype == "highlight") {
            out->highlight = true;
        } else {
            *error = QStringLiteral("只支持 text（便签）与 highlight（高亮）两种批注。");
            return false;
        }
        const QJsonArray rect = instruction.value("rect").toArray();
        if (rect.size() != 4) {
            *error = QStringLiteral("批注矩形必须是四个数字：left, bottom, right, top。");
            return false;
        }
        double values[4] = {0.0, 0.0, 0.0, 0.0};
        for (int index = 0; index < 4; ++index) {
            if (!rect.at(index).isDouble()) {
                *error = QStringLiteral("批注矩形的四个坐标都必须是数字。");
                return false;
            }
            values[index] = rect.at(index).toDouble();
        }
        out->left = values[0];
        out->bottom = values[1];
        out->right = values[2];
        out->top = values[3];
        if (!(out->right > out->left) || !(out->top > out->bottom)) {
            *error = QStringLiteral("批注矩形必须是一个有效的矩形（左<右、下<上）。");
            return false;
        }
        if (!instruction.value("text").isString()) {
            *error = QStringLiteral("批注文字必须是字符串。");
            return false;
        }
        out->text = instruction.value("text").toString();
        return true;
    }

    *error = QStringLiteral("不认识的编辑指令类型：%1").arg(type);
    return false;
}

bool PdfParseServer::applyInstruction(const EditInstruction& instruction, QString* error) {
    if (instruction.kind == EditInstruction::Kind::SetFormFieldValue) {
        return assignFormFieldValue(instruction.fieldName, instruction.value, error);
    }
    return createAnnotation(instruction, error);
}

bool PdfParseServer::assignFormFieldValue(const QString& name,
                                          const QString& value,
                                          QString* error) {
    FPDF_FORMHANDLE form = formHandle();
    if (form == nullptr) {
        *error = QStringLiteral("这个文档没有可用的表单环境。");
        return false;
    }

    bool found = false;
    bool changed = false;
    QString failure;
    forEachFormField([&](FPDF_ANNOTATION annot, int, int type, const QString& fieldName) {
        if (found || fieldName != name) {
            return;
        }
        found = true;
        const std::u16string wide = value.toStdU16String();
        const auto* text = reinterpret_cast<const FPDF_WIDESTRING>(wide.c_str());
        // 勾选/单选改的是「外观状态」（/AS），文本与下拉改的是值（/V）。
        const bool isToggle = (type == FPDF_FORMFIELD_CHECKBOX ||
                                type == FPDF_FORMFIELD_RADIOBUTTON);
        const char* key = isToggle ? "AS" : "V";
        if (FPDFAnnot_SetStringValue(annot, key, text) == 0) {
            failure = QStringLiteral("字段 %1 的值写不进去。").arg(name);
            return;
        }
        if (isToggle) {
            // 顺带把 /V 也写一份：读取时两个都认。
            FPDFAnnot_SetStringValue(annot, "V", text);
        }
        changed = true;
    });

    if (!found) {
        *error = QStringLiteral("找不到名为 %1 的表单域。").arg(name);
        return false;
    }
    if (!changed) {
        *error = failure.isEmpty() ? QStringLiteral("字段 %1 的值写不进去。").arg(name)
                                   : failure;
        return false;
    }
    return true;
}

bool PdfParseServer::createAnnotation(const EditInstruction& instruction, QString* error) {
    FPDF_PAGE page = FPDF_LoadPage(document_, instruction.pageIndex);
    if (page == nullptr) {
        *error = QStringLiteral("这一页打不开，批注没加上。");
        return false;
    }

    FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(
        page, instruction.highlight ? FPDF_ANNOT_HIGHLIGHT : FPDF_ANNOT_TEXT);
    if (annot == nullptr) {
        FPDF_ClosePage(page);
        *error = QStringLiteral("这个解析器造不出这种批注。");
        return false;
    }

    FS_RECTF rect = {};
    rect.left = static_cast<float>(instruction.left);
    rect.right = static_cast<float>(instruction.right);
    rect.bottom = static_cast<float>(instruction.bottom);
    rect.top = static_cast<float>(instruction.top);
    FPDFAnnot_SetRect(annot, &rect);

    if (instruction.highlight) {
        // 高亮要四角点，否则渲染出来什么都没有。
        FS_QUADPOINTSF points = {};
        points.x1 = static_cast<float>(instruction.left);
        points.y1 = static_cast<float>(instruction.top);
        points.x2 = static_cast<float>(instruction.right);
        points.y2 = static_cast<float>(instruction.top);
        points.x3 = static_cast<float>(instruction.left);
        points.y3 = static_cast<float>(instruction.bottom);
        points.x4 = static_cast<float>(instruction.right);
        points.y4 = static_cast<float>(instruction.bottom);
        FPDFAnnot_AppendAttachmentPoints(annot, &points);
        FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, 255, 235, 60, 255);
    } else {
        FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, 255, 220, 80, 255);
    }

    if (!instruction.text.isEmpty()) {
        const std::u16string wide = instruction.text.toStdU16String();
        FPDFAnnot_SetStringValue(annot, "Contents",
                                  reinterpret_cast<const FPDF_WIDESTRING>(wide.c_str()));
    }

    FPDFPage_CloseAnnot(annot);
    FPDF_ClosePage(page);
    return true;
}

QString PdfParseServer::openErrorCode(unsigned long pdfiumError, bool passwordProvided) {
    switch (pdfiumError) {
        case FPDF_ERR_PASSWORD:
            // PDFium 对「要密码」和「口令不对」给的是同一个错误码：
            // 只能靠「这次有没有带口令」来区分。
            return passwordProvided ? QStringLiteral("password_incorrect")
                                    : QStringLiteral("password_required");
        case FPDF_ERR_FILE:
            return QStringLiteral("file_not_found");
        case FPDF_ERR_FORMAT:
            return QStringLiteral("invalid_pdf");
        case FPDF_ERR_SECURITY:
            return QStringLiteral("unsupported_security");
        default:
            return QStringLiteral("open_failed");
    }
}

QString PdfParseServer::openErrorMessage(unsigned long pdfiumError, bool passwordProvided) {
    switch (pdfiumError) {
        case FPDF_ERR_PASSWORD:
            return passwordProvided
                ? QStringLiteral("口令不对：这个 PDF 的密码与输入的不一致。")
                : QStringLiteral("这个 PDF 需要密码才能打开，请输入密码后重试。");
        case FPDF_ERR_FILE:
            return QStringLiteral("打不开这个文件：路径不存在或没有读取权限。");
        case FPDF_ERR_FORMAT:
            return QStringLiteral("这个文件不是有效的 PDF，或者已经损坏。");
        case FPDF_ERR_SECURITY:
            return QStringLiteral("这个 PDF 用了当前解析器不支持的加密方式。");
        default:
            return QStringLiteral("隔离解析进程无法打开这个文件。");
    }
}

}  // namespace aipdf::worker::parse
