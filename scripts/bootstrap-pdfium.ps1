<#
.SYNOPSIS
    获取并校验固定版本的 PDFium 预编译包。

.DESCRIPTION
    PDFium 不在 Qt 套件里，Chromium 官方也不发布 Windows 预编译包，
    这里固定使用 bblanchon/pdfium-binaries 的发布产物（chromium/8152，
    PDFium API 159），下载后按 SHA-256 校验并解压到 tools\pdfium。

    该 DLL 静态链接 CRT，不依赖 MSVC 运行时，可直接被 MinGW 构建的程序调用。
#>
param(
    [string]$InstallDir = (Join-Path $PSScriptRoot '..\tools\pdfium'),
    [string]$Version = 'chromium/8152',
    [string]$Asset = 'pdfium-win-x64.tgz',
    [string]$Sha256 = 'cee2b0e9990bc67c0721774932961b9dbf2766046c10eec925acd4cfba6fdfe3',
    [string]$ExpectedApiVersion = 'MAJOR=159',
    [switch]$RegenerateLicenseManifest
)

$ErrorActionPreference = 'Stop'

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$dll = Join-Path $InstallDir 'bin\pdfium.dll'
if (Test-Path -LiteralPath $dll) {
    Write-Host "PDFium 已存在于 $InstallDir" -ForegroundColor Green
} else {
    $encodedVersion = $Version -replace '/', '%2F'
    $url = "https://github.com/bblanchon/pdfium-binaries/releases/download/$encodedVersion/$Asset"
    $archive = Join-Path $InstallDir $Asset

    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Write-Host "下载 PDFium $Version <- $url" -ForegroundColor Cyan
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing -TimeoutSec 600

    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Sha256) {
        Write-Host "SHA-256 不匹配：期望 $Sha256 实际 $actual" -ForegroundColor Red
        exit 1
    }
    Write-Host "校验通过：$actual" -ForegroundColor Green

    Write-Host '解压' -ForegroundColor Cyan
    tar -xzf $archive -C $InstallDir
    if ($LASTEXITCODE -ne 0) {
        Write-Host "解压失败（退出码 $LASTEXITCODE）" -ForegroundColor Red
        exit 1
    }
}

foreach ($required in @('bin\pdfium.dll', 'lib\pdfium.dll.lib', 'include\fpdfview.h', 'VERSION')) {
    $path = Join-Path $InstallDir $required
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "PDFium 包不完整：缺少 $required" -ForegroundColor Red
        exit 1
    }
}

$versionText = (Get-Content -LiteralPath (Join-Path $InstallDir 'VERSION') -Raw).Trim()
if ($ExpectedApiVersion -and $versionText -notmatch [regex]::Escape($ExpectedApiVersion)) {
    Write-Host "PDFium API 版本不符：期望包含 $ExpectedApiVersion，实际 $versionText" -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host '==> 核对许可证材料清单' -ForegroundColor Cyan
# 仓库内的 licenses\pdfium-CHECKSUMS.txt 是许可证分发的锚点：
# 打包与发布门都按它逐份核对，所以清单必须与固定版本 PDFium 的实际许可证材料一致。
$repoRoot = Join-Path $PSScriptRoot '..'
$manifestPath = Join-Path $repoRoot 'licenses\pdfium-CHECKSUMS.txt'
$licenseDir = Join-Path $InstallDir 'licenses'
$licenseSource = Join-Path $InstallDir 'LICENSE'

$manifestEntries = [ordered]@{}
foreach ($file in (Get-ChildItem -LiteralPath $licenseDir -File | Sort-Object Name)) {
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $relative = "tools/pdfium/licenses/$($file.Name)"
    $manifestEntries[$file.Name] = @{
        Hash = $hash
        Source = $relative
        Target = $file.Name
    }
}
if (Test-Path -LiteralPath $licenseSource) {
    $hash = (Get-FileHash -LiteralPath $licenseSource -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifestEntries['pdfium-BSD-3-Clause.txt'] = @{
        Hash = $hash
        Source = 'tools/pdfium/LICENSE'
        Target = 'pdfium-BSD-3-Clause.txt'
    }
}

$manifestLines = New-Object System.Collections.Generic.List[string]
$manifestLines.Add('# PDFium 及其第三方组件的许可证材料清单')
$manifestLines.Add('#')
$manifestLines.Add('# 格式：<SHA-256>  <包内文件名>  <仓库内来源路径>')
$manifestLines.Add('# 包内位置：licenses/pdfium/<包内文件名>')
$manifestLines.Add('#')
$manifestLines.Add("# 来源：bblanchon/pdfium-binaries 的 $Version 发布产物（$Asset）")
$manifestLines.Add("# 归档 SHA-256：$Sha256")
$manifestLines.Add("# PDFium API 版本：$($versionText -replace "`n", ' ')")
$manifestLines.Add('#')
$manifestLines.Add('# 这份清单是仓库内的锚点：打包时按清单逐份复制并核对哈希，')
$manifestLines.Add('# 发布门再核对安装包内的文件集合与哈希，任一份缺失或被改动都必须变红。')
$manifestLines.Add('# 清单可由 scripts\bootstrap-pdfium.ps1 -RegenerateLicenseManifest 重新生成。')
$manifestLines.Add('')
foreach ($name in $manifestEntries.Keys) {
    $entry = $manifestEntries[$name]
    $manifestLines.Add("$($entry.Hash)  $($entry.Target)  $($entry.Source)")
}
$generated = ($manifestLines -join "`n") + "`n"

if ($RegenerateLicenseManifest) {
    [System.IO.File]::WriteAllText($manifestPath, $generated, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "    已重新生成清单: $manifestPath（$($manifestEntries.Count) 份）" -ForegroundColor Green
} elseif (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "缺少许可证清单 $manifestPath（可用 -RegenerateLicenseManifest 生成）"
} else {
    # 只比对条目，不比对注释：注释可以自由编辑，条目必须与固定版本 PDFium 完全一致。
    $committedEntries = @{}
    foreach ($line in (Get-Content -LiteralPath $manifestPath -Encoding UTF8)) {
        if ($line -match '^([0-9a-f]{64})\s+(\S+)\s+(\S+)$') {
            $committedEntries[$matches[2]] = "$($matches[1])|$($matches[3])"
        }
    }
    $drift = New-Object System.Collections.Generic.List[string]
    foreach ($name in $manifestEntries.Keys) {
        $entry = $manifestEntries[$name]
        $expectedEntry = "$($entry.Hash)|$($entry.Source)"
        if (-not $committedEntries.ContainsKey($name)) {
            $drift.Add("清单缺少 $name")
        } elseif ($committedEntries[$name] -ne $expectedEntry) {
            $drift.Add("$name 的哈希或来源与固定版本 PDFium 不一致")
        }
    }
    foreach ($name in $committedEntries.Keys) {
        if (-not $manifestEntries.Contains($name)) {
            $drift.Add("清单多出 $name")
        }
    }
    if ($drift.Count -gt 0) {
        throw "许可证清单与固定版本 PDFium 的实际材料不一致：`n  $($drift -join "`n  ")`n请用 scripts\bootstrap-pdfium.ps1 -RegenerateLicenseManifest 重新生成"
    }
    Write-Host "    清单与固定版本 PDFium 一致（$($manifestEntries.Count) 份）" -ForegroundColor Green
}

Write-Host ''
Write-Host "PDFium 就绪：$InstallDir" -ForegroundColor Green
Write-Host "  API 版本：$($versionText -replace "`n", ' ')"
Write-Host "  许可证：  $(Join-Path $InstallDir 'LICENSE')"
Write-Host ''
Write-Host "下一步："
Write-Host "  `$env:AIPDF_PDFIUM_ROOT = '$InstallDir'"
