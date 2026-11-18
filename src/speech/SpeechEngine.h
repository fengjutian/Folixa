#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>

#include "speech/SpeechSettings.h"
#include "speech/SpeechText.h"

#include <memory>

class QTemporaryDir;

namespace aipdf::speech {

// 朗读引擎：把文本逐词送去系统语音合成，并在每个词开始朗读时回报下标，
// 界面据此做逐词高亮。
//
// 为什么绕 PowerShell：MinGW-w64 没有 sapi.h，Qt 6 也没有 QTextToSpeech
// （Qt5 才有）。直接调 SAPI 的 ISpVoice 只能手写 COM vtable，顺序错了会崩。
// 所以改用一个常驻的 Windows PowerShell 5.1 子进程，用 .NET 的
// System.Speech.Synthesis.SpeechSynthesizer 合成语音：
//
//   C++ -> 子进程（stdin，每行一条）：「词序号 词内容的 base64(UTF-8)」
//   子进程 -> C++（stdout，每行一条）：「W 词序号」/「D」/「E 错误码」
//
// 子进程在开始朗读某个词之前先回「W 序号」，C++ 收到就发 wordStarted，
// 界面把该词高亮。全部读完回「D」，出错回「E 错误码」（中文文案在 C++
// 这一侧按错误码生成，脚本里不写死中文）。
//
// 引擎是异步的：speak() 立即返回，朗读在后台进行。
// 系统没装语音时优雅降级：isSupported() 返回 false，unavailableReason()
// 给出中文原因，speak() 发 failed()，不崩溃也不卡死。
class SpeechEngine final : public QObject {
    Q_OBJECT
public:
    explicit SpeechEngine(QObject* parent = nullptr);
    ~SpeechEngine() override;

    // 系统是否具备朗读能力（PowerShell 存在 + System.Speech 能加载 + 有可用语音）。
    // 探测时不真的朗读。结果在进程内缓存，只探一次。
    static bool isSupported();
    // 不支持时给界面看的中文原因（"系统没有可用的语音"等）；支持时为空。
    QString unavailableReason() const;

    // 朗读一段文本。引擎内部切词、逐词送子进程，并在开始朗读某个词时
    // 发出 wordStarted。重复调用会打断上一次。
    void speak(const QString& text);
    void stop();
    bool isSpeaking() const;

    // 朗读设置：音色、语速、音量。
    //
    // 设置在下次朗读时生效（子进程是每场朗读起一个的）。
    void setSettings(const SpeechSettings& settings);
    SpeechSettings settings() const;

    // 系统里装了哪些音色（只列可用的）。
    //
    // 探测要起一次子进程，比 isSupported() 慢，所以结果在进程内缓存。
    // 系统没装语音时返回空列表，不报错。
    static QVector<SpeechVoice> availableVoices();

signals:
    // 第 index 个词开始朗读（index 与 splitIntoWords 的下标一致）。
    void wordStarted(int index, const QString& word);
    void finished();
    void failed(const QString& message);

private:
    struct SupportInfo {
        bool supported = false;
        QString reason;
    };

    static SupportInfo probeSupport();
    static QVector<SpeechVoice> probeVoices();
    static QString findPowerShell();

    bool ensureScript();
    void writeWords();
    void handleStandardOutput();
    void handleStandardError();
    void handleLine(const QString& line);
    void handleProcessStarted();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    void failWith(const QString& code);
    void complete();
    QString crashMessage() const;
    void stopSession();
    void terminateProcess();
    QString detectLanguage(const QString& text) const;

    QProcess* process_ = nullptr;
    std::unique_ptr<QTemporaryDir> scriptDir_;
    QString scriptPath_;
    QVector<SpeechWord> words_;
    QByteArray outputBuffer_;
    QByteArray errorBuffer_;
    bool speaking_ = false;
    // 朗读设置（音色/语速/音量）。默认按文本语言自动挑音色。
    SpeechSettings settings_;
};

}  // namespace aipdf::speech
