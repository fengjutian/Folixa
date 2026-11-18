// 表单模板存储（本地 JSON 模板目录）的单元测试。
//
// 这条通路的失败很难被用户察觉：模板文件写坏了、模板名撞了车、坏模板把整个
// 列表都拖垮——最后表现都是"套上去的模板少了几个域"，而不是报错。
//
// 所有测试都用 QTemporaryDir，不碰用户真实的模板目录。

#include "pdf/FormTemplateStore.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using aipdf::pdf::FormFieldKind;
using aipdf::pdf::FormTemplate;
using aipdf::pdf::FormTemplateField;
using aipdf::pdf::FormTemplateStore;

namespace {

// 一个带两种域的模板：文本域 + 带选项的组合框。
FormTemplate sampleTemplate(const QString& name) {
    FormTemplate tmpl;
    tmpl.name = name;

    FormTemplateField text;
    text.name = QStringLiteral("fullName");
    text.toolTip = QStringLiteral("姓名");
    text.kind = FormFieldKind::Text;
    text.relX = 0.1;
    text.relY = 0.2;
    text.relWidth = 0.3;
    text.relHeight = 0.04;
    text.required = true;
    tmpl.fields.append(text);

    FormTemplateField combo;
    combo.name = QStringLiteral("country");
    combo.toolTip = QStringLiteral("国家/地区");
    combo.kind = FormFieldKind::ComboBox;
    combo.relX = 0.1;
    combo.relY = 0.3;
    combo.relWidth = 0.3;
    combo.relHeight = 0.04;
    combo.options = QStringList{ QStringLiteral("中国"), QStringLiteral("中国香港") };
    combo.readOnly = true;
    combo.multiline = true;
    tmpl.fields.append(combo);

    return tmpl;
}

// 直接往模板目录里写一个文件，用来造坏文件 / 未知类型域这类情况。
bool writeRaw(const QString& root, const QString& fileName, const QByteArray& data) {
    QDir().mkpath(root);
    QFile file(QDir(root).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(data) == data.size();
}

}  // namespace

class FormTemplateStoreTests : public QObject {
    Q_OBJECT

private slots:
    void savesAndListsTemplate();
    void roundTripsAllFieldAttributes();
    void overwritesTemplateWithSameName();
    void keepsTemplatesWithCollidingFileNames();
    void removesTemplateAndRejectsUnknownName();
    void emptyDirectoryListsNothing();
    void corruptFileDoesNotBreakOtherTemplates();
    void rejectsInvalidNames();
    void skipsFieldWithUnknownKind();
    void defaultRootDirEndsWithFormTemplates();
    void builtInTemplatesCoverCommonForms();
    void builtInTemplateLookupFindsByName();
    void builtInTemplateLookupRejectsUnknownName();
    void builtInTemplateFieldsStayInsidePage();
};

void FormTemplateStoreTests::savesAndListsTemplate() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("templates"));
    FormTemplateStore store(root);

    // 目录还不存在，保存时应该自动建出来。
    QString error;
    QVERIFY2(store.save(sampleTemplate(QStringLiteral("合同模板")), &error),
             qPrintable(error));
    QVERIFY(QDir(root).exists());
    QVERIFY(store.templateNames().contains(QStringLiteral("合同模板")));
}

void FormTemplateStoreTests::roundTripsAllFieldAttributes() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("templates"));
    FormTemplateStore store(root);
    const FormTemplate original = sampleTemplate(QStringLiteral("往返"));
    QVERIFY(store.save(original));

    FormTemplate loaded;
    QString error;
    QVERIFY2(store.load(QStringLiteral("往返"), &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.name, original.name);
    QCOMPARE(loaded.fields.size(), original.fields.size());

    const FormTemplateField& expected = original.fields.at(0);
    const FormTemplateField& actual = loaded.fields.at(0);
    QCOMPARE(actual.name, expected.name);
    QCOMPARE(actual.toolTip, expected.toolTip);
    QVERIFY(actual.kind == expected.kind);
    QCOMPARE(actual.relX, expected.relX);
    QCOMPARE(actual.relY, expected.relY);
    QCOMPARE(actual.relWidth, expected.relWidth);
    QCOMPARE(actual.relHeight, expected.relHeight);
    QCOMPARE(actual.options, expected.options);
    QCOMPARE(actual.readOnly, expected.readOnly);
    QCOMPARE(actual.required, expected.required);
    QCOMPARE(actual.multiline, expected.multiline);

    const FormTemplateField& expectedSecond = original.fields.at(1);
    const FormTemplateField& actualSecond = loaded.fields.at(1);
    QCOMPARE(actualSecond.name, expectedSecond.name);
    QVERIFY(actualSecond.kind == FormFieldKind::ComboBox);
    QCOMPARE(actualSecond.options, expectedSecond.options);
    QCOMPARE(actualSecond.readOnly, true);
    QCOMPARE(actualSecond.multiline, true);
}

void FormTemplateStoreTests::overwritesTemplateWithSameName() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("templates"));
    FormTemplateStore store(root);
    QVERIFY(store.save(sampleTemplate(QStringLiteral("同名"))));

    FormTemplate replacement = sampleTemplate(QStringLiteral("同名"));
    replacement.fields.clear();
    FormTemplateField only;
    only.name = QStringLiteral("only");
    replacement.fields.append(only);
    QVERIFY(store.save(replacement));

    // 同名保存是覆盖，不是多出一份。
    QCOMPARE(store.templateNames().count(QStringLiteral("同名")), 1);
    FormTemplate loaded;
    QVERIFY(store.load(QStringLiteral("同名"), &loaded));
    QCOMPARE(loaded.fields.size(), 1);
    QCOMPARE(loaded.fields.at(0).name, QStringLiteral("only"));
}

void FormTemplateStoreTests::keepsTemplatesWithCollidingFileNames() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("templates"));
    FormTemplateStore store(root);

    // 两个名字里的非法字符被替换后落到同一个文件名上：谁也不能被覆盖掉。
    QVERIFY(store.save(sampleTemplate(QStringLiteral("a*b"))));
    QVERIFY(store.save(sampleTemplate(QStringLiteral("a?b"))));

    const QStringList names = store.templateNames();
    QCOMPARE(names.size(), 2);
    QVERIFY(names.contains(QStringLiteral("a*b")));
    QVERIFY(names.contains(QStringLiteral("a?b")));

    FormTemplate loaded;
    QVERIFY(store.load(QStringLiteral("a?b"), &loaded));
    QCOMPARE(loaded.name, QStringLiteral("a?b"));
}

void FormTemplateStoreTests::removesTemplateAndRejectsUnknownName() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("templates"));
    FormTemplateStore store(root);
    QVERIFY(store.save(sampleTemplate(QStringLiteral("待删除"))));
    QVERIFY(store.templateNames().contains(QStringLiteral("待删除")));

    QString error;
    QVERIFY2(store.remove(QStringLiteral("待删除"), &error), qPrintable(error));
    QVERIFY(!store.templateNames().contains(QStringLiteral("待删除")));

    // 删一个不存在的模板要失败，并且给出原因。
    error.clear();
    QVERIFY(!store.remove(QStringLiteral("不存在"), &error));
    QVERIFY(!error.isEmpty());
}

void FormTemplateStoreTests::emptyDirectoryListsNothing() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("templates"));
    QDir().mkpath(root);
    FormTemplateStore store(root);
    QVERIFY(store.templateNames().isEmpty());

    // 连目录都不存在时也不能出问题。
    FormTemplateStore missing(temporary.filePath(QStringLiteral("not-created")));
    QVERIFY(missing.templateNames().isEmpty());
}

void FormTemplateStoreTests::corruptFileDoesNotBreakOtherTemplates() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("templates"));
    FormTemplateStore store(root);
    QVERIFY(store.save(sampleTemplate(QStringLiteral("好模板"))));

    QVERIFY(writeRaw(root, QStringLiteral("坏模板.json"),
                     QByteArrayLiteral("{ this is not json")));
    QVERIFY(writeRaw(root, QStringLiteral("没名字.json"), QByteArrayLiteral("{ \"fields\": [] }")));

    // 坏文件不影响其它模板的列出。
    const QStringList names = store.templateNames();
    QVERIFY(names.contains(QStringLiteral("好模板")));
    QVERIFY(!names.contains(QStringLiteral("坏模板")));

    // 读坏模板要失败，并且说清楚是文件坏了。
    QString error;
    FormTemplate loaded;
    QVERIFY(!store.load(QStringLiteral("坏模板"), &loaded, &error));
    QVERIFY(!error.isEmpty());
}

void FormTemplateStoreTests::rejectsInvalidNames() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("templates"));
    FormTemplateStore store(root);

    QString error;
    FormTemplate empty;
    error.clear();
    QVERIFY(!store.save(empty, &error));
    QVERIFY(!error.isEmpty());

    FormTemplate slash = sampleTemplate(QStringLiteral("a/b"));
    error.clear();
    QVERIFY(!store.save(slash, &error));
    QVERIFY(!error.isEmpty());

    FormTemplate backslash = sampleTemplate(QStringLiteral("a\\b"));
    error.clear();
    QVERIFY(!store.save(backslash, &error));
    QVERIFY(!error.isEmpty());

    // 被拒的名字不该留下任何文件。
    QVERIFY(store.templateNames().isEmpty());
}

void FormTemplateStoreTests::skipsFieldWithUnknownKind() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("templates"));
    FormTemplateStore store(root);

    const QByteArray json = QByteArrayLiteral(
        "{\n"
        "  \"version\": 1,\n"
        "  \"name\": \"未来模板\",\n"
        "  \"fields\": [\n"
        "    { \"name\": \"weird\", \"kind\": \"Hologram\" },\n"
        "    { \"name\": \"normal\", \"kind\": \"Text\", \"relX\": 0.5 }\n"
        "  ]\n"
        "}\n");
    QVERIFY(writeRaw(root, QStringLiteral("未来模板.json"), json));

    FormTemplate loaded;
    QString error;
    QVERIFY2(store.load(QStringLiteral("未来模板"), &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.name, QStringLiteral("未来模板"));
    // 不认识的域类型只丢掉那一个域，模板本身照样能用。
    QCOMPARE(loaded.fields.size(), 1);
    QCOMPARE(loaded.fields.at(0).name, QStringLiteral("normal"));
    QVERIFY(store.templateNames().contains(QStringLiteral("未来模板")));
}

void FormTemplateStoreTests::defaultRootDirEndsWithFormTemplates() {
    const QString path = FormTemplateStore::defaultRootDir();
    QVERIFY(!path.isEmpty());
    QVERIFY(path.endsWith(QStringLiteral("form-templates")));
}

// 内置模板库：常用表单要真的有模板，用户第一次用「套用模板」才不用先手画一遍。
void FormTemplateStoreTests::builtInTemplatesCoverCommonForms() {
    const QVector<FormTemplate> templates = FormTemplateStore::builtInTemplates();
    QVERIFY(templates.size() >= 4);
    QStringList names;
    for (const FormTemplate& tmpl : templates) {
        names << tmpl.name;
        // 每个模板都得有域：一个域都没有的模板套上去等于没套。
        QVERIFY2(!tmpl.fields.isEmpty(),
                 qPrintable(QStringLiteral("内置模板 %1 一个域都没有").arg(tmpl.name)));
        // 域的名字不能重名：重名的话建域时会互相顶掉。
        QSet<QString> fieldNames;
        for (const FormTemplateField& field : tmpl.fields) {
            QVERIFY2(!fieldNames.contains(field.name),
                     qPrintable(QStringLiteral("模板 %1 里域名 %2 重复了")
                                    .arg(tmpl.name, field.name)));
            fieldNames.insert(field.name);
        }
    }
    QVERIFY(names.contains(QStringLiteral("登记表")));
    QVERIFY(names.contains(QStringLiteral("签到表")));
    QVERIFY(names.contains(QStringLiteral("意见反馈表")));
    QVERIFY(names.contains(QStringLiteral("费用报销单")));
}

// 按名字找内置模板要找得到，而且要把域一起带出来。
void FormTemplateStoreTests::builtInTemplateLookupFindsByName() {
    FormTemplate tmpl;
    QVERIFY(FormTemplateStore::builtInTemplate(QStringLiteral("登记表"), &tmpl));
    QCOMPARE(tmpl.name, QStringLiteral("登记表"));
    QVERIFY(!tmpl.fields.isEmpty());
    // 域的提示文字要能直接用（模板里带的是显示名）。
    bool hasTooltip = false;
    for (const FormTemplateField& field : tmpl.fields) {
        hasTooltip = hasTooltip || !field.toolTip.isEmpty();
    }
    QVERIFY(hasTooltip);
}

// 名字对不上就说找不到，不能返回一份空的模板。
void FormTemplateStoreTests::builtInTemplateLookupRejectsUnknownName() {
    FormTemplate tmpl;
    QVERIFY(!FormTemplateStore::builtInTemplate(QStringLiteral("不存在的模板"), &tmpl));
    QVERIFY(!FormTemplateStore::builtInTemplate(QString(), &tmpl));
}

// 内置模板的域必须落在页面里：比例坐标越界的话套上去就跑到页面外去了。
void FormTemplateStoreTests::builtInTemplateFieldsStayInsidePage() {
    for (const FormTemplate& tmpl : FormTemplateStore::builtInTemplates()) {
        for (const FormTemplateField& field : tmpl.fields) {
            QVERIFY2(field.relX >= 0.0 && field.relX <= 1.0,
                     qPrintable(QStringLiteral("模板 %1 的域 %2 横坐标越界")
                                    .arg(tmpl.name, field.name)));
            QVERIFY2(field.relY >= 0.0 && field.relY <= 1.0,
                     qPrintable(QStringLiteral("模板 %1 的域 %2 纵坐标越界")
                                    .arg(tmpl.name, field.name)));
            QVERIFY2(field.relWidth > 0.0 && field.relWidth <= 1.0,
                     qPrintable(QStringLiteral("模板 %1 的域 %2 宽度越界")
                                    .arg(tmpl.name, field.name)));
            QVERIFY2(field.relHeight > 0.0 && field.relHeight <= 1.0,
                     qPrintable(QStringLiteral("模板 %1 的域 %2 高度越界")
                                    .arg(tmpl.name, field.name)));
            QVERIFY2(field.relX + field.relWidth <= 1.0001,
                     qPrintable(QStringLiteral("模板 %1 的域 %2 横向超出页面")
                                    .arg(tmpl.name, field.name)));
            QVERIFY2(field.relY + field.relHeight <= 1.0001,
                     qPrintable(QStringLiteral("模板 %1 的域 %2 纵向超出页面")
                                    .arg(tmpl.name, field.name)));
        }
    }
}

QTEST_MAIN(FormTemplateStoreTests)
#include "FormTemplateStoreTests.moc"
