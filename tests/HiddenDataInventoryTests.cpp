// 隐藏数据逐项清理（枚举 + 选择性清理）的单元测试。
//
// 盯的是欠账 G3 的验收口径：「用含全部隐藏向量的夹具逐项证明清除、未选择项保留」。
//
// 为什么夹具要自己造：现成的夹具里没有哪一份同时带齐元数据、嵌入文件、
// JavaScript、缩略图、不可见文字层、隐藏注释……，没有这样的夹具就证明不了
// 「勾了哪类才动哪类」。
//
// 断言的形状：枚举要能说清「凭什么算这一类」，清理要能说清「勾了哪类就只动哪类」。

#include "pdf/PdfSecurity.h"
#include "pdf/PdfiumAdapter.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QStringList>
#include <QTest>
#include <QVector>

#include <memory>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

using aipdf::pdf::enumerateHiddenData;
using aipdf::pdf::HiddenDataCategory;
using aipdf::pdf::sanitizeHiddenDataSelective;

namespace {

// 各类别 id。顺序与领域层的清单一致——顺序变了，界面的勾选项位置就变了。
const QStringList kAllCategoryIds = {
    QStringLiteral("metadata"),
    QStringLiteral("incremental-history"),
    QStringLiteral("embedded-files"),
    QStringLiteral("file-attachments"),
    QStringLiteral("scripts"),
    QStringLiteral("actions"),
    QStringLiteral("annotation-actions"),
    QStringLiteral("field-actions"),
    QStringLiteral("form-data"),
    QStringLiteral("thumbnails"),
    QStringLiteral("annotations"),
    QStringLiteral("invisible-text"),
    QStringLiteral("layers"),
    QStringLiteral("xfa"),
    QStringLiteral("signatures"),
    QStringLiteral("metadata-streams"),
};

// 好认的标记：输出字节里搜到它，就说明那类隐藏数据还在。
const QByteArray kPayload = QByteArrayLiteral("EMBEDDED-PAYLOAD-MARKER-12345");
const QByteArray kOriginalTitle = QByteArrayLiteral("ORIGINAL-TITLE-MARKER");
const QByteArray kRevisedTitle = QByteArrayLiteral("REVISED-TITLE-MARKER");
const QByteArray kFormValue = QByteArrayLiteral("FORM-VALUE-MARKER");
const QByteArray kSignatureValue = QByteArrayLiteral("SIGNATURE-VALUE-MARKER");
const QByteArray kXfaMarker = QByteArrayLiteral("XFA-FORM-MARKER");
const QByteArray kInvisibleText = QByteArrayLiteral("INVISIBLE-OCR-TEXT-MARKER");
const QByteArray kHiddenAnnotation = QByteArrayLiteral("HIDDEN-ANNOTATION-MARKER");

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

QByteArray readFixture(const QString& name) {
    QFile file(fixturePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// 某一类的条数。找不到这一类就当成 -1（这样「清单里少了一类」不会被当成 0 蒙混过去）。
int countFor(const QVector<HiddenDataCategory>& categories, const QString& id) {
    for (const HiddenDataCategory& category : categories) {
        if (category.id == id) {
            return category.itemCount;
        }
    }
    return -1;
}

#ifdef AIPDF_WITH_QPDF

// 造一份「每一类隐藏数据都有」的 PDF。
//
// 为什么逐项手工搭，而不是改现成夹具：现成夹具没有不可见文字层，也没有 XFA
// 与签名；这些向量得自己放进去，测试才有意义。
QByteArray buildRichPdf() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    if (source.isEmpty()) {
        return {};
    }

    QPDF pdf;
    pdf.processMemoryFile("rich-source.pdf", source.constData(),
                          static_cast<size_t>(source.size()));

    QPDFObjectHandle root = pdf.getRoot();
    QPDFObjectHandle page = pdf.getAllPages().at(0);

    const auto makeRect = []() {
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newInteger(0));
        rect.appendItem(QPDFObjectHandle::newInteger(0));
        rect.appendItem(QPDFObjectHandle::newInteger(120));
        rect.appendItem(QPDFObjectHandle::newInteger(20));
        return rect;
    };
    const auto appendAnnot = [&pdf](QPDFObjectHandle& pageDict, const QPDFObjectHandle& annot) {
        QPDFObjectHandle annots = pageDict.getKey("/Annots");
        if (!annots.isArray()) {
            annots = QPDFObjectHandle::newArray();
            pageDict.replaceKey("/Annots", annots);
        }
        annots.appendItem(annot);
    };
    const auto makeJsAction = [&pdf](const char* code) {
        QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
        action.replaceKey("/S", QPDFObjectHandle::newName("/JavaScript"));
        action.replaceKey("/JS", QPDFObjectHandle::newString(code));
        return pdf.makeIndirectObject(action);
    };

    // embedded-files：名称树里的嵌入文件本体。
    const QPDFEFStreamObjectHelper efStream =
        QPDFEFStreamObjectHelper::createEFStream(pdf, kPayload.toStdString());
    const QPDFFileSpecObjectHelper fileSpec =
        QPDFFileSpecObjectHelper::createFileSpec(pdf, "payload.txt", efStream);
    QPDFEmbeddedFileDocumentHelper::get(pdf).replaceEmbeddedFile("payload.txt", fileSpec);

    // file-attachments：页面上的 /FileAttachment 批注（第二条引用路径）。
    QPDFObjectHandle attachment = QPDFObjectHandle::newDictionary();
    attachment.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    attachment.replaceKey("/Subtype", QPDFObjectHandle::newName("/FileAttachment"));
    attachment.replaceKey("/Rect", makeRect());
    attachment.replaceKey("/FS", fileSpec.getObjectHandle());
    appendAnnot(page, pdf.makeIndirectObject(attachment));

    // scripts：/Names /JavaScript 名称树。
    QPDFObjectHandle names = root.getKey("/Names");
    QPDFObjectHandle jsArray = QPDFObjectHandle::newArray();
    jsArray.appendItem(QPDFObjectHandle::newString("script1"));
    jsArray.appendItem(pdf.makeIndirectObject(pdf.newStream("app.alert('hidden script');")));
    QPDFObjectHandle jsTree = QPDFObjectHandle::newDictionary();
    jsTree.replaceKey("/Names", pdf.makeIndirectObject(jsArray));
    names.replaceKey("/JavaScript", pdf.makeIndirectObject(jsTree));

    // metadata：trailer 的 /Info 文档信息字典。
    QPDFObjectHandle info = QPDFObjectHandle::newDictionary();
    info.replaceKey("/Title", QPDFObjectHandle::newString(kOriginalTitle.toStdString()));
    info.replaceKey("/Author", QPDFObjectHandle::newString("Hidden Author Marker"));
    info.replaceKey("/Producer", QPDFObjectHandle::newString("Hidden Producer Marker"));
    pdf.getTrailer().replaceKey("/Info", pdf.makeIndirectObject(info));

    // metadata-streams：catalog 的 /Metadata（XMP）流。
    QPDFObjectHandle xmp = pdf.newStream("<x:xmpmeta>METADATA-STREAM-MARKER</x:xmpmeta>");
    xmp.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/Metadata"));
    xmp.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
    root.replaceKey("/Metadata", pdf.makeIndirectObject(xmp));

    // actions：catalog 的 /OpenAction、catalog 的 /AA、页面的 /AA。
    root.replaceKey("/OpenAction", makeJsAction("app.alert('open action');"));
    QPDFObjectHandle catalogAA = QPDFObjectHandle::newDictionary();
    catalogAA.replaceKey("/WC", makeJsAction("app.alert('close action');"));
    root.replaceKey("/AA", pdf.makeIndirectObject(catalogAA));
    QPDFObjectHandle pageAA = QPDFObjectHandle::newDictionary();
    pageAA.replaceKey("/O", makeJsAction("app.alert('page open action');"));
    page.replaceKey("/AA", pdf.makeIndirectObject(pageAA));

    // form-data：一个填过值的文本域。
    //
    // 它同时是 field-actions 的载体：表单域上的 /AA 是校验、格式化脚本的挂点，
    // 与普通批注的 /AA 是两回事，必须能被分开清。
    QPDFObjectHandle textField = QPDFObjectHandle::newDictionary();
    textField.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    textField.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    textField.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
    textField.replaceKey("/T", QPDFObjectHandle::newString("secretField"));
    textField.replaceKey("/V", QPDFObjectHandle::newString(kFormValue.toStdString()));
    textField.replaceKey("/F", QPDFObjectHandle::newInteger(4));
    textField.replaceKey("/Rect", makeRect());
    textField.replaceKey("/DA", QPDFObjectHandle::newString("/Helv 12 Tf 0 g"));
    QPDFObjectHandle fieldAA = QPDFObjectHandle::newDictionary();
    fieldAA.replaceKey("/K", makeJsAction("app.alert('keystroke validation');"));
    textField.replaceKey("/AA", pdf.makeIndirectObject(fieldAA));
    const QPDFObjectHandle textFieldIndirect = pdf.makeIndirectObject(textField);
    appendAnnot(page, textFieldIndirect);

    // signatures：一个签过名的签名字段。
    QPDFObjectHandle signatureValue = QPDFObjectHandle::newDictionary();
    signatureValue.replaceKey("/Type", QPDFObjectHandle::newName("/Sig"));
    signatureValue.replaceKey("/Filter", QPDFObjectHandle::newName("/Adobe.PPKLite"));
    signatureValue.replaceKey("/SubFilter", QPDFObjectHandle::newName("/adbe.pkcs7.detached"));
    signatureValue.replaceKey("/Contents", QPDFObjectHandle::newString(kSignatureValue.toStdString()));
    QPDFObjectHandle signatureField = QPDFObjectHandle::newDictionary();
    signatureField.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    signatureField.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    signatureField.replaceKey("/FT", QPDFObjectHandle::newName("/Sig"));
    signatureField.replaceKey("/T", QPDFObjectHandle::newString("signature1"));
    signatureField.replaceKey("/V", pdf.makeIndirectObject(signatureValue));
    signatureField.replaceKey("/F", QPDFObjectHandle::newInteger(4));
    signatureField.replaceKey("/Rect", makeRect());
    const QPDFObjectHandle signatureFieldIndirect = pdf.makeIndirectObject(signatureField);
    appendAnnot(page, signatureFieldIndirect);

    // xfa：/AcroForm /XFA。
    QPDFObjectHandle xfaStream = pdf.newStream(std::string("<xdp:xdp>") +
                                               kXfaMarker.toStdString() + "</xdp:xdp>");
    QPDFObjectHandle acroForm = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle fieldArray = QPDFObjectHandle::newArray();
    fieldArray.appendItem(textFieldIndirect);
    fieldArray.appendItem(signatureFieldIndirect);
    acroForm.replaceKey("/Fields", pdf.makeIndirectObject(fieldArray));
    acroForm.replaceKey("/SigFlags", QPDFObjectHandle::newInteger(3));
    acroForm.replaceKey("/XFA", pdf.makeIndirectObject(xfaStream));
    root.replaceKey("/AcroForm", pdf.makeIndirectObject(acroForm));

    // thumbnails：页面 /Thumb 与 /Resources 里 /Thumb1 命名的图像。
    QPDFObjectHandle thumbStream = pdf.newStream(std::string(1, '\x01'));
    thumbStream.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    thumbStream.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    thumbStream.getDict().replaceKey("/Width", QPDFObjectHandle::newInteger(1));
    thumbStream.getDict().replaceKey("/Height", QPDFObjectHandle::newInteger(1));
    thumbStream.getDict().replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceGray"));
    thumbStream.getDict().replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    const QPDFObjectHandle thumbIndirect = pdf.makeIndirectObject(thumbStream);
    page.replaceKey("/Thumb", thumbIndirect);
    QPDFObjectHandle resources = page.getKey("/Resources");
    QPDFObjectHandle xobjects = resources.getKey("/XObject");
    if (!xobjects.isDictionary()) {
        xobjects = QPDFObjectHandle::newDictionary();
        resources.replaceKey("/XObject", pdf.makeIndirectObject(xobjects));
    }
    xobjects.replaceKey("/Thumb1", thumbIndirect);

    // annotations：一个隐藏批注（/F 2 = 不显示）+ 一个可见批注（/F 4）。
    QPDFObjectHandle hiddenAnnot = QPDFObjectHandle::newDictionary();
    hiddenAnnot.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    hiddenAnnot.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
    hiddenAnnot.replaceKey("/F", QPDFObjectHandle::newInteger(2));
    hiddenAnnot.replaceKey("/Rect", makeRect());
    hiddenAnnot.replaceKey("/Contents", QPDFObjectHandle::newString(kHiddenAnnotation.toStdString()));
    appendAnnot(page, pdf.makeIndirectObject(hiddenAnnot));

    QPDFObjectHandle visibleAnnot = QPDFObjectHandle::newDictionary();
    visibleAnnot.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    visibleAnnot.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
    visibleAnnot.replaceKey("/F", QPDFObjectHandle::newInteger(4));
    visibleAnnot.replaceKey("/Rect", makeRect());
    visibleAnnot.replaceKey("/Contents", QPDFObjectHandle::newString("VISIBLE-ANNOTATION-MARKER"));
    // annotation-actions：可见批注上的 /AA（进入时触发的动作）。
    //
    // 可见批注不是「隐藏批注」，但它身上的自动动作是——点开就跑的东西不该留着。
    QPDFObjectHandle visibleAnnotAA = QPDFObjectHandle::newDictionary();
    visibleAnnotAA.replaceKey("/E", makeJsAction("app.alert('annotation enter');"));
    visibleAnnot.replaceKey("/AA", pdf.makeIndirectObject(visibleAnnotAA));
    appendAnnot(page, pdf.makeIndirectObject(visibleAnnot));

    // invisible-text：不可见渲染模式（Tr 3）下的文字层。
    const std::shared_ptr<Buffer> decoded = page.getKey("/Contents").getStreamData();
    std::string content;
    if (decoded) {
        content.assign(reinterpret_cast<const char*>(decoded->getBuffer()),
                      static_cast<size_t>(decoded->getSize()));
    }
    content += "\nBT /F1 12 Tf 3 Tr (";
    content += kInvisibleText.toStdString();
    content += ") Tj ET\n";
    page.replaceKey("/Contents", pdf.makeIndirectObject(pdf.newStream(content)));

    // layers：可选内容组（图层）配置。
    //
    // 故意不在正文里放 /OC 引用：夹具要证明的是「图层配置本身」这一类隐藏数据，
    // 正文里少一层引用不影响判定，反而让清理后的渲染结果更确定。
    QPDFObjectHandle ocg = QPDFObjectHandle::newDictionary();
    ocg.replaceKey("/Type", QPDFObjectHandle::newName("/OCG"));
    ocg.replaceKey("/Name", QPDFObjectHandle::newString("Hidden Layer Marker"));
    const QPDFObjectHandle ocgIndirect = pdf.makeIndirectObject(ocg);
    QPDFObjectHandle ocgArray = QPDFObjectHandle::newArray();
    ocgArray.appendItem(ocgIndirect);
    QPDFObjectHandle defaultConfig = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle offArray = QPDFObjectHandle::newArray();
    offArray.appendItem(ocgIndirect);
    defaultConfig.replaceKey("/OFF", pdf.makeIndirectObject(offArray));
    defaultConfig.replaceKey("/BaseState", QPDFObjectHandle::newName("/ON"));
    QPDFObjectHandle ocProperties = QPDFObjectHandle::newDictionary();
    ocProperties.replaceKey("/OCGs", pdf.makeIndirectObject(ocgArray));
    ocProperties.replaceKey("/D", pdf.makeIndirectObject(defaultConfig));
    root.replaceKey("/OCProperties", pdf.makeIndirectObject(ocProperties));
    QPDFObjectHandle properties = QPDFObjectHandle::newDictionary();
    properties.replaceKey("/OC1", ocgIndirect);
    resources.replaceKey("/Properties", pdf.makeIndirectObject(properties));

    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    // 不压缩流、不用对象流：上面的标记串要能在字节里直接搜到，
    // 「输入里有、输出里没有」这个断言才不是空转。
    writer.setCompressStreams(false);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (!buffer || buffer->getSize() == 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                      static_cast<int>(buffer->getSize()));
}

// 造一份「里面嵌了一份 PDF」的文档。
//
// 内层 PDF 自带 %%EOF 与 startxref：旧的「数字节里的 %%EOF」实现会把它
// 当成一次增量更新，于是增量历史多报一截。这份文档自己只有一节 xref，
// 增量历史就该是 0。
QByteArray buildPdfWithEmbeddedPdf() {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    if (source.isEmpty()) {
        return {};
    }
    QPDF pdf;
    pdf.processMemoryFile("embedded-pdf-source.pdf", source.constData(),
                          static_cast<size_t>(source.size()));
    const QPDFEFStreamObjectHelper efStream =
        QPDFEFStreamObjectHelper::createEFStream(pdf, source.toStdString());
    const QPDFFileSpecObjectHelper fileSpec =
        QPDFFileSpecObjectHelper::createFileSpec(pdf, "inner.pdf", efStream);
    QPDFEmbeddedFileDocumentHelper::get(pdf).replaceEmbeddedFile("inner.pdf", fileSpec);

    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.setCompressStreams(false);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (!buffer || buffer->getSize() == 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                      static_cast<int>(buffer->getSize()));
}

// 给一份 PDF 追加一次增量更新。
//
// 增量更新会在文件末尾接上新一版的对象 + 新的 xref + trailer（/Prev 指向上一版）。
// 旧版本的对象还留在字节里，只是 xref 不再指向它——这就是「增量历史里的旧版本
// 对象」的真实样子。这里把 /Info 重新定义一遍，于是旧标题与新标题同时存在于字节里。
QByteArray buildIncrementalPdf(const QByteArray& base) {
    if (base.isEmpty()) {
        return {};
    }
    const int startxref = base.lastIndexOf("startxref");
    if (startxref < 0) {
        return {};
    }
    int pos = startxref + 9;
    while (pos < base.size() &&
           (base.at(pos) == ' ' || base.at(pos) == '\r' || base.at(pos) == '\n')) {
        ++pos;
    }
    int previousXref = 0;
    bool havePrevious = false;
    while (pos < base.size() && base.at(pos) >= '0' && base.at(pos) <= '9') {
        previousXref = previousXref * 10 + (base.at(pos) - '0');
        ++pos;
        havePrevious = true;
    }
    if (!havePrevious) {
        return {};
    }

    int infoObject = 0;
    int rootObject = 0;
    int baseSize = 0;
    {
        QPDF pdf;
        pdf.processMemoryFile("incremental-scan.pdf", base.constData(),
                              static_cast<size_t>(base.size()));
        infoObject = pdf.getTrailer().getKey("/Info").getObjGen().getObj();
        rootObject = pdf.getRoot().getObjGen().getObj();
        baseSize = pdf.getTrailer().getKey("/Size").getIntValue();
    }
    if (infoObject <= 0 || rootObject <= 0) {
        return {};
    }

    QByteArray out = base;
    const int objectOffset = out.size();
    out += QByteArray::number(infoObject) + " 0 obj\n<< /Title (" + kRevisedTitle +
           ") /Author (REVISED-AUTHOR-MARKER) >>\nendobj\n";
    const int xrefOffset = out.size();
    out += "xref\n";
    out += QByteArray::number(infoObject) + " 1\n";
    // xref 条目必须正好 20 字节：10 位偏移 + 空格 + 5 位代数 + 空格 + 类型 + 换行。
    // 补零用 rightJustified：QString::arg 的第三个参数是**进制**不是填充字符，
    // 写错了会把偏移按 48 进制输出，qpdf 会直接判定 xref 损坏。
    QByteArray entry = QByteArray::number(objectOffset).rightJustified(10, '0');
    entry += " ";
    entry += QByteArray::number(0).rightJustified(5, '0');
    entry += " n \n";
    out += entry;
    // /Size 得沿用 base 的：写小了 qpdf 会警告「对象数与最高对象号不符」。
    out += "trailer\n<< /Size " +
           QByteArray::number(qMax(baseSize, infoObject + 1)) + " /Root " +
           QByteArray::number(rootObject) + " 0 R /Info " + QByteArray::number(infoObject) +
           " 0 R /Prev " + QByteArray::number(previousXref) + " >>\n";
    out += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return out;
}

// 文件里还挂着几个 /FileAttachment 批注。
int fileAttachmentCount(const QByteArray& bytes) {
    QPDF pdf;
    pdf.processMemoryFile("scan.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    int count = 0;
    for (const QPDFObjectHandle& page : pdf.getAllPages()) {
        const QPDFObjectHandle annots = page.getKey("/Annots");
        if (!annots.isArray()) {
            continue;
        }
        for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
            const QPDFObjectHandle subtype = annot.getKey("/Subtype");
            if (subtype.isName() && subtype.getName() == "/FileAttachment") {
                ++count;
            }
        }
    }
    return count;
}

// 文件里（不管可不可达）还有没有这个标记。
//
// 只在原始字节里搜是不够的：清理后的输出会把流压成 Flate，压过之后明文就搜不到
// 了——那样「搜不到」证明不了任何事。所以这里把文件里所有对象翻一遍：
// 流看解码后的数据，字典看 unparse 出来的文本。
bool markerSurvives(const QByteArray& bytes, const QByteArray& marker) {
    QPDF pdf;
    pdf.processMemoryFile("scan.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    for (QPDFObjectHandle object : pdf.getAllObjects()) {
        if (object.isStream()) {
            const std::shared_ptr<Buffer> data = object.getStreamData();
            if (data && data->getSize() > 0) {
                const QByteArray content(reinterpret_cast<const char*>(data->getBuffer()),
                                         static_cast<int>(data->getSize()));
                if (content.contains(marker)) {
                    return true;
                }
            }
            continue;
        }
        try {
            if (object.unparse().find(marker.toStdString()) != std::string::npos) {
                return true;
            }
        } catch (const std::exception&) {
            // unparse 不了的怪对象跳过：它本来也不是要找的标记。
        }
    }
    return false;
}

// 含全部隐藏向量的夹具：先做一份带各类隐藏数据的，再补一次增量更新。
//
// 为什么一定要带增量更新：欠账 G3 的验收要求「用含全部隐藏向量的夹具」，
// 增量历史也是隐藏向量之一，只有一份单修订的文档测不到它。
QByteArray buildFullFixture() {
    const QByteArray base = buildRichPdf();
    if (base.isEmpty()) {
        return {};
    }
    const QByteArray incremental = buildIncrementalPdf(base);
    return incremental.isEmpty() ? base : incremental;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

class HiddenDataInventoryTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void plainDocumentReportsEveryCategory();
    void richFixtureIsDetectedInEveryCategory();
    void enumerationIsReadOnly();
    void emptySelectionLeavesTheDocumentUntouched();
    void unknownCategoryIdIsRejected();
    void cleaningOneCategoryZeroesOnlyThatCategory();
    void cleaningEverythingLeavesAReadableDocument();
    void unselectedEmbeddedFilesSurvive();
    void unselectedFileAttachmentsSurvive();
    void incrementalHistoryIsDetectedAndFlattened();
    void embeddedPdfIsNotCountedAsIncrementalHistory();
    void emptyInputIsRejected();
    void brokenInputIsRejected();
    void missingQpdfIsReported();
    void encryptedDocumentNeedsPasswordToBeInspected();
    void cleaningEncryptedDocumentKeepsItEncrypted();
    void annotationAndFieldActionsAreSeparateCategories();
    void pdfiumSerializationKeepsEncryption();

private:
    bool qpdfAvailable_ = false;
};

void HiddenDataInventoryTests::initTestCase() {
    qpdfAvailable_ = aipdf::pdf::encryptionAvailable();
    if (!qpdfAvailable_) {
        QSKIP("本次构建没有启用 qpdf，隐藏数据清单不可测");
    }
    if (readFixture(QStringLiteral("normal.pdf")).isEmpty()) {
        QSKIP("缺少 normal.pdf 夹具");
    }
}

// 清单必须始终包含全部受支持的类别：界面按这个顺序渲染勾选列表，
// 少一类就意味着界面上少一个可勾的项。
void HiddenDataInventoryTests::plainDocumentReportsEveryCategory() {
    QString error;
    const QVector<HiddenDataCategory> categories =
        enumerateHiddenData(readFixture(QStringLiteral("normal.pdf")), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(categories.size(), kAllCategoryIds.size());

    for (int i = 0; i < categories.size(); ++i) {
        QCOMPARE(categories.at(i).id, kAllCategoryIds.at(i));
        // 普通文档里不该有这些隐藏数据。
        QCOMPARE(categories.at(i).itemCount, 0);
        QVERIFY2(!categories.at(i).detail.isEmpty(), "每一类都要说清凭什么算进来");
        QVERIFY2(categories.at(i).detail.toLatin1().size() ==
                     categories.at(i).detail.toUtf8().size(),
                 "detail 必须是 ASCII：领域层不给人看句子，标题由 app 层拼");
    }
}

// 含全部隐藏向量的夹具，每一类都要能被检出。
void HiddenDataInventoryTests::richFixtureIsDetectedInEveryCategory() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildFullFixture();
    QVERIFY2(!source.isEmpty(), "造不出含全部隐藏数据的夹具");

    QString error;
    const QVector<HiddenDataCategory> categories = enumerateHiddenData(source, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(categories.size(), kAllCategoryIds.size());

    for (const HiddenDataCategory& category : categories) {
        QVERIFY2(category.itemCount > 0,
                 qPrintable(QStringLiteral("夹具里 %1 这一类应该是有的").arg(category.id)));
    }
    // 可见批注不算「隐藏批注」：夹具里有一个 /F 4 的可见批注。
    QCOMPARE(countFor(categories, QStringLiteral("annotations")), 1);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 枚举是只读的：查两遍结果一样，文档字节一个也没动。
void HiddenDataInventoryTests::enumerationIsReadOnly() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildRichPdf();
    QVERIFY(!source.isEmpty());
    const QByteArray original = source;

    QString error;
    const QVector<HiddenDataCategory> first = enumerateHiddenData(source, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QVector<HiddenDataCategory> second = enumerateHiddenData(source, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QCOMPARE(second.size(), first.size());
    for (int i = 0; i < first.size(); ++i) {
        QCOMPARE(second.at(i).id, first.at(i).id);
        QCOMPARE(second.at(i).itemCount, first.at(i).itemCount);
    }
    QCOMPARE(source, original);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 空集合 = 什么都不做。原样返回的字节必须一个不差。
void HiddenDataInventoryTests::emptySelectionLeavesTheDocumentUntouched() {
    QString error;
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    const QByteArray result = sanitizeHiddenDataSelective(source, QSet<QString>(), &error);

    QCOMPARE(result, source);
    // 原因要写出来：调用方得知道「什么都没做」不是失败。
    QVERIFY2(!error.isEmpty(), "空集合必须写明原因");
    QVERIFY2(error.contains(QStringLiteral("没有勾选")), qPrintable(error));
}

// 未知 id 必须明确失败，不能静默忽略。
void HiddenDataInventoryTests::unknownCategoryIdIsRejected() {
    QString error;
    const QByteArray result = sanitizeHiddenDataSelective(
        readFixture(QStringLiteral("normal.pdf")), QSet<QString>{QStringLiteral("no-such-kind")},
        &error);

    QVERIFY(result.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("no-such-kind")), qPrintable(error));
}

// 核心验收：勾哪一类就只动哪一类。
//
// 每一类单独勾一次 → 这一类归零，其它类的条数一个都不许变。
void HiddenDataInventoryTests::cleaningOneCategoryZeroesOnlyThatCategory() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildFullFixture();
    QVERIFY2(!source.isEmpty(), "造不出含全部隐藏数据的夹具");
    const QVector<HiddenDataCategory> before = enumerateHiddenData(source);
    QCOMPARE(before.size(), kAllCategoryIds.size());

    for (const HiddenDataCategory& category : before) {
        QVERIFY2(category.itemCount > 0,
                 qPrintable(QStringLiteral("夹具里 %1 这一类应该是有的").arg(category.id)));

        QString error;
        const QByteArray cleaned =
            sanitizeHiddenDataSelective(source, QSet<QString>{category.id}, &error);
        QVERIFY2(!cleaned.isEmpty(),
                 qPrintable(QStringLiteral("清理 %1 失败：%2").arg(category.id, error)));

        const QVector<HiddenDataCategory> after = enumerateHiddenData(cleaned);
        QCOMPARE(after.size(), before.size());
        for (const HiddenDataCategory& next : after) {
            if (next.id == category.id) {
                QCOMPARE(next.itemCount, 0);
            } else if (next.id == QStringLiteral("incremental-history")) {
                // 唯一的例外：增量历史的本质是「文件里还存着旧版本」，
                // 重写文件等于只留最新版，所以不管清理哪一类它都会被顺带拍平。
                // 这是写出器的固有行为，不是清理项的效果。
                QCOMPARE(next.itemCount, 0);
            } else {
                QCOMPARE(next.itemCount, countFor(before, next.id));
            }
        }
    }
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 全勾也要留下一个能读、能搜的文档。
void HiddenDataInventoryTests::cleaningEverythingLeavesAReadableDocument() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildFullFixture();
    QVERIFY(!source.isEmpty());

    QSet<QString> everything;
    for (const QString& id : kAllCategoryIds) {
        everything.insert(id);
    }
    QString error;
    const QByteArray cleaned = sanitizeHiddenDataSelective(source, everything, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    const QVector<HiddenDataCategory> after = enumerateHiddenData(cleaned);
    for (const HiddenDataCategory& category : after) {
        QCOMPARE(category.itemCount, 0);
    }

    const auto opened = aipdf::pdf::openDocumentFromBytes(cleaned, QStringLiteral("cleaned.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());
    QVERIFY2(text->plainText().contains(QStringLiteral("Hello PDFium")),
             "清理隐藏数据之后正文没了");
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 没勾「嵌入文件」就别动它——这正是欠账里说的那个缺口。
void HiddenDataInventoryTests::unselectedEmbeddedFilesSurvive() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildRichPdf();
    QVERIFY(!source.isEmpty());
    QVERIFY(source.contains(kPayload));

    QString error;
    const QByteArray cleaned = sanitizeHiddenDataSelective(
        source, QSet<QString>{QStringLiteral("metadata")}, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    QVERIFY2(markerSurvives(cleaned, kPayload), "没勾嵌入文件，文件本体却被清掉了");
    const QVector<HiddenDataCategory> after = enumerateHiddenData(cleaned);
    QCOMPARE(countFor(after, QStringLiteral("embedded-files")), 1);
    QCOMPARE(countFor(after, QStringLiteral("file-attachments")), 1);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 没勾「附件」就别删附件批注：可见的批注与附件都不是隐藏数据。
void HiddenDataInventoryTests::unselectedFileAttachmentsSurvive() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildRichPdf();
    QVERIFY(!source.isEmpty());
    QCOMPARE(fileAttachmentCount(source), 1);

    QString error;
    const QByteArray cleaned = sanitizeHiddenDataSelective(
        source, QSet<QString>{QStringLiteral("scripts")}, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    QCOMPARE(fileAttachmentCount(cleaned), 1);
    QCOMPARE(countFor(enumerateHiddenData(cleaned), QStringLiteral("file-attachments")), 1);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 增量历史：能被数出来，重写之后旧版本的内容真的没了。
void HiddenDataInventoryTests::incrementalHistoryIsDetectedAndFlattened() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray base = buildRichPdf();
    QVERIFY(!base.isEmpty());
    const QByteArray incremental = buildIncrementalPdf(base);
    QVERIFY2(!incremental.isEmpty(), "造不出增量更新的夹具");
    QVERIFY(incremental.contains(kOriginalTitle));
    QVERIFY(incremental.contains(kRevisedTitle));

    QString error;
    const QVector<HiddenDataCategory> before = enumerateHiddenData(incremental, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(countFor(before, QStringLiteral("incremental-history")), 1);

    const QByteArray cleaned = sanitizeHiddenDataSelective(
        incremental, QSet<QString>{QStringLiteral("incremental-history")}, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    QCOMPARE(countFor(enumerateHiddenData(cleaned), QStringLiteral("incremental-history")), 0);
    QVERIFY2(!cleaned.contains(kOriginalTitle), "旧修订的内容还留在字节里");
    QVERIFY2(markerSurvives(cleaned, kRevisedTitle), "新修订的内容不该被弄丢");
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 文档里嵌了一份 PDF：内层的 %%EOF 不算增量历史。
void HiddenDataInventoryTests::embeddedPdfIsNotCountedAsIncrementalHistory() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray bytes = buildPdfWithEmbeddedPdf();
    QVERIFY2(!bytes.isEmpty(), "造不出嵌了 PDF 的夹具");
    // 字节里确实有不止一个 %%EOF：外层一份、内层一份。
    QVERIFY2(bytes.count("%%EOF") >= 2, "内层 PDF 的 %%EOF 没进字节，夹具没造对");

    QString error;
    const QVector<HiddenDataCategory> categories = enumerateHiddenData(bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // 但这份文档自己只有一节 xref：增量历史是 0，不是 1。
    QCOMPARE(countFor(categories, QStringLiteral("incremental-history")), 0);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 空输入：返回空 + 错误信息，不许静默「成功」。
void HiddenDataInventoryTests::emptyInputIsRejected() {
    QString error;
    QVERIFY(enumerateHiddenData(QByteArray(), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(sanitizeHiddenDataSelective(QByteArray(), QSet<QString>{QStringLiteral("metadata")},
                                        &error)
                .isEmpty());
    QVERIFY(!error.isEmpty());
}

// 坏输入：不能崩，也不能假装成功。
void HiddenDataInventoryTests::brokenInputIsRejected() {
    QString error;
    QVERIFY(enumerateHiddenData(QByteArrayLiteral("this is not a pdf at all"), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(sanitizeHiddenDataSelective(QByteArrayLiteral("this is not a pdf at all"),
                                        QSet<QString>{QStringLiteral("metadata")}, &error)
                .isEmpty());
    QVERIFY(!error.isEmpty());
}

// qpdf 在不在，行为都得说清楚。
//
// 本机构建启用了 qpdf，走可用路径；没启用 qpdf 的构建必须给出原因，而不是空返回。
// 两条都写在同一条用例里：用例数不因构建差异而变（跳过不算通过）。
void HiddenDataInventoryTests::missingQpdfIsReported() {
    QString error;
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    if (qpdfAvailable_) {
        QVERIFY(!enumerateHiddenData(source, &error).isEmpty());
        QVERIFY2(error.isEmpty(), qPrintable(error));
        return;
    }
    QVERIFY(enumerateHiddenData(source, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

// 加密文档：不给密码就查不了，但必须说清「这是密码的事」。
//
// 欠账 G3 的原话：「加密文档解锁后也能枚举并逐项清理」。
// 这里守的是「说不清楚」这一侧：拿不到密码时不能给一份「什么都没查出来」的
// 假清单（那会让用户以为文档是干净的），必须明确说文档加密了、要密码。
void HiddenDataInventoryTests::encryptedDocumentNeedsPasswordToBeInspected() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray plain = buildRichPdf();
    QVERIFY2(!plain.isEmpty(), "造不出夹具");

    aipdf::pdf::PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("secret");
    QString error;
    const QByteArray encrypted = aipdf::pdf::encryptPdf(plain, options, &error);
    QVERIFY2(!encrypted.isEmpty(), qPrintable(error));

    // 判定：它确实是加密文档，而且确实要密码才能打开。
    QVERIFY(aipdf::pdf::isEncryptedDocument(encrypted, nullptr));
    QVERIFY(aipdf::pdf::requiresPasswordToOpen(encrypted, nullptr));

    // 不给密码：枚举必须失败并说明要密码。
    QString noPasswordError;
    QVERIFY(enumerateHiddenData(encrypted, &noPasswordError).isEmpty());
    QVERIFY2(noPasswordError.contains(QString::fromUtf8("密码")), qPrintable(noPasswordError));

    // 密码不对：必须说密码不对，不能笼统报「打不开」。
    QString wrongError;
    QVERIFY(enumerateHiddenData(encrypted, &wrongError, QStringLiteral("wrong")).isEmpty());
    QVERIFY2(wrongError.contains(QString::fromUtf8("密码")), qPrintable(wrongError));

    // 密码对：清单出来，条数与明文那份一致。
    QString rightError;
    const QVector<HiddenDataCategory> granted =
        enumerateHiddenData(encrypted, &rightError, options.userPassword);
    QVERIFY2(rightError.isEmpty(), qPrintable(rightError));
    QCOMPARE(granted.size(), kAllCategoryIds.size());
    QVERIFY(countFor(granted, QStringLiteral("metadata")) > 0);
    QVERIFY(countFor(granted, QStringLiteral("annotation-actions")) > 0);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 加密文档清理完**仍然加密**：清成明文等于替用户拆了锁。
void HiddenDataInventoryTests::cleaningEncryptedDocumentKeepsItEncrypted() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray plain = buildRichPdf();
    QVERIFY2(!plain.isEmpty(), "造不出夹具");

    aipdf::pdf::PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("secret");
    QString error;
    const QByteArray encrypted = aipdf::pdf::encryptPdf(plain, options, &error);
    QVERIFY2(!encrypted.isEmpty(), qPrintable(error));

    const QByteArray cleaned = sanitizeHiddenDataSelective(
        encrypted, QSet<QString>{QStringLiteral("metadata")}, &error, options.userPassword);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    // 仍然加密：不带密码打不开。
    QVERIFY2(aipdf::pdf::requiresPasswordToOpen(cleaned, nullptr),
             "清理加密文档后不能把它清成明文");

    // 用同一个密码能打开，页数不变。
    const aipdf::pdf::PdfOpenResult reopened =
        aipdf::pdf::openDocumentFromBytes(cleaned, QString(), options.userPassword);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    QCOMPARE(reopened.document.pageCount(), 1);

    // 清理真的生效了，而且没勾的那几类还在。
    const QVector<HiddenDataCategory> after =
        enumerateHiddenData(cleaned, nullptr, options.userPassword);
    QCOMPARE(countFor(after, QStringLiteral("metadata")), 0);
    QVERIFY(countFor(after, QStringLiteral("scripts")) > 0);
    QVERIFY(countFor(after, QStringLiteral("annotation-actions")) > 0);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 批注动作与表单域动作必须分成两类，而且能分开清。
//
// 「清批注动作」顺手把表单的校验脚本删掉，用户会以为只是清了几个批注动作。
void HiddenDataInventoryTests::annotationAndFieldActionsAreSeparateCategories() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildRichPdf();
    QVERIFY2(!source.isEmpty(), "造不出夹具");

    QString error;
    const QVector<HiddenDataCategory> before = enumerateHiddenData(source, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(countFor(before, QStringLiteral("annotation-actions")), 1);
    QCOMPARE(countFor(before, QStringLiteral("field-actions")), 1);

    // 只清批注动作：字段动作必须原样留着。
    const QByteArray cleaned = sanitizeHiddenDataSelective(
        source, QSet<QString>{QStringLiteral("annotation-actions")}, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));
    const QVector<HiddenDataCategory> after = enumerateHiddenData(cleaned, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(countFor(after, QStringLiteral("annotation-actions")), 0);
    QCOMPARE(countFor(after, QStringLiteral("field-actions")), 1);

    // 只清字段动作：批注动作必须原样留着。
    const QByteArray fieldCleaned = sanitizeHiddenDataSelective(
        source, QSet<QString>{QStringLiteral("field-actions")}, &error);
    QVERIFY2(!fieldCleaned.isEmpty(), qPrintable(error));
    const QVector<HiddenDataCategory> fieldAfter = enumerateHiddenData(fieldCleaned, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(countFor(fieldAfter, QStringLiteral("field-actions")), 0);
    QCOMPARE(countFor(fieldAfter, QStringLiteral("annotation-actions")), 1);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// PDFium 序列化一份加密文档时，加密还在不在？
//
// 这条决定界面层能不能直接用序列化结果去保存：要是 PDFium 把加密丢了，
// 「打开加密文档 → 改一下 → 保存」就会静默把用户的密码保护拆掉。
void HiddenDataInventoryTests::pdfiumSerializationKeepsEncryption() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray plain = buildRichPdf();
    QVERIFY2(!plain.isEmpty(), "造不出夹具");

    aipdf::pdf::PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("secret");
    QString error;
    const QByteArray encrypted = aipdf::pdf::encryptPdf(plain, options, &error);
    QVERIFY2(!encrypted.isEmpty(), qPrintable(error));

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(encrypted, QString(), options.userPassword);
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));

    QString serializeError;
    const QByteArray serialized = opened.document.serialize(&serializeError);
    QVERIFY2(!serialized.isEmpty(), qPrintable(serializeError));

    // 结论写进日志：构建差异导致的两种行为都要看得见。
    const bool keepsEncryption = aipdf::pdf::requiresPasswordToOpen(serialized, nullptr);
    qInfo() << "pdfium serialization keeps encryption:" << keepsEncryption;

    // 不管保不保留，序列化结果都必须还能用同一个密码打开（内容不能丢）。
    const aipdf::pdf::PdfOpenResult reopened =
        aipdf::pdf::openDocumentFromBytes(serialized, QString(), options.userPassword);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    QCOMPARE(reopened.document.pageCount(), 1);

    // 加密要么原样保留，要么由界面层显式重新加密——绝不能是「悄悄没了」。
    // 实测（qpdf 12.4.2 + 本仓库的 PDFium）：序列化**保留**加密。
    QVERIFY2(keepsEncryption,
             "PDFium 序列化后加密丢了：界面层必须在保存时显式重新加密");
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

QTEST_GUILESS_MAIN(HiddenDataInventoryTests)
#include "HiddenDataInventoryTests.moc"