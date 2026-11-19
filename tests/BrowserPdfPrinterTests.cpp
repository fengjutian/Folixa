// BrowserPdfPrinter 的单元测试。
//
// 覆盖四组东西：
//   1. 引擎探测的**纯规则**（排序 / 去重 / 排除 / 家族识别）——用假路径喂进去，
//      不必真的装浏览器。
//   2. 命令行参数的**逐项断言**——不必真的起一个浏览器。
//   3. 四类失败原因必须能分辨开（没有引擎 / 启动失败 / 超时 / 产物坏掉）。
//   4. 真实渲染：JS 注入的正文会让页数变多（这是「JS 真的执行了」的可核对证据）。
//
// 为什么第 3 组不用真浏览器：这个测试文件自己就能当"假浏览器"用——
// 把自己的可执行文件填进引擎列表，子进程按环境变量扮演不同的坏引擎
// （挂住不退出 / 直接退出不写文件 / 写一份垃圾 PDF / 写一份好 PDF）。
// 这样超时与坏产物这两类失败在任何机器上都能稳定复现，不依赖本机浏览器。
//
// 真实渲染那一条在**没有浏览器的机器上会 QSKIP**（不红）：那台机器上
// 确实没有浏览器可测，如实跳过比假装通过好。

#include "doc/convert/BrowserPdfPrinter.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QThread>
#include <QVector>

using aipdf::doc::BrowserCandidate;
using aipdf::doc::BrowserEngine;
using aipdf::doc::BrowserPdfValidation;
using aipdf::doc::BrowserPrintError;
using aipdf::doc::BrowserPrintOptions;
using aipdf::doc::BrowserPrintResult;
using aipdf::doc::BrowserPrintWorkspace;

namespace {

// 子进程扮演哪种"坏引擎"。父进程用环境变量告诉子进程。
const char* const kChildModeEnv = "AIPDF_BROWSER_TEST_CHILD_MODE";

const char* const kModeHang = "hang";      // 挂住不退出（测超时）
const char* const kModeExit0 = "exit0";    // 立刻正常退出但不写文件（测产物缺失）
const char* const kModeGarbage = "garbage";// 写一份垃圾文件（测产物打不开）
const char* const kModeGood = "good";      // 写一份合法的 2 页 PDF（测正常路径与备选引擎）

// 造一份最小可用的 PDF（页数由参数决定）。
//
// 为什么自己造：这份测试要能证明「页数下限 > 0」这条规则，
// 手上得有一份**页数已知**的 PDF；而页数是 0 的那种 PDF 根本没法从
// 正常的转换流程里拿到，只能自己造。
QByteArray minimalPdfBytes(int pageCount) {
    if (pageCount < 0) {
        pageCount = 0;
    }
    QString kids;
    QVector<QByteArray> pageBodies;
    for (int index = 0; index < pageCount; ++index) {
        const int pageId = 3 + index * 2;
        const int contentId = pageId + 1;
        if (index > 0) {
            kids += QLatin1Char(' ');
        }
        kids += QStringLiteral("%1 0 R").arg(pageId);
        pageBodies.append(QStringLiteral("<< /Type /Page /Parent 2 0 R "
                                         "/MediaBox [0 0 200 200] /Contents %1 0 R >>")
                              .arg(contentId)
                              .toLatin1());
        pageBodies.append(QByteArray("<< /Length 0 >>\nstream\n\nendstream"));
    }

    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    int objectId = 1;
    const auto appendObject = [&out, &offsets, &objectId](const QByteArray& body) {
        offsets.insert(objectId, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(objectId).toLatin1();
        out += body;
        out += QByteArray("\nendobj\n");
        ++objectId;
    };

    appendObject(QByteArray("<< /Type /Catalog /Pages 2 0 R >>"));
    appendObject(QStringLiteral("<< /Type /Pages /Kids [%1] /Count %2 >>")
                     .arg(kids)
                     .arg(pageCount)
                     .toLatin1());
    for (const QByteArray& body : pageBodies) {
        appendObject(body);
    }

    const int xrefOffset = out.size();
    const int count = objectId;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 从命令行里找出 "--print-to-pdf=" 后面那个路径。
QString printToPdfPathFromArguments(const QStringList& arguments) {
    const QString prefix = QStringLiteral("--print-to-pdf=");
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) {
            return argument.mid(prefix.size());
        }
    }
    return {};
}

// 一个装在纸上的页面：CSS 渐变背景 + 文字。
QByteArray plainWebPage() {
    return QByteArray(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>body{margin:0;font-family:sans-serif}"
        ".bg{width:600px;height:200px;background:linear-gradient(90deg,#135,#a97);color:#fff;padding:20px}"
        "</style></head><body><div class=\"bg\">CSS 背景（渐变）</div></body></html>");
}

// 同一份页面，再加上 80 段由脚本插入的正文。
//
// 这是「JS 真的执行了」与「CSS 真的渲染了」的可核对证据：
// 脚本插进去的 80 段会把页面撑成好几页，页数因此变多。
QByteArray scriptedWebPage() {
    QByteArray page = plainWebPage();
    page.replace("</body>",
                  "<div id=\"slot\"></div><script>"
                  "for (var i = 0; i < 80; i++) {"
                  "  var p = document.createElement('p');"
                  "  p.textContent = 'JS injected line ' + i;"
                  "  document.getElementById('slot').appendChild(p);"
                  "}"
                  "</script></body>");
    return page;
}

}  // namespace

class BrowserPdfPrinterTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- 引擎识别与探测 ----

    void theEngineNameIsRecognizedFromTheFileName();
    void theEngineListIsSortedByFamilyPriority();
    void theEngineListDropsMissingPaths();
    void theEngineListRemovesDuplicatePaths();
    void theEngineListKeepsTheNewestOfEachFamily();
    void theDetectedEnginesAreUsableOnThisMachine();

    // ---- 参数拼装 ----

    void thePrintArgumentsCarryTheRequiredFlags();
    void thePrintArgumentsHaveExactlyOneUserDataDir();
    void thePrintArgumentsFollowTheHeaderFooterChoice();

    // ---- 四类失败原因 ----

    void theEmptyEngineListReportsNoEngine();
    void theMissingExecutableReportsLaunchFailure();
    void theTimeoutIsReportedAsTimeout();
    void theMissingOutputIsReportedAsInvalidOutput();
    void theGarbageOutputIsReportedAsInvalidOutput();

    // ---- 备选引擎与产物校验 ----

    void theFallbackEngineIsUsedWhenTheFirstEngineFails();
    void theEmptyOutputIsRejected();
    void theGarbageOutputIsRejected();
    void thePageCountIsReadFromTheProducedPdf();
    void theZeroPageOutputIsRejected();

    // ---- 临时文件生命周期 ----

    void theTemporaryHtmlFileIsWrittenAndRemoved();
    void theTemporaryDirectoryLivesOutsideTheRepository();

    // ---- 真实渲染（没有浏览器时跳过）----

    void theHtmlIsPrintedWithJavaScriptExecuted();
};

void BrowserPdfPrinterTests::initTestCase() {
    // 子进程模式的环境变量必须从干净的起点开始。
    qunsetenv(kChildModeEnv);
}

// ============================================================ 引擎识别 ======

void BrowserPdfPrinterTests::theEngineNameIsRecognizedFromTheFileName() {
    QCOMPARE(aipdf::doc::browserEngineNameForExecutable(
                 QStringLiteral("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe")),
             QStringLiteral("Edge"));
    QCOMPARE(aipdf::doc::browserEngineNameForExecutable(
                 QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe")),
             QStringLiteral("Chrome"));
    QCOMPARE(aipdf::doc::browserEngineNameForExecutable(
                 QStringLiteral("C:/Program Files/Chromium/Application/chromium.exe")),
             QStringLiteral("Chromium"));
    // Chromium 的正式版可执行文件也叫 chrome.exe，只能靠目录名区分。
    QCOMPARE(aipdf::doc::browserEngineNameForExecutable(
                 QStringLiteral("C:/Program Files/Chromium/Application/chrome.exe")),
             QStringLiteral("Chromium"));
    // 认不出来就不该硬猜：界面上出现一个名不副实的引擎比没有更糟。
    QVERIFY(aipdf::doc::browserEngineNameForExecutable(QStringLiteral("C:/x/notepad.exe")).isEmpty());
    QVERIFY(aipdf::doc::browserEngineNameForExecutable(QString()).isEmpty());
}

void BrowserPdfPrinterTests::theEngineListIsSortedByFamilyPriority() {
    QVector<BrowserCandidate> candidates;
    candidates.append({QStringLiteral("C:/x/chromium.exe"), true, QStringLiteral("120.0.1")});
    candidates.append({QStringLiteral("C:/x/chrome.exe"), true, QStringLiteral("130.0.1")});
    candidates.append({QStringLiteral("C:/x/msedge.exe"), true, QStringLiteral("140.0.1")});

    const QVector<BrowserEngine> engines = aipdf::doc::buildBrowserEngines(candidates);
    QCOMPARE(engines.size(), 3);
    // Edge → Chrome → Chromium：Edge 是 Windows 自带的，装机命中率最高。
    QCOMPARE(engines.at(0).name, QStringLiteral("Edge"));
    QCOMPARE(engines.at(1).name, QStringLiteral("Chrome"));
    QCOMPARE(engines.at(2).name, QStringLiteral("Chromium"));
}

void BrowserPdfPrinterTests::theEngineListDropsMissingPaths() {
    QVector<BrowserCandidate> candidates;
    candidates.append({QStringLiteral("C:/x/msedge.exe"), false, QString()});
    candidates.append({QStringLiteral("C:/x/chrome.exe"), true, QStringLiteral("130.0.1")});

    const QVector<BrowserEngine> engines = aipdf::doc::buildBrowserEngines(candidates);
    // 装不上的路径不能进引擎列表（否则每次打印都白等一次启动失败）。
    QCOMPARE(engines.size(), 1);
    QCOMPARE(engines.at(0).name, QStringLiteral("Chrome"));
}

void BrowserPdfPrinterTests::theEngineListRemovesDuplicatePaths() {
    QVector<BrowserCandidate> candidates;
    // 同一个文件的两种写法：注册表给的是反斜杠、常见路径给的是正斜杠。
    candidates.append({QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe"), true,
                        QStringLiteral("130.0.1")});
    candidates.append({QStringLiteral("c:\\program files\\google\\chrome\\application\\CHROME.EXE"),
                        true, QStringLiteral("130.0.1")});

    const QVector<BrowserEngine> engines = aipdf::doc::buildBrowserEngines(candidates);
    // 去重之后同一个引擎只出现一次。
    QCOMPARE(engines.size(), 1);
}

void BrowserPdfPrinterTests::theEngineListKeepsTheNewestOfEachFamily() {
    QVector<BrowserCandidate> candidates;
    candidates.append({QStringLiteral("C:/old/msedge.exe"), true, QStringLiteral("100.0.0")});
    candidates.append({QStringLiteral("C:/new/msedge.exe"), true, QStringLiteral("157.0.4341.46")});

    const QVector<BrowserEngine> engines = aipdf::doc::buildBrowserEngines(candidates);
    // 同一个家族只留一条，而且留的是版本新的那条。
    QCOMPARE(engines.size(), 1);
    QCOMPARE(engines.at(0).version, QStringLiteral("157.0.4341.46"));
    QCOMPARE(engines.at(0).executable, QStringLiteral("C:/new/msedge.exe"));
}

void BrowserPdfPrinterTests::theDetectedEnginesAreUsableOnThisMachine() {
    const QVector<BrowserEngine> engines = aipdf::doc::detectBrowserEngines();
    // 这台机器上有没有浏览器都行：有的话每一条都必须真的能用。
    for (const BrowserEngine& engine : engines) {
        QVERIFY(!engine.name.isEmpty());
        QVERIFY(QFileInfo::exists(engine.executable));
        QVERIFY(!aipdf::doc::browserEngineLabel(engine).isEmpty());
    }
}

// ============================================================ 参数拼装 ======

void BrowserPdfPrinterTests::thePrintArgumentsCarryTheRequiredFlags() {
    BrowserPrintOptions options;
    options.virtualTimeBudgetMs = 4321;

    const QStringList arguments = aipdf::doc::buildBrowserPrintArguments(
        options, QStringLiteral("file:///C:/tmp/x.html"), QStringLiteral("C:/tmp/out.pdf"),
        QStringLiteral("C:/tmp/profile"));

    QVERIFY(arguments.contains(QStringLiteral("--headless=new")));
    QVERIFY(arguments.contains(QStringLiteral("--virtual-time-budget=4321")));
    // 默认不要页眉页脚。
    QVERIFY(arguments.contains(QStringLiteral("--print-to-pdf-no-header")));

    // --print-to-pdf= 只能有一个（两个会互相覆盖，产物路径说不清）。
    int printToPdfCount = 0;
    for (const QString& argument : arguments) {
        if (argument.startsWith(QStringLiteral("--print-to-pdf="))) {
            ++printToPdfCount;
        }
    }
    QCOMPARE(printToPdfCount, 1);

    // 网址必须是最后一个参数：浏览器把最后一个非开关参数当要打开的页面。
    QCOMPARE(arguments.last(), QStringLiteral("file:///C:/tmp/x.html"));
}

void BrowserPdfPrinterTests::thePrintArgumentsHaveExactlyOneUserDataDir() {
    BrowserPrintOptions options;
    const QStringList arguments = aipdf::doc::buildBrowserPrintArguments(
        options, QStringLiteral("file:///C:/tmp/x.html"), QStringLiteral("C:/tmp/out.pdf"),
        QStringLiteral("C:/tmp/profile"));

    int userDataDirCount = 0;
    for (const QString& argument : arguments) {
        if (argument.startsWith(QStringLiteral("--user-data-dir="))) {
            ++userDataDirCount;
        }
    }
    // 重复给 --user-data-dir 会让浏览器用最后一个，profile 隔离就失效了。
    QCOMPARE(userDataDirCount, 1);
}

void BrowserPdfPrinterTests::thePrintArgumentsFollowTheHeaderFooterChoice() {
    BrowserPrintOptions options;
    options.includeHeaderFooter = true;
    QStringList arguments = aipdf::doc::buildBrowserPrintArguments(
        options, QStringLiteral("file:///C:/tmp/x.html"), QStringLiteral("C:/tmp/out.pdf"),
        QStringLiteral("C:/tmp/profile"));
    QVERIFY(!arguments.contains(QStringLiteral("--print-to-pdf-no-header")));

    options.includeHeaderFooter = false;
    arguments = aipdf::doc::buildBrowserPrintArguments(
        options, QStringLiteral("file:///C:/tmp/x.html"), QStringLiteral("C:/tmp/out.pdf"),
        QStringLiteral("C:/tmp/profile"));
    QVERIFY(arguments.contains(QStringLiteral("--print-to-pdf-no-header")));
}

// ============================================================ 失败分类 ======

void BrowserPdfPrinterTests::theEmptyEngineListReportsNoEngine() {
    BrowserPrintOptions options;
    options.html = plainWebPage();

    const BrowserPrintResult result = aipdf::doc::printWebPageToPdfWithEngines(options, {});
    QVERIFY(!result.ok);
    // 「本机没有浏览器」与「浏览器起不来」是两回事：前者让用户去装浏览器，
    // 后者让用户重试。混成一句就没法处理。
    QCOMPARE(result.errorKind, BrowserPrintError::NoEngine);
    QVERIFY(result.error.contains(QStringLiteral("本机没有可用的浏览器引擎")));
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.pageCount, 0);
}

void BrowserPdfPrinterTests::theMissingExecutableReportsLaunchFailure() {
    BrowserPrintOptions options;
    options.html = plainWebPage();

    BrowserEngine engine;
    engine.name = QStringLiteral("Ghost");
    engine.executable = QDir::tempPath() + QStringLiteral("/aipdf-no-such-browser-9f3a.exe");

    const BrowserPrintResult result =
        aipdf::doc::printWebPageToPdfWithEngines(options, QVector<BrowserEngine>{engine});
    QVERIFY(!result.ok);
    QCOMPARE(result.errorKind, BrowserPrintError::LaunchFailed);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.bytes.isEmpty());
}

void BrowserPdfPrinterTests::theTimeoutIsReportedAsTimeout() {
    BrowserPrintOptions options;
    options.html = plainWebPage();
    options.timeoutMs = 2000;

    BrowserEngine engine;
    engine.name = QStringLiteral("Hang");
    engine.executable = QCoreApplication::applicationFilePath();

    qputenv(kChildModeEnv, kModeHang);
    const BrowserPrintResult result =
        aipdf::doc::printWebPageToPdfWithEngines(options, QVector<BrowserEngine>{engine});
    qunsetenv(kChildModeEnv);

    QVERIFY(!result.ok);
    QCOMPARE(result.errorKind, BrowserPrintError::Timeout);
    // 原因里要说清等了多久，别只丢一句「失败」。
    QVERIFY(result.error.contains(QStringLiteral("秒还没出结果")));
}

void BrowserPdfPrinterTests::theMissingOutputIsReportedAsInvalidOutput() {
    BrowserPrintOptions options;
    options.html = plainWebPage();

    BrowserEngine engine;
    engine.name = QStringLiteral("Silent");
    engine.executable = QCoreApplication::applicationFilePath();

    qputenv(kChildModeEnv, kModeExit0);
    const BrowserPrintResult result =
        aipdf::doc::printWebPageToPdfWithEngines(options, QVector<BrowserEngine>{engine});
    qunsetenv(kChildModeEnv);

    QVERIFY(!result.ok);
    // 退出码 0 但没产物 —— 属于「产物不合格」，不是「启动失败」。
    QCOMPARE(result.errorKind, BrowserPrintError::InvalidOutput);
    QVERIFY(result.bytes.isEmpty());
}

void BrowserPdfPrinterTests::theGarbageOutputIsReportedAsInvalidOutput() {
    BrowserPrintOptions options;
    options.html = plainWebPage();

    BrowserEngine engine;
    engine.name = QStringLiteral("Garbage");
    engine.executable = QCoreApplication::applicationFilePath();

    qputenv(kChildModeEnv, kModeGarbage);
    const BrowserPrintResult result =
        aipdf::doc::printWebPageToPdfWithEngines(options, QVector<BrowserEngine>{engine});
    qunsetenv(kChildModeEnv);

    QVERIFY(!result.ok);
    QCOMPARE(result.errorKind, BrowserPrintError::InvalidOutput);
    // 坏文件绝不能交出去。
    QVERIFY(result.bytes.isEmpty());
}

// ============================================================ 备选与校验 ====

void BrowserPdfPrinterTests::theFallbackEngineIsUsedWhenTheFirstEngineFails() {
    BrowserPrintOptions options;
    options.html = plainWebPage();

    BrowserEngine broken;
    broken.name = QStringLiteral("Broken");
    broken.executable = QDir::tempPath() + QStringLiteral("/aipdf-no-such-browser-9f3a.exe");

    BrowserEngine working;
    working.name = QStringLiteral("Working");
    working.executable = QCoreApplication::applicationFilePath();

    qputenv(kChildModeEnv, kModeGood);
    const BrowserPrintResult result = aipdf::doc::printWebPageToPdfWithEngines(
        options, QVector<BrowserEngine>{broken, working});
    qunsetenv(kChildModeEnv);

    QVERIFY2(result.ok, qPrintable(result.error));
    // 首选引擎挂了才换备选，换了就得如实标出来。
    QVERIFY(result.usedFallbackEngine);
    QCOMPARE(result.engineName, QStringLiteral("Working"));
    QCOMPARE(result.pageCount, 2);
    QVERIFY(!result.bytes.isEmpty());
}

void BrowserPdfPrinterTests::theEmptyOutputIsRejected() {
    const BrowserPdfValidation validation = aipdf::doc::validateBrowserPdfOutput(QByteArray());
    QVERIFY(!validation.ok);
    QVERIFY(!validation.error.isEmpty());
}

void BrowserPdfPrinterTests::theGarbageOutputIsRejected() {
    const BrowserPdfValidation validation =
        aipdf::doc::validateBrowserPdfOutput(QByteArray("not a pdf at all"));
    QVERIFY(!validation.ok);
    QVERIFY(!validation.error.isEmpty());
}

void BrowserPdfPrinterTests::thePageCountIsReadFromTheProducedPdf() {
    const BrowserPdfValidation validation =
        aipdf::doc::validateBrowserPdfOutput(minimalPdfBytes(2));
    QVERIFY2(validation.ok, qPrintable(validation.error));
    // 页数是 PDFium 数出来的，不是猜的。
    QCOMPARE(validation.pageCount, 2);
}

void BrowserPdfPrinterTests::theZeroPageOutputIsRejected() {
    // 页数下限：0 页不算数。
    QVERIFY(!aipdf::doc::hasEnoughBrowserPdfPages(0));
    QVERIFY(aipdf::doc::hasEnoughBrowserPdfPages(1));
    QVERIFY(!aipdf::doc::hasEnoughBrowserPdfPages(-1));

    // 端到端也要判不合格。
    const BrowserPdfValidation validation =
        aipdf::doc::validateBrowserPdfOutput(minimalPdfBytes(0));
    QVERIFY(!validation.ok);
    QVERIFY(!validation.error.isEmpty());
}

// ============================================================ 临时文件 ======

void BrowserPdfPrinterTests::theTemporaryHtmlFileIsWrittenAndRemoved() {
    BrowserPrintOptions options;
    options.html = plainWebPage();
    options.htmlBaseName = QStringLiteral("probe");

    BrowserPrintWorkspace workspace;
    QString error;
    QVERIFY2(aipdf::doc::prepareBrowserPrintWorkspace(options, &workspace, &error),
             qPrintable(error));
    QVERIFY(workspace.isValid());
    QVERIFY(QFileInfo::exists(workspace.htmlFilePath));
    QCOMPARE(QFileInfo(workspace.htmlFilePath).fileName(), QStringLiteral("probe.html"));

    QFile file(workspace.htmlFilePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), options.html);
    file.close();

    const QString directory = workspace.directory;
    aipdf::doc::cleanupBrowserPrintWorkspace(&workspace);
    // 用完即删：临时 HTML 与整个临时目录都不该留下。
    QVERIFY(!QFileInfo::exists(directory));
    QVERIFY(!QFileInfo::exists(workspace.htmlFilePath));
}

void BrowserPdfPrinterTests::theTemporaryDirectoryLivesOutsideTheRepository() {
    BrowserPrintOptions options;
    options.html = plainWebPage();

    BrowserPrintWorkspace workspace;
    QString error;
    QVERIFY2(aipdf::doc::prepareBrowserPrintWorkspace(options, &workspace, &error),
             qPrintable(error));

    const QString directory = QDir::cleanPath(workspace.directory);
    // 临时目录必须在系统临时目录下，不能落在仓库里（会污染源码树）。
    QVERIFY2(directory.startsWith(QDir::cleanPath(QDir::tempPath())),
             qPrintable(directory));
    QVERIFY2(!directory.startsWith(QDir::cleanPath(QDir::currentPath()) + QLatin1Char('/')),
             qPrintable(directory));

    aipdf::doc::cleanupBrowserPrintWorkspace(&workspace);
}

// ============================================================ 真实渲染 ======

void BrowserPdfPrinterTests::theHtmlIsPrintedWithJavaScriptExecuted() {
    const QVector<BrowserEngine> engines = aipdf::doc::detectBrowserEngines();
    if (engines.isEmpty()) {
        QSKIP("本机没有可用的浏览器引擎（Edge/Chrome），跳过真实渲染验证。");
    }

    BrowserPrintOptions options;
    options.timeoutMs = 90000;
    options.virtualTimeBudgetMs = 3000;

    options.html = plainWebPage();
    const BrowserPrintResult plain =
        aipdf::doc::printWebPageToPdfWithEngines(options, engines);
    QVERIFY2(plain.ok, qPrintable(plain.error));
    QVERIFY(plain.pageCount > 0);
    QVERIFY(!plain.bytes.isEmpty());
    // 临时目录用完即删。
    QVERIFY(!QFileInfo::exists(plain.temporaryDirectory));

    options.html = scriptedWebPage();
    const BrowserPrintResult scripted =
        aipdf::doc::printWebPageToPdfWithEngines(options, engines);
    QVERIFY2(scripted.ok, qPrintable(scripted.error));
    QVERIFY(scripted.pageCount > 0);

    // 把对照数据打出来：这组数字就是「JS 真的执行了」的可核对证据。
    QString engineLabel = scripted.engineName;
    for (const BrowserEngine& engine : engines) {
        if (engine.name == scripted.engineName) {
            engineLabel = aipdf::doc::browserEngineLabel(engine);
            break;
        }
    }
    qInfo().noquote() << QStringLiteral("engine=%1 plain=%2页/%3字节 scripted=%4页/%5字节")
                             .arg(engineLabel)
                             .arg(plain.pageCount)
                             .arg(plain.bytes.size())
                             .arg(scripted.pageCount)
                             .arg(scripted.bytes.size());

    // 关键断言：脚本插进去的 80 段正文把页面撑开了 —— 页数变多才说明
    // JavaScript 真的执行了、CSS 真的渲染了。
    QVERIFY2(scripted.pageCount > plain.pageCount,
             qPrintable(QStringLiteral("JS 注入后页数应当变多：普通 %1 页 / 注入后 %2 页")
                            .arg(plain.pageCount)
                            .arg(scripted.pageCount)));
    QVERIFY(scripted.bytes.size() > plain.bytes.size());
}

int main(int argc, char* argv[]) {
    // 子进程模式：这个测试程序自己被当成"浏览器"拉起来时，按环境变量扮演
    // 不同的坏引擎。必须在 QTest 接管参数之前拦下来——浏览器的参数
    // （--headless=new 之类）会被 QTest 当成未知选项而报错。
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLatin1(argv[index]) != QLatin1String("--headless=new")) {
            continue;
        }
        QCoreApplication application(argc, argv);
        const QByteArray mode = qgetenv(kChildModeEnv);
        const QStringList arguments = QCoreApplication::arguments();
        const QString outputPath = printToPdfPathFromArguments(arguments);
        if (mode == kModeHang) {
            // 挂住不退出：父进程必须靠超时把它杀掉。
            QThread::sleep(300);
            return 0;
        }
        if (mode == kModeGarbage) {
            QFile file(outputPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write("this is not a pdf");
            }
            return 0;
        }
        if (mode == kModeGood) {
            QFile file(outputPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(minimalPdfBytes(2));
            }
            return 0;
        }
        // kModeExit0：正常退出，但不写任何产物。
        return 0;
    }

    QCoreApplication application(argc, argv);
    BrowserPdfPrinterTests test;
    return QTest::qExec(&test, argc, argv);
}

#include "BrowserPdfPrinterTests.moc"
