#pragma once

#include "app/layout/PageLayout.h"
#include "i18n/Translate.h"

#include <QDialog>

class QCheckBox;
class QDialogButtonBox;
class QGroupBox;
class QLabel;
class QRadioButton;
class QSpinBox;

namespace aipdf::app {

// 一份「视图设置」。
//
// 为什么用结构体传进传出：对话框只负责收集参数，不碰文档；
// 确认之后由主窗口把设置应用到视图上（与「加密与权限」同一个分工）。
struct ViewSettings {
    // 缩放方式。
    enum class ZoomMode {
        ActualSize,  // 100%
        FitWidth,    // 适合宽度
        FitPage,     // 适合页面
        Custom,      // 自定义百分比
    };
    ZoomMode zoomMode = ZoomMode::ActualSize;
    // 自定义百分比（1.0 = 100%）。只在 zoomMode == Custom 时有意义。
    double customZoom = 1.0;
    // 页面布局：连续 / 单页 / 双页 / 双页封面。
    PageLayout::LayoutMode layoutMode = PageLayout::LayoutMode::Continuous;
    // 侧栏与标尺的可见性。
    bool navigationVisible = true;
    bool aiVisible = false;
    bool rulerVisible = false;
    // 视图旋转（0/90/180/270）：只改显示，不写进文档。
    int rotation = 0;
};

// 视图设置面板：把散在 Ribbon 上的视图选项收到一个对话框里。
//
// 面板里列的每一项都对应**已经能用**的能力：
// 缩放（实际大小 / 适合宽度 / 适合页面 / 自定义）、页面布局（连续 / 单页 /
// 双页 / 双页封面）、显示（导航面板 / AI 面板 / 标尺）、视图旋转。
//
// 本程序没有「隐藏批注 / 隐藏表单域 / 灰度显示 / 显示裁剪框」这类显示开关，
// 所以面板里也没有这一组——不列没用的选项，也不编一个点不动的开关。
class ViewSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ViewSettingsDialog(const ViewSettings& current, QWidget* parent = nullptr);

    // 用户确认后的设置（只在 accepted 之后有意义）。
    ViewSettings settings() const;

protected:
    void accept() override;

private:
    void buildUi(const ViewSettings& current);
    void retranslateUi();
    // 自定义百分比只在选中「自定义」时能改。
    void updateZoomEnabledState();

    QGroupBox* zoomGroup_ = nullptr;
    QRadioButton* actualSizeButton_ = nullptr;
    QRadioButton* fitWidthButton_ = nullptr;
    QRadioButton* fitPageButton_ = nullptr;
    QRadioButton* customZoomButton_ = nullptr;
    QSpinBox* customZoomSpin_ = nullptr;
    QLabel* customZoomSuffix_ = nullptr;

    QGroupBox* layoutGroup_ = nullptr;
    QRadioButton* continuousButton_ = nullptr;
    QRadioButton* singlePageButton_ = nullptr;
    QRadioButton* facingButton_ = nullptr;
    QRadioButton* facingCoverButton_ = nullptr;

    QGroupBox* displayGroup_ = nullptr;
    QCheckBox* navigationCheck_ = nullptr;
    QCheckBox* aiCheck_ = nullptr;
    QCheckBox* rulerCheck_ = nullptr;

    QGroupBox* rotationGroup_ = nullptr;
    QRadioButton* rotation0Button_ = nullptr;
    QRadioButton* rotation90Button_ = nullptr;
    QRadioButton* rotation180Button_ = nullptr;
    QRadioButton* rotation270Button_ = nullptr;

    QDialogButtonBox* buttons_ = nullptr;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
