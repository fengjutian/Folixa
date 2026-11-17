#pragma once

#include <QByteArray>
#include <QDialog>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "doc/ConversionInputs.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

namespace aipdf::app {

// ---------------------------------------------------------- 抓网页 ----

// 抓一个网页的结果。
//
// 为什么网络放在 app 层：领域层不碰网络（doc 层不该有副作用，
// 也没法在单测里跑）。抓回来的正文再交给 doc::htmlToPdf 排版。
struct WebFetchResult {
    bool ok = false;
    // 抓回来的 HTML（已按 UTF-8 / UTF-16 解码）。
    QString html;
    // 失败原因（中文）。几种失败说得不一样：网址无效 / 连不上 / 超时 /
    // 服务器返回的不是 200 / 返回的不是网页 / 编码不对。
    QString error;
    // HTTP 状态码（没拿到响应时是 0）。
    int statusCode = 0;
    // 最终地址（跟着重定向走）。
    QUrl finalUrl;
};

// 同步抓一个网页。timeoutMs 毫秒还没回来就放弃。
WebFetchResult fetchWebPage(const QUrl& url, int timeoutMs = 15000);

// 取网页里图片的字节（给 doc 层当回调用）。
//
// 参照 fetchWebPage 的做法：同样的超时、同样的跳转策略（https 不跳 http）、
// 同样的 User-Agent。图片是二进制，没有编码问题 —— 网页正文的编码由
// fetchWebPage 管（那里对 GBK 这类编码是明确报错的）。
//
// 取不到就返回空字节：doc 层会把这一张记成「下载失败」，不影响整篇转换。
doc::ImageFetcher webImageFetcher(int timeoutMs = 15000);

// ---------------------------------------------------------- 转换结果 ----

// 一次转换的结果（界面层交给上层：Ribbon handler、主窗口）。
struct ConversionInputOutcome {
    bool ok = false;
    // 生成的 PDF 字节。
    QByteArray bytes;
    // 失败原因（中文）。
    QString error;
    // 警告（中文）：成功但有损（例如有字符显示不出来）。
    QString warning;
    // 建议的文件名。
    QString suggestedFileName;
    // 逐条说明（例如「填不上的字段 姓名：模板里没有同名字段」）。
    QStringList detailLines;
    // 生成的页数。
    int pageCount = 0;
    // 这次排版用的是哪一路输入（剪贴板转换时才有意义）。
    //
    // 为什么把 doc 层的枚举搬上来：界面要跟用户说清「这次用的是纯文本还是
    // 富文本」，这个判断在领域层做，结论得原样传到界面。
    doc::ConversionSourceKind sourceKind = doc::ConversionSourceKind::Unknown;
};

// ---------------------------------------------------------- 对话框 ----

// 转换输入对话框：剪贴板 / 网页 / 表单文件三种输入。
//
// 为什么做成一个对话框而不是三个：三种输入的界面骨架一样（几个输入框 +
// 一个状态区 + 一个「生成 PDF」），拆成三个类只会多出两份几乎相同的代码。
class ConversionInputDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Mode {
        Clipboard,  // 剪贴板文本
        Web,        // 网页 URL
        FormFile,   // 表单数据文件 + 模板 PDF
    };
    Q_ENUM(Mode)

    explicit ConversionInputDialog(QWidget* parent = nullptr);

    void setMode(Mode mode);
    Mode mode() const;

    // 剪贴板文本（只读预览；没设置过就读系统剪贴板）。
    void setClipboardText(const QString& text);
    QString clipboardText() const;

    // 剪贴板里的富文本（HTML）。设置了就优先按富文本排版；
    // 没设置时看系统剪贴板（剪贴板里带 HTML 就用 HTML）。
    //
    // 为什么要有这一对：剪贴板里从 Word / 网页复制过来的内容带 HTML，
    // 只取纯文本会把标题、段落、列表结构全丢掉。
    void setClipboardHtml(const QString& html);
    QString clipboardHtml() const;

    // 网页地址。
    void setUrlText(const QString& text);
    QString urlText() const;

    // 模板 PDF 路径。
    void setTemplatePath(const QString& path);
    QString templatePath() const;

    // 表单数据文件路径。
    void setFormDataPath(const QString& path);
    QString formDataPath() const;

    // 字体文件路径（可选，TTF）。留空时文本里有中文会自动用系统里的中文字体。
    void setFontPath(const QString& path);
    QString fontPath() const;

    // 执行转换。失败时返回 false，outcome->error 是中文原因。
    bool convert(ConversionInputOutcome* outcome);

    // 上一次转换的结果。
    const ConversionInputOutcome& outcome() const;

    // 状态区文字（成功 / 失败 / 警告）。
    QString statusText() const;

private:
    void applyMode();
    void onGenerate();
    bool convertClipboard(ConversionInputOutcome* outcome);
    bool convertWeb(ConversionInputOutcome* outcome);
    bool convertFormFile(ConversionInputOutcome* outcome);

    Mode mode_ = Mode::Clipboard;
    QStackedWidget* stack_ = nullptr;
    QPlainTextEdit* clipboardView_ = nullptr;
    // 显式设置的剪贴板富文本（为空时看系统剪贴板）。
    QString clipboardHtml_;
    QLineEdit* urlEdit_ = nullptr;
    QLineEdit* templateEdit_ = nullptr;
    QLineEdit* dataEdit_ = nullptr;
    QLineEdit* fontEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    ConversionInputOutcome outcome_;
    bool generated_ = false;
};

// ---------------------------------------------------------- 三个入口 ----
//
// Ribbon handler 直接调这三个就够了：各自返回结果与中文原因，
// 上层只负责「打开生成的 PDF」或者「把原因说给用户听」。

// 从剪贴板新建 PDF（一键完成，不用弹窗）。
ConversionInputOutcome convertClipboardToPdf();

// 从网页新建 PDF（弹对话框让用户填地址）。
ConversionInputOutcome convertWebToPdf(QWidget* parent = nullptr);

// 从表单文件新建 PDF（弹对话框让用户选模板与数据文件）。
ConversionInputOutcome convertFormFileToPdf(QWidget* parent = nullptr);

}  // namespace aipdf::app
