#pragma once

#include "i18n/Translate.h"

#include <QByteArray>
#include <QDialog>
#include <QString>
#include <QVector>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace aipdf::app {

// 扫描失败的原因种类。
//
// 为什么是枚举而不是一句现成的中文：「没装扫描仪」「设备被占用」「用户自己取消了」
// 是三件完全不同的事，给用户同一句「扫描失败」等于让人自己猜。
// 枚举把「是哪一类」固定下来，中文句子由 scanErrorMessage() 统一拼，
// 翻译时也只改那一处。
enum class ScanErrorKind {
    None,
    EnumerationFailed,  // WIA 本身起不来（服务没开 / COM 出错 / 没装采集组件）
    NoDevice,           // WIA 在了，但本机一台扫描仪都没有
    UserCancelled,      // 用户自己放弃
    TransferFailed,     // 设备在，但取帧失败（被占用、卡纸、驱动报错）
    UnsupportedImage,   // 取到了帧，但图像格式不是 BMP/JPEG
    PdfFailed,          // 帧取到了，拼 PDF 失败
};

// 把失败种类翻成一句给用户看的话。
//
// detail 是底层给的技术细节（英文/技术性），拼在括号里；
// 它不是翻译对象，所以单独用一句固定格式拼上去。
QString scanErrorMessage(ScanErrorKind kind, const QString& detail = QString());

// 本机的一台图像设备。
struct ScannerDevice {
    // WIA 的设备 ID（形如 \\?\usb#...）。回头找设备靠它，不靠名字。
    QString deviceId;
    // 设备名（WIA 的 "Name" 属性）。
    QString name;
    // WIA 设备类型：1 = 扫描仪，2 = 相机，3 = 摄像头。
    int deviceType = 0;

    bool isScanner() const { return deviceType == 1; }
    // 列表里显示的文字。
    QString displayName() const;
};

// 扫描参数。
struct ScannerCaptureOptions {
    // 扫描分辨率（DPI）。会写进 WIA 的分辨率属性（设备不支持时忽略）。
    int dpi = 300;
    // 最多取几页（0 = 不限）。自动进纸器一次出多页时用得上。
    int maxPages = 0;
};

// 一次扫描的结果。
struct ScanOutcome {
    bool ok = false;
    // 失败原因种类（成功时是 None）。
    ScanErrorKind errorKind = ScanErrorKind::None;
    // 给用户看的中文原因（失败时非空）。
    QString errorMessage;
    // 技术细节（失败时的英文/技术性说明）。
    QString errorDetail;
    // 取到的帧，按页序排列（第一帧 = 第一页）。
    QVector<QByteArray> frames;

    int pageCount() const { return frames.size(); }
};

// WIA 后端：枚举设备 + 取帧。
//
// 为什么这一层单独拎出来：取帧要开 COM、连设备、传数据，
// 这些在**没有扫描仪的机器上也必须能编译、能跑**（只能报「没找到设备」），
// 所以它不碰任何界面控件。
class ScannerCapture {
public:
    ScannerCapture();
    ~ScannerCapture();

    // 枚举本机的图像设备。
    //
    // 一台都没有时返回空列表并把原因写进 error（中文）；
    // WIA 本身起不来时也是空列表，但原因不同（见 ScanErrorKind）。
    QVector<ScannerDevice> listDevices(QString* error = nullptr);

    // 从指定设备取帧。frames 按页序排列。
    //
    // 设备在、但取不到图时返回失败，并写明是哪一类失败。
    ScanOutcome capture(const ScannerDevice& device, const ScannerCaptureOptions& options);

    // 枚举 → 取帧 → 拼成一份多页 PDF。
    //
    // 这是 Ribbon 两个入口（主页「从扫描仪」、转换「从扫描仪」）共同走的路：
    // 页序由 frames 的顺序决定，一帧一页。
    ScanOutcome scanToPdf(const ScannerCaptureOptions& options, QByteArray* pdfOut);

private:
    // 枚举的原始结果：能把「没有设备」和「枚举本身失败」分开。
    struct DeviceQuery {
        QVector<ScannerDevice> devices;
        ScanErrorKind errorKind = ScanErrorKind::None;
        QString detail;
    };
    DeviceQuery queryDevices();
};

// 扫描对话框：选设备、选分辨率、扫描，拿到一份多页 PDF。
//
// 为什么自带对话框而不是让调用方现搭：设备枚举、取帧、拼 PDF 本来就是一条
// 完整的流程，拆给每个入口重写一遍只会写出三个不一样的版本。
class ScannerCaptureDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ScannerCaptureDialog(QWidget* parent = nullptr);

    // 扫描成功后拿到的 PDF 字节（只在 accepted 之后有意义）。
    QByteArray pdfBytes() const { return pdfBytes_; }
    // 扫到的页数。
    int pageCount() const { return pageCount_; }
    // 没用扫描仪就关掉时，原因是什么（None 表示成功或还没扫）。
    ScanErrorKind cancelReason() const { return cancelReason_; }

private slots:
    void refreshDevices();
    void startScan();

protected:
    // 统一记录「用户自己放弃」的地方。
    //
    // 为什么用 done() 而不是 reject()/closeEvent()：reject() 是槽不是虚函数，
    // 覆盖不了；Esc 键、取消按钮、窗口右上角的 × 走的又是三条不同的路。
    // done() 是这三条路的终点（accept/reject/close 最终都会调它），只接一处就够。
    void done(int result) override;

private:
    void buildUi();
    void setBusy(bool busy);
    void showMessage(const QString& text, bool isError);

    QComboBox* deviceBox_ = nullptr;
    QSpinBox* dpiBox_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* scanButton_ = nullptr;
    QPushButton* acceptButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;

    QVector<ScannerDevice> devices_;
    QByteArray pdfBytes_;
    int pageCount_ = 0;
    ScanErrorKind cancelReason_ = ScanErrorKind::None;
    bool scanned_ = false;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
