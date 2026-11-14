# v0.13.0 界面验收：扫描件（无文本层）自动 OCR 并入索引。
#
# 判据全部来自「用户真能看到的东西」：
#   - 打开扫描件后不需要点任何按钮，OCR 自己跑起来；
#   - 识别出的文字进了本地索引，且标着 source=ocr；
#   - 文字页不会被误做 OCR（不该为正常文档白白消耗 CPU、更不该覆盖文本块）。
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

$exe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'AiPdfEditor.exe'
$fixture = Join-Path $RepositoryRoot 'tests/fixtures/scanned.pdf'
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-ocr-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$indexDir = Join-Path $workDir 'index'
$dataDir = Join-Path $workDir 'data'
$tracePath = Join-Path $workDir 'trace.log'
$textFixture = Join-Path $RepositoryRoot 'build/pdf-fixtures/text.pdf'

$process = $null
try {
    Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $exe) $exe
    Test-Check '扫描件样例存在' (Test-Path -LiteralPath $fixture) $fixture
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $fixture)) {
        throw '缺少可执行文件或样例'
    }

    $env:AIPDF_TRACE_FILE = $tracePath
    $env:AIPDF_INDEX_DIR = $indexDir
    $env:AIPDF_DATA_DIR = $dataDir
    $env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
    Reset-AiPdfTrace -Path $tracePath

    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $fixture + '"') -PassThru
    Test-Check '主程序已启动' ($null -ne $process)

    # 自动触发：打开文档后应当自己开始识别，不需要点菜单。
    # 等「自动识别完成」或「明确失败」；两者都没有就是超时。
    $deadline = (Get-Date).AddSeconds(60)
    $traceText = ''
    while ((Get-Date) -lt $deadline) {
        $traceText = Get-AiPdfTraceText -Path $tracePath
        if ($traceText -match 'ocr applied page=(\d+) chunks=(\d+)') {
            break
        }
        if ($traceText -match 'ocr failed page=(\d+) reason=(.+)') {
            break
        }
        Start-Sleep -Milliseconds 500
    }

    # 本机没有 OCR 引擎（CI 的 Server 版可能没装语言包）时跳过，不算失败：
    # 这条验收要守的是「有引擎时整条链路通」，不是「每台机器都有 OCR」。
    if ($traceText -match 'ocr failed page=\d+ reason=(?<reason>.*(没有可用的系统 OCR|没有找到 tesseract|本机没有可用的 OCR).*)') {
        Write-Host "SKIP  本机没有可用的 OCR 引擎：$($Matches['reason'])" -ForegroundColor Yellow
        exit 0
    }

    Test-Check '打开扫描件后自动开始 OCR' ($traceText -match 'ocr auto request')
    Test-Check 'OCR 完成并写入索引' ($traceText -match 'ocr applied page=\d+ chunks=\d+')

    $chunkCount = 0
    if ($traceText -match 'ocr applied page=\d+ chunks=(\d+)') {
        $chunkCount = [int]$Matches[1]
    }
    Test-Check '识别出了至少一个文本块' ($chunkCount -gt 0) "chunks=$chunkCount"

    # 索引文件里要能找到识别出来的文字，且标着 source=ocr。
    $indexText = ''
    $indexFiles = @()
    if (Test-Path -LiteralPath $indexDir) {
        $indexFiles = Get-ChildItem -LiteralPath $indexDir -Recurse -Filter '*.json' -ErrorAction SilentlyContinue
    }
    foreach ($file in $indexFiles) {
        $indexText += [System.IO.File]::ReadAllText($file.FullName)
    }
    Test-Check '索引里标着 source=ocr' ($indexText -match '"source"\s*:\s*"ocr"')
    # 不拿 'Invoice' 当判据：OCR 常把大写 I 认成小写 l（实测 'lnvoice'）。
    # 数字与 'Total' 稳定得多，中英语言包都能认。
    Test-Check '索引里有识别出的文字（1234 / Total）' ($indexText -match '1234' -or $indexText -match 'Total') $indexText.Substring(0, [Math]::Min(120, $indexText.Length))

    # 文字页不该被 OCR：另开一份有文本层的 PDF，等一会儿确认没有 ocr 记录。
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit(5000)
    }
    Stop-AiPdfOrphanWorker
    Reset-AiPdfTrace -Path $tracePath
    $textProcess = $null
    if (Test-Path -LiteralPath $textFixture) {
        $textProcess = Start-Process -FilePath $exe -ArgumentList ('"' + $textFixture + '"') -PassThru
        Start-Sleep -Seconds 8
        $textTrace = Get-AiPdfTraceText -Path $tracePath
        Test-Check '文字页不会被误做 OCR' (-not ($textTrace -match 'ocr (auto )?request'))
        if ($ScreenshotPath -ne '' -and $textProcess -and -not $textProcess.HasExited) {
            Save-AiPdfWindowScreenshot -Process $textProcess -Path $ScreenshotPath
        }
        if ($textProcess -and -not $textProcess.HasExited) {
            Stop-Process -Id $textProcess.Id -Force
        }
    } else {
        Write-Host 'SKIP  文字页对照（缺少 text.pdf 样例）' -ForegroundColor Yellow
    }

    if ($script:failures -gt 0) {
        Write-Host "OCR 验收失败：$($script:failures) 项未通过" -ForegroundColor Red
        exit 1
    }
    Write-Host 'OCR 验收通过：扫描件打开即自动识别，文字进索引并标记来源，文字页不被误识别' -ForegroundColor Green
    exit 0
} catch {
    Write-Host "OCR 验收异常：$($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    Stop-AiPdfOrphanWorker
    Remove-Item -Recurse -Force $workDir -ErrorAction SilentlyContinue
}
