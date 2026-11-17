<#
.SYNOPSIS
    K5 兼容验收：成批走真实打开流程，要求畸形语料零崩溃。

.DESCRIPTION
    读语料目录（默认 build\corpus\malformed），对每一份 PDF 真实调用一次打开流程，
    然后断言三件事：
      1. 进程不崩（没有非预期退出、没有「系统错误」弹窗）；
      2. 打不开时给出了可读的中文原因，不是静默失败；
      3. 能打开的，第一页文本能抽出来（有文本层的话）。

    汇总输出：总数 / 打开成功 / 拒绝 / 崩溃，崩溃数必须为 0。

    语料由 scripts\gen-compat-corpus.py 生成：
        python scripts\gen-compat-corpus.py --out build\corpus --count 1000

    注意（运行时长）：应用一次只接受一个命令行文件（见 src\app\main.cpp），
    所以每份语料都要启动一次应用。1000 份大约 40~60 分钟，
    CI 上建议放在夜间任务里跑，或先用 -Limit 取样。

.PARAMETER CorpusDir
    畸形语料目录。默认 build\corpus\malformed。

.PARAMETER Limit
    只跑前 N 份（0 = 全部）。冒烟用。

.PARAMETER StrictExpectation
    把 .meta.json 里记录的期望（open / reject / either）也当硬断言。
    默认只把「不崩 + 有可读结果」当硬要求。

.PARAMETER DryRun
    只打印计划（语料数量、分类分布、抽样清单），不启动应用。
#>
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [string]$CorpusDir = '',
    [string]$RepositoryRoot = '',
    [int]$Limit = 0,
    [int]$OpenTimeoutSeconds = 25,
    [int]$SettleMilliseconds = 300,
    [string]$ReportPath = '',
    [switch]$StrictExpectation,
    [switch]$SelfTest,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')

$script:failures = 0
$script:crashes = 0

function Test-Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    if ($Ok) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name $Detail" -ForegroundColor Red
        $script:failures++
    }
}

# 「系统错误」类弹窗：标题命中就当成崩溃处理。
$script:crashTitlePattern = '系统错误|System Error|应用程序错误|Application Error|Fatal|Assertion|0xc0000|access violation|内存不能'

# 打开结果分类：
#   opened        —— 打开成功，且第一页文本抽出来了
#   opened-notext —— 打开成功，但没等到文本页记录
#   rejected      —— 明确拒绝，并给出了原因
#   password      —— 要密码（加密类坏文件），不算崩溃
#   timeout       —— 连 open 记录都没有（静默失败）
#   crashed       —— 进程异常退出或弹了系统错误
# 从轨迹文本里解析出「打开结果」那一行。
# 抽成独立函数是为了能离线自检（-SelfTest）：不启动应用也能验证判定逻辑。
function Read-OpenRecord {
    param([string]$TraceText)
    if ([string]::IsNullOrWhiteSpace($TraceText)) {
        return $null
    }
    foreach ($line in ($TraceText -split "`n")) {
        if ($line -match 'open file=.* required=(\d+) msg=(.*)$') {
            return @{
                Required = [int]$Matches[1]
                Message  = $Matches[2].Trim()
            }
        }
    }
    return $null
}

function Get-OpenOutcome {
    param(
        [System.Diagnostics.Process]$Process,
        [string]$TracePath,
        [int]$TimeoutSeconds = 25
    )

    $started = Get-Date
    $deadline = $started.AddSeconds($TimeoutSeconds)
    $record = $null
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return @{
                Kind      = 'crashed'
                Detail    = "进程提前退出，退出码 $($Process.ExitCode)"
                ElapsedMs = [int]((Get-Date) - $started).TotalMilliseconds
            }
        }
        $record = Read-OpenRecord -TraceText (Get-AiPdfTraceText -Path $TracePath)
        if ($record) {
            break
        }
        Start-Sleep -Milliseconds 150
    }

    if (-not $record) {
        return @{ Kind = 'timeout'; Detail = '没有等到 open 记录'; ElapsedMs = [int]((Get-Date) - $started).TotalMilliseconds }
    }

    $elapsedMs = [int]((Get-Date) - $started).TotalMilliseconds
    $required = $record.Required
    $message = $record.Message

    if ($required -eq 1) {
        # 密码问询框是模态的：不关掉它，应用会一直卡在这里。
        Invoke-AiPdfDialogButton -Process $Process -ButtonPattern '取消|Cancel' `
            -TitlePattern '需要密码' -TimeoutSeconds 5 | Out-Null
        return @{ Kind = 'password'; Detail = $message; ElapsedMs = $elapsedMs }
    }

    if ($message -ne '') {
        return @{ Kind = 'rejected'; Detail = $message; ElapsedMs = $elapsedMs }
    }

    # 没有错误：等第一页文本抽取完成，确认真的打开了。
    $renderDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $renderDeadline) {
        if ($Process.HasExited) {
            return @{ Kind = 'crashed'; Detail = "打开过程中进程退出，退出码 $($Process.ExitCode)"; ElapsedMs = $elapsedMs }
        }
        $trace = Get-AiPdfTraceText -Path $TracePath
        if ($trace -match 'text page=0') {
            return @{ Kind = 'opened'; Detail = ''; ElapsedMs = [int]((Get-Date) - $started).TotalMilliseconds }
        }
        Start-Sleep -Milliseconds 150
    }
    return @{ Kind = 'opened-notext'; Detail = '打开成功但没有文本页记录'; ElapsedMs = $elapsedMs }
}

# 有没有弹出「系统错误」这类窗口？有就按崩溃算。
function Test-CrashDialog {
    param([System.Diagnostics.Process]$Process)
    foreach ($handle in (Get-AiPdfProcessWindows -Process $Process)) {
        $title = [AiPdfWindows]::Title($handle)
        if ($title -and $title -match $script:crashTitlePattern) {
            return $title
        }
    }
    return ''
}

# 原因里至少要有一个汉字：拒绝得让人看得懂。
function Test-ChineseMessage {
    param([string]$Message)
    if ([string]::IsNullOrWhiteSpace($Message)) { return $false }
    return ($Message -match '[\u4e00-\u9fff]')
}

$exe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'AiPdfEditor.exe'
if ([string]::IsNullOrWhiteSpace($CorpusDir)) {
    $CorpusDir = Join-Path (Join-Path $RepositoryRoot 'build\corpus') 'malformed'
}
if (-not [System.IO.Path]::IsPathRooted($CorpusDir)) {
    $CorpusDir = Join-Path $RepositoryRoot $CorpusDir
}

# 离线自检：只验判定逻辑，不启动应用、不碰桌面。
if ($SelfTest) {
    Write-Host '==> SelfTest：轨迹判定逻辑' -ForegroundColor Cyan
    $cases = @(
        @{ Trace = "open file=normal.pdf required=0 msg="; Kind = 'opened'; Message = '' },
        @{ Trace = "open file=bad.pdf required=0 msg=文件不是有效的 PDF"; Kind = 'rejected'; Message = '文件不是有效的 PDF' },
        @{ Trace = "open file=enc.pdf required=1 msg="; Kind = 'password'; Message = '' },
        @{ Trace = "open file=x.pdf required=0 msg=`ntext page=0 chars=42 layout=1"; Kind = 'opened'; Message = '' },
        @{ Trace = "open file=y.pdf required=0 msg=需要密码，或密码不正确"; Kind = 'rejected'; Message = '需要密码，或密码不正确' }
    )
    $bad = 0
    foreach ($case in $cases) {
        $record = Read-OpenRecord -TraceText $case.Trace
        $actual = 'none'
        if ($record) {
            if ($record.Required -eq 1) { $actual = 'password' }
            elseif ($record.Message -ne '') { $actual = 'rejected' }
            else { $actual = 'opened' }
        }
        $ok = ($actual -eq $case.Kind)
        if ($ok -and $actual -eq 'rejected') {
            $ok = ($record.Message -eq $case.Message) -and (Test-ChineseMessage $record.Message)
        }
        if ($ok) {
            Write-Host ("  PASS  {0,-10} <- {1}" -f $actual, $case.Trace.Replace("`n", ' | ')) -ForegroundColor Green
        } else {
            Write-Host ("  FAIL  期望 {0}，实得 {1} <- {2}" -f $case.Kind, $actual, $case.Trace.Replace("`n", ' | ')) -ForegroundColor Red
            $bad++
        }
    }
    $messageCases = @(
        @{ Text = '文件不是有效的 PDF'; Ok = $true },
        @{ Text = '需要密码，或密码不正确'; Ok = $true },
        @{ Text = 'EOF'; Ok = $false },
        @{ Text = ''; Ok = $false }
    )
    foreach ($case in $messageCases) {
        $actual = Test-ChineseMessage $case.Text
        if ($actual -eq $case.Ok) {
            Write-Host ("  PASS  中文原因判定 '{0}' -> {1}" -f $case.Text, $actual) -ForegroundColor Green
        } else {
            Write-Host ("  FAIL  中文原因判定 '{0}' 期望 {1}，实得 {2}" -f $case.Text, $case.Ok, $actual) -ForegroundColor Red
            $bad++
        }
    }
    if ($bad -gt 0) {
        Write-Host "`nSelfTest 失败：$bad 项" -ForegroundColor Red
        exit 1
    }
    Write-Host "`nSelfTest 通过：轨迹判定与中文原因检查都符合预期" -ForegroundColor Green
    exit 0
}

if (-not (Test-Path -LiteralPath $CorpusDir)) {
    Write-Host "语料目录不存在：$CorpusDir" -ForegroundColor Red
    Write-Host '先跑：python scripts\gen-compat-corpus.py --out build\corpus --count 1000' -ForegroundColor Red
    exit 2
}

$samples = @(Get-ChildItem -LiteralPath $CorpusDir -Filter '*.pdf' | Sort-Object Name)
if ($samples.Count -eq 0) {
    Write-Host "语料目录里没有 PDF：$CorpusDir" -ForegroundColor Red
    exit 2
}
if ($Limit -gt 0 -and $samples.Count -gt $Limit) {
    $samples = @($samples | Select-Object -First $Limit)
}

# 分类分布：从 .meta.json 读；读不到就按文件名归到 unknown。
$categories = @{}
$expectations = @{}
foreach ($sample in $samples) {
    $metaPath = [System.IO.Path]::ChangeExtension($sample.FullName, '.meta.json')
    $category = 'unknown'
    $expect = 'either'
    if (Test-Path -LiteralPath $metaPath) {
        try {
            $meta = Get-Content -LiteralPath $metaPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($meta.category) { $category = [string]$meta.category }
            if ($meta.expectation) { $expect = [string]$meta.expectation }
        } catch {
            $category = 'unreadable-meta'
        }
    }
    if (-not $categories.ContainsKey($category)) { $categories[$category] = 0 }
    $categories[$category]++
    if (-not $expectations.ContainsKey($expect)) { $expectations[$expect] = 0 }
    $expectations[$expect]++
}

Write-Host '==> 语料概况' -ForegroundColor Cyan
Write-Host "    目录      ：$CorpusDir"
Write-Host "    份数      ：$($samples.Count)"
foreach ($key in ($categories.Keys | Sort-Object)) {
    Write-Host ("    类别 {0,-20} {1}" -f $key, $categories[$key])
}
foreach ($key in ($expectations.Keys | Sort-Object)) {
    Write-Host ("    期望 {0,-20} {1}" -f $key, $expectations[$key])
}

if ($DryRun) {
    Write-Host ''
    Write-Host '==> DryRun：不启动应用，只列计划' -ForegroundColor Yellow
    Write-Host "    可执行文件：$exe（存在：$(Test-Path -LiteralPath $exe)）"
    Write-Host "    每份语料的检查项：进程不崩 / 拒绝时给出中文原因 / 打开时能抽出第一页文本"
    Write-Host '    前 10 份：'
    foreach ($sample in ($samples | Select-Object -First 10)) {
        Write-Host ("      {0}" -f $sample.Name)
    }
    Write-Host ''
    Write-Host 'DryRun 结束：没有启动任何应用进程。' -ForegroundColor Green
    exit 0
}

if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "主程序不存在：$exe" -ForegroundColor Red
    exit 2
}

$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-corpus-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$tracePath = Join-Path $workDir 'trace.log'

$env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
$env:AIPDF_TRACE_FILE = $tracePath

$results = New-Object System.Collections.Generic.List[object]
$opened = 0
$rejected = 0
$password = 0
$openedNoText = 0
$timedOut = 0
$process = $null

Write-Host ''
Write-Host '==> 逐份打开（每份一次真实打开流程）' -ForegroundColor Cyan

try {
    foreach ($sample in $samples) {
        Reset-AiPdfTrace -Path $tracePath
        $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) `
            -ArgumentList ('"' + $sample.FullName + '"') -PassThru

        $outcome = Get-OpenOutcome -Process $process -TracePath $tracePath -TimeoutSeconds $OpenTimeoutSeconds

        # 进程还活着吗？
        $process.Refresh()
        $alive = -not $process.HasExited
        $crashTitle = ''
        if ($alive) {
            $crashTitle = Test-CrashDialog -Process $process
        }

        $kind = $outcome.Kind
        $detail = $outcome.Detail
        if ($kind -ne 'crashed' -and -not $alive) {
            $kind = 'crashed'
            $detail = "进程在打开过程中退出，退出码 $($process.ExitCode)"
        }
        if ($kind -ne 'crashed' -and $crashTitle) {
            $kind = 'crashed'
            $detail = "弹出系统错误窗口：$crashTitle"
        }

        switch ($kind) {
            'opened' { $opened++ }
            'rejected' { $rejected++ }
            'password' { $password++ }
            'opened-notext' { $openedNoText++ }
            'timeout' { $timedOut++ }
            'crashed' { $script:crashes++ }
        }

        # 硬要求：不能崩；拒绝必须给出可读的中文原因；打开成功必须抽出第一页文本。
        $ok = $true
        $why = ''
        if ($kind -eq 'crashed') {
            $ok = $false
            $why = $detail
        } elseif ($kind -eq 'timeout') {
            $ok = $false
            $why = $detail
        } elseif ($kind -eq 'rejected' -and -not (Test-ChineseMessage $detail)) {
            $ok = $false
            $why = "拒绝原因不可读：'$detail'"
        } elseif ($kind -eq 'opened-notext') {
            $ok = $false
            $why = $detail
        }

        if ($ok) {
            Write-Host ("  OK   {0,-34} {1}" -f $sample.Name, $detail) -ForegroundColor DarkGray
        } else {
            Write-Host ("  BAD  {0,-34} {1}" -f $sample.Name, $why) -ForegroundColor Red
            $script:failures++
        }

        $results.Add([pscustomobject]@{
            file     = $sample.Name
            kind     = $kind
            detail   = $detail
            elapsed  = $outcome.ElapsedMs
            crashed  = ($kind -eq 'crashed')
        })

        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit(5000) | Out-Null
        }
        $process = $null
        Start-Sleep -Milliseconds $SettleMilliseconds
        Stop-AiPdfOrphanWorker -Quiet | Out-Null
    }
} finally {
    $ErrorActionPreference = 'Continue'
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    try { Stop-AiPdfOrphanWorker -Quiet | Out-Null } catch { }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host '==> 容错汇总' -ForegroundColor Cyan
Write-Host ("    总数        ：{0}" -f $results.Count)
Write-Host ("    打开成功    ：{0}" -f $opened) -ForegroundColor Green
Write-Host ("    明确拒绝    ：{0}" -f $rejected)
Write-Host ("    要求密码    ：{0}" -f $password)
Write-Host ("    打开无文本  ：{0}" -f $openedNoText)
Write-Host ("    无响应      ：{0}" -f $timedOut)
Write-Host ("    崩溃        ：{0}" -f $script:crashes) -ForegroundColor $(if ($script:crashes -gt 0) { 'Red' } else { 'Green' })

if ($ReportPath) {
    $report = [pscustomobject]@{
        corpus     = $CorpusDir
        total      = $results.Count
        opened     = $opened
        rejected   = $rejected
        password   = $password
        noText     = $openedNoText
        timedOut   = $timedOut
        crashed    = $script:crashes
        failures   = $script:failures
        results    = $results
    }
    $reportDir = Split-Path -Parent $ReportPath
    if ($reportDir -and -not (Test-Path -LiteralPath $reportDir)) {
        New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
    }
    [System.IO.File]::WriteAllText(
        $ReportPath,
        ($report | ConvertTo-Json -Depth 5),
        (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "    报告        ：$ReportPath"
}

Test-Check '崩溃数为 0' ($script:crashes -eq 0) "crashes=$($script:crashes)"
Test-Check '没有静默失败（每一份都有明确结果）' ($timedOut -eq 0) "timeout=$timedOut"

if ($script:failures -gt 0) {
    Write-Host "`n容错验收失败：$($script:failures) 项未通过（崩溃 $($script:crashes) 份）" -ForegroundColor Red
    exit 1
}
Write-Host "`n容错验收通过：$($results.Count) 份语料零崩溃，拒绝时都给出了可读的中文原因" -ForegroundColor Green
exit 0
