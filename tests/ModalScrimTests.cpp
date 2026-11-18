// 弹窗蒙层的单元测试。
//
// 盯的是蒙层的三件事：铺满父窗口、跟着父窗口尺寸走、不吃鼠标事件；
// 以及 execWithScrim() 用完会把蒙层收掉，不留在窗口上。

#include "app/ModalScrim.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QTest>
#include <QTimer>

using aipdf::app::execWithScrim;
using aipdf::app::ModalScrim;

class ModalScrimTests : public QObject {
    Q_OBJECT

private slots:
    void scrimCoversTheWholeParent();
    void scrimFollowsParentResize();
    void scrimIgnoresMouseEvents();
    void execWithScrimCleansUpAfterTheDialog();
};

void ModalScrimTests::scrimCoversTheWholeParent() {
    QWidget window;
    // 不弹窗到桌面上：本地跑测试不能抢用户的桌面。
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.resize(800, 600);
    window.show();

    ModalScrim scrim(&window);
    QCOMPARE(scrim.geometry(), window.rect());
}

void ModalScrimTests::scrimFollowsParentResize() {
    QWidget window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.resize(800, 600);
    window.show();

    ModalScrim scrim(&window);
    window.resize(640, 480);
    QCOMPARE(scrim.geometry(), window.rect());
}

void ModalScrimTests::scrimIgnoresMouseEvents() {
    QWidget window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.show();

    ModalScrim scrim(&window);
    // 模态弹窗已经挡住父窗口的输入了，蒙层再吃事件只会像卡死。
    QVERIFY(scrim.testAttribute(Qt::WA_TransparentForMouseEvents));
}

void ModalScrimTests::execWithScrimCleansUpAfterTheDialog() {
    QWidget window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.show();

    QDialog dialog(&window);
    // 弹窗本身也不上屏：本地跑测试不能抢用户的桌面。
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    // exec() 会进模态事件循环，得让弹窗自己关掉，否则测试卡死。
    QTimer::singleShot(0, &dialog, &QDialog::accept);

    const int result = execWithScrim(dialog, &window);
    QCOMPARE(result, int(QDialog::Accepted));

    // 蒙层是 deleteLater 收的：把延迟删除事件跑完再查。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(window.findChildren<ModalScrim*>().isEmpty());
}

QTEST_MAIN(ModalScrimTests)
#include "ModalScrimTests.moc"
