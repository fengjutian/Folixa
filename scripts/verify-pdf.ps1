<#
.SYNOPSIS
    v0.3.0 PDF 能力验收：主程序能打开 PDF、渲染首页并正确报告错误。

.DESCRIPTION
    生成测试 PDF 集合，然后用命令行参数启动主程序直接打开 PDF，
    通过 UI Automation 校验：
      1. 打开普通 PDF → 出现对应标签页、页码信息与状态栏提示；
      2. 打开旋转页 PDF → 页码信息正确；
      3. 打开损坏 PDF → 主程序不崩溃，状态栏给出可见错误。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\pdf-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10
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

$buildPath = (Resolve-Path -LiteralPath $BuildDir -ErrorAction SilentlyContinue)
if (-not $buildPath) {
    Write-Host "Build directory not found: $BuildDir" -ForegroundColor Red
    exit 2
}
$buildPath = $buildPath.Path
$appExe = Join-Path $buildPath 'AiPdfEditor.exe'
if (-not (Test-Path -LiteralPath $appExe)) {
    Write-Host "主程序不存在: $appExe" -ForegroundColor Red
    exit 2
}

# 让子进程能加载 Qt、MinGW 与 PDFium 运行库。
$qtRoot = if ($env:AIPDF_QT_ROOT) { $env:AIPDF_QT_ROOT } else { 'C:\Qt\6.8.3\mingw_64' }
$mingwRoot = if ($env:AIPDF_MINGW_ROOT) { $env:AIPDF_MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }
$pdfiumRoot = if ($env:AIPDF_PDFIUM_ROOT) { $env:AIPDF_PDFIUM_ROOT } else { 'C:\Qt\..\tools\pdfium' }
foreach ($extra in @(
    (Join-Path $qtRoot 'bin'),
    (Join-Path $mingwRoot 'bin'),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\pdfium\bin')
)) {
    if (Test-Path -LiteralPath $extra) {
        $env:PATH = "$extra;$env:PATH"
    }
}

Write-Host '==> 生成测试 PDF' -ForegroundColor Cyan
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
$fixtureDir = Join-Path $WorkDir 'fixtures'
& $Python (Join-Path $PSScriptRoot 'generate-test-pdfs.py') $fixtureDir --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host '生成测试 PDF 失败' -ForegroundColor Red
    exit 2
}

function Invoke-AppWithPdf {
    param(
        [string]$PdfPath,
        [string]$Screenshot = '',
        [int]$WaitSeconds = $StartupSeconds
    )

    $app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath `
        -ArgumentList $PdfPath -PassThru
    $screenshotTaken = $false
    try {
        Start-Sleep -Seconds $WaitSeconds
        if ($app.HasExited) {
            Write-Host "    主程序已退出，退出码 $($app.ExitCode)" -ForegroundColor Red
            return [pscustomobject]@{ Process = $app; Probe = $null; Alive = $false; ScreenshotTaken = $false }
        }
        $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 15
        if ($Screenshot) {
            $screenshotTaken = Save-AiPdfWindowScreenshot -Process $app -Path $Screenshot
        }
        return [pscustomobject]@{ Process = $app; Probe = $probe; Alive = $true; ScreenshotTaken = $screenshotTaken }
    } finally {
        if (-not $app.HasExited) {
            $app.CloseMainWindow() | Out-Null
            if (-not $app.WaitForExit(10000)) {
                $app.Kill()
            }
        }
    }
}

Write-Host '==> 打开普通 PDF' -ForegroundColor Cyan
$normal = Join-Path $fixtureDir 'normal.pdf'
$run = Invoke-AppWithPdf -PdfPath $normal -Screenshot $ScreenshotPath
Test-Check '主程序保持运行' $run.Alive
if ($run.Probe) {
    Test-Check '出现 normal.pdf 标签页' ($run.Probe.Text -match 'normal\.pdf') ''
    Test-Check '显示页码信息' ($run.Probe.Text -match '第\s*1\s*/\s*1\s*页') ''
    Test-Check '状态栏提示已打开' ($run.Probe.Text -match '已打开') ''
    Test-Check '显示缩放比例' ($run.Probe.Text -match '100%') ''
    Test-Check '未出现渲染失败' ($run.Probe.Text -notmatch '渲染失败') ''
    if ($ScreenshotPath) {
        Test-Check '界面截图已保存' $run.ScreenshotTaken $ScreenshotPath
    }
}

Write-Host '==> 打开旋转页 PDF' -ForegroundColor Cyan
$rotated = Join-Path $fixtureDir 'rotated.pdf'
$run = Invoke-AppWithPdf -PdfPath $rotated
Test-Check '主程序保持运行' $run.Alive
if ($run.Probe) {
    Test-Check '旋转页显示页码信息' ($run.Probe.Text -match '第\s*1\s*/\s*1\s*页') ''
    Test-Check '旋转页未渲染失败' ($run.Probe.Text -notmatch '渲染失败') ''
}

Write-Host '==> 打开损坏 PDF' -ForegroundColor Cyan
$corrupt = Join-Path $fixtureDir 'corrupt.pdf'
$run = Invoke-AppWithPdf -PdfPath $corrupt
Test-Check '损坏文件不会导致崩溃' $run.Alive
if ($run.Probe) {
    Test-Check '状态栏给出可见错误' ($run.Probe.Text -match '无法打开') ''
}

Remove-Item -LiteralPath $WorkDir -Recurse -Force -ErrorAction SilentlyContinue

if ($script:Failures -gt 0) {
    Write-Host "`nPDF 验收失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`nPDF 验收通过：打开、渲染与错误处理均符合预期" -ForegroundColor Green
exit 0
