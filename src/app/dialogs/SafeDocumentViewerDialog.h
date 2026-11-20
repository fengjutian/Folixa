#pragma once

#include <QDialog>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QString>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QWidget;

namespace aipdf::app {

class SafeDocumentController;

// 安全文档查看器：页面由隔离进程渲染，表单与批注也由隔离进程改。
//
// 为什么单独一个窗口：安全模式下主进程拿不到文档句柄，也就没有任何可编辑的对象；
// 把它塞进编辑器视图只会让人误以为能改。
//
// 安全边界不变：主进程**不打开**这个 PDF，也不持有它的字节。
// 填表单、加批注、保存都走 IPC，由隔离进程里的那份文档完成。
class SafeDocumentViewerDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SafeDocumentViewerDialog(const QString& path, QWidget* parent = nullptr);
    ~SafeDocumentViewerDialog() override;

    // 解析进程可执行文件路径（默认在应用同目录找 PdfParseWorker）。
    // 必须在第一次显示之前设置。
    void setWorkerExecutable(const QString& path);

    // 打开文档（第一次显示时自动调用；失败后可以用密码重试）。
    bool openDocument();
    bool documentOpened() const;
    int currentPage() const;
    int pageCount() const;
    QString lastError() const;
    // 顶部那条隔离提示的原文（界面测试用）。
    QString bannerText() const;
    // 文档里的表单域清单（界面测试用）。
    QJsonArray formFields() const;
    // 表单域当前填的值（界面测试用）。没这个域返回空串。
    QString formFieldValue(const QString& name) const;
    // 状态栏现在的文字（界面测试用）。
    QString statusText() const;

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void retranslateUi();
    bool attemptOpen(const QString& password);
    void showFailure(const QString& reason);
    void gotoPage(int pageIndex);
    void loadPage(int pageIndex);
    void prefetchNeighbours();
    void updateNavigationState();
    void applyZoom();

    // 表单面板：按解析进程报出来的字段清单生成控件。
    void buildFormPanel();
    QWidget* createFieldEditor(const QJsonObject& field);
    QString editorValue(const QWidget* editor) const;
    QJsonObject fieldByName(const QString& name) const;
    void applyFormValues();

    // 加批注：先进入「放置」状态，用户在页面上点一下。
    void startPlacement(const QString& subtype);
    bool handlePageClick(const QPoint& position);
    void updatePlacementUi();

    // 保存。
    void doSave(bool saveAs);
    QString saveSummary(const QStringList& notes) const;

    QString path_;
    SafeDocumentController* controller_ = nullptr;
    QLabel* banner_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* pageImage_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QPushButton* previous_ = nullptr;
    QPushButton* next_ = nullptr;
    QSpinBox* pageBox_ = nullptr;
    QComboBox* zoomBox_ = nullptr;
    QLabel* pageCountLabel_ = nullptr;
    QWidget* passwordRow_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QPushButton* retryButton_ = nullptr;
    // 表单面板（没有表单域时不露出来）。
    QWidget* formPanel_ = nullptr;
    QFormLayout* formLayout_ = nullptr;
    QPushButton* applyFormButton_ = nullptr;
    QHash<QString, QWidget*> fieldEditors_;
    // 工具条：加便签 / 加高亮 / 保存 / 另存为。
    QPushButton* addNoteButton_ = nullptr;
    QPushButton* addHighlightButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* saveAsButton_ = nullptr;
    QHash<int, QPixmap> cache_;
    int currentPage_ = 0;
    int pageCount_ = 0;
    double zoom_ = 1.0;
    bool openAttempted_ = false;
    bool loading_ = false;
    QString lastError_;
    // 正在等用户点一个位置来放批注。
    bool placing_ = false;
    QString placementSubtype_;
};

}  // namespace aipdf::app
