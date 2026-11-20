<#
.SYNOPSIS
    AI PDF Editor 的完整验证流水线（本地与 CI 共用同一入口）。

.DESCRIPTION
    依次执行：
      1. 安装/校验固定工具链
      2. Debug 配置、构建、单元与集成测试、启动验收
      3. Release 配置、构建、单元与集成测试
      4. 打包（Qt 运行库 + 许可证材料 + SBOM）
      5. 无 Qt SDK 环境的打包验收（含 SBOM 一致性校验）
      6. 版本一致性校验

    任一步失败立即中止并返回非零退出码。
#>
param(
    [switch]$SkipToolchain,
    [switch]$SkipPackage,
    # 跳过界面验收（M0/PDF/滚动/文本/导航/批注/页面/索引/AI/整篇问答）。
    #
    # 界面验收会抢占前台窗口、置顶、并用合成鼠标事件点屏幕坐标：
    # 在开发机上跑会打断正在写代码的人。CI 上有独立的会话，照跑不误；
    # 本地要跑完整流水线时，请先确认此刻没有人正在用这台机器的桌面。
    [switch]$SkipAcceptance,
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..')
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
Push-Location $root
try {
    $shellExe = if ($PSVersionTable.PSEdition -eq 'Core') {
        Join-Path $PSHOME 'pwsh.exe'
    } else {
        Join-Path $PSHOME 'powershell.exe'
    }

    function Invoke-Native {
        param([string]$Name, [string]$Executable, [string[]]$Arguments = @())
        Write-Host "--- $Name" -ForegroundColor DarkGray
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$Name 失败（退出码 $LASTEXITCODE）"
        }
    }

    function Invoke-Script {
        param([string]$Name, [string]$Script, [string[]]$Arguments = @())
        Write-Host "--- $Name" -ForegroundColor DarkGray
        & $shellExe -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$Name 失败（退出码 $LASTEXITCODE）"
        }
    }

    $cmakeLists = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
    $versionMatch = [regex]::Match($cmakeLists, 'project\(AiPdfEditor VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
    if (-not $versionMatch.Success) {
        throw '无法从 CMakeLists.txt 解析项目版本'
    }
    $version = $versionMatch.Groups[1].Value
    Write-Host "AI PDF Editor $version 流水线开始" -ForegroundColor Cyan

    # 脚本编码自检：PowerShell 5.1 读没有 BOM 的 UTF-8 文件时按 GBK 解析，
    # 中文注释会把语法搅乱，而且报错位置往往离真正的问题很远。
    # 这个坑踩过两次（都是编辑器把 BOM 去掉了），所以让流水线直接拦住。
    $missingBom = @()
    foreach ($script in Get-ChildItem -LiteralPath (Join-Path $root 'scripts') -Filter *.ps1 -File) {
        $bytes = [System.IO.File]::ReadAllBytes($script.FullName)
        $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
        if (-not $hasBom) {
            $missingBom += $script.Name
        }
    }
    if ($missingBom.Count -gt 0) {
        throw "以下脚本缺少 UTF-8 BOM（PowerShell 5.1 会按 GBK 解析）：$($missingBom -join ', ')"
    }

    if (-not $SkipToolchain) {
        Invoke-Script '安装固定工具链' (Join-Path $PSScriptRoot 'bootstrap-toolchain.ps1')
    }

    Write-Host '=== PDFium ===' -ForegroundColor Cyan
    Invoke-Script '获取 PDFium' (Join-Path $PSScriptRoot 'bootstrap-pdfium.ps1')

    Write-Host '=== 许可证全文 ===' -ForegroundColor Cyan
    Invoke-Script '校验许可证全文' (Join-Path $PSScriptRoot 'fetch-licenses.ps1') @('-VerifyOnly')

    Write-Host '=== 依赖登记 ===' -ForegroundColor Cyan
    Invoke-Script '依赖登记与固定版本' (Join-Path $PSScriptRoot 'verify-dependencies.ps1') @(
        '-RepositoryRoot', $root
    )

    # qpdf 是必需依赖：保存前的结构校验、对象级读写、字体与页面图形解析都走它。
    # 拿不到就直接失败——不再有「无 qpdf 构建」这条降级路径，
    # 否则单测全绿也可能一次都没跑过真实写盘路径。
    Write-Host '=== qpdf（必需：结构校验与对象级读写）===' -ForegroundColor Cyan
    $qpdfExe = Join-Path $root 'tools\qpdf\bin\qpdf.exe'
    if (-not (Test-Path -LiteralPath $qpdfExe)) {
        & $shellExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'bootstrap-qpdf.ps1')
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $qpdfExe)) {
            throw 'qpdf 获取失败：它是必需依赖，流水线不能继续。'
        }
    }
    Write-Host "    qpdf = $qpdfExe" -ForegroundColor DarkGray

    # QuickJS-ng 同样是必需依赖（表单脚本引擎）。取不到就中止。
    Write-Host '=== QuickJS（必需：表单脚本引擎）===' -ForegroundColor Cyan
    $quickJsHeader = Join-Path $root 'third_party\quickjs\quickjs.h'
    if (-not (Test-Path -LiteralPath $quickJsHeader)) {
        & $shellExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'bootstrap-quickjs.ps1')
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $quickJsHeader)) {
            throw 'QuickJS 获取失败：它是必需依赖，流水线不能继续。'
        }
    }
    Write-Host "    quickjs = $(Split-Path -Parent $quickJsHeader)" -ForegroundColor DarkGray

    Write-Host '=== Debug ===' -ForegroundColor Cyan
    Invoke-Native '配置 Debug' 'cmake' @('--preset', 'windows-debug')
    Invoke-Native '构建 Debug' 'cmake' @('--build', '--preset', 'windows-debug')
    Invoke-Native '测试 Debug' 'ctest' @('--preset', 'windows-debug', '--output-on-failure')

    if ($SkipAcceptance) {
        Write-Host '    （按要求跳过界面验收）' -ForegroundColor DarkGray
    } else {
    Invoke-Script '启动验收 Debug' (Join-Path $PSScriptRoot 'verify-m0.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/m0-debug.png'
    )
    Invoke-Script 'PDF 能力验收' (Join-Path $PSScriptRoot 'verify-pdf.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/pdf-open.png'
    )
    Invoke-Script '连续滚动验收' (Join-Path $PSScriptRoot 'verify-scroll.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/scroll-long.png'
    )
    Invoke-Script '文本选择与搜索验收' (Join-Path $PSScriptRoot 'verify-text.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/text-search.png'
    )
    Invoke-Script '目录、书签与链接验收' (Join-Path $PSScriptRoot 'verify-navigation.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/navigation-outline.png'
    )
    Invoke-Script '批注与撤销重做验收' (Join-Path $PSScriptRoot 'verify-annotations.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/annotations.png'
    )
    Invoke-Script '页面管理与安全保存验收' (Join-Path $PSScriptRoot 'verify-pages.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/pages.png'
    )
    Invoke-Script '文档索引与检索验收' (Join-Path $PSScriptRoot 'verify-index.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/index.png'
    )
    Invoke-Script 'AI 面板与流式对话验收' (Join-Path $PSScriptRoot 'verify-ai.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/ai.png'
    )
    Invoke-Script '整篇问答与引用核验验收' (Join-Path $PSScriptRoot 'verify-qa.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/qa.png'
    )
    Invoke-Script '隐私策略验收' (Join-Path $PSScriptRoot 'verify-privacy.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/privacy.png'
    )
    Invoke-Script '扫描件 OCR 验收' (Join-Path $PSScriptRoot 'verify-ocr.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/ocr.png'
    )
    Invoke-Script 'AI 提案确认验收' (Join-Path $PSScriptRoot 'verify-proposals.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/proposals.png'
    )
    Invoke-Script '放映验收' (Join-Path $PSScriptRoot 'verify-presentation.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/presentation.png'
    )
    Invoke-Script '排布方式验收' (Join-Path $PSScriptRoot 'verify-layout.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/layout.png'
    )
    Invoke-Script '阅读与检索验收' (Join-Path $PSScriptRoot 'verify-reading.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/reading.png'
    )
    Invoke-Script '批注工具栏验收' (Join-Path $PSScriptRoot 'verify-annotation-types.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/annotation-types.png'
    )
    # 这三张是第三梯队里漏接的界面验收（表单 / 编辑 / 界面语言）。
    # 它们会抢前台，所以放在 -SkipAcceptance 的 else 分支里：本地默认跳过，CI 上照跑。
    Invoke-Script '表单界面验收' (Join-Path $PSScriptRoot 'verify-forms.ps1') @(
        '-BuildDir', 'build/windows-debug/bin',
        '-ScreenshotPath', 'build/acceptance/forms.png'
    )
    Invoke-Script '编辑动作验收' (Join-Path $PSScriptRoot 'verify-editing.ps1') @(
        '-BuildDir', 'build/windows-debug/bin'
    )
    Invoke-Script '界面语言验收' (Join-Path $PSScriptRoot 'verify-i18n.ps1') @(
        '-BuildDir', 'build/windows-debug/bin'
    )
    }

    Write-Host '=== Release ===' -ForegroundColor Cyan
    Invoke-Native '配置 Release' 'cmake' @('--preset', 'windows-release')
    Invoke-Native '构建 Release' 'cmake' @('--build', '--preset', 'windows-release')
    Invoke-Native '测试 Release' 'ctest' @('--preset', 'windows-release', '--output-on-failure')

    if (-not $SkipPackage) {
        Write-Host '=== 打包 ===' -ForegroundColor Cyan
        Invoke-Native '打包' 'cmake' @('--build', '--preset', 'windows-release', '--target', 'package')
        if ($SkipAcceptance) {
            Write-Host '    （按要求跳过打包验收的界面部分）' -ForegroundColor DarkGray
        } else {
            $packageScreenshot = 'build/acceptance/package-{0}.png' -f $version
            Invoke-Script '打包验收（无 Qt SDK 环境）' (Join-Path $PSScriptRoot 'verify-package.ps1') @(
                '-PackagePath', "dist/ai-pdf-editor-$version-win64.zip",
                '-Version', $version,
                # 注意：数组字面量里逗号比 + 结合更紧，字符串拼接必须加括号，
                # 否则会被拆成 'build/acceptance/package-'、版本号、'.png' 三个参数。
                '-ScreenshotPath', $packageScreenshot
            )
            # 验收截图必须以预期文件名落盘：参数拼错会静默写出一个别的文件名。
            if (-not (Test-Path -LiteralPath $packageScreenshot)) {
                throw "打包验收截图缺失: $packageScreenshot"
            }
            Write-Host "    打包验收截图: $packageScreenshot" -ForegroundColor DarkGray
        }

        Write-Host '=== 包内依赖归属 ===' -ForegroundColor Cyan
        Invoke-Script '包内二进制都是已登记组件' (Join-Path $PSScriptRoot 'verify-dependencies.ps1') @(
            '-RepositoryRoot', $root,
            '-PackagePath', "dist/ai-pdf-editor-$version-win64.zip"
        )
    }

    if (-not $SkipAcceptance) {
        Write-Host '=== 兼容性与崩溃恢复验收 ===' -ForegroundColor Cyan
        Invoke-Script '兼容性验收' (Join-Path $PSScriptRoot 'verify-compat.ps1') @(
            '-BuildDir', 'build/windows-debug/bin',
            '-ScreenshotPath', 'build/acceptance/compat.png'
        )
        Invoke-Script '崩溃恢复验收' (Join-Path $PSScriptRoot 'verify-recovery.ps1') @(
            '-BuildDir', 'build/windows-debug/bin',
            '-ScreenshotPath', 'build/acceptance/recovery.png'
        )
    } else {
        Write-Host '    （按要求跳过兼容性与崩溃恢复的界面部分）' -ForegroundColor DarkGray
    }

    Write-Host '=== 性能基线与敏感日志 ===' -ForegroundColor Cyan
    Invoke-Script '性能基线' (Join-Path $PSScriptRoot 'perf.ps1') @(
        '-BuildDir', 'build/windows-debug/bin'
    )

    Invoke-Script '敏感日志扫描' (Join-Path $PSScriptRoot 'verify-logs.ps1') @('-SelfTest')

    # H8 与 A12 的验收门：都是无 GUI 的（不启动应用、不合成鼠标、不抢前台），
    # 所以放在这里而不是界面验收那一段。
    Write-Host '=== 优化保真与 CJK 兼容语料 ===' -ForegroundColor Cyan
    Invoke-Native '生成 CJK 兼容语料' 'python' @(
        'scripts/gen-cjk-corpus.py', 'gen', '--out', 'build/cjk-corpus'
    )
    Invoke-Script '优化保真验收' (Join-Path $PSScriptRoot 'verify-optimize-fidelity.ps1') @(
        '-RepositoryRoot', $root
    )
    Invoke-Script 'CJK 兼容语料验收' (Join-Path $PSScriptRoot 'verify-cjk-corpus.ps1') @(
        '-RepositoryRoot', $root
    )

    # H5 的验收门：同样是无 GUI 的（不启动应用、不合成鼠标、不抢前台），
    # 所以放在这里而不是界面验收那一段。
    # 独立解析器（python-docx / python-pptx / openpyxl）没装时门禁会降级：
    # 标准库那几项照跑，缺包会打印「未装 X，跳过…」——降级不等于放行，
    # 负例对照仍然必须红。
    Write-Host '=== Office 四合一语料（双栏 / 表格 / 图片 / CJK）===' -ForegroundColor Cyan
    Invoke-Native '生成 Office 四合一语料' 'python' @(
        'scripts/gen-office-corpus.py', '--out', 'build/office-corpus', '--count', '50', '--quiet'
    )
    Invoke-Native 'Office 图片与表格导出验收' 'python' @(
        'scripts/verify-office-images.py',
        '--corpus', 'build/office-corpus',
        '--dump', 'build/office-export',
        '--exporter', 'build/windows-debug/bin/OfficeImagesTests.exe'
    )

    # 文档与矩阵这一组门：矩阵 CSV 是唯一数据源，矩阵文档由脚本生成，
    # 评分卡与版本审计的数字必须互相对得上，i18n 覆盖率与功能宣称也要当场核。
    Write-Host '=== 文档、矩阵与 i18n 一致性 ===' -ForegroundColor Cyan
    Invoke-Native '矩阵生成校验' 'python' @(
        'scripts/build-foxit-matrix.py', '--check'
    )
    Invoke-Native '文档一致性校验' 'python' @(
        'scripts/check-docs-consistency.py'
    )
    Invoke-Native '功能宣称守卫' 'python' @(
        'scripts/check-feature-claims.py'
    )
    Invoke-Native '功能宣称守卫的负例对照' 'python' @(
        'scripts/test-feature-claims-negative.py'
    )
    # 同一项能力在不同文档里的说法不能打架：矩阵、评分卡、整改方案、版本审计
    # 对同一能力的措辞要一致（登记表：docs/benchmark/doc-claim-contradictions.csv）。
    Invoke-Native '跨文档主张守卫' 'python' @(
        'scripts/check-doc-claims.py'
    )
    Invoke-Native '跨文档主张守卫的负例对照' 'python' @(
        'scripts/test-doc-claims-negative.py'
    )
    # partial 守卫：矩阵里每条 partial 都必须用加粗标出降级边界，且与评分卡口径
    # 对得上（评分卡判「对齐」而矩阵还挂着 partial 的，必须在豁免表里登记理由；
    # 豁免过期也算错——降级补上了、矩阵升成 aligned，豁免不能留着当通行证）。
    Invoke-Native 'partial 边界守卫' 'python' @(
        'scripts/check-partial-boundaries.py'
    )
    Invoke-Native 'partial 边界守卫的负例对照' 'python' @(
        'scripts/test-partial-boundaries-negative.py'
    )
    Invoke-Native 'i18n 覆盖校验' 'python' @(
        'scripts/check-i18n.py', '--src', 'src', '--ts', 'src/i18n/ai-pdf-editor_en.ts'
    )
    # 这道门要求 Debug 的测试二进制已经在（上面 112 行跑过 ctest），所以放在这里。
    # 它对的是「文档里写的专项单测条数 == 实测条数」，防止加了用例却忘了改文档，
    # 让能力背书的数字变成过期承诺。
    Invoke-Native '专项单测条数对账' 'python' @(
        'scripts/check-test-counts.py', '--bin-dir', 'build/windows-debug/bin'
    )
    Invoke-Native '专项单测条数对账的负例对照' 'python' @(
        'scripts/test-test-counts-negative.py'
    )

    Write-Host '=== 版本一致性 ===' -ForegroundColor Cyan
    # 这一行必须单独一行：跟 Write-Host 写在一行会被当成 Write-Host 的参数吃掉，
    # $versionArgs 拿不到值，check-version.ps1 就不会去比对 exe 与安装包。
    $versionArgs = @('-RepositoryRoot', $root)
    if (-not $SkipPackage) {
        $versionArgs += @('-Executable', "build/windows-release/bin/AiPdfEditor.exe")
        $versionArgs += @('-PackagePath', "dist/ai-pdf-editor-$version-win64.zip")
    }
    Invoke-Script '版本一致性校验' (Join-Path $PSScriptRoot 'check-version.ps1') $versionArgs

    if (-not $SkipPackage) {
        Write-Host '=== 发布门负例守卫 ===' -ForegroundColor Cyan
        Invoke-Script '门的负例守卫' (Join-Path $PSScriptRoot 'test-gates.ps1') @(
            '-PackagePath', "dist/ai-pdf-editor-$version-win64.zip",
            '-Version', $version,
            '-RepositoryRoot', $root
        )
    }

    Write-Host ''
    Write-Host "流水线全部通过：AI PDF Editor $version" -ForegroundColor Green
} finally {
    Pop-Location
}
