#include "PdfParseServer.h"

#include "ipc/IpcProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

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
        FPDF_ClosePage(page);
        pages.append(QJsonObject{
            {"widthPt", widthPt},
            {"heightPt", heightPt},
            {"rotation", rotation},
        });
    }
    pageMeta_ = pages;

    writeFrame(ipc::makeResult(requestId, {{"pageCount", pageCount}, {"pages", pages}}));
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

void PdfParseServer::handleClose(const QString& requestId) {
    closeDocument();
    writeFrame(ipc::makeResult(requestId, {{"closed", true}}));
    // 回执发出去之后本进程就没有存在的必要了：
    // 留着它只会让用户看到「还有一个解析进程在跑」，却什么也干不了。
    // 回执发出去之后本进程就没有存在的必要了：
    // 留着它只会让用户看到「还有一个解析进程在跑」，却什么也干不了。
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
}

void PdfParseServer::closeDocument() {
    if (document_ != nullptr) {
        FPDF_CloseDocument(document_);
        document_ = nullptr;
    }
    pageMeta_ = QJsonArray();
    // 已渲染的 PNG 没有保留价值：清掉，别让临时目录越滚越大。
    if (renderDir_.isValid()) {
        const QString pages = QDir(renderDir_.path()).filePath(QStringLiteral("pages"));
        QDir(pages).removeRecursively();
        QDir().mkpath(pages);
    }
}

QString PdfParseServer::renderDirectory() const {
    if (!renderDir_.isValid()) {
        return {};
    }
    const QString pages = QDir(renderDir_.path()).filePath(QStringLiteral("pages"));
    QDir().mkpath(pages);
    return pages;
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
