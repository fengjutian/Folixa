<#
.SYNOPSIS
    v0.3.0 PDF 能力验收：主程序能打开 PDF、渲染首页并正确报告错误。

.DESCRIPTION
    生成测试 PDF 集合，然后用命令行参数启动主程序直接打开 PDF，
    通过 UI Automation 校验：
      1. 打开普通 PDF → 出现对应标签页、页码信息与状态栏提示；
      2. 打开旋转页 PDF → 页码信息正确；
      3. 打开损坏 PDF → 主程序不崩溃，状态栏给出可见错误；
      4. 打开加密 PDF → 弹出密码框，正确密码可打开、错误密码给出可见错误、
         取消时状态栏提示已取消。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\pdf-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')

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

$buildPath = (Resolve-Path -LiteralPath $BuildDir -ErrorAction SilentlyContinue)
if (-not $buildPath) {
    Write-Host "Build directory not found: $BuildDir" -ForegroundColor Red
    exit 2
}
$buildPath = $buildPath.Path
$appExe = Join-Path $buildPath 'AiPdfEditor.exe'
if (-not (Test-Path -LiteralPath $appExe)) {
    Write-Host "主程序不存在: $appExe" -ForegroundColor Red
    exit 2
}

# 让子进程能加载 Qt、MinGW 与 PDFium 运行库。
$qtRoot = if ($env:AIPDF_QT_ROOT) { $env:AIPDF_QT_ROOT } else { 'C:\Qt\6.8.3\mingw_64' }
$mingwRoot = if ($env:AIPDF_MINGW_ROOT) { $env:AIPDF_MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }
$pdfiumRoot = if ($env:AIPDF_PDFIUM_ROOT) { $env:AIPDF_PDFIUM_ROOT } else { 'C:\Qt\..\tools\pdfium' }
foreach ($extra in @(
    (Join-Path $qtRoot 'bin'),
    (Join-Path $mingwRoot 'bin'),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\pdfium\bin')
)) {
    if (Test-Path -LiteralPath $extra) {
        $env:PATH = "$extra;$env:PATH"
    }
}

Write-Host '==> 生成测试 PDF' -ForegroundColor Cyan
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
$fixtureDir = Join-Path $WorkDir 'fixtures'
& $Python (Join-Path $PSScriptRoot 'generate-test-pdfs.py') $fixtureDir --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host '生成测试 PDF 失败' -ForegroundColor Red
    exit 2
}

function Stop-LeftoverApp {
    <#
    .SYNOPSIS
        清掉上一轮遗留的主程序进程。
    .DESCRIPTION
        连续跑多个场景时，前一个进程若还带着模态密码框活着，
        它的窗口会留在桌面上并抢前台，干扰下一个场景的查找。
    #>
    $leftovers = @(Get-Process -Name 'AiPdfEditor' -ErrorAction SilentlyContinue)
    foreach ($process in $leftovers) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    if ($leftovers.Count -gt 0) {
        Write-Host "    清理遗留主程序进程 $($leftovers.Count) 个" -ForegroundColor DarkGray
        Start-Sleep -Milliseconds 500
    }
    Stop-AiPdfOrphanWorker -Quiet | Out-Null
}

function Invoke-AppWithPdf {
    param(
        [string]$PdfPath,
        [string]$Screenshot = '',
        [int]$WaitSeconds = $StartupSeconds,
        [string]$Password = '',
        [string]$DialogTitlePattern = '',
        [switch]$CancelDialog,
        [string]$ScreenshotTitlePattern = '',
        [string]$DialogScreenshot = '',
        [string]$ExpectTextPattern = '',
        [string]$RequireText = ''
    )

    Stop-LeftoverApp

    # 恢复日志单独放一份：验收改过文档之后，启动时的恢复问询会挡住界面。
    $env:AIPDF_RECOVERY_DIR = Join-Path (Split-Path -Parent $buildPath) 'acceptance-recovery'

    $app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath `
        -ArgumentList $PdfPath -PassThru
    $screenshotTaken = $false
    $dialogSeen = $false
    $passwordEntered = $false
    try {
        Start-Sleep -Seconds $WaitSeconds
        if ($app.HasExited) {
            Write-Host "    主程序已退出，退出码 $($app.ExitCode)" -ForegroundColor Red
            return [pscustomobject]@{
                Process = $app; Probe = $null; Alive = $false
                ScreenshotTaken = $false; DialogSeen = $false; PasswordEntered = $false
                TextMatched = $false
            }
        }

        if ($DialogTitlePattern) {
            # UI Automation 的元素随时可能失效：异常只让本场景判失败，
            # 不应该把整条验收流水线打断。
            try {
                $dialog = Wait-AiPdfDialog -Process $app -TitlePattern $DialogTitlePattern -TimeoutSeconds 15
                $dialogSeen = ($null -ne $dialog)
                if ($dialogSeen -and $DialogScreenshot) {
                    Save-AiPdfWindowScreenshot -Process $app -Path $DialogScreenshot `
                        -WindowTitlePattern $DialogTitlePattern | Out-Null
                }
                if ($dialogSeen -and $CancelDialog) {
                    $dialogSeen = (Invoke-AiPdfDialogButton -Process $app `
                        -ButtonPattern '^(Cancel|取消|Abort)$' -TitlePattern $DialogTitlePattern)
                } elseif ($dialogSeen -and $Password) {
                    $passwordEntered = (Enter-AiPdfDialogPassword -Process $app -Password $Password `
                        -TitlePattern $DialogTitlePattern)
                }
            } catch {
                Write-Host "    对话框交互异常：$($_.Exception.Message)" -ForegroundColor Red
                $dialogSeen = $false
                $passwordEntered = $false
            }
            # 等对话框关掉，再读主窗口。
            Start-Sleep -Seconds 1
        }

        $probe = Wait-AiPdfWindowProbe -Process $app -TimeoutSeconds 15 -TitlePattern 'AI PDF Editor' `
            -RequirePattern $RequireText
        if ($Screenshot) {
            $screenshotTaken = Save-AiPdfWindowScreenshot -Process $app -Path $Screenshot `
                -WindowTitlePattern $ScreenshotTitlePattern
        }
        # 状态栏里「已取消打开」这类提示只保留 5 秒，必须在进程还活着的时候读。
        $textMatched = $true
        if ($ExpectTextPattern) {
            $textMatched = Wait-AiPdfTextMatch -Process $app -Pattern $ExpectTextPattern `
                -TimeoutSeconds 6 -TitlePattern 'AI PDF Editor'
        }
        return [pscustomobject]@{
            Process = $app; Probe = $probe; Alive = $true
            ScreenshotTaken = $screenshotTaken; DialogSeen = $dialogSeen
            PasswordEntered = $passwordEntered; TextMatched = $textMatched
        }
    } finally {
        if (-not $app.HasExited) {
            $app.CloseMainWindow() | Out-Null
            # 带模态对话框时关闭请求可能不生效，别在这里空等十秒。
            if (-not $app.WaitForExit(3000)) {
                $app.Kill() | Out-Null
                $app.WaitForExit(5000) | Out-Null
            }
        }
        Stop-AiPdfOrphanWorker -Quiet | Out-Null
    }
}

Write-Host '==> 打开普通 PDF' -ForegroundColor Cyan
$normal = Join-Path $fixtureDir 'normal.pdf'
$run = Invoke-AppWithPdf -PdfPath $normal -Screenshot $ScreenshotPath -RequireText '已打开\s*normal\.pdf'
Test-Check '主程序保持运行' $run.Alive
if ($run.Probe) {
    Test-Check '出现 normal.pdf 标签页' ($run.Probe.Text -match 'normal\.pdf') ''
    Test-Check '显示页码信息' ($run.Probe.Text -match '第\s*1\s*/\s*1\s*页') ''
    Test-Check '状态栏提示已打开' ($run.Probe.Text -match '已打开') ''
    Test-Check '显示缩放比例' ($run.Probe.Text -match '100%') ''
    Test-Check '未出现渲染失败' ($run.Probe.Text -notmatch '渲染失败') ''
    if ($ScreenshotPath) {
        Test-Check '界面截图已保存' $run.ScreenshotTaken $ScreenshotPath
    }
}

Write-Host '==> 打开旋转页 PDF' -ForegroundColor Cyan
$rotated = Join-Path $fixtureDir 'rotated.pdf'
$run = Invoke-AppWithPdf -PdfPath $rotated -RequireText '已打开\s*rotated\.pdf'
Test-Check '主程序保持运行' $run.Alive
if ($run.Probe) {
    Test-Check '旋转页显示页码信息' ($run.Probe.Text -match '第\s*1\s*/\s*1\s*页') ''
    Test-Check '旋转页未渲染失败' ($run.Probe.Text -notmatch '渲染失败') ''
}

Write-Host '==> 打开损坏 PDF' -ForegroundColor Cyan
$corrupt = Join-Path $fixtureDir 'corrupt.pdf'
$run = Invoke-AppWithPdf -PdfPath $corrupt -RequireText '无法打开\s*corrupt\.pdf'
Test-Check '损坏文件不会导致崩溃' $run.Alive
if ($run.Probe) {
    Test-Check '状态栏给出可见错误' ($run.Probe.Text -match '无法打开') ''
}

Write-Host '==> 打开加密 PDF（正确密码）' -ForegroundColor Cyan
$encrypted = Join-Path $fixtureDir 'encrypted.pdf'
$dialogScreenshot = ''
if ($ScreenshotPath) {
    $dialogScreenshot = [System.IO.Path]::Combine(
        [System.IO.Path]::GetDirectoryName($ScreenshotPath),
        [System.IO.Path]::GetFileNameWithoutExtension($ScreenshotPath) + '-password.png')
}
$run = Invoke-AppWithPdf -PdfPath $encrypted -Password 'secret' `
    -DialogTitlePattern '需要密码' -DialogScreenshot $dialogScreenshot `
    -RequireText '已打开\s*encrypted\.pdf'
Test-Check '主程序保持运行' $run.Alive
Test-Check '弹出密码输入框' $run.DialogSeen
Test-Check '密码已提交' $run.PasswordEntered
if ($run.Probe) {
    Test-Check '加密文档打开成功' ($run.Probe.Text -match '已打开\s*encrypted\.pdf') ''
    Test-Check '加密文档显示页码信息' ($run.Probe.Text -match '第\s*1\s*/\s*1\s*页') ''
    Test-Check '加密文档未出现渲染失败' ($run.Probe.Text -notmatch '渲染失败') ''
}

Write-Host '==> 打开加密 PDF（错误密码）' -ForegroundColor Cyan
$run = Invoke-AppWithPdf -PdfPath $encrypted -Password 'wrong-password' `
    -DialogTitlePattern '需要密码' -ExpectTextPattern '无法打开' `
    -RequireText '无法打开\s*encrypted\.pdf'
Test-Check '错误密码不会导致崩溃' $run.Alive
Test-Check '错误密码给出可见错误' $run.TextMatched ''

Write-Host '==> 打开加密 PDF（取消）' -ForegroundColor Cyan
$run = Invoke-AppWithPdf -PdfPath $encrypted -DialogTitlePattern '需要密码' -CancelDialog `
    -ExpectTextPattern '已取消打开' -RequireText '已取消打开'
Test-Check '取消密码不会导致崩溃' $run.Alive
Test-Check '密码框已取消' $run.DialogSeen
Test-Check '取消后状态栏提示已取消' $run.TextMatched (
    ($run.Probe.Text -split "`n" | Where-Object { $_ -match '取消|无法打开|密码' }) -join ' | ')
if ($run.Probe) {
    # 取消不能留下半开的文档：既没有标签页，也没有成功提示。
    Test-Check '取消后没有打开文档' ($run.Probe.Text -notmatch '已打开\s*encrypted\.pdf') ''
}

Remove-Item -LiteralPath $WorkDir -Recurse -Force -ErrorAction SilentlyContinue

if ($script:Failures -gt 0) {
    Write-Host "`nPDF 验收失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`nPDF 验收通过：打开、渲染与错误处理均符合预期" -ForegroundColor Green
exit 0
