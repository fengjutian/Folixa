#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 图像字节的格式。
//
// 为什么只认两种：JPEG 能**原样**当 /DCTDecode 塞进 PDF（不重编码、不掉画质），
// BMP 自己解成 RGB 再 /FlateDecode。这两条路都能保证「扫出来的像素一个不差」。
//
// 为什么没有 TIFF：本机 Qt 只部署了 gif/ico/jpeg/svg 图像插件，读 TIFF 会直接失败，
// 所以 TIFF 明确不支持（而不是「看情况失败」）。
enum class ImageFormat {
    Unknown,  // 认不出来
    Jpeg,
    Bmp,
};

// 按字节头猜格式。
//
// 为什么要猜：WIA 取回来的帧就是一串字节，没有扩展名也没有 MIME；
// 只认扩展名的话，扫描仪这条路根本走不通。
ImageFormat detectImageFormat(const QByteArray& bytes);

// 一张图像解出来的内容。
struct DecodedImage {
    // 像素宽高。
    int width = 0;
    int height = 0;
    // DeviceRGB / 每通道 8 位 / 行优先 / 行不补齐（BMP 走这条路）。
    QByteArray rgb;
    // 原样的 JPEG 字节（/DCTDecode 直接用它，一个字节都不改）。
    QByteArray jpeg;
    // JPEG 的分量数：1 = 灰度，3 = RGB，4 = CMYK。
    int jpegComponents = 0;
    // 4 分量 JPEG（Adobe 约定，值是反的）：写进 PDF 时要配 /Decode 反相。
    bool jpegInverted = false;

    bool isJpeg() const { return !jpeg.isEmpty(); }
};

// 解一张图。失败返回 false，并把**技术细节**写进 detail。
//
// detail 是 ASCII 的技术描述（不是给人读的句子）：领域层不放 tr()，
// 中文句子由调用方拼，这里只给「到底哪儿不对」的事实。
bool decodeImage(const QByteArray& bytes, DecodedImage* out, QString* detail = nullptr);

// 转 PDF 失败的原因。
//
// 为什么是枚举而不是一句现成的中文：领域层返回结构化结果，中文原因由调用方拼，
// 这样换语言时提示也跟着换，而技术细节保持原样。
enum class ImageToPdfError {
    None,
    EmptyInput,         // 一张图都没有
    UnsupportedFormat,  // 不是 JPEG/BMP（TIFF/PNG 走这条路）
    DecodeFailed,       // 头坏了 / 数据被截断
    InvalidPageSize,    // 分辨率或像素尺寸算出来的页面不合法
    EncodeFailed,       // 拼 PDF 失败
};

// 图像转 PDF 的参数。
struct ImageToPdfOptions {
    // 扫描分辨率（DPI）。只决定页面的物理尺寸，不影响像素本身。
    //
    // 默认 300 是扫描仪的常用档；给错只会让页面尺寸对不上，不会掉画质。
    double dpi = 300.0;
};

// 转换结果。
struct ImageToPdfResult {
    // 失败原因（成功时是 None）。
    ImageToPdfError error = ImageToPdfError::None;
    // 成功时的 PDF 字节（失败时为空）。
    QByteArray pdf;
    // 写进去的页数。
    int pageCount = 0;
    // 多图转换时第几张图失败（0 基；-1 表示不是逐图失败）。
    int failedIndex = -1;
    // 技术细节（ASCII）。
    QString detail;

    bool ok() const { return error == ImageToPdfError::None; }
};

// 一张图 → 一页 PDF。
ImageToPdfResult imageToPdf(const QByteArray& imageBytes,
                            const ImageToPdfOptions& options = ImageToPdfOptions());

// 多张图 → 一份多页 PDF（页序 = 数组顺序，一图一页）。
//
// 页序不能乱：扫描仪的自动进纸器一次出几页，顺序就是用户叠纸的顺序，
// 弄错了等于把文件做废。
ImageToPdfResult imagesToPdf(const QVector<QByteArray>& images,
                             const ImageToPdfOptions& options = ImageToPdfOptions());

}  // namespace aipdf::pdf
