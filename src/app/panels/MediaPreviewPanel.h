#pragma once

#include "pdf/media/MediaPlayback.h"
#include "pdf/media/MediaSource.h"

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include <memory>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace aipdf::app {

class PcmPlayer;

// ============================================================================
// 取帧的来源
// ============================================================================
//
// 为什么抽这一层：面板的时序逻辑（该出帧时出帧、播完就停、拖动定位）
// 必须能在**没有媒体文件**的地方测。领域层的解码器要真的媒体文件才能出帧，
// 把它直接写死在面板里，界面层的逻辑就只能靠真文件才能测——
// 而真文件不一定在（CI 上就没有）。
//
// 所以面板只认这个接口：正式环境塞解码器（见 DecoderFrameProvider），
// 测试塞一个假的，自己喂帧。
class MediaFrameProvider {
public:
    virtual ~MediaFrameProvider() = default;

    // 打开媒体。失败时 error 里有中文原因，
    // 并且 info().errorKind 说明是哪一类失败（打不开 / 不是媒体 / 解码失败）。
    virtual bool open(const QString& path, QString* error) = 0;

    // 取下一帧。frame->endOfStream 为真表示到此为止（播完了）。
    virtual bool nextFrame(pdf::MediaFrame* frame, QString* error) = 0;

    // 关闭并释放系统句柄。可以重复调用。
    virtual void close() = 0;

    // 已经读到的基本信息。
    virtual pdf::MediaInfo info() const = 0;
};

// 媒体信息的中文摘要（多行，一行一项）。
//
// 为什么单独做成自由函数：界面显示的这段文字是**纯逻辑**，
// 不该被窗口的生死绑住——这样能拿假数据直接测。
QString mediaPreviewInfoText(const pdf::MediaInfo& info);

// 三类失败的中文提示。
//
// 三类必须给**三句不同的话**：打不开、不是媒体、解码失败，
// 要修的地方完全不一样。错误分类是给程序看的，中文是给人看的。
QString mediaPreviewErrorText(pdf::MediaErrorKind kind);

// 时长的显示格式：分:秒；超过一小时用 时:分:秒。
QString mediaPreviewTimeText(qint64 ms);

// 画面区的提示文字。
//
// 音频文件必须**明说「没有画面」**：留一块空白会让人以为程序没加载出来。
QString mediaPreviewVideoHintText(const pdf::MediaInfo& info);

// ============================================================================
// 媒体预览面板
// ============================================================================
//
// 一个可复用的 QWidget：显示媒体信息、预览画面、播放 / 暂停 / 停止、拖动定位。
//
// 职责边界：
//   * 领域层（pdf/media）负责「读信息、解帧」，这里不碰 Media Foundation；
//   * 声卡（PcmPlayer）负责「把 PCM 放出去」，这里不碰 waveOut；
//   * 这里只做界面与时序：用 MediaPlaybackController 管播放状态，
//     用定时器驱动 advanceTo()，该出帧时向取帧来源要一帧。
class MediaPreviewPanel final : public QWidget {
    Q_OBJECT

public:
    explicit MediaPreviewPanel(QWidget* parent = nullptr);
    ~MediaPreviewPanel() override;

    // 换掉取帧来源。传入的指针**不由面板拥有**；传 nullptr 换回默认的解码器。
    void setFrameProvider(MediaFrameProvider* provider);

    // 设置要预览的媒体文件。会读信息、刷新界面。
    // 打不开 / 不是媒体文件时，界面显示对应的错误文案。
    void setSourcePath(const QString& path);
    QString sourcePath() const;

    // 播放 / 暂停 / 停止。
    void play();
    void pause();
    void stop();

    // 拖到指定位置（毫秒）。
    void seekTo(qint64 positionMs);

    // 当前播放位置（毫秒）与总时长（毫秒）。
    qint64 positionMs() const;
    qint64 durationMs() const;

    // 播放状态。
    pdf::MediaPlaybackState playbackState() const;

    // 界面上的错误文案（空表示没有错误）。
    QString errorText() const;
    // 错误的分类（None 表示没有错误）。
    pdf::MediaErrorKind errorKind() const;
    // 错误的补充说明（文件路径、HRESULT 之类的细节）。
    QString errorDetail() const;

    // 媒体信息摘要。
    QString mediaInfoText() const;
    // 画面区的提示文字。
    QString videoHintText() const;
    // 画面区当前的文字（没有画面时为空）。
    QString videoAreaText() const;
    // 进度条上的「当前位置 / 总时长」。
    QString positionLabelText() const;
    // 当前这一帧（验收与测试用）。
    QImage currentFrame() const;

    // 推进播放：定时器每拍调一次。
    //
    // 单独暴露出来，是因为时序逻辑要能被测试**直接驱动**，
    // 不必真的等定时器（真等会引入时间抖动，测不稳）。
    void advancePlayback(qint64 wallClockMs);

protected:
    // 窗口尺寸变了要重新缩放画面。
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void retranslateUi();
    // 复位：停掉解码器与声卡，位置回到 0。
    void resetPlayback();
    void updateControls();
    void updateInfoArea();
    void showFrame(const QImage& image);
    void pushAudio(const QByteArray& pcm);
    // 播完：停住，不再出帧。
    void markFinished();
    void setError(pdf::MediaErrorKind kind, const QString& detail);
    // 定位之后把解码器快进到目标位置（顺序解码只能往前读）。
    void skipToPosition(qint64 positionMs);
    // 「当前位置 / 总时长」这句话（位置可以另外给，用于拖动时的即时反馈）。
    QString positionLabelTextFor(qint64 positionMs) const;
    void updatePositionLabel(qint64 positionMs);

    // 默认的取帧来源（包着领域层的解码器）。
    std::unique_ptr<MediaFrameProvider> ownedProvider_;
    // 当前用的取帧来源。
    MediaFrameProvider* provider_ = nullptr;
    // 声卡。
    std::unique_ptr<PcmPlayer> player_;
    // 播放时序。
    pdf::MediaPlaybackController controller_;

    QString sourcePath_;
    pdf::MediaInfo info_;
    pdf::MediaErrorKind errorKind_ = pdf::MediaErrorKind::None;
    QString errorText_;
    QString errorDetail_;
    QImage currentFrame_;
    // 播完过（用来决定「再按播放」是不是从头开始）。
    bool finished_ = false;

    QLabel* infoLabel_ = nullptr;
    QLabel* videoLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QLabel* detailLabel_ = nullptr;
    QLabel* positionLabel_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QPushButton* pauseButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QSlider* slider_ = nullptr;
    QTimer* timer_ = nullptr;
    // 单调递增的墙钟：暂停期间过掉的时间不算进播放位置。
    QElapsedTimer clock_;
};

}  // namespace aipdf::app
