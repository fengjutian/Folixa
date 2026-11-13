<#
.SYNOPSIS
    v0.10.0 AI 面板与流式对话验收。

.DESCRIPTION
    走真实界面路径，分五个场景：

      1. 面板就位：AI 面板的控件都在，状态标签显示「演示模式」
         （没配密钥时必须显式标出来，不能让人以为模型真的回答了）；
      2. 流式总结：点「总结」→ 先看到本次发送范围 → 回答逐段出现 →
         最后报出 token 用量与估算费用；
      3. 就当前页提问：输入问题 → 发送 → 轨迹里 task=chat，范围仍是当前页；
      4. 取消：点「总结」后立刻点「停止」→ 请求真的停了，不是把答案放完；
      5. 设置对话框：点「设置」→ 弹出「AI 服务设置」→ 里面有密钥输入框 →
         取消后主程序照常活着。

    证据来自应用写出的轨迹（AIPDF_TRACE_FILE）：
      - ai scope page=P chars=N task=T   送出去了什么（页、字数、任务）
      - ai request id=... task=...       发起了流式请求
      - ai usage input=I output=O cost=C token 用量与估算费用
      - ai cancel requested id=...       用户点了停止
      - ai cancelled                     这次生成被真的停掉了

    取消场景要靠 AIPDF_AI_MOCK_DELAY_MS 把演示模式的吐字速度调慢：
    默认 30ms 一段，答案 1 秒内就吐完了，「点停止」会慢一步。

    这套脚本会抢前台窗口、置顶、并用合成鼠标事件操作桌面：
    **不要在别人正在用这台机器时跑**（见 README 的「界面验收会抢桌面」）。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\ai-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10,
    [int]$StreamTimeoutSeconds = 30,
    [int]$MockDelayMs = 250
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
$targetPdf = Join-Path $WorkDir 'ai-target.pdf'
Copy-Item -LiteralPath $sourcePdf -Destination $targetPdf -Force

$tracePath = Join-Path $WorkDir 'trace.log'
$env:AIPDF_TRACE_FILE = $tracePath
$recoveryDir = Join-Path $WorkDir 'recovery'
$env:AIPDF_RECOVERY_DIR = $recoveryDir
$env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
# 演示模式的吐字速度：调慢，取消场景才有确定的时序。
$env:AIPDF_AI_MOCK_DELAY_MS = "$MockDelayMs"

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
#
# Qt 的按钮在禁用时仍然出现在 UIA 树里，但 InvokePattern 不会生效——
# 对禁用按钮 Invoke 会静默什么也不做，看起来就像「点了没反应」。
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

# 读标签的文字。
#
# 标签没设 accessibleName：设了之后 UI Automation 的 Name 就是它，
# 真正的文本只能走 TextPattern 读。先试 TextPattern，再退回 Name。
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

# 对话区的正文（QTextBrowser 的内容走 TextPattern）。
function Get-AiPdfConversationText {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $element = Get-AiPdfElementById -Process $Process -IdPattern '*aiConversation*'
    if (-not $element) {
        return ''
    }
    try {
        $pattern = $null
        if ($element.TryGetCurrentPattern(
                [System.Windows.Automation.TextPattern]::Pattern, [ref]$pattern)) {
            $text = $pattern.DocumentRange.GetText(-1)
            if ($text) {
                return $text
            }
        }
    } catch {
    }
    return ''
}

# --------------------------------------------------------------- 场景 1 ------
Write-Host '==> 场景 1：AI 面板就位（演示模式）' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app

    # Worker 必须先就绪：AI 面板的一切都要经过它。
    $ready = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'worker state=ready' -TimeoutSeconds 20
    Test-Check 'Worker 已就绪' $ready (Get-AiPdfTraceText -Path $tracePath)

    foreach ($id in @('aiSummaryButton', 'aiExplainButton', 'aiTranslateButton',
            'aiPromptInput', 'aiButton', 'aiCancelButton', 'aiSettingsButton',
            'aiConversation', 'aiProviderLabel')) {
        $element = Wait-AiPdfElementById -Process $app -IdPattern "*$id*" -TimeoutSeconds 5
        Test-Check "AI 控件存在：$id" ($null -ne $element) ''
    }

    # Provider 状态是异步回来的（Worker 就绪后另发一次 ai.provider.status），
    # 不能只等轨迹里的 ready：那时候标签还写着「已就绪」。
    $deadline = (Get-Date).AddSeconds(15)
    $provider = ''
    while ((Get-Date) -lt $deadline) {
        $provider = Get-AiPdfLabelText -Process $app -IdPattern '*aiProviderLabel*'
        if ($provider -match '演示模式') {
            break
        }
        Start-Sleep -Milliseconds 300
    }
    # 没配密钥时必须显式标出演示模式：不能让人以为模型真的回答了。
    Test-Check '状态标签标出演示模式' ($provider -match '演示模式') "provider='$provider'"
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 2 ------
Write-Host '==> 场景 2：流式总结与用量' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app
    $null = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'worker state=ready' -TimeoutSeconds 20

    $clicked = Click-AiPdfById -Process $app -IdPattern '*aiSummaryButton*'
    Test-Check '点了「总结」' $clicked ''

    # 先看轨迹：送出去了哪一页、多少字、什么任务。
    $scoped = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern 'ai scope page=1 chars=[1-9]\d* task=summary' -TimeoutSeconds 15
    Test-Check '送出的是当前页且带上了字数' $scoped (Get-AiPdfTraceText -Path $tracePath)

    # 数据范围必须在界面上看得见。
    $scope = Get-AiPdfLabelText -Process $app -IdPattern '*aiScopeLabel*'
    Test-Check '界面显示了本次发送范围' ($scope -match '第\s*1\s*页') "scope='$scope'"

    # 流式回答：等演示答复出现。
    $deadline = (Get-Date).AddSeconds($StreamTimeoutSeconds)
    $conversation = ''
    while ((Get-Date) -lt $deadline) {
        $conversation = Get-AiPdfConversationText -Process $app
        if ($conversation -match '演示模式') {
            break
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '对话区出现了流式回答' ($conversation -match '演示模式') ''

    # 用量与费用。
    $used = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern 'ai usage input=[0-9]+ output=[0-9]+ cost=[0-9.]+' -TimeoutSeconds 15
    Test-Check '报出了 token 用量与估算费用' $used (Get-AiPdfTraceText -Path $tracePath)

    $usage = Get-AiPdfLabelText -Process $app -IdPattern '*aiUsageLabel*'
    Test-Check '界面显示了用量' ($usage -match 'tokens') "usage='$usage'"

    Test-Check '场景 2 结束时主程序仍在运行' (-not $app.HasExited) ''
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 3 ------
Write-Host '==> 场景 3：就当前页提问' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app
    $null = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'worker state=ready' -TimeoutSeconds 20

    $input = Wait-AiPdfElementById -Process $app -IdPattern '*aiPromptInput*' -TimeoutSeconds 5
    Test-Check '提问输入框存在' ($null -ne $input) ''
    if ($input) {
        $written = Set-AiPdfElementText -Element $input -Text '这页在讲什么'
        Test-Check '问题已输入' $written ''
    }
    $null = Click-AiPdfById -Process $app -IdPattern '*aiButton*'

    $asked = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern 'ai scope page=1 chars=[1-9]\d* task=chat' -TimeoutSeconds 15
    Test-Check '提问只送了当前页' $asked (Get-AiPdfTraceText -Path $tracePath)
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 4 ------
Write-Host '==> 场景 4：取消正在进行的生成' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app
    $null = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'worker state=ready' -TimeoutSeconds 20

    $null = Click-AiPdfById -Process $app -IdPattern '*aiSummaryButton*' -SettleMs 200

    # 「停止」按钮要等生成开始才变可点：先等它可用再点，
    # 否则 Invoke 打在禁用按钮上会静默失效。
    $cancel = Wait-AiPdfEnabled -Process $app -IdPattern '*aiCancelButton*' -TimeoutSeconds 15
    Test-Check '生成中「停止」按钮可用' ($null -ne $cancel) ''
    if ($cancel) {
        $null = Invoke-AiPdfElement -Element $cancel
        Start-Sleep -Milliseconds 300
    }

    $requested = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern 'ai cancel requested id=\S+' -TimeoutSeconds 15
    Test-Check '点了「停止」' $requested (Get-AiPdfTraceText -Path $tracePath)

    # 关键：这次生成要真的停下来（收到取消事件），不能把答案放完。
    $cancelled = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'ai cancelled' -TimeoutSeconds 15
    Test-Check '这次生成被停掉了' $cancelled (Get-AiPdfTraceText -Path $tracePath)

    # 停掉之后还能继续用：再发一次要能正常发出去。
    Reset-AiPdfTrace -Path $tracePath
    $null = Click-AiPdfById -Process $app -IdPattern '*aiSummaryButton*' -SettleMs 200
    $again = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern 'ai request id=\S+ task=summary' -TimeoutSeconds 15
    Test-Check '取消之后还能再次发起请求' $again (Get-AiPdfTraceText -Path $tracePath)

    Test-Check '场景 4 结束时主程序仍在运行' (-not $app.HasExited) ''
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 5 ------
Write-Host '==> 场景 5：设置对话框' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app

    $null = Click-AiPdfById -Process $app -IdPattern '*aiSettingsButton*'
    $dialog = Wait-AiPdfDialog -Process $app -TitlePattern 'AI 服务设置' -TimeoutSeconds 15
    Test-Check '「AI 服务设置」对话框打开了' ($null -ne $dialog) ''

    if ($dialog) {
        foreach ($id in @('aiSettingsProvider', 'aiSettingsBaseUrl',
                'aiSettingsModel', 'aiSettingsApiKey')) {
            $element = Wait-AiPdfElementById -Process $app -IdPattern "*$id*" -TimeoutSeconds 5
            Test-Check "设置控件存在：$id" ($null -ne $element) ''
        }
        # 取消：这一轮不写真实凭据管理器。
        $closed = Invoke-AiPdfDialogButton -Process $app -ButtonPattern '^取消$' `
            -TitlePattern 'AI 服务设置' -TimeoutSeconds 10
        Test-Check '对话框可以取消' $closed ''
    }

    Test-Check '场景 5 结束时主程序仍在运行' (-not $app.HasExited) ''

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
    Write-Host "AI 验收失败：$($script:Failures) 项不通过" -ForegroundColor Red
    exit 1
}
Write-Host 'AI 验收全部通过' -ForegroundColor Green
exit 0
