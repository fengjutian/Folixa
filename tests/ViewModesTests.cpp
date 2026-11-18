#include "app/editor/ViewModes.h"

#include "pdf/kernel/PdfiumAdapter.h"

#include <QTest>

using aipdf::app::viewmodes::autoScrollState;
using aipdf::app::viewmodes::AutoScrollState;
using aipdf::app::viewmodes::formatRulerLabel;
using aipdf::app::viewmodes::mirrorRect;
using aipdf::app::viewmodes::mirrorX;
using aipdf::app::viewmodes::pageAsPlainText;
using aipdf::app::viewmodes::PageTransition;
using aipdf::app::viewmodes::PageTransitionFrame;
using aipdf::app::viewmodes::pointsToUnit;
using aipdf::app::viewmodes::rulerTicks;
using aipdf::app::viewmodes::RulerTick;
using aipdf::app::viewmodes::RulerUnit;
using aipdf::app::viewmodes::unitToPoints;
using aipdf::pdf::TextPage;

#ifndef AIPDF_PDF_FIXTURES_DIR
#define AIPDF_PDF_FIXTURES_DIR "fixtures"
#endif

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

// 人造一个字符：PDF user space，y 是下边（y 向上），与 TextChar::box 同约定。
aipdf::pdf::TextChar makeChar(const QString& text,
                               double left,
                               double bottom,
                               double width,
                               double height) {
    aipdf::pdf::TextChar character;
    character.text = text;
    character.box = QRectF(left, bottom, width, height);
    return character;
}

// 在页面上加一行字符：行内字符按 advance 挨个排开。
void appendLine(TextPage& page,
                const QString& text,
                double left,
                double bottom,
                double height,
                double advance) {
    double x = left;
    for (const QChar& ch : text) {
        page.chars.append(makeChar(QString(ch), x, bottom, advance * 0.8, height));
        x += advance;
    }
}

QVector<RulerTick> majorTicks(const QVector<RulerTick>& ticks) {
    QVector<RulerTick> majors;
    for (const RulerTick& tick : ticks) {
        if (tick.major) {
            majors.append(tick);
        }
    }
    return majors;
}

}  // namespace

class ViewModesTests final : public QObject {
    Q_OBJECT

private slots:
    void rulerPointsProducesMajorTicks();
    void rulerMillimetersUseTenMillimeterStep();
    void rulerInchesUseHalfInchStep();
    void unitConversionsRoundTrip();
    void minorTicksDivideMajorStepIntoFifths();
    void rulerRejectsInvalidInput();
    void autoScrollIsIdleInViewportCenter();
    void autoScrollMovesDownAtBottomEdge();
    void autoScrollMovesUpAtTopEdge();
    void autoScrollNeverExceedsMaxVelocity();
    void autoScrollRejectsInvalidInput();
    void pageTransitionFadeEndsTransparent();
    void pageTransitionSlideMovesOldPage();
    void pageTransitionNoneIsIdentity();
    void pageTransitionClampsOutOfRangeProgress();
    void plainTextGroupsSyntheticLines();
    void plainTextFixtureHasThreeLines();
    void plainTextKeepsInnerSpaces();
    void plainTextEmptyPageReturnsEmptyString();
    void mirrorXMirrorsAroundPageCenter();
    void mirrorRectSwapsEdges();
    void mirrorRejectsInvalidPageWidth();
    void immersivePlanHidesEverythingByDefault();
    void immersivePlanKeepsDocksInFocusMode();
    void panDragStartsInactive();
    void panScrollFollowsPointer();
    void panScrollClampsToRange();
};

// ---------------------------------------------------------------- 标尺 ----

// 点单位：10pt 的标尺上，主刻度在 0 与 10，标签就是 "0" 与 "10"。
void ViewModesTests::rulerPointsProducesMajorTicks() {
    const QVector<RulerTick> ticks = rulerTicks(0.0, 10.0, RulerUnit::Points);
    const QVector<RulerTick> majors = majorTicks(ticks);
    QCOMPARE(majors.size(), 2);
    QCOMPARE(majors.at(0).position, 0.0);
    QCOMPARE(majors.at(1).position, 10.0);
    QCOMPARE(majors.at(0).label, QStringLiteral("0"));
    QCOMPARE(majors.at(1).label, QStringLiteral("10"));
}

// 毫米单位：默认主刻度 10mm，标签带 "mm"，1mm = 2.8346… 点。
void ViewModesTests::rulerMillimetersUseTenMillimeterStep() {
    const double tenMillimeters = unitToPoints(10.0, RulerUnit::Millimeters);
    const QVector<RulerTick> ticks = rulerTicks(0.0, tenMillimeters, RulerUnit::Millimeters);
    const QVector<RulerTick> majors = majorTicks(ticks);
    QCOMPARE(majors.size(), 2);
    QCOMPARE(majors.at(0).label, QStringLiteral("0 mm"));
    QCOMPARE(majors.at(1).label, QStringLiteral("10 mm"));
    QVERIFY(qAbs(majors.at(1).position - tenMillimeters) < 1e-6);

    // 换算：1 毫米 = 72/25.4 点 ≈ 2.8346 点。
    QVERIFY(qAbs(unitToPoints(1.0, RulerUnit::Millimeters) - 2.834645669) < 1e-6);
    QVERIFY(qAbs(pointsToUnit(unitToPoints(1.0, RulerUnit::Millimeters),
                              RulerUnit::Millimeters) - 1.0) < 1e-9);
}

// 英寸单位：默认主刻度半英寸（36 点），标签用 ASCII 的 " in" 后缀。
void ViewModesTests::rulerInchesUseHalfInchStep() {
    const QVector<RulerTick> ticks = rulerTicks(0.0, 36.0, RulerUnit::Inches);
    const QVector<RulerTick> majors = majorTicks(ticks);
    QCOMPARE(majors.size(), 2);
    QCOMPARE(majors.at(0).label, QStringLiteral("0 in"));
    QCOMPARE(majors.at(1).label, QStringLiteral("0.5 in"));
    QVERIFY(majors.at(1).label.contains(QStringLiteral("in")));
}

// 单位换算往返：points → 单位 → points 回到原值。
void ViewModesTests::unitConversionsRoundTrip() {
    const double points = 123.456;
    for (RulerUnit unit : {RulerUnit::Points, RulerUnit::Millimeters, RulerUnit::Inches}) {
        const double there = pointsToUnit(points, unit);
        const double back = unitToPoints(there, unit);
        QVERIFY2(qAbs(back - points) < 1e-9, "单位换算往返必须回到原值");
    }
    // 刻度文字也跟着换算走。
    QCOMPARE(formatRulerLabel(12.5, RulerUnit::Millimeters), QStringLiteral("12.5 mm"));
    QCOMPARE(formatRulerLabel(0.5, RulerUnit::Inches), QStringLiteral("0.5 in"));
}

// 次刻度：毫米单位下，一个主刻度（10mm）之间切成 5 段。
void ViewModesTests::minorTicksDivideMajorStepIntoFifths() {
    const double tenMillimeters = unitToPoints(10.0, RulerUnit::Millimeters);
    const QVector<RulerTick> ticks = rulerTicks(0.0, tenMillimeters, RulerUnit::Millimeters);
    QCOMPARE(ticks.size(), 6);  // 2 个主刻度 + 4 个次刻度

    QVector<double> minorPositions;
    for (const RulerTick& tick : ticks) {
        if (!tick.major) {
            minorPositions.append(tick.position);
        }
    }
    QCOMPARE(minorPositions.size(), 4);
    const double minorSpan = minorPositions.at(1) - minorPositions.at(0);
    QVERIFY(qAbs(minorSpan - tenMillimeters / 5.0) < 1e-6);
}

// 非法输入：长度非正 → 空 vector（不给假刻度）。
void ViewModesTests::rulerRejectsInvalidInput() {
    QVERIFY(rulerTicks(0.0, 0.0, RulerUnit::Points).isEmpty());
    QVERIFY(rulerTicks(0.0, -10.0, RulerUnit::Points).isEmpty());
}

// ---------------------------------------------------------- 自动滚动 ----

// 指针在视口中间 → 不滚动。
void ViewModesTests::autoScrollIsIdleInViewportCenter() {
    const AutoScrollState state = autoScrollState(300.0, 600.0, 40.0, 800.0);
    QVERIFY(!state.active);
    QCOMPARE(state.velocity, 0.0);
}

// 指针贴着下边缘 → 向下滚（速度为正）。
void ViewModesTests::autoScrollMovesDownAtBottomEdge() {
    const AutoScrollState state = autoScrollState(598.0, 600.0, 40.0, 800.0);
    QVERIFY(state.active);
    QVERIFY(state.velocity > 0.0);
}

// 指针贴着上边缘 → 向上滚（速度为负）。
void ViewModesTests::autoScrollMovesUpAtTopEdge() {
    const AutoScrollState state = autoScrollState(2.0, 600.0, 40.0, 800.0);
    QVERIFY(state.active);
    QVERIFY(state.velocity < 0.0);
}

// 速度不超过给定的上限（指针在视口外也一樣）。
void ViewModesTests::autoScrollNeverExceedsMaxVelocity() {
    const double maxVelocity = 800.0;
    const AutoScrollState below = autoScrollState(600.0, 600.0, 40.0, maxVelocity);
    QVERIFY(qAbs(below.velocity) <= maxVelocity + 1e-9);
    const AutoScrollState above = autoScrollState(-50.0, 600.0, 40.0, maxVelocity);
    QVERIFY(qAbs(above.velocity) <= maxVelocity + 1e-9);
}

// 参数非法 → 不滚动。
void ViewModesTests::autoScrollRejectsInvalidInput() {
    QVERIFY(!autoScrollState(300.0, 0.0, 40.0, 800.0).active);
    QVERIFY(!autoScrollState(300.0, 600.0, 0.0, 800.0).active);
    QVERIFY(!autoScrollState(300.0, 600.0, 40.0, 0.0).active);
    QCOMPARE(autoScrollState(300.0, 0.0, 40.0, 800.0).velocity, 0.0);
}

// ---------------------------------------------------------- 页面过渡 ----

// 淡入：旧页从完全不透明过渡到全透明。
void ViewModesTests::pageTransitionFadeEndsTransparent() {
    const PageTransitionFrame start = pageTransitionFrame(PageTransition::Fade, 0.0);
    QCOMPARE(start.opacity, 1.0);
    QCOMPARE(start.offsetX, 0.0);

    const PageTransitionFrame end = pageTransitionFrame(PageTransition::Fade, 1.0);
    QCOMPARE(end.opacity, 0.0);
    QCOMPARE(end.offsetX, 0.0);
}

// 滑动：旧页从原位滑到一整页宽的位置（方向由 SlideLeft/SlideRight 决定）。
void ViewModesTests::pageTransitionSlideMovesOldPage() {
    QCOMPARE(pageTransitionFrame(PageTransition::SlideLeft, 0.0).offsetX, 0.0);
    QCOMPARE(pageTransitionFrame(PageTransition::SlideLeft, 1.0).offsetX, -1.0);
    QCOMPARE(pageTransitionFrame(PageTransition::SlideRight, 0.0).offsetX, 0.0);
    QCOMPARE(pageTransitionFrame(PageTransition::SlideRight, 1.0).offsetX, 1.0);
    // 滑动的过程中旧页仍然不透明。
    QCOMPARE(pageTransitionFrame(PageTransition::SlideLeft, 0.5).opacity, 1.0);
}

// 无过渡：不透明、无偏移。
void ViewModesTests::pageTransitionNoneIsIdentity() {
    for (double t : {0.0, 0.5, 1.0}) {
        const PageTransitionFrame frame = pageTransitionFrame(PageTransition::None, t);
        QCOMPARE(frame.opacity, 1.0);
        QCOMPARE(frame.offsetX, 0.0);
    }
}

// 进度越界要夹住，不能算出花屏的值。
void ViewModesTests::pageTransitionClampsOutOfRangeProgress() {
    QCOMPARE(pageTransitionFrame(PageTransition::Fade, -0.5).opacity,
             pageTransitionFrame(PageTransition::Fade, 0.0).opacity);
    QCOMPARE(pageTransitionFrame(PageTransition::Fade, 2.0).opacity,
             pageTransitionFrame(PageTransition::Fade, 1.0).opacity);
    QCOMPARE(pageTransitionFrame(PageTransition::SlideLeft, -1.0).offsetX, 0.0);
    QCOMPARE(pageTransitionFrame(PageTransition::SlideLeft, 5.0).offsetX, -1.0);
}

// ------------------------------------------------------- 纯文本查看器 ----

// 人造三行 → 纯文本里就是三行，顺序从上到下。
void ViewModesTests::plainTextGroupsSyntheticLines() {
    TextPage page;
    appendLine(page, QStringLiteral("FIRST LINE"), 50, 700, 12, 10);
    appendLine(page, QStringLiteral("SECOND LINE"), 50, 600, 12, 10);
    appendLine(page, QStringLiteral("THIRD LINE"), 50, 500, 12, 10);

    const QStringList lines = pageAsPlainText(page).split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0), QStringLiteral("FIRST LINE"));
    QCOMPARE(lines.at(1), QStringLiteral("SECOND LINE"));
    QCOMPARE(lines.at(2), QStringLiteral("THIRD LINE"));
}

// 真实夹具：text.pdf 第 1 页是 3 行，不是 1 行；文字对得上。
void ViewModesTests::plainTextFixtureHasThreeLines() {
    const auto opened = aipdf::pdf::openDocument(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());

    const QString plain = pageAsPlainText(*text);
    const QStringList lines = plain.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);
    QVERIFY(lines.at(0).contains(QStringLiteral("Folixa")));
    QVERIFY(lines.at(1).contains(QStringLiteral("Full text search")));
    QVERIFY(lines.at(2).contains(QStringLiteral("quick brown fox")));
}

// 词内的空格必须保留。
void ViewModesTests::plainTextKeepsInnerSpaces() {
    const auto opened = aipdf::pdf::openDocument(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());

    const QString plain = pageAsPlainText(*text);
    QVERIFY(plain.contains(QStringLiteral("Full text search")));
    QVERIFY(!plain.contains(QStringLiteral("Fulltextsearch")));
    QVERIFY(plain.contains(QStringLiteral("jumps over the lazy dog")));
}

// 空页 / 空字符 → 空串，不崩。
void ViewModesTests::plainTextEmptyPageReturnsEmptyString() {
    const TextPage empty;
    QVERIFY(pageAsPlainText(empty).isEmpty());

    TextPage blank;
    blank.chars.append(makeChar(QString(), 10, 700, 8, 12));
    blank.chars.append(makeChar(QStringLiteral("A"), 10, 700, 0, 12));
    QVERIFY(pageAsPlainText(blank).isEmpty());
}

// ---------------------------------------------------------- 逆序阅读 ----

// 镜像：左右对调，中点不动。
void ViewModesTests::mirrorXMirrorsAroundPageCenter() {
    QCOMPARE(mirrorX(0.0, 612.0), 612.0);
    QCOMPARE(mirrorX(612.0, 612.0), 0.0);
    QCOMPARE(mirrorX(306.0, 612.0), 306.0);
}

// 镜像矩形：左右边互换，宽度与高度不变。
void ViewModesTests::mirrorRectSwapsEdges() {
    const QRectF rect(100.0, 50.0, 200.0, 40.0);
    const QRectF mirrored = mirrorRect(rect, 612.0);
    QCOMPARE(mirrored.left(), 312.0);
    QCOMPARE(mirrored.right(), 512.0);
    QCOMPARE(mirrored.width(), rect.width());
    QCOMPARE(mirrored.height(), rect.height());
    QCOMPARE(mirrored.top(), rect.top());
}

// 页面宽度非法 → 恒等变换。
void ViewModesTests::mirrorRejectsInvalidPageWidth() {
    QCOMPARE(mirrorX(123.0, 0.0), 123.0);
    const QRectF rect(10.0, 20.0, 30.0, 40.0);
    QCOMPARE(mirrorRect(rect, -5.0), rect);
}

// ------------------------------------------------------ 沉浸阅读与平移 ----

void ViewModesTests::immersivePlanHidesEverythingByDefault() {
    const auto plan = aipdf::app::viewmodes::immersiveReadingPlan(false);
    QVERIFY(plan.hideRibbon);
    QVERIFY(plan.hideTabs);
    QVERIFY(plan.hideStatusBar);
    QVERIFY(plan.hideDocks);
    QVERIFY(plan.fullScreen);
    QVERIFY(plan.autoHideCursor);
}

void ViewModesTests::immersivePlanKeepsDocksInFocusMode() {
    // 专注模式里面板已经藏着了：不用再藏一次，但也别把它放出来。
    const auto plan = aipdf::app::viewmodes::immersiveReadingPlan(true);
    QVERIFY(!plan.hideDocks);
    // 全屏与光标自动隐藏照旧。
    QVERIFY(plan.fullScreen);
    QVERIFY(plan.autoHideCursor);
    QVERIFY(plan.hideRibbon);
}

void ViewModesTests::panDragStartsInactive() {
    const auto state = aipdf::app::viewmodes::beginPanDrag(QPoint(5, 7), 100, 200);
    QVERIFY(state.active);
    QCOMPARE(state.anchor, QPoint(5, 7));
    QCOMPARE(state.startScrollX, 100);
    QCOMPARE(state.startScrollY, 200);
}

void ViewModesTests::panScrollFollowsPointer() {
    // 抓住内容往右拖 40 像素 → 滚动位置往回走 40。
    QCOMPARE(aipdf::app::viewmodes::panScrollValue(300, 100, 140, 1000), 260);
    // 往左拖 → 滚动位置往前走。
    QCOMPARE(aipdf::app::viewmodes::panScrollValue(300, 100, 60, 1000), 340);
}

void ViewModesTests::panScrollClampsToRange() {
    // 拖过头：夹到边界，不要越界。
    QCOMPARE(aipdf::app::viewmodes::panScrollValue(10, 100, 300, 500), 0);
    QCOMPARE(aipdf::app::viewmodes::panScrollValue(490, 100, 0, 500), 500);
    // 范围非法（没有可滚的内容）时只能是 0。
    QCOMPARE(aipdf::app::viewmodes::panScrollValue(100, 0, 50, 0), 0);
}

QTEST_GUILESS_MAIN(ViewModesTests)
#include "ViewModesTests.moc"