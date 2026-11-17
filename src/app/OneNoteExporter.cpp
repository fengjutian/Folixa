#include "OneNoteExporter.h"

#include "i18n/Translate.h"

#include <QBuffer>
#include <QImageWriter>
#include <QXmlStreamReader>

#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#endif

namespace aipdf::app {

namespace {

// OneNote XML 的命名空间（与 xs2013 对应）。
const char kOneNoteNamespace[] = "http://schemas.microsoft.com/office/onenote/2013/onenote";

// 按 %1、%2…… 一次填好占位符。
//
// 为什么不直接用 QString::arg 的多参数版本：Qt 6 里没有那个重载；
// 链式 .arg() 又会在替换文本里出现 "%2" 时串味——笔记本名、分区名是用户数据，
// 里面出现 % 并不奇怪。一次扫过去就不会有这个问题。
QString fillPlaceholders(const QString& pattern, const QStringList& values) {
    QString result;
    result.reserve(pattern.size() + 32);
    int index = 0;
    while (index < pattern.size()) {
        const QChar ch = pattern.at(index);
        if (ch == QLatin1Char('%') && index + 1 < pattern.size() && pattern.at(index + 1).isDigit()) {
            int cursor = index + 1;
            int number = 0;
            while (cursor < pattern.size() && pattern.at(cursor).isDigit()) {
                number = number * 10 + static_cast<int>(pattern.at(cursor).unicode()) - '0';
                ++cursor;
            }
            if (number >= 1 && number <= values.size()) {
                result += values.at(number - 1);
                index = cursor;
                continue;
            }
        }
        result += ch;
        ++index;
    }
    return result;
}

#ifdef Q_OS_WIN

// IID_IDispatch 与 IID_NULL 自己写一份。
//
// 为什么不用 MinGW 头里现成的：那两个 GUID 定义在 libuuid 里，
// 自己写就能不给主程序多加一个链接依赖（只要 ole32 + oleaut32）。
const IID kIIDispatch = {0x00020400, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID kIIDNull = {0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

// OneNote 的 COM 枚举值。手抄数值而不是引类型库：
// 构建机上没装 Office 也会编译失败（这也是不能用 #import "OneNote.tlb" 的原因）。
constexpr int kHierarchyScopeSections = 3;  // hsSections
constexpr int kNewPageStyleDefault = 0;     // npsDefault
constexpr int kXmlSchema2013 = 2;           // xs2013

// HRESULT → 「0x80042001」这样的十六进制文本。
QString hresultText(HRESULT hr) {
    return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

// 被调方抛异常时 Invoke 返回的 HRESULT。MinGW 的 oaidl.h 里没有这两个宏，自己写。
constexpr HRESULT kDispatchException = 0x80020009L;
constexpr HRESULT kDispatchExceptionNotFound = 0x80020004L;

BSTR allocBstr(const QString& text) {
    return SysAllocStringLen(reinterpret_cast<const OLECHAR*>(text.utf16()),
                              static_cast<UINT>(text.size()));
}

// 晚绑定调用：按方法名取 DISPID，再 Invoke。
//
// 全程只用 IDispatch，不 import 任何类型库：构建机没装 Office 也能编译。
HRESULT invokeDispatch(IDispatch* app, const wchar_t* method, VARIANT* args, UINT argCount,
                       VARIANT* result) {
    if (!app) {
        return E_POINTER;
    }
    wchar_t* names[1] = {const_cast<wchar_t*>(method)};
    DISPID dispid = DISPID_UNKNOWN;
    // MinGW 的 IDispatch::GetIDsOfNames 第一个参数是 REFIID（传 IID_NULL）。
    HRESULT hr = app->GetIDsOfNames(kIIDNull, names, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) {
        return hr;
    }

    DISPPARAMS params;
    params.rgvarg = args;
    params.cArgs = argCount;
    // 没有命名参数：rgdispidNamedArgs 必须置空（它是指针，不是 DISPID）。
    params.rgdispidNamedArgs = nullptr;
    params.cNamedArgs = 0;
    if (result) {
        VariantInit(result);
    }

    EXCEPINFO info{};
    hr = app->Invoke(dispid, kIIDNull, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, result, &info,
                     nullptr);
    if ((hr == kDispatchException || hr == kDispatchExceptionNotFound) && info.scode != 0) {
        // 被调方抛了异常：真正的原因在 EXCEPINFO 里。
        hr = info.scode;
    }
    if (info.bstrSource) {
        SysFreeString(info.bstrSource);
    }
    if (info.bstrDescription) {
        SysFreeString(info.bstrDescription);
    }
    return hr;
}

// GetHierarchy(startNodeId, scope, &hierarchyXml, schema)。
HRESULT callGetHierarchy(IDispatch* app, const QString& startNodeId, int scope, QString* xml) {
    BSTR outBstr = nullptr;

    VARIANT argStart;
    VariantInit(&argStart);
    argStart.vt = VT_BSTR;
    argStart.bstrVal = allocBstr(startNodeId);

    VARIANT argScope;
    VariantInit(&argScope);
    argScope.vt = VT_I4;
    argScope.lVal = scope;

    // 出参：晚绑定时用「按引用的 BSTR」把结果取回来。
    VARIANT argOut;
    VariantInit(&argOut);
    argOut.vt = VT_BSTR | VT_BYREF;
    argOut.pbstrVal = &outBstr;

    VARIANT argSchema;
    VariantInit(&argSchema);
    argSchema.vt = VT_I4;
    argSchema.lVal = kXmlSchema2013;

    VARIANT args[4] = {argStart, argScope, argOut, argSchema};
    VARIANT result;
    VariantInit(&result);

    const HRESULT hr = invokeDispatch(app, L"GetHierarchy", args, 4, &result);

    if (outBstr) {
        if (xml) {
            *xml = QString::fromWCharArray(outBstr, static_cast<int>(SysStringLen(outBstr)));
        }
        SysFreeString(outBstr);
    }

    // args[2] 指向的那个 BSTR 上面已经自己释放过了，别让 VariantClear 再释放一次。
    args[2].vt = VT_EMPTY;
    args[2].pbstrVal = nullptr;
    VariantClear(&args[0]);
    VariantClear(&args[1]);
    VariantClear(&args[2]);
    VariantClear(&args[3]);
    VariantClear(&result);
    return hr;
}

// CreateNewPage(sectionId, &pageId, style)。
HRESULT callCreateNewPage(IDispatch* app, const QString& sectionId, QString* pageId) {
    BSTR outBstr = nullptr;

    VARIANT argSection;
    VariantInit(&argSection);
    argSection.vt = VT_BSTR;
    argSection.bstrVal = allocBstr(sectionId);

    VARIANT argOut;
    VariantInit(&argOut);
    argOut.vt = VT_BSTR | VT_BYREF;
    argOut.pbstrVal = &outBstr;

    VARIANT argStyle;
    VariantInit(&argStyle);
    argStyle.vt = VT_I4;
    argStyle.lVal = kNewPageStyleDefault;

    VARIANT args[3] = {argSection, argOut, argStyle};
    VARIANT result;
    VariantInit(&result);

    const HRESULT hr = invokeDispatch(app, L"CreateNewPage", args, 3, &result);

    if (outBstr) {
        if (pageId) {
            *pageId = QString::fromWCharArray(outBstr, static_cast<int>(SysStringLen(outBstr)));
        }
        SysFreeString(outBstr);
    }

    args[1].vt = VT_EMPTY;
    args[1].pbstrVal = nullptr;
    VariantClear(&args[0]);
    VariantClear(&args[1]);
    VariantClear(&args[2]);
    VariantClear(&result);
    return hr;
}

// UpdatePageContent(pageChangesXml, dateExpectedLastModified, schema, force)。
HRESULT callUpdatePageContent(IDispatch* app, const QString& xml) {
    VARIANT argXml;
    VariantInit(&argXml);
    argXml.vt = VT_BSTR;
    argXml.bstrVal = allocBstr(xml);

    // 日期传 0：表示「不检查上次修改时间」，直接写。
    VARIANT argDate;
    VariantInit(&argDate);
    argDate.vt = VT_DATE;
    argDate.date = 0.0;

    VARIANT argSchema;
    VariantInit(&argSchema);
    argSchema.vt = VT_I4;
    argSchema.lVal = kXmlSchema2013;

    VARIANT argForce;
    VariantInit(&argForce);
    argForce.vt = VT_BOOL;
    argForce.bVal = VARIANT_FALSE;

    VARIANT args[4] = {argXml, argDate, argSchema, argForce};
    VARIANT result;
    VariantInit(&result);

    const HRESULT hr = invokeDispatch(app, L"UpdatePageContent", args, 4, &result);

    VariantClear(&args[0]);
    VariantClear(&args[1]);
    VariantClear(&args[2]);
    VariantClear(&args[3]);
    VariantClear(&result);
    return hr;
}

// 从 GetHierarchy 的 XML 里找出第一个分区（连同它所在的笔记本）。
bool parseFirstSection(const QString& xml, OneNoteTarget* target) {
    QXmlStreamReader reader(xml);
    QString notebookName;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            continue;
        }
        const QString name = reader.name().toString();
        const QXmlStreamAttributes attributes = reader.attributes();
        if (name == QLatin1String("Notebook")) {
            notebookName = attributes.value(QStringLiteral("name")).toString();
        } else if (name == QLatin1String("Section")) {
            target->notebookName = notebookName;
            target->sectionName = attributes.value(QStringLiteral("name")).toString();
            target->sectionId = attributes.value(QStringLiteral("ID")).toString();
            return !target->sectionId.isEmpty();
        }
    }
    return false;
}

// 读注册表里某个键的默认值。读不到就返回空串。
QString readRegistryString(HKEY root, const QString& subKey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return QString();
    }
    DWORD type = 0;
    DWORD size = 0;
    QString value;
    if (RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 0 &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, 0);
        if (RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()),
                             &size) == ERROR_SUCCESS) {
            value = QString::fromWCharArray(buffer.data());
        }
    }
    RegCloseKey(key);
    return value;
}

// 真的去调 OneNote 的后端。
class ComOneNoteBackend final : public OneNoteBackend {
public:
    ~ComOneNoteBackend() override { release(); }

    bool findTargetSection(OneNoteTarget* target, OneNoteErrorKind* errorKind,
                           QString* detail) override {
        *errorKind = OneNoteErrorKind::None;
        detail->clear();
        if (!ensureApplication(errorKind, detail)) {
            return false;
        }

        QString hierarchy;
        const HRESULT hr = callGetHierarchy(app_, QString(), kHierarchyScopeSections, &hierarchy);
        if (FAILED(hr)) {
            *errorKind = OneNoteErrorKind::ExportFailed;
            *detail = i18n::tr("aipdf::app::OneNoteExporter", "读取 OneNote 的笔记本结构失败（%1）")
                          .arg(hresultText(hr));
            return false;
        }

        OneNoteTarget found;
        if (!parseFirstSection(hierarchy, &found)) {
            *errorKind = OneNoteErrorKind::ExportFailed;
            *detail = i18n::tr("aipdf::app::OneNoteExporter", "OneNote 里没有找到可用的分区");
            return false;
        }
        *target = found;
        return true;
    }

    bool createPageWithImage(const QString& sectionId, const QString& pageTitle,
                             const QByteArray& imageFile, OneNoteImageFormat format,
                             const QSizeF& displaySizePoints, OneNoteErrorKind* errorKind,
                             QString* detail) override {
        *errorKind = OneNoteErrorKind::None;
        detail->clear();
        if (!ensureApplication(errorKind, detail)) {
            return false;
        }

        QString pageId;
        HRESULT hr = callCreateNewPage(app_, sectionId, &pageId);
        if (FAILED(hr)) {
            *errorKind = OneNoteErrorKind::ExportFailed;
            *detail = i18n::tr("aipdf::app::OneNoteExporter", "在 OneNote 里新建页面失败（%1）")
                          .arg(hresultText(hr));
            return false;
        }

        const QString xml =
            buildOneNotePageXml(pageId, pageTitle, imageFile, format, displaySizePoints);
        hr = callUpdatePageContent(app_, xml);
        if (FAILED(hr)) {
            *errorKind = OneNoteErrorKind::ExportFailed;
            *detail = i18n::tr("aipdf::app::OneNoteExporter", "把页面写入 OneNote 失败（%1）")
                          .arg(hresultText(hr));
            return false;
        }
        return true;
    }

private:
    // 连上 OneNote。分两步：初始化 COM、创建 Application 对象。
    bool ensureApplication(OneNoteErrorKind* errorKind, QString* detail) {
        if (app_) {
            return true;
        }
        if (!comInitialized_) {
            const HRESULT hr =
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (hr == RPC_E_CHANGED_MODE) {
                *errorKind = OneNoteErrorKind::ComUnavailable;
                *detail = i18n::tr("aipdf::app::OneNoteExporter",
                                   "当前线程的 COM 模式与 OneNote 需要的单线程单元冲突");
                return false;
            }
            if (FAILED(hr)) {
                *errorKind = OneNoteErrorKind::ComUnavailable;
                *detail = i18n::tr("aipdf::app::OneNoteExporter", "初始化 COM 失败（%1）")
                              .arg(hresultText(hr));
                return false;
            }
            // S_FALSE 也算数：这台机器上 COM 已经初始化过，
            // 但这一次调用也算一次引用，退出时必须配对 CoUninitialize。
            comInitialized_ = true;
        }

        CLSID clsid;
        HRESULT hr = CLSIDFromProgID(L"OneNote.Application", &clsid);
        if (FAILED(hr)) {
            *errorKind = OneNoteErrorKind::ComUnavailable;
            *detail = i18n::tr("aipdf::app::OneNoteExporter", "找不到 OneNote 的 COM 接口（%1）")
                          .arg(hresultText(hr));
            release();
            return false;
        }

        hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, kIIDispatch,
                              reinterpret_cast<void**>(&app_));
        if (FAILED(hr) || !app_) {
            app_ = nullptr;
            *errorKind = OneNoteErrorKind::ComUnavailable;
            *detail = i18n::tr("aipdf::app::OneNoteExporter", "无法启动 OneNote 进程（%1）")
                          .arg(hresultText(hr));
            release();
            return false;
        }
        return true;
    }

    void release() {
        if (app_) {
            app_->Release();
            app_ = nullptr;
        }
        if (comInitialized_) {
            CoUninitialize();
            comInitialized_ = false;
        }
    }

    IDispatch* app_ = nullptr;
    bool comInitialized_ = false;
};

#else  // !Q_OS_WIN

// 非 Windows 上没有 OneNote 桌面版，直接说清楚，别假装能做。
class UnsupportedOneNoteBackend final : public OneNoteBackend {
public:
    bool findTargetSection(OneNoteTarget*, OneNoteErrorKind* errorKind, QString* detail) override {
        *errorKind = OneNoteErrorKind::ComUnavailable;
        *detail = i18n::tr("aipdf::app::OneNoteExporter", "OneNote 桌面版只支持 Windows");
        return false;
    }

    bool createPageWithImage(const QString&, const QString&, const QByteArray&, OneNoteImageFormat,
                             const QSizeF&, OneNoteErrorKind* errorKind, QString* detail) override {
        *errorKind = OneNoteErrorKind::ComUnavailable;
        *detail = i18n::tr("aipdf::app::OneNoteExporter", "OneNote 桌面版只支持 Windows");
        return false;
    }
};

#endif  // Q_OS_WIN

// 造一个真实的后端（按平台挑）。
std::unique_ptr<OneNoteBackend> createOneNoteBackend() {
#ifdef Q_OS_WIN
    return std::make_unique<ComOneNoteBackend>();
#else
    return std::make_unique<UnsupportedOneNoteBackend>();
#endif
}

}  // namespace

// ------------------------------------------------------------------
// 本机环境
// ------------------------------------------------------------------

OneNoteEnvironment probeOneNoteEnvironment() {
    OneNoteEnvironment environment;
#ifdef Q_OS_WIN
    const QString clsid =
        readRegistryString(HKEY_CLASSES_ROOT, QStringLiteral("OneNote.Application\\CLSID"));
    if (clsid.isEmpty()) {
        environment.detail =
            QStringLiteral("注册表 HKCR\\OneNote.Application\\CLSID 未找到");
        return environment;
    }
    QString executable = readRegistryString(
        HKEY_CLASSES_ROOT, QStringLiteral("CLSID\\") + clsid + QStringLiteral("\\LocalServer32"));
    executable = executable.trimmed();
    if (executable.size() >= 2 && executable.startsWith(QLatin1Char('"')) &&
        executable.endsWith(QLatin1Char('"'))) {
        executable = executable.mid(1, executable.size() - 2);
    }
    environment.executablePath = executable;
    environment.installed = !executable.isEmpty();
    if (!environment.installed) {
        environment.detail = QStringLiteral("注册表里没有 OneNote.Application 的 LocalServer32");
    }
#else
    environment.detail = QStringLiteral("本平台没有 OneNote 桌面版");
#endif
    return environment;
}

// ------------------------------------------------------------------
// 导出计划
// ------------------------------------------------------------------

QVector<int> normalizeOneNotePageNumbers(const QVector<int>& selected, int totalPageCount) {
    QVector<int> result;
    if (totalPageCount <= 0) {
        return result;
    }
    if (selected.isEmpty()) {
        result.reserve(totalPageCount);
        for (int page = 1; page <= totalPageCount; ++page) {
            result.append(page);
        }
        return result;
    }
    for (int page : selected) {
        if (page < 1 || page > totalPageCount) {
            continue;
        }
        if (!result.contains(page)) {
            result.append(page);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

OneNoteExportPlan buildOneNoteExportPlan(const QString& documentTitle, int totalPageCount,
                                          const OneNoteExportOptions& options) {
    OneNoteExportPlan plan;
    plan.documentTitle = documentTitle;
    plan.pageNumbers = normalizeOneNotePageNumbers(options.pageNumbers, totalPageCount);
    plan.format = options.format;
    plan.dpi = options.dpi > 0 ? options.dpi : 144;
    return plan;
}

QString formatOneNotePageRange(const QVector<int>& pageNumbers) {
    if (pageNumbers.isEmpty()) {
        return i18n::tr("aipdf::app::OneNoteExporter", "（没有可导出的页面）");
    }
    QStringList parts;
    int start = pageNumbers.first();
    int previous = start;
    for (int index = 1; index <= pageNumbers.size(); ++index) {
        if (index < pageNumbers.size() && pageNumbers.at(index) == previous + 1) {
            previous = pageNumbers.at(index);
            continue;
        }
        parts << (start == previous ? QString::number(start)
                                   : QStringLiteral("%1-%2").arg(start).arg(previous));
        if (index < pageNumbers.size()) {
            start = pageNumbers.at(index);
            previous = start;
        }
    }
    return i18n::tr("aipdf::app::OneNoteExporter", "第 %1 页").arg(parts.join(QStringLiteral("、")));
}

QString oneNoteImageFormatLabel(OneNoteImageFormat format) {
    return format == OneNoteImageFormat::Png
               ? i18n::tr("aipdf::app::OneNoteExporter", "PNG 图片")
               : i18n::tr("aipdf::app::OneNoteExporter", "JPEG 图片");
}

QString oneNotePageTitle(const OneNoteExportPlan& plan, int pageNumber) {
    return fillPlaceholders(i18n::tr("aipdf::app::OneNoteExporter", "%1 - 第 %2 页"),
                            {plan.documentTitle, QString::number(pageNumber)});
}

// ------------------------------------------------------------------
// 图片与 XML
// ------------------------------------------------------------------

QByteArray encodeOneNoteImageBytes(const QImage& image, OneNoteImageFormat format, int dpi) {
    if (image.isNull()) {
        return QByteArray();
    }
    QImage source = image;
    if (format == OneNoteImageFormat::Jpeg && source.hasAlphaChannel()) {
        // JPEG 没有透明通道：直接写会得到黑底，得先压成不透明的 RGB。
        source = source.convertToFormat(QImage::Format_RGB32);
    }
    if (dpi > 0) {
        // 把分辨率写进图片文件：OneNote 按它算显示尺寸，不写的话 A4 页会被拉得巨大。
        // Qt 6 的 QImageWriter 没有 setResolution，只能改图像自己的点密度。
        const int dotsPerMeter = qRound(dpi / 0.0254);
        source.setDotsPerMeterX(dotsPerMeter);
        source.setDotsPerMeterY(dotsPerMeter);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return QByteArray();
    }
    QImageWriter writer(&buffer, format == OneNoteImageFormat::Png ? QByteArrayLiteral("png")
                                                                   : QByteArrayLiteral("jpeg"));
    if (!writer.write(source)) {
        return QByteArray();
    }
    buffer.close();
    return bytes;
}

QSizeF oneNoteImageDisplaySizePoints(const QImage& image, int dpi) {
    const double scale = dpi > 0 ? 72.0 / static_cast<double>(dpi) : 1.0;
    return QSizeF(image.width() * scale, image.height() * scale);
}

QString buildOneNotePageXml(const QString& pageId, const QString& pageTitle,
                             const QByteArray& imageFile, OneNoteImageFormat format,
                             const QSizeF& displaySizePoints) {
    const QString formatName =
        format == OneNoteImageFormat::Png ? QStringLiteral("png") : QStringLiteral("jpeg");
    // CDATA 里不能出现 "]]>"，否则整个 XML 直接坏掉。
    QString title = pageTitle;
    title.replace(QStringLiteral("]]>"), QStringLiteral("]]&gt;"));

    QString xml;
    xml.reserve(imageFile.size() * 2 + 512);
    xml += QStringLiteral("<?xml version=\"1.0\"?>");
    xml += QStringLiteral("<one:Page xmlns:one=\"");
    xml += QString::fromLatin1(kOneNoteNamespace);
    xml += QStringLiteral("\"");
    if (!pageId.isEmpty()) {
        xml += QStringLiteral(" ID=\"");
        xml += pageId.toHtmlEscaped();
        xml += QStringLiteral("\"");
    }
    xml += QStringLiteral(">");
    xml += QStringLiteral(
        "<one:PageSettings RTL=\"false\" color=\"automatic\"><one:PageSize><one:Automatic/>"
        "</one:PageSize><one:RuleLines visible=\"false\"/></one:PageSettings>");
    xml += QStringLiteral("<one:Title><one:OE alignment=\"left\"><one:T><![CDATA[");
    xml += title;
    xml += QStringLiteral("]]></one:T></one:OE></one:Title>");
    xml += QStringLiteral("<one:Outline><one:Position x=\"0\" y=\"0\"/><one:Size width=\"");
    xml += QString::number(displaySizePoints.width(), 'f', 1);
    xml += QStringLiteral("\" height=\"");
    xml += QString::number(displaySizePoints.height(), 'f', 1);
    xml += QStringLiteral("\"/><one:OEChildren><one:OE alignment=\"left\"><one:Image format=\"");
    xml += formatName;
    xml += QStringLiteral("\"><one:Data>");
    xml += QString::fromLatin1(imageFile.toBase64());
    xml += QStringLiteral("</one:Data></one:Image></one:OE></one:OEChildren></one:Outline>");
    xml += QStringLiteral("</one:Page>");
    return xml;
}

// ------------------------------------------------------------------
// 文案
// ------------------------------------------------------------------

QString oneNoteErrorMessage(OneNoteErrorKind kind, const QString& detail) {
    // 前两类失败给的是「该做什么」，技术细节挂在括号里补一句；
    // 导出失败那一类的 detail 本身就是给用户看的原因，直接跟在冒号后面。
    const QString technical =
        detail.isEmpty()
            ? QString()
            : i18n::tr("aipdf::app::OneNoteExporter", "（技术细节：%1）").arg(detail);
    switch (kind) {
    case OneNoteErrorKind::NotInstalled:
        return i18n::tr("aipdf::app::OneNoteExporter",
                        "未检测到本机安装 OneNote，无法把页面发送到 OneNote。"
                        "请先安装 Microsoft OneNote 桌面版后重试。") +
               technical;
    case OneNoteErrorKind::ComUnavailable:
        return i18n::tr("aipdf::app::OneNoteExporter",
                        "OneNote 已安装，但无法连接 OneNote 的后台接口。"
                        "请先手动打开一次 OneNote 完成首次设置后重试。") +
               technical;
    case OneNoteErrorKind::ExportFailed:
        return detail.isEmpty()
                   ? i18n::tr("aipdf::app::OneNoteExporter", "发送到 OneNote 失败。")
                   : i18n::tr("aipdf::app::OneNoteExporter", "发送到 OneNote 失败：%1").arg(detail);
    case OneNoteErrorKind::None:
    default:
        return QString();
    }
}

QString oneNoteSuccessMessage(const OneNoteExportPlan& plan, int createdPageCount) {
    QString location = plan.sectionName;
    if (!plan.notebookName.isEmpty()) {
        location = plan.sectionName.isEmpty()
                       ? plan.notebookName
                       : plan.notebookName + QStringLiteral(" / ") + plan.sectionName;
    }
    return fillPlaceholders(
        i18n::tr("aipdf::app::OneNoteExporter",
                 "已把 %1 导出为 %2（%3 DPI），并发送到 OneNote 的「%4」，共新建 %5 页。"),
        {formatOneNotePageRange(plan.pageNumbers), oneNoteImageFormatLabel(plan.format),
         QString::number(plan.dpi), location, QString::number(createdPageCount)});
}

// ------------------------------------------------------------------
// 导出器
// ------------------------------------------------------------------

OneNoteExporter::OneNoteExporter() : OneNoteExporter(probeOneNoteEnvironment(), nullptr) {}

OneNoteExporter::OneNoteExporter(const OneNoteEnvironment& environment)
    : OneNoteExporter(environment, nullptr) {}

OneNoteExporter::OneNoteExporter(const OneNoteEnvironment& environment,
                                 std::unique_ptr<OneNoteBackend> backend)
    : environment_(environment), backend_(std::move(backend)) {
    if (!backend_) {
        backend_ = createOneNoteBackend();
    }
}

OneNoteExportResult OneNoteExporter::exportToOneNote(const OneNoteExportPlan& plan,
                                                      const OneNotePageRenderer& renderer) {
    OneNoteExportResult result;

    // 失败出口统一走这里：消息里要带上失败原因，
    // 中途失败时还要说明「已经送出去几页」——否则用户不知道 OneNote 里已经有东西了。
    auto fail = [&result](OneNoteErrorKind kind, const QString& detail) {
        result.errorKind = kind;
        result.detail = detail;
        result.message = oneNoteErrorMessage(kind, detail);
        if (!result.createdPageTitles.isEmpty()) {
            result.message +=
                i18n::tr("aipdf::app::OneNoteExporter", "（已送出 %1 页）")
                    .arg(result.createdPageTitles.size());
        }
        return result;
    };

    // 1) 没装 OneNote：最常见、也最容易解释的一种，先判。
    if (!environment_.installed) {
        return fail(OneNoteErrorKind::NotInstalled, environment_.detail);
    }

    // 2) 没有可导的页 / 没有渲染回调：都是调用方给错了参数。
    if (plan.pageNumbers.isEmpty()) {
        return fail(OneNoteErrorKind::ExportFailed,
                    i18n::tr("aipdf::app::OneNoteExporter", "没有可导出的页面"));
    }
    if (!renderer) {
        return fail(OneNoteErrorKind::ExportFailed,
                    i18n::tr("aipdf::app::OneNoteExporter", "缺少页面渲染回调"));
    }

    // 3) 找到要把内容送进去的分区。
    OneNoteTarget target;
    OneNoteErrorKind errorKind = OneNoteErrorKind::None;
    QString detail;
    if (!backend_ || !backend_->findTargetSection(&target, &errorKind, &detail)) {
        return fail(errorKind == OneNoteErrorKind::None ? OneNoteErrorKind::ExportFailed : errorKind,
                    detail);
    }

    // 4) 一页一页地送：每个 PDF 页在 OneNote 里各占一页，标题写清是第几页。
    for (int pageNumber : plan.pageNumbers) {
        const QImage image = renderer(pageNumber - 1, plan.dpi);
        if (image.isNull()) {
            return fail(OneNoteErrorKind::ExportFailed,
                        i18n::tr("aipdf::app::OneNoteExporter", "第 %1 页渲染成图片失败")
                            .arg(pageNumber));
        }
        const QByteArray imageFile = encodeOneNoteImageBytes(image, plan.format, plan.dpi);
        if (imageFile.isEmpty()) {
            return fail(OneNoteErrorKind::ExportFailed,
                        i18n::tr("aipdf::app::OneNoteExporter", "第 %1 页编码成 %2 失败")
                            .arg(pageNumber)
                            .arg(oneNoteImageFormatLabel(plan.format)));
        }

        const QString title = oneNotePageTitle(plan, pageNumber);
        if (!backend_->createPageWithImage(target.sectionId, title, imageFile, plan.format,
                                           oneNoteImageDisplaySizePoints(image, plan.dpi),
                                           &errorKind, &detail)) {
            return fail(errorKind == OneNoteErrorKind::None ? OneNoteErrorKind::ExportFailed
                                                            : errorKind,
                        detail);
        }
        result.createdPageTitles.append(title);
    }

    // 5) 成功：把「导了哪几页、导成什么、送到哪一页」讲清楚。
    OneNoteExportPlan effective = plan;
    effective.notebookName = target.notebookName;
    effective.sectionName = target.sectionName;
    result.ok = true;
    result.notebookName = target.notebookName;
    result.sectionName = target.sectionName;
    result.message = oneNoteSuccessMessage(effective, result.createdPageCount());
    return result;
}

}  // namespace aipdf::app
