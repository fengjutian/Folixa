#include "doc/print/PrintTemplate.h"

#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

using aipdf::doc::PrintTemplate;
using aipdf::doc::deletePrintTemplate;
using aipdf::doc::listPrintTemplates;
using aipdf::doc::loadPrintTemplate;
using aipdf::doc::savePrintTemplate;
using aipdf::doc::validatePrintTemplate;

namespace {

PrintTemplate makeTemplate(const QString& name = QStringLiteral("默认模板")) {
    PrintTemplate t;
    t.name = name;
    return t;
}

}  // namespace

class PrintTemplateTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void defaultTemplateIsValid();
    void emptyNameIsRejected();
    void zeroCopiesIsRejected();
    void tooManyCopiesIsRejected();
    void tooSmallScalePercentIsRejected();
    void tooLargeScalePercentIsRejected();
    void illegalScalingIsRejected();
    void savedTemplateReadsBackIdentical();
    void listReturnsSavedTemplates();
    void deleteRemovesTemplateFromList();
    void deletingUnknownIdReturnsFalse();
    void emptyIdGetsGeneratedOnSave();
    void differentSettingsKeysAreIsolated();
    void savingInvalidTemplateFails();
    void loadingUnknownTemplateFails();

private:
    // 设置存储的临时根目录：模板必须存在用户的真实设置之外。
    QTemporaryDir* m_storageRoot = nullptr;
};

void PrintTemplateTests::initTestCase() {
    // 把设置存储整个指到临时目录：模板必须存在**用户的真实设置**之外，
    // 否则跑一次单元测试就往用户机器里塞一份模板。
    m_storageRoot = new QTemporaryDir();
    QVERIFY(m_storageRoot->isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_storageRoot->path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_storageRoot->path());
}

void PrintTemplateTests::cleanupTestCase() {
    delete m_storageRoot;
    m_storageRoot = nullptr;
}

void PrintTemplateTests::defaultTemplateIsValid() {
    QString error;
    const PrintTemplate t = makeTemplate();
    QVERIFY2(validatePrintTemplate(t, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
}

void PrintTemplateTests::emptyNameIsRejected() {
    PrintTemplate t = makeTemplate();
    t.name = QString();
    QString error;
    QVERIFY(!validatePrintTemplate(t, &error));
    QVERIFY(!error.isEmpty());
}

void PrintTemplateTests::zeroCopiesIsRejected() {
    PrintTemplate t = makeTemplate();
    t.copies = 0;
    QString error;
    QVERIFY(!validatePrintTemplate(t, &error));
    QVERIFY(!error.isEmpty());
}

void PrintTemplateTests::tooManyCopiesIsRejected() {
    PrintTemplate t = makeTemplate();
    t.copies = 100;
    QString error;
    QVERIFY(!validatePrintTemplate(t, &error));
    QVERIFY(!error.isEmpty());
}

void PrintTemplateTests::tooSmallScalePercentIsRejected() {
    PrintTemplate t = makeTemplate();
    t.scalePercent = 5;
    QString error;
    QVERIFY(!validatePrintTemplate(t, &error));
    QVERIFY(!error.isEmpty());
}

void PrintTemplateTests::tooLargeScalePercentIsRejected() {
    PrintTemplate t = makeTemplate();
    t.scalePercent = 400;
    QString error;
    QVERIFY(!validatePrintTemplate(t, &error));
    QVERIFY(!error.isEmpty());
}

void PrintTemplateTests::illegalScalingIsRejected() {
    PrintTemplate t = makeTemplate();
    t.scaling = QStringLiteral("Whatever");
    QString error;
    QVERIFY(!validatePrintTemplate(t, &error));
    QVERIFY(!error.isEmpty());

    // 三种合法值都要过。
    for (const QString& mode : {QStringLiteral("None"), QStringLiteral("FitToPage"),
                                QStringLiteral("ShrinkToFit")}) {
        t.scaling = mode;
        QVERIFY2(validatePrintTemplate(t, &error), qPrintable(mode.toUtf8().constData()));
    }
}

void PrintTemplateTests::savedTemplateReadsBackIdentical() {
    const QString key = QStringLiteral("tests/readBack");
    PrintTemplate t = makeTemplate(QStringLiteral("带全部字段的模板"));
    t.id = QStringLiteral("fixed-id-1");
    t.copies = 3;
    t.paperSize = QStringLiteral("A3");
    t.orientation = aipdf::doc::PageOrientation::Landscape;
    t.scaling = QStringLiteral("ShrinkToFit");
    t.scalePercent = 88;
    t.duplex = true;
    t.color = false;
    t.pageRange = QStringLiteral("2-5");

    QString error;
    QVERIFY2(savePrintTemplate(t, key, &error), qPrintable(error));

    PrintTemplate loaded;
    QVERIFY2(loadPrintTemplate(QStringLiteral("fixed-id-1"), key, &loaded, &error),
             qPrintable(error));
    QCOMPARE(loaded.id, t.id);
    QCOMPARE(loaded.name, t.name);
    QCOMPARE(loaded.copies, t.copies);
    QCOMPARE(loaded.paperSize, t.paperSize);
    QCOMPARE(static_cast<int>(loaded.orientation), static_cast<int>(t.orientation));
    QCOMPARE(loaded.scaling, t.scaling);
    QCOMPARE(loaded.scalePercent, t.scalePercent);
    QCOMPARE(loaded.duplex, t.duplex);
    QCOMPARE(loaded.color, t.color);
    QCOMPARE(loaded.pageRange, t.pageRange);

    deletePrintTemplate(QStringLiteral("fixed-id-1"), key);
}

void PrintTemplateTests::listReturnsSavedTemplates() {
    const QString key = QStringLiteral("tests/list");
    PrintTemplate first = makeTemplate(QStringLiteral("模板一"));
    first.id = QStringLiteral("list-1");
    PrintTemplate second = makeTemplate(QStringLiteral("模板二"));
    second.id = QStringLiteral("list-2");

    QString error;
    QVERIFY2(savePrintTemplate(first, key, &error), qPrintable(error));
    QVERIFY2(savePrintTemplate(second, key, &error), qPrintable(error));

    const QVector<PrintTemplate> templates = listPrintTemplates(key);
    QCOMPARE(templates.size(), 2);

    QStringList names;
    for (const PrintTemplate& t : templates) {
        names.append(t.name);
    }
    QVERIFY(names.contains(QStringLiteral("模板一")));
    QVERIFY(names.contains(QStringLiteral("模板二")));

    deletePrintTemplate(QStringLiteral("list-1"), key);
    deletePrintTemplate(QStringLiteral("list-2"), key);
}

void PrintTemplateTests::deleteRemovesTemplateFromList() {
    const QString key = QStringLiteral("tests/delete");
    PrintTemplate t = makeTemplate(QStringLiteral("待删除的模板"));
    t.id = QStringLiteral("del-1");

    QString error;
    QVERIFY2(savePrintTemplate(t, key, &error), qPrintable(error));
    QCOMPARE(listPrintTemplates(key).size(), 1);

    QVERIFY(deletePrintTemplate(QStringLiteral("del-1"), key));
    QCOMPARE(listPrintTemplates(key).size(), 0);
}

void PrintTemplateTests::deletingUnknownIdReturnsFalse() {
    const QString key = QStringLiteral("tests/deleteUnknown");
    // 不崩、返回 false。
    QVERIFY(!deletePrintTemplate(QStringLiteral("never-existed"), key));
    QVERIFY(!deletePrintTemplate(QString(), key));
}

void PrintTemplateTests::emptyIdGetsGeneratedOnSave() {
    const QString key = QStringLiteral("tests/generatedId");
    PrintTemplate t = makeTemplate(QStringLiteral("自动编号的模板"));
    t.id = QString();
    t.copies = 2;
    t.pageRange = QStringLiteral("1-3");

    QString error;
    QVERIFY2(savePrintTemplate(t, key, &error), qPrintable(error));

    const QVector<PrintTemplate> templates = listPrintTemplates(key);
    QCOMPARE(templates.size(), 1);
    const PrintTemplate stored = templates.first();
    // 存的时候没有 id，现在有了一个自动生成的。
    QVERIFY(!stored.id.isEmpty());

    PrintTemplate loaded;
    QVERIFY2(loadPrintTemplate(stored.id, key, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.id, stored.id);
    QCOMPARE(loaded.name, t.name);
    QCOMPARE(loaded.copies, t.copies);
    QCOMPARE(loaded.pageRange, t.pageRange);

    deletePrintTemplate(stored.id, key);
}

void PrintTemplateTests::differentSettingsKeysAreIsolated() {
    PrintTemplate a = makeTemplate(QStringLiteral("甲组模板"));
    a.id = QStringLiteral("same-id");
    PrintTemplate b = makeTemplate(QStringLiteral("乙组模板"));
    b.id = QStringLiteral("same-id");

    QString error;
    QVERIFY2(savePrintTemplate(a, QStringLiteral("tests/groupA"), &error), qPrintable(error));
    QVERIFY2(savePrintTemplate(b, QStringLiteral("tests/groupB"), &error), qPrintable(error));

    const QVector<PrintTemplate> groupA = listPrintTemplates(QStringLiteral("tests/groupA"));
    const QVector<PrintTemplate> groupB = listPrintTemplates(QStringLiteral("tests/groupB"));
    QCOMPARE(groupA.size(), 1);
    QCOMPARE(groupB.size(), 1);
    QCOMPARE(groupA.first().name, QStringLiteral("甲组模板"));
    QCOMPARE(groupB.first().name, QStringLiteral("乙组模板"));

    deletePrintTemplate(QStringLiteral("same-id"), QStringLiteral("tests/groupA"));
    deletePrintTemplate(QStringLiteral("same-id"), QStringLiteral("tests/groupB"));
}

void PrintTemplateTests::savingInvalidTemplateFails() {
    PrintTemplate t = makeTemplate();
    t.name = QString();
    QString error;
    QVERIFY(!savePrintTemplate(t, QStringLiteral("tests/invalid"), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(listPrintTemplates(QStringLiteral("tests/invalid")).size(), 0);
}

void PrintTemplateTests::loadingUnknownTemplateFails() {
    QString error;
    PrintTemplate loaded;
    QVERIFY(!loadPrintTemplate(QStringLiteral("no-such-template"), QStringLiteral("tests/unknown"),
                               &loaded, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!loadPrintTemplate(QString(), QStringLiteral("tests/unknown"), &loaded, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(PrintTemplateTests)
#include "PrintTemplateTests.moc"
