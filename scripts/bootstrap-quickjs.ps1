# 获取并校验固定版本的 QuickJS-ng 源码（表单脚本引擎）。
#
# 为什么是源码而不是预编译库：QuickJS 只有几个 C 文件，用同一个 MinGW 工具链
# 一起编译最省事，也不会踩 C++ 库跨 ABI 的坑。
#
# 版本与哈希都是钉死的：下载后按 SHA-256 校验，不匹配立即失败，不落半成品。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\bootstrap-quickjs.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\bootstrap-quickjs.ps1 -VerifyOnly
param(
    [string]$InstallDir = (Join-Path $PSScriptRoot '..\third_party\quickjs'),
    [string]$Version = 'v0.9.0',
    [string]$Sha256 = '77f9e79b42e2e7cff9517bae612431af47e120730286cb1dcfad0753bc160f10',
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$header = Join-Path $InstallDir 'quickjs.h'
if ($VerifyOnly) {
    if (Test-Path -LiteralPath $header) {
        Write-Host "QuickJS 已安装：$InstallDir" -ForegroundColor Green
        exit 0
    }
    Write-Host "QuickJS 未安装：$InstallDir" -ForegroundColor Red
    exit 1
}

if (Test-Path -LiteralPath $header) {
    Write-Host "QuickJS 已存在于 $InstallDir" -ForegroundColor Green
    exit 0
}

$url = "https://github.com/quickjs-ng/quickjs/archive/refs/tags/$Version.tar.gz"
$archive = Join-Path ([System.IO.Path]::GetTempPath()) "quickjs-ng-$Version.tar.gz"

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
Write-Host "下载 QuickJS $Version <- $url" -ForegroundColor Cyan
Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing -TimeoutSec 600

$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $Sha256) {
    Write-Host "SHA-256 不匹配：期望 $Sha256 实际 $actual" -ForegroundColor Red
    Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "SHA-256 校验通过" -ForegroundColor Green

# 解压时去掉压缩包里的顶层目录（quickjs-<版本>/），让 third_party\quickjs 直接是根。
$stage = Join-Path ([System.IO.Path]::GetTempPath()) "quickjs-stage-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage -Force | Out-Null
try {
    tar -xzf $archive -C $stage
    if ($LASTEXITCODE -ne 0) { throw "解压失败（tar 退出码 $LASTEXITCODE）" }
    $root = Get-ChildItem -LiteralPath $stage -Directory | Select-Object -First 1
    if ($null -eq $root) { throw "压缩包里没有顶层目录" }
    Copy-Item -Path (Join-Path $root.FullName '*') -Destination $InstallDir -Recurse -Force
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath $header)) {
    Write-Host "解压后没找到 quickjs.h：$InstallDir" -ForegroundColor Red
    exit 1
}
Write-Host "QuickJS 安装完成：$InstallDir" -ForegroundColor Green
exit 0
