// 图标资源的回归守卫。
//
// 症状：工具链只装 qtbase 时，运行时没有 qsvg 图像插件，
// QIcon(":/icons/pages.svg") 会**静默**变成空图标——左侧导航栏因此
// 只剩一列白方块，而任何「按钮存在 / 位置正确」的断言都发现不了：
// 控件在、尺寸对，就是没画东西。
//
// 所以这里不看控件，直接看图标本身：能取到非空 pixmap，并且真的有
// 不透明的笔画像素（只看尺寸的话，全透明的图也能通过）。
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QStringList>
#include <QtTest>

class IconResourceTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void iconHasInk_data();
    void iconHasInk();
};

void IconResourceTests::initTestCase() {
    // 只认应用目录下的插件。否则开发机上 Qt SDK 里的 qsvg.dll 会被找到，
    // 这条守卫就永远绿着——而真正要守的恰恰是「发布包/构建树里带了插件」。
    QCoreApplication::setLibraryPaths({QCoreApplication::applicationDirPath()});
}

void IconResourceTests::iconHasInk_data() {
    QTest::addColumn<QString>("path");
    const QStringList names{QStringLiteral("pages"),   QStringLiteral("outline"),
                            QStringLiteral("bookmark"), QStringLiteral("search"),
                            QStringLiteral("annotation"), QStringLiteral("index"),
                            QStringLiteral("send"),     QStringLiteral("chat"),
                            QStringLiteral("rewrite"),  QStringLiteral("proofread"),
                            QStringLiteral("explain"),  QStringLiteral("summarize")};
    for (const QString& name : names) {
        QTest::newRow(qPrintable(name))
            << QStringLiteral(":/icons/%1.svg").arg(name);
    }
}

void IconResourceTests::iconHasInk() {
    QFETCH(QString, path);

    const QIcon icon(path);
    // 资源缺失（qt_add_resources 没打包）时这里就红了。
    QVERIFY2(!icon.isNull(), qPrintable(QStringLiteral("图标资源不存在: %1").arg(path)));

    const QPixmap pixmap = icon.pixmap(QSize(20, 20));
    QVERIFY2(!pixmap.isNull(), "图标渲染为空：通常是缺少 qsvg 图像插件");

    // 空插件/全透明图也能给出非空 QPixmap，所以再找一次真实的笔画像素。
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    bool hasInk = false;
    for (int y = 0; y < image.height() && !hasInk; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                hasInk = true;
                break;
            }
        }
    }
    QVERIFY2(hasInk, "图标渲染成了全透明：SVG 插件在，但没有画出内容");
}

QTEST_MAIN(IconResourceTests)
#include "IconResourceTests.moc"
