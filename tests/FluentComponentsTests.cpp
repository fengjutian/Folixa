#include "app/ui/FluentTokens.h"
#include "app/ui/components/FluentControls.h"

#include <QAbstractItemView>
#include <QTest>

using namespace aipdf::app;

class FluentComponentsTests : public QObject {
    Q_OBJECT

private slots:
    void tokenMetricsMatchDocumentedScale();
    void semanticColorsSupportBothThemes();
    void buttonExposesAppearanceAndSize();
    void lineEditUsesStandardControlHeight();
    void comboOwnsAReusablePopup();
    void searchBoxProvidesSearchAndClearActions();
    void toolButtonUsesSquareMetrics();
    void cardExposesThemeProperty();
};

void FluentComponentsTests::tokenMetricsMatchDocumentedScale() {
    using fluent::MetricRole;
    QCOMPARE(fluent::metric(MetricRole::SpacingXS), 4);
    QCOMPARE(fluent::metric(MetricRole::SpacingS), 8);
    QCOMPARE(fluent::metric(MetricRole::SpacingM), 12);
    QCOMPARE(fluent::metric(MetricRole::ControlMedium), 32);
    QCOMPARE(fluent::metric(MetricRole::RadiusMedium), 4);
}

void FluentComponentsTests::semanticColorsSupportBothThemes() {
    using fluent::ColorRole;
    QCOMPARE(fluent::color(ColorRole::NeutralForeground1), QColor(QStringLiteral("#242424")));
    QCOMPARE(fluent::color(ColorRole::NeutralForeground1, true),
             QColor(QStringLiteral("#EDF2F8")));
    QVERIFY(fluent::color(ColorRole::NeutralBackground1) !=
            fluent::color(ColorRole::NeutralBackground1, true));
}

void FluentComponentsTests::buttonExposesAppearanceAndSize() {
    FluentButton button(QStringLiteral("保存"));
    QCOMPARE(button.property("fluentControl").toBool(), true);
    QCOMPARE(button.property("fluentAppearance").toString(), QStringLiteral("secondary"));
    QCOMPARE(button.height(), 32);

    button.setAppearance(FluentButtonAppearance::Primary);
    button.setControlSize(FluentControlSize::Large);
    QCOMPARE(button.appearance(), FluentButtonAppearance::Primary);
    QCOMPARE(button.property("fluentAppearance").toString(), QStringLiteral("primary"));
    QCOMPARE(button.height(), 40);
}

void FluentComponentsTests::lineEditUsesStandardControlHeight() {
    FluentLineEdit edit;
    QCOMPARE(edit.property("fluentControl").toBool(), true);
    QCOMPARE(edit.height(), 32);
    edit.setControlSize(FluentControlSize::Small);
    QCOMPARE(edit.height(), 24);
}

void FluentComponentsTests::comboOwnsAReusablePopup() {
    FluentComboBox combo;
    combo.addItems({QStringLiteral("全部"), QStringLiteral("高亮")});
    combo.setPopupMinimumWidth(196);
    combo.setVisibleItemLimit(8);

    QVERIFY(combo.view() != nullptr);
    QCOMPARE(combo.view()->property("fluentPopup").toBool(), true);
    QCOMPARE(combo.popupMinimumWidth(), 196);
    QCOMPARE(combo.maxVisibleItems(), 8);
    QCOMPARE(combo.height(), 32);
}

void FluentComponentsTests::searchBoxProvidesSearchAndClearActions() {
    FluentSearchBox search;
    QCOMPARE(search.property("fluentSearchBox").toBool(), true);
    QVERIFY(search.isClearButtonEnabled());
    QVERIFY(!search.actions().isEmpty());
    QCOMPARE(search.height(), 32);
}

void FluentComponentsTests::toolButtonUsesSquareMetrics() {
    FluentToolButton button;
    QCOMPARE(button.property("fluentAppearance").toString(), QStringLiteral("subtle"));
    QCOMPARE(button.size(), QSize(32, 32));
    button.setAppearance(FluentButtonAppearance::Secondary);
    button.setControlSize(FluentControlSize::Small);
    QCOMPARE(button.property("fluentAppearance").toString(), QStringLiteral("secondary"));
    QCOMPARE(button.size(), QSize(24, 24));
}

void FluentComponentsTests::cardExposesThemeProperty() {
    FluentCard card;
    QCOMPARE(card.property("fluentCard").toBool(), true);
    QCOMPARE(card.frameShape(), QFrame::NoFrame);
}

QTEST_MAIN(FluentComponentsTests)
#include "FluentComponentsTests.moc"
