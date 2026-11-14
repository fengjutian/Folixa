#pragma once

#include "ocr/OcrEngine.h"

#include <QString>

#include <atomic>

namespace aipdf::ocr {

// 用 Windows 自带的 OCR（Windows.Media.Ocr）识别页面。
//
// 为什么不装 Tesseract：这台机器上用户没装、安装包要管理员权限；
// 而 Windows 10+ 自带 OCR 引擎，中文语言包通常已随系统安装。
// 实现方式是在临时目录里生成一个 PowerShell 脚本（WinRT 调用只能这么进），
// 跑完即删——不需要随包分发任何原生库。
//
// 没有可用引擎时返回明确错误，上层退回「无 OCR」。
class WindowsOcrEngine final : public OcrEngine {
public:
    WindowsOcrEngine();

    QString id() const override;
    bool isAvailable() const override;
    OcrPageResult recognize(const OcrJob& job) override;
    void cancel() override;

    // 解析脚本输出：每行一个 JSON 对象 {line,text,x,y,w,h}。
    // 单独公开便于在没有 OCR 环境的机器上单测。
    static OcrPageResult parseWordsJson(const QString& jsonLines,
                                        int pageIndex,
                                        const QSize& imageSize,
                                        const QSizeF& pageSize);

    // 生成脚本内容（也用于测试）。
    static QString scriptText();

private:
    std::atomic<bool> cancelled_{false};
    bool powershellAvailable_ = false;
};

}  // namespace aipdf::ocr
