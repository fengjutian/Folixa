# 用 qpdf --check 校验一批 PDF 的结构。
#
# 为什么需要单独一道门：PDFium 和 pypdf 之类的实现都很宽容，"能打开"不代表结构正确。
# qpdf 的检查会逐对象走一遍交叉引用、流长度、字典完整性，是发布前最便宜的结构门。
#
# 依赖 tools\qpdf（由 scripts\bootstrap-qpdf.ps1 获取）。没装时直接失败，不静默跳过——
# 一道会自己消失的门比没有门更糟。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\verify-qpdf.ps1 -Path dist\sample.pdf
#   powershell -ExecutionPolicy Bypass -File scripts\verify-qpdf.ps1 -Path .tmp -Recurse
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$QpdfPath = '',
    [switch]$Recurse
)

$ErrorActionPreference = 'Stop'

if (-not $QpdfPath) {
    $QpdfPath = Join-Path $PSScriptRoot '..\tools\qpdf\bin\qpdf.exe'
}

if (-not (Test-Path -LiteralPath $QpdfPath)) {
    Write-Host "找不到 qpdf：$QpdfPath" -ForegroundColor Red
    Write-Host "先运行 scripts\bootstrap-qpdf.ps1" -ForegroundColor Yellow
    exit 2
}

$resolved = (Resolve-Path -LiteralPath $Path).Path
if (Test-Path -LiteralPath $resolved -PathType Container) {
    $files = Get-ChildItem -LiteralPath $resolved -Filter *.pdf -File -Recurse:$Recurse |
        Select-Object -ExpandProperty FullName
} else {
    $files = @($resolved)
}

if ($files.Count -eq 0) {
    Write-Host "没有找到 PDF：$resolved" -ForegroundColor Red
    exit 2
}

$failures = 0
foreach ($file in $files) {
    # 原生程序往 stderr 写东西时，$ErrorActionPreference='Stop' 会把它当异常抛出来；
    # 这一层只关心退出码，所以临时放宽，跑完还原。
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $QpdfPath --check $file 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference

    if ($exitCode -ne 0) {
        $failures++
        Write-Host "FAIL  $file" -ForegroundColor Red
        $output | ForEach-Object { Write-Host "      $_" }
    } else {
        Write-Host "PASS  $(Split-Path -Leaf $file)" -ForegroundColor Green
    }
}

Write-Host ""
if ($failures -gt 0) {
    Write-Host "qpdf 校验失败：$failures / $($files.Count)" -ForegroundColor Red
    exit 1
}
Write-Host "qpdf 校验通过：$($files.Count) 个文件" -ForegroundColor Green
exit 0
