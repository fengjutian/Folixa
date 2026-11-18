// Code 128 条形码编码器的单元测试。
//
// 条码编码最怕"看着像条码、实际扫不出来"：宽度串写错一位、校验位权重算错、
// 码集切换漏了切换符，都不会报错，只会安静地生成一张废图。
// 所以这里对着标准表的已知向量逐段比对图案，而不是只断言"没崩"。

#include "pdf/forms/BarcodeEncoder.h"

#include <QTest>

using aipdf::pdf::BarcodeCode128;
using aipdf::pdf::BarcodeModule;
using aipdf::pdf::encodeCode128;

namespace {

// 按标准表里的宽度串展开出期望的模块序列。
// 宽度串是"条、空、条、空……"这样交替的；测试自己展开一遍，才能真的校验到图案，
// 而不只是校验到宽度总和。
QVector<BarcodeModule> expectedModules(const QStringList& patterns) {
    QVector<BarcodeModule> modules;
    for (const QString& pattern : patterns) {
        bool bar = true;
        for (const QChar ch : pattern) {
            modules.append(BarcodeModule{ch.unicode() - '0', bar});
            bar = !bar;
        }
    }
    return modules;
}

int totalWidth(const BarcodeCode128& code) {
    int width = 0;
    for (const BarcodeModule& module : code.modules) {
        width += module.width;
    }
    return width;
}

void compareModules(const BarcodeCode128& code, const QVector<BarcodeModule>& expected) {
    QCOMPARE(code.modules.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        QCOMPARE(code.modules.at(i).width, expected.at(i).width);
        QCOMPARE(code.modules.at(i).bar, expected.at(i).bar);
    }
}

}  // namespace

class BarcodeEncoderTests : public QObject {
    Q_OBJECT

private slots:
    void codeSetSelection_data();
    void codeSetSelection();
    void encodesKnownCodeCVector();
    void computesMod103Checksum();
    void rejectsNonAsciiText();
    void rejectsEmptyText();
    void mergesAdjacentSameColorModules();
    void encodesPlainAsciiText();
};

// 码集选择：偶数长度且长度 >= 4 的纯数字串走 Code C，其余走 Code B。
// 总宽 = 11 × (起始符 + 数据符号 + 校验位) + 13（终止符 13 模块）。
void BarcodeEncoderTests::codeSetSelection_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("codeSet");
    QTest::addColumn<int>("width");

    QTest::newRow("8 位纯数字走 Code C") << QStringLiteral("12345678") << QStringLiteral("C") << 79;
    QTest::newRow("奇数长度数字回退 Code B") << QStringLiteral("12345") << QStringLiteral("B") << 90;
    QTest::newRow("2 位数字不够长，留在 Code B") << QStringLiteral("AB12") << QStringLiteral("B") << 79;
    QTest::newRow("纯文本走 Code B") << QStringLiteral("ABC-123") << QStringLiteral("B") << 112;
    QTest::newRow("文本加数字段") << QStringLiteral("ABC-1234") << QStringLiteral("B+C") << 112;
}

void BarcodeEncoderTests::codeSetSelection() {
    QFETCH(QString, text);
    QFETCH(QString, codeSet);
    QFETCH(int, width);

    const BarcodeCode128 code = encodeCode128(text);
    QVERIFY(code.ok);
    QCOMPARE(code.codeSet, codeSet);
    QCOMPARE(totalWidth(code), width);
}

// 已知向量："12345678" 全程 Code C：Start C(105) + 12 + 34 + 56 + 78 + 校验位 + Stop。
// 校验位 = (105 + 1×12 + 2×34 + 3×56 + 4×78) mod 103 = 47。
void BarcodeEncoderTests::encodesKnownCodeCVector() {
    const BarcodeCode128 code = encodeCode128(QStringLiteral("12345678"));
    QVERIFY(code.ok);
    QCOMPARE(code.codeSet, QStringLiteral("C"));
    QCOMPARE(totalWidth(code), 79);
    compareModules(code, expectedModules({
        QStringLiteral("211232"),  // Start C
        QStringLiteral("112232"),  // 12
        QStringLiteral("131123"),  // 34
        QStringLiteral("331121"),  // 56
        QStringLiteral("241112"),  // 78
        QStringLiteral("133121"),  // 校验位 47
        QStringLiteral("2331112"), // Stop
    }));
}

// 校验位算法：混合码集时切换码也要按位置加权。
// "ABC-1234" = Start B + A B C - + 切到 Code C + 12 + 34。
// 校验位 = (104 + 1×33 + 2×34 + 3×35 + 4×13 + 5×99 + 6×12 + 7×34) mod 103 = 34。
void BarcodeEncoderTests::computesMod103Checksum() {
    const BarcodeCode128 code = encodeCode128(QStringLiteral("ABC-1234"));
    QVERIFY(code.ok);
    QCOMPARE(totalWidth(code), 112);
    compareModules(code, expectedModules({
        QStringLiteral("211214"),  // Start B
        QStringLiteral("111323"),  // A
        QStringLiteral("131123"),  // B
        QStringLiteral("131321"),  // C
        QStringLiteral("122132"),  // '-'
        QStringLiteral("113141"),  // 切换到 Code C
        QStringLiteral("112232"),  // 12
        QStringLiteral("131123"),  // 34
        QStringLiteral("131123"),  // 校验位 34
        QStringLiteral("2331112"), // Stop
    }));
}

// 含中文必须报错：静默替换出来的条码扫出来是另一个值，用户只会看到"扫不出来"。
void BarcodeEncoderTests::rejectsNonAsciiText() {
    const BarcodeCode128 code = encodeCode128(QStringLiteral("条码-123"));
    QVERIFY(!code.ok);
    QVERIFY(!code.error.isEmpty());
    QVERIFY(code.modules.isEmpty());
}

void BarcodeEncoderTests::rejectsEmptyText() {
    const BarcodeCode128 code = encodeCode128(QString());
    QVERIFY(!code.ok);
    QVERIFY(!code.error.isEmpty());
}

// 相邻同色的模块必须已经合并：同一串里不该出现两个连续的 bar。
void BarcodeEncoderTests::mergesAdjacentSameColorModules() {
    const QStringList samples = {
        QStringLiteral("12345678"),
        QStringLiteral("ABC-1234"),
        QStringLiteral("ABC-123"),
        QStringLiteral("12345"),
    };
    for (const QString& sample : samples) {
        const BarcodeCode128 code = encodeCode128(sample);
        QVERIFY(code.ok);
        QVERIFY(!code.modules.isEmpty());
        QVERIFY(code.modules.first().bar);   // 起始符是条
        QVERIFY(code.modules.last().bar);    // 终止符末尾是 2 模块宽的结束条
        for (int i = 1; i < code.modules.size(); ++i) {
            QVERIFY2(code.modules.at(i).bar != code.modules.at(i - 1).bar,
                     qPrintable(QStringLiteral("相邻同色模块没有合并: %1").arg(sample)));
        }
    }
}

// 纯 ASCII 文本（含 '-' 这种非数字字符）能正常编码。
void BarcodeEncoderTests::encodesPlainAsciiText() {
    const BarcodeCode128 code = encodeCode128(QStringLiteral("ABC-123"));
    QVERIFY(code.ok);
    QCOMPARE(code.codeSet, QStringLiteral("B"));
    QCOMPARE(totalWidth(code), 112);
    compareModules(code, expectedModules({
        QStringLiteral("211214"),  // Start B
        QStringLiteral("111323"),  // A
        QStringLiteral("131123"),  // B
        QStringLiteral("131321"),  // C
        QStringLiteral("122132"),  // '-'
        QStringLiteral("123221"),  // '1'
        QStringLiteral("223211"),  // '2'
        QStringLiteral("221132"),  // '3'
        QStringLiteral("112412"),  // 校验位 70
        QStringLiteral("2331112"), // Stop
    }));
}

QTEST_MAIN(BarcodeEncoderTests)
#include "BarcodeEncoderTests.moc"
