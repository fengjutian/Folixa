// 文档级附件（/Names → /EmbeddedFiles 名字树）的测试。
//
// 覆盖两层：
//   1) 适配层（pdf::PdfDocument）：加 → 序列化 → 重开 → 读回 → 删。
//      这一层钉住的是**写盘之后附件还在**——只测「内存里能加」是不够的，
//      附件的全部价值就在它跟着文档走。
//   2) 面板（app::AttachmentPanel）：列表、选中、空状态、四个信号。
//
// 硬规矩（别破坏）：
//   1) **不许抢用户的桌面**：要建窗口的用例必须先
//      setAttribute(Qt::WA_DontShowOnScreen, true) 再 show()。
//   2) 不依赖网络，也不依赖本机装了什么软件。
//   3) 夹具用仓库自带的 pdf-fixtures（normal.pdf），不自己造 PDF。
#include "app/panels/AttachmentPanel.h"

#include "pdf/kernel/PdfiumAdapter.h"

#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>#include <QTest>

using aipdf::app::AttachmentPanel;
using aipdf::pdf::DocumentAttachment;
using aipdf::pdf::PdfDocument;
using aipdf::pdf::openDocument;
using aipdf::pdf::openDocumentFromBytes;

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

// 打开夹具里的 normal.pdf；打不开就直接失败（夹具缺失是环境问题，不是被测行为）。
PdfDocument openFixture() {
    aipdf::pdf::PdfOpenResult result = openDocument(fixturePath(QStringLiteral("normal.pdf")));
    if (!result.document.isValid()) {
        return PdfDocument();
    }
    return result.document;
}

}  // namespace

class AttachmentPanelTests : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------------- 适配层 ----
    void attachmentSurvivesSerialization();
    void attachmentContentIsReadBackByteForByte();
    void removingAttachmentTakesItOutOfTheList();
    void duplicateNameIsRejectedWithReason();
    void emptyNameAndEmptyDataAreRejected();
    void documentWithoutAttachmentsListsNothing();
    // ---------------------------------------------------------- 面板层 ----
    void panelListsAttachmentsAndSelectsByName();
    void panelWithoutAttachmentsShowsEmptyState();
    void panelSignalsCarryTheAttachmentName();
};

// 加进去的附件，序列化再重开之后还得在。
// 这一条是整个功能的命门：附件的价值就在「跟着文档走」，
// 只在内存里加一个不算数。
void AttachmentPanelTests::attachmentSurvivesSerialization() {
    PdfDocument document = openFixture();
    QVERIFY2(document.isValid(), "夹具 normal.pdf 打不开");

    QString error;
    QVERIFY2(document.addAttachment(QStringLiteral("readme.txt"),
                                    QByteArray("hello attachment"),
                                    QStringLiteral("说明文字"), &error),
             qPrintable(QStringLiteral("添加失败：%1").arg(error)));

    const QVector<DocumentAttachment> before = document.attachments();
    QCOMPARE(before.size(), 1);
    QCOMPARE(before.first().name, QStringLiteral("readme.txt"));

    QString serializeError;
    const QByteArray bytes = document.serialize(&serializeError);
    QVERIFY2(!bytes.isEmpty(), qPrintable(serializeError));

    aipdf::pdf::PdfOpenResult reopened = openDocumentFromBytes(bytes);
    QVERIFY2(reopened.document.isValid(),
             qPrintable(QStringLiteral("重开失败：%1").arg(reopened.errorMessage)));

    const QVector<DocumentAttachment> after = reopened.document.attachments();
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.first().name, QStringLiteral("readme.txt"));
    QCOMPARE(after.first().description, QStringLiteral("说明文字"));
    QCOMPARE(after.first().size, qint64(16));
    // 内容必须逐字节原样回来：写盘时多一个字节、少一个字节都不行。
    QCOMPARE(reopened.document.attachmentData(0), QByteArray("hello attachment"));
}

// 读回来的内容必须和写进去的一个字节不差。
void AttachmentPanelTests::attachmentContentIsReadBackByteForByte() {
    PdfDocument document = openFixture();
    QVERIFY(document.isValid());

    const QByteArray payload("\x00\x01\x02 binary \xff\xfe", 13);
    QString error;
    QVERIFY2(document.addAttachment(QStringLiteral("blob.bin"), payload, QString(), &error),
             qPrintable(error));

    const QVector<DocumentAttachment> entries = document.attachments();
    QCOMPARE(entries.size(), 1);

    QString readError;
    const QByteArray read = document.attachmentData(0, &readError);
    QVERIFY2(!read.isEmpty(), qPrintable(readError));
    QCOMPARE(read, payload);

    // 再走一遍写盘：二进制内容经 PDFium 序列化后仍要一个字节不差。
    QString serializeError;
    const QByteArray bytes = document.serialize(&serializeError);
    QVERIFY2(!bytes.isEmpty(), qPrintable(serializeError));
    const aipdf::pdf::PdfOpenResult reopened = openDocumentFromBytes(bytes);
    QVERIFY2(reopened.document.isValid(),
             qPrintable(QStringLiteral("重开失败：%1").arg(reopened.errorMessage)));
    QCOMPARE(reopened.document.attachmentData(0), payload);
}

// 删掉的附件不该再出现在列表里。
void AttachmentPanelTests::removingAttachmentTakesItOutOfTheList() {
    PdfDocument document = openFixture();
    QVERIFY(document.isValid());

    QString error;
    QVERIFY(document.addAttachment(QStringLiteral("a.txt"), QByteArray("aaa"), QString(), &error));
    QVERIFY(document.addAttachment(QStringLiteral("b.txt"), QByteArray("bbb"), QString(), &error));
    QCOMPARE(document.attachments().size(), 2);

    QVERIFY2(document.removeAttachment(0, &error), qPrintable(error));
    const QVector<DocumentAttachment> entries = document.attachments();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().name, QStringLiteral("b.txt"));
}

// 同名附件要明确失败，并给出原因——不能静默覆盖或静默成功。
void AttachmentPanelTests::duplicateNameIsRejectedWithReason() {
    PdfDocument document = openFixture();
    QVERIFY(document.isValid());

    QString error;
    QVERIFY(document.addAttachment(QStringLiteral("same.txt"), QByteArray("1"), QString(), &error));
    QVERIFY2(!document.addAttachment(QStringLiteral("same.txt"), QByteArray("2"), QString(), &error),
             "同名附件应该被拒绝");
    QVERIFY2(!error.isEmpty(), "失败时必须给出原因");
    QCOMPARE(document.attachments().size(), 1);
}

// 空名字与空内容都要被拒绝，并给出原因。
void AttachmentPanelTests::emptyNameAndEmptyDataAreRejected() {
    PdfDocument document = openFixture();
    QVERIFY(document.isValid());

    QString error;
    QVERIFY2(!document.addAttachment(QStringLiteral("   "), QByteArray("x"), QString(), &error),
             "空名字应该被拒绝");
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY2(!document.addAttachment(QStringLiteral("ok.txt"), QByteArray(), QString(), &error),
             "空内容应该被拒绝");
    QVERIFY(!error.isEmpty());
    QCOMPARE(document.attachments().size(), 0);
}

// 本来就没有附件的文档：列表为空，不报错。
void AttachmentPanelTests::documentWithoutAttachmentsListsNothing() {
    PdfDocument document = openFixture();
    QVERIFY(document.isValid());
    QCOMPARE(document.attachments().size(), 0);
}

// ---------------------------------------------------------- 面板层 ----

void AttachmentPanelTests::panelListsAttachmentsAndSelectsByName() {
    AttachmentPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.show();

    QVector<DocumentAttachment> entries;
    DocumentAttachment first;
    first.name = QStringLiteral("a.txt");
    first.description = QStringLiteral("第一个");
    first.size = 12;
    entries.append(first);
    DocumentAttachment second;
    second.name = QStringLiteral("b.pdf");
    second.size = 2048;
    entries.append(second);

    panel.setAttachments(entries);
    QCOMPARE(panel.count(), 2);
    QVERIFY(panel.selectAttachment(QStringLiteral("b.pdf")));
    QCOMPARE(panel.selectedName(), QStringLiteral("b.pdf"));
}

void AttachmentPanelTests::panelWithoutAttachmentsShowsEmptyState() {
    AttachmentPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.show();

    panel.setAttachments(QVector<DocumentAttachment>());
    QCOMPARE(panel.count(), 0);
    QVERIFY(panel.selectedName().isEmpty());
    // 没选中时「打开 / 保存副本 / 删除」都不该可点。
    const auto buttons = panel.findChildren<QPushButton*>();
    QVERIFY(!buttons.isEmpty());
    for (QPushButton* button : buttons) {
        const QString name = button->objectName();
        if (name == QLatin1String("attachmentOpenButton")
            || name == QLatin1String("attachmentSaveButton")
            || name == QLatin1String("attachmentRemoveButton")) {
            QVERIFY2(!button->isEnabled(), qPrintable(name));
        }
    }
}

void AttachmentPanelTests::panelSignalsCarryTheAttachmentName() {
    AttachmentPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.show();

    DocumentAttachment entry;
    entry.name = QStringLiteral("doc.zip");
    entry.size = 7;
    panel.setAttachments(QVector<DocumentAttachment>{entry});
    QVERIFY(panel.selectAttachment(QStringLiteral("doc.zip")));

    QSignalSpy removed(&panel, &AttachmentPanel::removeRequested);
    QSignalSpy opened(&panel, &AttachmentPanel::openRequested);
    QSignalSpy saved(&panel, &AttachmentPanel::saveRequested);

    for (QPushButton* button : panel.findChildren<QPushButton*>()) {
        if (button->objectName() == QLatin1String("attachmentRemoveButton")) {
            button->click();
        }
        if (button->objectName() == QLatin1String("attachmentOpenButton")) {
            button->click();
        }
        if (button->objectName() == QLatin1String("attachmentSaveButton")) {
            button->click();
        }
    }

    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().first().toString(), QStringLiteral("doc.zip"));
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.first().first().toString(), QStringLiteral("doc.zip"));
    QCOMPARE(saved.count(), 1);
    QCOMPARE(saved.first().first().toString(), QStringLiteral("doc.zip"));
}

QTEST_MAIN(AttachmentPanelTests)
#include "AttachmentPanelTests.moc"
