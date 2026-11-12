<#
.SYNOPSIS
    v0.5.0 文本选择、复制与全文搜索验收。

.DESCRIPTION
    三个场景，全部走真实界面路径：
      1. 拉丁文档：鼠标拖选页面上的一行字 → Ctrl+C → 剪贴板内容正确；
         Ctrl+F 打开搜索面板 → 输入关键词 → 命中数正确 → 点结果跳转 →
         页面上出现高亮。
      2. 中文文档：搜索中文关键词，命中数与结果列表正确。
      3. 500 页长文档：搜索过程中可以「停止」，界面不卡死。

    坐标换算：画布视口的屏幕矩形来自 UI Automation，
    页面在视口里水平居中（PageCanvas::pageLeft），
    页面坐标是点（1/72 英寸），缩放为 1 时 1 点 = 1 逻辑像素，
    屏幕像素还要乘以系统 DPI 比例。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\text-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10,
    [int]$SearchTimeoutSeconds = 25
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')

# 必须在任何窗口/鼠标操作之前：DPI 感知决定坐标用物理像素还是逻辑像素。
$null = Enable-AiPdfDpiAwareness

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
Add-Type -AssemblyName System.Windows.Forms

Write-Host '==> 生成测试 PDF' -ForegroundColor Cyan
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
$fixtureDir = Join-Path $WorkDir 'fixtures'
& $Python (Join-Path $PSScriptRoot 'generate-test-pdfs.py') $fixtureDir --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host '生成测试 PDF 失败' -ForegroundColor Red
    exit 2
}
$textPdf = Join-Path $fixtureDir 'text.pdf'
$cjkPdf = Join-Path $fixtureDir 'cjk.pdf'
$longPdf = Join-Path $fixtureDir 'long.pdf'

# 轨迹文件：选择与复制过程要能被脚本断言（界面上的选中状态没法直接读）。
$tracePath = Join-Path $WorkDir 'trace.log'
$env:AIPDF_TRACE_FILE = $tracePath

function Start-AiPdfApp {
    param(
        [Parameter(Mandatory = $true)][string]$Pdf,
        [int]$WaitSeconds = 10
    )
    $app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath `
        -ArgumentList @($Pdf, '--diagnostics') -PassThru
    Start-Sleep -Seconds $WaitSeconds
    return $app
}

function Stop-AiPdfApp {
    param([Parameter(Mandatory = $true)]$App)
    if ($App -and -not $App.HasExited) {
        $App.CloseMainWindow() | Out-Null
        if (-not $App.WaitForExit(5000)) {
            $App.Kill()
        }
    }
    Stop-AiPdfOrphanWorker -Quiet | Out-Null
}

# 搜索面板里的结果条目（文本形如「第 3 页 · …」）。
function Get-AiPdfSearchResults {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $results = New-Object System.Collections.Generic.List[object]
    foreach ($element in (Get-AiPdfWindowElements -Process $Process)) {
        try {
            if ($element.Current.Name -match '^第\s*\d+\s*页\s*·') {
                $results.Add($element)
            }
        } catch {
            # 元素可能在读取瞬间失效。
        }
    }
    return $results.ToArray()
}

# 搜索面板的状态标签（对象名 searchPanel.secondaryText）。
function Get-AiPdfSearchStatus {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $element = Get-AiPdfElementById -Process $Process -IdPattern '*searchPanel*secondaryText*'
    if (-not $element) {
        return ''
    }
    try {
        # 标签设了 accessibleName，UIA 的 Name 是它；真正的文本要走 TextPattern。
        $pattern = $null
        if ($element.TryGetCurrentPattern(
                [System.Windows.Automation.TextPattern]::Pattern, [ref]$pattern)) {
            $text = $pattern.DocumentRange.GetText(-1)
            if ($text) {
                return $text.Trim()
            }
        }
    } catch {
    }
    try {
        return $element.Current.Name
    } catch {
        return ''
    }
}

# 扫描截图里是否有搜索高亮的颜色。
#
# 高亮是黄色半透明（255,198,51,120）叠在白纸上 → 约 RGB(255,228,159)，
# 和页面白底、黑色文字都差得开。
function Test-AiPdfHighlightPixel {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Viewport,
        [Parameter(Mandatory = $true)]$Window,
        [Parameter(Mandatory = $true)][double]$Scale,
        [Parameter(Mandatory = $true)][double]$PageLeft,
        [Parameter(Mandatory = $true)][double]$PageTopInViewport,
        [double]$X = 55.0,
        [double]$Y = 60.0,
        [double]$Width = 150.0,
        [double]$Height = 28.0
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }
    Add-Type -AssemblyName System.Drawing
    $bitmap = New-Object System.Drawing.Bitmap($Path)
    try {
        $left = [int]($Viewport.Left + ($PageLeft + $X) * $Scale - $Window.Left)
        $top = [int]($Viewport.Top + ($PageTopInViewport + $Y) * $Scale - $Window.Top)
        $right = [int]($left + $Width * $Scale)
        $bottom = [int]($top + $Height * $Scale)
        if ($left -lt 0) { $left = 0 }
        if ($top -lt 0) { $top = 0 }
        if ($right -gt $bitmap.Width) { $right = $bitmap.Width }
        if ($bottom -gt $bitmap.Height) { $bottom = $bitmap.Height }

        for ($y = $top; $y -lt $bottom; $y++) {
            for ($x = $left; $x -lt $right; $x++) {
                $color = $bitmap.GetPixel($x, $y)
                if ($color.R -ge 246 -and $color.G -ge 210 -and $color.G -le 246 -and
                    $color.B -ge 130 -and $color.B -le 195) {
                    return $true
                }
            }
        }
        return $false
    } finally {
        $bitmap.Dispose()
    }
}

# --------------------------------------------------------------- 场景 1 ------
Write-Host '==> 场景 1：拉丁文档的拖选、复制与搜索' -ForegroundColor Cyan
$app = $null
try {
    $app = Start-AiPdfApp -Pdf $textPdf -WaitSeconds $StartupSeconds

    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 20 -TitlePattern 'AI PDF Editor' `
        -RequirePattern '第\s*1\s*/\s*2\s*页'
    Test-Check '文档已打开并显示总页数' `
        ($null -ne $probe -and $probe.Text -match '第\s*1\s*/\s*2\s*页') ''

    # --- 拖选 + 复制 ---
    $viewport = Get-AiPdfElementById -Process $app -IdPattern '*pdfCanvas*viewport*'
    Test-Check '找到画布视口' ($null -ne $viewport) ''
    if ($viewport) {
        $scale = Set-AiPdfForeground -Process $app
        # 窗口位置必须确定，否则算出来的屏幕坐标会打到别处。
        Set-AiPdfWindowGeometry -Process $app | Out-Null
        $viewport = Get-AiPdfElementById -Process $app -IdPattern '*pdfCanvas*viewport*'
        $rect = $viewport.Current.BoundingRectangle
        $viewportWidth = $rect.Width / $scale
        # 页面在视口里水平居中（PageCanvas::pageLeft 的算法）。
        $pageLeft = [Math]::Max(24.0, ($viewportWidth - 612.0) / 2.0 + 24.0)
        Write-Host "    视口矩形 $($rect.Left),$($rect.Top) $($rect.Width)x$($rect.Height)，DPI 比例 $scale，页面左边 $pageLeft"
        # "Folixa"：PDF 坐标 x=60、基线 y=320、字号 32 →
        # 显示空间 x 60..146、y 57..80（页面高 400）。
        $x1 = [int]($rect.Left + ($pageLeft + 66.0) * $scale)
        $y1 = [int]($rect.Top + 68.0 * $scale)
        $x2 = [int]($rect.Left + ($pageLeft + 150.0) * $scale)
        $y2 = $y1
        Write-Host "    拖选区间 ($x1,$y1) -> ($x2,$y2)"
        Invoke-AiPdfMouseDrag -X1 $x1 -Y1 $y1 -X2 $x2 -Y2 $y2

        Start-Sleep -Milliseconds 500
        [System.Windows.Forms.SendKeys]::SendWait('^c')
        Start-Sleep -Milliseconds 800

        $clipboard = ''
        try {
            $clipboard = Get-Clipboard -Raw
        } catch {
            $clipboard = ''
        }
        Test-Check 'Ctrl+C 复制到剪贴板的文本正确' ($clipboard -eq 'Folixa') "clipboard='$clipboard'"

        $trace = Get-AiPdfTraceText -Path $tracePath
        Test-Check '拖选选中了预期的文字' ($trace -match 'selection page=0 chars=6 text=Folixa') ''
        Test-Check '复制动作被触发' ($trace -match 'copy chars=6 text=Folixa') ''
    }

    # --- 搜索 ---
    Set-AiPdfForeground -Process $app | Out-Null
    [System.Windows.Forms.SendKeys]::SendWait('^f')
    $input = Wait-AiPdfElementById -Process $app -IdPattern '*searchQueryInput*' -TimeoutSeconds 15
    Test-Check 'Ctrl+F 打开搜索面板' ($null -ne $input) ''
    if ($input) {
        $written = Set-AiPdfElementText -Element $input -Text 'Folixa'
        Test-Check '搜索关键词已输入' $written ''
        $deadline = (Get-Date).AddSeconds($SearchTimeoutSeconds)
        $status = ''
        while ((Get-Date) -lt $deadline) {
            $status = Get-AiPdfSearchStatus -Process $app
            if ($status -match '共\s*2\s*处结果') {
                break
            }
            Start-Sleep -Milliseconds 300
        }
        Test-Check '搜索命中 2 处' ($status -match '共\s*2\s*处结果') "status='$status'"
        $results = Get-AiPdfSearchResults -Process $app
        Test-Check '结果列表有 2 条' ($results.Count -eq 2) "count=$($results.Count)"

        if ($results.Count -ge 2) {
            # 点第 2 条结果 → 跳到第 2 页。
            Set-AiPdfForeground -Process $app | Out-Null
            $target = $results[1].Current.BoundingRectangle
            Invoke-AiPdfMouseClick -X ([int]($target.Left + $target.Width / 2)) `
                -Y ([int]($target.Top + $target.Height / 2))
            Start-Sleep -Seconds 1
            $probe = Get-AiPdfWindowProbe -Process $app -TitlePattern 'AI PDF Editor'
            Test-Check '点击结果后跳到第 2 页' `
                ($null -ne $probe -and $probe.Text -match '第\s*2\s*/\s*2\s*页') ''

            if ($ScreenshotPath) {
                $taken = Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath
                Test-Check '界面截图已保存' $taken $ScreenshotPath

                # 命中高亮应当画在页面上：扫第 2 页首行那块区域。
                $scale = Set-AiPdfForeground -Process $app
                $viewport = Get-AiPdfElementById -Process $app -IdPattern '*pdfCanvas*viewport*'
                $windowElement = Get-AiPdfElementById -Process $app -IdPattern '*MainWindow'
                $scroll = 0
                foreach ($element in (Get-AiPdfWindowElements -Process $app)) {
                    try {
                        if ($element.Current.ControlType.ProgrammaticName -ne 'ControlType.ScrollBar') {
                            continue
                        }
                        $range = $null
                        if ($element.TryGetCurrentPattern(
                                [System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$range)) {
                            $scroll = $range.Current.Value
                        }
                    } catch {
                    }
                }
                if ($viewport -and $windowElement) {
                    # 第 2 页的内容起点：页高 400 + 页间距 12。
                    $pageTopInViewport = (400.0 + 12.0) - $scroll
                    $pageLeft = [Math]::Max(24.0, ($viewport.Current.BoundingRectangle.Width / $scale - 612.0) / 2.0 + 24.0)
                    $highlighted = Test-AiPdfHighlightPixel -Path $ScreenshotPath `
                        -Viewport $viewport.Current.BoundingRectangle `
                        -Window $windowElement.Current.BoundingRectangle `
                        -Scale $scale -PageLeft $pageLeft -PageTopInViewport $pageTopInViewport
                    Test-Check '页面上出现搜索高亮' $highlighted ''
                }
            }
        }
    }
    Test-Check '场景 1 结束时主程序仍在运行' (-not $app.HasExited) ''
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 2 ------
Write-Host '==> 场景 2：中文文档的全文搜索' -ForegroundColor Cyan
$app = $null
try {
    $app = Start-AiPdfApp -Pdf $cjkPdf -WaitSeconds $StartupSeconds
    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 20 -TitlePattern 'AI PDF Editor' `
        -RequirePattern '第\s*1\s*/\s*2\s*页'
    Test-Check '中文文档已打开' ($null -ne $probe) ''

    Set-AiPdfForeground -Process $app | Out-Null
    [System.Windows.Forms.SendKeys]::SendWait('^f')
    $input = Wait-AiPdfElementById -Process $app -IdPattern '*searchQueryInput*' -TimeoutSeconds 15
    Test-Check '搜索面板已打开' ($null -ne $input) ''
    if ($input) {
        $written = Set-AiPdfElementText -Element $input -Text '中文'
        Test-Check '中文关键词已输入' $written ''
        $deadline = (Get-Date).AddSeconds($SearchTimeoutSeconds)
        $status = ''
        while ((Get-Date) -lt $deadline) {
            $status = Get-AiPdfSearchStatus -Process $app
            if ($status -match '共\s*4\s*处结果') {
                break
            }
            Start-Sleep -Milliseconds 300
        }
        Test-Check '中文搜索命中 4 处' ($status -match '共\s*4\s*处结果') "status='$status'"
        $results = Get-AiPdfSearchResults -Process $app
        Test-Check '中文结果列表有 4 条' ($results.Count -eq 4) "count=$($results.Count)"
    }
    Test-Check '场景 2 结束时主程序仍在运行' (-not $app.HasExited) ''
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 3 ------
Write-Host '==> 场景 3：长文档搜索可停止' -ForegroundColor Cyan
$app = $null
try {
    $app = Start-AiPdfApp -Pdf $longPdf -WaitSeconds $StartupSeconds
    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 25 -TitlePattern 'AI PDF Editor' `
        -RequirePattern '第\s*1\s*/\s*500\s*页'
    Test-Check '500 页文档已打开' `
        ($null -ne $probe -and $probe.Text -match '第\s*1\s*/\s*500\s*页') ''

    Set-AiPdfForeground -Process $app | Out-Null
    [System.Windows.Forms.SendKeys]::SendWait('^f')
    $input = Wait-AiPdfElementById -Process $app -IdPattern '*searchQueryInput*' -TimeoutSeconds 15
    if ($input) {
        Set-AiPdfElementText -Element $input -Text 'Page' | Out-Null
        $searching = Wait-AiPdfProcessTextMatch -Process $app -Pattern '正在搜索' -TimeoutSeconds 10
        Test-Check '搜索开始（界面显示正在搜索）' $searching ''

        $stop = Get-AiPdfElementById -Process $app -IdPattern '*searchStopButton*'
        Test-Check '找到停止按钮' ($null -ne $stop) ''
        if ($stop) {
            $invoke = $null
            if ($stop.TryGetCurrentPattern(
                    [System.Windows.Automation.InvokePattern]::Pattern, [ref]$invoke)) {
                $invoke.Invoke()
            }
            $cancelled = Wait-AiPdfProcessTextMatch -Process $app -Pattern '搜索已取消' -TimeoutSeconds 15
            Test-Check '搜索可以被停止' $cancelled ''
        }
    }
    Test-Check '场景 3 结束时主程序仍在运行' (-not $app.HasExited) ''
} finally {
    Stop-AiPdfApp -App $app
}

if ($script:Failures -gt 0) {
    Write-Host "`n文本验收失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n文本验收通过：选择、复制、全文搜索与高亮均符合预期" -ForegroundColor Green
exit 0
