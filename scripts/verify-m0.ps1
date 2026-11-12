<#
.SYNOPSIS
    AI PDF Editor 启动验收脚本（开发构建）。

.DESCRIPTION
    启动主程序，验证 Worker 自动拉起、IPC 健康检查、版本显示与退出处理。
    每项检查输出 PASS / FAIL，脚本退出码在全部通过时为 0。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [int]$StartupSeconds = 8,
    [int]$ShutdownTimeoutSeconds = 10,
    [string]$ScreenshotPath = '',
    [string]$Version = ''
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

if (-not $Version) {
    $cmakeLists = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\CMakeLists.txt') -Raw
    $match = [regex]::Match($cmakeLists, 'project\(AiPdfEditor VERSION ([0-9.]+)')
    if (-not $match.Success) {
        Write-Host '无法从 CMakeLists.txt 解析版本号' -ForegroundColor Red
        exit 2
    }
    $Version = $match.Groups[1].Value
}

# 让子进程能加载 Qt 与 MinGW 运行库，即使 bin 目录里还没有部署 DLL。
$qtRoot = if ($env:AIPDF_QT_ROOT) { $env:AIPDF_QT_ROOT } else { 'C:\Qt\6.8.3\mingw_64' }
$mingwRoot = if ($env:AIPDF_MINGW_ROOT) { $env:AIPDF_MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }
foreach ($extra in @((Join-Path $qtRoot 'bin'), (Join-Path $mingwRoot 'bin'))) {
    if (Test-Path -LiteralPath $extra) {
        $env:PATH = "$extra;$env:PATH"
    }
}

$appExe = Join-Path $buildPath 'AiPdfEditor.exe'
$workerExe = Join-Path $buildPath 'PdfAiWorker.exe'

Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $appExe) $appExe
Test-Check 'Worker 可执行文件与主程序同目录' (Test-Path -LiteralPath $workerExe) $workerExe
if ($script:Failures -gt 0) {
    exit 2
}

Write-Host "启动 $appExe ..." -ForegroundColor Cyan
# 记录启动前已存在的 Worker，避免把历史遗留进程算进本次验收。
$workersBefore = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Id })
# Worker 状态与健康检查结果记在轨迹里：界面上的状态标签在默认收起的
# AI 面板中，UI Automation 读不到，面板文案也会随改版变化。
# 轨迹写在 bin 目录之外，别混进发布包。
$tracePath = Join-Path (Split-Path -Parent $buildPath) 'worker-trace.log'
$env:AIPDF_TRACE_FILE = $tracePath
Reset-AiPdfTrace -Path $tracePath
$app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath -PassThru
try {
    Start-Sleep -Seconds $StartupSeconds

    Test-Check '主程序进程存活' (-not $app.HasExited) "exit=$($app.ExitCode)"

    $workers = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
        Where-Object { $workersBefore -notcontains $_.Id })
    Test-Check 'Worker 被主程序自动拉起' ($workers.Count -ge 1) "count=$($workers.Count)"

    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 15
    Test-Check '主窗口可被自动化接口访问' ($null -ne $probe)
    if ($probe) {
        Test-Check "窗口标题显示版本 $Version" ($probe.Title -match [regex]::Escape($Version)) $probe.Title
    }

    $ready = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'worker state=ready' -TimeoutSeconds 20
    Test-Check 'Worker 状态上报为 ready' $ready ''
    $health = Wait-AiPdfTraceMatch -Path $tracePath `
        -Pattern ("worker health protocol=1 version=" + [regex]::Escape($Version)) -TimeoutSeconds 20
    Test-Check '健康检查返回协议版本与 Worker 版本' $health ''

    if ($ScreenshotPath) {
        Test-Check '界面截图已保存' (Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath) $ScreenshotPath
    }
} finally {
    if (-not $app.HasExited) {
        $app.CloseMainWindow() | Out-Null
        if (-not $app.WaitForExit($ShutdownTimeoutSeconds * 1000)) {
            $app.Kill()
        }
    }
}

Start-Sleep -Seconds 2
$remaining = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
    Where-Object { $workersBefore -notcontains $_.Id })
Test-Check '主程序退出后 Worker 随之结束' ($remaining.Count -eq 0) "count=$($remaining.Count)"

# 强杀路径：CloseMainWindow 走的是正常退出，WorkerClient 析构函数能收尾。
# 被 Kill() 强杀时析构函数不会执行，只有 worker 自己的父进程看门狗能收尾，
# 否则会留下永久孤儿进程并锁住 PdfAiWorker.exe。
Write-Host '==> 强杀主程序，验证 Worker 自行退出' -ForegroundColor Cyan
$workersBeforeKill = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Id })
$killedApp = Start-Process -FilePath $appExe -WorkingDirectory $buildPath -PassThru
try {
    Start-Sleep -Seconds $StartupSeconds
    $killWorkers = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
        Where-Object { $workersBeforeKill -notcontains $_.Id })
    Test-Check '强杀前 Worker 已拉起' ($killWorkers.Count -ge 1) "count=$($killWorkers.Count)"

    $killedApp.Kill()
    $killedApp.WaitForExit(5000) | Out-Null

    $deadline = (Get-Date).AddSeconds(15)
    $aliveAfterKill = @()
    do {
        Start-Sleep -Milliseconds 500
        $aliveAfterKill = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
            Where-Object { $workersBeforeKill -notcontains $_.Id })
    } while ($aliveAfterKill.Count -gt 0 -and (Get-Date) -lt $deadline)
    Test-Check '主程序被强杀后 Worker 自行退出' ($aliveAfterKill.Count -eq 0) "count=$($aliveAfterKill.Count)"
} finally {
    if (-not $killedApp.HasExited) {
        $killedApp.Kill()
    }
    Stop-AiPdfOrphanWorker -Quiet | Out-Null
}

if ($script:Failures -gt 0) {
    Write-Host "`n验收失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n验收通过：M0 启动与 IPC 健康检查全部符合预期" -ForegroundColor Green
exit 0
