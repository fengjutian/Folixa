// 表单模板存储（本地 JSON 模板目录）的单元测试。
//
// 这条通路的失败很难被用户察觉：模板文件写坏了、模板名撞了车、坏模板把整个
// 列表都拖垮——最后表现都是"套上去的模板少了几个域"，而不是报错。
//
// 所有测试都用 QTemporaryDir，不碰用户真实的模板目录。

#include "pdf/FormTemplateStore.h"

#include <QDir>
#include <QFile>
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

QTEST_MAIN(FormTemplateStoreTests)
#include "FormTemplateStoreTests.moc"
