<#
.SYNOPSIS
    界面语言验收（中英文）。

.DESCRIPTION
    验证四种情况，全部走真实进程 + UI Automation，不看源码：

    1. 中文（源语言）：界面文案是中文；
    2. 英文：翻译目录生效，界面文案是英文；
    3. 记住选择：设置里写了 ui/language=en 时，不带环境变量启动也是英文；
    4. 覆盖率：源码里每条 tr() 都有英文翻译（scripts/check-i18n.py）。

    环境变量 AIPDF_UI_LANGUAGE 是最高优先级，不写进设置（见 docs/I18N.md）；
    第 3 种情况要临时把它摘掉，测完再写回中文，别影响其它验收脚本。

.PARAMETER BuildDir
    含 AiPdfEditor.exe 的目录，默认 build/windows-debug/bin。
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [int]$StartupSeconds = 4
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')

$script:Failures = 0
function Test-Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    if ($Ok) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name" -ForegroundColor Red
        if ($Detail) { Write-Host "      $Detail" -ForegroundColor DarkGray }
        $script:Failures++
    }
}

$buildPath = (Resolve-Path (Join-Path $root $BuildDir)).Path
$appExe = Join-Path $buildPath 'AiPdfEditor.exe'
Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $appExe) $appExe
if ($script:Failures -gt 0) { exit 2 }

$probeRoot = Join-Path $env:TEMP 'ai-pdf-editor-probe'
$settingsFile = Join-Path $probeRoot 'AI PDF Editor Project/AI PDF Editor.ini'
$tracePath = Join-Path $probeRoot 'i18n-trace.log'

function Start-App {
    param([string]$Language)
    Reset-AiPdfTrace -Path $tracePath
    $env:AIPDF_TRACE_FILE = $tracePath
    if ($Language) {
        $env:AIPDF_UI_LANGUAGE = $Language
    } else {
        Remove-Item Env:\AIPDF_UI_LANGUAGE -ErrorAction SilentlyContinue
    }
    return Start-Process -FilePath $appExe -WorkingDirectory $buildPath -PassThru
}

function Stop-App {
    param([System.Diagnostics.Process]$Process)
    if ($null -eq $Process -or $Process.HasExited) { return }
    $Process.CloseMainWindow() | Out-Null
    Start-Sleep -Milliseconds 1000
    if (-not $Process.HasExited) { Stop-Process -Id $Process.Id -Force }
}

function Write-LanguageSetting {
    param([string]$Language)
    $dir = Split-Path -Parent $settingsFile
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    "[ui]`nlanguage=$Language`n" | Set-Content -LiteralPath $settingsFile -Encoding utf8
}

$app = $null
try {
    # ---------------------------------------------------------------- 中文 ----
    Write-Host '=== 中文（源语言）===' -ForegroundColor Cyan
    $app = Start-App -Language 'zh_CN'
    Start-Sleep -Seconds $StartupSeconds
    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 15
    Test-Check '主窗口可访问' ($null -ne $probe)
    Test-Check '欢迎页是中文' (Wait-AiPdfProcessTextMatch -Process $app -Pattern '打开 PDF' -TimeoutSeconds 10)
    Test-Check '状态栏是中文' (Wait-AiPdfProcessTextMatch -Process $app -Pattern '就绪|缓存\s*\d' -TimeoutSeconds 10)
    Test-Check '没有混入英文按钮' (-not (Wait-AiPdfProcessTextMatch -Process $app -Pattern 'Open PDF' -TimeoutSeconds 2))
    Stop-App -Process $app
    $app = $null

    # ---------------------------------------------------------------- 英文 ----
    Write-Host '=== English ===' -ForegroundColor Cyan
    $app = Start-App -Language 'en'
    Start-Sleep -Seconds $StartupSeconds
    $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 15
    Test-Check '主窗口可访问' ($null -ne $probe)
    Test-Check '欢迎页是英文' (Wait-AiPdfProcessTextMatch -Process $app -Pattern 'Open PDF' -TimeoutSeconds 10)
    Test-Check '拖放区是英文' (Wait-AiPdfProcessTextMatch -Process $app -Pattern 'Drop a PDF here' -TimeoutSeconds 10)
    Test-Check '状态栏是英文' (Wait-AiPdfProcessTextMatch -Process $app -Pattern 'Ready|Cache\s*\d' -TimeoutSeconds 10)
    Test-Check '最近文档标题是英文' (Wait-AiPdfProcessTextMatch -Process $app -Pattern 'Recently opened' -TimeoutSeconds 10)
    Test-Check '没有混入中文按钮' (-not (Wait-AiPdfProcessTextMatch -Process $app -Pattern '托到这里|最近打开' -TimeoutSeconds 2))
    Stop-App -Process $app
    $app = $null

    # -------------------------------------------------------- 记住用户选择 ----
    Write-Host '=== 记住选择（设置里写 en，不带环境变量）===' -ForegroundColor Cyan
    Write-LanguageSetting -Language 'en'
    $app = Start-App -Language $null
    Start-Sleep -Seconds $StartupSeconds
    Test-Check '按设置里的语言启动（英文）' `
        (Wait-AiPdfProcessTextMatch -Process $app -Pattern 'Open PDF' -TimeoutSeconds 15)
    Stop-App -Process $app
    $app = $null
} finally {
    Stop-App -Process $app
    # 探针设置目录是共用的：把语言写回中文，别让后面的验收脚本跑到英文界面。
    Write-LanguageSetting -Language 'zh_CN'
}

# ---------------------------------------------------------------- 覆盖率 ----
Write-Host '=== 翻译覆盖率 ===' -ForegroundColor Cyan
& (Join-Path $root 'scripts/check-i18n.py') --src (Join-Path $root 'src') `
    --ts (Join-Path $root 'src/i18n/ai-pdf-editor_en.ts') | Out-Host
Test-Check '源码里的文案都有英文翻译' ($LASTEXITCODE -eq 0)

if ($script:Failures -gt 0) {
    Write-Host "`n$script:Failures 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n全部通过" -ForegroundColor Green
exit 0

