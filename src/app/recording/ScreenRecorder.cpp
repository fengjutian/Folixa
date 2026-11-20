#include "app/recording/ScreenRecorder.h"

#include "app/controllers/ScreenCapture.h"
#include "app/recording/MotionJpegAviWriter.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QTimer>

namespace aipdf::app {
namespace {

// 连续抓不到画面的次数上限。
//
// 为什么要有上限：抓屏在某些环境（远程桌面断开、锁屏）会一直返回空图，
// 不设上限就会一直录一个 0 帧的空文件，用户还以为录了十分钟。
constexpr int kMaxConsecutiveFailures = 30;

}  // namespace

ScreenRecorder::ScreenRecorder(QObject* parent) : QObject(parent) {
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &ScreenRecorder::onTimeout);
}

ScreenRecorder::~ScreenRecorder() {
    if (recording_) cancel();
}

void ScreenRecorder::setFrameProvider(FrameProvider provider) {
    provider_ = std::move(provider);
}

QImage ScreenRecorder::grabFrame() const {
    if (provider_) return provider_();
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        return screen->grabWindow(0).toImage();
    }
    return QImage();
}

QImage ScreenRecorder::prepareFrame() const {
    QImage image = grabFrame();
    if (image.isNull()) return QImage();

    if (cropRect_.isValid() && !cropRect_.isEmpty()) {
        image = cropRegion(image, cropRect_);
        if (image.isNull()) return QImage();
    }
    if (options_.maxWidth > 0 && image.width() > options_.maxWidth) {
        image = image.scaledToWidth(options_.maxWidth, Qt::SmoothTransformation);
    }
    return image.convertToFormat(QImage::Format_RGB32);
}

QByteArray ScreenRecorder::encodeJpeg(const QImage& image) const {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) return QByteArray();
    const int quality = qBound(1, options_.jpegQuality, 100);
    const bool ok = image.save(&buffer, "JPG", quality);
    buffer.close();
    return ok ? bytes : QByteArray();
}

qint64 ScreenRecorder::elapsedMs() const {
    if (startedAtMs_ <= 0) return 0;
    return QDateTime::currentMSecsSinceEpoch() - startedAtMs_;
}

bool ScreenRecorder::start(const Options& options, const QString& outputPath, QString* error) {
    if (recording_) {
        if (error != nullptr) *error = QStringLiteral("已经在录了。");
        return false;
    }
    if (outputPath.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("没有给输出文件路径。");
        return false;
    }
    if (options.framesPerSecond <= 0 || options.framesPerSecond > 60) {
        if (error != nullptr) *error = QStringLiteral("帧率要在 1 到 60 之间。");
        return false;
    }

    options_ = options;
    outputPath_ = outputPath;
    frameCount_ = 0;
    consecutiveFailures_ = 0;

    // 选区是屏幕坐标，而抓到的画面是从屏幕左上角开始的：多屏时两者差一个偏移。
    cropRect_ = QRect();
    if (options_.region.isValid() && !options_.region.isEmpty()) {
        QRect screenOrigin;
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            screenOrigin = screen->geometry();
        }
        cropRect_ = options_.region.translated(-screenOrigin.topLeft());
    }

    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    file_ = new QFile(outputPath);
    if (!file_->open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("写不进录制文件：%1").arg(file_->errorString());
        }
        cleanup();
        QFile::remove(outputPath);
        return false;
    }

    // 开录失败时不留下空文件：用户看到「录制没开始」，磁盘上也不该有个 0 字节的 .avi。
    const auto abortStart = [this, outputPath]() {
        cleanup();
        QFile::remove(outputPath);
        return false;
    };

    const QImage first = prepareFrame();
    if (first.isNull()) {
        if (error != nullptr) *error = QStringLiteral("抓不到画面，录制没有开始。");
        return abortStart();
    }
    width_ = first.width();
    height_ = first.height();

    writer_ = std::make_unique<MotionJpegAviWriter>(file_, width_, height_,
                                                    options_.framesPerSecond);
    if (!writer_->begin(error)) {
        cleanup();
        QFile::remove(outputPath);
        return false;
    }

    const QByteArray jpeg = encodeJpeg(first);
    if (jpeg.isEmpty() || !writer_->appendFrame(jpeg, error)) {
        cleanup();
        QFile::remove(outputPath);
        return false;
    }
    frameCount_ = 1;
    startedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    recording_ = true;

    timer_->start(qMax(20, 1000 / options_.framesPerSecond));
    emit frameAppended(frameCount_);
    return true;
}

bool ScreenRecorder::captureFrame(QString* error) {
    if (!recording_ || writer_ == nullptr) {
        if (error != nullptr) *error = QStringLiteral("没有在录制。");
        return false;
    }
    const QImage image = prepareFrame();
    if (image.isNull()) {
        ++consecutiveFailures_;
        if (error != nullptr) *error = QStringLiteral("这一帧没抓到画面。");
        return false;
    }
    const QByteArray jpeg = encodeJpeg(image);
    if (jpeg.isEmpty()) {
        ++consecutiveFailures_;
        if (error != nullptr) *error = QStringLiteral("这一帧编码失败。");
        return false;
    }
    if (!writer_->appendFrame(jpeg, error)) {
        ++consecutiveFailures_;
        return false;
    }
    consecutiveFailures_ = 0;
    ++frameCount_;
    emit frameAppended(frameCount_);
    return true;
}

void ScreenRecorder::onTimeout() {
    if (!recording_) return;
    QString error;
    if (!captureFrame(&error)) {
        // 连续失败太多次就收尾：留着打不开的文件比没有文件更糟。
        if (consecutiveFailures_ >= kMaxConsecutiveFailures) {
            stop(&error);
        }
    }
}

bool ScreenRecorder::stop(QString* error) {
    if (!recording_) return true;
    timer_->stop();
    bool ok = true;
    if (writer_ != nullptr) {
        ok = writer_->finish(error);
    }
    const QString path = outputPath_;
    cleanup();
    recording_ = false;
    emit finished(ok, path, ok ? QString() : (error != nullptr ? *error : QString()));
    return ok;
}

void ScreenRecorder::cancel() {
    if (!recording_ && file_ == nullptr) return;
    timer_->stop();
    const QString path = outputPath_;
    cleanup();
    recording_ = false;
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
    emit finished(false, QString(), QStringLiteral("录制已取消。"));
}

void ScreenRecorder::cleanup() {
    writer_.reset();
    if (file_ != nullptr) {
        file_->close();
        delete file_;
        file_ = nullptr;
    }
}

}  // namespace aipdf::app
