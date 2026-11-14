#include "ocr/WindowsOcrEngine.h"

#include "ocr/ImagePreprocess.h"
#include "ocr/OcrGeometry.h"
#include "ocr/OcrTextUtils.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>

namespace aipdf::ocr {

namespace {

// WinRT 的 OCR 只能在 PowerShell 里调（MinGW 用不了 C++/WinRT 头），
// 所以把脚本写成常量，运行时落到临时目录执行，跑完即删。
const char* const kScriptTemplate = R"PS(
param([Parameter(Mandatory=$true)][string]$ImagePath)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType=WindowsRuntime]
$null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType=WindowsRuntime]
$null = [Windows.Graphics.Imaging.SoftwareBitmap, Windows.Foundation, ContentType=WindowsRuntime]
$null = [Windows.Storage.StorageFile, Windows.Foundation, ContentType=WindowsRuntime]
$null = [Windows.Storage.Streams.IRandomAccessStream, Windows.Foundation, ContentType=WindowsRuntime]

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]

function Await($WinRtTask, $ResultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    return $netTask.Result
}

try {
    $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($ImagePath)) ([Windows.Storage.StorageFile])
    $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
    if ($null -eq $engine) { Write-Error '没有可用的系统 OCR 语言'; exit 2 }
    $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])

    $lineIndex = 0
    foreach ($line in $result.Lines) {
        foreach ($word in $line.Words) {
            $rect = $word.BoundingRect
            $obj = [ordered]@{
                line = $lineIndex
                text = $word.Text
                x = [double]$rect.X
                y = [double]$rect.Y
                w = [double]$rect.Width
                h = [double]$rect.Height
            }
            ($obj | ConvertTo-Json -Compress)
        }
        $lineIndex++
    }
} catch {
    $inner = $_.Exception
    while ($inner.InnerException) { $inner = $inner.InnerException }
    $payload = [ordered]@{ error = $inner.Message }
    ($payload | ConvertTo-Json -Compress)
    exit 3
}
)PS";

}  // namespace

WindowsOcrEngine::WindowsOcrEngine() {
    powershellAvailable_ =
        !QStandardPaths::findExecutable(QStringLiteral("powershell")).isEmpty();
}

QString WindowsOcrEngine::id() const {
    return QStringLiteral("windows-ocr");
}

bool WindowsOcrEngine::isAvailable() const {
    // 能起 PowerShell 就认为可用：具体语言包在真正识别时才暴露。
    return powershellAvailable_;
}

void WindowsOcrEngine::cancel() {
    cancelled_.store(true);
}

QString WindowsOcrEngine::scriptText() {
    return QString::fromUtf8(kScriptTemplate);
}

OcrPageResult WindowsOcrEngine::recognize(const OcrJob& job) {
    OcrPageResult result;
    result.pageIndex = job.pageIndex;
    result.engineId = id();

    if (!powershellAvailable_) {
        result.error = QStringLiteral("找不到 powershell，无法调用系统 OCR");
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

    QTemporaryDir workDir;
    if (!workDir.isValid()) {
        result.error = QStringLiteral("无法创建临时目录");
        return result;
    }
    const QString scriptPath = workDir.filePath(QStringLiteral("ocr.ps1"));
    {
        QFile script(scriptPath);
        if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.error = QStringLiteral("OCR 脚本写入失败");
            return result;
        }
        script.write(scriptText().toUtf8());
    }

    const QString imagePath = workDir.filePath(QStringLiteral("page.png"));
    const QImage input = preprocessForOcr(job.image);
    if (!input.save(imagePath, "PNG")) {
        result.error = QStringLiteral("页面图像写盘失败");
        return result;
    }

    QProcess process;
    // WinRT 的 GetFileFromPathAsync 只认本地分隔符：QTemporaryDir::filePath
    // 给的是正斜杠，直接传会报「指定的路径无效」。
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                   QStringLiteral("Bypass"), QStringLiteral("-File"),
                   QDir::toNativeSeparators(scriptPath), QStringLiteral("-ImagePath"),
                   QDir::toNativeSeparators(imagePath)});
    if (!process.waitForStarted(10'000)) {
        result.error = QStringLiteral("powershell 启动失败");
        return result;
    }
    if (!process.waitForFinished(120'000)) {
        process.kill();
        process.waitForFinished(2000);
        result.error = QStringLiteral("识别超时");
        return result;
    }
    if (cancelled_.load()) {
        result.cancelled = true;
        return result;
    }
    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        // 脚本失败时会把原因写成一行 JSON：优先用它，
        // stderr 在中文控制台上会被代码页搅成乱码。
        for (const QString& row : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QJsonDocument document = QJsonDocument::fromJson(row.trimmed().toUtf8());
            if (document.isObject()) {
                const QString message = document.object().value(QStringLiteral("error")).toString();
                if (!message.isEmpty()) {
                    result.error = message;
                    return result;
                }
            }
        }
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        result.error = stderrText.isEmpty()
                           ? QStringLiteral("系统 OCR 退出码 %1").arg(process.exitCode())
                           : stderrText;
        return result;
    }

    return parseWordsJson(output, job.pageIndex, input.size(), job.pageSize);
}

OcrPageResult WindowsOcrEngine::parseWordsJson(const QString& jsonLines,
                                               int pageIndex,
                                               const QSize& imageSize,
                                               const QSizeF& pageSize) {
    OcrPageResult result;
    result.pageIndex = pageIndex;
    result.engineId = QStringLiteral("windows-ocr");

    struct LineAccumulator {
        QRectF pixelRect;
        QStringList words;
        bool hasRect = false;
    };
    QVector<QPair<int, LineAccumulator>> lines;
    QHash<int, int> indexByLine;

    const QStringList rows = jsonLines.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& row : rows) {
        const QJsonDocument document = QJsonDocument::fromJson(row.trimmed().toUtf8());
        if (!document.isObject()) {
            continue;
        }
        const QJsonObject object = document.object();
        const QString text = object.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty()) {
            continue;
        }
        const int lineNumber = object.value(QStringLiteral("line")).toInt();
        const QRectF wordRect(object.value(QStringLiteral("x")).toDouble(),
                              object.value(QStringLiteral("y")).toDouble(),
                              object.value(QStringLiteral("w")).toDouble(),
                              object.value(QStringLiteral("h")).toDouble());

        int index = indexByLine.value(lineNumber, -1);
        if (index < 0) {
            index = static_cast<int>(lines.size());
            lines.append({lineNumber, LineAccumulator{}});
            indexByLine.insert(lineNumber, index);
        }
        LineAccumulator& accumulator = lines[index].second;
        accumulator.words.append(text);
        accumulator.pixelRect =
            accumulator.hasRect ? accumulator.pixelRect.united(wordRect) : wordRect;
        accumulator.hasRect = true;
    }

    for (const auto& entry : lines) {
        const LineAccumulator& accumulator = entry.second;
        if (accumulator.words.isEmpty()) {
            continue;
        }
        OcrLine line;
        line.text = joinWords(accumulator.words);
        line.rect = imageRectToPage(accumulator.pixelRect, imageSize, pageSize);
        result.lines.append(line);
    }

    // Windows OCR 不提供置信度：给 1.0 会误导（像是「很确定」），
    // 给 0 又会被上层当成失败。这里留 0.0 并让调用方知道它不可用。
    result.meanConfidence = 0.0;
    return result;
}

}  // namespace aipdf::ocr
