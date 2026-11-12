<#
.SYNOPSIS
    校验版本号在各处一致，并检查版本索引的状态约束。

.DESCRIPTION
    依据 docs\VERSIONING.md 的要求：
      1. CMake 项目版本、vcpkg 清单版本必须一致；
      2. 必须存在对应的版本文档，文档状态合法；
      3. 版本索引中该版本的状态必须与文档一致；
      4. 版本索引中同一时间只能有一个 In Progress 版本；
      5. 可执行文件与安装包名称中的版本号必须与项目版本一致。
#>
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..'),
    [string]$Executable = '',
    [string]$PackagePath = ''
)

$ErrorActionPreference = 'Stop'

$script:Failures = 0
$allowedStates = @('Planned', 'In Progress', 'Verification', 'Released', 'Blocked')

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

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path

$cmakeLists = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw -Encoding UTF8
$match = [regex]::Match($cmakeLists, 'project\(AiPdfEditor VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $match.Success) {
    Write-Host '无法从 CMakeLists.txt 解析项目版本' -ForegroundColor Red
    exit 2
}
$cmakeVersion = $match.Groups[1].Value

$manifest = Get-Content -LiteralPath (Join-Path $root 'vcpkg.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$vcpkgVersion = $manifest.'version-string'

Test-Check 'CMake 项目版本可解析' ($cmakeVersion -ne '') $cmakeVersion
Test-Check "vcpkg 清单版本与 CMake 一致 ($cmakeVersion)" ($vcpkgVersion -eq $cmakeVersion) "vcpkg=$vcpkgVersion"

$versionDoc = Join-Path $root "docs\versions\v$cmakeVersion.md"
Test-Check "存在版本文档 v$cmakeVersion.md" (Test-Path -LiteralPath $versionDoc)

$docState = ''
if (Test-Path -LiteralPath $versionDoc) {
    $docText = Get-Content -LiteralPath $versionDoc -Raw -Encoding UTF8
    $stateMatch = [regex]::Match($docText, '(?m)^\*\*状态：\*\*\s*([A-Za-z ]+?)\s*$')
    if ($stateMatch.Success) {
        $docState = $stateMatch.Groups[1].Value.Trim()
    }
    Test-Check "版本文档状态合法" ($allowedStates -contains $docState) "state=$docState"
}

$indexPath = Join-Path $root 'docs\versions\README.md'
$indexText = Get-Content -LiteralPath $indexPath -Raw -Encoding UTF8
$rows = [regex]::Matches($indexText, '(?m)^\|\s*\[(?<version>[0-9]+\.[0-9]+\.[0-9]+)\]\([^)]+\)\s*\|[^|]*\|\s*(?<state>[A-Za-z ]+?)\s*\|')
$indexStates = @{}
$inProgress = @()
foreach ($row in $rows) {
    $version = $row.Groups['version'].Value
    $state = $row.Groups['state'].Value.Trim()
    $indexStates[$version] = $state
    if ($state -eq 'In Progress') {
        $inProgress += $version
    }
}

Test-Check "版本索引包含 $cmakeVersion" ($indexStates.ContainsKey($cmakeVersion))
if ($indexStates.ContainsKey($cmakeVersion) -and $docState) {
    Test-Check "索引状态与版本文档一致 ($docState)" `
        ($indexStates[$cmakeVersion] -eq $docState) "index=$($indexStates[$cmakeVersion])"
}
Test-Check '同一时间只有一个 In Progress 版本' ($inProgress.Count -le 1) ($inProgress -join ', ')

if ($Executable -and (Test-Path -LiteralPath $Executable)) {
    $fileVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path -LiteralPath $Executable).Path).FileVersion
    Test-Check "可执行文件版本包含 $cmakeVersion" ($fileVersion -like "$cmakeVersion*") "fileVersion=$fileVersion"
}

if ($PackagePath) {
    $name = Split-Path -Leaf $PackagePath
    Test-Check "安装包名称包含版本 $cmakeVersion" ($name -like "*$cmakeVersion*") $name
}

if ($script:Failures -gt 0) {
    Write-Host "`n版本一致性校验失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n版本一致性校验通过：当前版本 $cmakeVersion，状态 $docState" -ForegroundColor Green
exit 0
