# v1.11.0 界面验收：E 域的编辑动作能不能真的触发。
#
# 菜单栏是隐藏的，UIA 点不到 QAction，所以这些动作都有独立快捷键：
#   Ctrl+Shift+W 水印 / Ctrl+Shift+H 页眉页脚 / Ctrl+Shift+N 页码
#   Ctrl+Shift+M 合并 / Ctrl+Shift+E 抽取当前页
#
# 判据：页码是立即生效的（trace 里看得到）；水印/页眉/合并/抽取会先弹对话框，
# 断言「对话框真的出现了」，然后 Esc 关掉，不落盘任何文件。
#
# 会抢前台，只应在 CI 或空闲机器上跑。
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
$fixture = Join-Path $RepositoryRoot 'build/pdf-fixtures/multipage.pdf'
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-edit-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$tracePath = Join-Path $workDir 'trace.log'

$process = $null
try {
    Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $exe) $exe
    Test-Check '多页样例存在' (Test-Path -LiteralPath $fixture) $fixture
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

    Get-Process -Name AiPdfEditor -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500

    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $fixture + '"') -PassThru
    Test-Check '主程序已启动' ($null -ne $process)

    $started = Get-Date
    $deadline = (Get-Date).AddSeconds(90)
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        if ($process.HasExited) { break }
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            Set-AiPdfWindowTopmost -Process $process -Enable $true -ErrorAction SilentlyContinue | Out-Null
            if (Test-AiPdfForeground -Process $process) { $ready = $true; break }
            if (((Get-Date) - $started).TotalSeconds -gt 6) {
                $bounds = Get-AiPdfWindowRect -Process $process
                if ($null -ne $bounds) {
                    Invoke-AiPdfMouseClick -X ($bounds.Left + 400) -Y ($bounds.Top + 8)
                }
            }
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '主窗口就绪并可切到前台' $ready

    if ($ready) {
        # 页码是立即生效的：不弹对话框，直接看 trace。
        Send-AiPdfKey -Key 'N' -Ctrl -Shift
        Test-Check 'Ctrl+Shift+N 添加页码' (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'decor page numbers applied' -TimeoutSeconds 10)

        # 其余动作会先弹对话框：断言对话框出现，然后 Esc 取消。
        # 对话框是模态的：断言 trace 里出现"打开对话框"标记，然后 Esc 取消。
        foreach ($entry in ([ordered]@{
                    'Ctrl+Shift+W 打开水印对话框'     = @('W', 'decor watermark dialog')
                    'Ctrl+Shift+H 打开页眉页脚对话框' = @('H', 'decor header footer dialog')
                    'Ctrl+Shift+M 打开合并对话框'     = @('M', 'document merge dialog')
                    'Ctrl+Shift+E 打开抽取对话框'     = @('E', 'page extract dialog')
                }).GetEnumerator()) {
            $key = $entry.Value[0]
            $pattern = $entry.Value[1]
            Send-AiPdfKey -Key $key -Ctrl -Shift
            Test-Check $entry.Key (Wait-AiPdfTraceMatch -Path $tracePath -Pattern $pattern -TimeoutSeconds 8)
            Send-AiPdfKey -Key 'Escape'
            Start-Sleep -Milliseconds 500
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
    if ($null -ne $process) {
        Set-AiPdfWindowTopmost -Process $process -Enable $false -ErrorAction SilentlyContinue | Out-Null
    }
    Stop-AiPdfOrphanWorker -ErrorAction SilentlyContinue
    if ($null -ne $cursor) { Restore-AiPdfCursorPosition -Position $cursor }
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures -gt 0) {
    Write-Host "`n编辑动作验收失败：$($script:failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n编辑动作验收通过" -ForegroundColor Green
exit 0
