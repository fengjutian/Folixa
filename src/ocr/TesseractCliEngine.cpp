#include "ocr/TesseractCliEngine.h"

#include "ocr/ImagePreprocess.h"
#include "ocr/OcrGeometry.h"
#include "ocr/OcrTextUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>

namespace aipdf::ocr {

namespace {



}  // namespace

TesseractCliEngine::TesseractCliEngine(const QString& executable, const QStringList& languages)
    : executable_(executable),
      languages_(languages.isEmpty() ? QStringList{QStringLiteral("chi_sim"), QStringLiteral("eng")}
                                     : languages) {
    if (executable_.isEmpty()) {
        executable_ = findExecutable();
    }
}

QString TesseractCliEngine::findExecutable() {
    const QString fromEnvironment = qEnvironmentVariable("AIPDF_TESSERACT_PATH");
    if (!fromEnvironment.isEmpty() && QFileInfo::exists(fromEnvironment)) {
        return fromEnvironment;
    }
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (!onPath.isEmpty()) {
        return onPath;
    }
    // 仓库自带的便携版（开发机常见）：应用在 build/<preset>/bin 下，
    // 仓库根在往上三层，引擎放在 tools/tess 里。
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates{
        appDir.absoluteFilePath(QStringLiteral("../../../tools/tess/Library/bin/tesseract.exe")),
        appDir.absoluteFilePath(QStringLiteral("../../tools/tess/Library/bin/tesseract.exe")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

QString TesseractCliEngine::findTessdataDirectory() const {
    const QString fromEnvironment = qEnvironmentVariable("AIPDF_TESSDATA_PREFIX");
    if (!fromEnvironment.isEmpty() && QFileInfo::exists(fromEnvironment)) {
        return fromEnvironment;
    }
    if (executable_.isEmpty()) {
        return {};
    }
    const QDir exeDir(QFileInfo(executable_).absolutePath());
    // 解压版：<exe 目录>/tessdata；conda 版：<exe 目录>/../../share/tessdata。
    const QStringList candidates{
        exeDir.filePath(QStringLiteral("tessdata")),
        exeDir.absoluteFilePath(QStringLiteral("../../share/tessdata")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}
QString TesseractCliEngine::id() const {
    return QStringLiteral("tesseract-cli");
}

bool TesseractCliEngine::isAvailable() const {
    return !executable_.isEmpty() && QFileInfo::exists(executable_);
}

void TesseractCliEngine::setLanguages(const QStringList& languages) {
    if (!languages.isEmpty()) {
        languages_ = languages;
    }
}

void TesseractCliEngine::setTimeoutMs(int timeoutMs) {
    timeoutMs_ = std::max(1000, timeoutMs);
}

void TesseractCliEngine::setUsePreprocessing(bool enabled) {
    usePreprocessing_ = enabled;
}

void TesseractCliEngine::cancel() {
    cancelled_.store(true);
}

OcrPageResult TesseractCliEngine::recognize(const OcrJob& job) {
    OcrPageResult result;
    result.pageIndex = job.pageIndex;
    result.engineId = id();

    if (!isAvailable()) {
        result.error = QStringLiteral("没有找到 tesseract 可执行文件");
        return result;
    }
    if (job.image.isNull()) {
        result.error = QStringLiteral("页面图像为空");
        return result;
    }
    if (cancelled_.load()) {
        result.cancelled = true;
        return result;
    }

    // 临时目录：识别完就删，不在用户磁盘上留页面截图。
    QTemporaryDir workDir;
    if (!workDir.isValid()) {
        result.error = QStringLiteral("无法创建临时目录");
        return result;
    }
    const QString imagePath = workDir.filePath(QStringLiteral("page.png"));
    const QImage input = usePreprocessing_ ? preprocessForOcr(job.image) : job.image;
    if (!input.save(imagePath, "PNG")) {
        result.error = QStringLiteral("页面图像写盘失败");
        return result;
    }

    QProcess process;
    QStringList arguments;
    arguments << imagePath << QStringLiteral("stdout");
    // 便携包（conda / 解压版）不把 tessdata 放在系统默认位置，
    // 不指定的话会报「找不到语言包」，而错误信息里根本看不出是路径问题。
    const QString tessdata = findTessdataDirectory();
    if (!tessdata.isEmpty()) {
        arguments << QStringLiteral("--tessdata-dir") << QDir::toNativeSeparators(tessdata);
    }
    arguments << QStringLiteral("-l") << languages_.join(QLatin1Char('+'))
              << QStringLiteral("--psm") << QStringLiteral("6")
              // 用 -c 开 TSV，而不是把 tsv 当配置文件传：便携包不保证
              // configs/tsv 能找到，找不到时 tesseract 只打一行警告、
              // 继续输出纯文本——解析器就看到「没有识别出文字」。
              << QStringLiteral("-c") << QStringLiteral("tessedit_create_tsv=1");
    process.start(executable_, arguments);
    if (!process.waitForStarted(10'000)) {
        result.error = QStringLiteral("tesseract 启动失败");
        return result;
    }
    if (!process.waitForFinished(timeoutMs_)) {
        process.kill();
        process.waitForFinished(2000);
        result.error = QStringLiteral("识别超时（%1 ms）").arg(timeoutMs_);
        return result;
    }
    if (cancelled_.load()) {
        result.cancelled = true;
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        result.error = stderrText.isEmpty() ? QStringLiteral("tesseract 退出码 %1").arg(process.exitCode())
                                            : stderrText;
        return result;
    }

    const QString tsv = QString::fromUtf8(process.readAllStandardOutput());
    return parseTsv(tsv, job.pageIndex, input.size(), job.pageSize);
}

OcrPageResult TesseractCliEngine::parseTsv(const QString& tsv,
                                           int pageIndex,
                                           const QSize& imageSize,
                                           const QSizeF& pageSize) {
    OcrPageResult result;
    result.pageIndex = pageIndex;
    result.engineId = QStringLiteral("tesseract-cli");

    struct LineAccumulator {
        QRectF pixelRect;
        QStringList words;
        double confidenceSum = 0.0;
        int confidenceCount = 0;
        bool hasRect = false;
    };
    QVector<QPair<QString, LineAccumulator>> lines;  // key = block/par/line 组合
    QHash<QString, int> lineIndexByKey;

    const QStringList rows = tsv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& row : rows) {
        const QStringList columns = row.split(QLatin1Char('\t'));
        if (columns.size() < 12) {
            continue;
        }
        bool ok = false;
        const int level = columns.at(0).toInt(&ok);
        if (!ok || level != 5) {
            // 只要词级结果：行级行里没有文字（text 为空），行框靠词框并集算。
            continue;
        }
        const QString text = columns.at(11).trimmed();
        if (text.isEmpty()) {
            continue;
        }
        const double confidence = columns.at(10).toDouble(&ok);
        const int left = columns.at(6).toInt();
        const int top = columns.at(7).toInt();
        const int width = columns.at(8).toInt();
        const int height = columns.at(9).toInt();

        const QString key = columns.at(1) + QLatin1Char('/') + columns.at(2) + QLatin1Char('/') +
                            columns.at(3) + QLatin1Char('/') + columns.at(4);
        int lineIndex = lineIndexByKey.value(key, -1);
        if (lineIndex < 0) {
            LineAccumulator accumulator;
            lineIndex = static_cast<int>(lines.size());
            lines.append({key, accumulator});
            lineIndexByKey.insert(key, lineIndex);
        }
        LineAccumulator& line = lines[lineIndex].second;
        line.words.append(text);
        const QRectF wordRect(left, top, width, height);
        line.pixelRect = line.hasRect ? line.pixelRect.united(wordRect) : wordRect;
        line.hasRect = true;
        if (ok && confidence >= 0.0) {
            line.confidenceSum += confidence;
            line.confidenceCount++;
        }
    }

    double totalConfidence = 0.0;
    int totalWords = 0;
    for (const auto& entry : lines) {
        const LineAccumulator& accumulator = entry.second;
        if (accumulator.words.isEmpty()) {
            continue;
        }
        OcrLine line;
        line.text = joinWords(accumulator.words);
        line.rect = imageRectToPage(accumulator.pixelRect, imageSize, pageSize);
        result.lines.append(line);
        totalConfidence += accumulator.confidenceSum;
        totalWords += accumulator.confidenceCount;
    }
    if (totalWords > 0) {
        result.meanConfidence = totalConfidence / totalWords / 100.0;
    }
    return result;
}

}  // namespace aipdf::ocr
