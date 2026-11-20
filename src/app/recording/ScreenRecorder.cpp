#include "app/recording/ScreenRecorder.h"

#include "app/recording/MotionJpegAviWriter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QList>
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

// 安全地拿到「当前的 GUI 应用」。
//
// 为什么要有这个检查：QGuiApplication::screens() / primaryScreen() 读的是
// QGuiApplication 的私有数据。只建了 QCoreApplication 的进程（比如没有界面的
// 单元测试）里那部分数据根本不存在，硬问会读到野指针。
QGuiApplication* guiApplication() {
    return qobject_cast<QGuiApplication*>(QCoreApplication::instance());
}

// 选区落在哪块屏上？
//
// 规则：先看选区中心点在哪块屏上；选区横跨多屏时（中心点不在任何一块屏里，
// 或者用户就是框了一个跨屏的大区域），取与选区交集面积最大的那块。
QScreen* screenForRegion(const QRect& region) {
    QGuiApplication* app = guiApplication();
    if (app == nullptr) return nullptr;
    const QList<QScreen*> screens = app->screens();
    if (screens.isEmpty()) return nullptr;

    const QPoint center = region.center();
    for (QScreen* screen : screens) {
        if (screen->geometry().contains(center)) return screen;
    }

    QScreen* best = nullptr;
    int bestArea = 0;
    for (QScreen* screen : screens) {
        const QRect overlap = screen->geometry().intersected(region);
        const int area = overlap.width() * overlap.height();
        if (area > bestArea) {
            bestArea = area;
            best = screen;
        }
    }
    return best != nullptr ? best : screens.first();
}

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
    // 录哪块屏就用哪块屏：多屏时窗口可能在副屏上，抓主屏会录到别的东西。
    QScreen* screen = screen_;
    if (screen == nullptr) {
        QGuiApplication* app = guiApplication();
        if (app != nullptr) screen = app->primaryScreen();
    }
    if (screen == nullptr) return QImage();
    const QPixmap pixmap = screen->grabWindow(0);
    if (pixmap.isNull()) return QImage();
    QImage image = pixmap.toImage();
    // 带上设备像素比：选区是逻辑坐标，图像是设备像素，不换算就会裁歪。
    image.setDevicePixelRatio(pixmap.devicePixelRatio());
    return image;
}

QRect ScreenRecorder::sourceRectFor(const QImage& image) const {
    // 注入帧源（单元测试）时没有屏幕信息，就把图像当成从 (0,0) 开始的一整块。
    if (provider_) return QRect(QPoint(0, 0), image.size());
    QGuiApplication* app = guiApplication();
    if (app == nullptr) return QRect(QPoint(0, 0), image.size());
    const QScreen* screen = screen_ != nullptr ? screen_ : app->primaryScreen();
    if (screen == nullptr) return QRect(QPoint(0, 0), image.size());
    const qreal dpr = image.devicePixelRatio() > 0 ? image.devicePixelRatio() : 1.0;
    // Qt 在多屏下 grabWindow(0) 可能只返回这一块屏，也可能返回整个虚拟桌面。
    // 用图像尺寸反推这张图对应的是哪一块，别写死一种行为。
    const QRect virtualRect = app->primaryScreen()->virtualGeometry();
    if (qRound(virtualRect.width() * dpr) == image.width() &&
        qRound(virtualRect.height() * dpr) == image.height()) {
        return virtualRect;
    }
    return screen->geometry();
}

QImage ScreenRecorder::prepareFrame() const {
    QImage image = grabFrame();
    if (image.isNull()) return QImage();

    if (cropRect_.isValid() && !cropRect_.isEmpty()) {
        // 选区是全局屏幕坐标，先换算成「这张图」的逻辑坐标，再换算成设备像素。
        const QRect source = sourceRectFor(image);
        const qreal dpr = image.devicePixelRatio() > 0 ? image.devicePixelRatio() : 1.0;
        QRect box = cropRect_.translated(-source.topLeft());
        box = QRect(qRound(box.x() * dpr), qRound(box.y() * dpr),
                    qRound(box.width() * dpr), qRound(box.height() * dpr));
        box = box.intersected(image.rect());
        if (box.isEmpty()) return QImage();
        image = image.copy(box);
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

    // 选区是全局屏幕坐标。抓屏按「选区落在哪块屏上」来决定录哪一块，
    // 这样窗口在副屏上时不会录到主屏去。
    cropRect_ = QRect();
    screen_ = nullptr;
    if (options_.region.isValid() && !options_.region.isEmpty()) {
        cropRect_ = options_.region;
        // 注入帧源时（单元测试）没有屏幕概念：帧源给什么画面就录什么画面。
        if (!provider_) {
            screen_ = screenForRegion(cropRect_);
            if (screen_ == nullptr) {
                QGuiApplication* app = guiApplication();
                if (app != nullptr) screen_ = app->primaryScreen();
            }
        }
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
