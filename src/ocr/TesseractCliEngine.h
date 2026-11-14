#pragma once

#include "ocr/OcrEngine.h"

#include <QString>
#include <QStringList>

#include <atomic>

namespace aipdf::ocr {

// 用外部 tesseract 命令行的 OCR 引擎。
//
// 为什么不直接链库：识别引擎是几十 MB 的原生依赖，链接进来就得跟着做
// 部署、许可证与 SBOM 那一整套。先用命令行接（`tesseract <img> stdout tsv`），
// 功能先跑通；等要随包分发时再换成库或隔离 Worker，接口不变。
//
// 找不到可执行文件时 isAvailable() 为 false，上层退回「无 OCR」，
// 不会因为环境缺工具就崩。
class TesseractCliEngine final : public OcrEngine {
public:
    // executable 为空时自动查找：AIPDF_TESSERACT_PATH → PATH 上的 tesseract。
    explicit TesseractCliEngine(const QString& executable = QString(),
                                const QStringList& languages = QStringList());

    QString id() const override;
    bool isAvailable() const override;
    OcrPageResult recognize(const OcrJob& job) override;
    void cancel() override;

    QString executable() const { return executable_; }
    QStringList languages() const { return languages_; }
    void setLanguages(const QStringList& languages);
    void setTimeoutMs(int timeoutMs);
    void setUsePreprocessing(bool enabled);

    // 按 tesseract 的 TSV 输出解析成页结果。
    // 单独公开是为了能在没有安装 tesseract 的机器上直接单测解析逻辑。
    static OcrPageResult parseTsv(const QString& tsv,
                                  int pageIndex,
                                  const QSize& imageSize,
                                  const QSizeF& pageSize);

    // 自动查找可执行文件；找不到返回空串。
    static QString findExecutable();

private:
    QString executable_;
    QStringList languages_;
    int timeoutMs_ = 60'000;
    bool usePreprocessing_ = true;
    std::atomic<bool> cancelled_{false};
};

}  // namespace aipdf::ocr
