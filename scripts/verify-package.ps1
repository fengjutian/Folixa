<#
.SYNOPSIS
    验证打包产物在没有 Qt SDK 的环境中可以独立运行。

.DESCRIPTION
    解压 ZIP 到临时目录，清空 PATH 中的 Qt SDK 与 MinGW 工具链目录，
    再启动主程序，校验 Worker 拉起、就绪状态、版本显示与健康检查返回值。
    全部检查通过才算打包合格。
#>
param(
    [string]$PackagePath = 'dist\ai-pdf-editor-0.2.0-win64.zip',
    [string]$Version = '0.2.0',
    [string]$WorkDir = '',
    [string]$ScreenshotPath = ''
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')

$script:Failures = 0

function Test-Check {
    param(
        [string]$Name,
        [bool]$Condition,
        [string]$Detail = ''
    )
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

if (-not (Test-Path -LiteralPath $PackagePath)) {
    Write-Host "找不到安装包: $PackagePath" -ForegroundColor Red
    exit 2
}

# 协议版本从源码里读，不要写死（见 verify-m0.ps1 里的同一个坑）。
$protocolHeader = Get-Content -LiteralPath `
    (Join-Path $PSScriptRoot '..\src\ipc\IpcProtocol.h') -Raw
$protocolMatch = [regex]::Match($protocolHeader, 'kProtocolVersion\s*=\s*(\d+)')
if (-not $protocolMatch.Success) {
    Write-Host '无法从 IpcProtocol.h 解析协议版本' -ForegroundColor Red
    exit 2
}
$ProtocolVersion = $protocolMatch.Groups[1].Value

if (-not $WorkDir) {
    $WorkDir = Join-Path (Join-Path $PSScriptRoot '..\build\verify-package') ([guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null

Write-Host "==> 解压 $PackagePath" -ForegroundColor Cyan
Expand-Archive -LiteralPath $PackagePath -DestinationPath $WorkDir -Force
$appDir = Get-ChildItem -LiteralPath $WorkDir -Directory | Select-Object -First 1
if (-not $appDir) {
    Write-Host '安装包结构异常：没有找到顶层目录' -ForegroundColor Red
    exit 2
}
$appDir = $appDir.FullName

Write-Host '==> 校验包内文件' -ForegroundColor Cyan
foreach ($required in @(
    'AiPdfEditor.exe',
    'PdfAiWorker.exe',
    'pdfium.dll',
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'Qt6Network.dll',
    'platforms/qwindows.dll',
    'libgcc_s_seh-1.dll',
    'libstdc++-6.dll',
    'libwinpthread-1.dll',
    'LICENSE',
    'NOTICE.txt',
    'licenses/THIRD_PARTY_LICENSES.md',
    'licenses/LGPL-3.0.txt',
    'licenses/GPL-3.0.txt',
    'licenses/GCC-RUNTIME-LIBRARY-EXCEPTION.txt',
    'licenses/pdfium-CHECKSUMS.txt',
    'licenses/pdfium/pdfium-BSD-3-Clause.txt',
    "sbom/ai-pdf-editor-$Version.cdx.json"
)) {
    Test-Check "包含 $required" (Test-Path -LiteralPath (Join-Path $appDir $required))
}
if ($script:Failures -gt 0) {
    exit 1
}

Write-Host '==> 校验第三方许可证全文' -ForegroundColor Cyan
$licenseScript = Join-Path $PSScriptRoot 'verify-licenses.ps1'
& powershell -NoProfile -ExecutionPolicy Bypass -File $licenseScript -PackageDir $appDir
if ($LASTEXITCODE -ne 0) {
    $script:Failures++
    Write-Host 'FAIL  许可证校验未通过' -ForegroundColor Red
}

Write-Host '==> 校验 SBOM 与包内文件一致' -ForegroundColor Cyan
$sbomScript = Join-Path $PSScriptRoot 'verify-sbom.ps1'
$sbomPath = Join-Path $appDir "sbom/ai-pdf-editor-$Version.cdx.json"
& powershell -NoProfile -ExecutionPolicy Bypass -File $sbomScript `
    -PackageDir $appDir -SbomPath $sbomPath -Version $Version
if ($LASTEXITCODE -ne 0) {
    $script:Failures++
    Write-Host 'FAIL  SBOM 校验未通过' -ForegroundColor Red
}

Write-Host '==> 清空 Qt SDK 与工具链环境' -ForegroundColor Cyan
$originalPath = $env:PATH
$env:PATH = @(
    "$env:SystemRoot\System32",
    $env:SystemRoot,
    "$env:SystemRoot\System32\Wbem",
    "$env:SystemRoot\System32\WindowsPowerShell\v1.0"
) -join ';'
Remove-Item Env:\QT_PLUGIN_PATH -ErrorAction SilentlyContinue
Remove-Item Env:\QTDIR -ErrorAction SilentlyContinue
Remove-Item Env:\QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
Write-Host "    PATH 已限制为系统目录"

$app = $null
try {
    $workersBefore = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Id })

    Write-Host '==> 启动打包后的主程序' -ForegroundColor Cyan
    # Worker 状态与健康检查结果记在轨迹里（界面上的状态标签在默认收起的
    # AI 面板中，UI Automation 读不到，面板文案也会随改版变化）。
    # 轨迹写在解压目录之外，别混进发布包。
    $tracePath = Join-Path $WorkDir 'worker-trace.log'
    $env:AIPDF_TRACE_FILE = $tracePath
# 恢复日志单独放一份：验收改过文档之后，启动时的恢复问询会挡住界面。
$env:AIPDF_RECOVERY_DIR = Join-Path $WorkDir 'recovery'
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-Process -FilePath (Join-Path $appDir 'AiPdfEditor.exe') `
        -WorkingDirectory $appDir -PassThru

    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 20
    Test-Check '主程序启动并可被自动化接口访问' ($null -ne $probe)
    if ($probe) {
        Test-Check "界面显示版本 $Version" ($probe.Title -match [regex]::Escape($Version)) $probe.Title
    }

    $ready = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'worker state=ready' -TimeoutSeconds 20
    Test-Check 'Worker 状态上报为 ready' $ready ''
    $health = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern ("worker health protocol=$ProtocolVersion version=" + [regex]::Escape($Version)) -TimeoutSeconds 20
    Test-Check '健康检查返回协议版本与 Worker 版本' $health ''

    if ($ScreenshotPath) {
        Test-Check '界面截图已保存' (Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath) $ScreenshotPath
    }

    $workers = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
        Where-Object { $workersBefore -notcontains $_.Id })
    Test-Check 'Worker 被主程序自动拉起' ($workers.Count -ge 1) "count=$($workers.Count)"
} finally {
    if ($app -and -not $app.HasExited) {
        $app.CloseMainWindow() | Out-Null
        if (-not $app.WaitForExit(10000)) {
            $app.Kill()
        }
    }
    $env:PATH = $originalPath
}

Start-Sleep -Seconds 2
$remaining = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
    Where-Object { $workersBefore -notcontains $_.Id })
Test-Check '主程序退出后 Worker 随之结束' ($remaining.Count -eq 0) "count=$($remaining.Count)"

Remove-Item -LiteralPath $WorkDir -Recurse -Force -ErrorAction SilentlyContinue

if ($script:Failures -gt 0) {
    Write-Host "`n打包验收失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n打包验收通过：安装包在没有 Qt SDK 的环境中可正常运行" -ForegroundColor Green
exit 0
