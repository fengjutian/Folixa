#pragma once

#include <QDialog>
#include <QHash>
#include <QPixmap>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;

namespace aipdf::app {

class SafeDocumentController;

// 安全文档查看器：只读，页面由隔离进程渲染。
//
// 为什么单独一个窗口：安全模式下主进程拿不到文档句柄，也就没有任何可编辑的对象；
// 把它塞进编辑器视图只会让人误以为能改。
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

protected:
    void showEvent(QShowEvent* event) override;

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
    QHash<int, QPixmap> cache_;
    int currentPage_ = 0;
    int pageCount_ = 0;
    double zoom_ = 1.0;
    bool openAttempted_ = false;
    bool loading_ = false;
    QString lastError_;
};

}  // namespace aipdf::app
