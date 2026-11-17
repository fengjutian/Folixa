#pragma once

#include <QObject>
#include <QString>
#include <functional>

class QWidget;

namespace aipdf {
namespace ipc {
class WorkerClient;
}
}  // namespace aipdf

namespace aipdf::app {

class AiPanel;
class AiToolService;
class OcrController;
class PdfView;
// 转换结果（定义在 app/ConversionInputDialog.h）。
// 这里只前置声明：WiringActions.h 不需要知道结果的细节。
struct ConversionInputOutcome;

// 第二批接线（E/F/G/H）的动作集合。
//
// 为什么要单独一个类：这些入口要碰的东西很杂（当前文档、AI 服务、
// 打印、转换、注释工具箱），全塞进 MainWindow 会让那个文件没法看了。
// 这里只做编排：收参数、问用户、把结果交给领域层，**不重算任何判定**。
class WiringActions final : public QObject {
    Q_OBJECT

public:
    explicit WiringActions(QWidget* parent = nullptr);
    ~WiringActions() override;

    // 当前文档视图从哪里取（换标签页时它会变，所以用回调而不是缓存指针）。
    void setViewProvider(std::function<PdfView*()> provider);
    // 新建文档之后要把文件打开：交给调用方处理（主窗口负责开新标签页）。
    void setOpenDocumentHandler(std::function<void(const QString&)> handler);
    // AI 服务（聊天类请求要走 Worker）。
    void setWorkerClient(ipc::WorkerClient* client);

    // ---------------------------------------------------------- 文件三项 ----
    void newDocument();
    void printBatch();
    void printTemplates();

    // ---------------------------------------------------------- AI 五项 ----
    void imageGeneration();
    void summaryImage();
    void pptGeneration();
    void detection();
    void narration();

    // ------------------------------------------------------ 注释三件套 ----
    void calculator();
    void symbolPanel();
    void toggleContinuousAnnotationMode(bool enabled);

    // ------------------------------------------------------ 转换与导出 ----
    void preflight();
    void portfolio();
    void ocrSuspicion();
    void exportHtml();
    void exportImagePdf();
    void exportDxf();
    void exportOffice();
    void exportOfd();
    void flattenFonts();

    // ------------------------------------------------- 第三批接线（任务 J~U）----
    //
    // 同一套规矩：收参数、问用户、把结果交给领域层，**不重算任何判定**。

    // 识别控制器（写隐藏文字层要用识别出来的词级坐标）。
    void setOcrController(OcrController* controller);
    // AI 面板（本地离线问答在面板里做）。
    void setAiPanel(AiPanel* panel);

    // J：把 OCR 识别结果写成不可见文字层（可搜索 / 可复制）。
    void writeInvisibleTextLayer();
    // K：段落排版（对齐 / 行距 / 缩进）。
    void paragraphFormat();
    // L：读取文档里既有的标签树。
    void structTree();
    // M：OCG 图层面板。
    void ocgLayers();
    // N：任意文件 → PDF。
    void universalConvert();
    // O：本地离线问答（不联网、不调模型）。
    void offlineQa();
    // P：PDF/UA 合规检查（只检查，不生成合规文件）。
    void pdfUaCheck();
    // Q：静态 XFA 表单字段。
    void xfaFields();
    // R：插入 3D 模型 / 多媒体注解（只写结构，不渲染）。
    void insertRichMedia(bool preferThreeD = true);
    // S：按新页宽重排整篇。
    void documentReflow();
    // T：动作向导。
    void actionWizard();
    // U：发票打印。
    void invoicePrint();

    // ------------------------------------ 第四批接线（补齐缺口）----
    //
    // 这一批是把「看得见但点不动」的占位按钮换成真入口。
    // 每个 handler 只做编排，判定一律在领域层。
    // 从剪贴板新建 PDF（剪贴板里的文字 -> 一份新 PDF）。
    void convertFromClipboard();
    // 从网页新建 PDF（抓正文 -> 排版 -> 一份新 PDF）。
    void convertFromWeb();
    // 从表单数据文件新建 PDF（模板 PDF + 数据文件 -> 填好的 PDF）。
    void convertFromFormFile();
    // 从扫描仪新建 PDF（WIA 取帧 -> 多页 PDF）。
    void scanFromScanner();
    // 文档比较（两份 PDF 比出差异）。
    void compareDocuments();
    // 批量加密（给一个目录里的 PDF 批量加打开密码）。
    void batchEncrypt();
    // 放映是否循环（切换；正在放映的话立刻生效）。
    void togglePresentationLoop();
    // 给 PDF 里的表单域写标签树与替代文本。
    void autoTagFormFields();
    // 把当前页面送进本机 OneNote。
    void sendToOneNote();

signals:
    // 连续批注模式的开关状态变了。
    void continuousAnnotationModeChanged(bool enabled);
    // 要在状态栏显示一句话。
    void message(const QString& text);

private:
    // 把转换出来的 PDF 存盘并打开（转换输入三件套共用）。
    void saveConvertedPdf(const QString& title, const ConversionInputOutcome& outcome);
    // 当前文档视图；没有打开文档时返回 nullptr。
    PdfView* currentView() const;
    // 取当前视图；没有文档时给用户一句话并返回 nullptr。
    PdfView* requireView();
    // AI 服务；Worker 没连上时返回 nullptr。
    AiToolService* aiService();
    // 界面上提示一句话（用主窗口的状态栏）。
    void notify(const QString& message);

    std::function<PdfView*()> viewProvider_;
    std::function<void(const QString&)> openDocumentHandler_;
    AiToolService* aiToolService_ = nullptr;
    ipc::WorkerClient* workerClient_ = nullptr;
    OcrController* ocrController_ = nullptr;
    AiPanel* aiPanel_ = nullptr;
    // 对话框的父窗口（QObject 没有 parentWidget()，这里自己记着）。
    QWidget* parentWidget_ = nullptr;
};

}  // namespace aipdf::app
