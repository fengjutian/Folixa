#include "app/Language.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QXmlStreamReader>

namespace aipdf::i18n {
namespace {

constexpr auto kPreferenceKey = "ui/language";
constexpr auto kSystemPreference = "system";

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
    if (translator_ != nullptr) {
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::removeTranslator(translator_);
        }
        delete translator_;
        translator_ = nullptr;
    }

    const QString path = catalogPath(code);
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            auto* translator = new TsTranslator(this);
            QString error;
            if (translator->loadTs(&file, &error) && translator->messageCount() > 0 &&
                QCoreApplication::instance() != nullptr) {
                QCoreApplication::installTranslator(translator);
                translator_ = translator;
            } else {
                delete translator;
            }
        }
    }

    const bool changed = code_ != code;
    code_ = code;
    if (changed) {
        emit languageChanged(code_);
    }
    return true;
}

bool LanguageManager::setPreference(const QString& value) {
    const QString trimmed = value.trimmed();
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
