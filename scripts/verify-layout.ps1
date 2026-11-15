# v1.1.0 界面验收：排布方式（单页 / 双页 / 双页封面）。
#
# 判据来自 trace：每次切换都会写下 `layout mode=single|facing|cover`，
# 脚本只负责按快捷键并把「模式真的换了」这件事钉死——不依赖截图，
# 因为双页的视觉差异在不同 DPI 上没法用固定阈值比。
#
# 会抢前台并驱动键盘，**只应在 CI 或空闲机器上跑**。
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [string]$ScreenshotPath = '',
    [string]$RepositoryRoot = '',
    [switch]$DryRun
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

$exe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'AiPdfEditor.exe'
$fixture = Join-Path $RepositoryRoot 'build/pdf-fixtures/annotation.pdf'
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-layout-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$tracePath = Join-Path $workDir 'trace.log'

$process = $null
try {
    Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $exe) $exe
    Test-Check '样例文档存在' (Test-Path -LiteralPath $fixture) $fixture
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $fixture)) {
        throw '缺少可执行文件或样例'
    }
    if ($DryRun) {
        Write-Host 'DRY RUN：前置检查通过，未启动界面' -ForegroundColor Yellow
        exit 0
    }

    $cursor = Get-AiPdfCursorPosition
    $env:AIPDF_TRACE_FILE = $tracePath
    $env:AIPDF_DATA_DIR = (Join-Path $workDir 'data')
    $env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
    Reset-AiPdfTrace -Path $tracePath

    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $fixture + '"') -PassThru
    Test-Check '主程序已启动' ($null -ne $process)

    $deadline = (Get-Date).AddSeconds(60)
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        if (Test-AiPdfForeground -Process $process) { $ready = $true; break }
        Start-Sleep -Milliseconds 300
    }
    Test-Check '主窗口就绪并可切到前台' $ready

    if ($ready) {
        Send-AiPdfKey -Key '2' -Ctrl
        Test-Check 'Ctrl+2 切到双页' (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'layout mode=facing' -TimeoutSeconds 10)

        Send-AiPdfKey -Key '3' -Ctrl
        Test-Check 'Ctrl+3 切到双页（封面）' (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'layout mode=cover' -TimeoutSeconds 10)

        Send-AiPdfKey -Key '1' -Ctrl
        Test-Check 'Ctrl+1 切回单页' (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'layout mode=single' -TimeoutSeconds 10)

        if (-not [string]::IsNullOrWhiteSpace($ScreenshotPath)) {
            Save-AiPdfWindowScreenshot -Process $process -Path $ScreenshotPath -ErrorAction SilentlyContinue
        }
    }
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit(5000) | Out-Null
    }
    Stop-AiPdfOrphanWorker -ErrorAction SilentlyContinue
    if ($null -ne $cursor) { Restore-AiPdfCursorPosition -Position $cursor }
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures -gt 0) {
    Write-Host "`n排布方式验收失败：$($script:failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n排布方式验收通过" -ForegroundColor Green
exit 0
