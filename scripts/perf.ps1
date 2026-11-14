# v0.15.0 性能基线：跑一遍不依赖界面与 PDFium 的纯计算路径，打印实测值。
#
# 只跑 PerfTests 一个可执行文件：
#   - 它内部有预算断言，超了就退出码非 0；
#   - 这里再把 qInfo 打印的实测值整理成一张表，方便和上一次对照。
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [string]$RepositoryRoot = ''
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

$exe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'PerfTests.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "找不到 PerfTests：$exe（先构建 tests 目标）" -ForegroundColor Red
    exit 1
}

$output = & $exe 2>&1
$exitCode = $LASTEXITCODE

Write-Host '性能基线（毫秒，越小越好）：' -ForegroundColor Cyan
foreach ($line in $output) {
    $text = [string]$line
    if ($text -match 'QINFO.*?:\s+(.*?)\s*$') {
        Write-Host ("  " + $Matches[1])
    }
}

$failed = @($output | Where-Object { [string]$_ -match '^FAIL!' })
if ($failed.Count -gt 0) {
    Write-Host '性能基线未达标：' -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}
if ($exitCode -ne 0) {
    Write-Host "PerfTests 退出码 $exitCode" -ForegroundColor Red
    exit $exitCode
}

Write-Host '性能基线通过：所有指标都在预算内' -ForegroundColor Green
exit 0
