<#
.SYNOPSIS
    AI PDF Editor v0.1.0（M0）手工启动验收脚本。

.DESCRIPTION
    启动主程序，验证 Worker 自动拉起、IPC 健康检查、版本显示与退出处理。
    每项检查输出 PASS / FAIL，脚本退出码在全部通过时为 0。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts/verify-m0.ps1
    powershell -ExecutionPolicy Bypass -File scripts/verify-m0.ps1 -BuildDir build/windows-release/bin
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [int]$StartupSeconds = 8,
    [int]$ShutdownTimeoutSeconds = 10,
    [string]$ScreenshotPath = ''
)

$ErrorActionPreference = 'Stop'
$script:Failures = 0

function Save-WindowScreenshot {
    param(
        [System.Diagnostics.Process]$Process,
        [string]$Path
    )

    Add-Type -AssemblyName System.Drawing
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NativeWindow {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint flags);
}
'@

    $handle = $Process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) {
        return $false
    }
    [NativeWindow]::ShowWindow($handle, 9) | Out-Null
    [NativeWindow]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 700

    $rect = New-Object NativeWindow+RECT
    if (-not [NativeWindow]::GetWindowRect($handle, [ref]$rect)) {
        return $false
    }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        return $false
    }

    # 只抓窗口自身内容，避免把桌面上其他窗口截进来。
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    $captured = [NativeWindow]::PrintWindow($handle, $hdc, 2)
    $graphics.ReleaseHdc($hdc)
    if (-not $captured) {
        $graphics.Dispose()
        $bitmap.Dispose()
        return $false
    }
    $directory = Split-Path -Parent $Path
    if ($directory -and -not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    return $true
}

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

# 让子进程能加载 Qt 与 MinGW 运行库，即使 bin 目录里还没有部署 DLL。
$qtRoot = if ($env:AIPDF_QT_ROOT) { $env:AIPDF_QT_ROOT } else { 'C:\Qt\6.8.3\mingw_64' }
$mingwRoot = if ($env:AIPDF_MINGW_ROOT) { $env:AIPDF_MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }
foreach ($extra in @((Join-Path $qtRoot 'bin'), (Join-Path $mingwRoot 'bin'))) {
    if (Test-Path -LiteralPath $extra) {
        $env:PATH = "$extra;$env:PATH"
    }
}

$appExe = Join-Path $buildPath 'AiPdfEditor.exe'
$workerExe = Join-Path $buildPath 'PdfAiWorker.exe'

Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $appExe) $appExe
Test-Check 'Worker 可执行文件与主程序同目录' (Test-Path -LiteralPath $workerExe) $workerExe
if ($script:Failures -gt 0) {
    exit 2
}

Write-Host "启动 $appExe ..." -ForegroundColor Cyan
# 记录启动前已存在的 Worker，避免把历史遗留进程算进本次验收。
$workersBefore = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Id })
$app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath -PassThru
try {
    Start-Sleep -Seconds $StartupSeconds

    Test-Check '主程序进程存活' (-not $app.HasExited) "exit=$($app.ExitCode)"

    $workers = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
        Where-Object { $workersBefore -notcontains $_.Id })
    Test-Check 'Worker 被主程序自动拉起' ($workers.Count -ge 1) "count=$($workers.Count)"

    # 通过 UI Automation 读取界面文本，验证就绪状态与版本显示。
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $pidCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $app.Id)
    $window = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $pidCondition)

    Test-Check '主窗口可被自动化接口访问' ($null -ne $window)
    if ($window) {
        $texts = New-Object System.Collections.Generic.List[string]
        $texts.Add($window.Current.Name)
        $children = $window.FindAll(
            [System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition)
        foreach ($element in $children) {
            $texts.Add($element.Current.Name)
            # QTextBrowser 的正文通过 TextPattern 暴露，不在 Name 里。
            $textPattern = $null
            if ($element.TryGetCurrentPattern(
                    [System.Windows.Automation.TextPattern]::Pattern, [ref]$textPattern)) {
                $document = $textPattern.DocumentRange.GetText(-1)
                if ($document) {
                    $texts.Add($document)
                }
            }
        }
        $all = ($texts -join "`n")

        Test-Check '窗口标题显示版本 0.1.0' ($window.Current.Name -match '0\.1\.0') $window.Current.Name
        Test-Check '界面显示 Worker 就绪' ($all -match 'ready') ''
        Test-Check '健康检查返回协议版本与 Worker 版本' ($all -match 'protocol 1' -and $all -match 'worker 0\.1\.0') ''

        if ($ScreenshotPath) {
            $saved = Save-WindowScreenshot -Process $app -Path $ScreenshotPath
            Test-Check '界面截图已保存' $saved $ScreenshotPath
        }
    }
} finally {
    if (-not $app.HasExited) {
        $app.CloseMainWindow() | Out-Null
        if (-not $app.WaitForExit($ShutdownTimeoutSeconds * 1000)) {
            $app.Kill()
        }
    }
}

Start-Sleep -Seconds 2
$remaining = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue |
    Where-Object { $workersBefore -notcontains $_.Id })
Test-Check '主程序退出后 Worker 随之结束' ($remaining.Count -eq 0) "count=$($remaining.Count)"

if ($script:Failures -gt 0) {
    Write-Host "`n验收失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n验收通过：M0 启动与 IPC 健康检查全部符合预期" -ForegroundColor Green
exit 0
