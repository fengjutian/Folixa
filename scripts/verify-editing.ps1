# v1.11.0 界面验收：E 域的编辑动作能不能真的触发。
#
# 为什么不模拟按键：按键受前台切换与焦点影响，实测同一个快捷键时而生效时而不生效，
# 这样的验收只会制造"假红"。改成应用自带的诊断入口：
#
#     AiPdfEditor.exe <pdf> --diagnostic-action=<name>
#
# 窗口起来后触发对应动作（模态对话框会自动关掉），然后按触发成败返回 0 / 3。
# 判据 = 退出码 0 + trace 里出现该动作的标记。
#
# 覆盖：pageNumbers（立即生效）、watermark、headerFooter、merge、extract（都会弹对话框）。
param(
    [string]$BuildDir = 'build/windows-debug/bin',
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

    Get-Process -Name AiPdfEditor -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500

    # 每个动作独立跑一次进程：互不影响，也不受"上一个对话框没关干净"的干扰。
    $cases = [ordered]@{
        '页码立即生效'     = @('pageNumbers', 'decor page numbers applied')
        '水印动作触发'     = @('watermark', 'decor watermark dialog')
        '页眉页脚动作触发' = @('headerFooter', 'decor header footer dialog')
        '合并动作触发'     = @('merge', 'document merge dialog')
        '抽取动作触发'     = @('extract', 'page extract dialog')
    }

    foreach ($entry in $cases.GetEnumerator()) {
        $action = $entry.Value[0]
        $pattern = $entry.Value[1]
        $tracePath = Join-Path $workDir ('trace-' + $action + '.log')
        $env:AIPDF_TRACE_FILE = $tracePath
        $env:AIPDF_DATA_DIR = (Join-Path $workDir 'data')
        $env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
        Reset-AiPdfTrace -Path $tracePath

        $process = Start-Process -FilePath $exe -PassThru -ArgumentList @(
            ('"' + $fixture + '"'),
            ('--diagnostic-action=' + $action)
        )
        $finished = $process.WaitForExit(30000)
        if (-not $finished) {
            $process.Kill()
            $process.WaitForExit(5000) | Out-Null
        }
        $exitCode = if ($finished) { $process.ExitCode } else { -1 }
        $traced = ((Get-AiPdfTraceText -Path $tracePath) -join "`n").Contains($pattern)
        Test-Check $entry.Key ($exitCode -eq 0 -and $traced) "exit=$exitCode traced=$traced"
    }
} finally {
    Get-Process -Name AiPdfEditor -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Stop-AiPdfOrphanWorker -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures -gt 0) {
    Write-Host "`n编辑动作验收失败：$($script:failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n编辑动作验收通过" -ForegroundColor Green
exit 0
