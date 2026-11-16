#include "pdf/PdfiumAdapter.h"

#include "pdf/FormFieldWriter.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QtTest>

#include <algorithm>

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

QString goldenPath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_GOLDEN_DIR)).filePath(name);
}

bool goldenUpdateRequested() {
    return qEnvironmentVariableIsSet("AIPDF_UPDATE_GOLDEN");
}

// 统计非白色像素，用于判断页面是否真的画出了内容。
int countInkedPixels(const QImage& image) {
    int inked = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            if (qRed(pixel) != 0xFF || qGreen(pixel) != 0xFF || qBlue(pixel) != 0xFF) {
                ++inked;
            }
        }
    }
    return inked;
}

}  // namespace

class PdfiumAdapterTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void normalDocumentHasExpectedGeometry();
    void normalDocumentRendersContent();
    void emptyDocumentRendersBlank();
    void rotatedDocumentSwapsDimensions();
    void corruptDocumentFailsWithoutCrash();
    void truncatedDocumentFailsWithoutCrash();
    void missingFileIsReported();
    void encryptedDocumentNeedsPassword();
    void oversizedRenderIsRejected();
    void renderScalesWithZoom();
    void goldenImageMatches();

    void formFieldsAreEnumerated();
    void formFieldValueCanBeSet();
    void flattenRemovesFormFields();
    void textLayerChangesPixelsAndExtracts();
    void resizePageScalesContentAndBox();
    void pageBackgroundPaintsPage();
    void formWidgetsAreRendered();
    void mergeAndExtractPages();
    void backupAndRestoreDuplicatesPage();
    void deleteThenRestoreReplacesPage();
    void imageLayerCoversRect();
    void imageCanBeRemovedAgain();
    void textReplacementSwapsContent();
    void textReplacementSupportsChinese();
    void textReplacementAllowsEmptyText();
    void pageObjectsCanBeSelectedTransformedStyledAndRemoved();
    void inlineTextEditCreatesRealMultipleLines();
    void nestedFormObjectsHaveStablePathsAndCanEditText();
    void redactionRasterizesAndRemovesTextLayer();
    void formValuesSurviveSaveAndReopen();
    void checkboxAndChoiceFieldsAcceptValues();
    void createdFormFieldsAreRealWidgets();

private:
    bool hasEncryptedFixture_ = false;
};

void PdfiumAdapterTests::initTestCase() {
    hasEncryptedFixture_ = QFile::exists(fixturePath(QStringLiteral("encrypted.pdf")));
}

void PdfiumAdapterTests::pageObjectsCanBeSelectedTransformedStyledAndRemoved() {
    using namespace aipdf::pdf;
    auto opened = openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->addTextLayer(QStringLiteral("OBJECT"), 40, 40, 14, Qt::black));

    const QRectF hitArea(35, 30, 120, 40);
    QVector<PageObjectInfo> objects = page->objectsInRect(hitArea, 0.1);
    auto match = std::find_if(objects.begin(), objects.end(), [](const PageObjectInfo& info) {
        return info.type == PageObjectType::Text && info.bounds.intersects(QRectF(35, 30, 120, 40));
    });
    QVERIFY(match != objects.end());
    const int index = match->index;
    QCOMPARE(match->objectPath, QVector<int>{index});
    QCOMPARE(match->nestingDepth, 0);
    QVERIFY(match->fontWeight >= -1);
    const QRectF before = match->bounds;
    QVERIFY(page->transformObjects({index}, 12, 8, 1.25, 1.25));
    objects = page->objectsInRect(before.adjusted(-5, -5, 80, 60), 0.01);
    match = std::find_if(objects.begin(), objects.end(), [index](const PageObjectInfo& info) {
        return info.index == index;
    });
    QVERIFY(match != objects.end());
    QVERIFY(match->bounds.left() > before.left());
    QVERIFY(match->bounds.width() > before.width());
    const QRectF scaled = match->bounds;
    QVERIFY(page->rotateObjects({index}, 20.0));
    objects = page->objectsInRect(scaled.adjusted(-80, -80, 80, 80), 0.01);
    match = std::find_if(objects.begin(), objects.end(), [index](const PageObjectInfo& info) {
        return info.index == index;
    });
    QVERIFY(match != objects.end());
    QVERIFY(match->bounds.height() > scaled.height());
    QVERIFY(page->setTextObjectText(index, QStringLiteral("EDITED")));
    objects = page->objectsInRect(QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
    match = std::find_if(objects.begin(), objects.end(), [index](const PageObjectInfo& info) {
        return info.index == index && info.text.contains(QStringLiteral("EDITED"));
    });
    QVERIFY(match != objects.end());
    QVERIFY(page->setTextObjectProperties({index}, QStringLiteral("Courier"), 20, QColor(Qt::red)));
    objects = page->objectsInRect(QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
    match = std::find_if(objects.begin(), objects.end(), [](const PageObjectInfo& info) {
        return info.type == PageObjectType::Text && info.fontFamily.contains(QStringLiteral("Courier")) &&
               info.fillColor.red() > 200 && info.fillColor.green() < 80;
    });
    QVERIFY(match != objects.end());
    const int styledIndex = match->index;
    QVERIFY(page->addTextLayer(QStringLiteral("GROUP"), 90, 90, 12, Qt::black));
    objects = page->objectsInRect(QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
    const auto groupedPeer = std::find_if(objects.begin(), objects.end(), [](const PageObjectInfo& info) {
        return info.text.contains(QStringLiteral("GROUP"));
    });
    QVERIFY(groupedPeer != objects.end());
    QVERIFY(page->setObjectsGroup({styledIndex, groupedPeer->index}, 42));
    QVERIFY(page->setObjectsLocked({styledIndex, groupedPeer->index}, true));
    objects = page->objectsInRect(QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
    QCOMPARE(std::count_if(objects.cbegin(), objects.cend(), [](const PageObjectInfo& info) {
        return info.groupId == 42 && info.locked;
    }), 2);
    const QByteArray markedBytes = opened.document.serialize();
    auto markedDocument = openDocumentFromBytes(markedBytes);
    QVERIFY(markedDocument.document.isValid());
    auto markedPage = markedDocument.document.page(0);
    QVERIFY(markedPage.has_value());
    const auto persistedObjects = markedPage->objectsInRect(
        QRectF(0, 0, markedPage->widthPoints(), markedPage->heightPoints()), 0.0);
    QCOMPARE(std::count_if(persistedObjects.cbegin(), persistedObjects.cend(),
                           [](const PageObjectInfo& info) {
                               return info.groupId == 42 && info.locked;
                           }), 2);
    QVERIFY(page->setObjectsLocked({styledIndex, groupedPeer->index}, false));
    QVERIFY(page->clearObjectsGroup({styledIndex, groupedPeer->index}));
    match = std::find_if(objects.begin(), objects.end(), [styledIndex](const PageObjectInfo& info) {
        return info.index == styledIndex;
    });
    QVERIFY(match != objects.end());
    QVERIFY(page->removeObjects({match->index}));
    const QByteArray bytes = opened.document.serialize();
    QVERIFY(!bytes.isEmpty());
    QVERIFY(openDocumentFromBytes(bytes).document.isValid());
    PdfDocument sharedView = opened.document;
    QString reloadError;
    QVERIFY2(opened.document.reloadFromBytes(bytes, &reloadError), qPrintable(reloadError));
    QVERIFY(sharedView.isValid());
    QVERIFY(sharedView.page(0).has_value());
}

void PdfiumAdapterTests::inlineTextEditCreatesRealMultipleLines() {
    using namespace aipdf::pdf;
    auto opened = openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->addTextLayer(QStringLiteral("SINGLE"), 40, 140, 12, Qt::black));
    auto objects = page->objectsInRect(QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
    const auto source = std::find_if(objects.begin(), objects.end(), [](const PageObjectInfo& info) {
        return info.type == PageObjectType::Text && info.text == QStringLiteral("SINGLE");
    });
    QVERIFY(source != objects.end());
    QVERIFY(page->setTextObjectText(source->index, QStringLiteral("first line\nsecond line")));
    objects = page->objectsInRect(QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
    const auto first = std::find_if(objects.begin(), objects.end(), [](const PageObjectInfo& info) {
        return info.text == QStringLiteral("first line");
    });
    const auto second = std::find_if(objects.begin(), objects.end(), [](const PageObjectInfo& info) {
        return info.text == QStringLiteral("second line");
    });
    QVERIFY(first != objects.end());
    QVERIFY(second != objects.end());
    QVERIFY(second->bounds.top() < first->bounds.top());
    const QByteArray bytes = opened.document.serialize();
    QVERIFY(!bytes.isEmpty());
    auto reopened = openDocumentFromBytes(bytes);
    QVERIFY(reopened.document.isValid());
    const auto extracted = reopened.document.page(0)->extractText();
    QVERIFY(extracted.has_value());
    QVERIFY(extracted->plainText().contains(QStringLiteral("first line")));
    QVERIFY(extracted->plainText().contains(QStringLiteral("second line")));
}

void PdfiumAdapterTests::nestedFormObjectsHaveStablePathsAndCanEditText() {
    using namespace aipdf::pdf;
    auto opened = openDocument(fixturePath(QStringLiteral("nested-form.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    auto objects = page->objectsInRectDeep(QRectF(0, 0, 240, 160), 0.0);
    const auto nested = std::find_if(objects.begin(), objects.end(), [](const PageObjectInfo& info) {
        return info.type == PageObjectType::Text && info.text.contains(QStringLiteral("Nested text"));
    });
    QVERIFY(nested != objects.end());
    QCOMPARE(nested->objectPath.size(), 3);
    QCOMPARE(nested->nestingDepth, 2);
    QCOMPARE(nested->index, -1);
    QVERIFY(nested->bounds.left() >= 65.0);
    const QVector<int> path = nested->objectPath;
    QVERIFY(!page->setTextObjectText(path, QStringLiteral("Deep edit")));
    const QByteArray bytes = opened.document.serialize();
    QVERIFY(!bytes.isEmpty());
    auto reopened = openDocumentFromBytes(bytes);
    QVERIFY(reopened.document.isValid());
    const auto deep = reopened.document.page(0)->objectsInRectDeep(QRectF(0, 0, 240, 160), 0.0);
    QVERIFY(std::any_of(deep.begin(), deep.end(), [](const PageObjectInfo& info) {
        return info.text.contains(QStringLiteral("Nested text"));
    }));
}

void PdfiumAdapterTests::redactionRasterizesAndRemovesTextLayer() {
    using namespace aipdf::pdf;
    auto opened = openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto before = page->extractText();
    QVERIFY(before.has_value());
    QVERIFY(!before->plainText().trimmed().isEmpty());

    const MediaBox box = page->contentBox();
    const QRectF redaction(box.left + 10.0, box.bottom + 10.0, 80.0, 30.0);
    QVERIFY(page->redactByRasterizing(redaction, 2.0));
    QString error;
    const QByteArray bytes = opened.document.serialize(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    auto reopened = openDocumentFromBytes(bytes, QStringLiteral("redacted.pdf"));
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    auto redactedPage = reopened.document.page(0);
    QVERIFY(redactedPage.has_value());
    const auto after = redactedPage->extractText();
    QVERIFY(!after.has_value() || after->plainText().trimmed().isEmpty());
}

void PdfiumAdapterTests::createdFormFieldsAreRealWidgets() {
    using namespace aipdf::pdf;
    auto opened = openDocument(fixturePath(QStringLiteral("empty.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QString error;
    QByteArray bytes = opened.document.serialize(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    FormFieldInsert text;
    text.pageIndex = 0;
    text.name = QStringLiteral("customerName");
    text.toolTip = QStringLiteral("Customer name");
    text.rect = QRectF(10.0, 100.0, 90.0, 20.0);
    text.kind = FormFieldKind::Text;
    auto written = appendFormField(bytes, text);
    QVERIFY2(written.ok, qPrintable(written.error));

    FormFieldInsert check;
    check.pageIndex = 0;
    check.name = QStringLiteral("accepted");
    check.rect = QRectF(10.0, 70.0, 20.0, 20.0);
    check.kind = FormFieldKind::CheckBox;
    written = appendFormField(written.bytes, check);
    QVERIFY2(written.ok, qPrintable(written.error));

    const struct {
        const char* name;
        FormFieldKind kind;
        int expectedType;
    } remaining[] = {
        {"choice", FormFieldKind::RadioButton, 3},
        {"country", FormFieldKind::ComboBox, 4},
        {"items", FormFieldKind::ListBox, 5},
        {"submit", FormFieldKind::PushButton, 1},
        {"code", FormFieldKind::Barcode, 6},
    };
    double y = 45.0;
    for (const auto& item : remaining) {
        FormFieldInsert insert;
        insert.pageIndex = 0;
        insert.name = QString::fromLatin1(item.name);
        insert.rect = QRectF(40.0, y, 70.0, 18.0);
        insert.kind = item.kind;
        if (item.kind == FormFieldKind::ComboBox || item.kind == FormFieldKind::ListBox)
            insert.options = {QStringLiteral("Alpha"), QStringLiteral("Beta")};
        written = appendFormField(written.bytes, insert);
        QVERIFY2(written.ok, qPrintable(written.error));
        y -= 5.0;
    }

    auto verified = openDocumentFromBytes(written.bytes);
    QVERIFY2(verified.document.isValid(), qPrintable(verified.errorMessage));
    const QVector<FormField> fields = verified.document.formFields();
    QCOMPARE(fields.size(), 7);
    QCOMPARE(fields.at(0).name, QStringLiteral("customerName"));
    QCOMPARE(fields.at(0).type, 6);
    QCOMPARE(fields.at(1).name, QStringLiteral("accepted"));
    QCOMPARE(fields.at(1).type, 2);
    for (int i = 0; i < std::size(remaining); ++i) {
        QCOMPARE(fields.at(i + 2).name, QString::fromLatin1(remaining[i].name));
        QCOMPARE(fields.at(i + 2).type, remaining[i].expectedType);
    }
    QCOMPARE(fields.at(3).options, QVector<QString>({QStringLiteral("Alpha"),
                                                     QStringLiteral("Beta")}));
    QCOMPARE(fields.at(4).options, QVector<QString>({QStringLiteral("Alpha"),
                                                     QStringLiteral("Beta")}));
    QString optionSerializeError;
    const QByteArray optionSource = verified.document.serialize(&optionSerializeError);
    QVERIFY2(!optionSource.isEmpty(), qPrintable(optionSerializeError));
    const FormFieldWriteResult optionsUpdated = updateFormFieldOptions(
        optionSource, QStringLiteral("country"),
        {QStringLiteral("Gamma"), QStringLiteral("Delta")});
    QVERIFY2(optionsUpdated.ok, qPrintable(optionsUpdated.error));
    auto optionsVerified = openDocumentFromBytes(optionsUpdated.bytes);
    QVERIFY2(optionsVerified.document.isValid(), qPrintable(optionsVerified.errorMessage));
    const QVector<FormField> updatedOptionFields = optionsVerified.document.formFields();
    QCOMPARE(updatedOptionFields.at(3).options,
             QVector<QString>({QStringLiteral("Gamma"), QStringLiteral("Delta")}));
    verified.document = std::move(optionsVerified.document);
    QVERIFY(verified.document.setFormFieldProperties(0, QStringLiteral("customerName"),
                                                     QStringLiteral("customerName"),
                                                     QStringLiteral("Your full name"),
                                                     false, true, true));
    QVERIFY(verified.document.setFormFieldValue(0, QStringLiteral("customerName"),
                                                 QStringLiteral("Alice")));
    QVERIFY(verified.document.setFormFieldValue(0, QStringLiteral("accepted"),
                                                 QStringLiteral("Yes")));
    QString saveError;
    const QByteArray saved = verified.document.serialize(&saveError);
    QVERIFY2(!saved.isEmpty(), qPrintable(saveError));
    auto savedAgain = openDocumentFromBytes(saved);
    QVERIFY2(savedAgain.document.isValid(), qPrintable(savedAgain.errorMessage));
    const QVector<FormField> savedFields = savedAgain.document.formFields();
    QCOMPARE(savedFields.size(), 7);
    QCOMPARE(savedFields.at(0).name, QStringLiteral("customerName"));
    QCOMPARE(savedFields.at(0).value, QStringLiteral("Alice"));
    QCOMPARE(savedFields.at(0).toolTip, QStringLiteral("Your full name"));
    QVERIFY(savedFields.at(0).required);
    QVERIFY(savedFields.at(0).multiline);
}

void PdfiumAdapterTests::normalDocumentHasExpectedGeometry() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY2(result.document.isValid(), qPrintable(result.errorMessage));
    QCOMPARE(result.document.pageCount(), 1);

    const auto page = result.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->widthPoints(), 200.0);
    QCOMPARE(page->heightPoints(), 100.0);
    QVERIFY(!result.document.page(1).has_value());
    QVERIFY(!result.document.page(-1).has_value());
}

void PdfiumAdapterTests::normalDocumentRendersContent() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    const QImage image = page->render(1.0);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 200);
    QCOMPARE(image.height(), 100);
    QVERIFY2(countInkedPixels(image) > 0, "首页没有渲染出任何内容");
}

void PdfiumAdapterTests::emptyDocumentRendersBlank() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("empty.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    const QImage image = page->render(1.0);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 120);
    QCOMPARE(image.height(), 160);
    QCOMPARE(countInkedPixels(image), 0);
}

void PdfiumAdapterTests::rotatedDocumentSwapsDimensions() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("rotated.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    QCOMPARE(page->rotationDegrees(), 90);
    // PDFium 的 FPDF_GetPageWidthF 返回的是**已应用 /Rotate** 的尺寸。
    QCOMPARE(page->widthPoints(), 100.0);
    QCOMPARE(page->heightPoints(), 200.0);
    // 显示尺寸与之一致。
    QCOMPARE(page->displayWidthPoints(), 100.0);
    QCOMPARE(page->displayHeightPoints(), 200.0);

    // 渲染：PDFium 自己会应用 /Rotate，位图尺寸就是显示尺寸。
    const QImage image = page->render(1.0);
    QCOMPARE(image.width(), 100);
    QCOMPARE(image.height(), 200);

    // 再叠加用户 90 度旋转，宽高互换。
    const QImage rotated = page->render(1.0, 1);
    QCOMPARE(rotated.width(), 200);
    QCOMPARE(rotated.height(), 100);
}

void PdfiumAdapterTests::corruptDocumentFailsWithoutCrash() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("corrupt.pdf")));
    QVERIFY(!result.document.isValid());
    QVERIFY(!result.errorMessage.isEmpty());
    QCOMPARE(result.document.pageCount(), 0);
}

void PdfiumAdapterTests::truncatedDocumentFailsWithoutCrash() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("truncated.pdf")));
    if (result.document.isValid()) {
        // PDFium 能修复部分截断文件：至少要能正常取页，不崩溃。
        QVERIFY(result.document.pageCount() >= 1);
        QVERIFY(result.document.page(0).has_value());
        return;
    }
    QVERIFY(!result.errorMessage.isEmpty());
}

void PdfiumAdapterTests::missingFileIsReported() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("does-not-exist.pdf")));
    QVERIFY(!result.document.isValid());
    QVERIFY(result.errorMessage.contains(QStringLiteral("不存在")));
}

void PdfiumAdapterTests::encryptedDocumentNeedsPassword() {
    if (!hasEncryptedFixture_) {
        QSKIP("缺少 encrypted.pdf（需要 pypdf 生成）");
    }

    const auto denied = aipdf::pdf::openDocument(fixturePath(QStringLiteral("encrypted.pdf")));
    QVERIFY(!denied.document.isValid());
    QVERIFY2(denied.passwordRequired, "加密文档应当报告需要密码");

    const auto wrong =
        aipdf::pdf::openDocument(fixturePath(QStringLiteral("encrypted.pdf")), QStringLiteral("wrong"));
    QVERIFY(!wrong.document.isValid());
    QVERIFY(wrong.passwordRequired);

    const auto granted = aipdf::pdf::openDocument(
        fixturePath(QStringLiteral("encrypted.pdf")), QStringLiteral("secret"));
    QVERIFY2(granted.document.isValid(), qPrintable(granted.errorMessage));
    QCOMPARE(granted.document.pageCount(), 1);
}

void PdfiumAdapterTests::oversizedRenderIsRejected() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    // 超过像素上限时必须返回空图，而不是分配超大内存。
    QVERIFY(page->render(100000.0).isNull());
    QVERIFY(page->render(0.0).isNull());
    QVERIFY(page->render(-1.0).isNull());
}

void PdfiumAdapterTests::goldenImageMatches() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    const QImage rendered = page->render(1.0);
    QVERIFY(!rendered.isNull());

    const QString target = goldenPath(QStringLiteral("normal-page1.png"));
    if (goldenUpdateRequested()) {
        QVERIFY(rendered.save(target));
        QVERIFY(QFile::exists(target));
        return;
    }

    QVERIFY2(QFile::exists(target), qPrintable(QStringLiteral("缺少黄金图 %1").arg(target)));
    QImage golden;
    QVERIFY(golden.load(target));
    QCOMPARE(golden.size(), rendered.size());
    QCOMPARE(golden.format(), rendered.format());
    // 逐像素比较：PDFium 版本变化时需要用 AIPDF_UPDATE_GOLDEN=1 重新生成。
    QCOMPARE(countInkedPixels(golden), countInkedPixels(rendered));
    QVERIFY2(golden == rendered, "渲染结果与黄金图不一致");
}

void PdfiumAdapterTests::renderScalesWithZoom() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    // 页面是 200×100 点，zoom=1 表示 72 DPI。
    struct Case {
        double zoom;
        int width;
        int height;
    };
    const Case cases[] = {{1.0, 200, 100}, {1.5, 300, 150}, {2.0, 400, 200}};

    for (const auto& item : cases) {
        const QImage image = page->render(item.zoom);
        QVERIFY2(!image.isNull(), qPrintable(QStringLiteral("zoom=%1 渲染失败").arg(item.zoom)));
        QCOMPARE(image.width(), item.width);
        QCOMPARE(image.height(), item.height);
        // 放大后内容不能丢：墨迹像素应当随尺寸一起变多，而不是被裁掉。
        QVERIFY2(countInkedPixels(image) > 0,
                 qPrintable(QStringLiteral("zoom=%1 没有渲染出内容").arg(item.zoom)));
    }

    // 放大不能绕过安全上限。
    QVERIFY(page->render(200.0).isNull());
}

using aipdf::pdf::FormField;
using aipdf::pdf::openDocument;
using aipdf::pdf::openDocumentFromBytes;


void PdfiumAdapterTests::formFieldsAreEnumerated() {
    const auto opened = openDocument(fixturePath(QStringLiteral("form.pdf")));
    const auto& document = opened.document;
    QVERIFY(document.isValid());

    const QVector<FormField> fields = document.formFields();
    QCOMPARE(fields.size(), 4);
    QCOMPARE(fields.at(0).name, QStringLiteral("fullName"));
    QCOMPARE(fields.at(0).type, 6);   // FPDF_FORMFIELD_TEXTFIELD
    QCOMPARE(fields.at(1).name, QStringLiteral("agree"));
    QCOMPARE(fields.at(1).type, 2);   // FPDF_FORMFIELD_CHECKBOX
    QCOMPARE(fields.at(2).name, QStringLiteral("country"));
    QCOMPARE(fields.at(2).type, 4);   // FPDF_FORMFIELD_COMBOBOX
    // 选项标签：这个 PDFium 构建对 /Opt 的枚举返回 0（GetOptionCount），
    // 所以不断言 options；下拉框仍可按下标/值填写。
    QVERIFY(fields.at(0).rect.width() > 0.0);
    // 只读位：第 4 个字段是 /Ff 1 的文本域。
    QCOMPARE(fields.at(3).name, QStringLiteral("issuedBy"));
    QVERIFY(fields.at(3).readOnly);
    QVERIFY(!fields.at(0).readOnly);
}

void PdfiumAdapterTests::formFieldValueCanBeSet() {
    auto opened = openDocument(fixturePath(QStringLiteral("form.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    QVERIFY(document.setFormFieldValue(0, QStringLiteral("fullName"), QStringLiteral("张三")));
    QVERIFY(document.setFormFieldValue(0, QStringLiteral("agree"), QStringLiteral("Yes")));

    const QVector<FormField> fields = document.formFields();
    QCOMPARE(fields.at(0).value, QStringLiteral("张三"));
}

void PdfiumAdapterTests::flattenRemovesFormFields() {
    auto opened = openDocument(fixturePath(QStringLiteral("form.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    QVERIFY(!document.formFields().isEmpty());
    QVERIFY(document.flattenPage(0));
    QVERIFY2(document.formFields().isEmpty(), "扁平化之后不该再有可填字段");
}

void PdfiumAdapterTests::textLayerChangesPixelsAndExtracts() {
    auto opened = openDocument(fixturePath(QStringLiteral("empty.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    auto page = document.page(0);
    QVERIFY(page.has_value());

    const QImage before = page->render(1.0);
    QVERIFY(!before.isNull());
    QVERIFY(page->addTextLayer(QStringLiteral("CONFIDENTIAL"), 40.0, 60.0, 18.0,
                               QColor(200, 0, 0), 30.0, 0.5));
    const QImage after = page->render(1.0);
    QVERIFY(!after.isNull());
    QVERIFY(countInkedPixels(after) > countInkedPixels(before));

    // 文字层也该能被提取出来（这也解释了为什么水印会进搜索索引）。
    const auto text = page->extractText();
    QVERIFY(text.has_value());
    QVERIFY(text->plainText().contains(QStringLiteral("CONFIDENTIAL")));
}

void PdfiumAdapterTests::formWidgetsAreRendered() {
    const auto opened = openDocument(fixturePath(QStringLiteral("form.pdf")));
    const auto& document = opened.document;
    QVERIFY(document.isValid());
    const auto page = document.page(0);
    QVERIFY(page.has_value());

    // 字段的边框与输入框由 FPDF_FFLDraw 画出来；不画就是一片空白。
    const QImage image = page->render(1.0);
    QVERIFY(!image.isNull());
    QVERIFY2(countInkedPixels(image) > 50, "表单字段没有被渲染出来");
}

void PdfiumAdapterTests::mergeAndExtractPages() {
    auto destination = openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    auto source = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(destination.document.isValid());
    QVERIFY(source.document.isValid());
    const int before = destination.document.pageCount();
    const int extra = source.document.pageCount();
    QVERIFY(destination.document.appendDocumentPages(source.document));
    QCOMPARE(destination.document.pageCount(), before + extra);

    // 抽取：只留第 2 页，结果应当只有 1 页。
    auto original = openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    QVERIFY(original.document.isValid());
    const QByteArray extracted = original.document.extractPagesToBytes({ 1 });
    QVERIFY(!extracted.isEmpty());
    auto reopened = openDocumentFromBytes(extracted);
    QVERIFY(reopened.document.isValid());
    QCOMPARE(reopened.document.pageCount(), 1);
}

void PdfiumAdapterTests::imageLayerCoversRect() {
    auto opened = openDocument(fixturePath(QStringLiteral("empty.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    auto page = document.page(0);
    QVERIFY(page.has_value());

    QImage patch(20, 20, QImage::Format_RGB888);
    patch.fill(QColor(0, 0, 255));
    // empty.pdf 是 120x160 的小页面。
    QVERIFY(page->addImageLayer(QRectF(20.0, 40.0, 60.0, 60.0), patch));

    const QImage rendered = page->render(1.0);
    QVERIFY(!rendered.isNull());
    // 渲染倍率 1 时，矩形 (100,400) 对应图像里 y=400 的位置附近应是蓝色。
    const QColor sample = rendered.pixelColor(50, 160 - 70);
    QVERIFY2(sample.blue() > 150 && sample.red() < 80, "图片没有被画到页面上");

    auto objects = page->objectsInRect(QRectF(0, 0, page->widthPoints(), page->heightPoints()), 0.0);
    auto imageObject = std::find_if(objects.begin(), objects.end(), [](const auto& object) {
        return object.type == aipdf::pdf::PageObjectType::Image;
    });
    QVERIFY(imageObject != objects.end());
    const int imageIndex = imageObject->index;
    QImage replacement(30, 20, QImage::Format_ARGB32);
    replacement.fill(QColor(255, 0, 0));
    QVERIFY(page->replaceImageObject(imageIndex, replacement));
    QVERIFY(page->cropImageObject(imageIndex, QRectF(0.1, 0.1, 0.8, 0.8)));
    QVERIFY(page->setObjectOpacity({imageIndex}, 0.75));
}

void PdfiumAdapterTests::textReplacementSwapsContent() {
    auto opened = openDocument(fixturePath(QStringLiteral("text.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    auto page = document.page(0);
    QVERIFY(page.has_value());
    const auto before = page->extractText();
    QVERIFY(before.has_value());
    QVERIFY(before->plainText().contains(QStringLiteral("Folixa")));

    // text.pdf 的第一段在页面顶部；用整页上半部分作为替换范围。
    const double width = page->widthPoints();
    const double height = page->heightPoints();
    QVERIFY(page->replaceTextInRect(QRectF(0.0, height * 0.5, width, height * 0.5),
                                    QStringLiteral("REPLACED LINE"), 14.0, QColor(0, 0, 0)));

    auto updated = document.page(0);
    QVERIFY(updated.has_value());
    const auto after = updated->extractText();
    QVERIFY(after.has_value());
    QVERIFY2(after->plainText().contains(QStringLiteral("REPLACED LINE")), "新文字没写进去");
}

void PdfiumAdapterTests::textReplacementSupportsChinese() {
    const QStringList fonts{QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                            QStringLiteral("C:/Windows/Fonts/simhei.ttf"),
                            QStringLiteral("C:/Windows/Fonts/simsunb.ttf"),
                            QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
                            QStringLiteral("/System/Library/Fonts/PingFang.ttc")};
    if (std::none_of(fonts.cbegin(), fonts.cend(), [](const QString& path) {
            return QFileInfo::exists(path);
        })) {
        QSKIP("测试机没有可嵌入的 CJK 字体");
    }
    auto opened = openDocument(fixturePath(QStringLiteral("text.pdf")));
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const double width = page->widthPoints();
    const double height = page->heightPoints();
    QVERIFY(page->replaceTextInRect(QRectF(0.0, height * 0.5, width, height * 0.5),
                                    QStringLiteral("替换成功"), 14.0, QColor(0, 0, 0)));
    const auto text = page->extractText();
    QVERIFY(text.has_value());
    QVERIFY2(text->plainText().contains(QStringLiteral("替换成功")), "中文字体未正确嵌入");
}

void PdfiumAdapterTests::textReplacementAllowsEmptyText() {
    auto opened = openDocument(fixturePath(QStringLiteral("text.pdf")));
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const double width = page->widthPoints();
    const double height = page->heightPoints();
    QVERIFY(page->replaceTextInRect(QRectF(0.0, height * 0.5, width, height * 0.5),
                                    QString(), 14.0, QColor(0, 0, 0)));
    QVERIFY(!page->render(1.0).isNull());
}

void PdfiumAdapterTests::formValuesSurviveSaveAndReopen() {
    auto opened = openDocument(fixturePath(QStringLiteral("form.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    QVERIFY(document.setFormFieldValue(0, QStringLiteral("fullName"), QStringLiteral("张三")));

    QString error;
    const QByteArray bytes = document.serialize(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    auto reopened = openDocumentFromBytes(bytes);
    QVERIFY(reopened.document.isValid());
    const QVector<FormField> fields = reopened.document.formFields();
    QVERIFY(!fields.isEmpty());
    QCOMPARE(fields.at(0).name, QStringLiteral("fullName"));
    QVERIFY2(fields.at(0).value.contains(QStringLiteral("张三")),
             "填写的值没有随文档保存");
    reopened.document.flattenPage(0);
    QVERIFY(reopened.document.formFields().isEmpty());
}

void PdfiumAdapterTests::imageCanBeRemovedAgain() {
    auto opened = openDocument(fixturePath(QStringLiteral("empty.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    auto page = document.page(0);
    QVERIFY(page.has_value());
    const int before = countInkedPixels(page->render(1.0));

    QImage patch(20, 20, QImage::Format_RGB888);
    patch.fill(QColor(0, 0, 255));
    QVERIFY(page->addImageLayer(QRectF(20.0, 40.0, 60.0, 60.0), patch));
    QVERIFY(countInkedPixels(page->render(1.0)) > before);

    QVERIFY(page->removeImagesInRect(QRectF(20.0, 40.0, 60.0, 60.0)));
    QCOMPARE(countInkedPixels(page->render(1.0)), before);
}


void PdfiumAdapterTests::resizePageScalesContentAndBox() {
    auto opened = openDocument(fixturePath(QStringLiteral("normal.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    auto page = document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->contentBox().right - page->contentBox().left, 200.0);

    QVERIFY(page->resizePage(400.0, 200.0, true));
    const auto box = page->contentBox();
    QVERIFY(qAbs((box.right - box.left) - 400.0) < 0.5);
    QVERIFY(qAbs((box.top - box.bottom) - 200.0) < 0.5);
    const QImage image = page->render(1.0);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 400);
}

void PdfiumAdapterTests::pageBackgroundPaintsPage() {
    auto opened = openDocument(fixturePath(QStringLiteral("empty.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    auto page = document.page(0);
    QVERIFY(page.has_value());
    const QImage before = page->render(1.0);
    QVERIFY(!before.isNull());

    QVERIFY(page->setPageBackground(QColor(255, 249, 224)));
    // PDFium 的已加载页面对象在 GenerateContent 之后不会立刻反映到渲染上；
    // 落盘再重开才是权威结果（界面走的也是同步字节后的副本）。
    QString error;
    const QByteArray bytes = document.serialize(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    const auto reopened = openDocumentFromBytes(bytes);
    QVERIFY(reopened.document.isValid());
    const auto reopenedPage = reopened.document.page(0);
    QVERIFY(reopenedPage.has_value());
    const QImage after = reopenedPage->render(1.0);
    QVERIFY(!after.isNull());
    QVERIFY(countInkedPixels(after) > countInkedPixels(before));
    const QRgb corner = after.pixel(after.width() / 2, after.height() / 2);
    QVERIFY(qRed(corner) > 240 && qGreen(corner) > 230 && qBlue(corner) < 245);
}


void PdfiumAdapterTests::backupAndRestoreDuplicatesPage() {
    auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    const int before = document.pageCount();
    const auto backup = document.backupPage(1);
    QVERIFY(backup.isValid());
    QCOMPARE(backup.pageCount(), 1);
    QVERIFY(document.restorePageFrom(backup, 1));
    QCOMPARE(document.pageCount(), before + 1);
    const auto page = document.page(1);
    QVERIFY(page.has_value());
    QVERIFY(!page->render(1.0).isNull());
}

void PdfiumAdapterTests::deleteThenRestoreReplacesPage() {
    auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    const int before = document.pageCount();
    const auto original = document.backupPage(0);
    QVERIFY(original.isValid());
    const auto replacement = document.backupPage(1);
    QVERIFY(replacement.isValid());

    // 替换 = 删掉当前位置 + 把另一页恢复到同一位置。
    QVERIFY(document.deletePage(0));
    QCOMPARE(document.pageCount(), before - 1);
    QVERIFY(document.restorePageFrom(replacement, 0));
    QCOMPARE(document.pageCount(), before);

    // 撤销 = 删掉替换页 + 把原页恢复回来。
    QVERIFY(document.deletePage(0));
    QVERIFY(document.restorePageFrom(original, 0));
    QCOMPARE(document.pageCount(), before);
    const auto page = document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(!page->render(1.0).isNull());
}

void PdfiumAdapterTests::checkboxAndChoiceFieldsAcceptValues() {
    auto opened = openDocument(fixturePath(QStringLiteral("form.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());

    // 复选框写 /AS（外观状态），下拉框写 /V（选中值）。
    QVERIFY(document.setFormFieldValue(0, QStringLiteral("agree"), QStringLiteral("Yes")));
    QVERIFY(document.setFormFieldValue(0, QStringLiteral("country"), QStringLiteral("Other")));

    auto reopened = openDocumentFromBytes(document.serialize());
    QVERIFY(reopened.document.isValid());
    const QVector<FormField> fields = reopened.document.formFields();
    QString agree;
    QString country;
    for (const FormField& field : fields) {
        if (field.name == QStringLiteral("agree")) {
            agree = field.value;
        } else if (field.name == QStringLiteral("country")) {
            country = field.value;
        }
    }
    QVERIFY2(agree.contains(QStringLiteral("Yes")), qPrintable(agree));
    QVERIFY2(country.contains(QStringLiteral("Other")), qPrintable(country));
}

QTEST_GUILESS_MAIN(PdfiumAdapterTests)
#include "PdfiumAdapterTests.moc"
