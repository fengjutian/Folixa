#include "app/dialogs/ViewSettingsDialog.h"

#include "app/ui/Language.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// 缩放百分比 → spinbox 的整数（10%–800%）。
constexpr int kMinZoomPercent = 10;
constexpr int kMaxZoomPercent = 800;

int toPercent(double factor) {
    const int percent = qRound(factor * 100.0);
    return qBound(kMinZoomPercent, percent, kMaxZoomPercent);
}

double fromPercent(int percent) {
    return static_cast<double>(percent) / 100.0;
}

}  // namespace

ViewSettingsDialog::ViewSettingsDialog(const ViewSettings& current, QWidget* parent)
    : QDialog(parent) {
    buildUi(current);
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &ViewSettingsDialog::retranslateUi);
}

void ViewSettingsDialog::buildUi(const ViewSettings& current) {
    setObjectName(QStringLiteral("viewSettingsDialog"));
    setWindowTitle(tr("视图设置"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // ---------------------------------------------------------- 缩放 ----
    zoomGroup_ = new QGroupBox(this);
    zoomGroup_->setObjectName(QStringLiteral("viewSettings.zoomGroup"));
    auto* zoomLayout = new QVBoxLayout(zoomGroup_);
    actualSizeButton_ = new QRadioButton(zoomGroup_);
    actualSizeButton_->setObjectName(QStringLiteral("viewSettings.actualSize"));
    fitWidthButton_ = new QRadioButton(zoomGroup_);
    fitWidthButton_->setObjectName(QStringLiteral("viewSettings.fitWidth"));
    fitPageButton_ = new QRadioButton(zoomGroup_);
    fitPageButton_->setObjectName(QStringLiteral("viewSettings.fitPage"));
    customZoomButton_ = new QRadioButton(zoomGroup_);
    customZoomButton_->setObjectName(QStringLiteral("viewSettings.customZoom"));
    auto* customRow = new QHBoxLayout();
    customZoomSpin_ = new QSpinBox(zoomGroup_);
    customZoomSpin_->setObjectName(QStringLiteral("viewSettings.customZoomSpin"));
    customZoomSpin_->setRange(kMinZoomPercent, kMaxZoomPercent);
    customZoomSuffix_ = new QLabel(zoomGroup_);
    texts_.bind([this] {
        actualSizeButton_->setText(tr("实际大小（100%）"));
        fitWidthButton_->setText(tr("适合宽度"));
        fitPageButton_->setText(tr("适合页面"));
        customZoomButton_->setText(tr("自定义"));
        customZoomSuffix_->setText(tr("%"));
        zoomGroup_->setTitle(tr("缩放"));
    });
    customRow->addWidget(customZoomSpin_);
    customRow->addWidget(customZoomSuffix_);
    customRow->addStretch(1);
    zoomLayout->addWidget(actualSizeButton_);
    zoomLayout->addWidget(fitWidthButton_);
    zoomLayout->addWidget(fitPageButton_);
    zoomLayout->addWidget(customZoomButton_);
    zoomLayout->addLayout(customRow);
    root->addWidget(zoomGroup_);

    // ------------------------------------------------------ 页面布局 ----
    layoutGroup_ = new QGroupBox(this);
    layoutGroup_->setObjectName(QStringLiteral("viewSettings.layoutGroup"));
    auto* layoutBox = new QVBoxLayout(layoutGroup_);
    continuousButton_ = new QRadioButton(layoutGroup_);
    continuousButton_->setObjectName(QStringLiteral("viewSettings.continuous"));
    singlePageButton_ = new QRadioButton(layoutGroup_);
    singlePageButton_->setObjectName(QStringLiteral("viewSettings.singlePage"));
    facingButton_ = new QRadioButton(layoutGroup_);
    facingButton_->setObjectName(QStringLiteral("viewSettings.facing"));
    facingCoverButton_ = new QRadioButton(layoutGroup_);
    facingCoverButton_->setObjectName(QStringLiteral("viewSettings.facingCover"));
    texts_.bind([this] {
        continuousButton_->setText(tr("连续滚动"));
        singlePageButton_->setText(tr("一次显示一页"));
        facingButton_->setText(tr("双页对开"));
        facingCoverButton_->setText(tr("双页（第一页单独作为封面）"));
        layoutGroup_->setTitle(tr("页面布局"));
    });
    layoutBox->addWidget(continuousButton_);
    layoutBox->addWidget(singlePageButton_);
    layoutBox->addWidget(facingButton_);
    layoutBox->addWidget(facingCoverButton_);
    root->addWidget(layoutGroup_);

    // ---------------------------------------------------------- 显示 ----
    displayGroup_ = new QGroupBox(this);
    displayGroup_->setObjectName(QStringLiteral("viewSettings.displayGroup"));
    auto* displayBox = new QVBoxLayout(displayGroup_);
    navigationCheck_ = new QCheckBox(displayGroup_);
    navigationCheck_->setObjectName(QStringLiteral("viewSettings.navigation"));
    aiCheck_ = new QCheckBox(displayGroup_);
    aiCheck_->setObjectName(QStringLiteral("viewSettings.aiPanel"));
    rulerCheck_ = new QCheckBox(displayGroup_);
    rulerCheck_->setObjectName(QStringLiteral("viewSettings.ruler"));
    texts_.bind([this] {
        navigationCheck_->setText(tr("导航面板"));
        aiCheck_->setText(tr("AI 助手面板"));
        rulerCheck_->setText(tr("标尺"));
        displayGroup_->setTitle(tr("显示"));
    });
    displayBox->addWidget(navigationCheck_);
    displayBox->addWidget(aiCheck_);
    displayBox->addWidget(rulerCheck_);
    root->addWidget(displayGroup_);

    // ---------------------------------------------------------- 旋转 ----
    rotationGroup_ = new QGroupBox(this);
    rotationGroup_->setObjectName(QStringLiteral("viewSettings.rotationGroup"));
    auto* rotationBox = new QVBoxLayout(rotationGroup_);
    rotation0Button_ = new QRadioButton(rotationGroup_);
    rotation0Button_->setObjectName(QStringLiteral("viewSettings.rotation0"));
    rotation90Button_ = new QRadioButton(rotationGroup_);
    rotation90Button_->setObjectName(QStringLiteral("viewSettings.rotation90"));
    rotation180Button_ = new QRadioButton(rotationGroup_);
    rotation180Button_->setObjectName(QStringLiteral("viewSettings.rotation180"));
    rotation270Button_ = new QRadioButton(rotationGroup_);
    rotation270Button_->setObjectName(QStringLiteral("viewSettings.rotation270"));
    texts_.bind([this] {
        rotation0Button_->setText(tr("0°"));
        rotation90Button_->setText(tr("90°"));
        rotation180Button_->setText(tr("180°"));
        rotation270Button_->setText(tr("270°"));
        rotationGroup_->setTitle(tr("视图旋转（不改文档）"));
    });
    rotationBox->addWidget(rotation0Button_);
    rotationBox->addWidget(rotation90Button_);
    rotationBox->addWidget(rotation180Button_);
    rotationBox->addWidget(rotation270Button_);
    root->addWidget(rotationGroup_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName(QStringLiteral("viewSettings.buttons"));
    connect(buttons_, &QDialogButtonBox::accepted, this, &ViewSettingsDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &ViewSettingsDialog::reject);
    root->addWidget(buttons_);

    // ------------------------------------------------------------ 初值 ----
    switch (current.zoomMode) {
        case ViewSettings::ZoomMode::FitWidth:
            fitWidthButton_->setChecked(true);
            break;
        case ViewSettings::ZoomMode::FitPage:
            fitPageButton_->setChecked(true);
            break;
        case ViewSettings::ZoomMode::Custom:
            customZoomButton_->setChecked(true);
            break;
        case ViewSettings::ZoomMode::ActualSize:
            actualSizeButton_->setChecked(true);
            break;
    }
    customZoomSpin_->setValue(toPercent(current.customZoom));
    switch (current.layoutMode) {
        case PageLayout::LayoutMode::SinglePage:
            singlePageButton_->setChecked(true);
            break;
        case PageLayout::LayoutMode::Facing:
            facingButton_->setChecked(true);
            break;
        case PageLayout::LayoutMode::FacingCover:
            facingCoverButton_->setChecked(true);
            break;
        case PageLayout::LayoutMode::Continuous:
            continuousButton_->setChecked(true);
            break;
    }
    navigationCheck_->setChecked(current.navigationVisible);
    aiCheck_->setChecked(current.aiVisible);
    rulerCheck_->setChecked(current.rulerVisible);
    switch (current.rotation) {
        case 90:
            rotation90Button_->setChecked(true);
            break;
        case 180:
            rotation180Button_->setChecked(true);
            break;
        case 270:
            rotation270Button_->setChecked(true);
            break;
        default:
            rotation0Button_->setChecked(true);
            break;
    }

    // 选中「自定义」时百分比框才可改；否则那个数字没有意义。
    connect(customZoomButton_, &QRadioButton::toggled, this,
            [this] { updateZoomEnabledState(); });
    updateZoomEnabledState();
}

void ViewSettingsDialog::retranslateUi() {
    texts_.retranslate();
    updateZoomEnabledState();
}

void ViewSettingsDialog::updateZoomEnabledState() {
    if (customZoomSpin_ == nullptr) {
        return;
    }
    customZoomSpin_->setEnabled(customZoomButton_ != nullptr && customZoomButton_->isChecked());
}

ViewSettings ViewSettingsDialog::settings() const {
    ViewSettings result;
    if (actualSizeButton_ != nullptr && actualSizeButton_->isChecked()) {
        result.zoomMode = ViewSettings::ZoomMode::ActualSize;
    } else if (fitWidthButton_ != nullptr && fitWidthButton_->isChecked()) {
        result.zoomMode = ViewSettings::ZoomMode::FitWidth;
    } else if (fitPageButton_ != nullptr && fitPageButton_->isChecked()) {
        result.zoomMode = ViewSettings::ZoomMode::FitPage;
    } else {
        result.zoomMode = ViewSettings::ZoomMode::Custom;
    }
    result.customZoom = fromPercent(customZoomSpin_->value());

    if (singlePageButton_ != nullptr && singlePageButton_->isChecked()) {
        result.layoutMode = PageLayout::LayoutMode::SinglePage;
    } else if (facingButton_ != nullptr && facingButton_->isChecked()) {
        result.layoutMode = PageLayout::LayoutMode::Facing;
    } else if (facingCoverButton_ != nullptr && facingCoverButton_->isChecked()) {
        result.layoutMode = PageLayout::LayoutMode::FacingCover;
    } else {
        result.layoutMode = PageLayout::LayoutMode::Continuous;
    }

    result.navigationVisible = navigationCheck_ != nullptr && navigationCheck_->isChecked();
    result.aiVisible = aiCheck_ != nullptr && aiCheck_->isChecked();
    result.rulerVisible = rulerCheck_ != nullptr && rulerCheck_->isChecked();

    if (rotation90Button_ != nullptr && rotation90Button_->isChecked()) {
        result.rotation = 90;
    } else if (rotation180Button_ != nullptr && rotation180Button_->isChecked()) {
        result.rotation = 180;
    } else if (rotation270Button_ != nullptr && rotation270Button_->isChecked()) {
        result.rotation = 270;
    } else {
        result.rotation = 0;
    }
    return result;
}

void ViewSettingsDialog::accept() {
    // 「自定义」但百分比没填（或超出范围）时不让过：
    // spinbox 已经限了范围，这里再兜一道，免得将来加别的入口时漏掉。
    if (customZoomButton_ != nullptr && customZoomButton_->isChecked() &&
        customZoomSpin_->value() <= 0) {
        return;
    }
    QDialog::accept();
}

}  // namespace aipdf::app
