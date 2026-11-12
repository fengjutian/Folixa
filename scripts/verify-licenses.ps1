<#
.SYNOPSIS
    发布门：安装包必须随附第三方许可证全文，且与仓库内的校验清单一致。

.DESCRIPTION
    检查 <PackageDir>\licenses 下的每个许可证文件：
      1. 必须存在；
      2. SHA-256 必须与 licenses\CHECKSUMS.txt 一致。

    任何一项不满足即返回非零退出码。
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir,
    [string]$ManifestPath = ''
)

$ErrorActionPreference = 'Stop'

if (-not $ManifestPath) {
    $ManifestPath = Join-Path $PSScriptRoot '..\licenses\CHECKSUMS.txt'
}

$script:Failures = 0

function Test-Check {
    param(
        [string]$Name,
        [bool]$Condition,
        [string]$Detail = ''
    )
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

if (-not (Test-Path -LiteralPath $ManifestPath)) {
    Write-Host "缺少校验清单: $ManifestPath" -ForegroundColor Red
    exit 2
}
if (-not (Test-Path -LiteralPath $PackageDir)) {
    Write-Host "缺少包目录: $PackageDir" -ForegroundColor Red
    exit 2
}

$licenseDir = Join-Path $PackageDir 'licenses'
Test-Check '安装包包含 licenses 目录' (Test-Path -LiteralPath $licenseDir)
Test-Check '安装包包含第三方清单' (
    Test-Path -LiteralPath (Join-Path $licenseDir 'THIRD_PARTY_LICENSES.md'))

foreach ($line in (Get-Content -LiteralPath $ManifestPath -Encoding UTF8)) {
    if ($line -notmatch '^([0-9a-f]{64})\s+(\S+)$') {
        continue
    }
    $expected = $matches[1]
    $name = $matches[2]
    $path = Join-Path $licenseDir $name
    if (-not (Test-Path -LiteralPath $path)) {
        Test-Check "包含 $name" $false '缺失'
        continue
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    Test-Check "$name 与校验清单一致" ($actual -eq $expected) "期望 $expected 实际 $actual"
}

if ($script:Failures -gt 0) {
    Write-Host "`n许可证校验失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n许可证校验通过：安装包随附的许可证全文与校验清单一致" -ForegroundColor Green
exit 0
