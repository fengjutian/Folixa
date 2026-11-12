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
    [string]$ExpectedApiVersion = 'MAJOR=159'
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
Write-Host "PDFium 就绪：$InstallDir" -ForegroundColor Green
Write-Host "  API 版本：$($versionText -replace "`n", ' ')"
Write-Host "  许可证：  $(Join-Path $InstallDir 'LICENSE')"
Write-Host ''
Write-Host "下一步："
Write-Host "  `$env:AIPDF_PDFIUM_ROOT = '$InstallDir'"
