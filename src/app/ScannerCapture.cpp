#include "app/ScannerCapture.h"

#include "pdf/ImageToPdf.h"

#include <QComboBox>
#include <QCloseEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#endif

namespace aipdf::app {

QString scanErrorMessage(ScanErrorKind kind, const QString& detail) {
    QString base;
    switch (kind) {
    case ScanErrorKind::None:
        base = i18n::tr("aipdf::app::ScannerCapture", "扫描完成");
        break;
    case ScanErrorKind::EnumerationFailed:
        base = i18n::tr("aipdf::app::ScannerCapture",
                        "无法启动 Windows 图像采集（WIA），本机可能没有安装扫描仪支持");
        break;
    case ScanErrorKind::NoDevice:
        base = i18n::tr("aipdf::app::ScannerCapture", "没有找到扫描仪");
        break;
    case ScanErrorKind::UserCancelled:
        base = i18n::tr("aipdf::app::ScannerCapture", "已取消扫描，没有生成 PDF");
        break;
    case ScanErrorKind::TransferFailed:
        base = i18n::tr("aipdf::app::ScannerCapture", "从扫描仪取图失败，请检查设备是否被占用或卡纸");
        break;
    case ScanErrorKind::UnsupportedImage:
        base = i18n::tr("aipdf::app::ScannerCapture",
                        "扫描仪返回的图像格式不受支持（本程序只支持 BMP、JPEG 与 PNG）");
        break;
    case ScanErrorKind::PdfFailed:
        base = i18n::tr("aipdf::app::ScannerCapture", "扫描结果转成 PDF 失败");
        break;
    }
    if (detail.isEmpty()) {
        return base;
    }
    return base + i18n::tr("aipdf::app::ScannerCapture", "（技术细节：%1）").arg(detail);
}

QString ScannerDevice::displayName() const {
    QString label = name.trimmed();
    if (label.isEmpty()) {
        label = deviceId.trimmed();
    }
    if (label.isEmpty()) {
        label = i18n::tr("aipdf::app::ScannerCapture", "未命名设备");
    }
    if (isScanner()) {
        return label;
    }
    // 不是扫描仪的设备也要标出来：用户点了才发现是相机会以为程序坏了。
    return i18n::tr("aipdf::app::ScannerCapture", "%1（非扫描仪）").arg(label);
}

#ifdef Q_OS_WIN

namespace {

// ==================================================== COM 晚绑定工具 ========

// WIA 自动化层的 ProgID。
//
// 为什么用 ProgID 而不是写死 CLSID：写死的 GUID 要 WIA SDK 的头文件才有，
// 而本仓库不装 WIA SDK（#import "Wia.tlb" 会让编译机直接编不过）。
// ProgID 直接向系统问，编译机上有没有 WIA 都不影响编译。
constexpr const wchar_t* kWiaDeviceManagerProgId = L"WIA.DeviceManager";

// WIA 的传输格式 ID。
//
// 为什么点名要格式：领域层只支持 BMP 与 JPEG（JPEG 还能原样嵌进 PDF），
// 设备默认多半给 BMP，但也有驱动默认给 TIFF——那时只能明确报错，
// 不能塞一份解不了的字节进去。
constexpr const wchar_t* kWiaFormatBmp = L"{B96B3CAB-0728-11D3-9D7B-0000F81EF32E}";
constexpr const wchar_t* kWiaFormatJpeg = L"{B96B3CAE-0728-11D3-9D7B-0000F81EF32E}";

// 持有 IDispatch 指针的最小 RAII 包装。
class ComDispatch {
public:
    ComDispatch() = default;
    explicit ComDispatch(IDispatch* dispatch) : dispatch_(dispatch) {}
    ComDispatch(const ComDispatch&) = delete;
    ComDispatch& operator=(const ComDispatch&) = delete;
    ComDispatch(ComDispatch&& other) noexcept : dispatch_(other.dispatch_) {
        other.dispatch_ = nullptr;
    }
    ComDispatch& operator=(ComDispatch&& other) noexcept {
        if (this != &other) {
            reset(other.dispatch_);
            other.dispatch_ = nullptr;
        }
        return *this;
    }
    ~ComDispatch() { reset(); }

    void reset(IDispatch* dispatch = nullptr) {
        if (dispatch_ != nullptr) {
            dispatch_->Release();
        }
        dispatch_ = dispatch;
    }
    IDispatch* get() const { return dispatch_; }
    bool isNull() const { return dispatch_ == nullptr; }

private:
    IDispatch* dispatch_ = nullptr;
};

// COM 初始化 / 反初始化配对。
//
// 为什么必须配对：CoInitializeEx 每调一次就要 CoUninitialize 一次。
// 少一次会漏掉 apartment，多一次会把 COM 提前卸载。
class ComApartment {
public:
    ComApartment() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // S_OK：这次是我们初始化的，最后必须还回去。
        // S_FALSE：同一线程已经初始化过，不要再还（还了就把别人的 apartment 拆了）。
        // RPC_E_CHANGED_MODE：线程被别的模型初始化过，不能再改；
        //   COM 本身仍然可用（跨线程调用要编组而已），所以继续往下走。
        owned_ = SUCCEEDED(hr);
        usable_ = (hr == RPC_E_CHANGED_MODE) || SUCCEEDED(hr);
    }
    ~ComApartment() {
        if (owned_) {
            CoUninitialize();
        }
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    bool usable() const { return usable_; }

private:
    bool owned_ = false;
    bool usable_ = false;
};

// 线程级 COM apartment。
//
// 为什么必须是线程级、而不是每次调用建一个：
// COM 对象是在 apartment 里创建的。apartment 一旦被 CoUninitialize 拆掉，
// 还拿着这个对象的指针继续调方法就是未定义行为 —— 表现是「时好时坏」的崩溃
// （实测 WIA 设备枚举 40 次里崩 11 次，崩在 GetIDsOfNames 里）。
//
// 线程级 apartment 活到线程结束，所有 COM 指针在整个生命周期内都有效；
// CoInitializeEx / CoUninitialize 的配对也仍然平衡（每个线程各一次）。
thread_local ComApartment g_comApartment;

// 一次调用的参数。
//
// 为什么不用 QVector<VARIANT>：VARIANT 是联合体，默认拷贝是浅拷贝，
// 放进去的 BSTR 会被二次释放。参数少，就用定长数组。
class ComArgs {
public:
    static constexpr int kMax = 4;

    ComArgs() {
        for (int i = 0; i < kMax; ++i) {
            VariantInit(&values_[i]);
        }
    }
    ~ComArgs() {
        for (int i = 0; i < kMax; ++i) {
            VariantClear(&values_[i]);
        }
    }
    ComArgs(const ComArgs&) = delete;
    ComArgs& operator=(const ComArgs&) = delete;

    void addInt(long value) {
        if (count_ >= kMax) return;
        values_[count_].vt = VT_I4;
        values_[count_].lVal = value;
        ++count_;
    }

    void addBstrWide(const wchar_t* text) {
        if (count_ >= kMax || text == nullptr) return;
        values_[count_].vt = VT_BSTR;
        values_[count_].bstrVal = SysAllocString(text);
        ++count_;
    }

    void addBstrNarrow(const char* text) {
        if (count_ >= kMax || text == nullptr) return;
        values_[count_].vt = VT_BSTR;
#if defined(OLE2ANSI)
        values_[count_].bstrVal = SysAllocString(text);
#else
        const std::wstring buffer = QString::fromUtf8(text).toStdWString();
        values_[count_].bstrVal = SysAllocString(buffer.c_str());
#endif
        ++count_;
    }

    int count() const { return count_; }
    const VARIANT* at(int index) const { return &values_[index]; }

private:
    VARIANT values_[kMax];
    int count_ = 0;
};

// 取 IDispatch 上某个成员的 DISPID。
//
// 注意 GetIDsOfNames **没有** wFlags 参数（Windows SDK 与 MinGW 一致）：
// wFlags 是 Invoke 的参数，在这里传是传错位置——
// 多传一个参数会让真实实现把 DISPID 写到 wFlags 的地址上，直接踩坏内存。
HRESULT memberDispId(IDispatch* target, const char* name, DISPID* out) {
    if (target == nullptr || name == nullptr || out == nullptr) {
        return E_INVALIDARG;
    }
#if defined(OLE2ANSI)
    const std::string buffer(name);
    LPOLESTR namePtr = const_cast<LPOLESTR>(buffer.c_str());
#else
    const std::wstring buffer = QString::fromUtf8(name).toStdWString();
    LPOLESTR namePtr = const_cast<LPOLESTR>(buffer.c_str());
#endif
    return target->GetIDsOfNames(IID_NULL, &namePtr, 1, LOCALE_USER_DEFAULT, out);
}

// 调一个方法。
HRESULT invokeDispatch(IDispatch* target, const char* name, ComArgs& args, VARIANT* result) {
    if (target == nullptr || result == nullptr) {
        return E_INVALIDARG;
    }
    DISPID dispid = 0;
    HRESULT hr = memberDispId(target, name, &dispid);
    if (FAILED(hr)) {
        return hr;
    }
    // DISPPARAMS 要求参数**逆序**：rgvarg[0] 是最后一个参数。
    //
    // 用 VariantCopy 而不是直接赋值：直接赋值是浅拷贝，BSTR 会被清两次。
    VARIANT reversed[ComArgs::kMax];
    for (int i = 0; i < ComArgs::kMax; ++i) {
        VariantInit(&reversed[i]);
    }
    for (int i = 0; i < args.count(); ++i) {
        VariantCopy(&reversed[i], const_cast<VARIANT*>(args.at(args.count() - 1 - i)));
    }
    DISPPARAMS params{};
    params.cArgs = static_cast<DWORD>(args.count());
    params.rgvarg = args.count() > 0 ? reversed : nullptr;
    params.cNamedArgs = 0;
    params.rgdispidNamedArgs = nullptr;

    EXCEPINFO exception{};
    UINT argError = 0;
    hr = target->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, result,
                        &exception, &argError);
    for (int i = 0; i < ComArgs::kMax; ++i) {
        VariantClear(&reversed[i]);
    }
    if (FAILED(hr)) {
        VariantClear(result);
        VariantInit(result);
    }
    return hr;
}

// 调一个「最后一个参数可选」的方法，并且这次不传那个可选参数。
//
// COM 的规矩：省略可选参数要传一个 DISP_E_PARAMNOTFOUND 的错误变体，
// 并把它标成命名参数；直接少传一个参数只会拿到 E_INVALIDARG。
HRESULT invokeDispatchWithoutOptional(IDispatch* target, const char* name, VARIANT* result) {
    if (target == nullptr || result == nullptr) {
        return E_INVALIDARG;
    }
    DISPID dispid = 0;
    HRESULT hr = memberDispId(target, name, &dispid);
    if (FAILED(hr)) {
        return hr;
    }
    VARIANT missing{};
    VariantInit(&missing);
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    DISPID namedArg = DISPID_PROPERTYPUT;
    DISPPARAMS params{};
    params.cArgs = 1;
    params.rgvarg = &missing;
    params.cNamedArgs = 1;
    params.rgdispidNamedArgs = &namedArg;

    EXCEPINFO exception{};
    UINT argError = 0;
    hr = target->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, result,
                        &exception, &argError);
    if (FAILED(hr)) {
        VariantClear(result);
        VariantInit(result);
    }
    return hr;
}

// 取一个属性。
HRESULT getPropertyDispatch(IDispatch* target, const char* name, VARIANT* result) {
    if (target == nullptr || result == nullptr) {
        return E_INVALIDARG;
    }
    DISPID dispid = 0;
    HRESULT hr = memberDispId(target, name, &dispid);
    if (FAILED(hr)) {
        return hr;
    }
    DISPPARAMS params{};
    EXCEPINFO exception{};
    UINT argError = 0;
    hr = target->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &params,
                        result, &exception, &argError);
    if (FAILED(hr)) {
        VariantClear(result);
        VariantInit(result);
    }
    return hr;
}

// 写一个属性（只在设备支持时才用，失败就当没写）。
HRESULT setPropertyDispatch(IDispatch* target, const char* name, VARIANT value) {
    if (target == nullptr || name == nullptr) {
        return E_INVALIDARG;
    }
    DISPID dispid = 0;
    HRESULT hr = memberDispId(target, name, &dispid);
    if (FAILED(hr)) {
        return hr;
    }
    VARIANT args[1];
    VariantInit(&args[0]);
    // VariantCopy：直接赋值是浅拷贝，清掉副本会把调用方的值一起清掉。
    const HRESULT copied = VariantCopy(&args[0], &value);
    if (FAILED(copied)) {
        return copied;
    }
    DISPPARAMS params{};
    params.cArgs = 1;
    params.rgvarg = args;
    EXCEPINFO exception{};
    UINT argError = 0;
    hr = target->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &params,
                        nullptr, &exception, &argError);
    VariantClear(&args[0]);
    return hr;
}

// 从 VARIANT 里取出对象并**接管**它的引用计数。
//
// 关键点：VARIANT 自己持有一个引用。直接把指针塞进 RAII 包装、随后 VariantClear，
// 等于把这个引用 release 掉一次，包装里剩下的就是野指针——
// 用到它时可能正常（内存还没被复用），也可能直接崩，表现为「时好时坏」。
// 所以这里先 AddRef 再取走，VariantClear 释放 VARIANT 自己那一份，
// 包装持有的是新加的那一份。
ComDispatch takeDispatch(VARIANT& value) {
    ComDispatch result;
    if (value.vt == VT_DISPATCH && value.pdispVal != nullptr) {
        value.pdispVal->AddRef();
        result.reset(value.pdispVal);
    }
    return result;
}

// 取一个返回对象的属性（调用方拿到的是**带引用计数**的指针）。
ComDispatch getPropertyObject(IDispatch* target, const char* name) {
    VARIANT value{};
    VariantInit(&value);
    ComDispatch result;
    if (SUCCEEDED(getPropertyDispatch(target, name, &value))) {
        result = takeDispatch(value);
    }
    VariantClear(&value);
    return result;
}

// 集合的 Item：先按名字找，找不到就退回默认成员（DISPID 0）。
//
// 为什么要退回默认成员：WIA 自动化层的集合是用默认成员暴露的，
// 某些版本上 GetIDsOfNames("Item") 会失败，但默认成员一定在。
HRESULT callCollectionItem(IDispatch* collection, const VARIANT& index, VARIANT* result) {
    if (collection == nullptr || result == nullptr) {
        return E_INVALIDARG;
    }
    DISPID dispid = 0;
    HRESULT hr = memberDispId(collection, "Item", &dispid);
    if (FAILED(hr)) {
        dispid = 0;
    }
    VARIANT args[1];
    VariantInit(&args[0]);
    // 必须 VariantCopy 而不是直接赋值：直接赋值是浅拷贝，
    // 结束时清掉这份拷贝会把调用方的 BSTR 一起释放掉（调用方再清一次就双重释放）。
    const HRESULT copied = VariantCopy(&args[0], const_cast<VARIANT*>(&index));
    if (FAILED(copied)) {
        return copied;
    }
    DISPPARAMS params{};
    params.cArgs = 1;
    params.rgvarg = args;
    EXCEPINFO exception{};
    UINT argError = 0;
    hr = collection->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params,
                            result, &exception, &argError);
    VariantClear(&args[0]);
    if (FAILED(hr)) {
        VariantClear(result);
        VariantInit(result);
    }
    return hr;
}

// 集合的 Count。
int collectionCount(IDispatch* collection) {
    if (collection == nullptr) {
        return 0;
    }
    VARIANT value{};
    VariantInit(&value);
    int count = 0;
    if (SUCCEEDED(getPropertyDispatch(collection, "Count", &value))) {
        switch (value.vt) {
        case VT_I4:
            count = value.lVal;
            break;
        case VT_I2:
            count = value.iVal;
            break;
        case VT_UI4:
            count = static_cast<int>(value.ulVal);
            break;
        default:
            break;
        }
    }
    VariantClear(&value);
    return count > 0 ? count : 0;
}

// 取集合里的第 index 个（1 基，WIA 自动化层的下标就是从 1 开始）。
ComDispatch collectionItem(IDispatch* collection, int index) {
    ComDispatch result;
    if (collection == nullptr || index < 1) {
        return result;
    }
    VARIANT key{};
    VariantInit(&key);
    key.vt = VT_I4;
    key.lVal = index;
    VARIANT value{};
    VariantInit(&value);
    if (SUCCEEDED(callCollectionItem(collection, key, &value))) {
        result = takeDispatch(value);
    }
    VariantClear(&value);
    VariantClear(&key);
    return result;
}

// 变体转字符串。
QString variantToString(const VARIANT& value) {
    switch (value.vt) {
    case VT_BSTR:
        return value.bstrVal != nullptr ? QString::fromWCharArray(value.bstrVal) : QString();
    case VT_LPSTR:
        // MinGW 的 VARIANT 联合体里没有 pszVal/pwszVal，窄字符串放在 pcVal。
        return value.pcVal != nullptr ? QString::fromUtf8(value.pcVal) : QString();
    case VT_I4:
        return QString::number(value.lVal);
    default:
        break;
    }
    return QString();
}

// 变体转整数。
int variantToInt(const VARIANT& value) {
    switch (value.vt) {
    case VT_I4:
        return value.lVal;
    case VT_I2:
        return value.iVal;
    case VT_UI4:
        return static_cast<int>(value.ulVal);
    case VT_BOOL:
        return value.boolVal != VARIANT_FALSE ? 1 : 0;
    case VT_BSTR:
        return variantToString(value).toInt();
    default:
        break;
    }
    return 0;
}

// 读一个「按名字取的属性」：先拿 Properties 集合，再按名字取项，最后读它的 Value。
//
// 为什么绕这一圈：WIA 自动化层把设备属性放在一个 PropertyBag 里，
// 取值要按名字（"Name"、"Device ID"…），没有直接挂在对象上的属性名。
QString readNamedProperty(IDispatch* owner, const char* propertyName) {
    if (owner == nullptr || propertyName == nullptr) {
        return QString();
    }
    ComDispatch properties = getPropertyObject(owner, "Properties");
    if (properties.isNull()) {
        return QString();
    }
    VARIANT key{};
    VariantInit(&key);
    key.vt = VT_BSTR;
#if defined(OLE2ANSI)
    key.bstrVal = SysAllocString(propertyName);
#else
    const std::wstring buffer = QString::fromUtf8(propertyName).toStdWString();
    key.bstrVal = SysAllocString(buffer.c_str());
#endif
    VARIANT item{};
    VariantInit(&item);
    QString result;
    if (SUCCEEDED(callCollectionItem(properties.get(), key, &item)) && item.vt == VT_DISPATCH &&
        item.pdispVal != nullptr) {
        ComDispatch property = takeDispatch(item);
        VARIANT value{};
        VariantInit(&value);
        if (SUCCEEDED(getPropertyDispatch(property.get(), "Value", &value))) {
            result = variantToString(value);
        }
        VariantClear(&value);
    }
    VariantClear(&item);
    VariantClear(&key);
    return result;
}

// 读一个直接挂在对象上的整型属性。
int readIntProperty(IDispatch* owner, const char* propertyName) {
    if (owner == nullptr || propertyName == nullptr) {
        return 0;
    }
    VARIANT value{};
    VariantInit(&value);
    int result = 0;
    if (SUCCEEDED(getPropertyDispatch(owner, propertyName, &value))) {
        result = variantToInt(value);
    }
    VariantClear(&value);
    return result;
}

// 读一个直接挂在对象上的字符串属性。
QString readStringProperty(IDispatch* owner, const char* propertyName) {
    if (owner == nullptr || propertyName == nullptr) {
        return QString();
    }
    VARIANT value{};
    VariantInit(&value);
    QString result;
    if (SUCCEEDED(getPropertyDispatch(owner, propertyName, &value))) {
        result = variantToString(value);
    }
    VariantClear(&value);
    return result;
}

// 打开 WIA 设备管理器。
ComDispatch createDeviceManager(QString* detail) {
    ComDispatch manager;
    // 用线程级的 apartment：它的生命周期覆盖后面所有的 COM 调用。
    // 这里要是用函数局部变量，函数一返回 apartment 就被拆了，
    // 返回给调用方的 COM 指针会变成野指针。
    ComApartment& apartment = g_comApartment;
    if (!apartment.usable()) {
        *detail = QStringLiteral("COM is not usable on this thread");
        return manager;
    }
    CLSID clsid{};
    HRESULT hr = CLSIDFromProgID(kWiaDeviceManagerProgId, &clsid);
    if (FAILED(hr)) {
        *detail = QStringLiteral("CLSIDFromProgID failed (0x%1)")
                      .arg(static_cast<unsigned long>(hr), 8, 16, QLatin1Char('0'));
        return manager;
    }
    IDispatch* dispatch = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_IDispatch,
                          reinterpret_cast<void**>(&dispatch));
    if (FAILED(hr) || dispatch == nullptr) {
        *detail = QStringLiteral("CoCreateInstance failed (0x%1)")
                      .arg(static_cast<unsigned long>(hr), 8, 16, QLatin1Char('0'));
        if (dispatch != nullptr) {
            dispatch->Release();
        }
        return manager;
    }
    manager.reset(dispatch);
    return manager;
}

// 从一个 ImageFile 对象里取出原始字节。
//
// WIA 的 ImageFile.FileData 给的是一个 Vector 对象，
// 它的 BinaryData 才是真正的 SAFEARRAY 字节。
QByteArray readImageFileData(IDispatch* imageFile, QString* detail) {
    if (imageFile == nullptr) {
        *detail = QStringLiteral("no image file object");
        return {};
    }
    ComDispatch vector = getPropertyObject(imageFile, "FileData");
    if (vector.isNull()) {
        *detail = QStringLiteral("ImageFile has no FileData");
        return {};
    }
    VARIANT binary{};
    VariantInit(&binary);
    QByteArray bytes;
    if (SUCCEEDED(getPropertyDispatch(vector.get(), "BinaryData", &binary))) {
        if (binary.vt == (VT_ARRAY | VT_UI1) && binary.parray != nullptr) {
            long low = 0;
            long high = 0;
            if (SUCCEEDED(SafeArrayGetLBound(binary.parray, 1, &low)) &&
                SUCCEEDED(SafeArrayGetUBound(binary.parray, 1, &high)) && high >= low) {
                void* data = nullptr;
                if (SUCCEEDED(SafeArrayAccessData(binary.parray, &data)) && data != nullptr) {
                    const qint64 count = static_cast<qint64>(high) - low + 1;
                    const qint64 size =
                        count * static_cast<qint64>(SafeArrayGetElemsize(binary.parray));
                    bytes = QByteArray(static_cast<const char*>(data),
                                       static_cast<qsizetype>(size));
                    SafeArrayUnaccessData(binary.parray);
                }
            }
        } else {
            *detail = QStringLiteral("BinaryData is not a byte array (vt=0x%1)")
                          .arg(static_cast<unsigned long>(binary.vt), 0, 16);
        }
    } else {
        *detail = QStringLiteral("Vector has no BinaryData");
    }
    VariantClear(&binary);
    return bytes;
}

// 从一个 WIA 项上取一张图。
//
// 先点名要 BMP，再要 JPEG，最后用设备默认格式。
QByteArray transferItem(IDispatch* item, QString* detail) {
    if (item == nullptr) {
        return {};
    }
    const wchar_t* formats[] = {kWiaFormatBmp, kWiaFormatJpeg};
    for (const wchar_t* format : formats) {
        ComArgs args;
        args.addBstrWide(format);
        VARIANT image{};
        VariantInit(&image);
        const HRESULT hr = invokeDispatch(item, "Transfer", args, &image);
        if (SUCCEEDED(hr) && image.vt == VT_DISPATCH && image.pdispVal != nullptr) {
            ComDispatch imageFile = takeDispatch(image);
            VariantClear(&image);
            const QByteArray bytes = readImageFileData(imageFile.get(), detail);
            if (!bytes.isEmpty()) {
                return bytes;
            }
            continue;
        }
        VariantClear(&image);
    }
    // 设备默认格式：不传 FormatID。
    VARIANT image{};
    VariantInit(&image);
    if (SUCCEEDED(invokeDispatchWithoutOptional(item, "Transfer", &image)) &&
        image.vt == VT_DISPATCH && image.pdispVal != nullptr) {
        ComDispatch imageFile = takeDispatch(image);
        VariantClear(&image);
        return readImageFileData(imageFile.get(), detail);
    }
    VariantClear(&image);
    return {};
}

// 递归找可传输的图像项。
//
// 为什么递归：平板扫描仪的根项下面还有一层「图像」子项，
// 直接传根项在部分驱动上是拿不到图的。
void collectImages(IDispatch* item, QVector<QByteArray>* frames, int maxPages, int depth,
                   QString* detail) {
    if (item == nullptr || frames == nullptr) {
        return;
    }
    if (maxPages > 0 && frames->size() >= maxPages) {
        return;
    }
    if (depth <= 3) {
        const QByteArray bytes = transferItem(item, detail);
        if (!bytes.isEmpty()) {
            frames->append(bytes);
            return;
        }
    }
    if (depth >= 3) {
        return;
    }
    ComDispatch children = getPropertyObject(item, "Items");
    if (children.isNull()) {
        return;
    }
    const int count = collectionCount(children.get());
    for (int i = 1; i <= count; ++i) {
        ComDispatch child = collectionItem(children.get(), i);
        if (child.isNull()) {
            continue;
        }
        collectImages(child.get(), frames, maxPages, depth + 1, detail);
        if (maxPages > 0 && frames->size() >= maxPages) {
            return;
        }
    }
}

// 给一个项设置分辨率（设备不支持时忽略）。
void applyResolution(IDispatch* item, int dpi) {
    if (item == nullptr || dpi <= 0) {
        return;
    }
    for (const char* name : {"Horizontal Resolution", "Vertical Resolution"}) {
        ComDispatch properties = getPropertyObject(item, "Properties");
        if (properties.isNull()) {
            continue;
        }
        VARIANT key{};
        VariantInit(&key);
        key.vt = VT_BSTR;
#if defined(OLE2ANSI)
        key.bstrVal = SysAllocString(name);
#else
        const std::wstring buffer = QString::fromUtf8(name).toStdWString();
        key.bstrVal = SysAllocString(buffer.c_str());
#endif
        VARIANT itemValue{};
        VariantInit(&itemValue);
        if (SUCCEEDED(callCollectionItem(properties.get(), key, &itemValue)) &&
            itemValue.vt == VT_DISPATCH && itemValue.pdispVal != nullptr) {
            ComDispatch property = takeDispatch(itemValue);
            VARIANT value{};
            VariantInit(&value);
            value.vt = VT_I4;
            value.lVal = dpi;
            setPropertyDispatch(property.get(), "Value", value);
            VariantClear(&value);
        }
        VariantClear(&itemValue);
        VariantClear(&key);
    }
}

// 取设备的 Device ID。
//
// WIA 自动化层既能在 PropertyBag 里按名字取，也可能在对象上直接挂一个 DeviceID；
// 两条路都试一遍，取到哪条算哪条。
QString readDeviceId(IDispatch* info) {
    QString id = readNamedProperty(info, "Device ID");
    if (id.isEmpty()) {
        id = readStringProperty(info, "DeviceID");
    }
    return id;
}

}  // namespace

#else  // !Q_OS_WIN

namespace {

// 非 Windows 平台没有 WIA：枚举一律失败，并说清楚原因。
ComDispatch createDeviceManager(QString* detail) {
    *detail = QStringLiteral("WIA is only available on Windows");
    return ComDispatch();
}

}  // namespace

#endif  // Q_OS_WIN

// WIA 帧源：默认实现。
//
// 为什么把 WIA 的活儿单独拎成一个类：取帧要开 COM、连设备、传数据，
// 这些在**没有扫描仪的机器上也必须能编译、能跑**（只能报「没找到设备」），
// 所以它不碰任何界面控件；而且它可以被一个假源替换掉（测试用），
// 于是「取帧 → 排页序 → 拼 PDF」这条链在没有扫描仪的机器上也能被验证。
//
// 注意：这里的 WIA 行为与原来的 ScannerCapture::capture 完全一致，
// 只是把「一次取完」改成「先攒着，再一帧一帧交」。
class WiaScanFrameSource final : public ScanFrameSource {
public:
    QVector<ScannerDevice> enumerateDevices(ScanErrorKind* errorKind, QString* detail) override {
        QVector<ScannerDevice> devices;
        *errorKind = ScanErrorKind::None;
        *detail = QString();
#ifdef Q_OS_WIN
        ComDispatch manager = createDeviceManager(detail);
        if (manager.isNull()) {
            *errorKind = ScanErrorKind::EnumerationFailed;
            return devices;
        }
        ComDispatch infos = getPropertyObject(manager.get(), "DeviceInfos");
        if (infos.isNull()) {
            *errorKind = ScanErrorKind::EnumerationFailed;
            *detail = QStringLiteral("DeviceManager has no DeviceInfos");
            return devices;
        }
        const int total = collectionCount(infos.get());
        for (int i = 1; i <= total; ++i) {
            ComDispatch info = collectionItem(infos.get(), i);
            if (info.isNull()) {
                continue;
            }
            ScannerDevice device;
            device.deviceId = readDeviceId(info.get());
            device.name = readNamedProperty(info.get(), "Name");
            device.deviceType = readIntProperty(info.get(), "Type");
            if (device.deviceId.isEmpty() && device.name.isEmpty()) {
                continue;
            }
            devices.append(device);
        }
        if (devices.isEmpty()) {
            *errorKind = ScanErrorKind::NoDevice;
            *detail = QStringLiteral("DeviceManager reported %1 device(s)").arg(total);
        }
#else
        *errorKind = ScanErrorKind::EnumerationFailed;
        *detail = QStringLiteral("WIA is only available on Windows");
#endif
        return devices;
    }

    ScanFrameStatus openDevice(const ScannerDevice& device, const ScannerCaptureOptions& options,
                               QString* detail) override {
        frames_.clear();
        cursor_ = 0;
#ifdef Q_OS_WIN
        ComDispatch manager = createDeviceManager(detail);
        if (manager.isNull()) {
            return ScanFrameStatus::EnumerationFailed;
        }
        ComDispatch infos = getPropertyObject(manager.get(), "DeviceInfos");
        if (infos.isNull()) {
            *detail = QStringLiteral("DeviceManager has no DeviceInfos");
            return ScanFrameStatus::EnumerationFailed;
        }
        const int total = collectionCount(infos.get());
        ComDispatch info;
        for (int i = 1; i <= total && info.isNull(); ++i) {
            ComDispatch candidate = collectionItem(infos.get(), i);
            if (candidate.isNull()) {
                continue;
            }
            if (readDeviceId(candidate.get()) == device.deviceId) {
                info = std::move(candidate);
            }
        }
        if (info.isNull()) {
            *detail = QStringLiteral("device id not found: %1").arg(device.deviceId);
            return ScanFrameStatus::NoDevice;
        }

        ComArgs noArgs;
        VARIANT connected{};
        VariantInit(&connected);
        if (FAILED(invokeDispatch(info.get(), "Connect", noArgs, &connected)) ||
            connected.vt != VT_DISPATCH || connected.pdispVal == nullptr) {
            *detail = QStringLiteral("could not connect to the device");
            VariantClear(&connected);
            return ScanFrameStatus::Failed;
        }
        ComDispatch deviceObject = takeDispatch(connected);
        VariantClear(&connected);

        ComDispatch items = getPropertyObject(deviceObject.get(), "Items");
        if (items.isNull()) {
            *detail = QStringLiteral("device has no Items collection");
            return ScanFrameStatus::Failed;
        }
        const int count = collectionCount(items.get());
        for (int i = 1; i <= count; ++i) {
            if (options.maxPages > 0 && frames_.size() >= options.maxPages) {
                break;
            }
            ComDispatch item = collectionItem(items.get(), i);
            if (item.isNull()) {
                continue;
            }
            applyResolution(item.get(), options.dpi);
            collectImages(item.get(), &frames_, options.maxPages, 0, detail);
        }
        if (frames_.isEmpty()) {
            if (detail->isEmpty()) {
                *detail = QStringLiteral("no image returned by the device");
            }
            return ScanFrameStatus::Failed;
        }
        return ScanFrameStatus::Ok;
#else
        *detail = QStringLiteral("WIA is only available on Windows");
        return ScanFrameStatus::EnumerationFailed;
#endif
    }

    ScanFrameStatus nextFrame(QByteArray* frame, QString* detail) override {
        Q_UNUSED(detail)
        if (cursor_ >= frames_.size()) {
            return ScanFrameStatus::NoMoreFrames;
        }
        *frame = frames_.at(cursor_);
        ++cursor_;
        return ScanFrameStatus::Ok;
    }

private:
    // 已经取回来的帧（按页序）。
    //
    // 为什么一次取完再一帧一帧交：WIA 的传输是「推」式的——Transfer 一次给一张图，
    // 自动进纸器的下一页要再调一次 Transfer。要把它变成「拉」式的接口，
    // 就得在源这一侧先攒着。
    QVector<QByteArray> frames_;
    int cursor_ = 0;
};

// 帧源的状态 → 用户能看懂的原因。
ScanErrorKind mapFrameStatus(ScanFrameStatus status) {
    switch (status) {
    case ScanFrameStatus::NoDevice:
        return ScanErrorKind::NoDevice;
    case ScanFrameStatus::EnumerationFailed:
        return ScanErrorKind::EnumerationFailed;
    case ScanFrameStatus::Cancelled:
        return ScanErrorKind::UserCancelled;
    case ScanFrameStatus::Ok:
    case ScanFrameStatus::NoMoreFrames:
    case ScanFrameStatus::Failed:
    default:
        return ScanErrorKind::TransferFailed;
    }
}

// ============================================================== 后端 ========

ScannerCapture::ScannerCapture()
    : ownedSource_(std::make_unique<WiaScanFrameSource>()), source_(ownedSource_.get()) {}

ScannerCapture::ScannerCapture(ScanFrameSource* source) : source_(source) {
    // 注进来一个空指针就当没注：默认行为仍然是 WIA，
    // 调用方不必为「要不要用假源」写两套代码。
    if (source_ == nullptr) {
        ownedSource_ = std::make_unique<WiaScanFrameSource>();
        source_ = ownedSource_.get();
    }
}

ScannerCapture::~ScannerCapture() = default;

ScannerCapture::DeviceQuery ScannerCapture::queryDevices() {
    DeviceQuery query;
    QString detail;
    query.errorKind = ScanErrorKind::None;
    query.devices = source_->enumerateDevices(&query.errorKind, &detail);
    query.detail = detail;
    return query;
}

QVector<ScannerDevice> ScannerCapture::listDevices(QString* error) {
    const DeviceQuery query = queryDevices();
    if (query.devices.isEmpty() && error != nullptr) {
        *error = scanErrorMessage(query.errorKind, query.detail);
    }
    return query.devices;
}

ScanOutcome ScannerCapture::capture(const ScannerDevice& device,
                                    const ScannerCaptureOptions& options) {
    ScanOutcome outcome;
    outcome.ok = false;
    QString detail;

    const ScanFrameStatus opened = source_->openDevice(device, options, &detail);
    if (opened != ScanFrameStatus::Ok) {
        outcome.errorKind = mapFrameStatus(opened);
        outcome.errorDetail = detail;
        outcome.errorMessage = scanErrorMessage(outcome.errorKind, outcome.errorDetail);
        return outcome;
    }

    while (true) {
        QByteArray frame;
        const ScanFrameStatus status = source_->nextFrame(&frame, &detail);
        if (status == ScanFrameStatus::NoMoreFrames) {
            break;
        }
        if (status != ScanFrameStatus::Ok) {
            outcome.errorKind = mapFrameStatus(status);
            outcome.errorDetail = detail;
            outcome.errorMessage = scanErrorMessage(outcome.errorKind, outcome.errorDetail);
            outcome.frames.clear();
            return outcome;
        }
        if (frame.isEmpty()) {
            outcome.errorKind = ScanErrorKind::TransferFailed;
            outcome.errorDetail = QStringLiteral("the device returned an empty frame");
            outcome.errorMessage = scanErrorMessage(outcome.errorKind, outcome.errorDetail);
            outcome.frames.clear();
            return outcome;
        }
        // 帧格式不支持要在**取帧这一层**就报出来：拖到拼 PDF 时才发现，
        // 用户看到的是「转 PDF 失败」，根本不知道是扫描仪给的格式不对。
        if (pdf::detectImageFormat(frame) == pdf::ImageFormat::Unknown) {
            outcome.errorKind = ScanErrorKind::UnsupportedImage;
            outcome.errorDetail =
                QStringLiteral("unsupported image format: %1").arg(pdf::describeImageFormat(frame));
            outcome.errorMessage = scanErrorMessage(outcome.errorKind, outcome.errorDetail);
            outcome.frames.clear();
            return outcome;
        }
        outcome.frames.append(frame);
        if (options.maxPages > 0 && outcome.frames.size() >= options.maxPages) {
            break;
        }
    }

    if (outcome.frames.isEmpty()) {
        outcome.errorKind = ScanErrorKind::TransferFailed;
        outcome.errorDetail =
            detail.isEmpty() ? QStringLiteral("no image returned by the device") : detail;
        outcome.errorMessage = scanErrorMessage(outcome.errorKind, outcome.errorDetail);
        outcome.frames.clear();
        return outcome;
    }
    outcome.ok = true;
    outcome.errorKind = ScanErrorKind::None;
    return outcome;
}

ScanOutcome ScannerCapture::scanToPdf(const ScannerCaptureOptions& options, QByteArray* pdfOut) {
    ScanOutcome outcome;
    outcome.ok = false;

    const DeviceQuery query = queryDevices();
    if (query.devices.isEmpty()) {
        outcome.errorKind =
            query.errorKind == ScanErrorKind::None ? ScanErrorKind::NoDevice : query.errorKind;
        outcome.errorDetail = query.detail;
        outcome.errorMessage = scanErrorMessage(outcome.errorKind, outcome.errorDetail);
        return outcome;
    }

    // 优先用扫描仪；本机只有相机/摄像头时也照样能扫（图像设备都行）。
    int index = 0;
    for (int i = 0; i < query.devices.size(); ++i) {
        if (query.devices.at(i).isScanner()) {
            index = i;
            break;
        }
    }
    outcome = capture(query.devices.at(index), options);
    if (!outcome.ok) {
        return outcome;
    }

    pdf::ImageToPdfOptions pdfOptions;
    pdfOptions.dpi = options.dpi > 0 ? static_cast<double>(options.dpi) : 300.0;
    const pdf::ImageToPdfResult result = pdf::imagesToPdf(outcome.frames, pdfOptions);
    if (!result.ok()) {
        outcome.ok = false;
        outcome.errorKind = result.error == pdf::ImageToPdfError::UnsupportedFormat
                                ? ScanErrorKind::UnsupportedImage
                                : ScanErrorKind::PdfFailed;
        outcome.errorDetail = result.detail;
        outcome.errorMessage = scanErrorMessage(outcome.errorKind, outcome.errorDetail);
        outcome.frames.clear();
        return outcome;
    }
    if (pdfOut != nullptr) {
        *pdfOut = result.pdf;
    }
    return outcome;
}

// ============================================================== 对话框 ========

ScannerCaptureDialog::ScannerCaptureDialog(QWidget* parent) : QDialog(parent) {
    buildUi();
    refreshDevices();
}

void ScannerCaptureDialog::buildUi() {
    setObjectName(QStringLiteral("scannerCaptureDialog"));
    setModal(true);
    resize(460, 260);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    deviceBox_ = new QComboBox(this);
    deviceBox_->setObjectName(QStringLiteral("scannerDeviceBox"));
    deviceBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    form->addRow(deviceBox_);

    dpiBox_ = new QSpinBox(this);
    dpiBox_->setObjectName(QStringLiteral("scannerDpiBox"));
    dpiBox_->setRange(72, 1200);
    dpiBox_->setSingleStep(25);
    dpiBox_->setValue(300);
    form->addRow(dpiBox_);

    layout->addLayout(form);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("scannerStatusLabel"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    refreshButton_ = new QPushButton(this);
    refreshButton_->setObjectName(QStringLiteral("scannerRefreshButton"));
    scanButton_ = new QPushButton(this);
    scanButton_->setObjectName(QStringLiteral("scannerScanButton"));
    acceptButton_ = new QPushButton(this);
    acceptButton_->setObjectName(QStringLiteral("scannerAcceptButton"));
    acceptButton_->setEnabled(false);
    cancelButton_ = new QPushButton(this);
    cancelButton_->setObjectName(QStringLiteral("scannerCancelButton"));

    buttons->addWidget(refreshButton_);
    buttons->addStretch(1);
    buttons->addWidget(scanButton_);
    buttons->addWidget(acceptButton_);
    buttons->addWidget(cancelButton_);
    layout->addLayout(buttons);

    connect(refreshButton_, &QPushButton::clicked, this, &ScannerCaptureDialog::refreshDevices);
    connect(scanButton_, &QPushButton::clicked, this, &ScannerCaptureDialog::startScan);
    connect(acceptButton_, &QPushButton::clicked, this, &ScannerCaptureDialog::accept);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    texts_.bind([this] { setWindowTitle(i18n::tr("aipdf::app::ScannerCapture", "从扫描仪新建 PDF")); });
    texts_.bind([this] { deviceBox_->setAccessibleName(i18n::tr("aipdf::app::ScannerCapture", "扫描仪")); });
    texts_.bind([this] { dpiBox_->setAccessibleName(i18n::tr("aipdf::app::ScannerCapture", "扫描分辨率")); });
    texts_.bind([this] { dpiBox_->setSuffix(i18n::tr("aipdf::app::ScannerCapture", " DPI")); });
    texts_.bind([this] { refreshButton_->setText(i18n::tr("aipdf::app::ScannerCapture", "刷新设备")); });
    texts_.bind([this] { scanButton_->setText(i18n::tr("aipdf::app::ScannerCapture", "扫描")); });
    texts_.bind([this] { acceptButton_->setText(i18n::tr("aipdf::app::ScannerCapture", "生成 PDF")); });
    texts_.bind([this] { cancelButton_->setText(i18n::tr("aipdf::app::ScannerCapture", "取消")); });
    texts_.bind([this] {
        if (statusLabel_ != nullptr && statusLabel_->text().isEmpty()) {
            statusLabel_->setText(i18n::tr("aipdf::app::ScannerCapture", "选择设备后点「扫描」。"));
        }
    });
}

void ScannerCaptureDialog::refreshDevices() {
    devices_.clear();
    deviceBox_->clear();
    QString error;
    const QVector<ScannerDevice> devices = ScannerCapture().listDevices(&error);
    devices_ = devices;
    for (const ScannerDevice& device : devices) {
        deviceBox_->addItem(device.displayName());
    }
    if (devices.isEmpty()) {
        showMessage(error.isEmpty() ? i18n::tr("aipdf::app::ScannerCapture", "没有找到扫描仪") : error,
                     true);
        scanButton_->setEnabled(false);
        return;
    }
    scanButton_->setEnabled(true);
    showMessage(i18n::tr("aipdf::app::ScannerCapture", "找到 %1 台图像设备。").arg(devices.size()),
                false);
}

void ScannerCaptureDialog::startScan() {
    const int index = deviceBox_->currentIndex();
    if (index < 0 || index >= devices_.size()) {
        showMessage(i18n::tr("aipdf::app::ScannerCapture", "请先选择一台设备"), true);
        return;
    }
    setBusy(true);
    ScannerCaptureOptions options;
    options.dpi = dpiBox_->value();
    ScannerCapture capture;
    QByteArray pdf;
    const ScanOutcome outcome = capture.scanToPdf(options, &pdf);
    setBusy(false);
    if (!outcome.ok) {
        pdfBytes_.clear();
        pageCount_ = 0;
        scanned_ = false;
        acceptButton_->setEnabled(false);
        showMessage(outcome.errorMessage, true);
        return;
    }
    pdfBytes_ = pdf;
    pageCount_ = outcome.pageCount();
    scanned_ = true;
    acceptButton_->setEnabled(true);
    showMessage(i18n::tr("aipdf::app::ScannerCapture", "已扫描 %1 页，可以点「生成 PDF」。").arg(pageCount_),
                false);
}

void ScannerCaptureDialog::setBusy(bool busy) {
    scanButton_->setEnabled(!busy && !devices_.isEmpty());
    refreshButton_->setEnabled(!busy);
    deviceBox_->setEnabled(!busy);
    dpiBox_->setEnabled(!busy);
}

void ScannerCaptureDialog::showMessage(const QString& text, bool isError) {
    if (statusLabel_ == nullptr) {
        return;
    }
    statusLabel_->setText(text);
    // 错误用醒目的颜色，一眼能分出「扫完了」和「出事了」。
    statusLabel_->setStyleSheet(isError ? QStringLiteral("color: #c0392b;") : QString());
}

void ScannerCaptureDialog::done(int result) {
    // 没扫出东西就关掉 = 用户自己放弃，不能当成扫描失败。
    if (result != QDialog::Accepted && !scanned_) {
        cancelReason_ = ScanErrorKind::UserCancelled;
    }
    QDialog::done(result);
}

}  // namespace aipdf::app
