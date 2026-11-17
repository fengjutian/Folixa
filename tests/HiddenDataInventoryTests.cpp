// 隐藏数据逐项清理（枚举 + 选择性清理）的单元测试。
//
// 这个文件的用例由 wave6-C 路补齐；下面是能编译能跑的空骨架，
// 目的是让 CMake 片段先能配出来（配置期缺源文件会直接失败）。

#include "pdf/PdfSecurity.h"

#include <QTest>

class HiddenDataInventoryTests : public QObject {
    Q_OBJECT

private slots:
    void skeleton();
};

void HiddenDataInventoryTests::skeleton()
{
    // 骨架占位：真实的用例由 wave6-C 路写进来。
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(HiddenDataInventoryTests)
#include "HiddenDataInventoryTests.moc"
