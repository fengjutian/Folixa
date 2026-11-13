// 页面管理与安全保存的测试。
//
// 关注三件事：
//   1) 页面操作真的改了文档：页数、顺序、页面自身的旋转；
//   2) 批注跟着页面走，页面操作之后重写批注不会变成追加（/NM 标记）；
//   3) 安全保存：写出来的文件能打开，失败时原文件不动。
//
// 这些都要真 PDFium：页面结构是 PDFium 文档的一部分，
// 单元测试里不碰界面。

#include "app/AnnotationController.h"
#include "app/PdfPageOperations.h"
#include "doc/EditCommand.h"
#include "doc/PageCommand.h"
#include "doc/SafeSave.h"
#include "pdf/Annotation.h"
#include "pdf/PdfiumAdapter.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using aipdf::app::AnnotationController;
using aipdf::app::PdfPageOperations;
using aipdf::doc::DeletePageCommand;
using aipdf::doc::InsertBlankPageCommand;
using aipdf::doc::MovePageCommand;
using aipdf::doc::RotatePageCommand;
using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::PdfDocument;
using aipdf::pdf::PdfOpenResult;
using aipdf::pdf::openDocument;
using aipdf::pdf::openDocumentFromBytes;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

// 每一页的第一行文字：用来确认页的顺序。
QString firstLineOfPage(const PdfDocument& document, int index) {
    const auto page = document.page(index);
    if (!page.has_value()) {
        return QStringLiteral("<无此页>");
    }
    const auto text = page->extractText();
    if (!text.has_value() || text->chars.isEmpty()) {
        return QString();
    }
    QString line;
    for (const auto& character : text->chars) {
        if (character.text == QLatin1String("\n")) {
            break;
        }
        line += character.text;
    }
    return line.trimmed();
}

Annotation makeHighlight(quint64 id, int pageIndex) {
    Annotation annotation;
    annotation.id = id;
    annotation.pageIndex = pageIndex;
    annotation.type = AnnotationType::Highlight;
    annotation.rect = QRectF(60.0, 250.0, 200.0, 14.0);
    annotation.quads.append(QRectF(60.0, 250.0, 200.0, 14.0));
    annotation.color = QColor(255, 214, 0);
    return annotation;
}

}  // namespace

class PageOperationTests : public QObject {
    Q_OBJECT

private slots:
    void insertBlankPageAddsPage();
    void deletePageRemovesPage();
    void rotatePageChangesRotation();
    void movePageReordersPages();
    void movePageKeepsAnnotations();
    void deletePageUndoRestoresPage();
    void deletePageUndoRestoresAnnotations();
    void serializeKeepsPageOrder();
    void rewriteDoesNotDuplicateAnnotations();
    void safeSaveWritesValidFile();
    void safeSaveKeepsOriginalOnBadPayload();
    void safeSaveKeepsOriginalOnReadOnlyTarget();
    void safeSaveCreatesBackup();
};

void PageOperationTests::insertBlankPageAddsPage() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    const int before = result.document.pageCount();

    PdfPageOperations operations(result.document);
    QVector<int> mapping;
    QVERIFY(operations.insertBlankPage(1, 300.0, 500.0, &mapping));

    QCOMPARE(result.document.pageCount(), before + 1);
    QCOMPARE(mapping.size(), before);
    // 插入位置之后的页整体后移一位。
    QCOMPARE(mapping[0], 0);
    QCOMPARE(mapping[1], 2);
    // 新页的尺寸就是给定的尺寸。
    const auto inserted = result.document.page(1);
    QVERIFY(inserted.has_value());
    QCOMPARE(inserted->displayWidthPoints(), 300.0);
    QCOMPARE(inserted->displayHeightPoints(), 500.0);
}

void PageOperationTests::deletePageRemovesPage() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    const int before = result.document.pageCount();

    PdfPageOperations operations(result.document);
    QVector<int> mapping;
    QVERIFY(operations.deletePage(0, &mapping));

    QCOMPARE(result.document.pageCount(), before - 1);
    QCOMPARE(mapping[0], -1);
    QCOMPARE(mapping[1], 0);
    // 剩下的是原来的第 2 页。
    QCOMPARE(firstLineOfPage(result.document, 0), QStringLiteral("Page two"));
}

void PageOperationTests::rotatePageChangesRotation() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());

    PdfPageOperations operations(result.document);
    QVERIFY(operations.setPageRotation(0, 1));

    const auto page = result.document.page(0);
    QVERIFY(page.has_value());
    // 页面自身的 /Rotate 以四分之一圈计。
    QCOMPARE(page->rotationDegrees(), 90);
    // 旋转之后宽高要换。
    QCOMPARE(page->displayWidthPoints(), 400.0);
    QCOMPARE(page->displayHeightPoints(), 612.0);
}

void PageOperationTests::movePageReordersPages() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    QCOMPARE(firstLineOfPage(result.document, 0), QStringLiteral("Navigation demo"));

    PdfPageOperations operations(result.document);
    QVector<int> mapping;
    // 把第 1 页移到末尾。
    QVERIFY(operations.movePage(0, 2, &mapping));

    QCOMPARE(result.document.pageCount(), 3);
    QCOMPARE(firstLineOfPage(result.document, 0), QStringLiteral("Page two"));
    QCOMPARE(firstLineOfPage(result.document, 2), QStringLiteral("Navigation demo"));
    // 映射：第 0 页挪到第 2 位，第 1、2 页各前移一位。
    QCOMPARE(mapping[0], 2);
    QCOMPARE(mapping[1], 0);
    QCOMPARE(mapping[2], 1);
}

void PageOperationTests::movePageKeepsAnnotations() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());

    // 先在第 0 页上写一条批注。
    {
        auto page = result.document.page(0);
        QVERIFY(page.has_value());
        QVERIFY(page->setAnnotations({makeHighlight(1, 0)}));
    }

    PdfPageOperations operations(result.document);
    QVector<int> mapping;
    QVERIFY(operations.movePage(0, 2, &mapping));

    // 批注跟着页面走：现在在第 2 页上。
    const auto moved = result.document.page(2);
    QVERIFY(moved.has_value());
    const QVector<Annotation> annotations = moved->annotations();
    QCOMPARE(annotations.size(), 1);
    QCOMPARE(annotations.first().id, quint64(1));
    // 原来的第 0 页已经没有批注了。
    const auto empty = result.document.page(0);
    QVERIFY(empty.has_value());
    QCOMPARE(empty->annotations().size(), 0);
}

void PageOperationTests::deletePageUndoRestoresPage() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());

    AnnotationController controller(result.document);
    QVERIFY(controller.deletePage(0));
    QCOMPARE(controller.pageCount(), 2);

    // 撤销删除：页面要回来，内容也要对。
    QVERIFY(controller.undo());
    QCOMPARE(controller.pageCount(), 3);
    QCOMPARE(firstLineOfPage(result.document, 0), QStringLiteral("Navigation demo"));
}

void PageOperationTests::deletePageUndoRestoresAnnotations() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());

    AnnotationController controller(result.document);
    // 在第 0 页上放一条批注。
    const quint64 id = controller.addSquare(0, QRectF(60.0, 250.0, 100.0, 20.0));
    QVERIFY(id != 0);
    QCOMPARE(controller.annotationCountForPage(0), 1);

    QVERIFY(controller.deletePage(0));
    // 删页之后这页的批注不在模型里了。
    QCOMPARE(controller.annotationCountForPage(0), 0);

    // 撤销删除：页面回来，批注也要回来。
    QVERIFY(controller.undo());
    QCOMPARE(controller.pageCount(), 3);
    QCOMPARE(controller.annotationCountForPage(0), 1);
    const QVector<Annotation> annotations = controller.annotationsForPage(0);
    QCOMPARE(annotations.size(), 1);
    QCOMPARE(annotations.first().id, id);
}

void PageOperationTests::serializeKeepsPageOrder() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());

    PdfPageOperations operations(result.document);
    QVector<int> mapping;
    QVERIFY(operations.movePage(0, 2, &mapping));
    QVERIFY(operations.setPageRotation(1, 1));

    QString error;
    const QByteArray bytes = result.document.serialize(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    const PdfOpenResult reopened = openDocumentFromBytes(bytes);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    QCOMPARE(reopened.document.pageCount(), 3);
    QCOMPARE(firstLineOfPage(reopened.document, 0), QStringLiteral("Page two"));
    QCOMPARE(firstLineOfPage(reopened.document, 2), QStringLiteral("Navigation demo"));
    const auto rotated = reopened.document.page(1);
    QVERIFY(rotated.has_value());
    QCOMPARE(rotated->rotationDegrees(), 90);
}

void PageOperationTests::rewriteDoesNotDuplicateAnnotations() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());

    auto page = result.document.page(0);
    QVERIFY(page.has_value());
    // 第一次写一条。
    QVERIFY(page->setAnnotations({makeHighlight(1, 0)}));
    QCOMPARE(page->annotations().size(), 1);
    // 再写一次同样的一条：应当是替换而不是追加。
    QVERIFY(page->setAnnotations({makeHighlight(1, 0)}));
    QCOMPARE(page->annotations().size(), 1);
    // 换成两条：一条也不能多。
    QVERIFY(page->setAnnotations({makeHighlight(1, 0), makeHighlight(2, 0)}));
    QCOMPARE(page->annotations().size(), 2);
    // 清空：也要能删干净。
    QVERIFY(page->setAnnotations({}));
    QCOMPARE(page->annotations().size(), 0);
}

void PageOperationTests::safeSaveWritesValidFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = directory.filePath(QStringLiteral("out.pdf"));

    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    const QByteArray bytes = result.document.serialize();
    QVERIFY(!bytes.isEmpty());

    const aipdf::doc::SaveResult saved =
        aipdf::doc::SafeSave::writeAtomically(target, bytes, result.document.pageCount(), false);
    QVERIFY2(saved.ok, qPrintable(saved.errorMessage));

    QVERIFY(QFile::exists(target));
    // 写出来的文件能打开，页数对得上。
    const PdfOpenResult reopened = openDocument(target);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    QCOMPARE(reopened.document.pageCount(), 3);
    // 临时文件不该留下。
    QVERIFY(!QFile::exists(aipdf::doc::SafeSave::temporaryPathFor(target)));
}

void PageOperationTests::safeSaveKeepsOriginalOnBadPayload() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = directory.filePath(QStringLiteral("out.pdf"));

    // 先放一个能用的原文件。
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    QVERIFY(aipdf::doc::SafeSave::writeAtomically(target,
                                                   result.document.serialize(),
                                                   3,
                                                   false)
                .ok);

    // 写一个页数对不上的坏内容：校验必须挡住，原文件不能被动。
    const aipdf::doc::SaveResult saved =
        aipdf::doc::SafeSave::writeAtomically(target, QByteArray("not a pdf"), 3, false);
    QVERIFY(!saved.ok);
    QVERIFY(!saved.errorMessage.isEmpty());

    // 原文件还在，还是能打开的。
    const PdfOpenResult reopened = openDocument(target);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    QCOMPARE(reopened.document.pageCount(), 3);
    QVERIFY(!QFile::exists(aipdf::doc::SafeSave::temporaryPathFor(target)));
}

void PageOperationTests::safeSaveKeepsOriginalOnReadOnlyTarget() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = directory.filePath(QStringLiteral("out.pdf"));

    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    const QByteArray bytes = result.document.serialize();
    QVERIFY(!bytes.isEmpty());

    // 先放一个能用的原文件。
    QVERIFY(aipdf::doc::SafeSave::writeAtomically(target, bytes, 3, false).ok);
    const QByteArray original = [&target]() {
        QFile file(target);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    }();
    QVERIFY(!original.isEmpty());

    // 把原文件设成只读：替换它会被系统拒绝。
    QFile::setPermissions(target, QFile::ReadOwner | QFile::ReadUser);

    const aipdf::doc::SaveResult saved =
        aipdf::doc::SafeSave::writeAtomically(target, bytes, 3, false);
    // 只读目标要么写不进去，要么写进去的内容与原来一致——
    // 无论如何，原文件不能变成半截坏文件。
    if (!saved.ok) {
        QVERIFY2(!saved.errorMessage.isEmpty(), "保存失败必须给出原因");
    }
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray after = file.readAll();
    file.close();
    QCOMPARE(after, original);
    QVERIFY(!QFile::exists(aipdf::doc::SafeSave::temporaryPathFor(target)));

    // 恢复权限，免得临时目录删不掉。
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
}

void PageOperationTests::safeSaveCreatesBackup() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = directory.filePath(QStringLiteral("out.pdf"));

    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    const QByteArray bytes = result.document.serialize();
    QVERIFY(!bytes.isEmpty());

    // 第一次写：还没有原文件，没有备份。
    const aipdf::doc::SaveResult first =
        aipdf::doc::SafeSave::writeAtomically(target, bytes, 3, true);
    QVERIFY2(first.ok, qPrintable(first.errorMessage));
    QVERIFY(first.backupPath.isEmpty());

    // 第二次写：原文件在，要留下备份。
    const aipdf::doc::SaveResult second =
        aipdf::doc::SafeSave::writeAtomically(target, bytes, 3, true);
    QVERIFY2(second.ok, qPrintable(second.errorMessage));
    QVERIFY(!second.backupPath.isEmpty());
    QVERIFY(QFile::exists(second.backupPath));
    const PdfOpenResult backup = openDocument(second.backupPath);
    QVERIFY(backup.document.isValid());
}

QTEST_MAIN(PageOperationTests)
#include "PageOperationTests.moc"
