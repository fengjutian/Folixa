#pragma once

#include "doc/LocalIndex.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace aipdf::app {

class IndexController;

// 索引面板：看索引状态、取消索引、按关键词检索、点结果跳到原文。
//
// 面板自己不碰 PDF：索引由 IndexController 在后台线程做，
// 面板只负责显示状态、收集关键词、把「跳到第几页、哪一块」发出去。
class IndexPanel final : public QWidget {
    Q_OBJECT

public:
    explicit IndexPanel(QWidget* parent = nullptr);

    // 绑定当前文档的索引控制器；传 nullptr 表示没有打开文档。
    void setController(IndexController* controller);

    // 触发一次索引（验收脚本与测试用）。
    void startIndexing();
    // 取消索引。
    void cancelIndexing();

    // 当前结果条数（供验收与测试读取）。
    int resultCount() const;
    // 当前状态文字。
    QString statusText() const;

signals:
    // 用户选择了一条结果：请求跳到该页并高亮该块。
    void chunkActivated(int pageIndex, const QString& chunkId);

private:
    void buildUi();
    void runSearch();
    void refreshResults();
    void updateStatus();
    void goToResult(int row);
    void clearResults();

    IndexController* controller_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* buildButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QLineEdit* queryEdit_ = nullptr;
    QListWidget* results_ = nullptr;
    QVector<doc::IndexHit> hits_;
    // 重建结果列表时不要触发跳转。
    bool suppressNavigation_ = false;
};

}  // namespace aipdf::app
