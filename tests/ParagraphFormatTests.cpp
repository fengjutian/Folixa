#include "pdf/content/ParagraphFormat.h"

#include <QTest>

using aipdf::pdf::buildParagraphContentStream;
using aipdf::pdf::layoutParagraph;
using aipdf::pdf::LineSpacingMode;
using aipdf::pdf::ParagraphFormat;
using aipdf::pdf::ParagraphPlan;
using aipdf::pdf::TextAlignment;

namespace {

// 一段足够长、每行都能断在词上的文字。
QString longText() {
    return QStringLiteral("The quick brown fox jumps over the lazy dog and keeps running "
                          "until the very end of the sentence");
}

// 排版框：宽 300、高 200，y 是下边（PDF user space 的约定）。
QRectF box() {
    return QRectF(0.0, 0.0, 300.0, 200.0);
}

int spaceCount(const QString& text) {
    int count = 0;
    for (const QChar& ch : text) {
        if (ch == QLatin1Char(' ')) ++count;
    }
    return count;
}

}  // namespace

// 这里用 QTEST_MAIN 而不是 QTEST_GUILESS_MAIN。
//
// 原因：pdf::TextReflow 用 QFontMetricsF 量文字宽度，而 Qt 的字体库必须在
// QGuiApplication 之后才能访问——QTEST_GUILESS_MAIN 建的是 QCoreApplication，
// 会在 QFontDatabase 处直接 QFATAL（整个测试进程 abort）。
// 实测证据（build/w20-check/probe/font_probe.cpp）：
//   QWARN  QGuiApplication::font(): no QGuiApplication instance and no application font set.
//   QFATAL QFontDatabase: Must construct a QGuiApplication before accessing QFontDatabase
class ParagraphFormatTests : public QObject {
    Q_OBJECT

private slots:
    // ---- 负例 ----

    void emptyTextFails() {
        const ParagraphPlan plan = layoutParagraph(QString(), box(), ParagraphFormat());
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains(QStringLiteral("文字为空")));
        QVERIFY(plan.lines.isEmpty());
    }

    void nonPositiveWidthFails() {
        const ParagraphPlan plan = layoutParagraph(QStringLiteral("Hello"), QRectF(0, 0, 0, 100),
                                                   ParagraphFormat());
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains(QStringLiteral("宽度")));
    }

    void nonPositiveHeightFails() {
        const ParagraphPlan plan = layoutParagraph(QStringLiteral("Hello"), QRectF(0, 0, 300, 0),
                                                   ParagraphFormat());
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains(QStringLiteral("高度")));
    }

    void nonPositiveFontSizeFails() {
        ParagraphFormat format;
        format.fontSize = 0.0;
        const ParagraphPlan plan = layoutParagraph(QStringLiteral("Hello"), box(), format);
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains(QStringLiteral("字号")));
    }

    void firstLineIndentWiderThanBoxFails() {
        ParagraphFormat format;
        format.firstLineIndent = 320.0;
        const ParagraphPlan plan = layoutParagraph(QStringLiteral("Hello"), box(), format);
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains(QStringLiteral("首行缩进")));
    }

    void indentsEatingTheWholeWidthFail() {
        ParagraphFormat format;
        format.leftIndent = 200.0;
        format.rightIndent = 150.0;
        const ParagraphPlan plan = layoutParagraph(QStringLiteral("Hello"), box(), format);
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains(QStringLiteral("缩进")));
    }

    void negativeIndentFails() {
        ParagraphFormat format;
        format.leftIndent = -10.0;
        const ParagraphPlan plan = layoutParagraph(QStringLiteral("Hello"), box(), format);
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains(QStringLiteral("负数")));
    }

    // ---- 对齐 ----

    void leftAlignmentStartsAtContentLeft() {
        ParagraphFormat format;
        format.alignment = TextAlignment::Left;
        format.leftIndent = 25.0;
        const ParagraphPlan plan = layoutParagraph(QStringLiteral("Hello"), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QCOMPARE(plan.lines.size(), 1);
        QCOMPARE(plan.lines.first().x, 25.0);
        QVERIFY(plan.lines.first().width > 0.0);
        QVERIFY(plan.lines.first().width < 275.0);
    }

    void centerAlignmentCentersEachLine() {
        ParagraphFormat format;
        format.alignment = TextAlignment::Center;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.lines.size() >= 2);
        for (const aipdf::pdf::ParagraphLine& line : plan.lines) {
            const double expected = (box().width() - line.width) * 0.5;
            QVERIFY(qAbs(line.x - expected) < 0.01);
        }
    }

    void rightAlignmentEndsAtContentRight() {
        ParagraphFormat format;
        format.alignment = TextAlignment::Right;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.lines.size() >= 2);
        for (const aipdf::pdf::ParagraphLine& line : plan.lines) {
            const double expected = box().width() - line.width;
            QVERIFY(qAbs(line.x - expected) < 0.01);
        }
    }

    void justifyFillsEveryLineButTheLast() {
        ParagraphFormat format;
        format.alignment = TextAlignment::Justify;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.lines.size() >= 2);
        for (int i = 0; i < plan.lines.size() - 1; ++i) {
            const aipdf::pdf::ParagraphLine& line = plan.lines.at(i);
            // 除最后一行外，每一行都撑满可用宽度。
            QVERIFY2(qAbs(line.width - box().width()) < 0.01,
                     qPrintable(QStringLiteral("第 %1 行宽度 %2").arg(i).arg(line.width)));
            QVERIFY(line.justified);
        }
        const aipdf::pdf::ParagraphLine& last = plan.lines.last();
        QVERIFY2(!last.justified, "最后一行不应被拉伸");
        QVERIFY2(last.width < box().width() - 0.01,
                 qPrintable(QStringLiteral("最后一行宽度 %1").arg(last.width)));
    }

    void justifySpreadsTheExtraWidthOverSpaces() {
        ParagraphFormat format;
        format.alignment = TextAlignment::Justify;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        const aipdf::pdf::ParagraphLine& line = plan.lines.first();
        const int spaces = spaceCount(line.text);
        QVERIFY(spaces > 0);
        QVERIFY(line.spaceExtra > 0.0);
        // 多出来的宽度正好分摊给行内的空格。
        const double distributed = line.spaceExtra * spaces;
        const double expected = box().width() - line.naturalWidth;
        QVERIFY(qAbs(distributed - expected) < 0.01);
    }

    // ---- 缩进 ----

    void firstLineIndentOnlyAffectsTheFirstLine() {
        ParagraphFormat format;
        format.firstLineIndent = 40.0;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.lines.size() >= 2);
        QCOMPARE(plan.lines.first().x, 40.0);
        QCOMPARE(plan.lines.at(1).x, 0.0);
    }

    void firstLineIndentAlsoShortensTheFirstLine() {
        ParagraphFormat format;
        format.firstLineIndent = 120.0;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.lines.size() >= 2);
        // 第一行是按「内容宽度 - 首行缩进」断的，所以它比后面的行短。
        QVERIFY(plan.lines.first().width <= box().width() - 120.0 + 0.01);
    }

    void leftAndRightIndentsApplyToEveryLine() {
        ParagraphFormat format;
        format.leftIndent = 20.0;
        format.rightIndent = 30.0;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.lines.size() >= 2);
        const double right = box().right() - 30.0;
        for (const aipdf::pdf::ParagraphLine& line : plan.lines) {
            QVERIFY(line.x >= 20.0 - 0.01);
            QVERIFY(line.x + line.width <= right + 0.01);
        }
    }

    // ---- 行距 ----

    void multipleLineSpacingUsesTheFontSize() {
        ParagraphFormat format;
        format.lineSpacingMode = LineSpacingMode::Multiple;
        // 倍数要明显大于字体自己的行高，这时生效的就是「字号 × 倍数」。
        format.lineSpacing = 2.0;
        format.fontSize = 12.0;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.lines.size() >= 2);
        const double step = plan.lines.at(1).baseline.y() - plan.lines.first().baseline.y();
        QVERIFY2(qAbs(step - 24.0) < 0.01,
                 qPrintable(QStringLiteral("实际行距 %1").arg(step)));
    }

    void lineSpacingNeverGoesBelowTheFontHeight() {
        // 倍数很小的时候，行距退回字体自己的行高：行与行不能重叠。
        ParagraphFormat small;
        small.lineSpacingMode = LineSpacingMode::Multiple;
        small.lineSpacing = 0.5;
        small.fontSize = 12.0;
        ParagraphFormat tinier = small;
        tinier.lineSpacing = 0.1;
        const ParagraphPlan first = layoutParagraph(longText(), box(), small);
        const ParagraphPlan second = layoutParagraph(longText(), box(), tinier);
        QVERIFY2(first.ok(), qPrintable(first.error));
        QVERIFY2(second.ok(), qPrintable(second.error));
        QVERIFY(first.lines.size() >= 2);
        QVERIFY(second.lines.size() >= 2);
        const double firstStep = first.lines.at(1).baseline.y() - first.lines.first().baseline.y();
        const double secondStep = second.lines.at(1).baseline.y() - second.lines.first().baseline.y();
        QVERIFY2(qAbs(firstStep - secondStep) < 0.01,
                 qPrintable(QStringLiteral("行距 %1 与 %2 不一致").arg(firstStep).arg(secondStep)));
        QVERIFY2(firstStep > 12.0 * 0.5,
                 qPrintable(QStringLiteral("行距 %1 小于字号的 0.5 倍").arg(firstStep)));
    }

    void fixedLineSpacingUsesPoints() {
        ParagraphFormat format;
        format.lineSpacingMode = LineSpacingMode::Fixed;
        format.lineSpacing = 24.0;
        format.fontSize = 12.0;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.lines.size() >= 2);
        const double step = plan.lines.at(1).baseline.y() - plan.lines.first().baseline.y();
        QVERIFY(qAbs(step - 24.0) < 0.01);
    }

    // ---- 断行与溢出 ----

    void wrapsOnWordBoundaries() {
        const ParagraphPlan plan = layoutParagraph(longText(), box(), ParagraphFormat());
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.lines.size() >= 3);
        // 断行落在词上：每一行要么以空格结尾，要么是一个完整的词。
        for (const aipdf::pdf::ParagraphLine& line : plan.lines) {
            const QString trimmed = line.text.trimmed();
            QVERIFY(!trimmed.isEmpty());
            QVERIFY2(trimmed.contains(QLatin1Char(' ')) || trimmed == QStringLiteral("running")
                         || trimmed == QStringLiteral("until")
                         || trimmed == QStringLiteral("the")
                         || trimmed == QStringLiteral("very")
                         || trimmed == QStringLiteral("end")
                         || trimmed == QStringLiteral("of")
                         || trimmed == QStringLiteral("sentence"),
                     qPrintable(QStringLiteral("断行不在词边界：%1").arg(line.text)));
        }
    }

    void overflowIsReportedWhenTheBoxIsTooShort() {
        const ParagraphPlan plan = layoutParagraph(longText(), QRectF(0, 0, 300, 20), ParagraphFormat());
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.overflow);
        QVERIFY(!plan.remainingText.isEmpty());
    }

    void boxTooShortForOneLineFails() {
        const ParagraphPlan plan = layoutParagraph(longText(), QRectF(0, 0, 300, 2), ParagraphFormat());
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains(QStringLiteral("放不下")));
    }

    // ---- 内容流 ----

    void contentStreamUsesAbsoluteBaselines() {
        const ParagraphPlan plan = layoutParagraph(QStringLiteral("Hello"), box(), ParagraphFormat());
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        const QByteArray stream = buildParagraphContentStream(plan, QStringLiteral("F1"));
        QVERIFY(stream.startsWith("BT"));
        QVERIFY(stream.endsWith("ET\n"));
        QVERIFY(stream.contains("/F1 12 Tf"));
        QVERIFY(stream.contains("1 0 0 1 0 "));
        QVERIFY(stream.contains("(Hello) Tj"));
    }

    void contentStreamCarriesTheJustificationSpacing() {
        ParagraphFormat format;
        format.alignment = TextAlignment::Justify;
        const ParagraphPlan plan = layoutParagraph(longText(), box(), format);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        const QByteArray stream = buildParagraphContentStream(plan, QStringLiteral("F2"));
        QVERIFY(stream.contains("/F2 12 Tf"));
        QVERIFY2(stream.contains(" Tw"), "justify 的行距调整应当写进内容流");
    }

    void contentStreamIsEmptyForAFailedPlan() {
        const ParagraphPlan plan = layoutParagraph(QString(), box(), ParagraphFormat());
        QVERIFY(!plan.ok());
        QVERIFY(buildParagraphContentStream(plan).isEmpty());
    }
};

QTEST_MAIN(ParagraphFormatTests)
#include "ParagraphFormatTests.moc"
