# 获取并校验固定版本的 qpdf 预编译包（MinGW x64）。
#
# 为什么用 MinGW 版而不是 MSVC 版：本项目用 MinGW GCC 13 构建，C++ 库不能跨 ABI 链接。
# qpdf 官方为每个 release 同时提供 msvc64 与 mingw64 两个包，这里固定取后者。
#
# 版本与哈希都是钉死的：下载后按 SHA-256 校验，不匹配立即失败，不落半成品。
# 哈希来自 release 附带的 qpdf-<版本>.sha256。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\bootstrap-qpdf.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\bootstrap-qpdf.ps1 -VerifyOnly
param(
    [string]$InstallDir = (Join-Path $PSScriptRoot '..\tools\qpdf'),
    [string]$Version = 'v12.4.2',
    [string]$Asset = 'qpdf-12.4.2-mingw64.zip',
    [string]$Sha256 = '773d2fa0c7d161e2271430734338e560a07b25d0edf11ceee8e53de435012766',
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Find-FirstFile {
    param([string]$Root, [string]$Filter)
    if (-not (Test-Path -LiteralPath $Root)) { return $null }
    $hit = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $Filter -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

$dll = Find-FirstFile -Root $InstallDir -Filter 'qpdf*.dll'

if ($VerifyOnly) {
    if (-not $dll) {
        Write-Host "qpdf 未安装：$InstallDir" -ForegroundColor Red
        exit 1
    }
    Write-Host "qpdf 已安装：$dll" -ForegroundColor Green
    exit 0
}

if ($dll) {
    Write-Host "qpdf 已存在于 $InstallDir" -ForegroundColor Green
} else {
    $url = "https://github.com/qpdf/qpdf/releases/download/$Version/$Asset"
    $archive = Join-Path ([System.IO.Path]::GetTempPath()) $Asset

    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Write-Host "下载 qpdf $Version <- $url" -ForegroundColor Cyan
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing -TimeoutSec 900

    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Sha256) {
        Write-Host "SHA-256 不匹配：期望 $Sha256 实际 $actual" -ForegroundColor Red
        Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
        exit 1
    }
    Write-Host "SHA-256 校验通过" -ForegroundColor Green

    # 解压时去掉压缩包里的顶层目录（qpdf-<版本>-mingw64/），让 tools\qpdf 直接是根。
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $prefix = $zip.Entries[0].FullName.Split('/')[0] + '/'
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName -eq $prefix -or $entry.FullName.EndsWith('/')) { continue }
            $relative = $entry.FullName.Substring($prefix.Length)
            if ([string]::IsNullOrWhiteSpace($relative)) { continue }
            $target = Join-Path $InstallDir $relative
            $parent = Split-Path -Parent $target
            if (-not (Test-Path -LiteralPath $parent)) {
                New-Item -ItemType Directory -Path $parent -Force | Out-Null
            }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally {
        $zip.Dispose()
        Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
    }
}

$dll = Find-FirstFile -Root $InstallDir -Filter 'qpdf*.dll'
if (-not $dll) {
    Write-Host "解压后没找到 qpdf DLL：$InstallDir" -ForegroundColor Red
    exit 1
}

$header = Find-FirstFile -Root $InstallDir -Filter 'QPDF.hh'
$importLib = Find-FirstFile -Root $InstallDir -Filter 'libqpdf*.a'
$exe = Find-FirstFile -Root $InstallDir -Filter 'qpdf.exe'

Write-Host ""
Write-Host "qpdf 安装完成：$InstallDir" -ForegroundColor Green
Write-Host "  QPDF_DLL         = $dll"
if ($exe) { Write-Host "  QPDF_EXECUTABLE  = $exe" }
if ($importLib) { Write-Host "  QPDF_LIBRARY     = $importLib" }
if ($header) { Write-Host "  QPDF_INCLUDE_DIR = $(Split-Path -Parent $header)" }
Write-Host ""
Write-Host "把根目录传给 CMake：-DAIPDF_QPDF_ROOT=`"$((Resolve-Path -LiteralPath $InstallDir).Path)`"" -ForegroundColor Cyan
exit 0
