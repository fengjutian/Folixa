#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QVector>

class QIODevice;
class QWidget;

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

// 现在**能选**的语言：内置的那些 + 用户装了的那些。
//
// 为什么要有这个：装了语言包就得能选它，否则「安装」这一半就没有意义。
// 装来的语言包没有自带名字（.ts 里只有语言代码），菜单里就显示代码。
QVector<Language> availableLanguages();

// 语言代码是否在支持列表里。
bool isSupportedLanguage(const QString& code);

// 语言代码的自称（"中文"、"English"）。
QString nativeNameOf(const QString& code);

// 把系统给出的语言列表折成一个支持的语言代码：
// 按顺序取第一个认识的语言；不认识的语言回退到英文。
// 单独拿出来是为了能被单元测试直接喂各种 locale 组合。
QString resolveLanguage(const QStringList& uiLanguages);

// ---------------------------------------------------------------------------
// 语言包的用户侧安装
//
// 为什么要有用户目录：语言包原先只在**构建期**打进 Qt 资源，
// 用户侧装不了别的语言，缺包时也只能看着一句「没有找到语言包」，
// 不知道该干什么。这里给用户一个可写目录：把 .ts 放进去
// （或在界面上点「安装语言包…」选文件），程序就优先用它。
// ---------------------------------------------------------------------------

// 语言包目录（用户级、可写）。目录不一定存在：装了才会有。
QString languagePackDirectory();

// 覆盖语言包目录（测试用；传空串恢复默认目录）。
//
// 为什么需要它：测试不能往用户真实的 AppData 目录里写东西，
// 否则一台机器上跑一次测试，用户的语言目录就多出文件了。
void setLanguagePackDirectoryOverride(const QString& directory);

// 语言包文件名（"ai-pdf-editor_en.ts"）。
QString languagePackFileName(const QString& code);

// 用户目录里某个语言的语言包文件路径（没装就返回空串）。
QString installedLanguagePackPath(const QString& code);

// 用户目录里已经装了包的语言代码（按目录里声明的语言来，读不到就跳过）。
QStringList installedLanguageCodes();

// 读一份语言包里**自己声明**的语言（<TS language="…">）；读不到返回空串。
QString declaredLanguageOf(const QString& sourcePath);

// 安装一份语言包：校验 → 复制到用户目录 → 返回装好的路径。
//
// 校验四件事：
//   1. 能解析成目录且词条不为空（否则装进去也是坏的）；
//   2. 目录里有本应用的词条（aipdf::app:: 上下文）—— 装别人的 .ts 白装；
//   3. 目录自己声明的语言与 code 一致 —— 装错语言包比不装更糟：
//      界面会变成另一种语言，用户还以为是自己刚装的；
//   4. code 得像个语言代码（ja / zh_CN 这类），不然菜单里会多出一个看不懂的条目。
//
// 装进来的语言**不要求**在 supportedLanguages() 里：用户侧装的包
// 本来就可能是本版本没内置的语言（社区包），装了就能切。
bool installLanguagePack(const QString& sourcePath, const QString& code,
                         QString* error = nullptr);

// 走一遍「安装语言包」的完整流程：选文件 → 校验 → 复制 → 切语言 → 提示结果。
//
// 装成功时界面语言已经切过去了（用户刚装完就是要用）。
// 返回是否装成功。
bool installLanguagePackInteractively(QWidget* parent);

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

    // 目录里有没有**本应用**的词条（上下文以 "aipdf::app::" 开头）。
    //
    // 为什么要有这个判断：用户从网上随便下个 .ts（Qt 自带的、别的软件的）
    // 装进来，界面一个字都不会变 —— 看上去「装成功了」，其实白装。
    bool coversAppContexts() const;

private:
    QHash<QString, CatalogMessage> messages_;
    // 目录里出现过的上下文名。
    //
    // 为什么单独记一份：词条键是「上下文 + 分隔符 + 源文案 + 分隔符 + 消歧义」，
    // 从键里反推上下文要靠分隔符的位置，绕；直接记上下文更直白。
    QSet<QString> contexts_;
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

    // 装上某个语言的翻译。
    //
    // **返回 false 表示没装上**：语言包不在资源里、或者解析失败。
    // 这时**不能**把界面语言改掉、也不发 languageChanged ——
    // 否则界面会「看起来切了语言」但其实一个字都没变。
    bool apply(const QString& code);

    // 上一次 apply() 失败的原因（成功时为空）。
    //
    // 界面用它提示「语言包没装 / 加载失败」，**不许静默劣化**：
    // 静默失败的代价是用户以为界面已经换成英文，其实还是中文。
    QString lastError() const { return lastError_; }

    // 某个语言的语言包现在从哪来：
    // "user"（用户目录里装的）/ "builtin"（程序自带的）/ 空（哪儿都没有）。
    QString languagePackSource(const QString& code) const;

    // 缺包时给用户的**可执行引导**：语言包放哪、怎么装。
    //
    // 为什么不能只报「没有找到语言包」：用户看到那句话也不知道该干什么，
    // 只能干等。引导要写清「去哪个目录、放什么文件、界面上哪儿能装」。
    QString missingPackGuidance(const QString& code) const;

signals:
    // 语言已切换并已装好翻译；各窗口据此就地重译。
    void languageChanged(const QString& code);

private:
    LanguageManager();

    void persist() const;

    QString preference_ = QStringLiteral("system");
    QString code_;
    TsTranslator* translator_ = nullptr;
    QString lastError_;
};

}  // namespace aipdf::i18n
