#include "speech/SpeechEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

namespace aipdf::speech {

namespace {

// 语音子进程脚本。
//
// 只用 ASCII：Windows PowerShell 5.1 按系统 ANSI 代码页读 .ps1（没有 BOM
// 时），脚本里写中文会变成乱码，连错误码都会被搅坏。所以脚本只回稳定的
// 错误码，中文文案在 C++ 这一侧生成。
//
// 协议（每行一条，UTF-8）：
//   stdin ：「词序号 词内容的 base64(UTF-8)」
//   stdout：「W 词序号」（开始朗读该词）/「D」（读完）/「E 错误码」
const char* const kScriptTemplate = R"PS(
param(
    [string]$Mode = 'speak',
    [string]$Lang = ''
)
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Send-Line([string]$text) {
    [Console]::Out.WriteLine($text)
    [Console]::Out.Flush()
}

try {
    Add-Type -AssemblyName System.Speech
    $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
} catch {
    Send-Line 'E no_speech_assembly'
    exit 0
}

$voices = @($synth.GetInstalledVoices() | Where-Object { $_.Enabled })
if ($voices.Count -eq 0) {
    Send-Line 'E no_voice'
    exit 0
}

if ($Lang -ne '') {
    foreach ($voice in $voices) {
        if ($voice.VoiceInfo.Culture.Name -like ($Lang + '-*')) {
            $synth.SelectVoice($voice.VoiceInfo.Name)
            break
        }
    }
}

if ($Mode -eq 'probe') {
    Send-Line ('P ' + $voices.Count)
    exit 0
}

$failures = 0
while ($true) {
    $line = [Console]::In.ReadLine()
    if ($null -eq $line) { break }
    if ($line.Length -gt 0 -and $line[0] -eq [char]0xFEFF) { $line = $line.Substring(1) }
    if ($line.Trim() -eq '') { continue }
    $parts = $line.Split(' ', 2)
    if ($parts.Length -lt 2) { continue }
    $index = 0
    if (-not [int]::TryParse($parts[0], [ref]$index)) { continue }
    try {
        $word = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String($parts[1]))
    } catch {
        continue
    }
    Send-Line ('W ' + $index)
    try {
        $synth.Speak($word)
        $failures = 0
    } catch {
        $failures = $failures + 1
        if ($failures -ge 4) {
            Send-Line 'E speak_failed'
            exit 0
        }
    }
}
Send-Line 'D'
exit 0
)PS";

// 子进程与 C++ 之间的错误码 → 界面用的中文原因。
QString reasonForCode(const QString& code) {
    if (code == QLatin1String("no_voice")) {
        return QStringLiteral("系统没有可用的语音");
    }
    if (code == QLatin1String("no_speech_assembly")) {
        return QStringLiteral("系统缺少语音组件（System.Speech）");
    }
    if (code == QLatin1String("speak_failed")) {
        return QStringLiteral("语音朗读失败");
    }
    if (code.isEmpty()) {
        return QStringLiteral("语音引擎错误");
    }
    return QStringLiteral("语音引擎错误：%1").arg(code);
}

// 脚本写盘。带 UTF-8 BOM：PowerShell 5.1 认 BOM 才按 UTF-8 读脚本。
bool writeScriptFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QByteArray payload;
    payload.append(char(0xEF));
    payload.append(char(0xBB));
    payload.append(char(0xBF));
    payload.append(kScriptTemplate);
    return file.write(payload) == payload.size();
}

}  // namespace

SpeechEngine::SpeechEngine(QObject* parent) : QObject(parent) {
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process_, &QProcess::started, this, &SpeechEngine::handleProcessStarted);
    connect(process_, &QProcess::readyReadStandardOutput, this, &SpeechEngine::handleStandardOutput);
    connect(process_, &QProcess::readyReadStandardError, this, &SpeechEngine::handleStandardError);
    connect(process_, &QProcess::finished, this, &SpeechEngine::handleProcessFinished);
    connect(process_, &QProcess::errorOccurred, this, &SpeechEngine::handleProcessError);
}

SpeechEngine::~SpeechEngine() {
    // 析构时收掉子进程：不能留下还在朗读的孤儿进程。
    speaking_ = false;
    terminateProcess();
}

QString SpeechEngine::findPowerShell() {
    // 只认 Windows PowerShell 5.1：System.Speech 是 .NET Framework 的，
    // PowerShell 7（pwsh，跑在 .NET 上）里没有这个程序集。
    const QString found = QStandardPaths::findExecutable(QStringLiteral("powershell"));
    if (!found.isEmpty()) {
        return found;
    }
    const QString system = QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe");
    return QFileInfo::exists(system) ? system : QString();
}

bool SpeechEngine::isSupported() {
    static const SupportInfo info = probeSupport();
    return info.supported;
}

QString SpeechEngine::unavailableReason() const {
    static const SupportInfo info = probeSupport();
    return info.supported ? QString() : info.reason;
}

SpeechEngine::SupportInfo SpeechEngine::probeSupport() {
    SupportInfo info;

    const QString shell = findPowerShell();
    if (shell.isEmpty()) {
        info.reason = QStringLiteral("找不到 powershell，无法启动系统语音引擎");
        return info;
    }

    QTemporaryDir probeDir;
    if (!probeDir.isValid()) {
        info.reason = QStringLiteral("无法创建临时目录");
        return info;
    }
    const QString scriptPath = probeDir.filePath(QStringLiteral("aipdf-speech-probe.ps1"));
    if (!writeScriptFile(scriptPath)) {
        info.reason = QStringLiteral("语音引擎脚本写入失败");
        return info;
    }

    QProcess probe;
    probe.start(shell,
                {QStringLiteral("-NoProfile"), QStringLiteral("-NoLogo"),
                 QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
                 QStringLiteral("Bypass"), QStringLiteral("-File"),
                 QDir::toNativeSeparators(scriptPath), QStringLiteral("-Mode"),
                 QStringLiteral("probe")});
    if (!probe.waitForStarted(10000)) {
        info.reason = QStringLiteral("无法启动系统语音引擎");
        return info;
    }
    if (!probe.waitForFinished(20000)) {
        probe.kill();
        probe.waitForFinished(2000);
        info.reason = QStringLiteral("系统语音引擎检测超时");
        return info;
    }

    const QString output = QString::fromUtf8(probe.readAllStandardOutput());
    for (const QString& raw : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("P "))) {
            info.supported = true;
            info.reason.clear();
            return info;
        }
        if (line.startsWith(QLatin1String("E "))) {
            info.reason = reasonForCode(line.mid(2).trimmed());
            return info;
        }
    }

    info.reason = QStringLiteral("系统语音引擎不可用");
    return info;
}

bool SpeechEngine::ensureScript() {
    if (!scriptPath_.isEmpty() && QFileInfo::exists(scriptPath_)) {
        return true;
    }
    if (!scriptDir_) {
        scriptDir_ = std::make_unique<QTemporaryDir>();
        if (!scriptDir_->isValid()) {
            return false;
        }
    }
    scriptPath_ = scriptDir_->filePath(QStringLiteral("aipdf-speech.ps1"));
    return writeScriptFile(scriptPath_);
}

void SpeechEngine::speak(const QString& text) {
    const bool wasSpeaking = speaking_;
    // 重复调用会打断上一次：先收尾，再开新的一场。
    stopSession();

    words_ = splitIntoWords(text);
    if (words_.isEmpty()) {
        // 空文本没有词可读，直接结束（不发 wordStarted）。
        if (!wasSpeaking) {
            emit finished();
        }
        return;
    }
    if (!ensureScript()) {
        emit failed(QStringLiteral("语音引擎脚本写入失败"));
        return;
    }

    outputBuffer_.clear();
    errorBuffer_.clear();
    speaking_ = true;

    QStringList args{QStringLiteral("-NoProfile"), QStringLiteral("-NoLogo"),
                     QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
                     QStringLiteral("Bypass"), QStringLiteral("-File"),
                     QDir::toNativeSeparators(scriptPath_), QStringLiteral("-Mode"),
                     QStringLiteral("speak")};
    const QString lang = detectLanguage(text);
    if (!lang.isEmpty()) {
        args << QStringLiteral("-Lang") << lang;
    }

    if (process_->state() != QProcess::NotRunning) {
        terminateProcess();
    }
    process_->start(findPowerShell(), args);
}

void SpeechEngine::stop() {
    stopSession();
}

bool SpeechEngine::isSpeaking() const {
    return speaking_;
}

void SpeechEngine::handleProcessStarted() {
    writeWords();
}

void SpeechEngine::writeWords() {
    if (!process_ || process_->state() == QProcess::NotRunning) {
        return;
    }
    QByteArray payload;
    for (int index = 0; index < words_.size(); ++index) {
        payload += QByteArray::number(index);
        payload += ' ';
        // base64 走 ASCII，stdin 的编码问题就绕开了。
        payload += words_.at(index).text.toUtf8().toBase64();
        payload += '\n';
    }
    process_->write(payload);
    // 关闭写通道：子进程读到 EOF 后回「D」。
    process_->closeWriteChannel();
}

void SpeechEngine::handleStandardOutput() {
    outputBuffer_ += process_->readAllStandardOutput();
    if (outputBuffer_.startsWith("\xEF\xBB\xBF")) {
        outputBuffer_.remove(0, 3);
    }
    int newline = outputBuffer_.indexOf('\n');
    while (newline >= 0) {
        const QByteArray raw = outputBuffer_.left(newline);
        outputBuffer_.remove(0, newline + 1);
        handleLine(QString::fromUtf8(raw).trimmed());
        newline = outputBuffer_.indexOf('\n');
    }
}

void SpeechEngine::handleStandardError() {
    errorBuffer_ += process_->readAllStandardError();
    // stderr 只是诊断用的旁证：真正的失败原因走 stdout 的「E 错误码」。
    if (errorBuffer_.size() > 4096) {
        errorBuffer_.remove(0, errorBuffer_.size() - 4096);
    }
}

void SpeechEngine::handleLine(const QString& line) {
    if (!speaking_ || line.isEmpty()) {
        // 已经停止或已结束：管道里可能还留着被杀进程写下的数据，
        // 这时不能再冒出词事件。
        return;
    }
    if (line.startsWith(QLatin1String("W "))) {
        bool ok = false;
        const int wordIndex = line.mid(2).trimmed().toInt(&ok);
        if (!ok || wordIndex < 0 || wordIndex >= words_.size()) {
            return;
        }
        emit wordStarted(wordIndex, words_.at(wordIndex).text);
        return;
    }
    if (line == QLatin1String("D")) {
        complete();
        return;
    }
    if (line.startsWith(QLatin1String("E "))) {
        failWith(line.mid(2).trimmed());
        return;
    }
    // 其它行是脚本自己的噪声，忽略。
}

void SpeechEngine::complete() {
    if (!speaking_) {
        return;
    }
    speaking_ = false;
    emit finished();
    // 子进程读完 stdin 就自己退出；万一没退，两秒后收掉。
    QTimer::singleShot(2000, this, [this]() {
        if (process_ && process_->state() != QProcess::NotRunning) {
            terminateProcess();
        }
    });
}

void SpeechEngine::failWith(const QString& code) {
    if (!speaking_) {
        return;
    }
    speaking_ = false;
    terminateProcess();
    emit failed(reasonForCode(code));
}

void SpeechEngine::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (!speaking_) {
        // 正常结束，或者已经被 stop()/complete() 收尾过了。
        return;
    }
    speaking_ = false;
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        emit failed(crashMessage());
        return;
    }
    // 脚本正常退出一定会先回「D」；没收到就是提前结束了。
    emit failed(QStringLiteral("语音引擎提前退出"));
}

QString SpeechEngine::crashMessage() const {
    // stderr 只是诊断旁证：只有纯 ASCII 的短文本才敢贴给界面，
    // 乱码（代码页不一致）会盖掉真正的原因。
    const QString detail = QString::fromUtf8(errorBuffer_).trimmed();
    bool printable = !detail.isEmpty() && detail.size() <= 200;
    for (const QChar& unit : detail) {
        if (unit.unicode() > 0x7E && unit != QLatin1Char(' ')) {
            printable = false;
            break;
        }
    }
    if (!printable) {
        return QStringLiteral("语音引擎进程异常退出");
    }
    return QStringLiteral("语音引擎进程异常退出：%1").arg(detail);
}

void SpeechEngine::handleProcessError(QProcess::ProcessError error) {
    if (!speaking_) {
        return;
    }
    if (error == QProcess::FailedToStart) {
        speaking_ = false;
        emit failed(QStringLiteral("无法启动系统语音引擎（找不到 powershell）"));
        return;
    }
    if (error == QProcess::Crashed) {
        speaking_ = false;
        emit failed(QStringLiteral("语音引擎进程异常退出"));
    }
}

void SpeechEngine::stopSession() {
    if (!speaking_) {
        terminateProcess();
        return;
    }
    speaking_ = false;
    terminateProcess();
    // 停止也是一个明确的结束语义：界面据此把高亮清掉。
    emit finished();
}

void SpeechEngine::terminateProcess() {
    if (!process_ || process_->state() == QProcess::NotRunning) {
        return;
    }
    process_->closeWriteChannel();
    process_->kill();
    // 必须等到进程真的没了才能开下一场：QProcess::start 遇到还在跑的进程
    // 会直接忽略。
    process_->waitForFinished(2000);
}

QString SpeechEngine::detectLanguage(const QString& text) const {
    int cjkCount = 0;
    int latinCount = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar unit = text.at(index);
        if (unit.isHighSurrogate() && index + 1 < text.size() && text.at(index + 1).isLowSurrogate()) {
            ++index;
            continue;
        }
        const char32_t code = unit.unicode();
        if (code >= 0x4E00 && code <= 0x9FFF) {
            ++cjkCount;
        } else if ((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z')) {
            ++latinCount;
        }
    }
    if (cjkCount == 0 && latinCount == 0) {
        return QString();
    }
    return cjkCount >= latinCount ? QStringLiteral("zh") : QStringLiteral("en");
}

}  // namespace aipdf::speech
