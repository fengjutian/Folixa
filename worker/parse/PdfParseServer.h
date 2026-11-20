#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QTemporaryDir>

// FPDFPage_GetRotation 住在 fpdf_edit.h 里（不是 fpdfview.h）。
#include <fpdf_edit.h>
#include <fpdfview.h>

class QLocalSocket;

namespace aipdf::worker::parse {

// 隔离解析进程的服务端：PDF 在这里被打开、被渲染。
//
// 为什么必须独立进程：畸形 PDF 触发的解析器缺陷（越界读、栈溢出、死循环）
// 不应该发生在编辑器主进程里——主进程崩了，整个会话就没了。
// 这里只监听本地 socket：不持有 API 凭据，也不发任何网络请求。
//
// 主进程拿到的只有页数、页面尺寸和渲染出来的 PNG 路径，永远拿不到 PDF 字节。
class PdfParseServer final : public QObject {
    Q_OBJECT

public:
    explicit PdfParseServer(QString serverName, QObject* parent = nullptr);
    ~PdfParseServer() override;

    // 监听本地 socket 并初始化 PDFium。失败返回 false。
    bool start();

private:
    void acceptConnection();
    void readFrames();
    void handleMessage(QLocalSocket* socket, const QJsonObject& request);
    void handleOpen(const QString& requestId, const QJsonObject& payload);
    void handleRender(const QString& requestId, const QJsonObject& payload);
    void handleClose(const QString& requestId);
    void writeFrame(const QJsonObject& frame);
    // 关掉当前文档：释放 PDFium 句柄与已渲染的临时图片。
    void closeDocument();
    // 渲染输出目录（进程退出时整块删掉）。
    QString renderDirectory() const;
    // PDFium 打开失败的原因 → 错误码与中文说明。
    static QString openErrorCode(unsigned long pdfiumError, bool passwordProvided);
    static QString openErrorMessage(unsigned long pdfiumError, bool passwordProvided);

    QString serverName_;
    QLocalServer server_;
    QLocalSocket* client_ = nullptr;
    QByteArray receiveBuffer_;
    FPDF_DOCUMENT document_ = nullptr;
    QJsonArray pageMeta_;
    QTemporaryDir renderDir_;
    bool pdfiumInitialized_ = false;
};

}  // namespace aipdf::worker::parse
