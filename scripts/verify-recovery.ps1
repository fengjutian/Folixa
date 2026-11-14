# v0.15.0 稳定性验收：崩溃之后能把未保存的工作捡回来。
#
# 这条链路是 1.0 的硬要求，而且只有「真的杀掉进程」才能验：
#   - 改文档（删空白页）后不保存，恢复日志落盘；
#   - 直接结束进程（模拟崩溃/断电），不走正常退出；
#   - 重新打开同一份文档，应当弹「发现未保存的工作」，点恢复后改动回来。
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

function Get-MenuItemElement {
    param([string]$NamePattern)
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::MenuItem)
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        try {
            foreach ($item in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)) {
                try {
                    if ($item.Current.Name -match $NamePattern) {
                        return $item
                    }
                } catch {
                }
            }
            return $null
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }
    return $null
}

function Open-AppMenu {
    param([System.Diagnostics.Process]$App)
    $button = Wait-AiPdfElementById -Process $App -IdPattern '*appMenuButton*' -TimeoutSeconds 10
    if (-not $button) {
        return $false
    }
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

function Invoke-MenuItem {
    param([string]$NamePattern)
    $item = Get-MenuItemElement -NamePattern $NamePattern
    if (-not $item) {
        return $false
    }
    $rect = $item.Current.BoundingRectangle
    Invoke-AiPdfMouseClick -X ([int]($rect.Left + $rect.Width / 2)) -Y ([int]($rect.Top + $rect.Height / 2))
    Start-Sleep -Milliseconds 500
    return $true
}

function Click-ButtonById {
    param([System.Diagnostics.Process]$App, [string]$IdPattern)
    $button = Wait-AiPdfElementById -Process $App -IdPattern $IdPattern -TimeoutSeconds 10
    if (-not $button) {
        return $false
    }
    Invoke-AiPdfElement -Element $button | Out-Null
    return $true
}

function Click-ButtonByName {
    param([System.Diagnostics.Process]$App, [string]$Name)
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        foreach ($element in (Get-AiPdfWindowElements -Process $App)) {
            try {
                if ($element.Current.ControlType.ProgrammaticName -eq 'ControlType.Button' -and
                    $element.Current.Name -eq $Name) {
                    Invoke-AiPdfElement -Element $element | Out-Null
                    return $true
                }
            } catch {
            }
        }
        Start-Sleep -Milliseconds 400
    }
    return $false
}

$exe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'AiPdfEditor.exe'
$fixture = Join-Path $RepositoryRoot 'tests/fixtures/blank-pages.pdf'
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-recovery-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$documentPath = Join-Path $workDir 'recovery-demo.pdf'
$tracePath = Join-Path $workDir 'trace.log'
$recoveryDir = Join-Path $workDir 'recovery'
$dataDir = Join-Path $workDir 'data'

$process = $null
$script:exitCode = 1
try {
    Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $exe) $exe
    Test-Check '空白页样例存在' (Test-Path -LiteralPath $fixture) $fixture
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $fixture)) {
        throw '缺少可执行文件或样例'
    }
    Copy-Item -LiteralPath $fixture -Destination $documentPath -Force

    $env:AIPDF_TRACE_FILE = $tracePath
    $env:AIPDF_RECOVERY_DIR = $recoveryDir
    $env:AIPDF_DATA_DIR = $dataDir
    # 这一轮必须让恢复问询真的弹出来，所以不能设 AIPDF_SKIP_RECOVERY_PROMPT。
    Remove-Item Env:AIPDF_SKIP_RECOVERY_PROMPT -ErrorAction SilentlyContinue
    Reset-AiPdfTrace -Path $tracePath

    # ---- 第一轮：改文档，不保存，直接杀进程 ----
    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $documentPath + '"') -PassThru
    Start-Sleep -Seconds 8
    Test-Check '第一次启动成功' ($null -ne $process -and -not $process.HasExited)
    Set-AiPdfForeground -Process $process | Out-Null

    Test-Check '菜单已打开' (Open-AppMenu -App $process)
    Test-Check '找到提案动作' (Invoke-MenuItem -NamePattern '空白页')
    Test-Check '确认框出现' ($null -ne (Wait-AiPdfElementById -Process $process -IdPattern '*editProposalApply*' -TimeoutSeconds 15))
    Test-Check '执行了修改' (Click-ButtonById -App $process -IdPattern '*editProposalApply')

    $appliedDeadline = (Get-Date).AddSeconds(20)
    $appliedTrace = ''
    while ((Get-Date) -lt $appliedDeadline) {
        $appliedTrace = Get-AiPdfTraceText -Path $tracePath
        if ($appliedTrace -match 'ai proposal applied ops=(\d+)') {
            break
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '修改已生效（3 页删成 2 页）' ($appliedTrace -match 'ai proposal applied .*pages=2')

    $recoveryFiles = @()
    if (Test-Path -LiteralPath $recoveryDir) {
        $recoveryFiles = Get-ChildItem -LiteralPath $recoveryDir -Filter '*.json' -ErrorAction SilentlyContinue
    }
    Test-Check '恢复日志已落盘' ($recoveryFiles.Count -ge 1) "files=$($recoveryFiles.Count)"

    # 模拟崩溃：直接结束进程，不给它保存或清理的机会。
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Stop-AiPdfOrphanWorker | Out-Null
    Test-Check '进程被强制结束（模拟崩溃）' ($process.HasExited)

    # ---- 第二轮：重新打开，应当问「要不要恢复」 ----
    Reset-AiPdfTrace -Path $tracePath
    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $documentPath + '"') -PassThru
    Start-Sleep -Seconds 6
    Set-AiPdfForeground -Process $process | Out-Null

    $foundPrompt = $false
    $promptDeadline = (Get-Date).AddSeconds(25)
    while ((Get-Date) -lt $promptDeadline) {
        if (Get-AiPdfTraceText -Path $tracePath) {
            if ((Get-AiPdfTraceText -Path $tracePath) -match 'recovery found') {
                $foundPrompt = $true
                break
            }
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '检测到未保存的工作' $foundPrompt

    if ($ScreenshotPath -ne '') {
        Save-AiPdfWindowScreenshot -Process $process -Path $ScreenshotPath
    }
    Test-Check '点了「恢复」' (Click-ButtonByName -App $process -Name '恢复')

    $replayDeadline = (Get-Date).AddSeconds(25)
    $replayTrace = ''
    while ((Get-Date) -lt $replayDeadline) {
        $replayTrace = Get-AiPdfTraceText -Path $tracePath
        if ($replayTrace -match 'recovery replayed=(\d+)') {
            break
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '恢复重放了命令' ($replayTrace -match 'recovery replayed=1')

    # 再扫一次：页数应当已经是 2，且没有空白页——证明改动真的回来了。
    Set-AiPdfForeground -Process $process | Out-Null
    $rescanOk = (Open-AppMenu -App $process) -and (Invoke-MenuItem -NamePattern '空白页')
    Test-Check '恢复后界面仍可操作' $rescanOk
    Start-Sleep -Seconds 3
    $rescanTrace = Get-AiPdfTraceText -Path $tracePath
    Test-Check '恢复后确实是 2 页' ($rescanTrace -match 'ai blank scan pages=2')

    if ($script:failures -gt 0) {
        Write-Host "恢复验收失败：$($script:failures) 项未通过" -ForegroundColor Red
        $script:exitCode = 1
    } else {
        Write-Host '恢复验收通过：崩溃后重开能检测到未保存工作，恢复后改动与页数都回来了' -ForegroundColor Green
        $script:exitCode = 0
    }
} catch {
    Write-Host "恢复验收异常：$($_.Exception.Message)" -ForegroundColor Red
    $script:exitCode = 1
}

$ErrorActionPreference = 'Continue'
if ($process -and -not $process.HasExited) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
}
try {
    Stop-AiPdfOrphanWorker | Out-Null
} catch {
}
Remove-Item -Recurse -Force $workDir -ErrorAction SilentlyContinue

# 前面拿过一堆 UIA 元素（COM）：进程结束时它们若先于宿主释放，
# PowerShell 会以 -1 收场，看起来像验收崩了。先清干净再退出。
[GC]::Collect()
[GC]::WaitForPendingFinalizers()
exit $script:exitCode
