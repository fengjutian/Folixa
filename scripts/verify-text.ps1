<#
.SYNOPSIS
    v0.5.0 文本选择、复制与全文搜索验收。

.DESCRIPTION
    三个场景，全部走真实界面路径：
      1. 拉丁文档：鼠标拖选页面上的一行字 → Ctrl+C → 剪贴板内容正确；
         Ctrl+F 打开搜索面板 → 输入关键词 → 命中数正确 → 点结果跳转 →
         页面上出现高亮。
      2. 中文文档：搜索中文关键词，命中数与结果列表正确。
      3. 文字密集的超大文档（默认 1000 页、每页 60 行）：
         搜索要跑上好几秒，搜索过程中可以点「停止」，界面不卡死。

    坐标换算：画布视口的屏幕矩形来自 UI Automation，
    页面在视口里水平居中（PageCanvas::pageLeft），
    页面坐标是点（1/72 英寸），缩放为 1 时 1 点 = 1 逻辑像素，
    屏幕像素还要乘以系统 DPI 比例。

    两条容易被忽略的前置条件：
      - 合成鼠标事件落在**光标下的那个窗口**上，机器上别的程序随时可能
        盖住我们的窗口，所以操作前把窗口置顶并断言它在前台。
      - 轨迹文件是追加写的，每个场景开始前必须清空，
        否则上一轮的记录会被当成本轮的证据。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\text-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10,
    [int]$SearchTimeoutSeconds = 25,
    [int]$DensePages = 1000,
    [int]$DenseLines = 60
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
& $Python (Join-Path $PSScriptRoot 'generate-test-pdfs.py') $fixtureDir --quiet `
    --dense-pages $DensePages --dense-lines $DenseLines
if ($LASTEXITCODE -ne 0) {
    Write-Host '生成测试 PDF 失败' -ForegroundColor Red
    exit 2
}
$textPdf = Join-Path $fixtureDir 'text.pdf'
$cjkPdf = Join-Path $fixtureDir 'cjk.pdf'
$longPdf = Join-Path $fixtureDir 'dense.pdf'

# 轨迹文件：选择与复制过程要能被脚本断言（界面上的选中状态没法直接读）。
$tracePath = Join-Path $WorkDir 'trace.log'
$env:AIPDF_TRACE_FILE = $tracePath
# 恢复日志单独放一份：验收改过文档之后，启动时的恢复问询会挡住界面。
$env:AIPDF_RECOVERY_DIR = Join-Path $WorkDir 'recovery'

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

# 用 Ctrl+F 打开搜索面板。
#
# 快捷键要靠真实键盘事件送达前台窗口；窗口没在前台、或者前台切换时
# 顺带激活了菜单栏，按键都会石沉大海。所以这里反复试，
# 并且每次都确认窗口真的在前台。
function Open-AiPdfSearchPanel {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$Attempts = 5
    )
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        Set-AiPdfForeground -Process $Process | Out-Null
        if (-not (Test-AiPdfForeground -Process $Process)) {
            Write-Host "    第 $attempt 次：窗口没抢到前台，重试" -ForegroundColor DarkGray
            Start-Sleep -Milliseconds 500
            continue
        }
        [System.Windows.Forms.SendKeys]::SendWait('^f')
        $input = Wait-AiPdfElementById -Process $Process -IdPattern '*searchQueryInput*' -TimeoutSeconds 5
        if ($input) {
            return $input
        }
        Write-Host "    第 $attempt 次：Ctrl+F 已发出，搜索面板还没出现，重试" -ForegroundColor DarkGray
        Start-Sleep -Milliseconds 500
    }
    return $null
}

# --------------------------------------------------------------- 场景 1 ------
Write-Host '==> 场景 1：拉丁文档的拖选、复制与搜索' -ForegroundColor Cyan
$app = $null
try {
    # 轨迹是追加写的：清空，否则上一轮的记录会被当成本轮的证据。
    Reset-AiPdfTrace -Path $tracePath
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
        # 置顶：鼠标事件落在光标下的那个窗口上。机器上别的程序随时可能
        # 弹到前台盖住我们的窗口（实测点击落到了隔壁程序上）。
        Set-AiPdfWindowTopmost -Process $app -Enable $true | Out-Null
        $scale = Set-AiPdfForeground -Process $app
        $viewport = Get-AiPdfElementById -Process $app -IdPattern '*pdfCanvas*viewport*'
        $rect = $viewport.Current.BoundingRectangle
        $viewportWidth = $rect.Width / $scale
        # 页面在视口里水平居中（PageCanvas::pageLeft 的算法）：
        # 先扣掉两侧页边距，再把剩余空间平分。
        $pageMargin = 24.0
        $available = $viewportWidth - 2 * $pageMargin
        $pageLeft = [Math]::Max($pageMargin, ($available - 612.0) / 2.0 + $pageMargin)
        Write-Host "    视口矩形 $($rect.Left),$($rect.Top) $($rect.Width)x$($rect.Height)，DPI 比例 $scale，页面左边 $pageLeft"
        # 合成鼠标事件只送到前台窗口：不在前台会被静默丢掉，
        # 表现出来就是「拖选没反应」，很容易误判成产品缺陷。
        Test-Check '主窗口已在前台（否则鼠标事件送不进来）' `
            (Test-AiPdfForeground -Process $app) ''
        # "Folixa"：PDF 坐标 x=60、基线 y=320、字号 32 →
        # 显示空间 x 60..146、y 57..80（页面高 400）。
        $x1 = [int]($rect.Left + ($pageLeft + 66.0) * $scale)
        $y1 = [int]($rect.Top + 68.0 * $scale)
        $x2 = [int]($rect.Left + ($pageLeft + 150.0) * $scale)
        $y2 = $y1
        Write-Host "    拖选区间 ($x1,$y1) -> ($x2,$y2)"
        Invoke-AiPdfMouseDrag -X1 $x1 -Y1 $y1 -X2 $x2 -Y2 $y2

        Start-Sleep -Milliseconds 500

        # 清空剪贴板：上一轮残留的 'Folixa' 会让断言在根本没复制时也显示通过。
        try {
            Set-Clipboard -Value ''
        } catch {
        }

        # Ctrl+C 走 SendKeys，事件落在**前台窗口**上；机器上别的程序随时可能抢走前台，
        # 按键就丢在别处了（实测偶发：剪贴板空、轨迹里也没有 copy）。
        # 判定复制是否真的发生，看应用自己的轨迹而不是只看剪贴板：
        # 轨迹有 copy 记录才算复制动作发生过，剪贴板只是复核内容。
        # 丢一次就重发，重发三次都没有才算失败。
        $copied = $false
        for ($attempt = 1; $attempt -le 3 -and -not $copied; $attempt++) {
            Set-AiPdfWindowTopmost -Process $app -Enable $true | Out-Null
            Set-AiPdfForeground -Process $app | Out-Null
            [System.Windows.Forms.SendKeys]::SendWait('^c')
            $copied = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'copy chars=6 text=Folixa' -TimeoutSeconds 3
            if (-not $copied) {
                Write-Host "    第 $attempt 次 Ctrl+C 没有生效，重试"
            }
        }

        # 剪贴板可能被别的进程短暂占用，读不到就等一下再读。
        $clipboard = ''
        for ($i = 0; $i -lt 10 -and $clipboard -ne 'Folixa'; $i++) {
            try {
                $clipboard = Get-Clipboard -Raw
            } catch {
                $clipboard = ''
            }
            if ($clipboard -ne 'Folixa') {
                Start-Sleep -Milliseconds 200
            }
        }
        Test-Check 'Ctrl+C 复制到剪贴板的文本正确' ($clipboard -eq 'Folixa') "clipboard='$clipboard'"

        $trace = Get-AiPdfTraceText -Path $tracePath
        Test-Check '拖选选中了预期的文字' ($trace -match 'selection page=0 chars=6 text=Folixa') ''
        Test-Check '复制动作被触发' ($trace -match 'copy chars=6 text=Folixa') ''
        Set-AiPdfWindowTopmost -Process $app -Enable $false | Out-Null
    }

    # --- 搜索 ---
    $input = Open-AiPdfSearchPanel -Process $app
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
            Set-AiPdfWindowTopmost -Process $app -Enable $true | Out-Null
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
                $windowRect = Get-AiPdfWindowRect -Process $app
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
                # 几何取不到就明确判失败：静默跳过等于把门开着。
                Test-Check '取到画布视口与窗口矩形（用于高亮检查）' `
                    ($null -ne $viewport -and $null -ne $windowRect) ''
                if ($viewport -and $windowRect) {
                    # 第 2 页的内容起点：页高 400 + 页间距 12。
                    $pageTopInViewport = (400.0 + 12.0) - $scroll
                    $pageMargin = 24.0
                    $available = $viewport.Current.BoundingRectangle.Width / $scale - 2 * $pageMargin
                    $pageLeft = [Math]::Max($pageMargin, ($available - 612.0) / 2.0 + $pageMargin)
                    $highlighted = Test-AiPdfHighlightPixel -Path $ScreenshotPath `
                        -Viewport $viewport.Current.BoundingRectangle `
                        -Window $windowRect `
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
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $cjkPdf -WaitSeconds $StartupSeconds
    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 20 -TitlePattern 'AI PDF Editor' `
        -RequirePattern '第\s*1\s*/\s*2\s*页'
    Test-Check '中文文档已打开' ($null -ne $probe) ''

    $input = Open-AiPdfSearchPanel -Process $app
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
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $longPdf -WaitSeconds $StartupSeconds
    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 25 -TitlePattern 'AI PDF Editor' `
        -RequirePattern "第\s*1\s*/\s*$DensePages\s*页"
    Test-Check "$DensePages 页文档已打开" `
        ($null -ne $probe -and $probe.Text -match "第\s*1\s*/\s*$DensePages\s*页") ''
    # 窗口位置必须确定：要点击的停止按钮用的是屏幕坐标。
    Set-AiPdfWindowGeometry -Process $app | Out-Null

    $input = Open-AiPdfSearchPanel -Process $app
    Test-Check '搜索面板已打开' ($null -ne $input) ''
    if ($input) {
        # 提前把要用的控件抓下来。
        #
        # 搜索会产生上千条结果，每条命中在 UIA 树里都是一个元素；
        # 那之后整树遍历要好几秒（每次元素访问都是一次跨进程往返，
        # 而应用主线程正忙着处理命中），等读到时搜索早就结束了——
        # 于是「正在搜索」这种瞬时状态永远观察不到。
        # 抓好的元素引用只读单个属性，是一次往返，很快。
        $statusElement = Get-AiPdfElementById -Process $app -IdPattern '*searchPanel*secondaryText*'
        $stopElement = Get-AiPdfElementById -Process $app -IdPattern '*searchStopButton*'
        Test-Check '提前抓到状态标签与停止按钮' `
            ($null -ne $statusElement -and $null -ne $stopElement) ''

        Set-AiPdfElementText -Element $input -Text 'Page' | Out-Null
        # 用轨迹判断搜索的起止：界面上的「正在搜索…」是一闪而过的状态，
        # 等界面读到时早就变成最终态了。轨迹按时间顺序记，确定性高得多。
        $started = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'search start query=Page' -TimeoutSeconds 15
        Test-Check '搜索已启动' $started ''

        # 状态标签可能在轨迹落盘后才刷新，轮询一小会儿。
        $status = ''
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $deadline) {
            try {
                $status = $statusElement.Current.Name
            } catch {
                $status = ''
            }
            if ($status -match '正在搜索') {
                break
            }
            Start-Sleep -Milliseconds 200
        }
        Test-Check '界面显示正在搜索' ($status -match '正在搜索') "status='$status'"

        Test-Check '找到停止按钮' ($null -ne $stopElement) ''
        if ($stopElement) {
            Test-Check '停止按钮在搜索期间可用' $stopElement.Current.IsEnabled ''
            # 用真实鼠标点击：UI Automation 的 InvokePattern 在 Qt 按钮上
            # 不一定会真的触发 click()（实测点了没反应，搜索照常跑完）。
            Set-AiPdfWindowTopmost -Process $app -Enable $true | Out-Null
            Set-AiPdfForeground -Process $app | Out-Null
            $stopRect = $stopElement.Current.BoundingRectangle
            Write-Host "    停止按钮屏幕坐标 $($stopRect.Left),$($stopRect.Top) $($stopRect.Width)x$($stopRect.Height)"
            Invoke-AiPdfMouseClick -X ([int]($stopRect.Left + $stopRect.Width / 2)) `
                -Y ([int]($stopRect.Top + $stopRect.Height / 2))

            $cancelled = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'search finished cancelled=1' -TimeoutSeconds 20
            if (-not $cancelled) {
                # 兜底：再用 InvokePattern 试一次。
                $invoke = $null
                if ($stopElement.TryGetCurrentPattern(
                        [System.Windows.Automation.InvokePattern]::Pattern, [ref]$invoke)) {
                    $invoke.Invoke()
                }
                $cancelled = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'search finished cancelled=1' -TimeoutSeconds 20
            }
            Test-Check '搜索可以被停止' $cancelled ''
            $deadline = (Get-Date).AddSeconds(15)
            while ((Get-Date) -lt $deadline) {
                try {
                    $status = $statusElement.Current.Name
                } catch {
                    $status = ''
                }
                if ($status -match '搜索已取消') {
                    break
                }
                Start-Sleep -Milliseconds 300
            }
            Test-Check '界面显示搜索已取消' ($status -match '搜索已取消') "status='$status'"
            Set-AiPdfWindowTopmost -Process $app -Enable $false | Out-Null
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
