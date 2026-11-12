<#
.SYNOPSIS
    AI PDF Editor 验收共用探针：读取主窗口文本与截图。

.DESCRIPTION
    被 scripts\verify-m0.ps1、scripts\verify-package.ps1 与 scripts\verify-pdf.ps1 点源引用，
    避免几处验收脚本各写一份 UI Automation 逻辑。

    除主窗口读取外，还提供按标题查找顶层窗口、填写输入框、触发按钮的能力，
    用于自动化模态对话框（例如 PDF 密码输入框）。
#>

function Get-AiPdfProcessWindows {
    <#
    .SYNOPSIS
        返回属于该进程的全部顶层窗口（模态对话框也是顶层窗口）。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process
    )

    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $pidCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $Process.Id)
    # 用 , 包裹，避免集合在返回时被展开成一个个元素。
    return , $root.FindAll([System.Windows.Automation.TreeScope]::Children, $pidCondition)
}

function Get-AiPdfWindowByTitle {
    <#
    .SYNOPSIS
        按标题正则查找窗口，找不到返回 $null。
    .DESCRIPTION
        先看进程的顶层窗口；Qt 把带父窗口的模态对话框暴露成主窗口的后代
        （ControlType=Window，class=QInputDialog），而不是独立的顶层窗口，
        因此再补一次后代查找。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$TitlePattern
    )

    foreach ($window in (Get-AiPdfProcessWindows -Process $Process)) {
        if ($window.Current.Name -match $TitlePattern) {
            return $window
        }
    }

    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $pidCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $Process.Id)
    $top = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $pidCondition)
    if (-not $top) {
        return $null
    }

    $windowCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Window)
    foreach ($element in $top.FindAll(
            [System.Windows.Automation.TreeScope]::Descendants, $windowCondition)) {
        if ($element.Current.Name -match $TitlePattern) {
            return $element
        }
    }
    return $null
}

function Set-AiPdfElementText {
    <#
    .SYNOPSIS
        用 ValuePattern 设置控件文本（如密码输入框）。
    #>
    param(
        [Parameter(Mandatory = $true)]$Element,
        [Parameter(Mandatory = $true)][string]$Text
    )

    $pattern = $null
    if ($Element.TryGetCurrentPattern(
            [System.Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) {
        $pattern.SetValue($Text)
        return $true
    }
    return $false
}

function Invoke-AiPdfElement {
    <#
    .SYNOPSIS
        用 InvokePattern 触发控件（如对话框的确定按钮）。
    #>
    param(
        [Parameter(Mandatory = $true)]$Element
    )

    $pattern = $null
    if ($Element.TryGetCurrentPattern(
            [System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) {
        $pattern.Invoke()
        return $true
    }
    return $false
}

function Wait-AiPdfDialog {
    <#
    .SYNOPSIS
        等待标题匹配的模态对话框出现，返回窗口元素；超时返回 $null。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [string]$TitlePattern = '需要密码',
        [int]$TimeoutSeconds = 15
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return $null
        }
        $dialog = Get-AiPdfWindowByTitle -Process $Process -TitlePattern $TitlePattern
        if ($dialog) {
            return $dialog
        }
        Start-Sleep -Milliseconds 300
    }
    return $null
}

function Invoke-AiPdfDialogButton {
    <#
    .SYNOPSIS
        在模态对话框里点击名字匹配的按钮（如确定 / 取消）。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$ButtonPattern,
        [string]$TitlePattern = '需要密码',
        [int]$TimeoutSeconds = 15
    )

    $dialog = Wait-AiPdfDialog -Process $Process -TitlePattern $TitlePattern -TimeoutSeconds $TimeoutSeconds
    if (-not $dialog) {
        return $false
    }

    $buttonCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Button)
    foreach ($button in $dialog.FindAll(
            [System.Windows.Automation.TreeScope]::Descendants, $buttonCondition)) {
        if ($button.Current.Name -match $ButtonPattern) {
            return (Invoke-AiPdfElement -Element $button)
        }
    }
    return $false
}

function Enter-AiPdfDialogPassword {
    <#
    .SYNOPSIS
        等待标题匹配的对话框出现，填写密码并确定。成功返回 $true。
    .DESCRIPTION
        只用控件级 InvokePattern 点击确定按钮：不要用键盘兜底，
        回车键会发给前台窗口，在连续跑多个场景时可能打到下一个进程的对话框上。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$Password,
        [string]$TitlePattern = '需要密码',
        [int]$TimeoutSeconds = 15
    )

    $dialog = Wait-AiPdfDialog -Process $Process -TitlePattern $TitlePattern -TimeoutSeconds $TimeoutSeconds
    if (-not $dialog) {
        return $false
    }

    $editCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Edit)
    $edit = $dialog.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $editCondition)
    if (-not $edit) {
        return $false
    }
    if (-not (Set-AiPdfElementText -Element $edit -Text $Password)) {
        return $false
    }

    $buttonCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Button)
    foreach ($button in $dialog.FindAll(
            [System.Windows.Automation.TreeScope]::Descendants, $buttonCondition)) {
        if ($button.Current.Name -match '^(OK|确定|好|Yes)$') {
            return (Invoke-AiPdfElement -Element $button)
        }
    }
    return $false
}

function Wait-AiPdfTextMatch {
    <#
    .SYNOPSIS
        轮询主窗口文本直到匹配给定正则，返回 $true。用于状态栏这类短暂提示。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [int]$TimeoutSeconds = 6,
        [string]$TitlePattern = ''
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return $false
        }
        $probe = Get-AiPdfWindowProbe -Process $Process -TitlePattern $TitlePattern
        if ($probe -and $probe.Text -match $Pattern) {
            return $true
        }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

function Get-AiPdfWindowProbe {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [string]$TitlePattern = ''
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
    if ($TitlePattern -and $window.Current.Name -notmatch $TitlePattern) {
        # 模态对话框弹出时，第一个顶层窗口可能是对话框而不是主窗口，
        # 这时按标题精确取主窗口。
        $window = Get-AiPdfWindowByTitle -Process $Process -TitlePattern $TitlePattern
        if (-not $window) {
            return $null
        }
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
        [string]$Path,
        [string]$WindowTitlePattern = ''
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
    if ($WindowTitlePattern) {
        # 模态对话框弹出时 MainWindowHandle 可能指向对话框，按标题取更准。
        $window = Get-AiPdfWindowByTitle -Process $Process -TitlePattern $WindowTitlePattern
        if ($window) {
            $handle = [IntPtr]$window.Current.NativeWindowHandle
        }
    }
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
        [int]$TimeoutSeconds = 15,
        [string]$TitlePattern = ''
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return $null
        }
        $probe = Get-AiPdfWindowProbe -Process $Process -TitlePattern $TitlePattern
        if ($probe -and $probe.Text -match 'ready') {
            return $probe
        }
        Start-Sleep -Milliseconds 400
    }
    return (Get-AiPdfWindowProbe -Process $Process -TitlePattern $TitlePattern)
}
