#include "app/Language.h"

#include "i18n/Translate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace aipdf::i18n {
namespace {

constexpr auto kPreferenceKey = "ui/language";
constexpr auto kSystemPreference = "system";

// 语言包目录的覆盖值（测试用；空 = 用默认的用户目录）。
QString gLanguagePackDirectoryOverride;

// 词条键：上下文 + 源文案 + 消歧义，与 Qt 的查找键一一对应。
// 分隔符用不可能出现在 .ts 文本里的单元分隔符（U+001F）。
const QChar kKeySeparator(0x1f);

QString messageKey(const QString& context, const QString& source, const QString& disambiguation) {
    return context + kKeySeparator + source + kKeySeparator + disambiguation;
}

void skipElement(QXmlStreamReader& reader) {
    reader.skipCurrentElement();
}

// 读 <translation> 的内容。
//
// 不能直接用 readElementText()：复数词条的 <translation> 里套着
// <numerusform> 子元素，默认行为遇到子元素会判成格式错误。这里手动走一遍，
// 文本拼成单数/无复数文案，<numerusform> 按顺序收进形式表。
void readTranslationElement(QXmlStreamReader& reader, QString* text,
                            QStringList* numerusForms) {
    QString buffer;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.name() == u"translation") {
            break;
        }
        if (token == QXmlStreamReader::Characters) {
            buffer += reader.text();
        } else if (token == QXmlStreamReader::StartElement && reader.name() == u"numerusform") {
            numerusForms->append(reader.readElementText(QXmlStreamReader::SkipChildElements));
        } else if (token == QXmlStreamReader::StartElement) {
            reader.skipCurrentElement();
        }
    }
    *text = buffer;
}

// 读一条 <message>，读完后 reader 停在 </message> 上。
void readMessage(QXmlStreamReader& reader, const QString& context,
                 QHash<QString, CatalogMessage>& out) {
    QString source;
    QString disambiguation;
    QString translation;
    QStringList numerusForms;
    bool haveSource = false;
    bool dropped = false;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.name() == u"message") {
            break;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        if (name == u"source") {
            source = reader.readElementText();
            haveSource = true;
        } else if (name == u"name") {
            // <message> 里的 name 是消岐义键（QObject::tr 的第二个参数）。
            disambiguation = reader.readElementText();
        } else if (name == u"translation") {
            const QXmlStreamAttributes attributes = reader.attributes();
            const QStringView type = attributes.value(u"type");
            if (type == u"unfinished" || type == u"obsolete" || type == u"vanished") {
                dropped = true;
            }
            readTranslationElement(reader, &translation, &numerusForms);
        } else if (name == u"numerusform") {
            numerusForms.append(reader.readElementText());
        } else {
            // location / comment / extracomment / translatorcomment / oldsource /
            // userdata：目录元信息，运行时用不上。
            skipElement(reader);
        }
    }

    if (!haveSource || dropped) {
        return;
    }
    if (numerusForms.isEmpty() && translation.trimmed().isEmpty()) {
        return;
    }
    CatalogMessage message;
    message.text = translation;
    message.numerusForms = numerusForms;
    out.insert(messageKey(context, source, disambiguation), message);
}

// 读一个 <context>，读完后 reader 停在 </context> 上。
void readContext(QXmlStreamReader& reader, QHash<QString, CatalogMessage>& out) {
    QString context;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.name() == u"context") {
            return;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        if (reader.name() == u"name") {
            context = reader.readElementText();
        } else if (reader.name() == u"message") {
            readMessage(reader, context, out);
        } else {
            skipElement(reader);
        }
    }
}

}  // namespace

QString languagePackDirectory() {
    if (!gLanguagePackDirectoryOverride.isEmpty()) {
        return gLanguagePackDirectoryOverride;
    }
    // 用户级、可写：%LOCALAPPDATA%\AI PDF Editor Project\AI PDF Editor\translations
    // （Windows 上 QStandardPaths::AppDataLocation 就是这个形状）。
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QLatin1String("/translations");
}

void setLanguagePackDirectoryOverride(const QString& directory) {
    gLanguagePackDirectoryOverride = directory;
}

QString languagePackFileName(const QString& code) {
    return QStringLiteral("ai-pdf-editor_%1.ts").arg(code);
}

QString installedLanguagePackPath(const QString& code) {
    if (code.isEmpty() || code == QLatin1String("zh_CN")) {
        return {};
    }
    const QString path =
        languagePackDirectory() + QLatin1Char('/') + languagePackFileName(code);
    return QFile::exists(path) ? path : QString();
}

QStringList installedLanguageCodes() {
    QStringList codes;
    for (const Language& language : supportedLanguages()) {
        if (!installedLanguagePackPath(language.code).isEmpty()) {
            codes.append(language.code);
        }
    }
    return codes;
}

QString declaredLanguageOf(const QString& sourcePath) {
    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    TsTranslator probe;
    if (!probe.loadTs(&file)) {
        return {};
    }
    return probe.catalogLanguage();
}

bool installLanguagePack(const QString& sourcePath, const QString& code, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (code.isEmpty()) {
        return fail(i18n::tr("aipdf::app::Language", "没有指定语言。"));
    }

    // 先把文件读一遍：既校验它是不是目录，也顺手拿到它自己声明的语言。
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        return fail(i18n::tr("aipdf::app::Language", "读不了 %1：%2")
                        .arg(sourcePath, source.errorString()));
    }
    TsTranslator probe;
    QString parseError;
    if (!probe.loadTs(&source, &parseError) || probe.messageCount() == 0) {
        return fail(parseError.isEmpty()
                        ? i18n::tr("aipdf::app::Language",
                                   "%1 不是有效的语言包（解析失败或没有词条）。")
                              .arg(sourcePath)
                        : parseError);
    }

    // 校验二：目录声明的语言要和目标语言一致。
    //
    // 装错语言包比不装更糟：界面会变成另一种语言，用户还以为是自己刚装的。
    const QString declared = probe.catalogLanguage();
    if (!declared.isEmpty() && declared != code) {
        return fail(i18n::tr("aipdf::app::Language", "这份语言包是 %1 的，不是 %2 的：%3")
                        .arg(declared, code, sourcePath));
    }
    if (code == QLatin1String("zh_CN")) {
        return fail(i18n::tr("aipdf::app::Language", "中文是源语言，不需要语言包。"));
    }
    if (!isSupportedLanguage(code)) {
        return fail(i18n::tr("aipdf::app::Language", "当前版本不支持 %1 的界面语言。").arg(code));
    }

    const QString directoryPath = languagePackDirectory();
    if (!QDir().mkpath(directoryPath)) {
        return fail(i18n::tr("aipdf::app::Language", "建不了语言包目录 %1。").arg(directoryPath));
    }
    const QString target = QDir(directoryPath).filePath(languagePackFileName(code));
    if (QFile::exists(target) && !QFile::remove(target)) {
        return fail(i18n::tr("aipdf::app::Language", "换不掉旧的语言包 %1。").arg(target));
    }
    if (!QFile::copy(sourcePath, target)) {
        return fail(i18n::tr("aipdf::app::Language", "把语言包复制到 %1 失败。").arg(target));
    }
    return true;
}

bool installLanguagePackInteractively(QWidget* parent) {
    const QString path = QFileDialog::getOpenFileName(
        parent, i18n::tr("aipdf::app::Language", "选择语言包文件"), QString(),
        i18n::tr("aipdf::app::Language", "语言包 (*.ts)"));
    if (path.isEmpty()) {
        return false;  // 用户自己取消了，不是失败。
    }

    const QString code = declaredLanguageOf(path);
    if (code.isEmpty()) {
        QMessageBox::warning(
            parent, i18n::tr("aipdf::app::Language", "安装语言包"),
            i18n::tr("aipdf::app::Language",
                     "%1 读不出语言代码：它可能不是标准的 .ts 翻译目录。")
                .arg(path));
        return false;
    }
    if (!isSupportedLanguage(code)) {
        QMessageBox::warning(
            parent, i18n::tr("aipdf::app::Language", "安装语言包"),
            i18n::tr("aipdf::app::Language", "这份语言包的语言（%1）当前版本不支持。").arg(code));
        return false;
    }

    QString error;
    if (!installLanguagePack(path, code, &error)) {
        QMessageBox::warning(parent, i18n::tr("aipdf::app::Language", "安装语言包"),
                             i18n::tr("aipdf::app::Language", "%1 的语言包安装失败：%2")
                                 .arg(code, error));
        return false;
    }

    // 装好了就切过去：用户刚装完就是要用。
    if (!LanguageManager::instance().setPreference(code)) {
        QMessageBox::warning(
            parent, i18n::tr("aipdf::app::Language", "安装语言包"),
            i18n::tr("aipdf::app::Language", "%1 的语言包装好了，但切换失败：%2")
                .arg(code, LanguageManager::instance().lastError()));
        return false;
    }
    QMessageBox::information(
        parent, i18n::tr("aipdf::app::Language", "安装语言包"),
        i18n::tr("aipdf::app::Language", "已安装 %1 的语言包。").arg(code));
    return true;
}

QVector<Language> supportedLanguages() {
    return {
        {QStringLiteral("zh_CN"), QStringLiteral("中文")},
        {QStringLiteral("en"), QStringLiteral("English")},
    };
}

bool isSupportedLanguage(const QString& code) {
    for (const Language& language : supportedLanguages()) {
        if (language.code == code) {
            return true;
        }
    }
    return false;
}

QString nativeNameOf(const QString& code) {
    for (const Language& language : supportedLanguages()) {
        if (language.code == code) {
            return language.nativeName;
        }
    }
    return code;
}

QString resolveLanguage(const QStringList& uiLanguages) {
    for (const QString& tag : uiLanguages) {
        const QString name = QLocale(tag).name();
        if (name.startsWith(QLatin1String("zh"))) {
            return QStringLiteral("zh_CN");
        }
        if (name.startsWith(QLatin1String("en"))) {
            return QStringLiteral("en");
        }
    }
    // 既不认识也不像中文：给英文。总比让用户看中文强。
    return QStringLiteral("en");
}

// ---------------------------------------------------------------- TsTranslator ----

TsTranslator::TsTranslator(QObject* parent) : QTranslator(parent) {}

bool TsTranslator::loadTs(QIODevice* device, QString* errorMessage) {
    messages_.clear();
    catalogLanguage_.clear();
    if (device == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("翻译目录为空");
        }
        return false;
    }
    if (!device->isOpen() && !device->open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("翻译目录打不开");
        }
        return false;
    }

    QXmlStreamReader reader(device);
    bool sawTsElement = false;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token != QXmlStreamReader::StartElement || reader.name() != u"TS") {
            continue;
        }
        sawTsElement = true;
        catalogLanguage_ = reader.attributes().value(u"language").toString();
        while (!reader.atEnd()) {
            const QXmlStreamReader::TokenType inner = reader.readNext();
            if (inner == QXmlStreamReader::EndElement && reader.name() == u"TS") {
                break;
            }
            if (inner == QXmlStreamReader::StartElement && reader.name() == u"context") {
                readContext(reader, messages_);
            }
        }
    }

    if (reader.hasError()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("翻译目录格式错误（第 %1 行）：%2")
                                .arg(reader.lineNumber())
                                .arg(reader.errorString());
        }
        messages_.clear();
        return false;
    }
    if (!sawTsElement) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("不是 Qt .ts 翻译目录");
        }
        return false;
    }
    return true;
}

bool TsTranslator::isEmpty() const { return messages_.isEmpty(); }

QString TsTranslator::translate(const char* context, const char* sourceText,
                                const char* disambiguation, int n) const {
    if (sourceText == nullptr) {
        return QString();
    }
    const QString key = messageKey(QString::fromUtf8(context == nullptr ? "" : context),
                                   QString::fromUtf8(sourceText),
                                   QString::fromUtf8(disambiguation == nullptr ? "" : disambiguation));
    const auto it = messages_.constFind(key);
    if (it == messages_.constEnd()) {
        // 返回 null（而不是空串）：Qt 会据此继续找下一个翻译器，
        // 最后回退到源文案。
        return QString();
    }
    if (!it->numerusForms.isEmpty() && n >= 0) {
        if (n == 1 || it->numerusForms.size() == 1) {
            return it->numerusForms.first();
        }
        return it->numerusForms.size() >= 2 ? it->numerusForms.at(1) : it->numerusForms.last();
    }
    return it->text;
}

// ------------------------------------------------------------- LanguageManager ----

QString LanguageManager::systemPreference() { return QString::fromLatin1(kSystemPreference); }

LanguageManager& LanguageManager::instance() {
    // 故意不放析构：语言管理器活到进程结束，翻译器跟着一起走。
    // 用静态对象会在 QApplication 之后析构，那时卸载翻译器是无效操作。
    static LanguageManager* manager = new LanguageManager();
    return *manager;
}

LanguageManager::LanguageManager() = default;

QString LanguageManager::catalogPath(const QString& code) {
    // 中文是源语言：界面文案本身就是中文，没有目录。
    if (code.isEmpty() || code == QLatin1String("zh_CN")) {
        return QString();
    }
    return QStringLiteral(":/i18n/ai-pdf-editor_%1.ts").arg(code);
}

bool LanguageManager::apply(const QString& code) {
    lastError_.clear();

    // 先把新翻译准备好，**成功之后**再把旧的换下去 ——
    // 中途失败时如果先把旧的卸了，就会留下「旧的没了、新的也没上」的半截状态，
    // 界面会一半中文一半旧语言。
    //
    // 查找顺序：**用户目录 → 程序自带**。
    // 用户目录优先：用户装了新语言包就该用用户的，不能被程序自带的压住。
    const QString userPath = installedLanguagePackPath(code);
    const QString builtinPath = catalogPath(code);
    const QString path = !userPath.isEmpty() ? userPath : builtinPath;
    TsTranslator* translator = nullptr;
    if (!path.isEmpty()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            // 语言包没装上。**不能静默当成功** ——
            // 静默成功的代价是用户以为界面已经换成英文，其实还是中文。
            // 而且不能只说一句「没找到」：要说清去哪儿放、界面上哪儿能装。
            lastError_ = missingPackGuidance(code);
            return false;
        }
        auto* candidate = new TsTranslator(this);
        QString error;
        if (!candidate->loadTs(&file, &error) || candidate->messageCount() == 0) {
            delete candidate;
            lastError_ = error.isEmpty()
                ? i18n::tr("aipdf::app::Language",
                           "%1 的语言包是空的或解析失败，界面仍然是中文。").arg(code)
                : error;
            return false;
        }
        translator = candidate;
    }

    if (translator_ != nullptr) {
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::removeTranslator(translator_);
        }
        delete translator_;
        translator_ = nullptr;
    }
    if (translator != nullptr) {
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::installTranslator(translator);
            translator_ = translator;
        } else {
            delete translator;
        }
    }

    bool changed = code_ != code;
    code_ = code;
    if (changed) {
        emit languageChanged(code_);
    }
    return true;
}

QString LanguageManager::languagePackSource(const QString& code) const {
    if (!installedLanguagePackPath(code).isEmpty()) {
        return QStringLiteral("user");
    }
    if (!catalogPath(code).isEmpty()) {
        return QStringLiteral("builtin");
    }
    return {};
}

QString LanguageManager::missingPackGuidance(const QString& code) const {
    return i18n::tr("aipdf::app::Language",
                    "没有找到 %1 的语言包，界面仍然是中文。把语言包文件（%2）放到这个目录：%3；"
                    "或者在「设置 → 界面语言」里点「安装语言包…」直接选文件安装。")
        .arg(code, languagePackFileName(code), languagePackDirectory());
}

bool LanguageManager::setPreference(const QString& value) {    const QString trimmed = value.trimmed();
    const QString preference =
        trimmed.isEmpty() ? systemPreference() : trimmed;

    QString resolved;
    if (preference == systemPreference()) {
        resolved = resolveLanguage(QLocale::system().uiLanguages());
    } else if (isSupportedLanguage(preference)) {
        resolved = preference;
    } else {
        return false;
    }

    preference_ = preference;
    persist();
    return apply(resolved);
}

void LanguageManager::loadSavedPreference() {
    // 验收脚本与 CI 用环境变量钉死界面语言：断言是按中文文案写的，
    // 英文系统的 runner 上不能因为「跟随系统」而变成英文。
    const QString override = qEnvironmentVariable("AIPDF_UI_LANGUAGE").trimmed();
    if (!override.isEmpty()) {
        QString resolved;
        if (override == systemPreference()) {
            resolved = resolveLanguage(QLocale::system().uiLanguages());
        } else if (isSupportedLanguage(override)) {
            resolved = override;
        }
        if (!resolved.isEmpty()) {
            // 只改这次运行的界面语言，不写进用户设置（也不冲掉用户的选择）。
            preference_ = override;
            apply(resolved);
            return;
        }
    }

    QSettings settings;
    const QString saved =
        settings.value(QString::fromLatin1(kPreferenceKey), systemPreference()).toString();
    if (!setPreference(saved)) {
        // 设置被手工改坏了：回到跟随系统，不因为一个坏值就起不来。
        setPreference(systemPreference());
    }
}

void LanguageManager::persist() const {
    QSettings settings;
    const QString key = QString::fromLatin1(kPreferenceKey);
    if (settings.value(key).toString() != preference_) {
        settings.setValue(key, preference_);
    }
}

}  // namespace aipdf::i18n
