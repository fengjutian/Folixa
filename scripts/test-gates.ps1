<#
.SYNOPSIS
    发布门的负例守卫测试：故意弄坏它负责的东西，门必须变红。

.DESCRIPTION
    没有负例的门等于没被观察过报红的门。本脚本对两道门各做一次破坏性验证：

      SBOM 门（scripts\verify-sbom.ps1）
        A. 正常状态必须通过；
        B. 篡改某个文件的 SHA-256 必须失败；
        C. 从 SBOM 里删掉某个文件条目必须失败；
        D. 改掉 SBOM 声明的版本号必须失败。

      版本门（scripts\check-version.ps1）
        E. 版本一致必须通过；
        F. vcpkg 清单版本与 CMake 不一致必须失败；
        G. 索引里出现两个 In Progress 版本必须失败。

    全部负例都按预期变红才算通过。
#>
param(
    [string]$PackagePath = '',
    [string]$Version = '',
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..')
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
if (-not $Version) {
    $cmakeLists = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
    $Version = [regex]::Match($cmakeLists, 'project\(AiPdfEditor VERSION ([0-9.]+)').Groups[1].Value
}
if (-not $PackagePath) {
    $PackagePath = Join-Path $root "dist\ai-pdf-editor-$Version-win64.zip"
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

function Invoke-Gate {
    param([string]$Name, [string]$Script, [string[]]$Arguments)
    & powershell -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments 2>&1 |
        ForEach-Object { Write-Host "      $_" }
    return $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $PackagePath)) {
    Write-Host "找不到安装包: $PackagePath" -ForegroundColor Red
    exit 2
}

$workRoot = Join-Path $root 'build\gate-tests'
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null
$packageDir = Join-Path $workRoot 'package'
if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
Expand-Archive -LiteralPath $PackagePath -DestinationPath $packageDir -Force

$appDir = (Get-ChildItem -LiteralPath $packageDir -Directory | Select-Object -First 1).FullName
$sbomPath = Join-Path $appDir "sbom\ai-pdf-editor-$Version.cdx.json"
$sbomGate = Join-Path $PSScriptRoot 'verify-sbom.ps1'

Write-Host '=== SBOM 门 ===' -ForegroundColor Cyan
$exit = Invoke-Gate '正常状态' $sbomGate @('-PackageDir', $appDir, '-SbomPath', $sbomPath, '-Version', $Version)
Test-Check 'A 正常 SBOM 通过' ($exit -eq 0) "exit=$exit"

$original = Get-Content -LiteralPath $sbomPath -Raw -Encoding UTF8

# B. 篡改一个文件的 SHA-256
$tampered = $original -replace '("alg"\s*:\s*"SHA-256"\s*,\s*"content"\s*:\s*")[0-9a-f]{64}', '${1}0000000000000000000000000000000000000000000000000000000000000000'
[System.IO.File]::WriteAllText($sbomPath, $tampered, (New-Object System.Text.UTF8Encoding($false)))
$exit = Invoke-Gate '篡改哈希' $sbomGate @('-PackageDir', $appDir, '-SbomPath', $sbomPath, '-Version', $Version)
Test-Check 'B 哈希被篡改时必须失败' ($exit -ne 0) "exit=$exit"

# C. 删掉一个文件条目
$document = $original | ConvertFrom-Json
$fileComponent = @($document.components | Where-Object { $_.type -eq 'file' })[0]
$document.components = @($document.components | Where-Object { $_.'bom-ref' -ne $fileComponent.'bom-ref' })
[System.IO.File]::WriteAllText(
    $sbomPath,
    ($document | ConvertTo-Json -Depth 12),
    (New-Object System.Text.UTF8Encoding($false)))
$exit = Invoke-Gate '删除文件条目' $sbomGate @('-PackageDir', $appDir, '-SbomPath', $sbomPath, '-Version', $Version)
Test-Check 'C 文件条目缺失时必须失败' ($exit -ne 0) "exit=$exit"

# D. 改错版本号
$document = $original | ConvertFrom-Json
$document.metadata.component.version = '9.9.9'
[System.IO.File]::WriteAllText(
    $sbomPath,
    ($document | ConvertTo-Json -Depth 12),
    (New-Object System.Text.UTF8Encoding($false)))
$exit = Invoke-Gate '版本号不符' $sbomGate @('-PackageDir', $appDir, '-SbomPath', $sbomPath, '-Version', $Version)
Test-Check 'D 版本号不符时必须失败' ($exit -ne 0) "exit=$exit"

# 还原，避免影响后续用例
[System.IO.File]::WriteAllText($sbomPath, $original, (New-Object System.Text.UTF8Encoding($false)))

Write-Host '=== 版本门 ===' -ForegroundColor Cyan
$versionGate = Join-Path $PSScriptRoot 'check-version.ps1'

$fakeRoot = Join-Path $workRoot 'fake-repo'
if (Test-Path -LiteralPath $fakeRoot) {
    Remove-Item -LiteralPath $fakeRoot -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $fakeRoot 'docs\versions') -Force | Out-Null
[System.IO.File]::WriteAllText(
    (Join-Path $fakeRoot 'CMakeLists.txt'),
    "project(AiPdfEditor VERSION $Version LANGUAGES CXX)`n")
[System.IO.File]::WriteAllText(
    (Join-Path $fakeRoot 'vcpkg.json'),
    ('{ "name": "ai-pdf-editor", "version-string": "' + $Version + '" }'))
[System.IO.File]::WriteAllText(
    (Join-Path $fakeRoot 'docs\versions\README.md'),
    "| [$Version](v$Version.md) | 任务 | Verification | 产物 |`n")
[System.IO.File]::WriteAllText(
    (Join-Path $fakeRoot "docs\versions\v$Version.md"),
    "# v$Version`n`n**状态：** Verification`n")

$exit = Invoke-Gate '版本一致' $versionGate @('-RepositoryRoot', $fakeRoot)
Test-Check 'E 版本一致时通过' ($exit -eq 0) "exit=$exit"

[System.IO.File]::WriteAllText(
    (Join-Path $fakeRoot 'vcpkg.json'),
    ('{ "name": "ai-pdf-editor", "version-string": "0.0.1" }'))
$exit = Invoke-Gate 'vcpkg 版本不一致' $versionGate @('-RepositoryRoot', $fakeRoot)
Test-Check 'F vcpkg 版本不一致时必须失败' ($exit -ne 0) "exit=$exit"

[System.IO.File]::WriteAllText(
    (Join-Path $fakeRoot 'vcpkg.json'),
    ('{ "name": "ai-pdf-editor", "version-string": "' + $Version + '" }'))
[System.IO.File]::WriteAllText(
    (Join-Path $fakeRoot 'docs\versions\README.md'),
    "| [$Version](v$Version.md) | 任务 | In Progress | 产物 |`n| [0.9.0](v0.9.0.md) | 任务 | In Progress | 产物 |`n")
$exit = Invoke-Gate '两个 In Progress' $versionGate @('-RepositoryRoot', $fakeRoot)
Test-Check 'G 两个 In Progress 版本时必须失败' ($exit -ne 0) "exit=$exit"

Remove-Item -LiteralPath $workRoot -Recurse -Force -ErrorAction SilentlyContinue

if ($script:Failures -gt 0) {
    Write-Host "`n门的负例守卫失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n门的负例守卫全部通过：每道门在输入被破坏时都会变红" -ForegroundColor Green
exit 0
