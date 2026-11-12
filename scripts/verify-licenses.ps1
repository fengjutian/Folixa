<#
.SYNOPSIS
    发布门：安装包必须随附第三方许可证全文，且与仓库内的校验清单一致。

.DESCRIPTION
    检查 <PackageDir>\licenses 下的每个许可证文件：
      1. 必须存在；
      2. SHA-256 必须与 licenses\CHECKSUMS.txt 一致。

    顶层三份许可证全文按「规范化文本」校验（行尾统一为 LF 再算哈希，
    见 scripts\LicenseText.ps1）：上游来源行尾不一致，而仓库与安装包
    都按 LF 落地，不归一化就会因检出方式不同而误报。

    另检查 PDFium 及其第三方组件的许可证材料（licenses\pdfium\）：
      3. 包内文件集合必须与 licenses\pdfium-CHECKSUMS.txt 完全一致（不多不少）；
      4. 每份文件的 SHA-256 必须与清单一致；
      5. 包内随附的清单副本必须与仓库内的清单一致。

    任何一项不满足即返回非零退出码。
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir,
    [string]$ManifestPath = '',
    [string]$PdfiumManifestPath = ''
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'LicenseText.ps1')

if (-not $ManifestPath) {
    $ManifestPath = Join-Path $PSScriptRoot '..\licenses\CHECKSUMS.txt'
}
if (-not $PdfiumManifestPath) {
    $PdfiumManifestPath = Join-Path $PSScriptRoot '..\licenses\pdfium-CHECKSUMS.txt'
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
    $actual = Get-AiPdfCanonicalTextHashFromFile -Path $path
    Test-Check "$name 与校验清单一致" ($actual -eq $expected) "期望 $expected 实际 $actual"
}

# --- PDFium 及其第三方组件的许可证材料 ---
if (-not (Test-Path -LiteralPath $PdfiumManifestPath)) {
    Test-Check '存在 PDFium 许可证清单' $false $PdfiumManifestPath
} else {
    $pdfiumDir = Join-Path $licenseDir 'pdfium'
    Test-Check '安装包包含 licenses/pdfium 目录' (Test-Path -LiteralPath $pdfiumDir)

    $packagedManifest = Join-Path $licenseDir 'pdfium-CHECKSUMS.txt'
    Test-Check '安装包包含 PDFium 许可证清单副本' (Test-Path -LiteralPath $packagedManifest)
    if (Test-Path -LiteralPath $packagedManifest) {
        $repoHash = (Get-FileHash -LiteralPath $PdfiumManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $packageHash = (Get-FileHash -LiteralPath $packagedManifest -Algorithm SHA256).Hash.ToLowerInvariant()
        Test-Check '包内 PDFium 清单副本与仓库一致' ($repoHash -eq $packageHash) "仓库 $repoHash 包内 $packageHash"
    }

    $expectedFiles = @{}
    foreach ($line in (Get-Content -LiteralPath $PdfiumManifestPath -Encoding UTF8)) {
        if ($line -notmatch '^([0-9a-f]{64})\s+(\S+)\s+(\S+)$') {
            continue
        }
        $expectedFiles[$matches[2]] = $matches[1]
    }
    Test-Check 'PDFium 许可证清单非空' ($expectedFiles.Count -gt 0) "条目 $($expectedFiles.Count)"

    foreach ($name in ($expectedFiles.Keys | Sort-Object)) {
        $path = Join-Path $pdfiumDir $name
        if (-not (Test-Path -LiteralPath $path)) {
            Test-Check "包含 pdfium/$name" $false '缺失'
            continue
        }
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        Test-Check "pdfium/$name 与清单一致" ($actual -eq $expectedFiles[$name]) "期望 $($expectedFiles[$name]) 实际 $actual"
    }

    # 包内不得有清单之外的多余文件（漏进来一份未登记的许可证同样是不合规）
    if (Test-Path -LiteralPath $pdfiumDir) {
        foreach ($file in (Get-ChildItem -LiteralPath $pdfiumDir -File)) {
            Test-Check "pdfium/$($file.Name) 在清单内" ($expectedFiles.ContainsKey($file.Name)) '清单中无此文件'
        }
    }
}

if ($script:Failures -gt 0) {
    Write-Host "`n许可证校验失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n许可证校验通过：安装包随附的许可证全文与校验清单一致" -ForegroundColor Green
exit 0
