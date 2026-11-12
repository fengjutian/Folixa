<#
.SYNOPSIS
    发布门：CI 工作流必须与仓库里的真实内容保持一致。

.DESCRIPTION
    CI 配置最容易悄悄烂掉：脚本改名了、preset 没了、版本号写死在工作流里，
    这些都要等到远端跑失败才发现。本门在本地检查：
      1. 工作流引用的 scripts\*.ps1 必须真实存在；
      2. 工作流引用的 CMake preset 必须在 CMakePresets.json 中存在；
      3. 工作流里不允许出现硬编码的版本号（版本由 CMakeLists 决定）；
      4. 工作流必须调用单一入口 scripts/ci.ps1；
      5. 上传的制品路径必须是流水线真实产出的。

    任何一项不满足即返回非零退出码。
#>
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..'),
    [string]$WorkflowPath = ''
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

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
if (-not $WorkflowPath) {
    $WorkflowPath = Join-Path $root '.github\workflows\windows.yml'
}
if (-not (Test-Path -LiteralPath $WorkflowPath)) {
    Write-Host "找不到工作流: $WorkflowPath" -ForegroundColor Red
    exit 2
}

$workflow = Get-Content -LiteralPath $WorkflowPath -Raw -Encoding UTF8
$presetText = Get-Content -LiteralPath (Join-Path $root 'CMakePresets.json') -Raw -Encoding UTF8
$presets = [regex]::Matches($presetText, '"name"\s*:\s*"([^"]+)"') | ForEach-Object { $_.Groups[1].Value }

# 1. 工作流引用的脚本必须存在
$referencedScripts = [regex]::Matches($workflow, 'scripts[\\/][A-Za-z0-9_.-]+\.ps1') |
    ForEach-Object { $_.Value -replace '\\', '/' } | Sort-Object -Unique
if ($referencedScripts.Count -eq 0) {
    Write-Host '工作流没有引用任何脚本，检查逻辑是否失效' -ForegroundColor Red
    exit 2
}
foreach ($reference in $referencedScripts) {
    $relative = $reference -replace '/', '\'
    Test-Check "工作流引用的脚本存在: $reference" (Test-Path -LiteralPath (Join-Path $root $relative))
}

# 2. 工作流引用的 preset 必须存在
$referencedPresets = [regex]::Matches($workflow, '--preset[= ]+([A-Za-z0-9_-]+)') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
foreach ($preset in $referencedPresets) {
    Test-Check "工作流引用的 preset 存在: $preset" ($presets -contains $preset)
}

# 3. 工作流里不允许硬编码版本号
$hardcoded = [regex]::Matches($workflow, '(?<![\w.-])[0-9]+\.[0-9]+\.[0-9]+(?![\w.-])') |
    ForEach-Object { $_.Value } | Sort-Object -Unique
Test-Check '工作流没有硬编码版本号' ($hardcoded.Count -eq 0) ($hardcoded -join ', ')

# 4. 必须走单一入口
Test-Check '工作流调用单一入口 scripts/ci.ps1' ($workflow -match 'scripts[\\/]ci\.ps1')

# 5. 制品路径必须是流水线真实产出的
Test-Check '上传的安装包路径正确' ($workflow -match 'dist/\*\.zip')
Test-Check '上传的验收证据路径正确' ($workflow -match 'build/acceptance/')

if ($script:Failures -gt 0) {
    Write-Host "`nCI 工作流校验失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`nCI 工作流校验通过：工作流与仓库内容一致（$($referencedScripts.Count) 个脚本，$($referencedPresets.Count) 个 preset）" -ForegroundColor Green
exit 0
