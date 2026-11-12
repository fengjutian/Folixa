<#
.SYNOPSIS
    AI PDF Editor 验收共用探针：读取主窗口文本与截图。

.DESCRIPTION
    被 scripts\verify-m0.ps1 与 scripts\verify-package.ps1 点源引用，
    避免两处验收脚本各写一份 UI Automation 逻辑。
#>

function Get-AiPdfWindowProbe {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process
    )

    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $pidCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $Process.Id)
    $window = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $pidCondition)
    if (-not $window) {
        return $null
    }

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

    return [pscustomobject]@{
        Title = $window.Current.Name
        Text  = ($texts -join "`n")
    }
}

function Save-AiPdfWindowScreenshot {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
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

function Wait-AiPdfWindowProbe {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 15
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return $null
        }
        $probe = Get-AiPdfWindowProbe -Process $Process
        if ($probe -and $probe.Text -match 'ready') {
            return $probe
        }
        Start-Sleep -Milliseconds 400
    }
    return (Get-AiPdfWindowProbe -Process $Process)
}
