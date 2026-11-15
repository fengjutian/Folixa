# v1.1.0 界面验收：放映模式。
#
# 判据全部来自"用户真能看到/按到的东西"：
#   - F5 之后出现全屏放映窗口；
#   - 按右键翻到下一页（trace 里看得到页码变化）；
#   - Esc 退出，且退出前停在第 2 页（说明翻页真的生效，不是只开了个窗口）。
#
# 注意：这个脚本会把放映窗口拉成全屏并抢前台，**只应在 CI 或空闲机器上跑**。
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [string]$ScreenshotPath = '',
    [string]$RepositoryRoot = '',
    # 只做前置检查，不启动界面（在没有桌面的环境里做冒烟）。
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
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-present-' + [guid]::NewGuid().ToString('N'))
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

    # 等到主窗口能接收输入（有窗口句柄且能切前台）。
    $deadline = (Get-Date).AddSeconds(60)
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        if (Test-AiPdfForeground -Process $process) { $ready = $true; break }
        Start-Sleep -Milliseconds 300
    }
    Test-Check '主窗口就绪并可切到前台' $ready

    if ($ready) {
        Send-AiPdfKey -Key 'F5'
        $opened = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'presentation open pages=\d+' -TimeoutSeconds 20
        Test-Check 'F5 打开放映窗口' $opened

        if ($opened) {
            Send-AiPdfKey -Key 'Right'
            $stepped = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'presentation page=2' -TimeoutSeconds 10
            Test-Check '右键翻到第 2 页' $stepped

            Send-AiPdfKey -Key 'Escape'
            $closed = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'presentation closed page=2' -TimeoutSeconds 10
            Test-Check 'Esc 退出且停在翻过的那一页' $closed
        }

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
    Write-Host "`n放映验收失败：$($script:failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n放映验收通过" -ForegroundColor Green
exit 0
