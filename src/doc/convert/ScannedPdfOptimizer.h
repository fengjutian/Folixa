#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ocr/pipeline/ImagePreprocess.h"

namespace aipdf::doc {

// 「优化扫描的 PDF」的参数。
//
// 为什么单独一个结构体而不是直接传两个 bool：去噪与纠偏各自的阈值、
// 角度搜索范围都有自己的依据（见 ocr::ConnectedDenoiseOptions 与
// ocr::DeskewOptions 的注释），界面上以后要让用户调时也有地方放。
struct ScannedOptimizeOptions {
    // 渲染分辨率（DPI）。300 是扫描常用档。
    double dpi = 300.0;

    // 要不要去噪（连通域）+ 纠偏（投影方差）。
    bool denoise = true;
    bool deskew = true;

    ocr::ConnectedDenoiseOptions denoiseOptions;
    ocr::DeskewOptions deskewOptions;
};

// 一页的处理结果。
struct ScannedPageOptimize {
    int pageIndex = 0;        // 0 起
    double deskewAngleDeg = 0.0;  // 检出的倾斜角（纠偏时有效）
    bool denoised = false;    // 这次是否真的跑了去噪
    QString note;            // 这一页的说明（中文）；没有特别情况时为空
};

// 一次优化的汇报。
//
// 为什么要有它：纠偏是有损的（页面会重新排布、边角会裁掉一点），
// 去噪也可能有副作用。用户必须看到「哪几页纠了多少度」，而不是只有一句「完成」。
struct ScannedOptimizeReport {
    bool ok = false;
    QString error;                    // 失败原因（中文）；成功时为空
    int pageCount = 0;                // 处理了几页
    QVector<ScannedPageOptimize> pages;
    QStringList notes;                // 总体说明（中文）
};

// 把一份扫描出来的 PDF 做去噪与纠偏，产出一份新的 PDF。
//
// 这条链路的形态：**逐页渲染成位图 → 去噪 + 纠偏 → 再写回一份每页一张图的 PDF**。
//
// 为什么要这样（而不是直接在 PDF 上改）：去噪与纠偏都是**像素级**的操作，
// PDF 的页面内容是矢量与文字指令，没有「噪点」和「倾斜」可改。
//
// 代价必须说清楚：
//   - 产物是**图像化 PDF**：文字层没了（原来就是扫描件时通常无所谓），
//     文件体积随 dpi 走；
//   - 纠偏会旋转页面，页面尺寸可能与原文件有微小差别；
//   - 每一页的原文字层**不会**保留。
//
// 失败时返回空字节，并把中文原因写进 error。
QByteArray optimizeScannedPdf(const QByteArray& pdfBytes, const ScannedOptimizeOptions& options = {},
                               ScannedOptimizeReport* report = nullptr, QString* error = nullptr);

}  // namespace aipdf::doc
