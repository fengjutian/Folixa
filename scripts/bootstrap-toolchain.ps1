<#
.SYNOPSIS
    安装并固定 AI PDF Editor 的 Windows 构建工具链。

.DESCRIPTION
    只安装免管理员的官方二进制套件：
      - Qt 6.8.3 (win64_mingw, 仅 qtbase)
      - MinGW-w64 GCC 13.1.0 (Qt 官方工具包)
    脚本可重复执行：已安装且版本一致时直接跳过。

    CMake 与 Ninja 由系统提供，脚本只校验版本并给出提示，
    不代为安装（需要管理员权限）。
#>
param(
    [string]$InstallDir = 'C:\Qt',
    [string]$QtVersion = '6.8.3',
    [string]$QtArchitecture = 'win64_mingw',
    [string]$MinGWTool = 'tools_mingw1310',
    [string]$MinGWVariant = 'qt.tools.win64_mingw1310',
    [string]$AqtVersion = '3.3.0',
    [string]$MinimumCMakeVersion = '3.25',
    [string]$MinimumNinjaVersion = '1.10'
)

$ErrorActionPreference = 'Stop'

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Get-ToolVersion {
    param([string]$Command, [string]$Arguments = '--version')
    # 必须先收完输出再取第一行：直接管道给 Select-Object -First 1 会提前掐断
    # 原生命令的输出流，导致退出码异常、版本探测失败。
    $output = & $Command $Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    $first = $output | Select-Object -First 1
    if (-not $first) {
        return $null
    }
    return ($first -replace '\s+', ' ').Trim()
}

$qtRoot = Join-Path $InstallDir "$QtVersion\mingw_64"
$qtConfig = Join-Path $qtRoot 'lib\cmake\Qt6\Qt6Config.cmake'
$mingwRoot = Join-Path $InstallDir 'Tools\mingw1310_64'
$gpp = Join-Path $mingwRoot 'bin\g++.exe'

Write-Step '检查 aqtinstall'
$aqtReady = $false
try {
    $aqtVersion = (& aqt version 2>&1 | Select-Object -First 1)
    $aqtReady = $true
} catch {
    $aqtReady = $false
}
if (-not $aqtReady) {
    Write-Host "安装 aqtinstall==$AqtVersion"
    pip install --disable-pip-version-check "aqtinstall==$AqtVersion"
    if ($LASTEXITCODE -ne 0) {
        throw "aqtinstall 安装失败"
    }
} else {
    Write-Host "已安装: $aqtVersion"
}

if (Test-Path -LiteralPath $qtConfig) {
    Write-Step "Qt $QtVersion 已存在于 $qtRoot，跳过下载"
} else {
    Write-Step "下载 Qt $QtVersion ($QtArchitecture)"
    aqt install-qt windows desktop $QtVersion $QtArchitecture `
        --outputdir $InstallDir --archives qtbase
    if ($LASTEXITCODE -ne 0) {
        throw "Qt 安装失败"
    }
}

if (Test-Path -LiteralPath $gpp) {
    Write-Step "MinGW-w64 已存在于 $mingwRoot，跳过下载"
} else {
    Write-Step "下载 MinGW-w64 GCC 13.1.0 工具包"
    aqt install-tool windows desktop $MinGWTool $MinGWVariant --outputdir $InstallDir
    if ($LASTEXITCODE -ne 0) {
        throw "MinGW-w64 工具包安装失败"
    }
}

Write-Step '校验工具链'
if (-not (Test-Path -LiteralPath $qtConfig)) {
    throw "Qt 套件不完整: 缺少 $qtConfig"
}
if (-not (Test-Path -LiteralPath $gpp)) {
    throw "MinGW-w64 不完整: 缺少 $gpp"
}

$compiler = Get-ToolVersion -Command $gpp
$cmake = Get-ToolVersion -Command 'cmake'
$ninja = Get-ToolVersion -Command 'ninja'

Write-Host "Qt:        $QtVersion ($QtArchitecture) -> $qtRoot"
Write-Host "MinGW-w64: $compiler -> $mingwRoot"
Write-Host "CMake:     $cmake"
Write-Host "Ninja:     $ninja"

if (-not $cmake) {
    Write-Warning '未找到 CMake，请安装 3.25 或更高版本'
}
if (-not $ninja) {
    Write-Warning '未找到 Ninja，请安装 1.10 或更高版本'
}

Write-Step '完成'
Write-Host @"
下一步（在仓库根目录执行）：

  `$env:AIPDF_QT_ROOT    = '$qtRoot'
  `$env:AIPDF_MINGW_ROOT = '$mingwRoot'
  cmake --preset windows-debug
  cmake --build --preset windows-debug
  ctest --preset windows-debug
"@
