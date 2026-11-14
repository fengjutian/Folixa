#include "ocr/TesseractCliEngine.h"

#include "ocr/ImagePreprocess.h"
#include "ocr/OcrGeometry.h"

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

bool isCjk(QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x4E00 && code <= 0x9FFF) || (code >= 0x3400 && code <= 0x4DBF) ||
           (code >= 0xF900 && code <= 0xFAFF) || (code >= 0x3040 && code <= 0x30FF) ||
           (code >= 0xAC00 && code <= 0xD7AF);
}

// 中文按字切开时不能加空格，否则「中华人民共和国」会变成「中 华 人 民 …」，
// 检索与引用都会对不上原文。夹在中文之间的英文单词保持原样。
QString joinWords(const QStringList& words) {
    QString joined;
    for (const QString& word : words) {
        if (word.isEmpty()) {
            continue;
        }
        if (!joined.isEmpty()) {
            const QChar previous = joined.at(joined.size() - 1);
            const QChar next = word.at(0);
            const bool needsSpace = !isCjk(previous) && !isCjk(next) &&
                                    previous != QLatin1Char('-') && next != QLatin1Char(',') &&
                                    next != QLatin1Char('.');
            if (needsSpace) {
                joined.append(QLatin1Char(' '));
            }
        }
        joined.append(word);
    }
    return joined;
}

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
    return QStandardPaths::findExecutable(QStringLiteral("tesseract"));
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
    arguments << imagePath << QStringLiteral("stdout")
              << QStringLiteral("-l") << languages_.join(QLatin1Char('+'))
              << QStringLiteral("--psm") << QStringLiteral("6")
              << QStringLiteral("tsv");
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
