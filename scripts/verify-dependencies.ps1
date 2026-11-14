<#
.SYNOPSIS
    依赖登记与固定版本的发布门。

.DESCRIPTION
    规则来自 docs/DEPENDENCY_POLICY.md 第 1、2、7 条。这个门是静态检查：
    不联网、不下载，抓的是「登记与实现漂移」和「包内出现未登记二进制」。

      1. THIRD_PARTY_LICENSES.md 的每一行都有精确版本、关系、许可证与更新路径；
      2. 文件里的机器可读 pins 与构建脚本一致（Qt / MinGW / PDFium）；
      3. vcpkg 的 builtin-baseline 是 40 位提交哈希，依赖没有浮动版本；
      4. bootstrap-pdfium.ps1 固定了版本、归档 SHA-256 与 API 版本；
      5. 仓库不提交二进制，也不夹带第三方商业应用的产物；
      6. 给了 -PackagePath / -PackageDir 时，包内每个二进制都能归到登记表里的组件。

    它不替代漏洞数据库比对：CVE 复核的结论与日期记在 docs/SECURITY_REVIEW.md，
    本门只保证「复核对象没有在复核之后被悄悄换掉」。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\verify-dependencies.ps1
    powershell -ExecutionPolicy Bypass -File scripts\verify-dependencies.ps1 `
        -PackagePath dist\ai-pdf-editor-0.15.0-win64.zip
#>
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..'),
    # 安装包 ZIP；也可以改用 -PackageDir 指向已解压的目录。
    [string]$PackagePath = '',
    [string]$PackageDir = ''
)

$ErrorActionPreference = 'Stop'

$script:Failures = 0
function Test-Check {
    param([string]$Name, [bool]$Condition, [string]$Detail = '')
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

function Get-FileText {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8
}

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$registerPath = Join-Path $root 'THIRD_PARTY_LICENSES.md'
$register = Get-FileText $registerPath
Test-Check '依赖登记表存在' (Test-Path -LiteralPath $registerPath) $registerPath
if (-not $register) {
    Write-Host "`n依赖门失败：登记表不可读" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- pins ----
Write-Host '=== 机器可读 pins ===' -ForegroundColor Cyan
$pinBlock = [regex]::Match($register, '(?s)<!--\s*dependency-pins\s*(?<body>.*?)-->')
Test-Check '登记表包含 dependency-pins 块' $pinBlock.Success
$script:Pins = @{}
if ($pinBlock.Success) {
    foreach ($line in ($pinBlock.Groups['body'].Value -split "`r?`n")) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^([a-z0-9-]+)=(.+)$') {
            $script:Pins[$matches[1]] = $matches[2].Trim()
        }
    }
}
foreach ($required in @('qt', 'mingw', 'pdfium', 'pdfium-api')) {
    Test-Check "pin 存在：$required" $script:Pins.ContainsKey($required)
}

# --------------------------------------------------------------- table ----
Write-Host '=== 登记表行 ===' -ForegroundColor Cyan
$rows = New-Object System.Collections.Generic.List[object]
foreach ($line in ($register -split "`r?`n")) {
    if ($line -notmatch '^\s*\|') { continue }
    $cells = @($line.Trim().Trim('|') -split '\|' | ForEach-Object { $_.Trim() })
    if ($cells.Count -lt 6) { continue }
    if ($cells[0] -eq 'Component' -or $cells[0] -match '^-+$') { continue }
    if (@($cells | Where-Object { $_ -notmatch '^-+$' }).Count -eq 0) { continue }
    $rows.Add([pscustomobject]@{
        Component    = $cells[0]
        Version      = $cells[1]
        Relationship = $cells[2]
        Use          = $cells[3]
        License      = $cells[4]
        Update       = $cells[5]
    })
}
Test-Check '登记表至少 4 行有效组件' ($rows.Count -ge 4) "rows=$($rows.Count)"

foreach ($row in $rows) {
    $label = if ($row.Component) { $row.Component } else { '(空组件名)' }
    $isOptional = $row.Relationship -match '^Optional'
    Test-Check "$label 版本行不是占位符" ($row.Version -notmatch '(?i)pending|tbd|latest|unpinned|to be') $row.Version
    Test-Check "$label 关系已声明" ($row.Relationship -match '^(Direct|Transitive|Optional|Build-time)') $row.Relationship
    Test-Check "$label 许可证已声明" ($row.License -match '(?i)license|lgpl|gpl|bsd|apache|mit|unicode|public domain|商业') $row.License
    Test-Check "$label 更新路径已声明" ($row.Update -match '(?i)update|bump|follow|move|user-managed|升级|更新|跟随') $row.Update
    if ($isOptional) {
        Test-Check "$label 可选依赖明确不随包分发" ($row.Version -match '(?i)not bundled|可选|runtime') $row.Version
    } else {
        Test-Check "$label 非可选依赖有精确版本" ($row.Version -match '[0-9]') $row.Version
    }
}

foreach ($required in @('Qt', 'MinGW-w64', 'PDFium', 'Tesseract')) {
    Test-Check "登记表包含 $required" (@($rows | Where-Object { $_.Component -eq $required }).Count -eq 1)
}

# ------------------------------------------------------- build scripts ----
Write-Host '=== 构建脚本与 pins 一致 ===' -ForegroundColor Cyan
$cmakeToolchain = Get-FileText (Join-Path $root 'cmake\toolchains\qt-mingw.cmake')
$bootstrapToolchain = Get-FileText (Join-Path $root 'scripts\bootstrap-toolchain.ps1')
$bootstrapPdfium = Get-FileText (Join-Path $root 'scripts\bootstrap-pdfium.ps1')

if ($script:Pins.ContainsKey('qt')) {
    $qt = [regex]::Escape($script:Pins['qt'])
    Test-Check "toolchain 固定 Qt $($script:Pins['qt'])" ($cmakeToolchain -match "C:/Qt/$qt/mingw_64")
    Test-Check "bootstrap-toolchain 固定 Qt $($script:Pins['qt'])" ($bootstrapToolchain -match "\`$QtVersion\s*=\s*'$qt'")
    $qtRow = @($rows | Where-Object { $_.Component -eq 'Qt' })[0]
    Test-Check '登记表的 Qt 行包含同一版本' ($null -ne $qtRow -and $qtRow.Version -match $qt) $qtRow.Version
}
if ($script:Pins.ContainsKey('mingw')) {
    $mingw = [regex]::Escape($script:Pins['mingw'])
    Test-Check "bootstrap-toolchain 固定 MinGW $($script:Pins['mingw'])" ($bootstrapToolchain -match $mingw)
    Test-Check 'toolchain 使用 mingw1310 套件' ($cmakeToolchain -match 'mingw1310_64')
}
if ($script:Pins.ContainsKey('pdfium')) {
    $pdfium = [regex]::Escape($script:Pins['pdfium'])
    Test-Check "bootstrap-pdfium 固定版本 $($script:Pins['pdfium'])" ($bootstrapPdfium -match "\`$Version\s*=\s*'$pdfium'")
    $shaMatch = [regex]::Match($bootstrapPdfium, "(?m)\`$Sha256\s*=\s*'(?<sha>[0-9a-fA-F]{64})'")
    Test-Check 'bootstrap-pdfium 固定了 64 位 SHA-256' $shaMatch.Success
}
if ($script:Pins.ContainsKey('pdfium-api')) {
    $api = [regex]::Escape($script:Pins['pdfium-api'])
    Test-Check "bootstrap-pdfium 固定 API $($script:Pins['pdfium-api'])" ($bootstrapPdfium -match "\`$ExpectedApiVersion\s*=\s*'MAJOR=$api'")
}

# -------------------------------------------------------------- vcpkg ----
Write-Host '=== vcpkg 清单 ===' -ForegroundColor Cyan
$vcpkgPath = Join-Path $root 'vcpkg.json'
$vcpkgText = Get-FileText $vcpkgPath
Test-Check 'vcpkg 清单存在' ($vcpkgText -ne '')
if ($vcpkgText) {
    $manifest = $vcpkgText | ConvertFrom-Json
    $baseline = [string]$manifest.'builtin-baseline'
    Test-Check 'builtin-baseline 是 40 位提交哈希' ($baseline -match '^[0-9a-f]{40}$') "baseline=$baseline"
    foreach ($dependency in @($manifest.dependencies)) {
        if ($null -eq $dependency) { continue }
        $name = if ($dependency -is [string]) { $dependency } else { [string]$dependency.name }
        if ($dependency -is [string]) {
            Test-Check "vcpkg 依赖 $name 具有精确版本" $false '仅声明名称，没有 pin'
            continue
        }
        $version = [string]$dependency.'version>='
        Test-Check "vcpkg 依赖 $name 具有精确版本" ($version -ne '' -and $version -notmatch '[xX*]') $version
    }
}

# --------------------------------------------------------------- files ----
Write-Host '=== 仓库内二进制与商业产物 ===' -ForegroundColor Cyan
$forbiddenExtensions = @('.dll', '.exe', '.lib', '.a', '.sys', '.pdb', '.msi', '.cab')
if (Test-Path -LiteralPath (Join-Path $root '.git')) {
    Push-Location $root
    try {
        $tracked = @(& git ls-files)
        if ($LASTEXITCODE -ne 0) { throw 'git ls-files 失败' }
        $violations = @($tracked | Where-Object {
            $lower = $_.ToLowerInvariant()
            ($forbiddenExtensions | Where-Object { $lower.EndsWith($_) }).Count -gt 0
        })
        Test-Check '仓库没有提交二进制文件' ($violations.Count -eq 0) ($violations -join ', ')
        $commercial = @($tracked | Where-Object { $_ -match '(^|/)(fox|Foxit PDF Editor)(/|$)' -or $_ -match '(?i)foxit.*\.(dll|exe|ocx|pdf)$' })
        Test-Check '仓库没有夹带第三方商业应用产物' ($commercial.Count -eq 0) ($commercial -join ', ')
    } finally {
        Pop-Location
    }
} else {
    Write-Host '    （非 git 仓库，跳过已跟踪文件检查）' -ForegroundColor DarkGray
}

# ------------------------------------------------------------- package ----
$packageEntries = @()
if ($PackagePath) {
    if (-not (Test-Path -LiteralPath $PackagePath)) {
        Test-Check '安装包存在' $false $PackagePath
    } else {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $PackagePath).Path)
        try {
            $packageEntries = @($archive.Entries | ForEach-Object { $_.FullName -replace '\\', '/' })
        } finally {
            $archive.Dispose()
        }
    }
} elseif ($PackageDir) {
    if (-not (Test-Path -LiteralPath $PackageDir)) {
        Test-Check '安装包目录存在' $false $PackageDir
    } else {
        $packageEntries = @(Get-ChildItem -LiteralPath $PackageDir -Recurse -File |
            ForEach-Object { $_.FullName.Substring((Resolve-Path -LiteralPath $PackageDir).Path.Length).TrimStart('\', '/') -replace '\\', '/' })
    }
}

if ($packageEntries.Count -gt 0) {
    Write-Host '=== 包内二进制归属 ===' -ForegroundColor Cyan
    $binaryEntries = @($packageEntries | Where-Object { $_ -match '\.(dll|exe)$' })
    $owners = [ordered]@{
        '^AiPdfEditor\.exe$'                                                            = 'project'
        '^PdfAiWorker\.exe$'                                                            = 'project'
        '^pdfium\.dll$'                                                                 = 'PDFium'
        '^Qt6[A-Za-z0-9_]+\.dll$'                                                       = 'Qt'
        '^(generic|iconengines|imageformats|networkinformation|platforms|styles|tls)/[A-Za-z0-9_]+\.dll$' = 'Qt'
        '^libgcc_s_seh-1\.dll$'                                                         = 'MinGW-w64'
        '^libstdc\+\+-6\.dll$'                                                          = 'MinGW-w64'
        '^libwinpthread-1\.dll$'                                                        = 'MinGW-w64'
    }
    $unowned = New-Object System.Collections.Generic.List[string]
    $ownerCounts = @{}
    foreach ($entry in $binaryEntries) {
        $leaf = $entry
        # ZIP 里带一层包名目录（ai-pdf-editor-<版本>-win64/），只去掉这一层；
        # 不能把 platforms/、imageformats/ 这类插件目录也当成包名目录吃掉。
        if ($leaf -match '^ai-pdf-editor-[^/]+/(.+)$') { $leaf = $matches[1] }
        $owner = $null
        foreach ($pattern in $owners.Keys) {
            if ($leaf -match $pattern) { $owner = $owners[$pattern]; break }
        }
        if (-not $owner) {
            $unowned.Add($leaf)
        } else {
            if (-not $ownerCounts.ContainsKey($owner)) { $ownerCounts[$owner] = 0 }
            $ownerCounts[$owner]++
        }
    }
    Test-Check '包内每个二进制都能归到已登记的组件' ($unowned.Count -eq 0) ($unowned -join ', ')
    foreach ($owner in @('project', 'PDFium', 'Qt', 'MinGW-w64')) {
        $count = if ($ownerCounts.ContainsKey($owner)) { $ownerCounts[$owner] } else { 0 }
        Test-Check "包内包含 $owner 的二进制" ($count -gt 0) "count=$count"
    }
    Test-Check '包内没有第三方商业应用产物' (@($binaryEntries | Where-Object { $_ -match '(?i)foxit' }).Count -eq 0)
}

if ($script:Failures -gt 0) {
    Write-Host "`n依赖门失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n依赖门通过：登记表与固定版本一致" -ForegroundColor Green
exit 0
