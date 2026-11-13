<#
.SYNOPSIS
    v0.8.0 页面管理与安全保存验收。

.DESCRIPTION
    走真实界面路径，分五个场景：

      1. 页面面板：命令栏的「页面」→ 导航面板的页面页 → 页面操作按钮都在；
      2. 页面操作：插入空白页 → 撤销 → 重做 → 旋转 → 下移 → 删除，
         每一步都核对轨迹里的页数；
      3. 安全保存：点「保存」→ 原文件被换成新内容 → 备份留在旁边 →
         临时文件没有残留；
      4. 恢复日志：改过文档之后，恢复目录里有记录；保存之后记录被清掉；
      5. 启动恢复：崩溃之后重新启动，弹出恢复问询，点「恢复」后命令被重放。

    证据来自应用写出的轨迹（AIPDF_TRACE_FILE）：
      - page insert index=P pages=N   插入空白页后页数变成 N
      - page delete index=P pages=N   删除页后页数变成 N
      - page rotate index=P turns=T   页面自身转了 T 个四分之一圈
      - page move from=A to=B         页被移动了
      - annotation undo count=N pages=M   撤销之后页数是 M
      - save ok path=... pages=N      保存成功，页数 N
      - recovery replayed=N           启动时重放了 N 条命令

    这套脚本会抢前台窗口、置顶、并用合成鼠标事件操作桌面：
    **不要在别人正在用这台机器时跑**（见 README 的「界面验收会抢桌面」）。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\pages-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10
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

# 打开的是工作目录里的副本：验收要保存，原始夹具不能被改。
$sourcePdf = Join-Path $fixtureDir 'navigation.pdf'
$targetPdf = Join-Path $WorkDir 'pages-target.pdf'
Copy-Item -LiteralPath $sourcePdf -Destination $targetPdf -Force

$tracePath = Join-Path $WorkDir 'trace.log'
$env:AIPDF_TRACE_FILE = $tracePath
# 恢复日志单独放一份：验收里改过文档，启动时会问要不要恢复，
# 问询框会挡住验收界面。指到临时目录就不会互相干扰。
$recoveryDir = Join-Path $WorkDir 'recovery'
$env:AIPDF_RECOVERY_DIR = $recoveryDir
if (Test-Path -LiteralPath $recoveryDir) {
    Remove-Item -LiteralPath $recoveryDir -Recurse -Force
}

# 记下用户真实的鼠标位置：验收要用合成鼠标事件点屏幕坐标，跑完必须放回去。
$script:CursorStart = Get-AiPdfCursorPosition

function Start-AiPdfApp {
    param(
        # 传空字符串就是「不带文件启动」：应用开欢迎页。
        # 恢复流程要这样起——命令行带文件的话，同一个文档会被打开两次。
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

# 清掉恢复目录里的旧记录。
#
# 上一轮留下的记录会让这次启动弹出恢复问询，弹框是模态的，会挡住验收界面。
# 问询本身由场景 5 单独验，这里只负责别挡路。
function Clear-RecoveryRecords {
    Get-ChildItem -LiteralPath $recoveryDir -Filter '*.json' -ErrorAction SilentlyContinue |
        ForEach-Object {
            Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
        }
}

# --------------------------------------------------------------- 场景 1 ------
Write-Host '==> 场景 1：页面面板' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app

    # 命令栏的「页面」：打开导航面板的页面页。
    $opened = Click-AiPdfById -Process $app -IdPattern '*commandPages*'
    Test-Check '命令栏的「页面」能点' $opened ''
    Start-Sleep -Milliseconds 600

    foreach ($id in @('page.insert', 'page.delete', 'page.rotateRight',
                     'page.rotateLeft', 'page.moveUp', 'page.moveDown')) {
        $button = Wait-AiPdfElementById -Process $app -IdPattern "*$id*" -TimeoutSeconds 5
        Test-Check "页面按钮存在：$id" ($null -ne $button) ''
    }

    $insertButton = Wait-AiPdfElementById -Process $app -IdPattern '*page.insert*' -TimeoutSeconds 5
    Test-Check '「插入」按钮可用' `
        ($null -ne $insertButton -and $insertButton.Current.IsEnabled) `
        '（按钮应当是启用的）'
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 2 ------
Write-Host '==> 场景 2：页面操作与撤销重做' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app
    $null = Click-AiPdfById -Process $app -IdPattern '*commandPages*'

    # 插入：3 页的文档插一页之后应当是 4 页。
    $null = Click-AiPdfById -Process $app -IdPattern '*page.insert*'
    Test-Check '插入空白页后页数变成 4' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'page insert index=\d+ pages=4' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 撤销：回到 3 页。
    Reset-AiPdfTrace -Path $tracePath
    $null = Click-AiPdfById -Process $app -IdPattern '*annotation.undo*'
    Test-Check '撤销插入后页数回到 3' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation undo count=\d+ pages=3' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 重做：又是 4 页。
    Reset-AiPdfTrace -Path $tracePath
    $null = Click-AiPdfById -Process $app -IdPattern '*annotation.redo*'
    Test-Check '重做后页数回到 4' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation redo count=\d+ pages=4' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 旋转：页面自身的 /Rotate 变了，页数不变。
    Reset-AiPdfTrace -Path $tracePath
    $null = Click-AiPdfById -Process $app -IdPattern '*page.rotateRight*'
    Test-Check '旋转页面' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'page rotate index=\d+ turns=1 pages=4' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 下移：第 1 页挪到第 2 位。
    Reset-AiPdfTrace -Path $tracePath
    $null = Click-AiPdfById -Process $app -IdPattern '*page.moveDown*'
    Test-Check '下移页面' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'page move from=0 to=1 pages=4' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 删除：4 页变 3 页。
    Reset-AiPdfTrace -Path $tracePath
    $null = Click-AiPdfById -Process $app -IdPattern '*page.delete*'
    Test-Check '删除页面后页数变成 3' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'page delete index=\d+ pages=3' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 3 ------
Write-Host '==> 场景 3：安全保存' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $targetPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app

    # 场景 2 改过这篇文档，恢复记录还在：问询框是模态的，
    # 探针模式必须跳过它，否则这一轮的界面根本点不动。
    Test-Check '启动时跳过恢复问询' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'recovery prompt skipped' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 先改一页，保存才有内容可写。
    $null = Click-AiPdfById -Process $app -IdPattern '*commandPages*'
    $null = Click-AiPdfById -Process $app -IdPattern '*page.insert*'
    Test-Check '保存前先插了一页' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'page insert index=\d+ pages=4' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)

    $before = (Get-Item -LiteralPath $targetPdf).LastWriteTimeUtc
    Start-Sleep -Seconds 1
    $null = Click-AiPdfById -Process $app -IdPattern '*document.save*'
    Test-Check '保存成功' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'save ok path=.* pages=4' -TimeoutSeconds 10) `
        (Get-AiPdfTraceText -Path $tracePath)

    $after = (Get-Item -LiteralPath $targetPdf).LastWriteTimeUtc
    Test-Check '原文件被换成了新内容' ($after -gt $before) ''

    # 备份留在旁边，临时文件不该残留。
    $backupPath = "$targetPdf.folixa.bak"
    Test-Check '原文件有备份' (Test-Path -LiteralPath $backupPath) $backupPath
    Test-Check '临时文件没有残留' (-not (Test-Path -LiteralPath "$targetPdf.folixa.tmp")) ''

    # 保存之后恢复日志要清掉：这篇文档已经没有未保存的工作了。
    $leftRecords = @(Get-ChildItem -LiteralPath $recoveryDir -Filter '*.json' -ErrorAction SilentlyContinue)
    Test-Check '保存后恢复日志已清空' `
        ($leftRecords.Count -eq 0) `
        "count=$($leftRecords.Count)"
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 4 ------
Write-Host '==> 场景 4：恢复日志' -ForegroundColor Cyan
# 场景 3 保存过 $targetPdf，磁盘上那份已经是 4 页了。
# 这一场景要用一份新的 3 页文档，页数才对得上。
$recoveryPdf = Join-Path $WorkDir 'pages-recovery.pdf'
Copy-Item -LiteralPath $sourcePdf -Destination $recoveryPdf -Force
Clear-RecoveryRecords
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $recoveryPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app
    $null = Click-AiPdfById -Process $app -IdPattern '*commandPages*'
    $null = Click-AiPdfById -Process $app -IdPattern '*page.insert*'
    Test-Check '页面改动已记录' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'page insert index=\d+ pages=4' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)
    $records = @(Get-ChildItem -LiteralPath $recoveryDir -Filter '*.json' -ErrorAction SilentlyContinue)
    Test-Check '恢复目录里有记录' ($records.Count -ge 1) "count=$($records.Count)"
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
}

# --------------------------------------------------------------- 场景 5 ------
Write-Host '==> 场景 5：启动时恢复未保存的工作' -ForegroundColor Cyan
# 这一场景要真的弹出恢复问询框，所以把「跳过问询」的环境变量摘掉。
$skipPrompt = $env:AIPDF_SKIP_RECOVERY_PROMPT
$env:AIPDF_SKIP_RECOVERY_PROMPT = $null
# 用另一份副本：恢复流程会重放命令，别把别的场景的文档搅进来。
$replayPdf = Join-Path $WorkDir 'pages-replay.pdf'
Copy-Item -LiteralPath $sourcePdf -Destination $replayPdf -Force
Clear-RecoveryRecords
$app = $null
try {
    # 第一轮：插一页然后强杀应用，模拟崩溃。
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $replayPdf -WaitSeconds $StartupSeconds
    $null = Prepare-AiPdfWindow -Process $app
    $null = Click-AiPdfById -Process $app -IdPattern '*commandPages*'
    $null = Click-AiPdfById -Process $app -IdPattern '*page.insert*'
    Test-Check '崩溃前插了一页' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'page insert index=\d+ pages=4' -TimeoutSeconds 8) `
        (Get-AiPdfTraceText -Path $tracePath)
    $app.Kill()
    $app.WaitForExit(5000) | Out-Null
    $app = $null
    Stop-AiPdfOrphanWorker -Quiet | Out-Null
    $records = @(Get-ChildItem -LiteralPath $recoveryDir -Filter '*.json' -ErrorAction SilentlyContinue)
    Test-Check '崩溃后恢复日志还在' ($records.Count -ge 1) "count=$($records.Count)"

    # 第二轮：不带文件启动，弹恢复问询，点「恢复」，命令被重放。
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -WaitSeconds $StartupSeconds
    $clicked = Invoke-AiPdfDialogButton -Process $app -ButtonPattern '恢复' `
        -TitlePattern '发现未保存的工作' -TimeoutSeconds 20
    Test-Check '启动时弹出恢复问询' $clicked ''
    Test-Check '恢复后命令被重放' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'recovery replayed=[1-9]\d*' -TimeoutSeconds 10) `
        (Get-AiPdfTraceText -Path $tracePath)
}
finally {
    if ($app) { Stop-AiPdfApp -App $app }
    if ($null -eq $skipPrompt) {
        Remove-Item Env:AIPDF_SKIP_RECOVERY_PROMPT -ErrorAction SilentlyContinue
    } else {
        $env:AIPDF_SKIP_RECOVERY_PROMPT = $skipPrompt
    }
}

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "页面管理验收失败：$($script:Failures) 项不通过" -ForegroundColor Red
    exit 1
}
Write-Host '页面管理验收全部通过' -ForegroundColor Green
exit 0
