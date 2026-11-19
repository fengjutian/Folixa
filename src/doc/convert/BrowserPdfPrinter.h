#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <memory>

class QTemporaryDir;

namespace aipdf::doc {

// ================================================== 用系统浏览器打印网页 ====
//
// 领域层模块：把一个网页（网址或一段 HTML）交给**本机的浏览器引擎**
// 渲染并打印成 PDF。
//
// 为什么走浏览器而不是自己拼 PDF：自己拼只能把 HTML 拆成文字再排版，
// **不执行 JavaScript、不渲染 CSS**，页面上一半的东西（脚本插进去的图、
// CSS 背景图、响应式布局）根本进不了产物。浏览器引擎是本机现成的、
// 渲染结果与用户自己在浏览器里看到的一致，代价是要起一个外部进程、
// 且结果依赖本机装了什么浏览器——所以「有没有引擎」「引擎起没起起来」
// 「产物是不是坏文件」这三件事必须各自能分辨，不能糊成一句「转换失败」。
//
// 边界（调用方要知道的部分）：
//   * 依赖**本机装了 Chromium 内核的浏览器**（Edge / Chrome / Chromium）。
//     一个都没有时本模块只能报「没有可用引擎」，这不是 bug，是环境事实。
//   * 打印走浏览器的 `--print-to-pdf`，产物是浏览器自己排的版：
//     页边距、页眉页脚、纸张都按浏览器默认来，本模块不接管分页。
//   * 脚本要跑完就得给浏览器一点时间：`virtualTimeBudgetMs` 是这个预算。
//     给小了会截断还没渲染完的页面，给大了会拖慢每一次打印。
//   * 临时 HTML 文件用完即删（见 BrowserPrintWorkspace）：不留垃圾，
//     也不往仓库目录里写东西。

// 一个可用的浏览器引擎。
struct BrowserEngine {
    // 显示名："Edge" / "Chrome" / "Chromium"。界面上拿它告诉用户
    // 「这次是用哪个引擎渲染的」——不同引擎排版有细微差别，用户需要知道。
    QString name;
    // 可执行文件的绝对路径。
    QString executable;
    // 文件版本号（读不到时为空，例如 "157.0.4341.46"）。
    QString version;
};

// 探测引擎时的**候选**。
//
// 为什么把「文件在不在」做成数据而不是在规则里现查：
// 引擎的排序 / 去重 / 排除规则是纯逻辑，必须能脱离文件系统单测——
// 单测里塞几条假路径就能验证这些规则，不必真的去装浏览器。
// 真正去摸文件系统的活儿只发生在 detectBrowserEngines() 里。
struct BrowserCandidate {
    // 可执行文件路径。
    QString path;
    // 这个路径上真的有可执行文件吗。
    bool exists = false;
    // 文件版本号（探测时读出来，认不出就留空）。
    QString version;
};

// 候选 → 引擎列表（**纯逻辑**，不碰文件系统）。
//
// 规则（顺序即优先级）：
//   1. 不存在的路径直接丢掉：引擎列表里只留真能跑的。
//   2. 同一个路径只留一条：注册表 App Paths 与常见安装路径常常指向同一个文件，
//      不去重的话同一个 Edge 会出现两次。
//   3. 同一个引擎家族只留一条（版本新的优先，再按路径排，保证结果稳定）。
//   4. 家族优先级：Edge → Chrome → Chromium。
//      Edge 排第一是因为它是 Windows 自带的，装机命中率最高；
//      Chromium 排最后是因为它基本只出现在开发机上。
QVector<BrowserEngine> buildBrowserEngines(const QVector<BrowserCandidate>& candidates);

// 从可执行文件路径认出引擎家族（"…\\msedge.exe" → "Edge"）。
// 认不出来返回空串：这个函数不猜，猜错会让界面上出现一个名不副实的引擎。
QString browserEngineNameForExecutable(const QString& path);

// 探测本机可用的浏览器引擎（注册表 App Paths + 常见安装路径），按优先级排好。
// extraPaths 是额外的候选路径（调用方自己知道的路径），与其它候选一起参与排序。
// 一个都没有时返回空列表——调用方据此报「本机没有可用的浏览器引擎」。
QVector<BrowserEngine> detectBrowserEngines(const QStringList& extraPaths = QStringList());

// 打印参数。
struct BrowserPrintOptions {
    // 要打开的网址（http/https）。html 为空时用它。
    QUrl url;
    // 直接给 HTML 字节。给了就落成临时文件、用 file:// 打开（html 优先于 url）。
    QByteArray html;
    // 临时 HTML 文件名的基名（默认 "aipdf-web"）。
    // 只影响临时文件名，不影响内容。
    QString htmlBaseName = QStringLiteral("aipdf-web");
    // 打印进程的超时（毫秒）。到点会杀掉进程并报中文原因，不留孤儿进程。
    int timeoutMs = 60000;
    // 交给浏览器的时间预算（毫秒）：等 JS 执行完、等图片加载完。
    int virtualTimeBudgetMs = 3000;
    // 要不要页眉页脚。默认不要（网页转 PDF 里那行日期和 URL 很少有人要）。
    bool includeHeaderFooter = false;
};

// 失败原因分类。
//
// 为什么要有分类：四类失败的处理完全不同——没引擎是**环境问题**
// （提示用户去装浏览器），启动失败与超时是**本次运行的问题**
// （可以重试、可以换引擎），产物打不开是**结果有问题**
// （绝不能把坏文件交给用户）。混成一句「转换失败」，界面就没法给出有用的话。
enum class BrowserPrintError {
    // 没失败。
    None,
    // 本机没有可用的浏览器引擎。
    NoEngine,
    // 浏览器起不来（可执行文件有问题、或被系统拦了）。
    LaunchFailed,
    // 等超时了，进程已被杀掉。
    Timeout,
    // 产物打不开、或页数为 0（坏文件，不许交出去）。
    InvalidOutput,
    // 压根没有可打印的东西（既没网址也没 HTML）。
    InvalidInput,
};

// 打印结果。
struct BrowserPrintResult {
    // 成功为真；失败时 bytes 为空、error 写明原因（中文）。
    bool ok = false;
    // 产物 PDF 字节（失败时为空）。
    QByteArray bytes;
    // 失败原因（中文）。成功时为空。
    QString error;
    // 这次实际用的是哪个引擎（成功时非空）。
    QString engineName;
    // 产物的页数（用 PDFium 打开后数出来的）。
    int pageCount = 0;
    // 首选引擎失败、换了备选时为真。
    bool usedFallbackEngine = false;
    // 失败原因分类。
    BrowserPrintError errorKind = BrowserPrintError::None;
    // 本次用过的临时目录（打印结束后**已经被删掉**）。
    // 暴露出来是为了让「临时文件用完要清掉」这条不变量能被单测核对：
    // 打印结束后断言这个目录已经不存在。
    QString temporaryDirectory;
};

// 打印工作区：临时目录 + 里面的三个路径。
//
// 为什么要单独暴露：临时 HTML 文件的生命周期是这条链上唯一的**副作用**，
// 必须能被单独观察（写出来了没有、删掉了没有）。所以把它拆成一个
// 可准备、可清理的对象，而不是藏在 printWebPageToPdf 里面。
struct BrowserPrintWorkspace {
    // 临时目录（系统临时目录下，不在仓库里）。
    QString directory;
    // HTML 落盘后的路径（没有给 html 时为空）。
    QString htmlFilePath;
    // 浏览器要写的 PDF 路径。
    QString outputPdfPath;
    // 浏览器 profile 目录（给 --user-data-dir 用，避免动用户的默认 profile）。
    QString userDataDir;
    // 临时目录的持有者（QTemporaryDir 析构时会把目录删掉）。
    // 不把它放进结构体的话，prepare 返回时目录就被删了。
    std::unique_ptr<QTemporaryDir> temporaryDir;
    bool isValid() const { return !directory.isEmpty(); }
};

// 临时 HTML 文件名（**纯函数**）：只用基名，去掉路径分隔符与非法字符，
// 补 ".html" 后缀。基名为空时用 "aipdf-web"。
QString browserTemporaryHtmlFileName(const QString& baseName);

// 准备打印工作区：建临时目录、把 HTML 落盘、算出输出路径与 profile 目录。
// 失败返回 false 并给出中文原因（error 不为空）。
bool prepareBrowserPrintWorkspace(const BrowserPrintOptions& options,
                                  BrowserPrintWorkspace* workspace,
                                  QString* error);

// 清理打印工作区（递归删临时目录）。可以重复调用。
void cleanupBrowserPrintWorkspace(BrowserPrintWorkspace* workspace);

// 产物校验结果。
struct BrowserPdfValidation {
    // 合格为真；不合格时 error 写明中文原因。
    bool ok = false;
    // 产物页数（PDFium 数出来的）。
    int pageCount = 0;
    // 不合格的原因（中文）。
    QString error;
};

// 产物页数的下限（**纯判定**）。
//
// 单独成函数是为了让这条下限能被单独断言：负例对照把 `> 0` 改成 `>= 0`，
// 「零页必须判不合格」这条用例必须当场变红。
bool hasEnoughBrowserPdfPages(int pageCount);

// 校验浏览器产物：能不能当 PDF 用、页数够不够。
// 坏文件一律判不合格——绝不能把打不开的 PDF 交给用户。
BrowserPdfValidation validateBrowserPdfOutput(const QByteArray& bytes);

// 拼装浏览器命令行参数（**纯函数**）。
//
// 单独成函数是为了能逐项断言（"--headless=new" 在不在、"--print-to-pdf=" 有没有、
// "--user-data-dir" 有没有重复），不必真的起一个浏览器。
//
// 参数清单为什么是这几条：
//   * --headless=new：不带界面地跑；没有它会真的弹出一个浏览器窗口。
//   * --virtual-time-budget=N：给 JS 与图片留出时间预算，预算走完就打印。
//   * --user-data-dir=…：用一份干净的临时 profile。不给的话浏览器会去抢
//     用户正在用的默认 profile（用户开着浏览器时直接失败），还会往用户
//     的 profile 里写缓存。
//   * --print-to-pdf=…：产物路径。必须是绝对路径，浏览器按它落盘。
//   * --print-to-pdf-no-header：默认不要页眉页脚。
//   * --disable-extensions / --hide-scrollbars：扩展可能改页面，
//     滚动条会占掉页面宽度，截图式产物里不该有它们。
QStringList buildBrowserPrintArguments(const BrowserPrintOptions& options,
                                       const QString& targetUrl,
                                       const QString& outputPdfPath,
                                       const QString& userDataDir);

// 用**给定**的引擎列表渲染并打印成 PDF。
//
// 与 printWebPageToPdf 的区别：这里**不做自动探测**——引擎列表为空就是
// 「没有引擎」，直接报「本机没有可用的浏览器引擎」。
// 「空列表」这条不变量必须能被单测覆盖，所以留一个不兜底的入口。
BrowserPrintResult printWebPageToPdfWithEngines(const BrowserPrintOptions& options,
                                                const QVector<BrowserEngine>& engines);

// 渲染并打印成 PDF。
//
// engines 为空时**自动探测**本机引擎；探测到一个都没有时报
// 「本机没有可用的浏览器引擎」。
// 首选引擎失败（起不来 / 超时 / 产物坏掉）会自动换下一个引擎，
// 换了备选时 result.usedFallbackEngine 为真。
BrowserPrintResult printWebPageToPdf(const BrowserPrintOptions& options,
                                     const QVector<BrowserEngine>& engines = QVector<BrowserEngine>());

// 引擎名 → 可读标签（界面上显示用，例如 "Edge 157.0.4341.46"）。
QString browserEngineLabel(const BrowserEngine& engine);

}  // namespace aipdf::doc
