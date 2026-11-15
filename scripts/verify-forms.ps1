# v1.5.0 界面验收：表单面板（字段列表 / 填写 / 扁平化 / 数据导入导出）。
#
# 判据：
#   - F8 打开表单面板，trace 里出现 `panel forms items=4`（固定装置 4 个字段）；
#   - 面板上的写入、扁平化、CSV/FDF 导入导出按钮都能按 AutomationId 找到。
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
$fixture = Join-Path $RepositoryRoot 'build/pdf-fixtures/form.pdf'
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-form-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$tracePath = Join-Path $workDir 'trace.log'

$process = $null
try {
    Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $exe) $exe
    Test-Check '表单样例存在' (Test-Path -LiteralPath $fixture) $fixture
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
        Send-AiPdfKey -Key 'F8'
        $listed = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'panel forms items=4' -TimeoutSeconds 15
        Test-Check 'F8 打开表单面板并列出 4 个字段' $listed

        foreach ($entry in ([ordered]@{
                    '写入按钮'   = '*formApply*'
                    '扁平化按钮' = '*formFlatten*'
                    '导出 CSV'   = '*formExportCsv*'
                    '导入 CSV'   = '*formImportCsv*'
                    '导出 FDF'   = '*formExportFdf*'
                    '导入 FDF'   = '*formImportFdf*'
                    '字段表格'   = '*formTable*'
                }).GetEnumerator()) {
            $element = Wait-AiPdfElementById -Process $process -IdPattern $entry.Value -TimeoutSeconds 10
            Test-Check ("表单面板有「" + $entry.Key + "」") ($null -ne $element)
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
    Set-AiPdfWindowTopmost -Process $process -Enable $false -ErrorAction SilentlyContinue | Out-Null
    Stop-AiPdfOrphanWorker -ErrorAction SilentlyContinue
    if ($null -ne $cursor) { Restore-AiPdfCursorPosition -Position $cursor }
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures -gt 0) {
    Write-Host "`n表单面板验收失败：$($script:failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n表单面板验收通过" -ForegroundColor Green
exit 0
