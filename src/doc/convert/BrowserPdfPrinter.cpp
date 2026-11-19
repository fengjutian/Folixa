#include "doc/convert/BrowserPdfPrinter.h"

#include "i18n/Translate.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QVersionNumber>

#include <algorithm>
#include <memory>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winver.h>
#endif

namespace aipdf::doc {

namespace {

// 浏览器家族：显示名 + 可执行文件名 + 常见安装位置。
//
// 顺序就是优先级（Edge → Chrome → Chromium）：
// Edge 是 Windows 自带的，装机命中率最高；Chromium 基本只出现在开发机上。
struct EngineFamily {
    QString name;
    QString executableName;
    QStringList commonPaths;
};

QVector<EngineFamily> engineFamilies() {
    QVector<EngineFamily> families;
    families.append(EngineFamily{
        QStringLiteral("Edge"),
        QStringLiteral("msedge.exe"),
        QStringList{
            QStringLiteral("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"),
            QStringLiteral("C:/Program Files/Microsoft/Edge/Application/msedge.exe"),
        }});
    families.append(EngineFamily{
        QStringLiteral("Chrome"),
        QStringLiteral("chrome.exe"),
        QStringList{
            QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe"),
            QStringLiteral("C:/Program Files (x86)/Google/Chrome/Application/chrome.exe"),
        }});
    families.append(EngineFamily{
        QStringLiteral("Chromium"),
        QStringLiteral("chromium.exe"),
        QStringList{
            QStringLiteral("C:/Program Files/Chromium/Application/chromium.exe"),
        }});
    return families;
}

// 注册表 App Paths 的三个可能位置。
//
// 为什么要三个：装浏览器的标准登记处是 App Paths，而这个键受 WOW64 重定向影响，
// 32 位的浏览器（Edge 就是 32 位的）登记的位置与 64 位程序看到的不是同一个。
// 三个都查一遍才不会漏。
QStringList appPathsRegistryKeys(const QString& executableName) {
    const QString suffix =
        QStringLiteral("Microsoft\\Windows\\CurrentVersion\\App Paths\\") + executableName;
    return QStringList{
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\") + suffix,
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\") + suffix,
        QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\") + suffix,
    };
}

// 每用户安装位置（%LOCALAPPDATA%）。
QStringList localAppDataPaths(const QString& executableName) {
    const QString base = QDir::fromNativeSeparators(QDir::homePath()) +
                         QStringLiteral("/AppData/Local/");
    return QStringList{
        base + QStringLiteral("Microsoft/Edge/Application/") + executableName,
        base + QStringLiteral("Google/Chrome/Application/") + executableName,
    };
}

// 读一个可执行文件的版本号（"157.0.4341.46"）。
//
// 为什么绕开链接依赖：Qt 6.8 的 QFileInfo 还没有 version()，而读版本资源
// 要用 version.dll 的三个函数。为一个「显示用」的字段给整个主程序加一条
// 链接库（还得让每个下游目标都记得加）不划算，所以这里**运行时**把
// version.dll 打开、用完就关：读得到就用，读不到就留空，不影响功能。
QString readFileVersion(const QString& path) {
#if defined(Q_OS_WIN)
    using GetSizeFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using GetInfoFn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using QueryFn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

    HMODULE library = LoadLibraryW(L"version.dll");
    if (library == nullptr) {
        return {};
    }
    const auto getSize = reinterpret_cast<GetSizeFn>(GetProcAddress(library, "GetFileVersionInfoSizeW"));
    const auto getInfo = reinterpret_cast<GetInfoFn>(GetProcAddress(library, "GetFileVersionInfoW"));
    const auto query = reinterpret_cast<QueryFn>(GetProcAddress(library, "VerQueryValueW"));
    if (!getSize || !getInfo || !query) {
        FreeLibrary(library);
        return {};
    }

    const wchar_t* widePath = reinterpret_cast<const wchar_t*>(path.utf16());
    DWORD handle = 0;
    const DWORD size = getSize(widePath, &handle);
    if (size == 0) {
        FreeLibrary(library);
        return {};
    }
    QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
    if (!getInfo(widePath, 0, size, buffer.data())) {
        FreeLibrary(library);
        return {};
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    QString version;
    if (query(buffer.constData(), L"\\", reinterpret_cast<LPVOID*>(&info), &length) && info != nullptr) {
        version = QStringLiteral("%1.%2.%3.%4")
                      .arg(HIWORD(info->dwFileVersionMS))
                      .arg(LOWORD(info->dwFileVersionMS))
                      .arg(HIWORD(info->dwFileVersionLS))
                      .arg(LOWORD(info->dwFileVersionLS));
    }
    FreeLibrary(library);
    return version;
#else
    Q_UNUSED(path);
    return {};
#endif
}

// 摸一下这个路径：文件在不在、版本号多少。
BrowserCandidate makeCandidate(const QString& path) {
    BrowserCandidate candidate;
    candidate.path = path;
    if (path.isEmpty()) {
        return candidate;
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return candidate;
    }
    candidate.exists = true;
    candidate.version = readFileVersion(path);
    return candidate;
}

// 家族优先级序号（越小越优先）。
int familyRankFor(const QString& name) {
    const QVector<EngineFamily> families = engineFamilies();
    for (int index = 0; index < families.size(); ++index) {
        if (families.at(index).name == name) {
            return index;
        }
    }
    return families.size();
}

// 版本号比较用的数值。读不出版本号的排最后。
QVersionNumber versionOf(const QString& version) {
    if (version.isEmpty()) {
        return QVersionNumber();
    }
    return QVersionNumber::fromString(version);
}

// 跑一次浏览器进程的结果。
struct ProcessOutcome {
    // 进程有没有成功启动（可执行文件找不到 / 被系统拒绝时为假）。
    bool started = false;
    // 是否超时（超时时进程已被杀掉）。
    bool timedOut = false;
    // 是否正常退出且退出码为 0。
    bool succeeded = false;
    // 退出码。
    int exitCode = -1;
};

// 启动浏览器并等它结束。
//
// stdout / stderr 落文件而不是走管道：浏览器在 Windows 上会刷一堆
// importer 报错（"QQBrowser user data path not found." 之类），
// 管道缓冲区（64 KB）一满，子进程就卡在写管道上不动，
// 最后被我们误判成「超时」。落文件既不会卡，也留着给我们事后看。
//
// 注意：**stderr 里的噪音不算失败**。判定成败只看退出码与产物本身。
ProcessOutcome runBrowserProcess(const QString& executable,
                                const QStringList& arguments,
                                int timeoutMs,
                                const QString& logDirectory) {
    ProcessOutcome outcome;
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setStandardOutputFile(QDir(logDirectory).filePath(QStringLiteral("browser-stdout.log")));
    process.setStandardErrorFile(QDir(logDirectory).filePath(QStringLiteral("browser-stderr.log")));
    process.start();
    // 起不来（文件不存在、被系统拦了）时 waitForStarted 立刻返回假。
    if (!process.waitForStarted(15000)) {
        return outcome;
    }
    outcome.started = true;
    const int effectiveTimeout = timeoutMs > 0 ? timeoutMs : 1;
    if (!process.waitForFinished(effectiveTimeout)) {
        outcome.timedOut = true;
        // 到点就杀，不留孤儿进程在后台啃 CPU。
        process.kill();
        process.waitForFinished(5000);
        return outcome;
    }
    outcome.exitCode = process.exitCode();
    outcome.succeeded = process.exitStatus() == QProcess::NormalExit && outcome.exitCode == 0;
    return outcome;
}

// 递归删目录，顺手把只读属性去掉。
//
// 为什么不能直接用 QDir::removeRecursively：浏览器在自己的 profile 里写的
// 文件带只读属性，删不掉，临时目录就会留在磁盘上（实测过一次：
// "Unable to remove ... most likely due to the presence of read-only files"）。
// 打印一次留一个目录，几百次之后磁盘就脏了。
//
// 删不掉就再试一次：浏览器进程刚退出时可能还有句柄没放，
// 立刻重试通常就删掉了；实在删不掉也不该拖垮打印流程。
void removeDirectoryTree(const QString& path) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        const QFileInfo info(path);
        if (!info.exists()) {
            return;
        }
        if (info.isDir()) {
            const QFileInfoList entries =
                QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
                                         QDir::NoDotAndDotDot);
            for (const QFileInfo& entry : entries) {
                if (entry.isDir()) {
                    removeDirectoryTree(entry.absoluteFilePath());
                } else {
                    QFile::setPermissions(entry.absoluteFilePath(),
                                          QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser |
                                              QFile::WriteUser);
                }
            }
        }
        if (QDir(path).removeRecursively()) {
            return;
        }
    }
}

// 读文件；读不出来返回空字节。
QByteArray readWholeFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

}  // namespace

// ============================================================ 引擎识别 ======

QString browserEngineNameForExecutable(const QString& path) {
    if (path.isEmpty()) {
        return {};
    }
    const QString normalized = QDir::fromNativeSeparators(path);
    const QString fileName = QFileInfo(normalized).fileName().toLower();
    if (fileName == QLatin1String("msedge.exe")) {
        return QStringLiteral("Edge");
    }
    if (fileName == QLatin1String("chromium.exe")) {
        return QStringLiteral("Chromium");
    }
    if (fileName == QLatin1String("chrome.exe")) {
        // Chromium 的正式版可执行文件也叫 chrome.exe，只能靠目录名区分。
        if (normalized.contains(QLatin1String("/chromium/"), Qt::CaseInsensitive)) {
            return QStringLiteral("Chromium");
        }
        return QStringLiteral("Chrome");
    }
    return {};
}

QVector<BrowserEngine> buildBrowserEngines(const QVector<BrowserCandidate>& candidates) {
    // 1) 丢掉不存在的路径，并按路径去重（Windows 上路径不区分大小写）。
    QVector<BrowserCandidate> usable;
    QSet<QString> seenPaths;
    for (const BrowserCandidate& candidate : candidates) {
        if (!candidate.exists || candidate.path.isEmpty()) {
            continue;
        }
        const QString key = QDir::fromNativeSeparators(candidate.path).toLower();
        if (seenPaths.contains(key)) {
            continue;
        }
        seenPaths.insert(key);
        usable.append(candidate);
    }

    // 2) 认家族、定优先级。
    struct Ranked {
        BrowserEngine engine;
        int familyRank = 0;
        QVersionNumber version;
    };
    QVector<Ranked> ranked;
    for (const BrowserCandidate& candidate : usable) {
        const QString name = browserEngineNameForExecutable(candidate.path);
        if (name.isEmpty()) {
            // 认不出来的引擎一律不要：宁可少列，也不列一个名不副实的。
            continue;
        }
        Ranked item;
        item.engine.name = name;
        item.engine.executable = candidate.path;
        item.engine.version = candidate.version;
        item.familyRank = familyRankFor(name);
        item.version = versionOf(candidate.version);
        ranked.append(item);
    }

    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& left, const Ranked& right) {
        if (left.familyRank != right.familyRank) {
            return left.familyRank < right.familyRank;
        }
        if (left.version != right.version) {
            return left.version > right.version;
        }
        // 版本一样时按路径排，保证同一台机器上每次探测结果都相同。
        return left.engine.executable.compare(right.engine.executable, Qt::CaseInsensitive) < 0;
    });

    // 3) 同一家族只留一条（排序后的第一条 = 版本最新的那条）。
    QVector<BrowserEngine> engines;
    QSet<QString> usedFamilies;
    for (const Ranked& item : ranked) {
        if (usedFamilies.contains(item.engine.name)) {
            continue;
        }
        usedFamilies.insert(item.engine.name);
        engines.append(item.engine);
    }
    return engines;
}

QVector<BrowserEngine> detectBrowserEngines(const QStringList& extraPaths) {
    QVector<BrowserCandidate> candidates;
    const QVector<EngineFamily> families = engineFamilies();

    // 注册表 App Paths：装浏览器的标准登记处，比猜路径准。
    for (const EngineFamily& family : families) {
        for (const QString& key : appPathsRegistryKeys(family.executableName)) {
            QSettings settings(key, QSettings::NativeFormat);
            const QString target = settings.value(QStringLiteral("Default")).toString();
            if (!target.isEmpty()) {
                candidates.append(makeCandidate(target));
            }
        }
    }

    // 常见安装路径（含每用户安装）。
    for (const EngineFamily& family : families) {
        QStringList paths = family.commonPaths;
        paths += localAppDataPaths(family.executableName);
        for (const QString& path : paths) {
            candidates.append(makeCandidate(path));
        }
    }

    // 调用方自己知道的路径。
    for (const QString& path : extraPaths) {
        candidates.append(makeCandidate(path));
    }

    return buildBrowserEngines(candidates);
}

QString browserEngineLabel(const BrowserEngine& engine) {
    if (engine.version.isEmpty()) {
        return engine.name;
    }
    return QStringLiteral("%1 %2").arg(engine.name, engine.version);
}

// ============================================================ 参数拼装 ======

QStringList buildBrowserPrintArguments(const BrowserPrintOptions& options,
                                       const QString& targetUrl,
                                       const QString& outputPdfPath,
                                       const QString& userDataDir) {
    QStringList arguments;
    arguments << QStringLiteral("--headless=new");
    arguments << QStringLiteral("--disable-gpu");
    arguments << QStringLiteral("--disable-extensions");
    arguments << QStringLiteral("--hide-scrollbars");
    arguments << QStringLiteral("--run-all-compositor-stages-before-draw");
    arguments << QStringLiteral("--virtual-time-budget=") +
                     QString::number(qMax(0, options.virtualTimeBudgetMs));
    if (!userDataDir.isEmpty()) {
        arguments << QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(userDataDir);
    }
    arguments << QStringLiteral("--print-to-pdf=") + QDir::toNativeSeparators(outputPdfPath);
    if (!options.includeHeaderFooter) {
        arguments << QStringLiteral("--print-to-pdf-no-header");
    }
    arguments << targetUrl;
    return arguments;
}

// ============================================================ 工作区 ========

QString browserTemporaryHtmlFileName(const QString& baseName) {
    QString base = baseName;
    // 只取基名：临时文件名的基名里带路径分隔符会把文件写到别处去。
    base.replace(QLatin1Char('/'), QLatin1Char('_'));
    base.replace(QLatin1Char('\\'), QLatin1Char('_'));
    base.replace(QLatin1Char(':'), QLatin1Char('_'));
    if (base.trimmed().isEmpty()) {
        base = QStringLiteral("aipdf-web");
    }
    if (!base.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)) {
        base += QStringLiteral(".html");
    }
    return base;
}

bool prepareBrowserPrintWorkspace(const BrowserPrintOptions& options,
                                  BrowserPrintWorkspace* workspace,
                                  QString* error) {
    if (!workspace) {
        if (error) {
            *error = i18n::tr("aipdf::doc::BrowserPdfPrinter", "没有可用的打印工作区。");
        }
        return false;
    }
    workspace->temporaryDir = std::make_unique<QTemporaryDir>();
    if (!workspace->temporaryDir->isValid()) {
        const QString reason = workspace->temporaryDir->errorString();
        cleanupBrowserPrintWorkspace(workspace);
        if (error) {
            *error = i18n::tr("aipdf::doc::BrowserPdfPrinter", "建临时目录失败：%1").arg(reason);
        }
        return false;
    }
    workspace->directory = workspace->temporaryDir->path();
    workspace->outputPdfPath = QDir(workspace->directory).filePath(QStringLiteral("output.pdf"));
    workspace->userDataDir = QDir(workspace->directory).filePath(QStringLiteral("profile"));
    if (!QDir().mkpath(workspace->userDataDir)) {
        cleanupBrowserPrintWorkspace(workspace);
        if (error) {
            *error = i18n::tr("aipdf::doc::BrowserPdfPrinter", "建浏览器临时 profile 目录失败。");
        }
        return false;
    }

    if (!options.html.isEmpty()) {
        const QString fileName = browserTemporaryHtmlFileName(options.htmlBaseName);
        workspace->htmlFilePath = QDir(workspace->directory).filePath(fileName);
        QFile file(workspace->htmlFilePath);
        if (!file.open(QIODevice::WriteOnly)) {
            cleanupBrowserPrintWorkspace(workspace);
            if (error) {
                *error = i18n::tr("aipdf::doc::BrowserPdfPrinter", "写临时 HTML 文件失败：%1")
                             .arg(fileName);
            }
            return false;
        }
        const qint64 written = file.write(options.html);
        file.close();
        if (written != options.html.size()) {
            cleanupBrowserPrintWorkspace(workspace);
            if (error) {
                *error = i18n::tr("aipdf::doc::BrowserPdfPrinter", "临时 HTML 文件没写完整。");
            }
            return false;
        }
    }
    return true;
}

void cleanupBrowserPrintWorkspace(BrowserPrintWorkspace* workspace) {
    if (!workspace) {
        return;
    }
    // 先把目录树连同只读文件一起删掉，再交给 QTemporaryDir 兜底：
    // 浏览器 profile 里的只读文件会让 QTemporaryDir 的析构删不掉目录。
    if (!workspace->directory.isEmpty()) {
        removeDirectoryTree(workspace->directory);
    }
    // QTemporaryDir 析构时会递归删掉整个目录。
    workspace->temporaryDir.reset();
    workspace->directory.clear();
    workspace->htmlFilePath.clear();
    workspace->outputPdfPath.clear();
    workspace->userDataDir.clear();
}

// ============================================================ 产物校验 ======

bool hasEnoughBrowserPdfPages(int pageCount) {
    return pageCount > 0;
}

BrowserPdfValidation validateBrowserPdfOutput(const QByteArray& bytes) {
    BrowserPdfValidation validation;
    if (bytes.isEmpty()) {
        validation.error =
            i18n::tr("aipdf::doc::BrowserPdfPrinter", "浏览器没有生成 PDF（产物是空的）。");
        return validation;
    }
    const pdf::PdfOpenResult opened = pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) {
        validation.error = i18n::tr("aipdf::doc::BrowserPdfPrinter", "浏览器生成的 PDF 打不开。");
        return validation;
    }
    validation.pageCount = opened.document.pageCount();
    if (!hasEnoughBrowserPdfPages(validation.pageCount)) {
        validation.error =
            i18n::tr("aipdf::doc::BrowserPdfPrinter", "浏览器生成的 PDF 里没有内容页。");
        return validation;
    }
    validation.ok = true;
    return validation;
}

// ============================================================ 打印 =========

BrowserPrintResult printWebPageToPdfWithEngines(const BrowserPrintOptions& options,
                                                const QVector<BrowserEngine>& engines) {
    BrowserPrintResult result;

    // 空列表就是「没有引擎」。这里**不**自动探测：
    // 「空列表必须报没有引擎、不能崩」这条不变量要能被单测覆盖，
    // 所以留一个不兜底的入口给测试与调用方。
    // 引擎列表为空就是「没有引擎」。这里**不**自动探测：
    // 「空列表必须报没有引擎、不能崩」这条不变量要能被单测覆盖，
    // 所以留一个不兜底的入口给测试与调用方。
    if (engines.isEmpty()) {
        result.errorKind = BrowserPrintError::NoEngine;
        result.error = i18n::tr(
            "aipdf::doc::BrowserPdfPrinter",
            "本机没有可用的浏览器引擎，请安装 Microsoft Edge 或 Google Chrome 后重试。");
        return result;
    }

    const bool hasHtml = !options.html.isEmpty();
    if (!hasHtml && (!options.url.isValid() || options.url.scheme().isEmpty())) {
        result.errorKind = BrowserPrintError::InvalidInput;
        result.error = i18n::tr("aipdf::doc::BrowserPdfPrinter",
                                "没有可打印的网页：既没有网址也没有 HTML 内容。");
        return result;
    }

    BrowserPrintWorkspace workspace;
    QString prepareError;
    if (!prepareBrowserPrintWorkspace(options, &workspace, &prepareError)) {
        result.errorKind = BrowserPrintError::InvalidInput;
        result.error = prepareError;
        return result;
    }
    result.temporaryDirectory = workspace.directory;

    const QString targetUrl = hasHtml ? QUrl::fromLocalFile(workspace.htmlFilePath).toString()
                                      : options.url.toString();

    BrowserPrintError lastKind = BrowserPrintError::None;
    QString lastError;

    for (int index = 0; index < engines.size(); ++index) {
        const BrowserEngine& engine = engines.at(index);
        // 每次重试都从干净状态开始：删掉上一轮的产物，
        // 否则上一轮的残留会被当成本轮的成果。
        QFile::remove(workspace.outputPdfPath);

        const QStringList arguments = buildBrowserPrintArguments(options, targetUrl,
                                                                 workspace.outputPdfPath,
                                                                 workspace.userDataDir);
        const ProcessOutcome outcome =
            runBrowserProcess(engine.executable, arguments, options.timeoutMs, workspace.directory);

        if (!outcome.started) {
            lastKind = BrowserPrintError::LaunchFailed;
            lastError = i18n::tr("aipdf::doc::BrowserPdfPrinter",
                                 "启动浏览器引擎 %1 失败：找不到可执行文件或被系统拒绝。")
                            .arg(engine.name);
            continue;
        }
        if (outcome.timedOut) {
            lastKind = BrowserPrintError::Timeout;
            const int seconds = qMax(1, (options.timeoutMs + 500) / 1000);
            lastError = i18n::tr("aipdf::doc::BrowserPdfPrinter", "等了 %1 秒还没出结果，已终止浏览器进程。")
                            .arg(seconds);
            continue;
        }
        if (!outcome.succeeded) {
            lastKind = BrowserPrintError::LaunchFailed;
            lastError = i18n::tr("aipdf::doc::BrowserPdfPrinter",
                                 "浏览器引擎 %1 退出码 %2，没有生成 PDF。")
                            .arg(engine.name)
                            .arg(outcome.exitCode);
            continue;
        }

        // 退出码为 0 也不算数：产物必须真的能打开、且有内容页。
        const QByteArray bytes = readWholeFile(workspace.outputPdfPath);
        const BrowserPdfValidation validation = validateBrowserPdfOutput(bytes);
        if (!validation.ok) {
            lastKind = BrowserPrintError::InvalidOutput;
            lastError = validation.error;
            continue;
        }

        result.ok = true;
        result.bytes = bytes;
        result.pageCount = validation.pageCount;
        result.engineName = engine.name;
        result.usedFallbackEngine = index > 0;
        break;
    }

    if (!result.ok) {
        result.bytes.clear();
        result.pageCount = 0;
        result.errorKind = lastKind == BrowserPrintError::None ? BrowserPrintError::InvalidOutput
                                                               : lastKind;
        result.error = lastError;
    }

    // 临时目录用完即删：不留 HTML 残骸，也不往仓库目录里写东西。
    cleanupBrowserPrintWorkspace(&workspace);
    return result;
}

BrowserPrintResult printWebPageToPdf(const BrowserPrintOptions& options,
                                     const QVector<BrowserEngine>& engines) {
    if (!engines.isEmpty()) {
        return printWebPageToPdfWithEngines(options, engines);
    }
    return printWebPageToPdfWithEngines(options, detectBrowserEngines());
}

}  // namespace aipdf::doc
