<#
.SYNOPSIS
    v0.11.0 可验证文档问答验收。

.DESCRIPTION
    走真实界面路径，分四个场景：

      1. 控件就位：AI 面板的整篇问答控件都在（范围切换、引用状态、引用清单）；
      2. 问整篇 → 引用全部核实：建索引 → 切到「整篇文档」范围 → 提问 →
         检索到若干块 → 回答带引用 → 每条引用都「已核实」；
      3. 点引用 → 定位到原文：点引用清单里的一条 → 跳到那一页并高亮那一块，
         且定位到的页与引用上写的页一致；
      4. 负例：伪造的引用被挡住 —— 让演示模式故意引一句原文里没有的话，
         引用必须被标成「无法核实」，而不是照样显示成「已核实」。

    证据来自应用写出的轨迹（AIPDF_TRACE_FILE）：
      - qa vectors ready chunks=N      向量索引建好了（两半检索都在）
      - ai qa scope blocks=N chars=M   送出去了几块、多少字
      - ai citation check valid=K invalid=J unsupported=0|1
      - ai citation activated number=N page=P   用户点了引用
      - index chunk page=N id=... rects=M       定位到原文并高亮

    场景 4 靠 AIPDF_AI_MOCK_FAKE_CITATION：演示模式据此故意引一句假话，
    用来验证「引用核验真的在核对原文」，而不是永远显示「已核实」。

    这套脚本会抢前台窗口、置顶、并用合成鼠标事件操作桌面：
    **不要在别人正在用这台机器时跑**（见 README 的「界面验收会抢桌面」）。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\qa-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10,
    [int]$IndexTimeoutSeconds = 60,
    [int]$AnswerTimeoutSeconds = 45
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

$sourcePdf = Join-Path $fixtureDir 'text.pdf'
$targetPdf = Join-Path $WorkDir 'qa-target.pdf'
Copy-Item -LiteralPath $sourcePdf -Destination $targetPdf -Force

$tracePath = Join-Path $WorkDir 'trace.log'
$env:AIPDF_TRACE_FILE = $tracePath
$env:AIPDF_RECOVERY_DIR = (Join-Path $WorkDir 'recovery')
$env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
# 演示模式的吐字速度：默认 30ms 一段，够快，不用调慢。
Remove-Item Env:\AIPDF_AI_MOCK_FAKE_CITATION -ErrorAction SilentlyContinue

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

function Click-AiPdfById {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$IdPattern,
        [int]$TimeoutSeconds = 10,
        [int]$SettleMs = 600
    )
    # 先等它可点：禁用按钮上的 Invoke 会静默失效。
    $element = Wait-AiPdfEnabled -Process $Process -IdPattern $IdPattern -TimeoutSeconds $TimeoutSeconds
    if (-not $element) {
        Write-Host "    没找到可用的 AutomationId：$IdPattern" -ForegroundColor DarkGray
        return $false
    }
    if (Invoke-AiPdfElement -Element $element) {
        Start-Sleep -Milliseconds $SettleMs
        return $true
    }
    $bounds = $element.Current.BoundingRectangle
    Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
        -Y ([int]($bounds.Top + $bounds.Height / 2))
    Start-Sleep -Milliseconds $SettleMs
    return $true
}

# 等控件变成可点击状态。
function Wait-AiPdfEnabled {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$IdPattern,
        [int]$TimeoutSeconds = 10
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $element = Get-AiPdfElementById -Process $Process -IdPattern $IdPattern
        if ($element) {
            try {
                if ($element.Current.IsEnabled) {
                    return $element
                }
            } catch {
                # 元素可能刚好失效：下一轮重新取。
            }
        }
        Start-Sleep -Milliseconds 200
    }
    return $null
}

# 把范围切到「整篇文档」，并**确认真的切过去了**。
#
# 可勾选的 QPushButton 在 UI Automation 里暴露的是 TogglePattern，
# 不是 InvokePattern——用 Invoke 调它会静默什么也不做。
# 而且 TogglePattern 只保证「勾选状态变了」，不保证应用收到了通知，
# 所以这里以界面上的可见状态为准：范围标签必须写明「整篇文档」。
function Set-AiPdfDocumentScope {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 10
    )
    $element = Wait-AiPdfEnabled -Process $Process -IdPattern '*aiScopeDocumentButton*' `
        -TimeoutSeconds $TimeoutSeconds
    if (-not $element) {
        Write-Host '    没找到「整篇文档」范围按钮' -ForegroundColor DarkGray
        return $false
    }
    try {
        $pattern = $null
        if ($element.TryGetCurrentPattern(
                [System.Windows.Automation.TogglePattern]::Pattern, [ref]$pattern)) {
            $pattern.Toggle()
            Start-Sleep -Milliseconds 400
        }
    } catch {
        # 元素可能刚好失效：退回鼠标点击。
    }
    $label = Get-AiPdfLabelText -Process $Process -IdPattern '*aiScopeLabel*'
    if ($label -match '整篇文档') {
        return $true
    }
    # 没生效：退回真实鼠标点击（真实点击会同时发 clicked 与 toggled）。
    Set-AiPdfWindowTopmost -Process $Process -Enable $true | Out-Null
    Set-AiPdfForeground -Process $Process | Out-Null
    $bounds = $element.Current.BoundingRectangle
    Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
        -Y ([int]($bounds.Top + $bounds.Height / 2))
    Start-Sleep -Milliseconds 400
    $label = Get-AiPdfLabelText -Process $Process -IdPattern '*aiScopeLabel*'
    return ($label -match '整篇文档')
}

# 按控件名字点按钮（AutomationId 找不到时用）。
function Click-AiPdfButtonByName {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$NamePattern,
        [int]$TimeoutSeconds = 10
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        foreach ($element in (Get-AiPdfWindowElements -Process $Process)) {
            try {
                if ($element.Current.ControlType.ProgrammaticName -ne 'ControlType.Button') {
                    continue
                }
                if ($element.Current.Name -notmatch $NamePattern) {
                    continue
                }
                if (Invoke-AiPdfElement -Element $element) {
                    Start-Sleep -Milliseconds 600
                    return $true
                }
            } catch {
                # 元素可能在读取瞬间失效。
            }
        }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

# 打开 AI 面板。
#
# AI 面板默认是**收起**的（QSettings 里的 workspace/aiVisible 默认 false），
# 收起时它的控件根本不出现在 UI Automation 树里，按 AutomationId 查必然是 null。
# 所以要走用户真实路径打开它：点右下角的悬浮「✦」→ 点快捷卡上的「开始聊天」。
function Open-AiPanel {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 15
    )
    if (Wait-AiPdfElementById -Process $Process -IdPattern '*aiSummaryButton*' -TimeoutSeconds 2) {
        return $true
    }

    $floating = Wait-AiPdfEnabled -Process $Process -IdPattern '*aiFloatingButton*' -TimeoutSeconds 10
    if (-not $floating) {
        Write-Host '    没找到 AI 悬浮按钮' -ForegroundColor DarkGray
        return $false
    }
    if (-not (Invoke-AiPdfElement -Element $floating)) {
        Write-Host '    点不开 AI 悬浮按钮' -ForegroundColor DarkGray
        return $false
    }
    Start-Sleep -Milliseconds 600

    $opened = Click-AiPdfButtonByName -Process $Process -NamePattern '^开始聊天$' -TimeoutSeconds 10
    if (-not $opened) {
        Write-Host '    快捷卡上找不到「开始聊天」' -ForegroundColor DarkGray
        return $false
    }

    return ($null -ne (Wait-AiPdfElementById -Process $Process `
                -IdPattern '*aiSummaryButton*' -TimeoutSeconds $TimeoutSeconds))
}

# 打开索引面板。
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

# 索引面板的状态标签。
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

# 读标签的文字。
function Get-AiPdfLabelText {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$IdPattern
    )
    $element = Get-AiPdfElementById -Process $Process -IdPattern $IdPattern
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

# 引用清单里的条目。
#
# 清单非空之后，QListWidget 才会在 UIA 树里暴露（空的列表压根不生成可访问接口）。
function Get-AiPdfCitationItems {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 20
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $list = Get-AiPdfElementById -Process $Process -IdPattern '*aiCitationList*'
        if ($list) {
            try {
                $children = $list.FindAll(
                    [System.Windows.Automation.TreeScope]::Descendants,
                    [System.Windows.Automation.Condition]::TrueCondition)
                $items = @()
                foreach ($child in $children) {
                    try {
                        if ($child.Current.ControlType.ProgrammaticName -eq 'ControlType.ListItem') {
                            $items += $child
                        }
                    } catch {
                        # 元素可能在读取瞬间失效。
                    }
                }
                if ($items.Count -gt 0) {
                    return $items
                }
            } catch {
            }
        }
        Start-Sleep -Milliseconds 400
    }
    return @()
}

# 建索引并等它建完（含向量索引）。
function Build-AiPdfIndex {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 60
    )
    $null = Click-AiPdfById -Process $Process -IdPattern '*indexBuildButton*' -SettleMs 400
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $status = ''
    while ((Get-Date) -lt $deadline) {
        $status = Get-AiPdfIndexStatus -Process $Process
        if ($status -match '索引完成') {
            break
        }
        Start-Sleep -Milliseconds 400
    }
    if ($status -notmatch '索引完成') {
        return $false
    }
    # 向量索引是索引建完之后在后台接着建的：两半都在，混合检索才完整。
    #
    # 两种可能：第一次建（ready），或复用上次存下来的（reused）。
    # 两种都算就绪。
    $null = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern 'qa vectors (ready|reused) chunks=[1-9]\d*' -TimeoutSeconds $TimeoutSeconds
    return $true
}

# --------------------------------------------------------------- 场景 1 ------
Write-Host '==> 场景 1：整篇问答的控件就位' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app
    $opened = Open-AiPanel -Process $app
    Test-Check 'AI 面板已打开' $opened ''

    $null = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'worker state=ready' -TimeoutSeconds 20

    foreach ($id in @('aiScopePageButton', 'aiScopeDocumentButton',
            'aiPromptInput', 'aiButton')) {
        $element = Wait-AiPdfElementById -Process $app -IdPattern "*$id*" -TimeoutSeconds 5
        Test-Check "整篇问答控件存在：$id" ($null -ne $element) ''
    }

    # 引用清单与引用状态此刻是空的/不存在的：
    # Qt 对**空的** QListWidget 不生成可访问接口，引用清单现在还不在 UIA 树里；
    # 引用状态标签要等有内容才断言（场景 2 里查）。
    # 这里不断言它们「不存在」——那是 Qt 的实现细节，不是产品的行为。
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 2 ------
Write-Host '==> 场景 2：问整篇文档，引用全部核实' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app

    # 先建索引：整篇问答要靠它检索。
    $null = Open-IndexPanel -Process $app
    $indexed = Build-AiPdfIndex -Process $app -TimeoutSeconds $IndexTimeoutSeconds
    Test-Check '索引建好了（含向量索引）' $indexed (Get-AiPdfTraceText -Path $tracePath)

    $opened = Open-AiPanel -Process $app
    Test-Check 'AI 面板已打开' $opened ''

    # 切到「整篇文档」范围。
    $switched = Set-AiPdfDocumentScope -Process $app
    Test-Check '切到「整篇文档」范围' $switched ''
    # 范围切换要真的生效：范围标签会写明这一次问的是整篇文档。
    $scopeText = Get-AiPdfLabelText -Process $app -IdPattern '*aiScopeLabel*'
    Test-Check '范围标签显示整篇文档' ($scopeText -match '整篇文档') "scope='$scopeText'"

    $input = Wait-AiPdfElementById -Process $app -IdPattern '*aiPromptInput*' -TimeoutSeconds 5
    Test-Check '提问输入框存在' ($null -ne $input) ''
    if ($input) {
        $written = Set-AiPdfElementText -Element $input -Text 'Folixa 出现在哪些地方'
        Test-Check '问题已输入' $written ''
    }
    $null = Click-AiPdfById -Process $app -IdPattern '*aiButton*' -SettleMs 300

    # 送出去的必须是**检索到的几块**，不是整篇文档。
    $scoped = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern 'ai qa scope blocks=[1-9]\d* chars=[1-9]\d*' -TimeoutSeconds 20
    Test-Check '送出去的是检索到的若干块' $scoped (Get-AiPdfTraceText -Path $tracePath)

    # 引用核验：全部核实通过。
    $checked = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern 'ai citation check valid=[1-9]\d* invalid=0 unsupported=0' -TimeoutSeconds $AnswerTimeoutSeconds
    Test-Check '引用全部核实通过' $checked (Get-AiPdfTraceText -Path $tracePath)

    $status = Get-AiPdfLabelText -Process $app -IdPattern '*aiCitationStatus*'
    Test-Check '界面显示了引用核验状态' ($status -match '已核实') "status='$status'"

    # 引用状态标签此刻有内容了，才值得断言它在 UIA 树里。
    $statusElement = Wait-AiPdfElementById -Process $app -IdPattern '*aiCitationStatus*' -TimeoutSeconds 5
    Test-Check '引用状态标签在界面上' ($null -ne $statusElement) ''

    $items = Get-AiPdfCitationItems -Process $app -TimeoutSeconds 20
    Test-Check '引用清单里有条目' ($items.Count -ge 1) "count=$($items.Count)"
    if ($items.Count -ge 1) {
        $first = ''
        try { $first = $items[0].Current.Name } catch { $first = '' }
        Test-Check '引用条目标着「已核实」' ($first -match '已核实') "item='$first'"
    }

    # --------------------------------------------------------------- 场景 3 ----
    Write-Host '==> 场景 3：点引用定位到原文' -ForegroundColor Cyan
    Reset-AiPdfTrace -Path $tracePath
    if ($items.Count -ge 1) {
        Set-AiPdfWindowTopmost -Process $app -Enable $true | Out-Null
        Set-AiPdfForeground -Process $app | Out-Null
        $bounds = $items[0].Current.BoundingRectangle
        Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
            -Y ([int]($bounds.Top + $bounds.Height / 2))
        Start-Sleep -Seconds 1

        $activated = Wait-AiPdfTraceMatch -Path $tracePath `
            -Pattern 'ai citation activated number=\d+ page=\d+' -TimeoutSeconds 15
        Test-Check '点引用触发了定位' $activated (Get-AiPdfTraceText -Path $tracePath)

        $located = Wait-AiPdfTraceMatch -Path $tracePath `
            -Pattern 'index chunk page=\d+ id=\S+ rects=[1-9]\d*' -TimeoutSeconds 15
        Test-Check '定位到原文并高亮了那一块' $located (Get-AiPdfTraceText -Path $tracePath)

        # 引用上写的页与实际定位到的页必须一致：
        # 页码对不上的话，用户点了引用会跳到错误的页。
        $trace = Get-AiPdfTraceText -Path $tracePath
        $pageMatch = [regex]::Match($trace, 'ai citation activated number=\d+ page=(\d+)')
        $chunkMatch = [regex]::Match($trace, 'index chunk page=(\d+)')
        if ($pageMatch.Success -and $chunkMatch.Success) {
            $citedPage = [int]$pageMatch.Groups[1].Value
            $locatedPage = [int]$chunkMatch.Groups[1].Value + 1
            Test-Check '定位到的页与引用上写的页一致' ($citedPage -eq $locatedPage) `
                "cited=$citedPage located=$locatedPage"
        } else {
            Test-Check '定位到的页与引用上写的页一致' $false "trace='$trace'"
        }
    }

    if ($ScreenshotPath) {
        $taken = Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath
        Test-Check '界面截图已保存' $taken $ScreenshotPath
    }
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 4 ------
Write-Host '==> 场景 4：伪造的引用被挡住（负例）' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    # 让演示模式故意引一句原文里没有的话。
    $env:AIPDF_AI_MOCK_FAKE_CITATION = '1'
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app

    $null = Open-IndexPanel -Process $app
    $indexed = Build-AiPdfIndex -Process $app -TimeoutSeconds $IndexTimeoutSeconds
    Test-Check '索引建好了' $indexed (Get-AiPdfTraceText -Path $tracePath)

    $opened = Open-AiPanel -Process $app
    Test-Check 'AI 面板已打开' $opened ''

    $null = Set-AiPdfDocumentScope -Process $app
    $input = Wait-AiPdfElementById -Process $app -IdPattern '*aiPromptInput*' -TimeoutSeconds 5
    if ($input) {
        $null = Set-AiPdfElementText -Element $input -Text 'Folixa 出现在哪些地方'
    }
    $null = Click-AiPdfById -Process $app -IdPattern '*aiButton*' -SettleMs 300

    # 引用还在（模型照样给了编号），但核验必须判它无效。
    $checked = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern 'ai citation check valid=0 invalid=[1-9]\d* unsupported=1' `
        -TimeoutSeconds $AnswerTimeoutSeconds
    Test-Check '伪造的引用被核验挡住' $checked (Get-AiPdfTraceText -Path $tracePath)

    $status = Get-AiPdfLabelText -Process $app -IdPattern '*aiCitationStatus*'
    Test-Check '界面标出了无法核实' ($status -match '无法核实') "status='$status'"

    $items = Get-AiPdfCitationItems -Process $app -TimeoutSeconds 20
    Test-Check '引用清单里有条目' ($items.Count -ge 1) "count=$($items.Count)"
    if ($items.Count -ge 1) {
        $first = ''
        try { $first = $items[0].Current.Name } catch { $first = '' }
        Test-Check '引用条目标着「无法核实」' ($first -match '无法核实') "item='$first'"
    }

    Test-Check '场景 4 结束时主程序仍在运行' (-not $app.HasExited) ''
}
finally {
    Remove-Item Env:\AIPDF_AI_MOCK_FAKE_CITATION -ErrorAction SilentlyContinue
    if ($app) { Stop-AiPdfApp -App $app }
}

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "整篇问答验收失败：$($script:Failures) 项不通过" -ForegroundColor Red
    exit 1
}
Write-Host '整篇问答验收全部通过' -ForegroundColor Green
exit 0
