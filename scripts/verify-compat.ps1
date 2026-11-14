# v0.15.0 兼容性验收：拿一批「不好对付」的 PDF 走真实打开流程。
#
# 每个样本只问三个问题：
#   1. 打开了没有（有文本层的话，第一页文本要能抽出来）；
#   2. 打不开的话，有没有给出可读的原因（不是静默失败、不是崩溃）；
#   3. 进程还活着吗（坏文档不该把应用带走）。
#
# 另外对大文档单独计时：500 页与 600 页的打开耗时纳入预算。
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

# 打开一份文档，等 trace 里出现「open file=…」，返回 (分类, 耗时毫秒, 详情)。
function Test-OpenDocument {
    param(
        [System.Diagnostics.Process]$App,
        [string]$TracePath,
        [int]$TimeoutSeconds = 25
    )

    $started = Get-Date
    $deadline = $started.AddSeconds($TimeoutSeconds)
    $openLine = $null
    while ((Get-Date) -lt $deadline) {
        $trace = Get-AiPdfTraceText -Path $TracePath
        foreach ($line in ($trace -split "`n")) {
            if ($line -match 'open file=.* required=\d msg=') {
                $openLine = $line
                break
            }
        }
        if ($openLine) {
            break
        }
        Start-Sleep -Milliseconds 200
    }

    if (-not $openLine) {
        return @{ Kind = 'timeout'; Detail = '没有等到 open 记录'; ElapsedMs = 0 }
    }
    $elapsedMs = [int]((Get-Date) - $started).TotalMilliseconds

    if ($openLine -match 'required=1') {
        return @{ Kind = 'password'; Detail = ''; ElapsedMs = $elapsedMs }
    }
    if ($openLine -match 'msg=(?<msg>.+)$') {
        $message = $Matches['msg'].Trim()
        if ($message -ne '') {
            return @{ Kind = 'error'; Detail = $message; ElapsedMs = $elapsedMs }
        }
    }

    # 没有错误：等第一页文本抽取完成，确认真的打开了。
    $renderDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $renderDeadline) {
        $trace = Get-AiPdfTraceText -Path $TracePath
        if ($trace -match 'text page=0') {
            return @{ Kind = 'opened'; Detail = ''; ElapsedMs = [int]((Get-Date) - $started).TotalMilliseconds }
        }
        Start-Sleep -Milliseconds 200
    }
    return @{ Kind = 'openedNoText'; Detail = '打开但没有文本页记录'; ElapsedMs = $elapsedMs }
}

$exe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'AiPdfEditor.exe'
$corpusDir = Join-Path $RepositoryRoot 'build/pdf-fixtures'
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-compat-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$tracePath = Join-Path $workDir 'trace.log'

# 样本 → 期望：opened / error / password / any（any 表示两种都算合格）
$corpus = @(
    @{ File = 'normal.pdf';      Expect = 'opened';   Label = '普通文档' },
    @{ File = 'multipage.pdf';   Expect = 'opened';   Label = '多页文档' },
    @{ File = 'rotated.pdf';     Expect = 'opened';   Label = '旋转页面' },
    @{ File = 'empty.pdf';       Expect = 'any';      Label = '空文档' },
    @{ File = 'navigation.pdf';  Expect = 'opened';   Label = '带书签/链接' },
    @{ File = 'corrupt.pdf';     Expect = 'error';    Label = '损坏文件' },
    @{ File = 'truncated.pdf';   Expect = 'error';    Label = '截断文件' },
    @{ File = 'encrypted.pdf';   Expect = 'password'; Label = '加密文档' }
)

$script:exitCode = 1
$process = $null
try {
    Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $exe) $exe
    Test-Check '样本目录存在' (Test-Path -LiteralPath $corpusDir) $corpusDir
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $corpusDir)) {
        throw '缺少可执行文件或样本'
    }

    $env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
    $env:AIPDF_TRACE_FILE = $tracePath

    foreach ($case in $corpus) {
        $path = Join-Path $corpusDir $case.File
        if (-not (Test-Path -LiteralPath $path)) {
            Write-Host "SKIP  $($case.Label)（缺少样本 $($case.File)）" -ForegroundColor Yellow
            continue
        }
        Reset-AiPdfTrace -Path $tracePath
        $process = Start-Process -FilePath $exe -ArgumentList ('"' + $path + '"') -PassThru
        $result = Test-OpenDocument -App $process -TracePath $tracePath
        Start-Sleep -Milliseconds 1500
        $process.Refresh()
        $alive = -not $process.HasExited

        $behaviourOk = switch ($case.Expect) {
            'opened' { $result.Kind -eq 'opened' }
            'error' { $result.Kind -eq 'error' -and $result.Detail.Length -ge 4 }
            'password' { $result.Kind -eq 'password' }
            default { $result.Kind -in @('opened', 'error', 'password') }
        }
        $detail = "$($result.Kind) $($result.Detail)"
        Test-Check "$($case.Label)：行为符合预期" $behaviourOk $detail
        Test-Check "$($case.Label)：进程仍然存活" $alive

        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Milliseconds 500
        Stop-AiPdfOrphanWorker | Out-Null
    }

    # ---- 大文档：打开耗时纳入预算 ----
    $largeBudgetMs = 20000
    foreach ($large in @(@{ File = 'long.pdf'; Label = '500 页'; Budget = $largeBudgetMs },
                         @{ File = 'dense.pdf'; Label = '600 页密集正文'; Budget = 30000 })) {
        $path = Join-Path $corpusDir $large.File
        if (-not (Test-Path -LiteralPath $path)) {
            Write-Host "SKIP  $($large.Label)（缺少样本 $($large.File)）" -ForegroundColor Yellow
            continue
        }
        Reset-AiPdfTrace -Path $tracePath
        $process = Start-Process -FilePath $exe -ArgumentList ('"' + $path + '"') -PassThru
        $result = Test-OpenDocument -App $process -TracePath $tracePath -TimeoutSeconds 60
        Test-Check "$($large.Label)：能打开" ($result.Kind -eq 'opened') "$($result.Kind) $($result.Detail)"
        Write-Host ("      打开耗时 {0} ms（预算 {1} ms）" -f $result.ElapsedMs, $large.Budget)
        Test-Check "$($large.Label)：打开耗时在预算内" ($result.ElapsedMs -gt 0 -and $result.ElapsedMs -lt $large.Budget) "$($result.ElapsedMs) ms"
        if ($ScreenshotPath -ne '' -and $process -and -not $process.HasExited) {
            Save-AiPdfWindowScreenshot -Process $process -Path $ScreenshotPath
        }
        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Milliseconds 500
        Stop-AiPdfOrphanWorker | Out-Null
    }

    if ($script:failures -gt 0) {
        Write-Host "兼容性验收失败：$($script:failures) 项未通过" -ForegroundColor Red
        $script:exitCode = 1
    } else {
        Write-Host '兼容性验收通过：坏文档给出可读错误、加密文档要密码、大文档在预算内打开' -ForegroundColor Green
        $script:exitCode = 0
    }
} catch {
    Write-Host "兼容性验收异常：$($_.Exception.Message)" -ForegroundColor Red
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
[GC]::Collect()
[GC]::WaitForPendingFinalizers()
Remove-Item -Recurse -Force $workDir -ErrorAction SilentlyContinue
exit $script:exitCode
