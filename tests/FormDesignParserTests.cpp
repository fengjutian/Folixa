// 「表单设计助手」解析层的单元测试。
//
// 这层最怕"看着解析成功、其实字段是错的"：比例被模型写成 85、种类名拼错、
// 名字空着——这些都不会抛异常，只会安静地生成一份错的表单。
// 所以这里逐条钉住容错行为，而不是只断言"没崩"。

#include "pdf/FormDesignParser.h"

#include <QTest>

using aipdf::pdf::DesignedFormField;
using aipdf::pdf::FormFieldKind;
using aipdf::pdf::kindFromName;
using aipdf::pdf::kindName;
using aipdf::pdf::parseDesignedFields;
using aipdf::pdf::stripCodeFence;

class FormDesignParserTests : public QObject {
    Q_OBJECT

private slots:
    void parsesPlainJsonArray();
    void parsesFencedJsonBlock();
    void findsArrayInsideSurroundingText();
    void rejectsInvalidJson();
    void rejectsEmptyArray();
    void rejectsAnswerWithoutArray();
    void skipsItemsWithoutName();
    void unknownKindFallsBackToText();
    void clampsOutOfRangeRatios();
    void usesDefaultRatiosWhenMissing();
    void ignoresNonNumericCoordinates();
    void kindNameRoundTrips();
    void kindFromNameAcceptsCaseAndSpaces();
    void stripCodeFenceKeepsPlainText();
    void systemPromptAsksForJsonOnly();
};

// 最普通的情况：一段干净的 JSON 数组。
void FormDesignParserTests::parsesPlainJsonArray() {
    const QString answer = QStringLiteral(
        R"([{"name":"购买方名称","tooltip":"公司全称","kind":"text",)"
        R"("x":0.1,"y":0.2,"width":0.3,"height":0.03}])");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY2(parseDesignedFields(answer, &fields, &error), qPrintable(error));
    QCOMPARE(fields.size(), 1);
    const DesignedFormField& field = fields.first();
    QCOMPARE(field.name, QStringLiteral("购买方名称"));
    QCOMPARE(field.toolTip, QStringLiteral("公司全称"));
    QCOMPARE(field.kind, FormFieldKind::Text);
    QCOMPARE(field.relX, 0.1);
    QCOMPARE(field.relY, 0.2);
    QCOMPARE(field.relWidth, 0.3);
    QCOMPARE(field.relHeight, 0.03);
}

// 模型爱加代码块围栏：围栏本身不该影响解析。
void FormDesignParserTests::parsesFencedJsonBlock() {
    const QString answer = QStringLiteral("```json\n"
                                          "[{\"name\":\"total\",\"kind\":\"combo\"}]\n"
                                          "```");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY2(parseDesignedFields(answer, &fields, &error), qPrintable(error));
    QCOMPARE(fields.size(), 1);
    QCOMPARE(fields.first().name, QStringLiteral("total"));
    QCOMPARE(fields.first().kind, FormFieldKind::ComboBox);
}

// 围栏前后还有解释文字：只要能挑出 [...] 就该解析成功。
void FormDesignParserTests::findsArrayInsideSurroundingText() {
    const QString answer = QStringLiteral("好的，这是设计：\n"
                                          "```json\n"
                                          "[{\"name\":\"amount\",\"kind\":\"checkbox\"}]\n"
                                          "```\n"
                                          "需要调整请告诉我。");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY2(parseDesignedFields(answer, &fields, &error), qPrintable(error));
    QCOMPARE(fields.size(), 1);
    QCOMPARE(fields.first().name, QStringLiteral("amount"));
    QCOMPARE(fields.first().kind, FormFieldKind::CheckBox);
}

// 坏 JSON 必须报错，而且要说得清为什么——界面只显示这一句话。
void FormDesignParserTests::rejectsInvalidJson() {
    const QString answer = QStringLiteral("[{\"name\": }]");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY(!parseDesignedFields(answer, &fields, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(fields.isEmpty());
}

// 空数组：解析成功但一个字段都没有，同样算失败。
void FormDesignParserTests::rejectsEmptyArray() {
    const QString answer = QStringLiteral("[]");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY(!parseDesignedFields(answer, &fields, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(fields.isEmpty());
}

// 完全没有数组：模型跑题了。
void FormDesignParserTests::rejectsAnswerWithoutArray() {
    const QString answer = QStringLiteral("抱歉，我没法设计这个表单。");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY(!parseDesignedFields(answer, &fields, &error));
    QVERIFY(!error.isEmpty());
}

// 名字为空的项直接丢掉：建域时没有名字的字段是废的。
void FormDesignParserTests::skipsItemsWithoutName() {
    const QString answer = QStringLiteral(
        R"([{"name":"   ","kind":"text"},{"name":"kept","kind":"text"}])");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY2(parseDesignedFields(answer, &fields, &error), qPrintable(error));
    QCOMPARE(fields.size(), 1);
    QCOMPARE(fields.first().name, QStringLiteral("kept"));
}

// 认不出来的种类名当文本域：文本域是唯一不会出事的兜底。
void FormDesignParserTests::unknownKindFallsBackToText() {
    const QString answer = QStringLiteral(R"([{"name":"weird","kind":"weird"}])");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY2(parseDesignedFields(answer, &fields, &error), qPrintable(error));
    QCOMPARE(fields.size(), 1);
    QCOMPARE(fields.first().kind, FormFieldKind::Text);
}

// 比例越界回落默认值：模型偶尔给 85（百分数）或负数。
void FormDesignParserTests::clampsOutOfRangeRatios() {
    const QString answer = QStringLiteral(
        R"([{"name":"a","x":85,"y":-1,"width":2,"height":85}])");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY2(parseDesignedFields(answer, &fields, &error), qPrintable(error));
    QCOMPARE(fields.size(), 1);
    const DesignedFormField& field = fields.first();
    QCOMPARE(field.relX, 0.1);
    QCOMPARE(field.relY, 0.1);
    QCOMPARE(field.relWidth, 0.3);
    QCOMPARE(field.relHeight, 0.03);
}

// 坐标缺省时用默认比例，而不是 0（0 会把字段堆到页面边上）。
void FormDesignParserTests::usesDefaultRatiosWhenMissing() {
    const QString answer = QStringLiteral(R"([{"name":"a","kind":"radio"}])");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY2(parseDesignedFields(answer, &fields, &error), qPrintable(error));
    QCOMPARE(fields.size(), 1);
    const DesignedFormField& field = fields.first();
    QCOMPARE(field.kind, FormFieldKind::RadioButton);
    QCOMPARE(field.relX, 0.1);
    QCOMPARE(field.relY, 0.1);
    QCOMPARE(field.relWidth, 0.3);
    QCOMPARE(field.relHeight, 0.03);
}

// 坐标写成字符串时不当成数字：JSON 里它不是数值，走默认值。
void FormDesignParserTests::ignoresNonNumericCoordinates() {
    const QString answer = QStringLiteral(R"([{"name":"a","x":"0.5","width":true}])");
    QVector<DesignedFormField> fields;
    QString error;
    QVERIFY2(parseDesignedFields(answer, &fields, &error), qPrintable(error));
    QCOMPARE(fields.size(), 1);
    QCOMPARE(fields.first().relX, 0.1);
    QCOMPARE(fields.first().relWidth, 0.3);
}

// 六种类型：名字与类型必须能互相认出来，界面表格才能来回显示。
void FormDesignParserTests::kindNameRoundTrips() {
    const FormFieldKind kinds[] = {
        FormFieldKind::Text,    FormFieldKind::CheckBox, FormFieldKind::RadioButton,
        FormFieldKind::ComboBox, FormFieldKind::ListBox,  FormFieldKind::PushButton,
    };
    for (FormFieldKind kind : kinds) {
        const QString name = kindName(kind);
        QVERIFY2(!name.isEmpty(), "种类名不能为空");
        QVERIFY2(kindFromName(name) == kind,
                 qPrintable(QStringLiteral("%1 ↔ %2 不一致").arg(name, kindName(kindFromName(name)))));
    }
    QCOMPARE(kindName(FormFieldKind::Text), QStringLiteral("text"));
    QCOMPARE(kindName(FormFieldKind::CheckBox), QStringLiteral("checkbox"));
    QCOMPARE(kindName(FormFieldKind::RadioButton), QStringLiteral("radio"));
    QCOMPARE(kindName(FormFieldKind::ComboBox), QStringLiteral("combo"));
    QCOMPARE(kindName(FormFieldKind::ListBox), QStringLiteral("list"));
    QCOMPARE(kindName(FormFieldKind::PushButton), QStringLiteral("button"));
}

// 种类名大小写、空格都不该影响判断。
void FormDesignParserTests::kindFromNameAcceptsCaseAndSpaces() {
    QCOMPARE(kindFromName(QStringLiteral(" CheckBox ")), FormFieldKind::CheckBox);
    QCOMPARE(kindFromName(QStringLiteral("TEXT")), FormFieldKind::Text);
    QCOMPARE(kindFromName(QStringLiteral("button")), FormFieldKind::PushButton);
    QCOMPARE(kindFromName(QStringLiteral("weird")), FormFieldKind::Text);
    QCOMPARE(kindFromName(QString()), FormFieldKind::Text);
}

// 围栏摘除：没有围栏就原样返回，围栏不完整也不该吃掉正文。
void FormDesignParserTests::stripCodeFenceKeepsPlainText() {
    QCOMPARE(stripCodeFence(QStringLiteral("  [1]  ")), QStringLiteral("[1]"));
    QCOMPARE(stripCodeFence(QStringLiteral("```json\n[1]\n```")), QStringLiteral("[1]"));
    QCOMPARE(stripCodeFence(QStringLiteral("```")), QStringLiteral("```"));
    QCOMPARE(stripCodeFence(QStringLiteral("```json")), QStringLiteral("```json"));
}

// 提示词是格式约定的载体：改了它，模型返回的东西就换了一种形状。
void FormDesignParserTests::systemPromptAsksForJsonOnly() {
    const QString prompt = aipdf::pdf::designSystemPrompt();
    QVERIFY(prompt.contains(QStringLiteral("JSON")));
    QVERIFY(prompt.contains(QStringLiteral("数组")));
    QVERIFY(prompt.contains(QStringLiteral("height")));
}

QTEST_MAIN(FormDesignParserTests)
#include "FormDesignParserTests.moc"
