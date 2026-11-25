#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// TIFF 里的一页（已经解成 RGB）。
//
// 为什么解成 RGB 而不是保留原始编码：PDF 的页面内容流里放的是
// DeviceRGB / DeviceGray 的位图，原始的 TIFF 条带与调色板没有对应物。
struct TiffPage {
    // 像素宽高。
    int width = 0;
    int height = 0;
    // DeviceRGB / 每通道 8 位 / 行优先（每像素 3 字节）。
    QByteArray rgb;
};

// 解一个 TIFF 文件。
//
// 做什么：
//   * 认两种字节序（II = 小端、MM = 大端）；
//   * 走 IFD 链，**一页 IFD 就是一页**（多页 TIFF 一页不少地解出来）；
//   * 认的压缩：未压缩 / PackBits / LZW / Deflate；
//   * 认的取样：灰度（1、2、4、8、16 位）、调色板（1、2、4、8 位）、RGB（8、16 位）；
//   * 白为最小（PhotometricInterpretation = 0）按规范反相。
//
// 不做什么（边界）：
//   * 不支持**分块**（tiled）TIFF 与**平面**（PlanarConfiguration = 2）——
//     这两种布局本程序没有对应的读法，明确失败，不给半张图；
//   * 不支持 JPEG 压缩的 TIFF（把 JPEG 藏在 TIFF 里）与 CCITT 传真压缩；
//   * 不支持 CMYK 与多通道（ExtraSamples 里的 Alpha 会被丢掉）。
//
// 失败时返回 false，detail 里写**技术细节**（ASCII）：领域层不放 tr()，
// 中文句子由调用方拼。
bool decodeTiff(const QByteArray& bytes, QVector<TiffPage>* out, QString* detail = nullptr);

}  // namespace aipdf::pdf
