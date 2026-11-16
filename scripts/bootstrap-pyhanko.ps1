<#
.SYNOPSIS
    Build the self-contained pyHanko command used by release packages.

.DESCRIPTION
    Creates pyhanko.exe with PyInstaller. The result runs without a system
    Python or a separately installed pyhanko-cli. Package metadata and license
    files are copied next to the executable for redistribution auditing.
#>
param(
    [string]$Python = 'py',
    [string]$OutputDir = '',
    [string]$PyHankoVersion = '0.38.0',
    [string]$PyHankoCliVersion = '0.6.0',
    [string]$PyInstallerVersion = '6.16.0'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot 'tools\pyhanko' }
$buildRoot = Join-Path $repoRoot 'build\pyhanko-runtime'
$venv = Join-Path $buildRoot 'venv'
$resolvedBuild = [System.IO.Path]::GetFullPath($buildRoot)
$expectedPrefix = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build')) + [System.IO.Path]::DirectorySeparatorChar
if (-not $resolvedBuild.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "拒绝清理仓库 build 目录之外的路径: $resolvedBuild"
}
if (Test-Path -LiteralPath $buildRoot) { Remove-Item -LiteralPath $buildRoot -Recurse -Force }
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

Write-Host '==> 创建隔离构建环境' -ForegroundColor Cyan
& $Python -m venv $venv
if ($LASTEXITCODE -ne 0) { throw '无法创建 Python 虚拟环境' }
$pythonExe = Join-Path $venv 'Scripts\python.exe'
$requirements = Join-Path $PSScriptRoot 'pyhanko-requirements.txt'
& $pythonExe -m pip install --disable-pip-version-check --requirement $requirements
if ($LASTEXITCODE -ne 0) { throw '安装固定版本的 pyHanko 构建依赖失败' }

Write-Host '==> 生成独立 pyhanko.exe' -ForegroundColor Cyan
& $pythonExe -m PyInstaller --noconfirm --clean --onefile --name pyhanko `
    --distpath (Join-Path $buildRoot 'dist') --workpath (Join-Path $buildRoot 'work') `
    --specpath $buildRoot --collect-all pyhanko --collect-all pyhanko_certvalidator `
    --copy-metadata pyHanko --copy-metadata pyhanko-cli `
    (Join-Path $PSScriptRoot 'pyhanko-launcher.py')
if ($LASTEXITCODE -ne 0) { throw 'PyInstaller 生成 pyhanko.exe 失败' }

if (Test-Path -LiteralPath $OutputDir) { Remove-Item -LiteralPath $OutputDir -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $OutputDir 'licenses') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'dist\pyhanko.exe') -Destination $OutputDir

# Preserve the license texts shipped by every installed distribution. This is
# intentionally broader than pyHanko itself because the one-file executable
# contains its Python dependencies as well.
$sitePackages = Join-Path $venv 'Lib\site-packages'
Get-ChildItem -LiteralPath $sitePackages -Directory -Filter '*.dist-info' | ForEach-Object {
    $distribution = $_.BaseName -replace '\.dist-info$', ''
    $target = Join-Path (Join-Path $OutputDir 'licenses') $distribution
    $licenseFiles = @(Get-ChildItem -LiteralPath $_.FullName -Recurse -File | Where-Object {
        $_.Name -match '^(LICENSE|LICENCE|COPYING|NOTICE)'
    })
    if ($licenseFiles.Count -gt 0) {
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        foreach ($licenseFile in $licenseFiles) {
            $name = $licenseFile.Name
            if (Test-Path -LiteralPath (Join-Path $target $name)) {
                $name = "$($licenseFile.Directory.Name)-$name"
            }
            Copy-Item -LiteralPath $licenseFile.FullName -Destination (Join-Path $target $name)
        }
    }
}
$pythonBase = & $pythonExe -c 'import sys; print(sys.base_prefix)'
if ($LASTEXITCODE -ne 0) { throw '无法定位 Python 运行时许可证' }
$pythonLicense = Join-Path $pythonBase 'LICENSE.txt'
if ($pythonLicense -and (Test-Path -LiteralPath $pythonLicense)) {
    Copy-Item -LiteralPath $pythonLicense -Destination (Join-Path $OutputDir 'licenses\Python-LICENSE.txt')
}

@(
    "pyHanko=$PyHankoVersion"
    "pyhanko-cli=$PyHankoCliVersion"
    "PyInstaller=$PyInstallerVersion"
) | Set-Content -LiteralPath (Join-Path $OutputDir 'VERSIONS.txt') -Encoding UTF8
Write-Host "pyHanko 独立运行时已生成: $OutputDir" -ForegroundColor Green
