#include "doc/PrintTemplate.h"

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

namespace aipdf::doc {

namespace {

// 允许的缩放方式。
const QStringList& allowedScalingModes() {
    static const QStringList modes = {QStringLiteral("None"), QStringLiteral("FitToPage"),
                                      QStringLiteral("ShrinkToFit")};
    return modes;
}

// 份数范围。
constexpr int kMinCopies = 1;
constexpr int kMaxCopies = 99;

// 缩放比例范围。
//
// 上限取 399：400% 以上的缩放没有任何实际用途，而放得太大会让
// 「按比例缩放」在打印驱动里算出离谱的页面。
constexpr int kMinScalePercent = 10;
constexpr int kMaxScalePercent = 399;

// 模板在设置文件里的分组。
QString templatesGroup(const QString& settingsKey) {
    return settingsKey + QStringLiteral("/templates");
}

QString templateGroup(const QString& settingsKey, const QString& id) {
    return templatesGroup(settingsKey) + QLatin1Char('/') + id;
}

// 打开存放打印模板的设置文件。
//
// 固定用 IniFormat（用户范围）：
//   - Windows 上默认的 NativeFormat 是注册表，那是用户自己那份设置，
//     单元测试碰不得；
//   - Ini 还能用 QSettings::setPath 把整个存储目录指到临时位置，
//     测试因此可以完全隔离，不必往真实设置里写东西。
//
// 组织名与应用名沿用 main.cpp 里设的那一套，模板和应用其它设置放在同一个目录树下。
QSettings openSettings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("AI PDF Editor Project"), QStringLiteral("PrintTemplates"));
}

void writeTemplate(QSettings& settings, const QString& group, const PrintTemplate& t) {
    settings.setValue(group + QStringLiteral("/name"), t.name);
    settings.setValue(group + QStringLiteral("/copies"), t.copies);
    settings.setValue(group + QStringLiteral("/paperSize"), t.paperSize);
    settings.setValue(group + QStringLiteral("/orientation"), static_cast<int>(t.orientation));
    settings.setValue(group + QStringLiteral("/scaling"), t.scaling);
    settings.setValue(group + QStringLiteral("/scalePercent"), t.scalePercent);
    settings.setValue(group + QStringLiteral("/duplex"), t.duplex);
    settings.setValue(group + QStringLiteral("/color"), t.color);
    settings.setValue(group + QStringLiteral("/pageRange"), t.pageRange);
}

PrintTemplate readTemplate(const QSettings& settings, const QString& group, const QString& id) {
    PrintTemplate t;
    t.id = id;
    t.name = settings.value(group + QStringLiteral("/name")).toString();
    t.copies = settings.value(group + QStringLiteral("/copies"), kMinCopies).toInt();
    t.paperSize =
        settings.value(group + QStringLiteral("/paperSize"), QStringLiteral("A4")).toString();
    t.orientation = static_cast<PageOrientation>(
        settings.value(group + QStringLiteral("/orientation"), 0).toInt());
    t.scaling =
        settings.value(group + QStringLiteral("/scaling"), QStringLiteral("FitToPage")).toString();
    t.scalePercent = settings.value(group + QStringLiteral("/scalePercent"), 100).toInt();
    t.duplex = settings.value(group + QStringLiteral("/duplex"), false).toBool();
    t.color = settings.value(group + QStringLiteral("/color"), true).toBool();
    t.pageRange = settings.value(group + QStringLiteral("/pageRange")).toString();
    return t;
}

void setError(QString* error, const QString& reason) {
    if (error != nullptr) {
        *error = reason;
    }
}

}  // namespace

bool validatePrintTemplate(const PrintTemplate& t, QString* error) {
    if (t.name.trimmed().isEmpty()) {
        setError(error, QStringLiteral("打印模板名称不能为空"));
        return false;
    }
    if (t.copies < kMinCopies || t.copies > kMaxCopies) {
        setError(error, QStringLiteral("打印模板份数必须在 %1 到 %2 之间，当前为 %3")
                            .arg(kMinCopies)
                            .arg(kMaxCopies)
                            .arg(t.copies));
        return false;
    }
    if (t.scalePercent < kMinScalePercent || t.scalePercent > kMaxScalePercent) {
        setError(error, QStringLiteral("打印模板缩放比例必须在 %1 到 %2 之间，当前为 %3")
                            .arg(kMinScalePercent)
                            .arg(kMaxScalePercent)
                            .arg(t.scalePercent));
        return false;
    }
    if (!allowedScalingModes().contains(t.scaling)) {
        setError(error, QStringLiteral("打印模板缩放方式只能是 None / FitToPage / ShrinkToFit 之一，当前为 %1")
                            .arg(t.scaling));
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool savePrintTemplate(const PrintTemplate& t, const QString& settingsKey, QString* error) {
    if (!validatePrintTemplate(t, error)) {
        return false;
    }
    if (settingsKey.trimmed().isEmpty()) {
        setError(error, QStringLiteral("保存打印模板失败：设置键不能为空"));
        return false;
    }

    PrintTemplate stored = t;
    if (stored.id.trimmed().isEmpty()) {
        // 调用方没给 id 就自己生成一个：模板列表靠 id 指认条目，
        // 没有 id 的模板存进去就再也找不回来了。
        stored.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QSettings settings = openSettings();
    writeTemplate(settings, templateGroup(settingsKey, stored.id), stored);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(error, QStringLiteral("保存打印模板失败：写入设置文件出错"));
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool loadPrintTemplate(const QString& id, const QString& settingsKey, PrintTemplate* out,
                        QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("读取打印模板失败：输出参数为空"));
        return false;
    }
    if (id.trimmed().isEmpty()) {
        setError(error, QStringLiteral("读取打印模板失败：模板 id 为空"));
        return false;
    }

    QSettings settings = openSettings();
    const QString group = templateGroup(settingsKey, id);
    if (!settings.contains(group + QStringLiteral("/name"))) {
        setError(error, QStringLiteral("找不到打印模板：%1").arg(id));
        return false;
    }
    *out = readTemplate(settings, group, id);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

QVector<PrintTemplate> listPrintTemplates(const QString& settingsKey) {
    QVector<PrintTemplate> templates;
    if (settingsKey.trimmed().isEmpty()) {
        return templates;
    }

    QSettings settings = openSettings();
    settings.beginGroup(templatesGroup(settingsKey));
    const QStringList ids = settings.childGroups();
    settings.endGroup();

    for (const QString& id : ids) {
        PrintTemplate t;
        if (loadPrintTemplate(id, settingsKey, &t, nullptr)) {
            templates.append(t);
        }
    }
    return templates;
}

bool deletePrintTemplate(const QString& id, const QString& settingsKey) {
    if (id.trimmed().isEmpty() || settingsKey.trimmed().isEmpty()) {
        return false;
    }

    QSettings settings = openSettings();
    const QString group = templateGroup(settingsKey, id);
    if (!settings.contains(group + QStringLiteral("/name"))) {
        // 删一个不存在的模板不是错误，但也确实没删掉任何东西：返回 false。
        return false;
    }
    settings.remove(group);
    settings.sync();
    return true;
}

}  // namespace aipdf::doc
