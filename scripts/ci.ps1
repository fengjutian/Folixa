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

    if (-not $SkipToolchain) {
        Invoke-Script '安装固定工具链' (Join-Path $PSScriptRoot 'bootstrap-toolchain.ps1')
    }

    Write-Host '=== PDFium ===' -ForegroundColor Cyan
    Invoke-Script '获取 PDFium' (Join-Path $PSScriptRoot 'bootstrap-pdfium.ps1')

    Write-Host '=== 许可证全文 ===' -ForegroundColor Cyan
    Invoke-Script '校验许可证全文' (Join-Path $PSScriptRoot 'fetch-licenses.ps1') @('-VerifyOnly')

    Write-Host '=== Debug ===' -ForegroundColor Cyan
    Invoke-Native '配置 Debug' 'cmake' @('--preset', 'windows-debug')
    Invoke-Native '构建 Debug' 'cmake' @('--build', '--preset', 'windows-debug')
    Invoke-Native '测试 Debug' 'ctest' @('--preset', 'windows-debug', '--output-on-failure')
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

    Write-Host '=== Release ===' -ForegroundColor Cyan
    Invoke-Native '配置 Release' 'cmake' @('--preset', 'windows-release')
    Invoke-Native '构建 Release' 'cmake' @('--build', '--preset', 'windows-release')
    Invoke-Native '测试 Release' 'ctest' @('--preset', 'windows-release', '--output-on-failure')

    if (-not $SkipPackage) {
        Write-Host '=== 打包 ===' -ForegroundColor Cyan
        Invoke-Native '打包' 'cmake' @('--build', '--preset', 'windows-release', '--target', 'package')
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

    Write-Host '=== 版本一致性 ===' -ForegroundColor Cyan
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
