# v1.1.0 界面验收：阅读与检索。
#
# 覆盖版本文档里剩下的四条验收：
#   1. 四种布局切换后，页码指示、缩略图选中、搜索跳转三处一致；
#   2. 后退 10 次再前进 10 次，视图状态与操作前一致；
#   3. 重开文档后缩放模式保持；
#   4. 窗口 resize 时适应宽度仍然正确。
#
# 证据全部来自 trace + UI Automation 文本，不依赖截图：
# 页码读状态栏「第 N / M 页」，缩略图靠 `thumbnail current page=N`，
# 布局靠 `layout mode=...`，缩放靠 `zoom set/restored` 与 `layout viewport=...`。
#
# 会抢前台并驱动键盘鼠标，**只应在 CI 或空闲机器上跑**。
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [string]$ScreenshotPath = '',
    [string]$RepositoryRoot = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')

$script:failures = 0
function Test-Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    if ($Ok) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name $Detail" -ForegroundColor Red
        $script:failures++
    }
}

$appExe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'AiPdfEditor.exe'
$fixture = Join-Path $RepositoryRoot 'build/pdf-fixtures/multipage.pdf'
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-reading-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$tracePath = Join-Path $workDir 'trace.log'

$env:AIPDF_TRACE_FILE = $tracePath
$env:AIPDF_RECOVERY_DIR = Join-Path $workDir 'recovery'
$env:AIPDF_TRACE_TEXT = '1'
$StartupSeconds = 6

function Start-AiPdfApp {
    param(
        [Parameter(Mandatory = $true)][string]$Pdf,
        [int]$WaitSeconds = $StartupSeconds
    )
    $app = Start-Process -FilePath $appExe -WorkingDirectory (Split-Path -Parent $appExe) `
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

function Click-AiPdfElement {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]$Element
    )
    if ($null -eq $Element) {
        return $false
    }
    if (Invoke-AiPdfElement -Element $Element) {
        Start-Sleep -Milliseconds 400
        return $true
    }
    $bounds = $Element.Current.BoundingRectangle
    Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
        -Y ([int]($bounds.Top + $bounds.Height / 2))
    Start-Sleep -Milliseconds 400
    return $true
}

function Prepare-AiPdfWindow {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $null = Set-AiPdfForeground -Process $Process
    Set-AiPdfWindowGeometry -Process $Process | Out-Null
    Set-AiPdfWindowTopmost -Process $Process -Enable $true | Out-Null
    return Set-AiPdfForeground -Process $Process
}

# 当前页码：状态栏「第 N / M 页」。
function Get-AiPdfCurrentPage {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $probe = Get-AiPdfWindowProbe -Process $Process -TitlePattern 'AI PDF Editor'
    if (-not $probe) {
        return -1
    }
    if ($probe.Text -match '第\s*(\d+)\s*/\s*(\d+)\s*页') {
        return [int]$Matches[1]
    }
    return -1
}

function Wait-AiPdfCurrentPage {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][int]$Page,
        [int]$TimeoutSeconds = 8
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ((Get-AiPdfCurrentPage -Process $Process) -eq $Page) {
            return $true
        }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

# 通过状态栏页码输入框跳页。
function Set-AiPdfPage {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][int]$Page
    )
    $input = Wait-AiPdfElementById -Process $Process -IdPattern '*statusPageInput*' -TimeoutSeconds 5
    if (-not $input) {
        return $false
    }
    $null = Prepare-AiPdfWindow -Process $Process
    $bounds = $input.Current.BoundingRectangle
    Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
        -Y ([int]($bounds.Top + $bounds.Height / 2))
    Start-Sleep -Milliseconds 150
    if (-not (Set-AiPdfElementText -Element $input -Text "$Page")) {
        return $false
    }
    Send-AiPdfKey -Key 'Enter'
    return (Wait-AiPdfCurrentPage -Process $Process -Page $Page -TimeoutSeconds 8)
}

# 只负责跳页，不假设跳转后状态栏显示的页号：
# 双页/封面会把相邻两页排成一行，落到该行后「当前页」可能是行内首页。
function Set-AiPdfPageRaw {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][int]$Page
    )
    $input = Wait-AiPdfElementById -Process $Process -IdPattern '*statusPageInput*' -TimeoutSeconds 5
    if (-not $input) {
        return $false
    }
    $null = Prepare-AiPdfWindow -Process $Process
    $bounds = $input.Current.BoundingRectangle
    Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
        -Y ([int]($bounds.Top + $bounds.Height / 2))
    Start-Sleep -Milliseconds 150
    if (-not (Set-AiPdfElementText -Element $input -Text "$Page")) {
        return $false
    }
    Send-AiPdfKey -Key 'Enter'
    Start-Sleep -Milliseconds 700
    return $true
}

# 最近一次 `zoom set value=` 的数值。
function Get-AiPdfLastZoomValue {
    $text = Get-AiPdfTraceText -Path $tracePath
    $matches = [regex]::Matches($text, 'zoom set value=([0-9.]+)')
    if ($matches.Count -eq 0) {
        return 0.0
    }
    return [double]$matches[$matches.Count - 1].Groups[1].Value
}

# 最近一条 layout trace：viewport / content / zoom。
function Get-AiPdfLayoutSnapshot {
    $text = Get-AiPdfTraceText -Path $tracePath
    $matches = [regex]::Matches($text, 'layout viewport=(\d+) canvas=(\d+) content=(\d+) zoom=([0-9.]+)')
    if ($matches.Count -eq 0) {
        return $null
    }
    $m = $matches[$matches.Count - 1]
    return [pscustomobject]@{
        Viewport = [int]$m.Groups[1].Value
        Content  = [int]$m.Groups[3].Value
        Zoom     = [double]$m.Groups[4].Value
    }
}

function Wait-AiPdfLayoutChange {
    param(
        [Parameter(Mandatory = $true)]$Baseline,
        [int]$TimeoutSeconds = 10
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $now = Get-AiPdfLayoutSnapshot
        if ($null -ne $now -and
            ($now.Viewport -ne $Baseline.Viewport -or
             $now.Content -ne $Baseline.Content -or
             [Math]::Abs($now.Zoom - $Baseline.Zoom) -gt 0.0001)) {
            return $now
        }
        Start-Sleep -Milliseconds 200
    }
    return $null
}

# 打开搜索面板（Ctrl+F），返回查询输入框。
function Open-AiPdfSearchPanel {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$Attempts = 5
    )
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        Set-AiPdfForeground -Process $Process | Out-Null
        if (-not (Test-AiPdfForeground -Process $Process)) {
            Start-Sleep -Milliseconds 400
            continue
        }
        [System.Windows.Forms.SendKeys]::SendWait('^f')
        $input = Wait-AiPdfElementById -Process $Process -IdPattern '*searchQueryInput*' -TimeoutSeconds 5
        if ($input) {
            return $input
        }
        Start-Sleep -Milliseconds 400
    }
    return $null
}

$app = $null
$cursor = Get-AiPdfCursorPosition
try {
    Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $appExe) $appExe
    Test-Check '样例文档存在' (Test-Path -LiteralPath $fixture) $fixture
    if (-not (Test-Path -LiteralPath $appExe) -or -not (Test-Path -LiteralPath $fixture)) {
        throw '缺少可执行文件或样例'
    }
    if ($DryRun) {
        Write-Host 'DRY RUN：前置检查通过，未启动界面' -ForegroundColor Yellow
        exit 0
    }

    # ------------------------------------------------- 场景 1：布局三处一致 --
    Write-Host '==> 场景 1：布局切换后的页码 / 缩略图 / 搜索' -ForegroundColor Cyan
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $fixture
    Test-Check '文档已打开' `
        (Wait-AiPdfCurrentPage -Process $app -Page 1 -TimeoutSeconds 20) "page=$(Get-AiPdfCurrentPage -Process $app)"

    Test-Check '跳到第 5 页' (Set-AiPdfPage -Process $app -Page 5) ''
    Test-Check '缩略图跟着选中第 5 页' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'thumbnail current page=4' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    $modes = @(
        @{ Key = '2'; Trace = 'facing'; Label = '双页' },
        @{ Key = '3'; Trace = 'cover';  Label = '双页封面' },
        @{ Key = '1'; Trace = 'single'; Label = '单页' }
    )
    foreach ($mode in $modes) {
        Reset-AiPdfTrace -Path $tracePath
        $switched = $false
        for ($try = 1; $try -le 3 -and -not $switched; $try++) {
            Set-AiPdfForeground -Process $app | Out-Null
            Send-AiPdfKey -Key $mode.Key -Ctrl
            $switched = Wait-AiPdfTraceMatch -Path $tracePath `
                -Pattern ('layout mode=' + $mode.Trace) -TimeoutSeconds 4
        }
        Test-Check ("切到" + $mode.Label) $switched (Get-AiPdfTraceText -Path $tracePath)

        # 双页/封面把相邻两页排成一行：跳转后「当前页」可能是该行的首页，
        # 所以断言两件事——页码落在目标附近，缩略图与页码指向同一页。
        $null = Set-AiPdfPageRaw -Process $app -Page 8
        $page = Get-AiPdfCurrentPage -Process $app
        Test-Check ($mode.Label + '模式下跳到第 8 页附近') `
            ($page -ge 7 -and $page -le 8) "page=$page"
        Test-Check ($mode.Label + '模式下缩略图与页码一致') `
            (Wait-AiPdfTraceMatch -Path $tracePath -Pattern ('thumbnail current page=' + ($page - 1)) -TimeoutSeconds 5) `
            (Get-AiPdfTraceText -Path $tracePath)
    }

    # 单页模式下做搜索跳转：页码、缩略图、搜索结果应当一起指到第 9 页。
    $input = Open-AiPdfSearchPanel -Process $app
    Test-Check 'Ctrl+F 打开搜索面板' ($null -ne $input) ''
    if ($input) {
        Test-Check '搜索关键词已输入' (Set-AiPdfElementText -Element $input -Text 'Page 9') ''
        $found = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'search finished cancelled=0 hits=1' -TimeoutSeconds 10
        Test-Check '搜索命中 1 处' $found (Get-AiPdfTraceText -Path $tracePath)

        $result = $null
        $deadline = (Get-Date).AddSeconds(8)
        while ((Get-Date) -lt $deadline -and $null -eq $result) {
            foreach ($element in (Get-AiPdfWindowElements -Process $app)) {
                try {
                    if ($element.Current.Name -match '^第\s*9\s*页') {
                        $result = $element
                        break
                    }
                } catch {
                }
            }
            if ($null -eq $result) { Start-Sleep -Milliseconds 200 }
        }
        Test-Check '搜索结果指向第 9 页' ($null -ne $result) ''
        if ($result) {
            $target = $result.Current.BoundingRectangle
            Invoke-AiPdfMouseClick -X ([int]($target.Left + $target.Width / 2)) `
                -Y ([int]($target.Top + $target.Height / 2))
            Test-Check '点搜索结果跳到第 9 页' `
                (Wait-AiPdfCurrentPage -Process $app -Page 9 -TimeoutSeconds 8) `
                "page=$(Get-AiPdfCurrentPage -Process $app)"
            Test-Check '缩略图同步到第 9 页' `
                (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'thumbnail current page=8' -TimeoutSeconds 5) `
                (Get-AiPdfTraceText -Path $tracePath)
        }
    }

    if ($ScreenshotPath) {
        $null = Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath
    }
    Stop-AiPdfApp -App $app
    $app = $null

    # ------------------------------------------------- 场景 2：后退 / 前进 --
    Write-Host '==> 场景 2：后退 10 次再前进 10 次' -ForegroundColor Cyan
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $fixture
    Test-Check '文档已打开（历史场景）' `
        (Wait-AiPdfCurrentPage -Process $app -Page 1 -TimeoutSeconds 20) ""

    $allJumps = $true
    for ($page = 2; $page -le 11; $page++) {
        if (-not (Set-AiPdfPage -Process $app -Page $page)) {
            $allJumps = $false
            Write-Host "    跳到第 $page 页失败" -ForegroundColor DarkGray
            break
        }
    }
    Test-Check '连续跳 10 次（第 2 → 11 页）' $allJumps "page=$(Get-AiPdfCurrentPage -Process $app)"

    for ($i = 0; $i -lt 10; $i++) {
        Set-AiPdfForeground -Process $app | Out-Null
        Send-AiPdfKey -Key 'Left' -Alt
        Start-Sleep -Milliseconds 250
    }
    Test-Check '后退 10 次回到第 1 页' `
        (Wait-AiPdfCurrentPage -Process $app -Page 1 -TimeoutSeconds 8) `
        "page=$(Get-AiPdfCurrentPage -Process $app)"

    for ($i = 0; $i -lt 10; $i++) {
        Set-AiPdfForeground -Process $app | Out-Null
        Send-AiPdfKey -Key 'Right' -Alt
        Start-Sleep -Milliseconds 250
    }
    Test-Check '前进 10 次回到第 11 页' `
        (Wait-AiPdfCurrentPage -Process $app -Page 11 -TimeoutSeconds 8) `
        "page=$(Get-AiPdfCurrentPage -Process $app)"

    Stop-AiPdfApp -App $app
    $app = $null

    # ------------------------------------------- 场景 3：缩放记忆 + 适宽 ----
    Write-Host '==> 场景 3：缩放记忆与 resize 适应宽度' -ForegroundColor Cyan
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $fixture
    Test-Check '文档已打开（缩放场景）' `
        (Wait-AiPdfCurrentPage -Process $app -Page 1 -TimeoutSeconds 20) ''

    # 起始缩放可能被上一次运行记住，不假设固定值：点一次放大，记下实际值。
    $zoomIn = Wait-AiPdfElementById -Process $app -IdPattern '*statusZoomIn*' -TimeoutSeconds 5
    Test-Check '找到缩放放大按钮' ($null -ne $zoomIn) ''
    $zoomed = $false
    for ($try = 1; $try -le 3 -and -not $zoomed; $try++) {
        $null = Prepare-AiPdfWindow -Process $app
        $zoomIn = Wait-AiPdfElementById -Process $app -IdPattern '*statusZoomIn*' -TimeoutSeconds 5
        if ($null -eq $zoomIn) { continue }
        Click-AiPdfElement -Process $app -Element $zoomIn | Out-Null
        $zoomed = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'zoom set value=' -TimeoutSeconds 3
    }
    $zoomValue = Get-AiPdfLastZoomValue
    Test-Check '点放大改变了缩放比例' ($zoomed -and $zoomValue -gt 0) "zoom=$zoomValue"
    $zoomPercent = [int][Math]::Round($zoomValue * 100)

    Stop-AiPdfApp -App $app
    $app = $null

    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $fixture
    Test-Check ("重开文档后恢复 " + $zoomPercent + '%') `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern ('zoom restored value=' + [regex]::Escape("$zoomValue")) -TimeoutSeconds 10) `
        (Get-AiPdfTraceText -Path $tracePath)
    $probe = Get-AiPdfWindowProbe -Process $app -TitlePattern 'AI PDF Editor'
    Test-Check ("状态栏显示 " + $zoomPercent + '%') `
        ($null -ne $probe -and $probe.Text -match ("$zoomPercent\s*%")) ''

    # 适宽：切到「主页」Ribbon 页，点「适宽」。
    Test-Check '切到 Ribbon 主页' (Select-AiPdfRibbonTab -Process $app -Name '主页') ''
    $baseline = Get-AiPdfLayoutSnapshot
    $fitWidth = Wait-AiPdfElementById -Process $app -IdPattern '*ribbonFitWidth*' -TimeoutSeconds 5
    Test-Check '找到「适宽」按钮' ($null -ne $fitWidth) ''
    if ($fitWidth -and $null -ne $baseline) {
        Click-AiPdfElement -Process $app -Element $fitWidth | Out-Null
        $fitted = Wait-AiPdfLayoutChange -Baseline $baseline -TimeoutSeconds 8
        Test-Check '适宽后页面宽度贴合视口' `
            ($null -ne $fitted -and $fitted.Content -le $fitted.Viewport -and
             $fitted.Content -ge ($fitted.Viewport - 120)) `
            $(if ($fitted) { "viewport=$($fitted.Viewport) content=$($fitted.Content) zoom=$($fitted.Zoom)" } else { 'no layout trace' })
        Test-Check '适宽后页码仍是第 1 页' `
            (Wait-AiPdfCurrentPage -Process $app -Page 1 -TimeoutSeconds 5) `
            "page=$(Get-AiPdfCurrentPage -Process $app)"

        if ($null -ne $fitted) {
            $beforeResize = $fitted
            Set-AiPdfWindowGeometry -Process $app -Width 1000 -Height 800 | Out-Null
            $resized = Wait-AiPdfLayoutChange -Baseline $beforeResize -TimeoutSeconds 8
            Test-Check '窗口变窄后仍然适应宽度' `
                ($null -ne $resized -and $resized.Content -le $resized.Viewport -and
                 $resized.Content -ge ($resized.Viewport - 120)) `
                $(if ($resized) { "viewport=$($resized.Viewport) content=$($resized.Content) zoom=$($resized.Zoom)" } else { 'no layout trace' })
            Test-Check '变窄后缩放比例随之下降' `
                ($null -ne $resized -and $resized.Zoom -lt $beforeResize.Zoom - 0.01) `
                $(if ($resized) { "before=$($beforeResize.Zoom) after=$($resized.Zoom)" } else { '' })
        }
    }
} finally {
    if ($null -ne $app -and -not $app.HasExited) {
        $app.Kill()
        $app.WaitForExit(5000) | Out-Null
    }
    Stop-AiPdfOrphanWorker -ErrorAction SilentlyContinue
    if ($null -ne $cursor) { Restore-AiPdfCursorPosition -Position $cursor }
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures -gt 0) {
    Write-Host "`n阅读与检索验收失败：$($script:failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n阅读与检索验收通过" -ForegroundColor Green
exit 0
