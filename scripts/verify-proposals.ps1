# v0.14.0 界面验收：AI 提案必须经用户确认才生效。
#
# 三件事必须同时成立，缺一条这条链路就不安全：
#   1. 点「取消」＝什么都没发生（页数不变、审计记 cancelled）；
#   2. 点「执行」＝按提案改文档（页数减少、审计记 applied）；
#   3. 改完能撤销（Ctrl+Z 回到原页数）。
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [string]$ScreenshotPath = '',
    [string]$RepositoryRoot = ''
)

$ErrorActionPreference = 'Stop'
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

function Open-AppMenu {
    param([System.Diagnostics.Process]$App)
    $button = Wait-AiPdfElementById -Process $App -IdPattern '*appMenuButton*' -TimeoutSeconds 10
    if (-not $button) {
        return $false
    }
    # 菜单是弹出窗口：点一下未必弹出来（窗口没在前台时第一次点击只激活窗口），
    # 所以点完要确认真的出现了菜单项，不行就再点。
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        Set-AiPdfForeground -Process $App | Out-Null
        Start-Sleep -Milliseconds 300
        $rect = $button.Current.BoundingRectangle
        Invoke-AiPdfMouseClick -X ([int]($rect.Left + $rect.Width / 2)) -Y ([int]($rect.Top + $rect.Height / 2))
        Start-Sleep -Milliseconds 800
        if (Get-MenuItemElement -NamePattern '空白页') {
            return $true
        }
    }
    return $false
}

function Get-MenuItemElement {
    param([string]$NamePattern)
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::MenuItem)
    foreach ($item in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)) {
        try {
            if ($item.Current.Name -match $NamePattern) {
                return $item
            }
        } catch {
        }
    }
    return $null
}

function Invoke-MenuItem {
    param([System.Diagnostics.Process]$App, [string]$NamePattern)
    $item = Get-MenuItemElement -NamePattern $NamePattern
    if (-not $item) {
        return $false
    }
    $rect = $item.Current.BoundingRectangle
    Invoke-AiPdfMouseClick -X ([int]($rect.Left + $rect.Width / 2)) -Y ([int]($rect.Top + $rect.Height / 2))
    Start-Sleep -Milliseconds 500
    return $true
}

function Click-DialogButton {
    param([System.Diagnostics.Process]$App, [string]$IdPattern)
    $button = Wait-AiPdfElementById -Process $App -IdPattern $IdPattern -TimeoutSeconds 10
    if (-not $button) {
        return $false
    }
    Invoke-AiPdfElement -Element $button | Out-Null
    return $true
}

$exe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'AiPdfEditor.exe'
$fixture = Join-Path $RepositoryRoot 'tests/fixtures/blank-pages.pdf'
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-proposal-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$tracePath = Join-Path $workDir 'trace.log'
$dataDir = Join-Path $workDir 'data'
$auditPath = Join-Path $dataDir 'ai/audit.jsonl'

$process = $null
try {
    Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $exe) $exe
    Test-Check '空白页样例存在' (Test-Path -LiteralPath $fixture) $fixture
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $fixture)) {
        throw '缺少可执行文件或样例'
    }

    $env:AIPDF_TRACE_FILE = $tracePath
    $env:AIPDF_DATA_DIR = $dataDir
    $env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
    Reset-AiPdfTrace -Path $tracePath

    Enable-AiPdfDpiAwareness
    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $fixture + '"') -PassThru
    Start-Sleep -Seconds 7
    Test-Check '主程序已启动' ($null -ne $process -and -not $process.HasExited)
    Set-AiPdfForeground -Process $process | Out-Null

    # ---- 先点「取消」：什么都不该发生 ----
    Test-Check '菜单已打开' (Open-AppMenu -App $process)
    Test-Check '找到提案动作' (Invoke-MenuItem -App $process -NamePattern '空白页')
    $dialogCancel = Wait-AiPdfElementById -Process $process -IdPattern '*editProposalCancel*' -TimeoutSeconds 15
    Test-Check '弹出确认框' ($null -ne $dialogCancel)
    Test-Check '确认框列出要删的页' ($null -ne (Wait-AiPdfElementById -Process $process -IdPattern '*editProposalList*' -TimeoutSeconds 5))
    if ($ScreenshotPath -ne '') {
        Save-AiPdfWindowScreenshot -Process $process -Path $ScreenshotPath
    }
    Test-Check '点了「取消」' (Click-DialogButton -App $process -IdPattern '*editProposalCancel')
    Start-Sleep -Seconds 2

    $traceText = Get-AiPdfTraceText -Path $tracePath
    Test-Check '取消后没有任何修改' (-not ($traceText -match 'ai proposal applied'))
    Test-Check '审计记下「取消」' ((Test-Path -LiteralPath $auditPath) -and
        ([System.IO.File]::ReadAllText($auditPath) -match '"result":"cancelled"'))

    # ---- 再点「执行」：应当真的删除 ----
    Set-AiPdfForeground -Process $process | Out-Null
    Test-Check '菜单已打开（第二次）' (Open-AppMenu -App $process)
    Test-Check '再次找到提案动作' (Invoke-MenuItem -App $process -NamePattern '空白页')
    Test-Check '再次弹出确认框' ($null -ne (Wait-AiPdfElementById -Process $process -IdPattern '*editProposalApply*' -TimeoutSeconds 15))
    Test-Check '点了「执行」' (Click-DialogButton -App $process -IdPattern '*editProposalApply')

    $appliedDeadline = (Get-Date).AddSeconds(20)
    $appliedTrace = ''
    while ((Get-Date) -lt $appliedDeadline) {
        $appliedTrace = Get-AiPdfTraceText -Path $tracePath
        if ($appliedTrace -match 'ai proposal applied ops=(\d+)') {
            break
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '执行后命令真的生效' ($appliedTrace -match 'ai proposal applied ops=1')
    # 原 3 页、删掉 1 个空白页 → 2 页。
    Test-Check '页数从 3 变成 2' ($appliedTrace -match 'ai proposal applied .*pages=2')

    $auditText = ''
    if (Test-Path -LiteralPath $auditPath) {
        $auditText = [System.IO.File]::ReadAllText($auditPath)
    }
    Test-Check '审计记下「已执行」' ($auditText -match '"result":"applied"')
    Test-Check '审计只记元数据（不含正文）' (-not ($auditText -match '第一页|第三页'))

    # ---- 撤销：回到原页数 ----
    Set-AiPdfForeground -Process $process | Out-Null
    Add-Type -AssemblyName System.Windows.Forms
    Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait('^z')
    $undoDeadline = (Get-Date).AddSeconds(15)
    $undoTrace = ''
    while ((Get-Date) -lt $undoDeadline) {
        $undoTrace = Get-AiPdfTraceText -Path $tracePath
        if ($undoTrace -match 'annotation undo .*pages=3') {
            break
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '撤销后页数回到 3' ($undoTrace -match 'annotation undo .*pages=3')

    if ($script:failures -gt 0) {
        Write-Host "提案验收失败：$($script:failures) 项未通过" -ForegroundColor Red
        exit 1
    }
    Write-Host '提案验收通过：取消不改动、执行才生效、可撤销、审计如实记录' -ForegroundColor Green
    exit 0
} catch {
    Write-Host "提案验收异常：$($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    Stop-AiPdfOrphanWorker
    Remove-Item -Recurse -Force $workDir -ErrorAction SilentlyContinue
}
