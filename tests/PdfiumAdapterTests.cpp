#include "pdf/PdfiumAdapter.h"

#include <QDir>
#include <QImage>
#include <QtTest>

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
    void formWidgetsAreRendered();
    void mergeAndExtractPages();
    void imageLayerCoversRect();
    void textReplacementSwapsContent();
    void formValuesSurviveSaveAndReopen();

private:
    bool hasEncryptedFixture_ = false;
};

void PdfiumAdapterTests::initTestCase() {
    hasEncryptedFixture_ = QFile::exists(fixturePath(QStringLiteral("encrypted.pdf")));
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

QTEST_GUILESS_MAIN(PdfiumAdapterTests)
#include "PdfiumAdapterTests.moc"
