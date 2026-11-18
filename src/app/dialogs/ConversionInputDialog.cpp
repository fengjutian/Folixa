#include "app/ConversionInputDialog.h"

#include <QBuffer>
#include <QClipboard>
#include <QComboBox>
#include <QImage>
#include <QImageWriter>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include <memory>
#include <utility>

#include "app/SystemCjkFont.h"
#include "doc/convert/ConversionInputs.h"
#include "i18n/Translate.h"

namespace aipdf::app {

namespace {

// 所有界面文案都登记在这个上下文里（字面量，不能靠推断）。
//
// 翻译目录的上下文（字面量），调用形如：
//
//   i18n::tr(<aipdf::app::ConversionInputs>, <中文文案>)
//
// 为什么每一处都写全（不抽一个 text() 小工具转发）：
// scripts/check-i18n.py 是**静态扫描** src/ 里的 i18n::tr("上下文", "文案")，
// 文案必须写成字面量它才认得。经一层函数转发的话，门禁看不见这些词条——
// 翻译被删掉也不会有人提醒，等于门禁形同虚设。
//
// 为什么叫 ConversionInputs 而不是 ConversionInputDialog：这一组文案覆盖
// 三个入口（剪贴板 / 网页 / 表单文件），不只是对话框那一半。

// ============================================================ 编码 =====

// 从 Content-Type 头里取 charset。
QString charsetFromContentType(const QString& contentType) {
    const int index = contentType.indexOf(QLatin1String("charset="), 0, Qt::CaseInsensitive);
    if (index < 0) {
        return QString();
    }
    QString charset = contentType.mid(index + 8).trimmed();
    const int semicolon = charset.indexOf(QLatin1Char(';'));
    if (semicolon >= 0) {
        charset = charset.left(semicolon);
    }
    return charset.trimmed().remove(QLatin1Char('"')).trimmed();
}

// HTML 里 <meta charset="…"> 声明的编码。
QString charsetFromHtml(const QString& html) {
    const int index = html.indexOf(QLatin1String("charset="), 0, Qt::CaseInsensitive);
    if (index < 0) {
        return QString();
    }
    QString charset = html.mid(index + 8).trimmed();
    const int end = charset.indexOf(QRegularExpression(QStringLiteral("[\\s\"'>;]")));
    if (end >= 0) {
        charset = charset.left(end);
    }
    return charset.trimmed();
}

// UTF-16 解码（手工处理字节序，不依赖 QTextCodec：Qt6 里它在 Qt5Compat）。
QString decodeUtf16(const QByteArray& bytes, bool bigEndian) {
    const int count = bytes.size() / 2;
    QVector<char16_t> units(count);
    for (int index = 0; index < count; ++index) {
        const unsigned char low = static_cast<unsigned char>(bytes.at(index * 2));
        const unsigned char high = static_cast<unsigned char>(bytes.at(index * 2 + 1));
        units[index] = bigEndian ? static_cast<char16_t>((high << 8) | low)
                                 : static_cast<char16_t>((low << 8) | high);
    }
    return QString::fromUtf16(units.constData(), units.size());
}

// 把抓回来的字节解成文本。
//
// 为什么只支持 UTF-8 / UTF-16：GBK 这类编码在 Qt6 里没有内置解码器
//（QTextCodec 在 Qt5Compat 里，本程序没链它）。与其给出满屏乱码，
// 不如明说「这个网页的编码不支持」。
QString decodeWebBytes(const QByteArray& bytes, const QString& contentType, QString* error) {
    if (bytes.isEmpty()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::ConversionInputs", "这个地址返回的是空的，没有正文。");
        }
        return QString();
    }
    QByteArray payload = bytes;
    if (payload.startsWith("\xEF\xBB\xBF")) {
        payload = payload.mid(3);
    } else if (payload.startsWith("\xFF\xFE")) {
        return decodeUtf16(payload.mid(2), false);
    } else if (payload.startsWith("\xFE\xFF")) {
        return decodeUtf16(payload.mid(2), true);
    }
    const QString decoded = QString::fromUtf8(payload);
    // U+FFFD 只可能在解码失败时冒出来：合法的 UTF-8 里不会有它。
    if (decoded.contains(QChar(0xFFFD))) {
        if (error != nullptr) {
            QString declared = charsetFromContentType(contentType);
            if (declared.isEmpty()) {
                declared = charsetFromHtml(QString::fromLatin1(payload.left(2048)));
            }
            *error = declared.isEmpty()
                ? i18n::tr("aipdf::app::ConversionInputs", "这个网页不是 UTF-8 编码，正文读出来会是乱码；本程序只支持 UTF-8 / UTF-16。")
                : i18n::tr("aipdf::app::ConversionInputs", "这个网页的编码是 %1，本程序只支持 UTF-8 / UTF-16。").arg(declared);
        }
        return QString();
    }
    return decoded;
}

// ============================================================ 字体 =====

// 文本里有没有标准 14 字体显示不了的字符。
//
// Latin-1 之外（含中文、日文、韩文、西里尔、希腊）都要嵌入字体，
// 否则这些字符会变成问号——那是静默丢字。
bool needsEmbeddedFont(const QString& text) {
    for (const QChar ch : text) {
        if (ch.unicode() > 0x00FF) {
            return true;
        }
    }
    return false;
}

// 在用户机器上找一份能显示中文的 TrueType 字体。
//
// 找法与理由写在 app/SystemCjkFont.h 里（剪贴板、网页、任意文件 → PDF
// 三条路共用同一份，免得各写各的、找出不一样的字体）。
using aipdf::app::loadSystemCjkFont;

// 组装转换参数：用户指定了字体就用它；没指定但文本里有非拉丁字符，
// 就自动嵌入系统里的中文字体。
doc::UniversalConverterOptions buildTextOptions(const QString& body, const QString& fontPath,
                                                QString* usedFontPath) {
    doc::UniversalConverterOptions options;
    if (!fontPath.isEmpty()) {
        QFile file(fontPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            file.close();
            if (!bytes.isEmpty()) {
                doc::FontResource resource;
                resource.bytes = bytes;
                resource.postScriptName = QFileInfo(fontPath).completeBaseName();
                options.regularFont = resource;
                options.boldFont = resource;
                if (usedFontPath != nullptr) {
                    *usedFontPath = fontPath;
                }
                return options;
            }
        }
    }
    if (!needsEmbeddedFont(body)) {
        return options;
    }
    if (loadSystemCjkFont(&options.regularFont, usedFontPath)) {
        options.boldFont = options.regularFont;
    }
    return options;
}

// 文件名里不能有的字符。
QString sanitizeFileName(const QString& name) {
    QString cleaned = name;
    static const QString kForbidden = QStringLiteral("\\/:*?\"<>|");
    for (const QChar ch : std::as_const(kForbidden)) {
        cleaned.replace(ch, QLatin1Char('_'));
    }
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty()) {
        cleaned = i18n::tr("aipdf::app::ConversionInputs", "文档");
    }
    if (cleaned.size() > 80) {
        cleaned = cleaned.left(80);
    }
    return cleaned;
}

// 剪贴板转换失败时给用户看的中文。
//
// 为什么在 app 层拼：doc 层没有翻译上下文，给用户看的中文句子一律在这边拼。
QString clipboardErrorText(const doc::ConversionResult& result) {
    switch (result.errorKind) {
    case doc::ConversionErrorKind::NoContent:
        return i18n::tr("aipdf::app::ConversionInputs", "剪贴板里没有文本，没法生成 PDF。");
    case doc::ConversionErrorKind::NoHtmlBody:
        return i18n::tr("aipdf::app::ConversionInputs",
                        "剪贴板里的富文本没有正文（去掉标签和样式之后就没东西了），没法生成 PDF。");
    case doc::ConversionErrorKind::LayoutFailed:
    case doc::ConversionErrorKind::None:
        break;
    }
    return result.error;
}

// 系统剪贴板里有没有图片。
//
// 为什么单独问一句：剪贴板里只有图片时，本程序**转不了**
// （没有把图片排成 PDF 的能力），得说清楚是「不支持」而不是「没内容」。
bool systemClipboardHasImage() {
    if (QGuiApplication::instance() == nullptr) {
        return false;
    }
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        return false;
    }
    const QMimeData* mime = clipboard->mimeData();
    return mime != nullptr && mime->hasImage();
}

// 从系统剪贴板取图片的原始字节。
//
// 为什么按 PNG / BMP / JPEG 的**原生**格式依次取，而不是直接用 QImageReader：
// 部署包里的 Qt 图像插件只保证 gif/ico/jpeg/svg（scripts/package.ps1），
// 用 QImageReader 读 PNG 在用户机上会直接失败 —— 这正是 wave5-A 自研 PNG 解码器
// 要绕开的那个坑（所以 pdf 层根本不依赖图像插件）。
//
// 只有 QImage（Windows 的 DIB 多数走这一路）时自己编一份 JPEG：
// 部署包保证带 qjpeg 插件，编 JPEG 不会在用户机上翻车。
QVector<QByteArray> readSystemClipboardImages() {
    if (QGuiApplication::instance() == nullptr) {
        return {};
    }
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        return {};
    }
    const QMimeData* mime = clipboard->mimeData();
    if (mime == nullptr) {
        return {};
    }
    // 原生格式优先：一个字节都不用编。
    for (const char* format : {"image/png", "image/bmp", "image/jpeg"}) {
        const QByteArray data = mime->data(QLatin1String(format));
        if (!data.isEmpty()) {
            return {data};
        }
    }
    const QImage image = qvariant_cast<QImage>(mime->imageData());
    if (image.isNull()) {
        return {};
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    QImageWriter writer(&buffer, "JPEG");
    if (!writer.write(image)) {
        return {};
    }
    return {bytes};
}

// 一张图没带进来的原因（给用户看的中文）。
//
// 为什么在 app 层拼：doc 层没有翻译上下文，给用户看的中文句子一律在这边拼。
QString imageIssueReasonText(const aipdf::doc::HtmlImageIssue& issue) {
    switch (issue.kind) {
    case aipdf::doc::HtmlImageIssueKind::DownloadFailed:
        return i18n::tr("aipdf::app::ConversionInputs", "下载失败");
    case aipdf::doc::HtmlImageIssueKind::UnsupportedFormat:
        return i18n::tr("aipdf::app::ConversionInputs", "格式不支持");
    case aipdf::doc::HtmlImageIssueKind::DecodeFailed:
        return i18n::tr("aipdf::app::ConversionInputs", "数据损坏，解不开");
    case aipdf::doc::HtmlImageIssueKind::TooLarge:
        return i18n::tr("aipdf::app::ConversionInputs", "图片太大，一页放不下");
    case aipdf::doc::HtmlImageIssueKind::LayoutFailed:
        return i18n::tr("aipdf::app::ConversionInputs", "排不进这份 PDF");
    case aipdf::doc::HtmlImageIssueKind::LimitReached:
        return i18n::tr("aipdf::app::ConversionInputs", "页面里的图太多，超出了单次上限");
    }
    return issue.detail;
}

// 一张剪贴板图片没带进来的原因（给用户看的中文）。
QString clipboardImageIssueText(const aipdf::doc::ClipboardImageIssue& issue) {
    using aipdf::doc::ClipboardImageIssueKind;
    switch (issue.kind) {
    case ClipboardImageIssueKind::EmptyData:
        return i18n::tr("aipdf::app::ConversionInputs", "没有内容");
    case ClipboardImageIssueKind::UnsupportedFormat:
        return i18n::tr("aipdf::app::ConversionInputs", "格式不支持（%1）").arg(issue.format);
    case ClipboardImageIssueKind::DecodeFailed:
        return i18n::tr("aipdf::app::ConversionInputs", "数据损坏，解不开");
    case ClipboardImageIssueKind::TooLarge:
        return i18n::tr("aipdf::app::ConversionInputs", "图片太大，一页放不下");
    case ClipboardImageIssueKind::LimitReached:
        return i18n::tr("aipdf::app::ConversionInputs", "这次带的图片太多，超出了单次上限");
    case ClipboardImageIssueKind::LayoutFailed:
        return i18n::tr("aipdf::app::ConversionInputs", "排不进这份 PDF");
    }
    return issue.detail;
}

// 剪贴板 → PDF 的结果。
//
// 对话框与「一键转换」两个入口共用这一份：写两遍的话，
// 两边的失败原因迟早会说得不一样。
//
// html 非空时走**富文本**排版：剪贴板里从 Word / 网页复制过来的内容带 HTML，
// 只取纯文本会把标题、段落、列表结构全丢掉。
bool buildClipboardOutcome(const QString& text, const QString& html, const QString& fontPath,
                           ConversionInputOutcome* outcome) {
    const bool rich = !html.trimmed().isEmpty();
    if (!rich && text.trimmed().isEmpty()) {
        outcome->error = i18n::tr("aipdf::app::ConversionInputs", "剪贴板里没有文本，没法生成 PDF。");
        return false;
    }
    QString usedFont;
    // 要不要嵌中文字体，看的是**排出来的正文**里有没有非拉丁字符：
    // 富文本的源码里有中文，不代表正文里有。
    const QString body = rich ? doc::extractHtmlBody(html).text : text;
    const doc::UniversalConverterOptions options =
        buildTextOptions(body, fontPath, &usedFont);
    const doc::ConversionResult result =
        rich ? doc::clipboardHtmlToPdf(html, QString(), options)
             : doc::clipboardTextToPdf(text, QString(), options);
    if (!result.ok) {
        outcome->error = clipboardErrorText(result);
        return false;
    }
    outcome->ok = true;
    outcome->bytes = result.bytes;
    outcome->warning = result.warning;
    outcome->pageCount = result.pageCount;
    outcome->sourceKind = result.sourceKind;
    outcome->suggestedFileName = i18n::tr("aipdf::app::ConversionInputs", "剪贴板.pdf");
    // 明说这次走的是哪一路：排版结果不一样，用户得能在结果里看出来。
    outcome->detailLines.append(
        rich ? i18n::tr("aipdf::app::ConversionInputs",
                        "这次按富文本排版（保留了标题、段落和列表结构）。")
             : i18n::tr("aipdf::app::ConversionInputs", "这次按纯文本排版。"));
    if (!usedFont.isEmpty()) {
        outcome->detailLines.append(i18n::tr("aipdf::app::ConversionInputs", "已嵌入系统字体 %1 来显示中文。").arg(usedFont));
    }
    return true;
}

// 剪贴板图片 → PDF 的结果。
//
// 与文字那一路分开写：失败原因、图片张数、逐张问题都不一样，
// 混在一起迟早会说错话。
bool buildClipboardImageOutcome(const QVector<QByteArray>& images, const QString& fontPath,
                                 ConversionInputOutcome* outcome) {
    if (images.isEmpty()) {
        outcome->error = i18n::tr("aipdf::app::ConversionInputs", "剪贴板里没有图片，没法生成 PDF。");
        return false;
    }
    const doc::UniversalConverterOptions options = buildTextOptions(QString(), fontPath, nullptr);
    const doc::ConversionResult result = doc::clipboardImageToPdf(images, QString(), options);
    if (!result.ok) {
        outcome->error = clipboardErrorText(result);
        return false;
    }
    outcome->ok = true;
    outcome->bytes = result.bytes;
    outcome->warning = result.warning;
    outcome->pageCount = result.pageCount;
    outcome->sourceKind = result.sourceKind;
    outcome->suggestedFileName = i18n::tr("aipdf::app::ConversionInputs", "剪贴板.pdf");
    outcome->detailLines.append(
        i18n::tr("aipdf::app::ConversionInputs", "这次按图片排版（每张图一页）。"));
    outcome->detailLines.append(
        i18n::tr("aipdf::app::ConversionInputs", "这次带了 %1 张图片。")
            .arg(result.clipboardImageOutcome.embedded));
    if (!result.clipboardImageOutcome.issues.isEmpty()) {
        outcome->detailLines.append(
            i18n::tr("aipdf::app::ConversionInputs", "有 %1 张图片没能带进来：")
                .arg(result.clipboardImageOutcome.issues.size()));
        for (const doc::ClipboardImageIssue& issue : result.clipboardImageOutcome.issues) {
            outcome->detailLines.append(
                i18n::tr("aipdf::app::ConversionInputs", "第 %1 张图片：%2")
                    .arg(issue.index + 1)
                    .arg(clipboardImageIssueText(issue)));
        }
    }
    return true;
}

}  // namespace

// ============================================================ 抓网页 =====

WebFetchResult fetchWebPage(const QUrl& url, int timeoutMs) {
    WebFetchResult result;
    if (url.isEmpty() || !url.isValid()) {
        result.error = i18n::tr("aipdf::app::ConversionInputs", "网址无效。");
        return result;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        result.error = i18n::tr("aipdf::app::ConversionInputs", "只支持 http / https 网址。");
        return result;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AiPdfEditor/"));
    // 只跟随安全方向的跳转：https 跳到 http 不跟。
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&loop, &timedOut] {
        timedOut = true;
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (timedOut) {
        reply->abort();
        result.error = i18n::tr("aipdf::app::ConversionInputs", "等了 %1 秒还没拿到网页，已经放弃。").arg(timeoutMs / 1000);
        return result;
    }

    result.finalUrl = reply->url();
    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();

    if (reply->error() != QNetworkReply::NoError) {
        result.error = i18n::tr("aipdf::app::ConversionInputs", "连不上这个网址：%1").arg(reply->errorString());
        return result;
    }
    if (result.statusCode != 200) {
        result.error = i18n::tr("aipdf::app::ConversionInputs", "服务器返回 %1（不是 200），没拿到正文。").arg(result.statusCode);
        return result;
    }
    if (!contentType.isEmpty() && !contentType.contains(QLatin1String("text/")) &&
        !contentType.contains(QLatin1String("html")) &&
        !contentType.contains(QLatin1String("xml"))) {
        result.error =
            i18n::tr("aipdf::app::ConversionInputs", "这个地址返回的不是网页（%1），不能转成 PDF。").arg(contentType);
        return result;
    }

    QString decodeError;
    result.html = decodeWebBytes(reply->readAll(), contentType, &decodeError);
    if (!decodeError.isEmpty()) {
        result.error = decodeError;
        return result;
    }
    result.ok = true;
    return result;
}

// 取网页里图片的字节。
//
// 为什么放在 app 层：网络是副作用，领域层不碰；单测里要能塞假图片，
// 就得让「取字节」这件事可以被替换。
doc::ImageFetcher webImageFetcher(int timeoutMs) {
    // 网络管理器整个批次共用一个：每张图都新建一个管理器会白建一堆连接池。
    auto manager = std::make_shared<QNetworkAccessManager>();
    return [manager, timeoutMs](const QUrl& url) -> QByteArray {
        if (url.isEmpty() || !url.isValid()) {
            return QByteArray();
        }
        const QString scheme = url.scheme().toLower();
        if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
            return QByteArray();
        }
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AiPdfEditor/"));
        // 只跟随安全方向的跳转：https 跳到 http 不跟。
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = manager->get(request);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        bool timedOut = false;
        QObject::connect(&timer, &QTimer::timeout, &loop, [&loop, &timedOut] {
            timedOut = true;
            loop.quit();
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        loop.exec();

        if (timedOut) {
            reply->abort();
            reply->deleteLater();
            return QByteArray();
        }
        if (reply->error() != QNetworkReply::NoError) {
            reply->deleteLater();
            return QByteArray();
        }
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode != 200) {
            reply->deleteLater();
            return QByteArray();
        }
        const QByteArray bytes = reply->readAll();
        reply->deleteLater();
        return bytes;
    };
}

// ============================================================ 对话框 =====

ConversionInputDialog::ConversionInputDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("conversionInputDialog"));
    setWindowTitle(i18n::tr("aipdf::app::ConversionInputs", "从剪贴板新建 PDF"));
    resize(660, 460);

    auto* layout = new QVBoxLayout(this);
    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("conversionInput.stack"));
    layout->addWidget(stack_, 1);

    // 剪贴板：只读预览。
    auto* clipboardPage = new QWidget(stack_);
    auto* clipboardLayout = new QVBoxLayout(clipboardPage);
    auto* clipboardHint = new QLabel(i18n::tr("aipdf::app::ConversionInputs", "剪贴板内容（只读预览）："), clipboardPage);
    clipboardHint->setObjectName(QStringLiteral("conversionInput.clipboardHint"));
    clipboardView_ = new QPlainTextEdit(clipboardPage);
    clipboardView_->setObjectName(QStringLiteral("conversionInput.clipboardView"));
    clipboardView_->setReadOnly(true);
    clipboardView_->setAccessibleName(i18n::tr("aipdf::app::ConversionInputs", "剪贴板内容"));
    clipboardLayout->addWidget(clipboardHint);
    clipboardLayout->addWidget(clipboardView_, 1);

    // 内容类型：剪贴板里同时有文字和图片时，用户得自己说转哪一样。
    auto* contentCombo = new QComboBox(clipboardPage);
    contentCombo->setObjectName(QStringLiteral("conversionInput.clipboardContent"));
    contentCombo->setAccessibleName(i18n::tr("aipdf::app::ConversionInputs", "剪贴板内容类型"));
    contentCombo->addItem(i18n::tr("aipdf::app::ConversionInputs", "自动（有文字用文字，没文字用图片）"),
                           static_cast<int>(ConversionInputDialog::ClipboardContent::Auto));
    contentCombo->addItem(i18n::tr("aipdf::app::ConversionInputs", "文字（富文本优先）"),
                           static_cast<int>(ConversionInputDialog::ClipboardContent::Text));
    contentCombo->addItem(i18n::tr("aipdf::app::ConversionInputs", "图片（每张一页）"),
                           static_cast<int>(ConversionInputDialog::ClipboardContent::Images));
    clipboardContentCombo_ = contentCombo;
    clipboardLayout->addWidget(contentCombo);
    stack_->addWidget(clipboardPage);

    // 打开就把剪贴板里的文本摆出来：用户看得见要转的是什么，才敢点「生成 PDF」。
    if (QGuiApplication::instance() != nullptr) {
        const QClipboard* clipboard = QGuiApplication::clipboard();
        if (clipboard != nullptr) {
            clipboardView_->setPlainText(clipboard->text());
        }
    }

    // 网页：地址框。
    auto* webPage = new QWidget(stack_);
    auto* webLayout = new QFormLayout(webPage);
    urlEdit_ = new QLineEdit(webPage);
    urlEdit_->setObjectName(QStringLiteral("conversionInput.url"));
    urlEdit_->setAccessibleName(i18n::tr("aipdf::app::ConversionInputs", "网页地址"));
    urlEdit_->setPlaceholderText(QStringLiteral("https://example.com/article"));
    webLayout->addRow(i18n::tr("aipdf::app::ConversionInputs", "网页地址"), urlEdit_);
    stack_->addWidget(webPage);

    // 表单文件：模板 + 数据文件。
    auto* formPage = new QWidget(stack_);
    auto* formLayout = new QFormLayout(formPage);
    templateEdit_ = new QLineEdit(formPage);
    templateEdit_->setObjectName(QStringLiteral("conversionInput.template"));
    templateEdit_->setAccessibleName(i18n::tr("aipdf::app::ConversionInputs", "模板 PDF"));
    auto* templateButton = new QPushButton(i18n::tr("aipdf::app::ConversionInputs", "选择文件…"), formPage);
    templateButton->setObjectName(QStringLiteral("conversionInput.chooseTemplate"));
    auto* templateRow = new QWidget(formPage);
    auto* templateLayout = new QHBoxLayout(templateRow);
    templateLayout->setContentsMargins(0, 0, 0, 0);
    templateLayout->addWidget(templateEdit_, 1);
    templateLayout->addWidget(templateButton);
    formLayout->addRow(i18n::tr("aipdf::app::ConversionInputs", "模板 PDF"), templateRow);

    dataEdit_ = new QLineEdit(formPage);
    dataEdit_->setObjectName(QStringLiteral("conversionInput.formData"));
    dataEdit_->setAccessibleName(i18n::tr("aipdf::app::ConversionInputs", "表单数据文件"));
    auto* dataButton = new QPushButton(i18n::tr("aipdf::app::ConversionInputs", "选择文件…"), formPage);
    dataButton->setObjectName(QStringLiteral("conversionInput.chooseFormData"));
    auto* dataRow = new QWidget(formPage);
    auto* dataLayout = new QHBoxLayout(dataRow);
    dataLayout->setContentsMargins(0, 0, 0, 0);
    dataLayout->addWidget(dataEdit_, 1);
    dataLayout->addWidget(dataButton);
    formLayout->addRow(i18n::tr("aipdf::app::ConversionInputs", "表单数据文件"), dataRow);
    stack_->addWidget(formPage);

    // 字体（可选）。
    fontEdit_ = new QLineEdit(this);
    fontEdit_->setObjectName(QStringLiteral("conversionInput.font"));
    fontEdit_->setAccessibleName(i18n::tr("aipdf::app::ConversionInputs", "字体文件"));
    auto* fontButton = new QPushButton(i18n::tr("aipdf::app::ConversionInputs", "选择文件…"), this);
    fontButton->setObjectName(QStringLiteral("conversionInput.chooseFont"));
    auto* fontRow = new QWidget(this);
    auto* fontLayout = new QHBoxLayout(fontRow);
    fontLayout->setContentsMargins(0, 0, 0, 0);
    fontLayout->addWidget(fontEdit_, 1);
    fontLayout->addWidget(fontButton);
    auto* fontForm = new QFormLayout();
    fontForm->addRow(i18n::tr("aipdf::app::ConversionInputs", "字体文件（可选，TTF）"), fontRow);
    layout->addLayout(fontForm);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("conversionInput.status"));
    statusLabel_->setAccessibleName(i18n::tr("aipdf::app::ConversionInputs", "转换结果说明"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* generateButton =
        buttons->addButton(i18n::tr("aipdf::app::ConversionInputs", "生成 PDF"), QDialogButtonBox::ApplyRole);
    generateButton->setObjectName(QStringLiteral("conversionInput.generate"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(generateButton, &QPushButton::clicked, this, &ConversionInputDialog::onGenerate);
    connect(templateButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, i18n::tr("aipdf::app::ConversionInputs", "选择模板 PDF"), templateEdit_->text(), i18n::tr("aipdf::app::ConversionInputs", "PDF 文件 (*.pdf)"));
        if (!path.isEmpty()) {
            templateEdit_->setText(path);
        }
    });
    connect(dataButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, i18n::tr("aipdf::app::ConversionInputs", "选择表单数据文件"), dataEdit_->text(),
            i18n::tr("aipdf::app::ConversionInputs", "表单数据文件 (*.fdf *.xfdf *.xml)"));
        if (!path.isEmpty()) {
            dataEdit_->setText(path);
        }
    });
    connect(fontButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, i18n::tr("aipdf::app::ConversionInputs", "选择字体文件"), fontEdit_->text(), i18n::tr("aipdf::app::ConversionInputs", "字体文件 (*.ttf)"));
        if (!path.isEmpty()) {
            fontEdit_->setText(path);
        }
    });

    applyMode();
}

void ConversionInputDialog::applyMode() {
    switch (mode_) {
    case Mode::Clipboard:
        stack_->setCurrentIndex(0);
        setWindowTitle(i18n::tr("aipdf::app::ConversionInputs", "从剪贴板新建 PDF"));
        break;
    case Mode::Web:
        stack_->setCurrentIndex(1);
        setWindowTitle(i18n::tr("aipdf::app::ConversionInputs", "从网页新建 PDF"));
        break;
    case Mode::FormFile:
        stack_->setCurrentIndex(2);
        setWindowTitle(i18n::tr("aipdf::app::ConversionInputs", "从表单文件新建 PDF"));
        break;
    }
}

void ConversionInputDialog::setMode(Mode mode) {
    mode_ = mode;
    applyMode();
}

ConversionInputDialog::Mode ConversionInputDialog::mode() const {
    return mode_;
}

void ConversionInputDialog::setClipboardText(const QString& value) {
    clipboardView_->setPlainText(value);
}

QString ConversionInputDialog::clipboardText() const {
    const QString preview = clipboardView_->toPlainText();
    if (!preview.isEmpty()) {
        return preview;
    }
    // 预览是空的：再看一眼系统剪贴板（可能刚被别的程序改过）。
    if (QGuiApplication::instance() == nullptr) {
        return QString();
    }
    const QClipboard* clipboard = QGuiApplication::clipboard();
    return clipboard != nullptr ? clipboard->text() : QString();
}

void ConversionInputDialog::setClipboardHtml(const QString& value) {
    clipboardHtml_ = value;
}

void ConversionInputDialog::setClipboardContent(ClipboardContent content) {
    clipboardContent_ = content;
    if (clipboardContentCombo_ != nullptr) {
        clipboardContentCombo_->setCurrentIndex(
            clipboardContentCombo_->findData(static_cast<int>(content)));
    }
}

ConversionInputDialog::ClipboardContent ConversionInputDialog::clipboardContent() const {
    return clipboardContent_;
}

void ConversionInputDialog::setClipboardImages(const QVector<QByteArray>& images) {
    clipboardImages_ = images;
}

QVector<QByteArray> ConversionInputDialog::clipboardImages() const {
    if (!clipboardImages_.isEmpty()) {
        return clipboardImages_;
    }
    return readSystemClipboardImages();
}

QString ConversionInputDialog::clipboardHtml() const {
    if (!clipboardHtml_.trimmed().isEmpty()) {
        return clipboardHtml_;
    }
    // 没显式设置：看系统剪贴板里有没有 HTML（Word / 网页复制过来的都有）。
    if (QGuiApplication::instance() == nullptr) {
        return QString();
    }
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        return QString();
    }
    const QMimeData* mime = clipboard->mimeData();
    if (mime == nullptr || !mime->hasHtml()) {
        return QString();
    }
    return mime->html();
}

void ConversionInputDialog::setUrlText(const QString& value) {
    urlEdit_->setText(value);
}

QString ConversionInputDialog::urlText() const {
    return urlEdit_->text();
}

void ConversionInputDialog::setTemplatePath(const QString& value) {
    templateEdit_->setText(value);
}

QString ConversionInputDialog::templatePath() const {
    return templateEdit_->text();
}

void ConversionInputDialog::setFormDataPath(const QString& value) {
    dataEdit_->setText(value);
}

QString ConversionInputDialog::formDataPath() const {
    return dataEdit_->text();
}

void ConversionInputDialog::setFontPath(const QString& value) {
    fontEdit_->setText(value);
}

QString ConversionInputDialog::fontPath() const {
    return fontEdit_->text();
}

bool ConversionInputDialog::convert(ConversionInputOutcome* outcome) {
    ConversionInputOutcome local;
    bool ok = false;
    switch (mode_) {
    case Mode::Clipboard:
        ok = convertClipboard(&local);
        break;
    case Mode::Web:
        ok = convertWeb(&local);
        break;
    case Mode::FormFile:
        ok = convertFormFile(&local);
        break;
    }
    outcome_ = local;
    generated_ = ok;
    statusLabel_->setText(statusText());
    if (outcome != nullptr) {
        *outcome = local;
    }
    return ok;
}

bool ConversionInputDialog::convertClipboard(ConversionInputOutcome* outcome) {
    const QString html = clipboardHtml();
    const QString text = clipboardText();
    const QVector<QByteArray> images = clipboardImages();
    const bool hasText = !html.trimmed().isEmpty() || !text.trimmed().isEmpty();

    // 图片这一路：用户点名要图片，或者（自动模式下）剪贴板里只有图片。
    const bool useImages = clipboardContent_ == ClipboardContent::Images
        || (clipboardContent_ == ClipboardContent::Auto && !hasText && !images.isEmpty());
    if (useImages) {
        return buildClipboardImageOutcome(images, fontPath(), outcome);
    }
    if (!hasText) {
        // 负例：什么都没有 —— 明确说「没内容」，别生成一份空白 PDF。
        outcome->error = i18n::tr("aipdf::app::ConversionInputs", "剪贴板里没有文本，没法生成 PDF。");
        return false;
    }
    // 混合内容按文字排：图片没进来要**明说**，不能静默丢掉。
    if (!images.isEmpty()) {
        outcome->warning = i18n::tr("aipdf::app::ConversionInputs",
                                     "剪贴板里还有 %1 张图片，这次按文字排版，图片没有进 PDF。")
                               .arg(images.size());
    }
    return buildClipboardOutcome(text, html, fontPath(), outcome);
}

bool ConversionInputDialog::convertWeb(ConversionInputOutcome* outcome) {
    const QString address = urlText().trimmed();
    if (address.isEmpty()) {
        outcome->error = i18n::tr("aipdf::app::ConversionInputs", "还没有网址。");
        return false;
    }
    const QUrl url(address);
    const WebFetchResult fetched = fetchWebPage(url);
    if (!fetched.ok) {
        outcome->error = fetched.error;
        return false;
    }
    const QUrl base = fetched.finalUrl.isValid() && !fetched.finalUrl.isEmpty()
        ? fetched.finalUrl
        : url;
    QString usedFont;
    const doc::UniversalConverterOptions options =
        buildTextOptions(fetched.html, fontPath(), &usedFont);
    // 连图片一起带进去：网页正文里的图是内容的一部分，丢了就是缺内容。
    const doc::ConversionResult result =
        doc::htmlToPdf(fetched.html, base, options, webImageFetcher());
    if (!result.ok) {
        // 给用户看的中文由 app 层拼（doc 层没有翻译上下文）。
        outcome->error =
            result.errorKind == doc::ConversionErrorKind::NoHtmlBody
                ? i18n::tr("aipdf::app::ConversionInputs",
                           "这个网页里没有正文（脚本、样式和导航都去掉之后就没东西了）。")
                : result.error;
        return false;
    }
    outcome->ok = true;
    outcome->bytes = result.bytes;
    outcome->warning = result.warning;
    outcome->pageCount = result.pageCount;
    const QString host = base.host().isEmpty() ? QStringLiteral("网页") : base.host();
    outcome->suggestedFileName =
        sanitizeFileName(result.title.isEmpty() ? host : result.title) + QStringLiteral(".pdf");
    if (!usedFont.isEmpty()) {
        outcome->detailLines.append(i18n::tr("aipdf::app::ConversionInputs", "已嵌入系统字体 %1 来显示中文。").arg(usedFont));
    }
    // 这次带图的结果：带了几张、哪几张没带进来（逐条带原因）。
    const doc::HtmlImageOutcome& images = result.imageOutcome;
    if (images.attempted) {
        if (images.embedded > 0) {
            outcome->detailLines.append(
                i18n::tr("aipdf::app::ConversionInputs", "这次带了 %1 张图片。").arg(images.embedded));
        } else if (images.found == 0) {
            outcome->detailLines.append(
                i18n::tr("aipdf::app::ConversionInputs", "这次没有带图片（网页里没有图片）。"));
        } else {
            outcome->detailLines.append(
                i18n::tr("aipdf::app::ConversionInputs", "这次没有带图片。"));
        }
        for (const doc::HtmlImageIssue& issue : images.issues) {
            outcome->detailLines.append(
                i18n::tr("aipdf::app::ConversionInputs", "图片没带进来：%1（%2）")
                    .arg(issue.url, imageIssueReasonText(issue)));
        }
    }
    return true;
}

bool ConversionInputDialog::convertFormFile(ConversionInputOutcome* outcome) {
    const QString templateFile = templatePath().trimmed();
    if (templateFile.isEmpty()) {
        outcome->error = i18n::tr("aipdf::app::ConversionInputs", "没有选模板 PDF。");
        return false;
    }
    const QString dataFile = formDataPath().trimmed();
    if (dataFile.isEmpty()) {
        outcome->error = i18n::tr("aipdf::app::ConversionInputs", "没有选表单数据文件。");
        return false;
    }

    QFile templateHandle(templateFile);
    if (!templateHandle.open(QIODevice::ReadOnly)) {
        outcome->error = i18n::tr("aipdf::app::ConversionInputs", "读不了这个文件：%1").arg(templateFile);
        return false;
    }
    const QByteArray templateBytes = templateHandle.readAll();
    templateHandle.close();

    QFile dataHandle(dataFile);
    if (!dataHandle.open(QIODevice::ReadOnly)) {
        outcome->error = i18n::tr("aipdf::app::ConversionInputs", "读不了这个文件：%1").arg(dataFile);
        return false;
    }
    const QByteArray dataBytes = dataHandle.readAll();
    dataHandle.close();

    const doc::FormFillReport report = doc::applyFormDataFileToTemplate(
        templateBytes, dataBytes, QFileInfo(dataFile).fileName());
    if (!report.ok) {
        outcome->error = report.error;
        return false;
    }
    outcome->ok = true;
    outcome->bytes = report.bytes;
    outcome->pageCount = report.pageCount;
    outcome->suggestedFileName =
        sanitizeFileName(QFileInfo(templateFile).completeBaseName()) + QStringLiteral(".pdf");
    if (!report.applied.isEmpty()) {
        outcome->detailLines.append(
            i18n::tr("aipdf::app::ConversionInputs", "填上了 %1 个字段：%2")
                .arg(report.applied.size())
                .arg(report.applied.join(QStringLiteral("、"))));
    }
    for (const doc::FormFieldIssue& issue : report.issues) {
        outcome->detailLines.append(i18n::tr("aipdf::app::ConversionInputs", "填不上的字段 %1：%2").arg(issue.name, issue.reason));
    }
    if (!report.untouched.isEmpty()) {
        outcome->detailLines.append(
            i18n::tr("aipdf::app::ConversionInputs", "模板里 %1 个字段没有数据，保持原样。").arg(report.untouched.size()));
    }
    return true;
}

void ConversionInputDialog::onGenerate() {
    ConversionInputOutcome outcome;
    if (!convert(&outcome)) {
        QMessageBox::warning(this, windowTitle(), outcome.error);
        return;
    }
    accept();
}

const ConversionInputOutcome& ConversionInputDialog::outcome() const {
    return outcome_;
}

QString ConversionInputDialog::statusText() const {
    if (!generated_) {
        if (!outcome_.error.isEmpty()) {
            return outcome_.error;
        }
        return i18n::tr("aipdf::app::ConversionInputs", "还没有生成 PDF。");
    }
    QString status = i18n::tr("aipdf::app::ConversionInputs", "已生成 %1 页。").arg(outcome_.pageCount);
    if (!outcome_.warning.isEmpty()) {
        status += QStringLiteral(" ") + outcome_.warning;
    }
    for (const QString& line : outcome_.detailLines) {
        status += QStringLiteral(" ") + line;
    }
    return status;
}

// ============================================================ 三个入口 =====

ConversionInputOutcome convertClipboardToPdf() {
    ConversionInputOutcome outcome;
    if (QGuiApplication::instance() == nullptr) {
        outcome.error = i18n::tr("aipdf::app::ConversionInputs", "没有图形界面环境，读不了剪贴板。");
        return outcome;
    }
    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* mime = clipboard != nullptr ? clipboard->mimeData() : nullptr;
    const QString html = (mime != nullptr && mime->hasHtml()) ? mime->html() : QString();
    const QString body = clipboard != nullptr ? clipboard->text() : QString();
    const QVector<QByteArray> images = readSystemClipboardImages();
    const bool hasText = !html.trimmed().isEmpty() || !body.trimmed().isEmpty();
    // 只有图片时走图片这一路；文字与图片都有时按文字排（并明说图片没进来）。
    if (!hasText && !images.isEmpty()) {
        buildClipboardImageOutcome(images, QString(), &outcome);
        return outcome;
    }
    if (!hasText) {
        outcome.error = i18n::tr("aipdf::app::ConversionInputs", "剪贴板里没有文本，没法生成 PDF。");
        return outcome;
    }
    if (!images.isEmpty()) {
        outcome.warning = i18n::tr("aipdf::app::ConversionInputs",
                                    "剪贴板里还有 %1 张图片，这次按文字排版，图片没有进 PDF。")
                              .arg(images.size());
    }
    buildClipboardOutcome(body, html, QString(), &outcome);
    return outcome;
}

ConversionInputOutcome convertWebToPdf(QWidget* parent) {
    ConversionInputOutcome outcome;
    ConversionInputDialog dialog(parent);
    dialog.setMode(ConversionInputDialog::Mode::Web);
    dialog.exec();
    outcome = dialog.outcome();
    if (!outcome.ok && outcome.error.isEmpty()) {
        outcome.error = i18n::tr("aipdf::app::ConversionInputs", "用户取消了转换。");
    }
    return outcome;
}

ConversionInputOutcome convertFormFileToPdf(QWidget* parent) {
    ConversionInputOutcome outcome;
    ConversionInputDialog dialog(parent);
    dialog.setMode(ConversionInputDialog::Mode::FormFile);
    dialog.exec();
    outcome = dialog.outcome();
    if (!outcome.ok && outcome.error.isEmpty()) {
        outcome.error = i18n::tr("aipdf::app::ConversionInputs", "用户取消了转换。");
    }
    return outcome;
}

}  // namespace aipdf::app
