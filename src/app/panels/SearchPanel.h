#pragma once

#include "i18n/Translate.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QCheckBox;
class QSpinBox;
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
    // 状态行当前显示的是哪一类文案：重译时按类型重新生成，
    // 而不是把「共 N 处结果」重置成初始提示。
    enum class StatusMode { Prompt, Live, Cancelled };

    void buildUi();
    void runSearch();
    void refreshResults();
    // 只重写已有结果项的前缀，不重建列表，保住当前选中项。
    void refreshResultTexts();
    void updateStatus();
    void goToResult(int index);
    void stepResult(int delta);
    void clearResults();
    // 第 index 条结果所在的页。
    int pageOfResult(int index) const;

    // 切换语言后重译常驻文案，并重算随搜索状态变化的动态文案。
    void retranslateUi();

    TextController* controller_ = nullptr;
    QLineEdit* queryEdit_ = nullptr;
    QPushButton* previousButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QCheckBox* caseSensitiveBox_ = nullptr;
    QCheckBox* wholeWordBox_ = nullptr;
    QCheckBox* regexBox_ = nullptr;
    QSpinBox* fromPageSpin_ = nullptr;
    QSpinBox* toPageSpin_ = nullptr;
    QListWidget* results_ = nullptr;
    // 输入停顿后再搜索，避免每敲一个字就全扫一遍。
    QTimer* debounceTimer_ = nullptr;
    int currentResult_ = -1;
    // 重建结果列表时不要触发跳转。
    bool suppressNavigation_ = false;
    StatusMode statusMode_ = StatusMode::Prompt;
    // 一次设定、长期显示的界面文案：切换语言时就地重译。
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
