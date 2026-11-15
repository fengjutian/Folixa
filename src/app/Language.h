#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QVector>

class QIODevice;

namespace aipdf::i18n {

// 一个可选的界面语言。
struct Language {
    QString code;        // QLocale 风格代码，如 "zh_CN"、"en"
    QString nativeName;  // 该语言自己的写法，菜单里直接显示，不翻译
};

// 目录里的一条词条。<numerusform> 支持复数形式（英文两个形式）。
struct CatalogMessage {
    QString text;
    QStringList numerusForms;
};

// 应用支持的语言（按菜单顺序）。中文是源语言：界面上写的就是中文，
// 因此中文不需要翻译目录；英文等其他语言各有一份 .ts。
QVector<Language> supportedLanguages();

// 语言代码是否在支持列表里。
bool isSupportedLanguage(const QString& code);

// 语言代码的自称（"中文"、"English"）。
QString nativeNameOf(const QString& code);

// 把系统给出的语言列表折成一个支持的语言代码：
// 按顺序取第一个认识的语言；不认识的语言回退到英文。
// 单独拿出来是为了能被单元测试直接喂各种 locale 组合。
QString resolveLanguage(const QStringList& uiLanguages);

// ---------------------------------------------------------------------------
// 标准 .ts 的运行时加载器
//
// 固定工具链里没有 Qt LinguistTools（lrelease），所以这里直接解析
// Qt Linguist 的标准 .ts XML：目录仍然是人工可编辑、Qt Linguist 能打开的标准
// 格式，构建链不变（不需要 lrelease 这一步），打包时 .ts 进 Qt 资源。
// ---------------------------------------------------------------------------
class TsTranslator final : public QTranslator {
public:
    explicit TsTranslator(QObject* parent = nullptr);

    // 读取一份 .ts。失败时返回 false，并通过 errorMessage 说明原因。
    // type="unfinished"/"obsolete" 的词条按「没有翻译」处理：
    // 界面回退到源文案，同时让覆盖率脚本报出来。
    bool loadTs(QIODevice* device, QString* errorMessage = nullptr);

    bool isEmpty() const override;

    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr, int n = -1) const override;

    // 载入的词条数（不含被跳过的空翻译）。
    int messageCount() const { return messages_.size(); }

    // 目录自身声明的语言（<TS language="...">），用于校验文件名与内容一致。
    QString catalogLanguage() const { return catalogLanguage_; }

private:
    QHash<QString, CatalogMessage> messages_;
    QString catalogLanguage_;
};

// ---------------------------------------------------------------------------
// 语言管理器：解析当前语言、加载/卸载翻译、记住用户选择
// ---------------------------------------------------------------------------
class LanguageManager final : public QObject {
    Q_OBJECT

public:
    // 设置里存的值：语言代码，或 "system"（跟随系统，默认）。
    static QString systemPreference();

    static LanguageManager& instance();

    // 当前选择："system" 或某个语言代码。
    QString preference() const { return preference_; }

    // 实际生效的语言代码（"system" 已解析成具体语言）。
    QString code() const { return code_; }

    // 启动时调用：按设置里记住的选择装上翻译。
    void loadSavedPreference();

    // 切换语言。value 取 "system" 或 supportedLanguages() 里的代码。
    // 返回 false 表示值不认识，界面保持原样。
    bool setPreference(const QString& value);

    // 目录在资源里的路径（"zh_CN" 这类源语言没有目录，返回空）。
    static QString catalogPath(const QString& code);

signals:
    // 语言已切换并已装好翻译；各窗口据此就地重译。
    void languageChanged(const QString& code);

private:
    LanguageManager();

    bool apply(const QString& code);
    void persist() const;

    QString preference_ = QStringLiteral("system");
    QString code_;
    TsTranslator* translator_ = nullptr;
};

}  // namespace aipdf::i18n
