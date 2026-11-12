<#
.SYNOPSIS
    发布门的负例守卫测试：故意弄坏它负责的东西，门必须变红。

.DESCRIPTION
    没有负例的门等于没被观察过报红的门。本脚本对三道门各做一次破坏性验证：

      SBOM 门（scripts\verify-sbom.ps1）
        A. 正常状态必须通过；
        B. 篡改某个文件的 SHA-256 必须失败；
        C. 从 SBOM 里删掉某个文件条目必须失败；
        D. 改掉 SBOM 声明的版本号必须失败。

      许可证门（scripts\verify-licenses.ps1）
        H1. 许可证齐全时通过；
        H2. 篡改 LGPL 全文必须失败；
        H3. 还原后恢复通过；
        H4. 少发一份 PDFium 第三方许可证必须失败；
        H5. 篡改一份 PDFium 许可证必须失败；
        H6. 多发一份清单之外的许可证必须失败；
        H7. 还原后 PDFium 许可证校验恢复通过；
        H8. 包内许可证改成 CRLF 行尾必须仍然通过（行尾不是许可证内容的一部分）。

      取件门（scripts\fetch-licenses.ps1 -VerifyOnly）
        I1. 仓库内许可证齐全时通过；
        I2. 篡改仓库内 GCC 运行时例外必须失败；
        I3. 还原后恢复通过；
        I4. 仓库内许可证改成 CRLF 行尾必须仍然通过；
        I5. 全部破坏性验证结束后仓库内许可证必须仍然通过。

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

Write-Host '=== 许可证门 ===' -ForegroundColor Cyan
$licenseGate = Join-Path $PSScriptRoot 'verify-licenses.ps1'

$exit = Invoke-Gate '许可证完整' $licenseGate @('-PackageDir', $appDir)
Test-Check 'H1 许可证齐全时通过' ($exit -eq 0) "exit=$exit"

$licenseFile = Join-Path $appDir 'licenses\LGPL-3.0.txt'
$licenseBackup = [System.IO.File]::ReadAllBytes($licenseFile)
[System.IO.File]::AppendAllText($licenseFile, "`n<!-- tampered -->`n")
$exit = Invoke-Gate '篡改许可证' $licenseGate @('-PackageDir', $appDir)
Test-Check 'H2 许可证被篡改时必须失败' ($exit -ne 0) "exit=$exit"
[System.IO.File]::WriteAllBytes($licenseFile, $licenseBackup)

$exit = Invoke-Gate '许可证还原后' $licenseGate @('-PackageDir', $appDir)
Test-Check 'H3 还原后恢复通过' ($exit -eq 0) "exit=$exit"

# H4. 删掉一份 PDFium 许可证材料（漏发一份第三方许可证）
$pdfiumLicenseDir = Join-Path $appDir 'licenses\pdfium'
$missingLicense = Join-Path $pdfiumLicenseDir 'libjpeg_turbo.ijg'
$missingBackup = Join-Path $workRoot 'libjpeg_turbo.ijg.bak'
if (Test-Path -LiteralPath $missingLicense) {
    Copy-Item -LiteralPath $missingLicense -Destination $missingBackup -Force
    Remove-Item -LiteralPath $missingLicense -Force
    $exit = Invoke-Gate '缺少 PDFium 许可证' $licenseGate @('-PackageDir', $appDir)
    Test-Check 'H4 缺少一份 PDFium 许可证时必须失败' ($exit -ne 0) "exit=$exit"
    Copy-Item -LiteralPath $missingBackup -Destination $missingLicense -Force
} else {
    Test-Check 'H4 缺少一份 PDFium 许可证时必须失败' $false '包内没有可删除的 PDFium 许可证样本'
}

# H5. 篡改一份 PDFium 许可证材料
$pdfiumLicenseFile = Join-Path $pdfiumLicenseDir 'pdfium.txt'
if (Test-Path -LiteralPath $pdfiumLicenseFile) {
    $pdfiumBackup = [System.IO.File]::ReadAllBytes($pdfiumLicenseFile)
    [System.IO.File]::AppendAllText($pdfiumLicenseFile, "`n<!-- tampered -->`n")
    $exit = Invoke-Gate '篡改 PDFium 许可证' $licenseGate @('-PackageDir', $appDir)
    Test-Check 'H5 PDFium 许可证被篡改时必须失败' ($exit -ne 0) "exit=$exit"
    [System.IO.File]::WriteAllBytes($pdfiumLicenseFile, $pdfiumBackup)
} else {
    Test-Check 'H5 PDFium 许可证被篡改时必须失败' $false '包内没有可篡改的 PDFium 许可证样本'
}

# H6. 塞进一份清单之外的文件（多发一份未登记的许可证）
$extraLicense = Join-Path $pdfiumLicenseDir 'unlisted-license.txt'
[System.IO.File]::WriteAllText($extraLicense, 'unlisted', (New-Object System.Text.UTF8Encoding($false)))
$exit = Invoke-Gate '多出未登记的许可证' $licenseGate @('-PackageDir', $appDir)
Test-Check 'H6 出现清单之外的许可证时必须失败' ($exit -ne 0) "exit=$exit"
Remove-Item -LiteralPath $extraLicense -Force

$exit = Invoke-Gate '许可证还原后（PDFium）' $licenseGate @('-PackageDir', $appDir)
Test-Check 'H7 还原后 PDFium 许可证校验恢复通过' ($exit -eq 0) "exit=$exit"

# H8. 包内许可证改成 CRLF 行尾仍应通过：行尾不是许可证内容的一部分，
# 归一化之后，包的哈希就不该随打包机器的行尾策略漂移。
$crlfLicenseFile = Join-Path $appDir 'licenses\LGPL-3.0.txt'
$crlfBackup = [System.IO.File]::ReadAllBytes($crlfLicenseFile)
$crlfText = ([System.Text.Encoding]::UTF8.GetString($crlfBackup) -replace "`r`n", "`n") -replace "`n", "`r`n"
[System.IO.File]::WriteAllBytes($crlfLicenseFile, [System.Text.Encoding]::UTF8.GetBytes($crlfText))
$exit = Invoke-Gate '包内许可证改 CRLF' $licenseGate @('-PackageDir', $appDir)
Test-Check 'H8 包内许可证改成 CRLF 时仍然通过' ($exit -eq 0) "exit=$exit"
[System.IO.File]::WriteAllBytes($crlfLicenseFile, $crlfBackup)

Write-Host '=== 取件门 ===' -ForegroundColor Cyan
$fetchGate = Join-Path $PSScriptRoot 'fetch-licenses.ps1'
$repoLicenseDir = Join-Path $root 'licenses'

$exit = Invoke-Gate '仓库许可证完整' $fetchGate @('-VerifyOnly', '-LicenseDir', $repoLicenseDir)
Test-Check 'I1 仓库内许可证齐全时通过' ($exit -eq 0) "exit=$exit"

$repoLicenseFile = Join-Path $repoLicenseDir 'GCC-RUNTIME-LIBRARY-EXCEPTION.txt'
$repoLicenseBackup = [System.IO.File]::ReadAllBytes($repoLicenseFile)
[System.IO.File]::AppendAllText($repoLicenseFile, "`n<!-- tampered -->`n")
$exit = Invoke-Gate '篡改仓库许可证' $fetchGate @('-VerifyOnly', '-LicenseDir', $repoLicenseDir)
Test-Check 'I2 仓库内许可证被篡改时必须失败' ($exit -ne 0) "exit=$exit"
[System.IO.File]::WriteAllBytes($repoLicenseFile, $repoLicenseBackup)

$exit = Invoke-Gate '仓库许可证还原后' $fetchGate @('-VerifyOnly', '-LicenseDir', $repoLicenseDir)
Test-Check 'I3 还原后恢复通过' ($exit -eq 0) "exit=$exit"

# I4. 仓库内许可证改成 CRLF 仍应通过：这条正是远端 CI 曾经失败的场景
# （runner 检出 LF、本地工作区是 CRLF，清单里只能记一个哈希）。
$repoCrlfText = ([System.Text.Encoding]::UTF8.GetString($repoLicenseBackup) -replace "`r`n", "`n") -replace "`n", "`r`n"
[System.IO.File]::WriteAllBytes($repoLicenseFile, [System.Text.Encoding]::UTF8.GetBytes($repoCrlfText))
$exit = Invoke-Gate '仓库许可证改 CRLF' $fetchGate @('-VerifyOnly', '-LicenseDir', $repoLicenseDir)
Test-Check 'I4 仓库内许可证改成 CRLF 时仍然通过' ($exit -eq 0) "exit=$exit"
[System.IO.File]::WriteAllBytes($repoLicenseFile, $repoLicenseBackup)

$exit = Invoke-Gate '仓库许可证最终还原后' $fetchGate @('-VerifyOnly', '-LicenseDir', $repoLicenseDir)
Test-Check 'I5 全部破坏性验证结束后仍然通过' ($exit -eq 0) "exit=$exit"

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
