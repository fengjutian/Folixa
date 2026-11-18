#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "doc/model/BlankDocument.h"
#include "doc/print/PrintBatch.h"
#include "doc/print/PrintTemplate.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace aipdf::app {

// 打印模板存放的设置分组名。
//
// 领域层用 settingsKey 决定模板存在设置文件的哪一组里；这里固定一组，
// 所有模板都在同一组里，不与其它设置混在一起。
inline constexpr char kPrintTemplatesSettingsKey[] = "printTemplates";

// 新建空白文档：选纸张、方向、页数，生成一份新的 PDF。
//
// 尺寸与页数的判定全在领域层（doc::BlankDocument），界面只负责收参数。
class NewDocumentDialog final : public QDialog {
    Q_OBJECT

public:
    explicit NewDocumentDialog(QWidget* parent = nullptr);

    // 选中的纸张。
    doc::PageSize selectedPaper() const;
    // 换纸张（会连带刷新方向与自定义宽高控件的可用状态）。
    void setPaper(doc::PageSize size);
    // 选中的方向。
    doc::PageOrientation selectedOrientation() const;
    // 方向控件当前是否可用。
    //
    // 自定义纸张时方向**不生效**（领域层明确如此），所以界面上要把它置灰，
    // 不能让用户以为改了方向真的有效果。
    bool orientationEnabled() const;
    // 页数。
    int pageCount() const;
    // 拼好的参数。
    doc::BlankDocumentOptions options() const;
    // 生成文档字节；失败时返回空字节并把中文原因写进 error。
    QByteArray createDocument(QString* error = nullptr) const;

private:
    void refreshEnabledState();

    QComboBox* paperBox_ = nullptr;
    QComboBox* orientationBox_ = nullptr;
    QSpinBox* pageCountSpin_ = nullptr;
    QDoubleSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QLabel* orientationHint_ = nullptr;
};

// 批量打印：把多个文件排成一个打印队列。
//
// 这一层不真的打印（打印要渲染页面、要弹系统打印对话框）：
// 领域层只负责排队与提醒，真正的执行由界面层的打印流程接手。
class PrintBatchDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PrintBatchDialog(QWidget* parent = nullptr);

    // 加一个待打印文件。
    void addFile(const QString& filePath);
    // 队列里的文件（份数与页范围取自界面上的设置）。
    QVector<doc::PrintBatchItem> items() const;
    // 整批的设置。
    doc::PrintBatchOptions options() const;
    // 排队列的结果（失败时 ok 为 false，error 是中文原因）。
    doc::PrintBatchPlan plan() const;
    // 排队列时的提醒（跳过的项、被夹住的参数）。
    QStringList warnings() const;
    // 计划里的任务数。
    int plannedJobCount() const;
    // 总页数。
    //
    // 领域层这一层不打开 PDF，读不到真实页数，所以恒为 -1（未知）。
    int totalPages() const;
    // 总页数在界面上怎么写：-1 时显示「未知」，不显示一个假的数字。
    QString totalPagesText() const;

private:
    void refreshPlan();

    QTableWidget* table_ = nullptr;
    QSpinBox* copiesSpin_ = nullptr;
    QLineEdit* rangeEdit_ = nullptr;
    QLineEdit* jobNameEdit_ = nullptr;
    QCheckBox* combineBox_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QLabel* planLabel_ = nullptr;
    QLabel* warningsLabel_ = nullptr;
    doc::PrintBatchPlan plan_;
};

// 打印模板：把打印参数存下来复用。
class PrintTemplatesDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PrintTemplatesDialog(QWidget* parent = nullptr);

    // 当前编辑的模板。
    doc::PrintTemplate currentTemplate() const;
    // 把一份模板填进界面。
    void setCurrentTemplate(const doc::PrintTemplate& value);
    // 保存当前模板；失败时把中文原因写进 error。
    bool saveTemplate(QString* error = nullptr);
    // 已保存的模板列表。
    QVector<doc::PrintTemplate> templates() const;
    // 删掉选中的模板。
    bool deleteSelected(QString* error = nullptr);
    // 校验当前模板（不保存）。
    bool validateCurrent(QString* error = nullptr) const;

signals:
    // 用户点「套用」：把当前模板的参数交给调用方去打印。
    void applyRequested(const doc::PrintTemplate& value);

private:
    void reloadList();
    void refreshFromSelection();

    QListWidget* list_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QSpinBox* copiesSpin_ = nullptr;
    QComboBox* paperBox_ = nullptr;
    QComboBox* orientationBox_ = nullptr;
    QComboBox* scalingBox_ = nullptr;
    QSpinBox* scaleSpin_ = nullptr;
    QCheckBox* duplexBox_ = nullptr;
    QCheckBox* colorBox_ = nullptr;
    QLineEdit* rangeEdit_ = nullptr;
    QString currentId_;
};

}  // namespace aipdf::app
