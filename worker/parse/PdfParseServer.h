#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QTemporaryDir>

// FPDFPage_GetRotation 住在 fpdf_edit.h 里（不是 fpdfview.h）。
// fpdf_formfill.h 提供表单环境（读字段、填字段），fpdf_save.h 提供 FPDF_FILEWRITE。
#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_formfill.h>
#include <fpdf_save.h>
#include <fpdfview.h>

#include <functional>

class QLocalSocket;

namespace aipdf::worker::parse {

// 一条编辑指令（pdf.edit 载荷里的一个元素）。
//
// 指令是结构化 JSON，**不放媒体字节**：只放页号、矩形、文字这类小数据，
// 这样一条 IPC 帧永远远小于 1 MiB 的上限。
struct EditInstruction {
    enum class Kind {
        SetFormFieldValue,  // 填表单域
        AddAnnotation,      // 加批注
    };

    Kind kind = Kind::SetFormFieldValue;
    // 表单域按名字找。
    QString fieldName;
    QString value;
    // 批注落在哪一页、什么样式、什么位置。
    int pageIndex = -1;
    bool highlight = false;  // true=高亮，false=便签
    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
    double top = 0.0;
    QString text;
};

// 隔离解析进程的服务端：PDF 在这里被打开、被渲染、被编辑、被保存。
//
// 为什么必须独立进程：畸形 PDF 触发的解析器缺陷（越界读、栈溢出、死循环）
// 不应该发生在编辑器主进程里——主进程崩了，整个会话就没了。
// 这里只监听本地 socket：不持有 API 凭据，也不发任何网络请求。
//
// 主进程拿到的只有页数、页面尺寸、表单域清单、渲染出来的 PNG 路径，
// 永远拿不到 PDF 字节。
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
    void handleEdit(const QString& requestId, const QJsonObject& payload);
    void handleSave(const QString& requestId, const QJsonObject& payload);
    void handleClose(const QString& requestId);
    void writeFrame(const QJsonObject& frame);
    // 关掉当前文档：释放 PDFium 句柄与已渲染的临时图片。
    void closeDocument();
    // 只释放 PDFium 文档句柄（不动临时图片）。
    // 覆盖保存时要用：Windows 上文件被 PDFium 打开着就换不掉。
    void releaseDocument();
    // 重新打开一份文档（覆盖保存之后用）。
    bool reopenDocument(const QString& path, const QString& password);
    // 重新算一遍每一页的显示尺寸与旋转。
    void rebuildPageMeta();
    // 标记「上一次编辑失败了」：文档状态不可信，之后不许保存。
    void markEditInconsistent();
    // 渲染输出目录（进程退出时整块删掉）。
    QString renderDirectory() const;
    // PDFium 打开失败的原因 → 错误码与中文说明。
    static QString openErrorCode(unsigned long pdfiumError, bool passwordProvided);
    static QString openErrorMessage(unsigned long pdfiumError, bool passwordProvided);

    // 表单环境：读字段、填字段都要它。
    FPDF_FORMHANDLE formHandle() const;
    // 遍历所有页面上的表单域：visit(批注, 页号, 字段类型, 字段名)。
    void forEachFormField(
        const std::function<void(FPDF_ANNOTATION, int, int, const QString&)>& visit) const;
    // 文档里有没有这个表单域。
    bool hasFormField(const QString& name) const;
    // 文档里一共有多少条批注。
    int totalAnnotationCount() const;
    // 文档里的表单域清单：名字、类型、当前值、可选项、只读标记。
    QJsonArray collectFormFields() const;
    // 把一条 JSON 指令解析成内部结构；失败时 error 写明理由。
    bool parseInstruction(const QJsonObject& instruction, EditInstruction* out, QString* error);
    // 执行一条指令（已经解析过）。
    bool applyInstruction(const EditInstruction& instruction, QString* error);
    // 按名字给表单域赋值。
    bool assignFormFieldValue(const QString& name, const QString& value, QString* error);
    // 加一条批注（便签或高亮）。
    bool createAnnotation(const EditInstruction& instruction, QString* error);

    QString serverName_;
    QLocalServer server_;
    QLocalSocket* client_ = nullptr;
    QByteArray receiveBuffer_;
    FPDF_DOCUMENT document_ = nullptr;
    QJsonArray pageMeta_;
    int pageCount_ = 0;
    QTemporaryDir renderDir_;
    bool pdfiumInitialized_ = false;

    // 表单环境。本仓库固定的 PDFium 构建在 FPDFDOC_ExitFormFillEnvironment
    // 时会 0xC0000005 崩溃，所以这里**只创建不释放**（每个文档泄漏一个环境对象）。
    mutable FPDF_FORMHANDLE form_ = nullptr;
    mutable FPDF_FORMFILLINFO formInfo_{};

    // 打开文档时的路径与口令：覆盖保存之后要重新打开这份文档。
    QString sourcePath_;
    QString sourcePassword_;

    // 编辑状态。保存时据此决定能不能写盘：
    // Inconsistent 表示上一次编辑没有完整成功，**不许保存**——
    // 否则会把改了一半的文档写进用户的文件。
    enum class EditState {
        Clean,        // 没有未保存的改动
        Dirty,        // 有改动，还没保存
        Inconsistent, // 上一次编辑失败了，状态不可信
    };
    EditState editState_ = EditState::Clean;
};

}  // namespace aipdf::worker::parse
