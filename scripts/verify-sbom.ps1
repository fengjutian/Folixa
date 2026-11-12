<#
.SYNOPSIS
    校验打包产物的 SBOM 与包内实际文件一致。

.DESCRIPTION
    这是一道发布门：
      1. SBOM 必须是可解析的 CycloneDX 1.5 文档，且声明了正确的版本；
      2. 包内每个文件都必须在 SBOM 里出现，且 SHA-256 与实际文件一致；
      3. 每个随包分发的二进制都必须带许可证标识；
      4. Qt 与 MinGW-w64 两个组件必须存在并带 purl。

    任何一项不满足即返回非零退出码。
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir,
    [Parameter(Mandatory = $true)]
    [string]$SbomPath,
    [string]$Version = ''
)

$ErrorActionPreference = 'Stop'

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

if (-not (Test-Path -LiteralPath $SbomPath)) {
    Write-Host "找不到 SBOM: $SbomPath" -ForegroundColor Red
    exit 2
}
if (-not (Test-Path -LiteralPath $PackageDir)) {
    Write-Host "找不到包目录: $PackageDir" -ForegroundColor Red
    exit 2
}

$packageRoot = (Resolve-Path -LiteralPath $PackageDir).Path
$sbom = Get-Content -LiteralPath $SbomPath -Raw -Encoding UTF8 | ConvertFrom-Json

Test-Check 'SBOM 是 CycloneDX 文档' ($sbom.bomFormat -eq 'CycloneDX') "bomFormat=$($sbom.bomFormat)"
Test-Check 'SBOM 遵循 1.5 规范' ($sbom.specVersion -eq '1.5') "specVersion=$($sbom.specVersion)"

if ($Version) {
    $declared = $sbom.metadata.component.version
    Test-Check "SBOM 声明的版本为 $Version" ($declared -eq $Version) "version=$declared"
}

$components = @($sbom.components)
$fileComponents = @{}
foreach ($component in $components) {
    $property = $component.properties | Where-Object { $_.name -eq 'aipdf:packagePath' }
    if ($property) {
        $fileComponents[$property.value] = $component
    }
}

Test-Check 'SBOM 登记了 Qt 组件' (
    @($components | Where-Object { $_.purl -eq "pkg:generic/qt@$($sbom.metadata.component.version)" -or $_.name -eq 'Qt' }).Count -ge 1)
Test-Check 'SBOM 登记了 MinGW-w64 组件' (
    @($components | Where-Object { $_.name -eq 'MinGW-w64 runtime' -and $_.purl }).Count -ge 1)

$sbomFileName = Split-Path -Leaf $SbomPath
$missing = New-Object System.Collections.Generic.List[string]
$mismatched = New-Object System.Collections.Generic.List[string]
$unlicensed = New-Object System.Collections.Generic.List[string]

$files = Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Sort-Object FullName
foreach ($file in $files) {
    $relative = $file.FullName.Substring($packageRoot.Length).TrimStart('\', '/') -replace '\\', '/'
    if ($relative -eq "sbom/$sbomFileName") {
        continue
    }
    if (-not $fileComponents.ContainsKey($relative)) {
        $missing.Add($relative)
        continue
    }
    $component = $fileComponents[$relative]
    $declaredHash = ($component.hashes | Where-Object { $_.alg -eq 'SHA-256' } | Select-Object -First 1).content
    $actualHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($declaredHash -ne $actualHash) {
        $mismatched.Add($relative)
    }
    if ($file.Extension -in @('.dll', '.exe') -and -not $component.licenses) {
        $unlicensed.Add($relative)
    }
}

Test-Check '包内每个文件都登记在 SBOM 中' ($missing.Count -eq 0) ($missing -join ', ')
Test-Check 'SBOM 中的 SHA-256 与实际文件一致' ($mismatched.Count -eq 0) ($mismatched -join ', ')
Test-Check '每个随包二进制都带许可证标识' ($unlicensed.Count -eq 0) ($unlicensed -join ', ')

if ($script:Failures -gt 0) {
    Write-Host "`nSBOM 校验失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`nSBOM 校验通过：$($files.Count) 个文件全部登记且哈希一致" -ForegroundColor Green
exit 0
