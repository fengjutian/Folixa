#pragma once

#include <QDialog>
#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "app/controllers/LastWaveBridge.h"
#include "doc/batch/ActionWizard.h"
#include "doc/convert/InvoiceLayout.h"
#include "doc/convert/UniversalConverter.h"
#include "pdf/ops/DocumentReflow.h"
#include "pdf/ops/OptionalContent.h"
#include "pdf/content/ParagraphFormat.h"
#include "pdf/compliance/PdfUaChecker.h"
#include "pdf/annot/RichMedia3d.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/forms/XfaSupport.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTreeWidget;

namespace aipdf::app {

// ================================================================ 任务 L ====
//
// 读取既有标签树。
//
// 「没有标签树」必须显示成一句话，而不是一个空列表：空列表会让人以为读过了。
class StructTreeDialog final : public QDialog {
    Q_OBJECT

public:
    explicit StructTreeDialog(QWidget* parent = nullptr);

    void setReport(const pdf::StructTreeReport& report);
    // 顶部摘要（没有标签树时是「这份文档没有标签树」）。
    QString summaryText() const;
    // 表格里的行数。
    int rowCount() const;
    // 没有标签树时显示的那句话。
    QString emptyText() const;

private:
    void rebuild();

    pdf::StructTreeReport report_;
    QLabel* summaryLabel_ = nullptr;
    QTreeWidget* tree_ = nullptr;
};

// ================================================================ 任务 M ====
//
// OCG 图层面板：列出图层、逐个开关、写回文档。
class OcgLayerPanel final : public QDialog {
    Q_OBJECT

public:
    explicit OcgLayerPanel(QWidget* parent = nullptr);

    void setSummary(const pdf::OptionalContentSummary& summary);
    // 图层条目数（与 readOptionalContent().groups.size() 一致）。
    int layerCount() const;
    // 顶部摘要。
    QString summaryText() const;
    // 页面级覆盖说明（一行一条）。
    QStringList pageOverrideLines() const;
    // 写回的边界说明。
    QString writeBackNoticeText() const;
    // 当前勾选状态（按图层顺序）。
    QVector<bool> visibleStates() const;
    // 按当前勾选状态生成的写回方案。
    pdf::OptionalContentPlan plan() const;

signals:
    // 用户点了「写回文档」。
    void applyRequested(const pdf::OptionalContentPlan& plan);

private:
    void rebuild();

    pdf::OptionalContentSummary summary_;
    QLabel* summaryLabel_ = nullptr;
    QLabel* overrideLabel_ = nullptr;
    QLabel* noticeLabel_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* applyButton_ = nullptr;
};

// ================================================================ 任务 N ====
//
// 任意文件 → PDF。
class UniversalConvertDialog final : public QDialog {
    Q_OBJECT

public:
    explicit UniversalConvertDialog(QWidget* parent = nullptr);

    void setSourcePath(const QString& path);
    QString sourcePath() const;
    void setFontPath(const QString& path);
    QString fontPath() const;

    // 转换。失败时返回 false，error 里是中文原因。
    bool convert(QString* error = nullptr);
    // 转换结果。
    const doc::UniversalConversionResult& result() const;
    // 结果提示（**必须**如实说明有多少字符显示不出来）。
    QString noticeText() const;

signals:
    // 用户点了「保存为 PDF」。
    void saveRequested(const QString& suggestedFileName);

private:
    void refreshNotice();

    QLineEdit* sourceEdit_ = nullptr;
    QPushButton* sourceButton_ = nullptr;
    QLineEdit* fontEdit_ = nullptr;
    QPushButton* fontButton_ = nullptr;
    QLabel* noticeLabel_ = nullptr;
    QPushButton* convertButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    doc::UniversalConversionResult result_;
    bool converted_ = false;
};

// ================================================================ 任务 P ====
//
// PDF/UA 合规检查报告：分三档显示 + 结论 + 边界说明。
class PdfUaReportDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PdfUaReportDialog(QWidget* parent = nullptr);

    void setReport(const pdf::PdfUaReport& report);
    // 顶部结论（是否合规 + 各项计数）。
    QString conclusionText() const;
    // 边界说明：**只做检查**，不生成合规文件。
    QString scopeNoticeText() const;
    // 全部结论行（code + title + detail + suggestion）。
    QStringList findingLines() const;
    int passCount() const;
    int warningCount() const;
    int errorCount() const;

    // 覆盖顶部结论与边界说明。
    //
    // 为什么需要：「一键生成 PDF/UA」用的不是检查器那套话术 ——
    // 检查器的结论是「本项目检查器判定」，不是合规认证；
    // 生成器要显示它自己的摘要与边界说明。
    // 留空就用默认（检查器那套）。
    void setConclusionText(const QString& text);
    void setScopeNotice(const QString& text);

private:
    void rebuild();

    pdf::PdfUaReport report_;
    QString conclusionOverride_;
    QString scopeOverride_;
    QLabel* conclusionLabel_ = nullptr;
    QLabel* scopeLabel_ = nullptr;
    QTreeWidget* passTree_ = nullptr;
    QTreeWidget* warningTree_ = nullptr;
    QTreeWidget* errorTree_ = nullptr;
};

// ================================================================ 任务 Q ====
//
// 静态 XFA 表单字段。
class XfaFieldsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit XfaFieldsDialog(QWidget* parent = nullptr);

    void setReport(const pdf::XfaReport& report);
    // 字段数。
    int fieldCount() const;
    // 顶部摘要。
    QString summaryText() const;
    // 动态 XFA 的提示（**必须**出现「只做静态字段提取，不执行脚本」）。
    QString dynamicNoticeText() const;
    // 字段清单（一行一条）。
    QStringList fieldLines() const;
    // 导出成 CSV。
    QByteArray exportCsv() const;

private:
    void rebuild();

    pdf::XfaReport report_;
    QLabel* summaryLabel_ = nullptr;
    QLabel* dynamicLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* exportButton_ = nullptr;
};

// ================================================================ 任务 R ====
//
// 插入 3D 模型 / 多媒体注解。
class RichMediaDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Kind {
        ThreeD,  // 3D 模型（U3D）
        Media,  // 多媒体（音频 / 视频 / 图片）
    };
    Q_ENUM(Kind)

    explicit RichMediaDialog(QWidget* parent = nullptr);

    void setSourcePath(const QString& path);
    QString sourcePath() const;
    void setKind(Kind kind);
    Kind kind() const;

    // 写完之后回读校验的结果说明。
    void setReadResult(const pdf::RichMediaReadResult& result);
    QString resultText() const;
    // 边界说明：**只写结构，不渲染、不解码**。
    QString scopeNoticeText() const;

signals:
    // 用户点了「插入」：文件与类型都齐了，让上层去写。
    void insertRequested(const QString& path, Kind kind);

private:
    void refreshKind();
    // 选了文件 / 换了类型之后，把预览面板同步过去。
    void refreshPreview();

    QComboBox* kindBox_ = nullptr;
    QLineEdit* pathEdit_ = nullptr;
    QPushButton* pathButton_ = nullptr;
    QPushButton* insertButton_ = nullptr;
    QLabel* scopeLabel_ = nullptr;
    QLabel* resultLabel_ = nullptr;
    // 音视频预览（Media Foundation 解码 + 声卡播放）。3D 模型那一路用不上。
    class MediaPreviewPanel* preview_ = nullptr;
    // 预览区的外框（3D 那一路要把它藏起来）。
    QGroupBox* previewBox_ = nullptr;
    pdf::RichMediaReadResult readResult_;
    bool hasResult_ = false;
};

// ================================================================ 任务 S ====
//
// 按新页宽重排。
class ReflowDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ReflowDialog(QWidget* parent = nullptr);

    void setOptions(const pdf::ReflowOptions& options);
    // 当前参数。
    pdf::ReflowOptions options() const;
    // 填入重排报告。
    void setReport(const pdf::ReflowReport& report);
    // 领域层给的报告。
    const pdf::ReflowReport& report() const;
    // 报告摘要。
    QString noticeText() const;
    // 丢了哪些内容（逐条一行）。
    QStringList droppedLines() const;
    // 「我明白版面会变」有没有勾。
    bool layoutChangeAcknowledged() const;
    // 重排前的警告文字。
    QString layoutChangeNoticeText() const;

signals:
    // 用户点了「重排」：参数交出去跑领域层。
    void reflowRequested(const pdf::ReflowOptions& options);

private:
    void rebuild();

    QDoubleSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QDoubleSpinBox* fontSizeSpin_ = nullptr;
    QDoubleSpinBox* lineSpacingSpin_ = nullptr;
    QComboBox* alignBox_ = nullptr;
    QCheckBox* acknowledgeBox_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QLabel* noticeLabel_ = nullptr;
    QLabel* layoutNoticeLabel_ = nullptr;
    QListWidget* droppedList_ = nullptr;
    pdf::ReflowReport report_;
    bool hasReport_ = false;
};

// ================================================================ 任务 T ====
//
// 动作向导：动作列表、条件、顺序、参数都由领域层的类型给出，
// 界面只负责收集参数与调用。
class WizardDialog final : public QDialog {
    Q_OBJECT

public:
    explicit WizardDialog(QWidget* parent = nullptr);

    // 向导名与步骤。
    doc::ActionWizard wizard() const;
    void setWizard(const doc::ActionWizard& value);
    // 填入执行报告。
    void setRunReport(const doc::WizardRunReport& report);
    // 执行摘要。
    QString runSummaryText() const;
    // 边界说明：只编排已有的文档级操作，不是脚本引擎。
    QString scopeNoticeText() const;

signals:
    // 用户点了「运行」：界面把收集好的向导交出去执行。
    void runRequested(const doc::ActionWizard& wizard);

private:
    void rebuildSteps();
    void rebuildParams();
    void addStep();
    void removeCurrentStep();
    void moveCurrentStep(int delta);
    void onParamChanged();

    QLineEdit* nameEdit_ = nullptr;
    QComboBox* typeBox_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* upButton_ = nullptr;
    QPushButton* downButton_ = nullptr;
    QListWidget* stepList_ = nullptr;
    QTableWidget* paramTable_ = nullptr;
    QLabel* scopeLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QPushButton* runButton_ = nullptr;
    doc::ActionWizard wizard_;
    doc::WizardRunReport report_;
    bool hasReport_ = false;
};

// ================================================================ 任务 U ====
//
// 发票打印：填要素 → 预览版式 → 打印 / 导出 PDF。
class InvoiceDialog final : public QDialog {
    Q_OBJECT

public:
    explicit InvoiceDialog(QWidget* parent = nullptr);

    // 当前填出来的发票。
    doc::Invoice invoice() const;
    // 版式预览。校验不通过时返回空串，error 里是中文原因。
    QString previewText(QString* error = nullptr) const;
    // 校验结果。
    doc::InvoiceCheckResult check() const;
    // 币种符号（界面上的选择）。
    QString currencySymbol() const;
    // 小数位（界面上的选择）。
    int decimals() const;
    // 税率（万分比）。
    qint64 taxRateBasisPoints() const;

signals:
    // 用户点了「打印」。
    void printRequested();
    // 用户点了「导出 PDF」。
    void exportRequested();

private:
    void refreshPreview();

    QLineEdit* numberEdit_ = nullptr;
    QLineEdit* titleEdit_ = nullptr;
    QLineEdit* sellerNameEdit_ = nullptr;
    QLineEdit* sellerTaxEdit_ = nullptr;
    QLineEdit* buyerNameEdit_ = nullptr;
    QLineEdit* buyerTaxEdit_ = nullptr;
    QLineEdit* dateEdit_ = nullptr;
    QComboBox* currencyBox_ = nullptr;
    QComboBox* decimalsBox_ = nullptr;
    QDoubleSpinBox* taxRateSpin_ = nullptr;
    QTableWidget* itemTable_ = nullptr;
    QPushButton* addItemButton_ = nullptr;
    QPushButton* removeItemButton_ = nullptr;
    QPushButton* printButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

// ================================================================ 任务 K ====
//
// 段落排版：参数原样交给领域层的 layoutParagraph，
// 写出去的内容流由领域层的 buildParagraphContentStream 生成。
class ParagraphFormatDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ParagraphFormatDialog(QWidget* parent = nullptr);

    // 要排的文字。
    void setText(const QString& text);
    QString text() const;
    // 目标框（PDF user space）。
    void setBox(const QRectF& box);
    QRectF box() const;
    // 当前参数。
    pdf::ParagraphFormat format() const;
    void setFormat(const pdf::ParagraphFormat& format);
    // 排一次（调用领域层的 layoutParagraph）。失败时返回 false。
    bool layout(QString* error = nullptr);
    // 上一次排版的结果。
    pdf::ParagraphPlan plan() const;
    // 参数摘要（界面上显示「原样交给领域层」的那些值）。
    QString formatSummaryText() const;

private:
    void refreshSummary();

    QPlainTextEdit* textEdit_ = nullptr;
    QDoubleSpinBox* xSpin_ = nullptr;
    QDoubleSpinBox* ySpin_ = nullptr;
    QDoubleSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QComboBox* alignBox_ = nullptr;
    QComboBox* spacingModeBox_ = nullptr;
    QDoubleSpinBox* fontSizeSpin_ = nullptr;
    QDoubleSpinBox* spacingSpin_ = nullptr;
    QDoubleSpinBox* firstLineIndentSpin_ = nullptr;
    QDoubleSpinBox* leftIndentSpin_ = nullptr;
    QDoubleSpinBox* rightIndentSpin_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* planLabel_ = nullptr;
    pdf::ParagraphPlan plan_;
    bool hasPlan_ = false;
};

}  // namespace aipdf::app
