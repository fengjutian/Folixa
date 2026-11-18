// 批量替换的单元测试。
//
// 两条线：
//   1) 规则语义（大小写、全字、正则、捕获组、顺序叠加、页范围、错误隔离）；
//   2) 落盘往返：改完的字节要用 PDFium 读回文本层，确认旧字真的没了、新字真的在。
//
// 为什么自己造 PDF：这些用例要的是确定的命中数与页边界，
// 夹具里的文字会随生成脚本调整而变，断言就会飘。
// 夹具（text.pdf）只用来验证「真实文件改完仍然读得出来」这一条。

#include "pdf/ops/BatchReplace.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTest>

using aipdf::pdf::BatchReplaceOptions;
using aipdf::pdf::BatchReplaceResult;
using aipdf::pdf::ReplaceRule;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

// PDF 字面量字符串里的转义。
QString escapePdfText(const QString& text) {
    QString out;
    for (const QChar c : text) {
        if (c == QLatin1Char('(') || c == QLatin1Char(')') || c == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

// 造一份最小可用的 PDF：每页一句文字（Helvetica，标准 14 字体，不用嵌字体）。
QByteArray buildTextPdf(const QStringList& pages) {
    const int pageCount = pages.size();
    const int fontId = 3 + pageCount;
    QStringList objects;
    objects.append(QStringLiteral("<< /Type /Catalog /Pages 2 0 R >>"));
    QString kids;
    for (int i = 0; i < pageCount; ++i) kids += QStringLiteral("%1 0 R ").arg(3 + i);
    objects.append(QStringLiteral("<< /Type /Pages /Kids [%1] /Count %2 >>").arg(kids).arg(pageCount));
    for (int i = 0; i < pageCount; ++i) {
        objects.append(QStringLiteral("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
                                      "/Resources << /Font << /F1 %1 0 R >> >> /Contents %2 0 R >>")
                            .arg(fontId)
                            .arg(fontId + 1 + i));
    }
    objects.append(QStringLiteral("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"));
    for (const QString& page : pages) {
        const QByteArray content = QStringLiteral("BT /F1 18 Tf 40 320 Td (%1) Tj ET")
                                       .arg(escapePdfText(page))
                                       .toLatin1();
        objects.append(QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                           .arg(content.size())
                           .arg(QString::fromLatin1(content))
                           .toLatin1());
    }

    QByteArray out = QByteArray("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (int id = 0; id < objects.size(); ++id) {
        offsets.insert(id + 1, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(id + 1).toLatin1();
        out += objects.at(id).toLatin1();
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = objects.size() + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        // xref 里的偏移必须是定长十进制：QString::arg 的重载太多，
        // 这里用 number() + rightJustified() 写死格式，免得踩到 base 的坑。
        out += (QString::number(offsets.value(id, 0)).rightJustified(10, QLatin1Char('0')) +
                QStringLiteral(" 00000 n \n"))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 用 PDFium 读回某一页的文本层。
QString pageText(const QByteArray& bytes, int pageIndex) {
    const auto opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) return {};
    const auto page = opened.document.page(pageIndex);
    if (!page.has_value()) return {};
    const auto text = page->extractText();
    if (!text.has_value()) return {};
    return text->plainText();
}

ReplaceRule literalRule(const QString& find, const QString& replace) {
    ReplaceRule rule;
    rule.find = find;
    rule.replace = replace;
    return rule;
}

}  // namespace

class BatchReplaceTests : public QObject {
    Q_OBJECT

private slots:
    void singleRuleReplacesOneOccurrence();
    void singleRuleReplacesEveryOccurrence();
    void caseSensitivityControlsMatching();
    void wholeWordSkipsLongerWords();
    void regexRuleReplacesMatches();
    void regexReplacementExpandsCaptureGroup();
    void invalidRegexOnlyFailsItsOwnRule();
    void stopOnFirstErrorAbortsTheBatch();
    void rulesApplyInOrderOnPreviousResult();
    void onlySelectedPagesAreChanged();
    void noMatchLeavesDocumentUnchanged();
    void emptyFindIsReportedAsError();
    void roundTripThroughFixture();
};

#ifndef AIPDF_WITH_QPDF
// 没有 qpdf 就没法改内容流：这些用例全部依赖它。
void BatchReplaceTests::singleRuleReplacesOneOccurrence() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::singleRuleReplacesEveryOccurrence() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::caseSensitivityControlsMatching() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::wholeWordSkipsLongerWords() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::regexRuleReplacesMatches() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::regexReplacementExpandsCaptureGroup() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::invalidRegexOnlyFailsItsOwnRule() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::stopOnFirstErrorAbortsTheBatch() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::rulesApplyInOrderOnPreviousResult() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::onlySelectedPagesAreChanged() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::noMatchLeavesDocumentUnchanged() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::emptyFindIsReportedAsError() { QSKIP("本次构建未启用 qpdf"); }
void BatchReplaceTests::roundTripThroughFixture() { QSKIP("本次构建未启用 qpdf"); }
#else

// 一处替换：命中数是 1，文本层里旧字没了、新字在。
void BatchReplaceTests::singleRuleReplacesOneOccurrence() {
    const QByteArray source = buildTextPdf({QStringLiteral("hello world")});
    BatchReplaceResult result;
    QString error;
    const QByteArray output = aipdf::pdf::applyBatchReplace(
        source, {literalRule(QStringLiteral("world"), QStringLiteral("there"))}, {}, &result, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY(result.ok);
    QCOMPARE(result.replacements, 1);
    QCOMPARE(result.perRule.size(), 1);
    QCOMPARE(result.perRule.first().replacements, 1);
    QCOMPARE(pageText(output, 0), QStringLiteral("hello there"));
}

// 多处替换：一个文本操作数里命中三次，计数就是三次。
void BatchReplaceTests::singleRuleReplacesEveryOccurrence() {
    const QByteArray source = buildTextPdf({QStringLiteral("cat cat cat")});
    BatchReplaceResult result;
    const QByteArray output = aipdf::pdf::applyBatchReplace(
        source, {literalRule(QStringLiteral("cat"), QStringLiteral("dog"))}, {}, &result, nullptr);
    QVERIFY(!output.isEmpty());
    QCOMPARE(result.replacements, 3);
    QCOMPARE(pageText(output, 0), QStringLiteral("dog dog dog"));
}

// 大小写：敏感时只改同样大小写的，不敏感时连大写一起改。
void BatchReplaceTests::caseSensitivityControlsMatching() {
    const QByteArray source = buildTextPdf({QStringLiteral("Cat cat CAT")});

    ReplaceRule sensitiveRule = literalRule(QStringLiteral("cat"), QStringLiteral("dog"));
    sensitiveRule.caseSensitive = true;
    BatchReplaceResult sensitive;
    const QByteArray sensitiveOutput = aipdf::pdf::applyBatchReplace(
        source, {sensitiveRule}, {}, &sensitive, nullptr);
    QVERIFY(!sensitiveOutput.isEmpty());
    QCOMPARE(sensitive.replacements, 1);
    QCOMPARE(pageText(sensitiveOutput, 0), QStringLiteral("Cat dog CAT"));

    ReplaceRule insensitiveRule = literalRule(QStringLiteral("cat"), QStringLiteral("dog"));
    insensitiveRule.caseSensitive = false;
    BatchReplaceResult insensitive;
    const QByteArray insensitiveOutput = aipdf::pdf::applyBatchReplace(
        source, {insensitiveRule}, {}, &insensitive, nullptr);
    QVERIFY(!insensitiveOutput.isEmpty());
    QCOMPARE(insensitive.replacements, 3);
    QCOMPARE(pageText(insensitiveOutput, 0), QStringLiteral("dog dog dog"));
}

// 全字匹配：catalog、concat 里边的 cat 不能碰。
void BatchReplaceTests::wholeWordSkipsLongerWords() {
    const QByteArray source = buildTextPdf({QStringLiteral("cat catalog concat cat")});
    ReplaceRule rule = literalRule(QStringLiteral("cat"), QStringLiteral("dog"));
    rule.wholeWord = true;
    BatchReplaceResult result;
    const QByteArray output = aipdf::pdf::applyBatchReplace(source, {rule}, {}, &result, nullptr);
    QVERIFY(!output.isEmpty());
    QCOMPARE(result.replacements, 2);
    const QString text = pageText(output, 0);
    QVERIFY(text.contains(QStringLiteral("dog")));
    QVERIFY(text.contains(QStringLiteral("catalog")));
    QVERIFY(text.contains(QStringLiteral("concat")));
}

// 正则：查找内容按正则解释，命中的是模式而不是字面量。
void BatchReplaceTests::regexRuleReplacesMatches() {
    const QByteArray source = buildTextPdf({QStringLiteral("a1 a22 a333")});
    ReplaceRule rule;
    rule.find = QStringLiteral("a\\d+");
    rule.replace = QStringLiteral("X");
    rule.regex = true;
    BatchReplaceResult result;
    const QByteArray output = aipdf::pdf::applyBatchReplace(source, {rule}, {}, &result, nullptr);
    QVERIFY(!output.isEmpty());
    QCOMPARE(result.replacements, 3);
    QCOMPARE(pageText(output, 0), QStringLiteral("X X X"));
}

// 捕获组：$1 与 \1 都要能引用到匹配到的内容。
void BatchReplaceTests::regexReplacementExpandsCaptureGroup() {
    const QByteArray source = buildTextPdf({QStringLiteral("item 42")});

    ReplaceRule dollarRule;
    dollarRule.find = QStringLiteral("item (\\d+)");
    dollarRule.replace = QStringLiteral("part $1");
    dollarRule.regex = true;
    BatchReplaceResult result;
    const QByteArray output = aipdf::pdf::applyBatchReplace(source, {dollarRule}, {}, &result, nullptr);
    QVERIFY(!output.isEmpty());
    QCOMPARE(result.replacements, 1);
    QCOMPARE(pageText(output, 0), QStringLiteral("part 42"));

    ReplaceRule backslashRule;
    backslashRule.find = QStringLiteral("item (\\d+)");
    backslashRule.replace = QStringLiteral("part \\1");
    backslashRule.regex = true;
    BatchReplaceResult backslashResult;
    const QByteArray backslashOutput =
        aipdf::pdf::applyBatchReplace(source, {backslashRule}, {}, &backslashResult, nullptr);
    QVERIFY(!backslashOutput.isEmpty());
    QCOMPARE(backslashResult.replacements, 1);
    QCOMPARE(pageText(backslashOutput, 0), QStringLiteral("part 42"));
}

// 非法正则：只有这一条规则失败，后面的规则照常执行。
void BatchReplaceTests::invalidRegexOnlyFailsItsOwnRule() {
    const QByteArray source = buildTextPdf({QStringLiteral("cat")});
    ReplaceRule broken;
    broken.find = QStringLiteral("[unclosed");
    broken.regex = true;
    ReplaceRule good = literalRule(QStringLiteral("cat"), QStringLiteral("dog"));

    BatchReplaceResult result;
    QString error;
    const QByteArray output =
        aipdf::pdf::applyBatchReplace(source, {broken, good}, {}, &result, &error);
    QCOMPARE(result.perRule.size(), 2);
    QVERIFY(!result.perRule.at(0).ok);
    QVERIFY(!result.perRule.at(0).error.isEmpty());
    QVERIFY(result.perRule.at(1).ok);
    QCOMPARE(result.perRule.at(1).replacements, 1);
    // 整批不算成功，但别的规则的结果要留下。
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(!output.isEmpty());
    QCOMPARE(pageText(output, 0), QStringLiteral("dog"));
}

// stopOnFirstError：第一条规则出错就整批停下，不返回半成品。
void BatchReplaceTests::stopOnFirstErrorAbortsTheBatch() {
    const QByteArray source = buildTextPdf({QStringLiteral("cat")});
    ReplaceRule broken;
    broken.find = QStringLiteral("(unclosed");
    broken.regex = true;
    ReplaceRule good = literalRule(QStringLiteral("cat"), QStringLiteral("dog"));
    BatchReplaceOptions options;
    options.stopOnFirstError = true;

    BatchReplaceResult result;
    QString error;
    const QByteArray output =
        aipdf::pdf::applyBatchReplace(source, {broken, good}, options, &result, &error);
    QVERIFY(output.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(!result.ok);
    QCOMPARE(result.perRule.size(), 1);
    QVERIFY(!result.perRule.first().ok);
}

// 顺序叠加：第一条的结果是第二条的输入。
void BatchReplaceTests::rulesApplyInOrderOnPreviousResult() {
    const QByteArray source = buildTextPdf({QStringLiteral("alpha")});
    BatchReplaceResult result;
    const QByteArray output = aipdf::pdf::applyBatchReplace(
        source,
        {literalRule(QStringLiteral("alpha"), QStringLiteral("beta")),
         literalRule(QStringLiteral("beta"), QStringLiteral("gamma"))},
        {}, &result, nullptr);
    QVERIFY(!output.isEmpty());
    QCOMPARE(result.perRule.size(), 2);
    QCOMPARE(result.perRule.at(0).replacements, 1);
    QCOMPARE(result.perRule.at(1).replacements, 1);
    QCOMPARE(result.replacements, 2);
    QCOMPARE(pageText(output, 0), QStringLiteral("gamma"));
}

// 页范围：只改第 1 页，第 2 页原样不动。
void BatchReplaceTests::onlySelectedPagesAreChanged() {
    const QByteArray source = buildTextPdf({QStringLiteral("cat first"), QStringLiteral("cat second")});
    BatchReplaceOptions options;
    options.pageIndexes = {0};
    BatchReplaceResult result;
    const QByteArray output = aipdf::pdf::applyBatchReplace(
        source, {literalRule(QStringLiteral("cat"), QStringLiteral("dog"))}, options, &result, nullptr);
    QVERIFY(!output.isEmpty());
    QCOMPARE(result.replacements, 1);
    QCOMPARE(pageText(output, 0), QStringLiteral("dog first"));
    QCOMPARE(pageText(output, 1), QStringLiteral("cat second"));
}

// 一处都没找到：计数是 0，字节必须原样返回。
void BatchReplaceTests::noMatchLeavesDocumentUnchanged() {
    const QByteArray source = buildTextPdf({QStringLiteral("hello world")});
    BatchReplaceResult result;
    const QByteArray output = aipdf::pdf::applyBatchReplace(
        source, {literalRule(QStringLiteral("nothing"), QStringLiteral("x"))}, {}, &result, nullptr);
    QVERIFY(result.ok);
    QCOMPARE(result.replacements, 0);
    QCOMPARE(result.perRule.first().replacements, 0);
    QCOMPARE(output, source);
}

// 查找内容为空：这条规则不合法，要报出来。
void BatchReplaceTests::emptyFindIsReportedAsError() {
    const QByteArray source = buildTextPdf({QStringLiteral("hello")});
    BatchReplaceResult result;
    QString error;
    const QByteArray output =
        aipdf::pdf::applyBatchReplace(source, {literalRule(QString(), QStringLiteral("x"))}, {}, &result, &error);
    QCOMPARE(result.perRule.size(), 1);
    QVERIFY(!result.perRule.first().ok);
    QVERIFY(!result.perRule.first().error.isEmpty());
    QVERIFY(!result.ok);
    QVERIFY(!error.isEmpty());
    // 规则不合法不等于文件坏了：内容仍然是原来的。
    QVERIFY(!output.isEmpty());
    QCOMPARE(pageText(output, 0), QStringLiteral("hello"));
}

// 落盘往返：用真实夹具改一遍，再用 PDFium 读回文本层确认结果。
void BatchReplaceTests::roundTripThroughFixture() {
    QFile file(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QByteArray source = file.readAll();
    QVERIFY(!source.isEmpty());

    // 夹具里的文字不写死：先读出来，再从里面挑一个词去替换。
    const QString firstPageBefore = pageText(source, 0);
    const QString secondPageBefore = pageText(source, 1);
    QVERIFY2(!firstPageBefore.isEmpty(), "夹具第一页没有文本层");
    QString target;
    for (const QString& part : firstPageBefore.split(QRegularExpression(QStringLiteral("[^A-Za-z]+")))) {
        if (part.size() >= 4) {
            target = part;
            break;
        }
    }
    QVERIFY2(!target.isEmpty(), "夹具第一页里找不到可替换的词");

    // 期望的替换次数 = 这个词在整篇里出现了几次（自己数，不写死夹具内容）。
    int expected = 0;
    for (const QString& page : {firstPageBefore, secondPageBefore}) {
        int index = page.indexOf(target);
        while (index >= 0) {
            ++expected;
            index = page.indexOf(target, index + 1);
        }
    }
    QVERIFY(expected > 0);

    BatchReplaceResult result;
    QString error;
    const QByteArray output = aipdf::pdf::applyBatchReplace(
        source, {literalRule(target, QStringLiteral("ZZMARKER"))}, {}, &result, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY(result.ok);
    QCOMPARE(result.replacements, expected);

    const QString firstPageAfter = pageText(output, 0);
    QVERIFY2(firstPageAfter.contains(QStringLiteral("ZZMARKER")), qPrintable(firstPageAfter));
    QVERIFY2(!firstPageAfter.contains(target), qPrintable(firstPageAfter));
    // 第二页原来没有这个词就不该被动过；原来有就必须换掉。
    const QString secondPageAfter = pageText(output, 1);
    if (secondPageBefore.contains(target)) {
        QVERIFY(secondPageAfter.contains(QStringLiteral("ZZMARKER")));
        QVERIFY(!secondPageAfter.contains(target));
    } else {
        QCOMPARE(secondPageAfter, secondPageBefore);
    }
}

#endif  // AIPDF_WITH_QPDF

QTEST_MAIN(BatchReplaceTests)
#include "BatchReplaceTests.moc"
