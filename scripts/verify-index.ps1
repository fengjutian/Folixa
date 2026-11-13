<#
.SYNOPSIS
    v0.9.0 文档索引与检索验收。

.DESCRIPTION
    走真实界面路径，分四个场景：

      1. 索引面板：命令栏的「索引」→ 面板打开，状态标签、按钮、输入框与
         结果列表都在；
      2. 建立索引：点「重建索引」→ 后台把文档切成块 → 状态文字报出块数与
         重建页数；
      3. 检索与定位：输入关键词 → 结果列表出条目 → 点一条 → 跳到那一页并
         高亮那一块；
      4. 增量重建：重启应用再点一次「重建索引」→ 内容没变的页直接复用，
         重建页数为 0。

    证据来自应用写出的轨迹（AIPDF_TRACE_FILE）：
      - index start path=...        开始建索引
      - index done chunks=N rebuilt=M pages=P   建完了：N 个块，重建 M 页
      - index cancelled processed=N  中途取消
      - index chunk page=P id=... rects=R  跳到了某页的某块并算出 R 个高亮矩形

    这套脚本会抢前台窗口、置顶、并用合成鼠标事件操作桌面：
    **不要在别人正在用这台机器时跑**（见 README 的「界面验收会抢桌面」）。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\index-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10,
    [int]$IndexTimeoutSeconds = 60
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

Write-Host '==> 生成测试 PDF' -ForegroundColor Cyan
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
$fixtureDir = Join-Path $WorkDir 'fixtures'
& $Python (Join-Path $PSScriptRoot 'generate-test-pdfs.py') $fixtureDir --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host '生成测试 PDF 失败' -ForegroundColor Red
    exit 2
}

# 打开的是工作目录里的副本：验收不碰原始夹具。
$sourcePdf = Join-Path $fixtureDir 'text.pdf'
$targetPdf = Join-Path $WorkDir 'index-target.pdf'
Copy-Item -LiteralPath $sourcePdf -Destination $targetPdf -Force

$tracePath = Join-Path $WorkDir 'trace.log'
$env:AIPDF_TRACE_FILE = $tracePath
# 索引文件单独放一份：它要跨两次启动保留下来（增量场景要用），
# 也不能污染用户真实的索引目录。
$env:AIPDF_INDEX_DIR = Join-Path $WorkDir 'index'
$recoveryDir = Join-Path $WorkDir 'recovery'
$env:AIPDF_RECOVERY_DIR = $recoveryDir
$env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
if (Test-Path -LiteralPath $recoveryDir) {
    Remove-Item -LiteralPath $recoveryDir -Recurse -Force
}
if (Test-Path -LiteralPath $env:AIPDF_INDEX_DIR) {
    Remove-Item -LiteralPath $env:AIPDF_INDEX_DIR -Recurse -Force
}

# 记下用户真实的鼠标位置：验收要用合成鼠标事件点屏幕坐标，跑完必须放回去。
$script:CursorStart = Get-AiPdfCursorPosition

function Start-AiPdfApp {
    param(
        [string]$Pdf = '',
        [int]$WaitSeconds = 10
    )
    if ($Pdf) {
        $app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath `
            -ArgumentList @($Pdf, '--diagnostics') -PassThru
    } else {
        $app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath -PassThru
    }
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($app.HasExited) {
            break
        }
        if ($app.MainWindowHandle -ne [IntPtr]::Zero) {
            break
        }
        Start-Sleep -Milliseconds 300
    }
    Start-Sleep -Seconds 2
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
    Restore-AiPdfCursorPosition -Position $script:CursorStart
}

function Prepare-AiPdfWindow {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $null = Set-AiPdfForeground -Process $Process
    Set-AiPdfWindowGeometry -Process $Process | Out-Null
    Set-AiPdfWindowTopmost -Process $Process -Enable $true | Out-Null
    return Set-AiPdfForeground -Process $Process
}

# 按 AutomationId 找元素并点它。
function Click-AiPdfById {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$IdPattern,
        [int]$TimeoutSeconds = 10
    )
    $element = Wait-AiPdfElementById -Process $Process -IdPattern $IdPattern -TimeoutSeconds $TimeoutSeconds
    if (-not $element) {
        Write-Host "    没找到 AutomationId：$IdPattern" -ForegroundColor DarkGray
        return $false
    }
    if (Invoke-AiPdfElement -Element $element) {
        Start-Sleep -Milliseconds 600
        return $true
    }
    $bounds = $element.Current.BoundingRectangle
    Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
        -Y ([int]($bounds.Top + $bounds.Height / 2))
    Start-Sleep -Milliseconds 600
    return $true
}

# 打开索引面板。
#
# 命令栏的「索引」按钮是工具栏里的 QToolButton（AutomationId 形如
# commandIndexButton），QAction 的对象名不会出现在 UI Automation 里，按它
# 找不到人。拿不到就退回导航栏的「索引」页签（railButton.index）。
# 面板有没有真的打开，以「重建索引」按钮在不在为准。
function Open-IndexPanel {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process
    )
    $button = Wait-AiPdfElementById -Process $Process -IdPattern '*commandIndexButton*' -TimeoutSeconds 5
    if (-not $button) {
        $button = Wait-AiPdfElementById -Process $Process -IdPattern '*railButton.index*' -TimeoutSeconds 5
    }
    if (-not $button) {
        Write-Host '    没找到「索引」入口按钮' -ForegroundColor DarkGray
        return $false
    }
    $null = Prepare-AiPdfWindow -Process $Process
    if (-not (Invoke-AiPdfElement -Element $button)) {
        $bounds = $button.Current.BoundingRectangle
        Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
            -Y ([int]($bounds.Top + $bounds.Height / 2))
    }
    Start-Sleep -Milliseconds 800
    return ($null -ne (Wait-AiPdfElementById -Process $Process -IdPattern '*indexBuildButton*' -TimeoutSeconds 5))
}

# 索引面板的状态标签（对象名 indexPanel.secondaryText）。
function Get-AiPdfIndexStatus {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $element = Get-AiPdfElementById -Process $Process -IdPattern '*indexPanel*secondaryText*'
    if (-not $element) {
        return ''
    }
    try {
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

# 索引结果条目（文本形如「第 1 页」或「第 1 页 · 章节」）。
function Get-AiPdfIndexResults {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $results = New-Object System.Collections.Generic.List[object]
    foreach ($element in (Get-AiPdfWindowElements -Process $Process)) {
        try {
            if ($element.Current.Name -match '^第\s*\d+\s*页') {
                $results.Add($element)
            }
        } catch {
            # 元素可能在读取瞬间失效。
        }
    }
    return $results.ToArray()
}

# --------------------------------------------------------------- 场景 1 ------
Write-Host '==> 场景 1：索引面板' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app

    $opened = Open-IndexPanel -Process $app
    Test-Check '索引面板能打开' $opened ''

    # 控件逐个找。
    #
    # 结果列表（indexResults）这一项不在这里查：Qt 对**空的** QListWidget
    # 不生成可访问接口，它根本不出现在 UIA 树里。
    # 列表非空之后才查（场景 3 里查），那时它才会暴露出来。
    foreach ($id in @('indexBuildButton', 'indexCancelButton', 'indexQueryInput')) {
        $element = Wait-AiPdfElementById -Process $app -IdPattern "*$id*" -TimeoutSeconds 5
        Test-Check "索引控件存在：$id" ($null -ne $element) ''
    }

    $status = Get-AiPdfIndexStatus -Process $app
    Test-Check '状态标签有内容' ($status -ne '') "status='$status'"
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 2 ------
Write-Host '==> 场景 2：建立索引' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app
    $null = Open-IndexPanel -Process $app

    $clicked = Click-AiPdfById -Process $app -IdPattern '*indexBuildButton*'
    Test-Check '点了「重建索引」' $clicked ''

    # 等状态变成「索引完成」：后台线程要跑一会儿。
    $deadline = (Get-Date).AddSeconds($IndexTimeoutSeconds)
    $status = ''
    while ((Get-Date) -lt $deadline) {
        $status = Get-AiPdfIndexStatus -Process $app
        if ($status -match '索引完成') {
            break
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '索引建完了' ($status -match '索引完成') "status='$status'"
    # 两页的文档应当切出若干个块。
    Test-Check '切出了块' ($status -match '索引完成：\s*([1-9]\d*)\s*个块') "status='$status'"
    # 第一轮没有旧索引：两页都要重建。
    Test-Check '第一轮重建了 2 页' ($status -match '重建\s*2\s*页') "status='$status'"

    Test-Check '轨迹里有建索引的记录' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'index done chunks=[1-9]\d* rebuilt=2 pages=2' -TimeoutSeconds 10) `
        (Get-AiPdfTraceText -Path $tracePath)
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 3 ------
Write-Host '==> 场景 3：检索与定位' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app
    $null = Open-IndexPanel -Process $app

    # 先把索引建起来（上一轮已经建过，这一轮是增量：重建 0 页）。
    $null = Click-AiPdfById -Process $app -IdPattern '*indexBuildButton*'
    $deadline = (Get-Date).AddSeconds($IndexTimeoutSeconds)
    $status = ''
    while ((Get-Date) -lt $deadline) {
        $status = Get-AiPdfIndexStatus -Process $app
        if ($status -match '索引完成') {
            break
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '索引可用' ($status -match '索引完成') "status='$status'"
    # 增量：这一轮内容没变，应当一页都不重建。
    Test-Check '增量重建（重建 0 页）' ($status -match '重建\s*0\s*页') "status='$status'"

    $input = Wait-AiPdfElementById -Process $app -IdPattern '*indexQueryInput*' -TimeoutSeconds 5
    Test-Check '检索输入框存在' ($null -ne $input) ''
    if ($input) {
        $written = Set-AiPdfElementText -Element $input -Text 'Folixa'
        Test-Check '关键词已输入' $written ''
    }

    # 结果列表：两页各一处。
    $deadline = (Get-Date).AddSeconds(15)
    $results = @()
    while ((Get-Date) -lt $deadline) {
        $results = Get-AiPdfIndexResults -Process $app
        if ($results.Count -ge 2) {
            break
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '结果列表有 2 条' ($results.Count -eq 2) "count=$($results.Count)"

    # 列表非空之后，列表控件本身才会在 UIA 树里出现。
    $listElement = Wait-AiPdfElementById -Process $app -IdPattern '*indexResults*' -TimeoutSeconds 5
    Test-Check '结果列表控件在界面上' ($null -ne $listElement) ''

    if ($results.Count -ge 1) {
        # 点第 2 条结果 → 跳到第 2 页，并高亮那一块。
        Reset-AiPdfTrace -Path $tracePath
        Set-AiPdfWindowTopmost -Process $app -Enable $true | Out-Null
        Set-AiPdfForeground -Process $app | Out-Null
        $target = $results[$results.Count - 1].Current.BoundingRectangle
        Invoke-AiPdfMouseClick -X ([int]($target.Left + $target.Width / 2)) `
            -Y ([int]($target.Top + $target.Height / 2))
        Start-Sleep -Seconds 1

        Test-Check '点了结果之后定位到那一块' `
            (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'index chunk page=\d+ id=\S+ rects=[1-9]\d*' -TimeoutSeconds 10) `
            (Get-AiPdfTraceText -Path $tracePath)

        $probe = Get-AiPdfWindowProbe -Process $app -TitlePattern 'AI PDF Editor'
        Test-Check '页面跳到了第 2 页' `
            ($null -ne $probe -and $probe.Text -match '第\s*2\s*/\s*2\s*页') ''
    }

    if ($ScreenshotPath) {
        $taken = Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath
        Test-Check '界面截图已保存' $taken $ScreenshotPath
    }
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "索引验收失败：$($script:Failures) 项不通过" -ForegroundColor Red
    exit 1
}
Write-Host '索引验收全部通过' -ForegroundColor Green
exit 0
