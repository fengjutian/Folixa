# v0.12.0 隐私策略的界面验收。
#
# 两个场景，都在真实界面里点：
#   1. 本地模式 + 已配云端密钥：文档内容不出本机——不写云端审计、不弹上传预览；
#   2. 片段云端 + 已配密钥：请求发出前必须弹预览，点「取消」后请求不发出，
#      但审计要记一条「没发出去」。
#
# 密钥是临时写进凭据管理器的假密钥（sk-acceptance-fake-…），脚本结束时删除。
# 配置与审计落在临时目录，不碰真实用户数据。

[CmdletBinding()]
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [string]$ScreenshotPath = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

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

$exe = Join-Path $BuildDir 'AiPdfEditor.exe'
Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $exe) $exe

$dataDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-privacy-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
$env:AIPDF_DATA_DIR = $dataDir
$env:AIPDF_TRACE_FILE = Join-Path $dataDir 'trace.log'
$env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
$env:AIPDF_RECOVERY_DIR = Join-Path $dataDir 'recovery'
$env:AIPDF_INDEX_DIR = Join-Path $dataDir 'index'
$configPath = Join-Path $dataDir 'ai/config.json'
$auditPath = Join-Path $dataDir 'ai/audit.jsonl'
$credTarget = 'Folixa/ai/openai/api-key'

function Write-AiConfig {
    param([string]$Mode)
    $config = [ordered]@{
        version               = 1
        provider              = 'openai'
        baseUrl               = 'http://127.0.0.1:9/v1'
        model                 = 'gpt-4o-mini'
        inputPricePerMillion  = 0.15
        outputPricePerMillion = 0.6
        timeoutMs             = 3000
        dataMode              = $Mode
    } | ConvertTo-Json
    New-Item -ItemType Directory -Path (Split-Path -Parent $configPath) -Force | Out-Null
    Set-Content -LiteralPath $configPath -Value $config -Encoding UTF8
}

function Start-AcceptanceApp {
    $app = Start-Process -FilePath $exe -PassThru
    $window = $null
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        if ($app.HasExited) { break }
        $window = Get-AiPdfWindowByTitle -Process $app -TitlePattern 'Folixa'
        if ($window) { break }
        Start-Sleep -Milliseconds 300
    }
    return $app
}

function Open-AiPanel {
    param([System.Diagnostics.Process]$App)
    $floating = Wait-AiPdfElementById -Process $App -IdPattern '*aiFloatingButton*' -TimeoutSeconds 10
    if (-not $floating) { return $false }
    Invoke-AiPdfElement -Element $floating | Out-Null
    Start-Sleep -Milliseconds 600
    foreach ($element in (Get-AiPdfWindowElements -Process $App)) {
        try {
            if ($element.Current.Name -eq '开始聊天' -and
                $element.Current.ControlType.ProgrammaticName -eq 'ControlType.Button') {
                Invoke-AiPdfElement -Element $element | Out-Null
                return ($null -ne (Wait-AiPdfElementById -Process $App -IdPattern '*aiPromptInput*' -TimeoutSeconds 10))
            }
        } catch {
            # 元素可能刚好失效。
        }
    }
    return $false
}

function Ask-CurrentPage {
    param([System.Diagnostics.Process]$App, [string]$Question)
    $input = Wait-AiPdfElementById -Process $App -IdPattern '*aiPromptInput*' -TimeoutSeconds 10
    if (-not $input) { return $false }
    Set-AiPdfElementText -Element $input -Text $Question | Out-Null
    $send = Wait-AiPdfElementById -Process $App -IdPattern '*aiButton*' -TimeoutSeconds 5
    if (-not $send) { return $false }
    Invoke-AiPdfElement -Element $send | Out-Null
    return $true
}

function Stop-AcceptanceApp {
    param([System.Diagnostics.Process]$App)
    if ($App -and -not $App.HasExited) {
        $App.CloseMainWindow() | Out-Null
        if (-not $App.WaitForExit(5000)) { $App.Kill() }
    }
    Get-Process PdfAiWorker -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
}

# 假密钥：只用来让「已配置云端」这条分支成立，值本身没有意义。
cmdkey /generic:$credTarget /user:apikey /pass:sk-acceptance-fake-000000000000 | Out-Null

$app = $null
try {
    # ---------------- 场景 1：本地模式 ----------------
    Write-AiConfig -Mode 'local'
    Reset-AiPdfTrace -Path $env:AIPDF_TRACE_FILE
    $app = Start-AcceptanceApp
    Test-Check '主程序已启动' (-not $app.HasExited)
    Test-Check 'AI 面板已打开' (Open-AiPanel -App $app)

    $asked = Ask-CurrentPage -App $app -Question '这一页讲了什么？'
    Test-Check '已发出提问' $asked
    Test-Check '本地模式仍然给出回答' (Wait-AiPdfTraceMatch -Path $env:AIPDF_TRACE_FILE `
                -Pattern 'ai request id=.+' -TimeoutSeconds 20)
    Start-Sleep -Seconds 3
    $trace = Get-AiPdfTraceText -Path $env:AIPDF_TRACE_FILE
    Test-Check '本地模式没有走云端（无 upload 记录）' (-not ($trace -match 'ai upload')) $trace
    Test-Check '本地模式不写云端审计' (-not (Test-Path -LiteralPath $auditPath))

    if ($ScreenshotPath) {
        Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath | Out-Null
    }
    Stop-AcceptanceApp -App $app
    $app = $null

    # ---------------- 场景 2：片段云端 + 取消 ----------------
    Write-AiConfig -Mode 'chunks'
    Reset-AiPdfTrace -Path $env:AIPDF_TRACE_FILE
    $app = Start-AcceptanceApp
    Test-Check '第二次启动成功' (-not $app.HasExited)
    Test-Check 'AI 面板已打开（云端模式）' (Open-AiPanel -App $app)

    $asked = Ask-CurrentPage -App $app -Question '这一页讲了什么？'
    Test-Check '已发出提问（云端模式）' $asked

    # 上传预览必须在请求真正发出前出现。
    $dialog = $null
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        $dialog = Get-AiPdfWindowByTitle -Process $app -TitlePattern '发送到云端之前'
        if ($dialog) { break }
        Start-Sleep -Milliseconds 300
    }
    Test-Check '云端请求前弹出上传预览' ($null -ne $dialog)

    if ($dialog) {
        $cancel = $null
        foreach ($element in $dialog.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                [System.Windows.Automation.Condition]::TrueCondition)) {
            try {
                if ($element.Current.AutomationId -like '*uploadPreviewCancel*') { $cancel = $element; break }
            } catch {
            }
        }
        Test-Check '预览里有「取消」按钮' ($null -ne $cancel)
        if ($cancel) { Invoke-AiPdfElement -Element $cancel | Out-Null }
    }

    Test-Check '取消后请求没有发出' (Wait-AiPdfTraceMatch -Path $env:AIPDF_TRACE_FILE `
                -Pattern 'ai upload cancelled' -TimeoutSeconds 10)
    Start-Sleep -Seconds 1
    $audit = ''
    if (Test-Path -LiteralPath $auditPath) {
        $audit = Get-Content -LiteralPath $auditPath -Raw -Encoding UTF8
    }
    Test-Check '审计记下了「没发出去」' ($audit -match '"allowed":false') $audit

    if ($ScreenshotPath) {
        Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath | Out-Null
    }
} finally {
    cmdkey /delete:$credTarget 2>&1 | Out-Null
    if ($app) { Stop-AcceptanceApp -App $app }
    Remove-Item -LiteralPath $dataDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item Env:AIPDF_DATA_DIR, Env:AIPDF_TRACE_FILE, Env:AIPDF_RECOVERY_DIR, Env:AIPDF_INDEX_DIR -ErrorAction SilentlyContinue
}

if ($script:Failures -gt 0) {
    Write-Host "隐私验收失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host '隐私验收通过：本地模式不出网、云端请求可预览可取消、审计如实记录' -ForegroundColor Green
exit 0
