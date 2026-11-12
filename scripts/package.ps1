<#
.SYNOPSIS
    生成可独立运行的 AI PDF Editor Windows x64 开发包（ZIP）。

.DESCRIPTION
    步骤：
      1. 收集 Release 构建产物（主程序与 Worker）。
      2. 用 windeployqt 复制 Qt 运行库与平台插件。
      3. 附带许可证材料（项目 LICENSE、第三方清单、Qt 官方 SPDX SBOM）。
      4. 生成 NOTICE.txt 与 CycloneDX SBOM。
      5. 压缩为 ZIP 并输出 SHA-256。

    打包结果不依赖 Qt SDK，可在没有 Qt 的机器上运行。
#>
param(
    [string]$BuildDir = 'build\windows-release',
    [string]$OutputDir = 'dist',
    [string]$Version = '',
    [string]$QtRoot = '',
    [string]$MinGWRoot = '',
    [string]$PdfiumRoot = '',
    [switch]$KeepStaging
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'LicenseMap.ps1')

if (-not $Version) {
    $cmakeLists = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'CMakeLists.txt') -Raw -Encoding UTF8
    $match = [regex]::Match($cmakeLists, 'project\(AiPdfEditor VERSION ([0-9.]+)')
    if (-not $match.Success) {
        throw '无法从 CMakeLists.txt 解析项目版本'
    }
    $Version = $match.Groups[1].Value
}

if (-not $QtRoot) {
    $QtRoot = if ($env:AIPDF_QT_ROOT) { $env:AIPDF_QT_ROOT } else { 'C:\Qt\6.8.3\mingw_64' }
}
if (-not $MinGWRoot) {
    $MinGWRoot = if ($env:AIPDF_MINGW_ROOT) { $env:AIPDF_MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }
}
if (-not $PdfiumRoot) {
    $PdfiumRoot = if ($env:AIPDF_PDFIUM_ROOT) {
        $env:AIPDF_PDFIUM_ROOT
    } else {
        Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\pdfium'
    }
}

$packageName = "ai-pdf-editor-$Version-win64"
$binDir = Join-Path $BuildDir 'bin'
$stagingDir = Join-Path $OutputDir 'staging'
$stage = Join-Path $stagingDir $packageName
$zipPath = Join-Path $OutputDir "$packageName.zip"

foreach ($required in @('AiPdfEditor.exe', 'PdfAiWorker.exe')) {
    $path = Join-Path $binDir $required
    if (-not (Test-Path -LiteralPath $path)) {
        throw "缺少构建产物 $path，请先执行 cmake --build --preset windows-release"
    }
}

$windeployqt = Join-Path $QtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $windeployqt)) {
    throw "找不到 windeployqt: $windeployqt（请检查 AIPDF_QT_ROOT）"
}

Write-Host "==> 清理暂存目录 $stagingDir" -ForegroundColor Cyan
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Write-Host '==> 复制构建产物' -ForegroundColor Cyan
Copy-Item -LiteralPath (Join-Path $binDir 'AiPdfEditor.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $binDir 'PdfAiWorker.exe') -Destination $stage

Write-Host '==> 复制 PDFium 运行库' -ForegroundColor Cyan
$pdfiumDll = Join-Path $PdfiumRoot 'bin\pdfium.dll'
if (-not (Test-Path -LiteralPath $pdfiumDll)) {
    throw "找不到 PDFium 运行库 $pdfiumDll（请运行 scripts\bootstrap-pdfium.ps1）"
}
Copy-Item -LiteralPath $pdfiumDll -Destination $stage

Write-Host '==> 部署 Qt 运行库' -ForegroundColor Cyan
& $windeployqt --release --no-translations --no-opengl-sw --dir $stage (Join-Path $stage 'AiPdfEditor.exe')
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt 执行失败（退出码 $LASTEXITCODE）"
}

$requiredFiles = @(
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'Qt6Network.dll',
    'platforms/qwindows.dll',
    'libgcc_s_seh-1.dll',
    'libstdc++-6.dll',
    'libwinpthread-1.dll'
)
foreach ($required in $requiredFiles) {
    $path = Join-Path $stage $required
    if (-not (Test-Path -LiteralPath $path)) {
        throw "部署结果缺少 $required，打包中止"
    }
}

Write-Host '==> 收集许可证材料' -ForegroundColor Cyan
$licenseDir = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $licenseDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\THIRD_PARTY_LICENSES.md') -Destination $licenseDir

# 第三方许可证全文（LGPL-3.0 / GPL-3.0 / GCC 运行时例外）
$licenseSources = Join-Path $PSScriptRoot '..\licenses'
foreach ($licenseFile in @('LGPL-3.0.txt', 'GPL-3.0.txt', 'GCC-RUNTIME-LIBRARY-EXCEPTION.txt')) {
    $source = Join-Path $licenseSources $licenseFile
    if (-not (Test-Path -LiteralPath $source)) {
        throw "缺少许可证全文 $source（可运行 scripts\fetch-licenses.ps1 补齐）"
    }
    Copy-Item -LiteralPath $source -Destination $licenseDir
}

$qtSbom = Join-Path $QtRoot 'sbom\qtbase-6.8.3.spdx.json'
if (Test-Path -LiteralPath $qtSbom) {
    Copy-Item -LiteralPath $qtSbom -Destination $licenseDir
    Write-Host "    附带 Qt 官方 SPDX SBOM: $(Split-Path -Leaf $qtSbom)"
} else {
    Write-Warning "未找到 Qt 官方 SPDX SBOM: $qtSbom"
}

# PDFium 及其第三方组件的许可证原文：按仓库内清单逐份复制并核对哈希。
# 清单是仓库内的锚点，缺任何一份或哈希不符都在打包阶段就中止。
$repoRoot = Join-Path $PSScriptRoot '..'
$pdfiumLicenseManifest = Join-Path $repoRoot 'licenses\pdfium-CHECKSUMS.txt'
if (-not (Test-Path -LiteralPath $pdfiumLicenseManifest)) {
    throw "缺少 PDFium 许可证清单 $pdfiumLicenseManifest"
}
$pdfiumLicenseTarget = Join-Path $licenseDir 'pdfium'
New-Item -ItemType Directory -Path $pdfiumLicenseTarget -Force | Out-Null
$copiedPdfiumLicenses = 0
foreach ($line in (Get-Content -LiteralPath $pdfiumLicenseManifest -Encoding UTF8)) {
    if ($line -notmatch '^([0-9a-f]{64})\s+(\S+)\s+(\S+)$') {
        continue
    }
    $expectedHash = $matches[1]
    $targetName = $matches[2]
    $sourceRelative = $matches[3]
    $sourcePath = Join-Path $repoRoot ($sourceRelative -replace '/', '\')
    if (-not (Test-Path -LiteralPath $sourcePath)) {
        throw "PDFium 许可证源文件缺失: $sourceRelative（清单条目 $targetName）"
    }
    $actualHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "PDFium 许可证源文件哈希不符: $sourceRelative 期望 $expectedHash 实际 $actualHash"
    }
    Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $pdfiumLicenseTarget $targetName)
    $copiedPdfiumLicenses++
}
if ($copiedPdfiumLicenses -lt 1) {
    throw 'PDFium 许可证清单没有有效条目，打包中止'
}
Copy-Item -LiteralPath $pdfiumLicenseManifest -Destination $licenseDir
Write-Host "    附带 PDFium 许可证材料: $copiedPdfiumLicenses 份"

Write-Host '==> 生成 NOTICE.txt' -ForegroundColor Cyan
$noticeLines = New-Object System.Collections.Generic.List[string]
$noticeLines.Add("AI PDF Editor $Version - Windows x64 开发包")
$noticeLines.Add('')
$noticeLines.Add('本包为内部开发包，项目自身尚未选定公开许可证，见 LICENSE。')
$noticeLines.Add('随包分发的第三方二进制及其许可证：')
$noticeLines.Add('')
$binaries = Get-ChildItem -LiteralPath $stage -Recurse -File |
    Where-Object { $_.Extension -in @('.dll', '.exe') } |
    Sort-Object FullName
foreach ($binary in $binaries) {
    $relative = $binary.FullName.Substring($stage.Length).TrimStart('\', '/')
    $license = Get-AiPdfLicense -Name $binary.Name
    $versionInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($binary.FullName)
    $fileVersion = if ($versionInfo.FileVersion) { $versionInfo.FileVersion } else { 'n/a' }
    $noticeLines.Add(("  {0,-52} {1,-16} {2}" -f $relative, $fileVersion, $license.id))
}
$noticeLines.Add('')
$noticeLines.Add('完整依赖清单与 SBOM：licenses/THIRD_PARTY_LICENSES.md、sbom/ai-pdf-editor-' + $Version + '.cdx.json')
[System.IO.File]::WriteAllLines(
    (Join-Path $stage 'NOTICE.txt'),
    $noticeLines,
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host '==> 生成 SBOM' -ForegroundColor Cyan
$sbomOutput = Join-Path $stage "sbom\ai-pdf-editor-$Version.cdx.json"
$pdfiumVersion = ''
$pdfiumVersionFile = Join-Path $PdfiumRoot 'VERSION'
if (Test-Path -LiteralPath $pdfiumVersionFile) {
    $fields = @{}
    foreach ($line in (Get-Content -LiteralPath $pdfiumVersionFile -Encoding UTF8)) {
        if ($line -match '^(\w+)=(\d+)$') {
            $fields[$matches[1]] = $matches[2]
        }
    }
    if ($fields.ContainsKey('MAJOR')) {
        $pdfiumVersion = "$($fields['MAJOR']).$($fields['MINOR']).$($fields['BUILD']).$($fields['PATCH'])"
    }
}
& (Join-Path $PSScriptRoot 'generate-sbom.ps1') `
    -PackageDir $stage `
    -OutputPath $sbomOutput `
    -Version $Version `
    -QtSbomPath $qtSbom `
    -PdfiumVersion $pdfiumVersion

Write-Host '==> 压缩' -ForegroundColor Cyan
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path $stage -DestinationPath $zipPath -CompressionLevel Optimal

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$hashPath = "$zipPath.sha256"
[System.IO.File]::WriteAllText(
    $hashPath,
    "$hash  $packageName.zip`n",
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host ''
Write-Host "打包完成: $zipPath" -ForegroundColor Green
Write-Host "SHA-256:  $hash"
Write-Host "大小:     $([math]::Round((Get-Item -LiteralPath $zipPath).Length / 1MB, 2)) MB"

if (-not $KeepStaging) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
