#include "app/panels/MediaPreviewPanel.h"

#include "app/controllers/PcmPlayer.h"
#include "app/ui/Language.h"

#include "i18n/Translate.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// 画面解出来的最大宽度。
//
// 为什么要限：面板就那么大，解一张 4K 帧既慢又占内存。
constexpr int kMaxFrameWidth = 640;

// 默认的取帧来源：包着领域层的解码器。
//
// 为什么不直接在面板里 new 一个 MediaDecoder：面板的时序逻辑要能在
// 没有媒体文件的地方测（见 MediaFrameProvider 的说明），
// 把解码器藏在这一层后面，换个实现就行。
class DecoderFrameProvider final : public MediaFrameProvider {
public:
    bool open(const QString& path, QString* error) override {
        close();
        decoder_ = std::make_unique<pdf::MediaDecoder>(path);
        pdf::MediaDecoderOptions options;
        options.maxFrameWidth = kMaxFrameWidth;
        options.decodeAudio = true;
        return decoder_->open(options, error);
    }

    bool nextFrame(pdf::MediaFrame* frame, QString* error) override {
        if (!decoder_) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::MediaPreview", "解码器还没有打开。");
            }
            return false;
        }
        return decoder_->nextFrame(frame, error);
    }

    void close() override {
        if (decoder_) decoder_->close();
    }

    pdf::MediaInfo info() const override {
        return decoder_ ? decoder_->info() : pdf::MediaInfo();
    }

private:
    std::unique_ptr<pdf::MediaDecoder> decoder_;
};

}  // namespace

// ============================================================================
// 纯逻辑：信息文案、时间格式、错误文案
// ============================================================================

QString mediaPreviewTimeText(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString mediaPreviewInfoText(const pdf::MediaInfo& info) {
    if (!info.ok) {
        return i18n::tr("aipdf::app::MediaPreview", "还没有读到媒体信息。");
    }

    QStringList lines;
    lines << i18n::tr("aipdf::app::MediaPreview", "类型：%1").arg(
        info.mediaType == QStringLiteral("video")
            ? i18n::tr("aipdf::app::MediaPreview", "视频")
            : i18n::tr("aipdf::app::MediaPreview", "音频"));
    lines << i18n::tr("aipdf::app::MediaPreview", "时长：%1").arg(mediaPreviewTimeText(info.durationMs));
    if (info.width > 0 && info.height > 0) {
        lines << i18n::tr("aipdf::app::MediaPreview", "画面尺寸：%1 × %2")
                   .arg(info.width)
                   .arg(info.height);
    }
    if (info.frameRate > 0.0) {
        lines << i18n::tr("aipdf::app::MediaPreview", "帧率：%1 帧/秒")
                   .arg(QString::number(info.frameRate, 'f', 2));
    }
    if (!info.codecName.isEmpty()) {
        lines << i18n::tr("aipdf::app::MediaPreview", "编码：%1").arg(info.codecName);
    }
    if (info.sampleRate > 0) {
        lines << i18n::tr("aipdf::app::MediaPreview", "采样率：%1 Hz").arg(info.sampleRate);
    }
    if (info.channelCount > 0) {
        lines << i18n::tr("aipdf::app::MediaPreview", "声道数：%1").arg(info.channelCount);
    }
    return lines.join(QLatin1Char('\n'));
}

QString mediaPreviewErrorText(pdf::MediaErrorKind kind) {
    switch (kind) {
    case pdf::MediaErrorKind::None:
        return QString();
    case pdf::MediaErrorKind::OpenFailed:
        return i18n::tr("aipdf::app::MediaPreview", "打不开这个媒体文件。");
    case pdf::MediaErrorKind::NotMedia:
        return i18n::tr("aipdf::app::MediaPreview", "这不是媒体文件，里面没有可以预览的音视频内容。");
    case pdf::MediaErrorKind::DecodeFailed:
        return i18n::tr("aipdf::app::MediaPreview", "解码失败，这个文件放不出内容。");
    }
    return QString();
}

QString mediaPreviewVideoHintText(const pdf::MediaInfo& info) {
    if (!info.ok) return QString();
    // 音频没有画面：**明说**，不要留一块空白——
    // 空白会让人以为程序没加载出来，而不是「这个文件本来就没画面」。
    if (info.mediaType == QStringLiteral("audio")) {
        return i18n::tr("aipdf::app::MediaPreview", "这是音频，没有画面。");
    }
    if (info.width <= 0 && info.height <= 0) {
        return i18n::tr("aipdf::app::MediaPreview", "这是音频，没有画面。");
    }
    return QString();
}

// ============================================================================
// MediaPreviewPanel
// ============================================================================

MediaPreviewPanel::MediaPreviewPanel(QWidget* parent) : QWidget(parent) {
    ownedProvider_ = std::make_unique<DecoderFrameProvider>();
    provider_ = ownedProvider_.get();

    buildUi();

    // 切换语言后常驻文案就地重译；随状态变化的文案由 update*() 重算。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &MediaPreviewPanel::retranslateUi);
}

MediaPreviewPanel::~MediaPreviewPanel() {
    // 析构也要把解码器与声卡关掉：面板可能没被 close 过就被删掉了。
    timer_->stop();
    if (player_) player_->close();
    if (provider_ != nullptr) provider_->close();
}

void MediaPreviewPanel::buildUi() {
    setObjectName(QStringLiteral("mediaPreviewPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    infoLabel_ = new QLabel(this);
    infoLabel_->setObjectName(QStringLiteral("mediaPreviewInfo"));
    infoLabel_->setWordWrap(true);
    infoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(infoLabel_);

    // 画面区：视频帧显示在这里；音频文件时这里显示「没有画面」的说明。
    videoLabel_ = new QLabel(this);
    videoLabel_->setObjectName(QStringLiteral("mediaPreviewVideo"));
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setMinimumHeight(160);
    videoLabel_->setFrameShape(QFrame::StyledPanel);
    videoLabel_->setFrameShadow(QFrame::Sunken);
    layout->addWidget(videoLabel_, 1);

    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("mediaPreviewError"));
    errorLabel_->setWordWrap(true);
    layout->addWidget(errorLabel_);

    detailLabel_ = new QLabel(this);
    detailLabel_->setObjectName(QStringLiteral("secondaryText"));
    detailLabel_->setWordWrap(true);
    layout->addWidget(detailLabel_);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setObjectName(QStringLiteral("mediaPreviewSlider"));
    slider_->setRange(0, 0);
    slider_->setEnabled(false);
    layout->addWidget(slider_);

    positionLabel_ = new QLabel(this);
    positionLabel_->setObjectName(QStringLiteral("mediaPreviewPosition"));
    layout->addWidget(positionLabel_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    playButton_ = new QPushButton(this);
    playButton_->setObjectName(QStringLiteral("mediaPreviewPlay"));
    pauseButton_ = new QPushButton(this);
    pauseButton_->setObjectName(QStringLiteral("mediaPreviewPause"));
    stopButton_ = new QPushButton(this);
    stopButton_->setObjectName(QStringLiteral("mediaPreviewStop"));
    buttons->addWidget(playButton_);
    buttons->addWidget(pauseButton_);
    buttons->addWidget(stopButton_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    timer_ = new QTimer(this);
    timer_->setInterval(40);
    connect(timer_, &QTimer::timeout, this, [this]() {
        advancePlayback(clock_.isValid() ? clock_.elapsed() : 0);
    });

    connect(playButton_, &QPushButton::clicked, this, &MediaPreviewPanel::play);
    connect(pauseButton_, &QPushButton::clicked, this, &MediaPreviewPanel::pause);
    connect(stopButton_, &QPushButton::clicked, this, &MediaPreviewPanel::stop);

    // 拖动时先更新数字，松手才真的定位：
    // 每挪一格就重开一次解码器，界面会卡成幻灯片。
    connect(slider_, &QSlider::sliderPressed, this, [this]() { sliderPressed_ = true; });
    connect(slider_, &QSlider::sliderMoved, this,
            [this](int value) { updatePositionLabel(value); });
    connect(slider_, &QSlider::sliderReleased, this, [this]() {
        sliderPressed_ = false;
        seekTo(slider_->value());
    });

    retranslateUi();
    updateInfoArea();
    updateControls();
}

void MediaPreviewPanel::retranslateUi() {
    playButton_->setText(i18n::tr("aipdf::app::MediaPreview", "播放"));
    playButton_->setAccessibleName(i18n::tr("aipdf::app::MediaPreview", "播放"));
    pauseButton_->setText(i18n::tr("aipdf::app::MediaPreview", "暂停"));
    pauseButton_->setAccessibleName(i18n::tr("aipdf::app::MediaPreview", "暂停"));
    stopButton_->setText(i18n::tr("aipdf::app::MediaPreview", "停止"));
    stopButton_->setAccessibleName(i18n::tr("aipdf::app::MediaPreview", "停止"));
    infoLabel_->setAccessibleName(i18n::tr("aipdf::app::MediaPreview", "媒体信息"));
    videoLabel_->setAccessibleName(i18n::tr("aipdf::app::MediaPreview", "画面"));
    slider_->setAccessibleName(i18n::tr("aipdf::app::MediaPreview", "播放进度"));
    updateInfoArea();
    updateControls();
}

void MediaPreviewPanel::setFrameProvider(MediaFrameProvider* provider) {
    if (provider == provider_) return;
    resetPlayback();
    provider_ = provider != nullptr ? provider : ownedProvider_.get();
}

void MediaPreviewPanel::setSourcePath(const QString& path) {
    // 换文件先复位：旧的解码器与声卡必须关掉，
    // 否则旧文件的画面还留在屏幕上、声音还在放。
    resetPlayback();

    sourcePath_ = path;
    info_ = pdf::MediaInfo();
    errorKind_ = pdf::MediaErrorKind::None;
    errorText_.clear();
    errorDetail_.clear();

    if (path.isEmpty()) {
        updateInfoArea();
        updateControls();
        return;
    }

    QString why;
    if (!provider_->open(path, &why)) {
        const pdf::MediaInfo failed = provider_->info();
        // 打不开 / 不是媒体 / 解码失败，三类给三句不同的话。
        setError(failed.errorKind == pdf::MediaErrorKind::None ? pdf::MediaErrorKind::OpenFailed
                                                              : failed.errorKind,
                 why);
        updateInfoArea();
        updateControls();
        return;
    }

    info_ = provider_->info();
    controller_.setDurationMs(info_.durationMs);
    controller_.setFrameRate(info_.frameRate);
    updateInfoArea();
    updateControls();
}

QString MediaPreviewPanel::sourcePath() const { return sourcePath_; }

void MediaPreviewPanel::play() {
    if (errorKind_ != pdf::MediaErrorKind::None) return;
    if (!info_.ok) return;

    if (finished_ || controller_.state() == pdf::MediaPlaybackState::Finished) {
        // 播完再按播放 = 从头重放。解码器已经在末尾了，要重开。
        QString why;
        if (!provider_->open(sourcePath_, &why)) {
            setError(pdf::MediaErrorKind::OpenFailed, why);
            updateControls();
            return;
        }
        finished_ = false;
    }

    controller_.play();
    clock_.restart();
    timer_->setInterval(static_cast<int>(controller_.frameIntervalMs()));
    timer_->start();
    updateControls();
}

void MediaPreviewPanel::pause() {
    controller_.pause();
    if (player_) player_->pause();
    updateControls();
}

void MediaPreviewPanel::stop() {
    timer_->stop();
    controller_.stop();
    finished_ = false;
    if (player_) player_->stop();
    // 解码器也要回到开头：否则「停止后再播放」会从解码器的当前位置接着走，
    // 而进度条从 0 开始，两边对不上。
    if (!sourcePath_.isEmpty() && info_.ok) {
        QString why;
        provider_->open(sourcePath_, &why);
    }
    updateControls();
}

void MediaPreviewPanel::seekTo(qint64 positionMs) {
    if (!info_.ok) return;
    controller_.seekTo(positionMs);
    finished_ = controller_.state() == pdf::MediaPlaybackState::Finished;
    // 解码器是**顺序读**的，往回走只能重开再快进：
    // 把目标位置之前的帧解出来扔掉，扔完的地方就是新的起点。
    QString why;
    if (!provider_->open(sourcePath_, &why)) {
        setError(pdf::MediaErrorKind::OpenFailed, why);
        updateControls();
        return;
    }
    skipToPosition(positionMs);
    updateControls();
}

void MediaPreviewPanel::skipToPosition(qint64 positionMs) {
    // 快进的帧数有上限：解一整段长视频会卡住界面，
    // 宁可少跳一点，也不要让窗口僵在那里。
    constexpr int kMaxSkipFrames = 600;
    const qint64 targetUs = positionMs * 1000;
    pdf::MediaFrame frame;
    for (int i = 0; i < kMaxSkipFrames; ++i) {
        QString why;
        if (!provider_->nextFrame(&frame, &why)) return;
        if (frame.endOfStream) return;
        if (frame.timestampUs >= targetUs) return;
    }
}

qint64 MediaPreviewPanel::positionMs() const { return controller_.positionMs(); }

qint64 MediaPreviewPanel::durationMs() const { return controller_.durationMs(); }

pdf::MediaPlaybackState MediaPreviewPanel::playbackState() const {
    return controller_.state();
}

QString MediaPreviewPanel::errorText() const { return errorText_; }

pdf::MediaErrorKind MediaPreviewPanel::errorKind() const { return errorKind_; }

QString MediaPreviewPanel::errorDetail() const { return errorDetail_; }

QString MediaPreviewPanel::mediaInfoText() const { return mediaPreviewInfoText(info_); }

QString MediaPreviewPanel::videoHintText() const { return mediaPreviewVideoHintText(info_); }

QString MediaPreviewPanel::videoAreaText() const { return videoLabel_->text(); }

QString MediaPreviewPanel::positionLabelText() const {
    return positionLabelTextFor(controller_.positionMs());
}

QImage MediaPreviewPanel::currentFrame() const { return currentFrame_; }

QString MediaPreviewPanel::positionLabelTextFor(qint64 positionMs) const {
    return QStringLiteral("%1 / %2")
        .arg(mediaPreviewTimeText(positionMs), mediaPreviewTimeText(controller_.durationMs()));
}

void MediaPreviewPanel::updatePositionLabel(qint64 positionMs) {
    positionLabel_->setText(positionLabelTextFor(positionMs));
}

void MediaPreviewPanel::advancePlayback(qint64 wallClockMs) {
    if (controller_.state() != pdf::MediaPlaybackState::Playing) {
        updateControls();
        return;
    }

    pdf::MediaFrame frame;
    if (!controller_.advanceTo(wallClockMs, &frame)) {
        // 这一拍不用出帧：还没到点，或者已经播完。
        if (controller_.state() == pdf::MediaPlaybackState::Finished) {
            markFinished();
        }
        updateControls();
        return;
    }

    QString why;
    if (!provider_->nextFrame(&frame, &why)) {
        setError(pdf::MediaErrorKind::DecodeFailed, why);
        timer_->stop();
        controller_.stop();
        updateControls();
        return;
    }

    if (frame.endOfStream) {
        // 播完了：停住，不再空转出帧。
        markFinished();
        updateControls();
        return;
    }

    if (!frame.image.isNull()) showFrame(frame.image);
    if (!frame.pcm.isEmpty()) pushAudio(frame.pcm);
    updateControls();
}

void MediaPreviewPanel::resetPlayback() {
    timer_->stop();
    controller_.stop();
    finished_ = false;
    clock_.invalidate();
    // 换文件 / 关闭时：解码器与声卡都要收干净。
    if (player_) player_->close();
    if (provider_ != nullptr) provider_->close();
    currentFrame_ = QImage();
    if (videoLabel_ != nullptr) videoLabel_->clear();
}

void MediaPreviewPanel::updateControls() {
    const bool hasMedia = info_.ok && errorKind_ == pdf::MediaErrorKind::None;
    const bool playing = controller_.state() == pdf::MediaPlaybackState::Playing;

    playButton_->setEnabled(hasMedia && !playing);
    pauseButton_->setEnabled(hasMedia && playing);
    stopButton_->setEnabled(hasMedia && controller_.state() != pdf::MediaPlaybackState::Stopped);
    slider_->setEnabled(hasMedia);

    slider_->setRange(0, static_cast<int>(qBound<qint64>(qint64(0), controller_.durationMs(),
                                                        qint64(1000000))));
    // 用户正按着滑块时不要改它的值：否则滑块会被拽回播放位置，拖不动。
    if (!sliderPressed_) {
        slider_->setValue(static_cast<int>(qBound<qint64>(qint64(0), controller_.positionMs(),
                                                          controller_.durationMs())));
    }

    updatePositionLabel(controller_.positionMs());

    errorLabel_->setText(errorText_);
    detailLabel_->setText(errorDetail_);
    detailLabel_->setVisible(!errorDetail_.isEmpty());
}

void MediaPreviewPanel::updateInfoArea() {
    infoLabel_->setText(mediaPreviewInfoText(info_));

    const QString hint = mediaPreviewVideoHintText(info_);
    if (currentFrame_.isNull()) {
        // 没有画面时把说明写在画面区里：不留空白。
        // 先清图片再写文字：QLabel 上有图时文字是不显示的。
        videoLabel_->setPixmap(QPixmap());
        videoLabel_->setText(hint);
    } else {
        showFrame(currentFrame_);
    }
}

void MediaPreviewPanel::showFrame(const QImage& image) {
    currentFrame_ = image;
    const QSize target = videoLabel_->size();
    if (target.width() <= 0 || target.height() <= 0) return;
    // 按面板大小等比缩放：不能超出面板，也不能把画面拉变形。
    const QImage scaled = image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    videoLabel_->setText(QString());
    videoLabel_->setPixmap(QPixmap::fromImage(scaled));
}

void MediaPreviewPanel::pushAudio(const QByteArray& pcm) {
    if (pcm.isEmpty()) return;
    if (!player_) player_ = std::make_unique<PcmPlayer>();
    if (!player_->isOpen()) {
        // 声卡懒打开：真的拿到声音数据才开设备，
        // 免得用户只是看看画面也占着声卡。
        QString why;
        const int rate = info_.sampleRate > 0 ? info_.sampleRate : 44100;
        const int channels = info_.channelCount > 0 ? info_.channelCount : 2;
        if (!player_->open(rate, channels, &why)) {
            // 没有声卡不该打断播放：画面照放，只把原因记在细节行里。
            errorDetail_ = i18n::tr("aipdf::app::MediaPreview", "声音放不出来：%1").arg(why);
            return;
        }
    }
    QString why;
    if (!player_->pushPcm(pcm, &why)) {
        errorDetail_ = i18n::tr("aipdf::app::MediaPreview", "声音放不出来：%1").arg(why);
    }
}

void MediaPreviewPanel::markFinished() {
    // 播完就停住：定时器停、位置停在总时长上、不再出帧。
    timer_->stop();
    if (player_) player_->stop();
    if (controller_.durationMs() <= 0) {
        // 有些容器不报时长。用当前位置当总长，
        // 否则状态机走不到「已播完」，界面会一直空转出帧。
        controller_.setDurationMs(qMax<qint64>(1, controller_.positionMs()));
    }
    controller_.seekTo(controller_.durationMs());
    finished_ = true;
}

void MediaPreviewPanel::setError(pdf::MediaErrorKind kind, const QString& detail) {
    errorKind_ = kind;
    errorText_ = mediaPreviewErrorText(kind);
    errorDetail_ = detail;
}

void MediaPreviewPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // 面板大小变了，画面要跟着重新缩放，不然会超出面板或者留边。
    if (!currentFrame_.isNull()) showFrame(currentFrame_);
}

}  // namespace aipdf::app
