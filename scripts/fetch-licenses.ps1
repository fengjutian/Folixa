<#
.SYNOPSIS
    获取并校验第三方许可证全文。

.DESCRIPTION
    默认从权威来源重新下载许可证全文并按 licenses\CHECKSUMS.txt 校验；
    -VerifyOnly 只校验本地已有文件，不访问网络。

    取件与校验都按「规范化文本」处理（行尾统一为 LF 再算 SHA-256，
    见 scripts\LicenseText.ps1）：上游来源行尾不一致（gnu.org 是 LF，
    MinGW 工具包是 CRLF），而仓库按 .gitattributes 以 LF 存对象库，
    不归一化的话本地工作区与 CI runner 必然算出两个不同的哈希。

    下载失败或校验不通过都会返回非零退出码。
#>
param(
    [string]$LicenseDir = (Join-Path $PSScriptRoot '..\licenses'),
    [string]$MinGWRoot = '',
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'LicenseText.ps1')

if (-not $MinGWRoot) {
    $MinGWRoot = if ($env:AIPDF_MINGW_ROOT) { $env:AIPDF_MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }
}

$dir = (Resolve-Path -LiteralPath $LicenseDir).Path
$checksumsPath = Join-Path $dir 'CHECKSUMS.txt'
if (-not (Test-Path -LiteralPath $checksumsPath)) {
    Write-Host "缺少校验清单: $checksumsPath" -ForegroundColor Red
    exit 2
}

$expected = @{}
foreach ($line in (Get-Content -LiteralPath $checksumsPath -Encoding UTF8)) {
    if ($line -match '^([0-9a-f]{64})\s+(\S+)$') {
        $expected[$matches[2]] = $matches[1]
    }
}
if ($expected.Count -eq 0) {
    Write-Host '校验清单为空或格式不正确' -ForegroundColor Red
    exit 2
}

$sources = @{
    'LGPL-3.0.txt' = 'https://www.gnu.org/licenses/lgpl-3.0.txt'
    'GPL-3.0.txt' = 'https://www.gnu.org/licenses/gpl-3.0.txt'
}
$localSources = @{
    'GCC-RUNTIME-LIBRARY-EXCEPTION.txt' = (Join-Path $MinGWRoot 'licenses\gcc\COPYING.RUNTIME')
}

if (-not $VerifyOnly) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    foreach ($name in $sources.Keys) {
        $target = Join-Path $dir $name
        Write-Host "下载 $name <- $($sources[$name])" -ForegroundColor Cyan
        try {
            Invoke-WebRequest -Uri $sources[$name] -OutFile $target -UseBasicParsing -TimeoutSec 120
        } catch {
            Write-Host "下载失败: $($_.Exception.Message)" -ForegroundColor Red
            exit 1
        }
        # 落地即归一化：仓库按 .gitattributes 以 LF 存对象库，
        # 写回 CRLF 会让工作区文件与仓库内容不一致。
        Write-AiPdfCanonicalTextFile -Path $target -Bytes ([System.IO.File]::ReadAllBytes($target))
    }
    foreach ($name in $localSources.Keys) {
        $origin = $localSources[$name]
        if (-not (Test-Path -LiteralPath $origin)) {
            Write-Host "找不到本地许可证来源: $origin" -ForegroundColor Red
            exit 1
        }
        # 直接写规范化后的内容，而不是原样复制 CRLF 原件。
        Write-AiPdfCanonicalTextFile -Path (Join-Path $dir $name) -Bytes ([System.IO.File]::ReadAllBytes($origin))
    }
}

$failures = 0
foreach ($name in $expected.Keys) {
    $path = Join-Path $dir $name
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "FAIL  缺少 $name" -ForegroundColor Red
        $failures++
        continue
    }
    $actual = Get-AiPdfCanonicalTextHashFromFile -Path $path
    if ($actual -eq $expected[$name]) {
        Write-Host "PASS  $name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $name 期望 $($expected[$name]) 实际 $actual" -ForegroundColor Red
        $failures++
    }
}

if ($failures -gt 0) {
    Write-Host "`n许可证校验失败：$failures 个文件不匹配" -ForegroundColor Red
    exit 1
}
Write-Host "`n许可证全文校验通过：$($expected.Count) 个文件" -ForegroundColor Green
exit 0
