#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

namespace aipdf::app {

class TextController;

// 全文搜索面板：输入关键词 → 显示结果列表 → 点击结果跳到对应页。
//
// 面板自己不碰 PDF：搜索由 TextController 在后台线程做，
// 面板只负责收集关键词、显示结果、把「跳到第几页」发出去。
class SearchPanel final : public QWidget {
    Q_OBJECT

public:
    explicit SearchPanel(QWidget* parent = nullptr);

    // 绑定当前文档的文本控制器；传 nullptr 表示没有打开文档。
    void setController(TextController* controller);

    // 把焦点放到输入框（Ctrl+F 用）。
    void focusSearch();

    // 当前结果条数（供验收与测试读取）。
    int resultCount() const;

signals:
    // 用户选择了一条结果，请求跳到该页。
    void navigateToPage(int pageIndex);

private:
    void buildUi();
    void runSearch();
    void refreshResults();
    void goToResult(int index);
    void stepResult(int delta);
    void clearResults();
    // 第 index 条结果所在的页。
    int pageOfResult(int index) const;

    TextController* controller_ = nullptr;
    QLineEdit* queryEdit_ = nullptr;
    QPushButton* previousButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QListWidget* results_ = nullptr;
    // 输入停顿后再搜索，避免每敲一个字就全扫一遍。
    QTimer* debounceTimer_ = nullptr;
    int currentResult_ = -1;
    // 重建结果列表时不要触发跳转。
    bool suppressNavigation_ = false;
};

}  // namespace aipdf::app
