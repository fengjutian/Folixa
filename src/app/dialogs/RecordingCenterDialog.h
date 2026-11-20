#pragma once

#include "i18n/Translate.h"

#include <QDialog>
#include <QVector>

#include <optional>

class QLabel;
class QPushButton;
class QTableWidget;

namespace aipdf::app {

class RecordingEntry;
class RecordingLibrary;

// 录制中心：既能开始录制，也能集中管理已经录出来的文件。
//
// 为什么要有这个对话框（而不是各自散在 Ribbon 上）：
//   * 「录完的文件在哪」是用户最常问的一句话——文件散在「视频\捕获」里时，
//     用户得自己去文件夹里翻；
//   * 录制、截图、声音是同一类产物，列表里能一起看；
//   * 删除与「在文件夹里显示」是列表该有的管理动作。
//
// 四个录制动作本身由主窗口执行（要开放映窗口、要抓屏），
// 这个对话框只负责发信号与展示列表。
class RecordingCenterDialog final : public QDialog {
    Q_OBJECT

public:
    // library 不由对话框持有（它比对话框活得久）。
    explicit RecordingCenterDialog(RecordingLibrary* library, QWidget* parent = nullptr);

signals:
    // 用户点了「屏幕录制」。
    void screenRecordingRequested();
    // 用户点了「PDF 演示录制」。
    void presentationRecordingRequested();
    // 用户点了「声音录制」。
    void audioRecordingRequested();
    // 用户点了「窗口截图」。
    void screenshotRequested();

private slots:
    void refreshList();
    void openSelected();
    void revealSelected();
    void deleteSelected();

private:
    void buildUi();
    void updateActionState();
    // 当前选中的一条（按值返回：entries() 是按值返回的，指回去会悬空）。
    std::optional<RecordingEntry> selectedEntry() const;

    RecordingLibrary* library_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* revealButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
