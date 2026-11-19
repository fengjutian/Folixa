#include "doc/batch/WizardActions.h"

#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

#include <limits>

using aipdf::doc::WizardActionData;
using aipdf::doc::WizardActionSpec;
using aipdf::doc::WizardActionType;
using aipdf::doc::WizardParam;
using aipdf::doc::hasWizardParam;
using aipdf::doc::parseWizardAction;
using aipdf::doc::takeBoolean;
using aipdf::doc::takeInteger;
using aipdf::doc::takeNumber;
using aipdf::doc::takeString;
using aipdf::doc::takeStringList;
using aipdf::doc::wizardActionCapability;
using aipdf::doc::wizardActionCatalog;
using aipdf::doc::wizardBooleanParam;
using aipdf::doc::wizardIntegerParam;
using aipdf::doc::wizardNumberParam;
using aipdf::doc::wizardStringListParam;
using aipdf::doc::wizardStringParam;

namespace {

// 造一组参数（按顺序）。
QVector<WizardParam> params(std::initializer_list<WizardParam> items) {
    QVector<WizardParam> result;
    for (const WizardParam& item : items) result.append(item);
    return result;
}

}  // namespace

class WizardActionsTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 十种动作各一条正常解析。
    void openFileActionParses();
    void mergeActionParses();
    void splitActionParses();
    void bookmarkActionParses();
    void pageNumbersActionParses();
    void watermarkActionParses();
    void encryptActionParses();
    void convertActionParses();
    void printActionParses();
    void endActionParses();

    // 缺参数、类型不对、取值非法、动作不认识。
    void missingRequiredParameterFails();
    void wrongParameterTypeFails();
    void illegalPositionValueFails();
    void illegalStartNumberFails();
    void illegalFormatFails();
    void unknownActionTypeFails();
    void unknownParameterIsRejected();

    // 边界值。
    void copiesBelowRangeFails();
    void copiesNegativeFails();
    void copiesAboveRangeFails();

    // 动作清单。
    void catalogIsNotEmptyAndEveryActionHasCapability();
    void catalogCoversAllTenActionTypes();

    // 通用取值函数本身。
    void takeStringReportsMissingAndWrongType();
    void takeNumberRejectsNonFiniteValue();
    void takeStringListRejectsEmptyItem();
    void takeBooleanRejectsWrongType();

    // 默认值与空值。
    void optionalParameterKeepsDefaultWhenAbsent();
    void requiredStringCannotBeEmpty();
};

void WizardActionsTests::initTestCase() {}

void WizardActionsTests::cleanupTestCase() {}

// ---------------------------------------------------------------------------
// 十种动作的正常解析
// ---------------------------------------------------------------------------

void WizardActionsTests::openFileActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(WizardActionType::OpenFile,
                                params({wizardStringParam(QStringLiteral("path"),
                                                          QStringLiteral("D:/tmp/a.pdf"))}),
                                &data, &error),
             qPrintable(error));
    QCOMPARE(data.type, WizardActionType::OpenFile);
    QCOMPARE(data.openFile.path, QStringLiteral("D:/tmp/a.pdf"));
}

void WizardActionsTests::mergeActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(
                 WizardActionType::MergeDocuments,
                 params({wizardStringListParam(QStringLiteral("inputPaths"),
                                               QStringList({QStringLiteral("a.pdf"),
                                                            QStringLiteral("b.pdf")})),
                         wizardStringParam(QStringLiteral("outputPath"),
                                           QStringLiteral("D:/tmp/out.pdf"))}),
                 &data, &error),
             qPrintable(error));
    QCOMPARE(data.merge.inputPaths.size(), 2);
    QCOMPARE(data.merge.inputPaths.at(0), QStringLiteral("a.pdf"));
    QCOMPARE(data.merge.outputPath, QStringLiteral("D:/tmp/out.pdf"));
}

void WizardActionsTests::splitActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(
                 WizardActionType::SplitDocument,
                 params({wizardStringParam(QStringLiteral("outputDirectory"),
                                           QStringLiteral("D:/tmp/out")),
                         wizardStringParam(QStringLiteral("mode"), QStringLiteral("each")),
                         wizardStringListParam(QStringLiteral("ranges"),
                                               QStringList({QStringLiteral("1-2")}))}),
                 &data, &error),
             qPrintable(error));
    QCOMPARE(data.split.outputDirectory, QStringLiteral("D:/tmp/out"));
    QCOMPARE(data.split.mode, QStringLiteral("each"));
    QCOMPARE(data.split.ranges.size(), 1);
    QCOMPARE(data.split.ranges.at(0), QStringLiteral("1-2"));
}

void WizardActionsTests::bookmarkActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(
                 WizardActionType::AddBookmark,
                 params({wizardStringParam(QStringLiteral("title"), QStringLiteral("第一章")),
                         wizardIntegerParam(QStringLiteral("pageIndex"), 7)}),
                 &data, &error),
             qPrintable(error));
    QCOMPARE(data.bookmark.title, QStringLiteral("第一章"));
    QCOMPARE(data.bookmark.pageIndex, 7);
}

void WizardActionsTests::pageNumbersActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(
                 WizardActionType::AddPageNumbers,
                 params({wizardStringParam(QStringLiteral("position"),
                                           QStringLiteral("bottomRight")),
                         wizardIntegerParam(QStringLiteral("startNumber"), 1)}),
                 &data, &error),
             qPrintable(error));
    QCOMPARE(data.pageNumbers.position, QStringLiteral("bottomRight"));
    QCOMPARE(data.pageNumbers.startNumber, 1);
}

void WizardActionsTests::watermarkActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(
                 WizardActionType::AddWatermark,
                 params({wizardStringParam(QStringLiteral("text"), QStringLiteral("机密")),
                         wizardNumberParam(QStringLiteral("opacity"), 0.5),
                         wizardStringParam(QStringLiteral("position"), QStringLiteral("center"))}),
                 &data, &error),
             qPrintable(error));
    QCOMPARE(data.watermark.text, QStringLiteral("机密"));
    QCOMPARE(data.watermark.opacity, 0.5);
    QCOMPARE(data.watermark.position, QStringLiteral("center"));
}

void WizardActionsTests::encryptActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(
                 WizardActionType::Encrypt,
                 params({wizardStringParam(QStringLiteral("userPassword"), QStringLiteral("u")),
                         wizardStringParam(QStringLiteral("ownerPassword"), QStringLiteral("o")),
                         wizardBooleanParam(QStringLiteral("allowPrinting"), false)}),
                 &data, &error),
             qPrintable(error));
    QCOMPARE(data.encrypt.userPassword, QStringLiteral("u"));
    QCOMPARE(data.encrypt.ownerPassword, QStringLiteral("o"));
    QCOMPARE(data.encrypt.allowPrinting, false);
}

void WizardActionsTests::convertActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(
                 WizardActionType::ConvertToFormat,
                 params({wizardStringParam(QStringLiteral("format"), QStringLiteral("docx")),
                         wizardStringParam(QStringLiteral("outputPath"),
                                           QStringLiteral("D:/tmp/out.docx"))}),
                 &data, &error),
             qPrintable(error));
    QCOMPARE(data.convert.format, QStringLiteral("docx"));
    QCOMPARE(data.convert.outputPath, QStringLiteral("D:/tmp/out.docx"));
}

void WizardActionsTests::printActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(
                 WizardActionType::Print,
                 params({wizardStringParam(QStringLiteral("printerName"), QStringLiteral("HP")),
                         wizardIntegerParam(QStringLiteral("copies"), 3),
                         wizardStringParam(QStringLiteral("paperSize"), QStringLiteral("A4")),
                         wizardStringParam(QStringLiteral("pageRange"), QStringLiteral("1-3,5"))}),
                 &data, &error),
             qPrintable(error));
    QCOMPARE(data.print.printerName, QStringLiteral("HP"));
    QCOMPARE(data.print.copies, 3);
    QCOMPARE(data.print.paperSize, QStringLiteral("A4"));
    QCOMPARE(data.print.pageRange, QStringLiteral("1-3,5"));
}

void WizardActionsTests::endActionParses() {
    QString error;
    WizardActionData data;
    QVERIFY2(parseWizardAction(WizardActionType::End, params({}), &data, &error), qPrintable(error));
    QCOMPARE(data.type, WizardActionType::End);
}

// ---------------------------------------------------------------------------
// 缺参数、类型不对、取值非法、动作不认识
// ---------------------------------------------------------------------------

void WizardActionsTests::missingRequiredParameterFails() {
    QString error;
    WizardActionData data;
    // path 是必填参数：缺了就必须说清缺的是哪个，不能拿空串顶替。
    QVERIFY(!parseWizardAction(WizardActionType::OpenFile, params({}), &data, &error));
    QVERIFY2(error.contains(QString::fromUtf8("缺少必填参数")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("path")), qPrintable(error));
}

void WizardActionsTests::wrongParameterTypeFails() {
    QString error;
    WizardActionData data;
    // path 必须是字符串：写成整数要报「类型不对」。
    QVERIFY(!parseWizardAction(WizardActionType::OpenFile,
                               params({wizardIntegerParam(QStringLiteral("path"), 5)}), &data, &error));
    QVERIFY2(error.contains(QString::fromUtf8("类型应为")), qPrintable(error));
    QVERIFY2(error.contains(QString::fromUtf8("字符串")), qPrintable(error));
    QVERIFY2(error.contains(QString::fromUtf8("整数")), qPrintable(error));
}

void WizardActionsTests::illegalPositionValueFails() {
    QString error;
    WizardActionData data;
    // 水印位置只支持 center：corner 是非法取值。
    QVERIFY(!parseWizardAction(
        WizardActionType::AddWatermark,
        params({wizardStringParam(QStringLiteral("text"), QStringLiteral("机密")),
                wizardStringParam(QStringLiteral("position"), QStringLiteral("corner"))}),
        &data, &error));
    QVERIFY2(error.contains(QString::fromUtf8("取值必须是")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("center")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("corner")), qPrintable(error));
}

void WizardActionsTests::illegalStartNumberFails() {
    QString error;
    WizardActionData data;
    // 页码起始号只支持 1：5 是非法取值。
    QVERIFY(!parseWizardAction(
        WizardActionType::AddPageNumbers,
        params({wizardStringParam(QStringLiteral("position"), QStringLiteral("bottomRight")),
                wizardIntegerParam(QStringLiteral("startNumber"), 5)}),
        &data, &error));
    QVERIFY2(error.contains(QStringLiteral("startNumber")), qPrintable(error));
    QVERIFY2(error.contains(QString::fromUtf8("当前为 5")), qPrintable(error));
}

void WizardActionsTests::illegalFormatFails() {
    QString error;
    WizardActionData data;
    // 目标格式只能是清单里的那几种：xyz 不是其中之一。
    QVERIFY(!parseWizardAction(
        WizardActionType::ConvertToFormat,
        params({wizardStringParam(QStringLiteral("format"), QStringLiteral("xyz")),
                wizardStringParam(QStringLiteral("outputPath"), QStringLiteral("D:/tmp/out.xyz"))}),
        &data, &error));
    QVERIFY2(error.contains(QStringLiteral("format")), qPrintable(error));
    QVERIFY2(error.contains(QString::fromUtf8("取值必须是")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("xyz")), qPrintable(error));
}

void WizardActionsTests::unknownActionTypeFails() {
    QString error;
    WizardActionData data;
    // 动作类型不认识时必须明确报错，不猜。
    QVERIFY(!parseWizardAction(WizardActionType::Unknown, params({}), &data, &error));
    QVERIFY2(error.contains(QString::fromUtf8("未知")), qPrintable(error));
}

void WizardActionsTests::unknownParameterIsRejected() {
    QString error;
    WizardActionData data;
    // 参数名拼错不许被吞掉。
    QVERIFY(!parseWizardAction(
        WizardActionType::Print, params({wizardIntegerParam(QStringLiteral("copy"), 2)}), &data,
        &error));
    QVERIFY2(error.contains(QString::fromUtf8("不认识参数")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("copy")), qPrintable(error));
}

// ---------------------------------------------------------------------------
// 边界值
// ---------------------------------------------------------------------------

void WizardActionsTests::copiesBelowRangeFails() {
    QString error;
    WizardActionData data;
    QVERIFY(!parseWizardAction(WizardActionType::Print,
                               params({wizardIntegerParam(QStringLiteral("copies"), 0)}), &data,
                               &error));
    QVERIFY2(error.contains(QStringLiteral("copies")), qPrintable(error));
    QVERIFY2(error.contains(QString::fromUtf8("1 到 99")), qPrintable(error));
}

void WizardActionsTests::copiesNegativeFails() {
    QString error;
    WizardActionData data;
    QVERIFY(!parseWizardAction(WizardActionType::Print,
                               params({wizardIntegerParam(QStringLiteral("copies"), -1)}), &data,
                               &error));
    QVERIFY2(error.contains(QStringLiteral("copies")), qPrintable(error));
    QVERIFY2(error.contains(QString::fromUtf8("1 到 99")), qPrintable(error));
}

void WizardActionsTests::copiesAboveRangeFails() {
    QString error;
    WizardActionData data;
    QVERIFY(!parseWizardAction(WizardActionType::Print,
                               params({wizardIntegerParam(QStringLiteral("copies"), 100)}), &data,
                               &error));
    QVERIFY2(error.contains(QStringLiteral("copies")), qPrintable(error));
    QVERIFY2(error.contains(QString::fromUtf8("1 到 99")), qPrintable(error));
}

// ---------------------------------------------------------------------------
// 动作清单
// ---------------------------------------------------------------------------

void WizardActionsTests::catalogIsNotEmptyAndEveryActionHasCapability() {
    const QVector<WizardActionSpec> catalog = wizardActionCatalog();
    QVERIFY(!catalog.isEmpty());
    for (const WizardActionSpec& spec : catalog) {
        // 能力边界说明必须非空：界面层靠它回答「这一步能不能做」。
        QVERIFY2(!spec.capability.trimmed().isEmpty(), qPrintable(spec.name));
        QVERIFY2(!spec.title.trimmed().isEmpty(), qPrintable(spec.name));
        QVERIFY2(!spec.name.trimmed().isEmpty(), qPrintable(spec.title));
        QVERIFY2(!wizardActionCapability(spec.type).trimmed().isEmpty(), qPrintable(spec.name));
    }
}

void WizardActionsTests::catalogCoversAllTenActionTypes() {
    const QVector<WizardActionSpec> catalog = wizardActionCatalog();
    QCOMPARE(catalog.size(), 10);
    const QVector<WizardActionType> expected = {
        WizardActionType::OpenFile,      WizardActionType::MergeDocuments,
        WizardActionType::SplitDocument, WizardActionType::AddBookmark,
        WizardActionType::AddPageNumbers, WizardActionType::AddWatermark,
        WizardActionType::Encrypt,       WizardActionType::ConvertToFormat,
        WizardActionType::Print,         WizardActionType::End};
    for (const WizardActionType type : expected) {
        bool found = false;
        for (const WizardActionSpec& spec : catalog) {
            if (spec.type == type) found = true;
        }
        QVERIFY2(found, qPrintable(wizardActionCapability(type)));
    }
}

// ---------------------------------------------------------------------------
// 通用取值函数本身
// ---------------------------------------------------------------------------

void WizardActionsTests::takeStringReportsMissingAndWrongType() {
    QString error;
    QString value;
    // 不存在与类型不对要说成两回事。
    QVERIFY(!hasWizardParam(params({}), QStringLiteral("path")));
    QVERIFY(!takeString(params({}), QStringLiteral("path"), &value, &error));
    QVERIFY2(error.contains(QString::fromUtf8("不存在")), qPrintable(error));

    error.clear();
    QVERIFY(!takeString(params({wizardIntegerParam(QStringLiteral("path"), 1)}),
                        QStringLiteral("path"), &value, &error));
    QVERIFY2(error.contains(QString::fromUtf8("类型应为")), qPrintable(error));

    error.clear();
    QVERIFY(takeString(params({wizardStringParam(QStringLiteral("path"), QStringLiteral("a.pdf"))}),
                        QStringLiteral("path"), &value, &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(value, QStringLiteral("a.pdf"));
}

void WizardActionsTests::takeNumberRejectsNonFiniteValue() {
    QString error;
    double value = 0.0;
    // NaN 不是能用的取值：报成取值非法，不让它流到下游。
    QVERIFY(!takeNumber(params({wizardNumberParam(QStringLiteral("opacity"),
                                                    std::numeric_limits<double>::quiet_NaN())}),
                         QStringLiteral("opacity"), &value, &error));
    QVERIFY2(error.contains(QString::fromUtf8("有限数")), qPrintable(error));

    error.clear();
    QVERIFY(takeNumber(params({wizardNumberParam(QStringLiteral("opacity"), 0.25)}),
                        QStringLiteral("opacity"), &value, &error));
    QCOMPARE(value, 0.25);
}

void WizardActionsTests::takeStringListRejectsEmptyItem() {
    QString error;
    QStringList values;
    // 列表里的空项一定是填错了。
    QVERIFY(!takeStringList(params({wizardStringListParam(QStringLiteral("inputPaths"),
                                                          QStringList({QStringLiteral("a.pdf"),
                                                                       QString()}))}),
                             QStringLiteral("inputPaths"), &values, &error));
    QVERIFY2(error.contains(QString::fromUtf8("空项")), qPrintable(error));

    error.clear();
    QVERIFY(takeStringList(params({wizardStringListParam(QStringLiteral("inputPaths"),
                                                          QStringList({QStringLiteral("a.pdf")}))}),
                            QStringLiteral("inputPaths"), &values, &error));
    QCOMPARE(values.size(), 1);
}

void WizardActionsTests::takeBooleanRejectsWrongType() {
    QString error;
    bool value = false;
    QVERIFY(!takeBoolean(params({wizardStringParam(QStringLiteral("allowPrinting"),
                                                    QStringLiteral("true"))}),
                         QStringLiteral("allowPrinting"), &value, &error));
    QVERIFY2(error.contains(QString::fromUtf8("类型应为")), qPrintable(error));

    error.clear();
    QVERIFY(takeBoolean(params({wizardBooleanParam(QStringLiteral("allowPrinting"), true)}),
                         QStringLiteral("allowPrinting"), &value, &error));
    QCOMPARE(value, true);
}

// ---------------------------------------------------------------------------
// 默认值与空值
// ---------------------------------------------------------------------------

void WizardActionsTests::optionalParameterKeepsDefaultWhenAbsent() {
    QString error;
    WizardActionData data;
    // 可选参数没给：用规格里的默认值，**不报错**。
    QVERIFY2(parseWizardAction(
                 WizardActionType::AddWatermark,
                 params({wizardStringParam(QStringLiteral("text"), QStringLiteral("机密"))}),
                 &data, &error),
             qPrintable(error));
    QCOMPARE(data.watermark.opacity, 0.15);
    QCOMPARE(data.watermark.position, QStringLiteral("center"));

    error.clear();
    QVERIFY2(parseWizardAction(WizardActionType::Print, params({}), &data, &error),
             qPrintable(error));
    QCOMPARE(data.print.copies, 1);
    QVERIFY(data.print.printerName.isEmpty());
}

void WizardActionsTests::requiredStringCannotBeEmpty() {
    QString error;
    WizardActionData data;
    // 必填参数给了个空值：与「没给」分开报。
    QVERIFY(!parseWizardAction(WizardActionType::OpenFile,
                               params({wizardStringParam(QStringLiteral("path"),
                                                         QStringLiteral("   "))}),
                               &data, &error));
    QVERIFY2(error.contains(QString::fromUtf8("取值不能为空")), qPrintable(error));
}

QTEST_GUILESS_MAIN(WizardActionsTests)
#include "WizardActionsTests.moc"
