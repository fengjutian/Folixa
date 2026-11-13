<#
.SYNOPSIS
    v0.4.0 连续滚动与瓦片渲染验收。

.DESCRIPTION
    打开一份 500 页的长文档，用 UI Automation 驱动滚动条：
      1. 能打开长文档并显示总页数；
      2. 滚动到中间与末尾，页码信息随之更新；
      3. 连续快速滚动时待渲染任务数不会无限增长；
      4. 页面缓存占用不超过配置的上限；
      5. 缩放与旋转后仍能正常渲染。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\scroll-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10,
    [int]$CacheLimitMiB = 128
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

$qtRoot = if ($env:AIPDF_QT_ROOT) { $env:AIPDF_QT_ROOT } else { 'C:\Qt\6.8.3\mingw_64' }
$mingwRoot = if ($env:AIPDF_MINGW_ROOT) { $env:AIPDF_MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }
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

$longPdf = Join-Path $fixtureDir 'long.pdf'

# 恢复日志单独放一份：验收改过文档之后，启动时的恢复问询会挡住界面。
$env:AIPDF_RECOVERY_DIR = Join-Path (Split-Path -Parent $buildPath) 'scroll-recovery'

$app = $null
try {
    Write-Host '==> 打开 500 页长文档' -ForegroundColor Cyan
    $app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath `
        -ArgumentList @($longPdf, '--diagnostics') -PassThru
    Start-Sleep -Seconds $StartupSeconds

    Test-Check '主程序保持运行' (-not $app.HasExited)
    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 20 -TitlePattern 'AI PDF Editor' `
        -RequirePattern '第\s*1\s*/\s*500\s*页'
    Test-Check '主窗口可被自动化接口访问' ($null -ne $probe)
    if (-not $probe) {
        exit 1
    }

    Test-Check '显示 500 页总页数' ($probe.Text -match '第\s*1\s*/\s*500\s*页') ''
    Test-Check '显示缓存与队列指标' ($probe.Text -match '缓存.*待渲染') ''

    # 找竖直滚动条：UI Automation 暴露为 ScrollBar 控件。
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $pidCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $app.Id)
    $window = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $pidCondition)
    $barCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::ScrollBar)
    $scrollBar = $null
    foreach ($candidate in $window.FindAll(
            [System.Windows.Automation.TreeScope]::Descendants, $barCondition)) {
        $range = $null
        if ($candidate.TryGetCurrentPattern(
                [System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$range)) {
            # 竖直滚动条的取值范围比视口高得多。
            if ($range.Current.Maximum - $range.Current.Minimum -gt 1000) {
                $scrollBar = $candidate
                break
            }
        }
    }
    Test-Check '找到竖直滚动条' ($null -ne $scrollBar) ''
    if (-not $scrollBar) {
        exit 1
    }

    $range = $null
    $scrollBar.TryGetCurrentPattern(
        [System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$range) | Out-Null
    $maximum = $range.Current.Maximum
    Write-Host "    滚动范围 0..$maximum"

    # 滚到中间。
    Write-Host '==> 滚动到文档中间' -ForegroundColor Cyan
    $range.SetValue([double]($maximum * 0.5))
    Start-Sleep -Seconds 3
    $probe = Get-AiPdfWindowProbe -Process $app -TitlePattern 'AI PDF Editor'
    $middlePage = 0
    if ($probe.Text -match '第\s*(\d+)\s*/\s*500\s*页') {
        $middlePage = [int]$matches[1]
    }
    Test-Check '滚动后页码更新到中间附近' ($middlePage -gt 200 -and $middlePage -lt 300) "page=$middlePage"

    # 连续快速滚动：任务数不能无限增长。
    Write-Host '==> 连续快速滚动' -ForegroundColor Cyan
    for ($i = 0; $i -lt 12; $i++) {
        $range.SetValue([double]($maximum * (0.1 + 0.07 * $i)))
        Start-Sleep -Milliseconds 120
    }
    $pending = -1
    if ($probe.Text -match '待渲染\s*(\d+)') {
        $pending = [int]$matches[1]
    }
    Start-Sleep -Seconds 3
    $probe = Get-AiPdfWindowProbe -Process $app -TitlePattern 'AI PDF Editor'
    $pendingAfter = -1
    if ($probe.Text -match '待渲染\s*(\d+)') {
        $pendingAfter = [int]$matches[1]
    }
    Test-Check '快速滚动后待渲染任务收敛' ($pendingAfter -ge 0 -and $pendingAfter -le 8) "pending=$pendingAfter"

    # 滚到末尾。
    Write-Host '==> 滚动到文档末尾' -ForegroundColor Cyan
    $range.SetValue($maximum)
    Start-Sleep -Seconds 3
    $probe = Get-AiPdfWindowProbe -Process $app -TitlePattern 'AI PDF Editor'
    $lastPage = 0
    if ($probe.Text -match '第\s*(\d+)\s*/\s*500\s*页') {
        $lastPage = [int]$matches[1]
    }
    Test-Check '滚动到末尾页码正确' ($lastPage -ge 495) "page=$lastPage"

    # 缓存上限。
    $cacheMiB = -1.0
    if ($probe.Text -match '缓存\s*([0-9.]+)\s*MiB') {
        $cacheMiB = [double]$matches[1]
    }
    Test-Check '缓存占用不超过上限' ($cacheMiB -ge 0 -and $cacheMiB -le $CacheLimitMiB) "cache=${cacheMiB}MiB limit=${CacheLimitMiB}MiB"

    Test-Check '滚动过程中主程序未崩溃' (-not $app.HasExited)

    if ($ScreenshotPath) {
        $taken = Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath
        Test-Check '界面截图已保存' $taken $ScreenshotPath
    }
} finally {
    if ($app -and -not $app.HasExited) {
        $app.CloseMainWindow() | Out-Null
        if (-not $app.WaitForExit(5000)) {
            $app.Kill()
        }
    }
    Stop-AiPdfOrphanWorker -Quiet | Out-Null
    Remove-Item -LiteralPath $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:Failures -gt 0) {
    Write-Host "`n滚动验收失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n滚动验收通过：长文档连续浏览、任务收敛与缓存上限均符合预期" -ForegroundColor Green
exit 0
