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
        用 Win32 枚举返回属于该进程的窗口句柄（含被拥有的模态对话框）。
    .DESCRIPTION
        不用 UI Automation 的窗口树：模态对话框在 UIA 树里是主窗口的后代，
        而且主窗口的子树在应用被模态框卡住时可能还没建好，查找会时灵时不灵。
        Win32 的 EnumWindows 直接给出 HWND，稳定得多。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process
    )

    Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class AiPdfWindows {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hWnd, StringBuilder text, int count);
    public static List<IntPtr> ForProcess(int processId) {
        var handles = new List<IntPtr>();
        EnumWindows((hWnd, lParam) => {
            uint pid;
            GetWindowThreadProcessId(hWnd, out pid);
            if (pid == (uint)processId) {
                handles.Add(hWnd);
            }
            return true;
        }, IntPtr.Zero);
        return handles;
    }
    public static string Title(IntPtr hWnd) {
        var text = new StringBuilder(512);
        GetWindowTextW(hWnd, text, text.Capacity);
        return text.ToString();
    }
}
'@

    return [AiPdfWindows]::ForProcess($Process.Id)
}

function Get-AiPdfWindowByTitle {
    <#
    .SYNOPSIS
        按窗口标题查找窗口，返回 UI Automation 元素；找不到返回 $null。
    .DESCRIPTION
        先按标题枚举 Win32 窗口，再用 AutomationElement.FromHandle 取元素，
        这样拿到的是真实 HWND 对应的元素，而不是 UIA 树里可能未就绪的后代。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$TitlePattern
    )

    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    foreach ($handle in (Get-AiPdfProcessWindows -Process $Process)) {
        if ([AiPdfWindows]::Title($handle) -match $TitlePattern) {
            return [System.Windows.Automation.AutomationElement]::FromHandle($handle)
        }
    }
    return $null
}

function Get-AiPdfElementValue {
    <#
    .SYNOPSIS
        读取控件当前值（密码框读回来是掩码，长度仍然可比）。
    #>
    param(
        [Parameter(Mandatory = $true)]$Element
    )

    $pattern = $null
    if ($Element.TryGetCurrentPattern(
            [System.Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) {
        return $pattern.Current.Value
    }
    return $null
}

function Set-AiPdfElementText {
    <#
    .SYNOPSIS
        用 ValuePattern 设置控件文本（如密码输入框）。
    .DESCRIPTION
        写入后回读校验长度；Qt 的 ValueProvider 在密码框上并不可靠，
        偶尔会只写进去一部分，必须确认写满了再点确定。

        控件元素随时可能失效（Qt 重建控件时 UIA provider 会被释放，
        抛 ElementNotAvailableException），所以每次尝试都重新取一次 pattern，
        失效时返回 false 让调用方重新取元素，而不是把异常抛穿整个验收脚本。
    #>
    param(
        [Parameter(Mandatory = $true)]$Element,
        [Parameter(Mandatory = $true)][string]$Text,
        [int]$MaxAttempts = 3
    )

    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        try {
            $pattern = $null
            if (-not $Element.TryGetCurrentPattern(
                    [System.Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) {
                return $false
            }
            $pattern.SetValue($Text)
            Start-Sleep -Milliseconds 200
            $current = $pattern.Current.Value
            if ($null -ne $current -and $current.Length -eq $Text.Length) {
                return $true
            }
            # 没写满就清空重来。
            $pattern.SetValue('')
        } catch {
            # 元素已失效：换一次 pattern 再试，仍不行就交给调用方重新取元素。
            Start-Sleep -Milliseconds 300
        }
        Start-Sleep -Milliseconds 100
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

    try {
        $pattern = $null
        if ($Element.TryGetCurrentPattern(
                [System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) {
            $pattern.Invoke()
            return $true
        }
    } catch {
        # 元素失效：当作这一次没点成。
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
    try {
        foreach ($button in $dialog.FindAll(
                [System.Windows.Automation.TreeScope]::Descendants, $buttonCondition)) {
            if ($button.Current.Name -match $ButtonPattern) {
                return (Invoke-AiPdfElement -Element $button)
            }
        }
    } catch {
        # 按钮元素失效：当作这一次没点成。
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

        密码框的 UIA 元素可能在写入前就失效，所以整段流程按次重试：
        每次重新取对话框和输入框，而不是复用上一次的元素。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$Password,
        [string]$TitlePattern = '需要密码',
        [int]$TimeoutSeconds = 15,
        [int]$MaxAttempts = 3
    )

    $editCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Edit)
    $buttonCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Button)

    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        if ($Process.HasExited) {
            return $false
        }

        $dialog = Wait-AiPdfDialog -Process $Process -TitlePattern $TitlePattern -TimeoutSeconds $TimeoutSeconds
        if (-not $dialog) {
            return $false
        }

        try {
            $edit = $dialog.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $editCondition)
            if (-not $edit) {
                Start-Sleep -Milliseconds 300
                continue
            }
            if (-not (Set-AiPdfElementText -Element $edit -Text $Password)) {
                continue
            }

            foreach ($button in $dialog.FindAll(
                    [System.Windows.Automation.TreeScope]::Descendants, $buttonCondition)) {
                if ($button.Current.Name -match '^(OK|确定|好|Yes)$') {
                    if (Invoke-AiPdfElement -Element $button) {
                        return $true
                    }
                    break
                }
            }
        } catch {
            # 对话框在读取过程中被重建：重新取一次对话框再来。
            Start-Sleep -Milliseconds 300
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
    try {
        $texts.Add($window.Current.Name)
    } catch {
        return $null
    }
    $children = $window.FindAll(
        [System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($element in $children) {
        try {
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
        } catch {
            # 元素可能在读取的瞬间失效（例如状态栏标签被定时刷新重建），
            # 跳过这一个元素，不让整次探测失败。
        }
    }

    try {
        $title = $window.Current.Name
    } catch {
        $title = ''
    }
    return [pscustomobject]@{
        Title = $title
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
    <#
    .SYNOPSIS
        轮询主窗口，直到窗口文本满足条件后返回最后一次探测结果。
    .DESCRIPTION
        -RequirePattern 用来消除竞态：窗口「就绪」不等于内容已经就位，
        比如 500 页文档刚打开时页码标签可能还没更新。
        指定后一直等到预期内容出现（或超时），断言才是确定的。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 15,
        [string]$TitlePattern = '',
        [string]$RequirePattern = ''
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $last = $null
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return $last
        }
        $probe = Get-AiPdfWindowProbe -Process $Process -TitlePattern $TitlePattern
        if ($probe) {
            $last = $probe
            $ready = $probe.Text -match 'ready'
            $matched = (-not $RequirePattern) -or ($probe.Text -match $RequirePattern)
            if ($ready -and $matched) {
                return $probe
            }
        }
        Start-Sleep -Milliseconds 400
    }
    return $last
}

function Get-AiPdfWindowElements {
    <#
    .SYNOPSIS
        收集进程**所有**顶层窗口里的 UIA 元素。
    .DESCRIPTION
        停靠面板（导航栏、AI 助手）在 UIA 树里是独立的顶层窗口，
        只查主窗口会漏掉它们（例如搜索面板的输入框与结果列表）。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process
    )

    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    $elements = New-Object System.Collections.Generic.List[object]
    foreach ($handle in (Get-AiPdfProcessWindows -Process $Process)) {
        try {
            $window = [System.Windows.Automation.AutomationElement]::FromHandle($handle)
            if (-not $window) {
                continue
            }
            $children = $window.FindAll(
                [System.Windows.Automation.TreeScope]::Descendants,
                [System.Windows.Automation.Condition]::TrueCondition)
            foreach ($element in $children) {
                $elements.Add($element)
            }
        } catch {
            # 窗口正在关闭：跳过。
        }
    }
    return $elements.ToArray()
}

function Get-AiPdfElementById {
    <#
    .SYNOPSIS
        按 AutomationId 片段找元素（Qt 把 objectName 放进 AutomationId）。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$IdPattern
    )

    foreach ($element in (Get-AiPdfWindowElements -Process $Process)) {
        try {
            if ($element.Current.AutomationId -like $IdPattern) {
                return $element
            }
        } catch {
            # 元素可能刚好失效：继续找下一个。
        }
    }
    return $null
}

function Get-AiPdfProcessText {
    <#
    .SYNOPSIS
        汇总进程所有顶层窗口的可访问文本（含停靠面板）。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process
    )

    $texts = New-Object System.Collections.Generic.List[string]
    foreach ($element in (Get-AiPdfWindowElements -Process $Process)) {
        try {
            $texts.Add($element.Current.Name)
            $textPattern = $null
            if ($element.TryGetCurrentPattern(
                    [System.Windows.Automation.TextPattern]::Pattern, [ref]$textPattern)) {
                $document = $textPattern.DocumentRange.GetText(-1)
                if ($document) {
                    $texts.Add($document)
                }
            }
        } catch {
            # 元素可能在读取瞬间失效，跳过这一个。
        }
    }
    return ($texts -join "`n")
}

function Wait-AiPdfProcessTextMatch {
    <#
    .SYNOPSIS
        轮询进程所有窗口的文本，直到匹配给定正则。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [int]$TimeoutSeconds = 15
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return $false
        }
        $text = Get-AiPdfProcessText -Process $Process
        if ($text -match $Pattern) {
            return $true
        }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

function Invoke-AiPdfMouseDrag {
    <#
    .SYNOPSIS
        在屏幕上做一次左键拖动（选择文本用）。
    .DESCRIPTION
        用 SetCursorPos + mouse_event 合成真实鼠标事件：
        UI Automation 的 InvokePattern 点不到画布上的字。
        调用方要保证目标窗口已经在前台。
    #>
    param(
        [Parameter(Mandatory = $true)][int]$X1,
        [Parameter(Mandatory = $true)][int]$Y1,
        [Parameter(Mandatory = $true)][int]$X2,
        [Parameter(Mandatory = $true)][int]$Y2,
        [int]$Steps = 12
    )

    if (-not ('AiPdfMouse' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AiPdfMouse {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    public const uint LEFTDOWN = 0x0002;
    public const uint LEFTUP = 0x0004;
}
'@
    }

    [AiPdfMouse]::SetCursorPos($X1, $Y1) | Out-Null
    Start-Sleep -Milliseconds 200
    [AiPdfMouse]::mouse_event([AiPdfMouse]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    for ($step = 1; $step -le $Steps; $step++) {
        $x = [int]($X1 + ($X2 - $X1) * $step / $Steps)
        $y = [int]($Y1 + ($Y2 - $Y1) * $step / $Steps)
        [AiPdfMouse]::SetCursorPos($x, $y) | Out-Null
        Start-Sleep -Milliseconds 40
    }
    Start-Sleep -Milliseconds 200
    [AiPdfMouse]::mouse_event([AiPdfMouse]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 300
}

function Invoke-AiPdfMouseClick {
    <#
    .SYNOPSIS
        在屏幕上点一下（左键）。
    #>
    param(
        [Parameter(Mandatory = $true)][int]$X,
        [Parameter(Mandatory = $true)][int]$Y
    )

    if (-not ('AiPdfMouse' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AiPdfMouse {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    public const uint LEFTDOWN = 0x0002;
    public const uint LEFTUP = 0x0004;
}
'@
    }

    [AiPdfMouse]::SetCursorPos($X, $Y) | Out-Null
    Start-Sleep -Milliseconds 150
    [AiPdfMouse]::mouse_event([AiPdfMouse]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [AiPdfMouse]::mouse_event([AiPdfMouse]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 300
}

function Wait-AiPdfElementById {
    <#
    .SYNOPSIS
        轮询等待某个 AutomationId 元素出现（面板展开、标签切换都需要等）。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$IdPattern,
        [int]$TimeoutSeconds = 15
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return $null
        }
        $element = Get-AiPdfElementById -Process $Process -IdPattern $IdPattern
        if ($element) {
            return $element
        }
        Start-Sleep -Milliseconds 300
    }
    return $null
}

function Get-AiPdfTraceText {
    <#
    .SYNOPSIS
        读取应用写出的轨迹文件（AIPDF_TRACE_FILE）。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return ''
    }
    try {
        return [System.IO.File]::ReadAllText($Path)
    } catch {
        return ''
    }
}

function Enable-AiPdfDpiAwareness {
    <#
    .SYNOPSIS
        让脚本进程自己变成 per-monitor DPI aware。
    .DESCRIPTION
        默认情况下 PowerShell 是 DPI unaware 的：Win32 的窗口坐标会被
        「虚拟化」成逻辑像素，而 UI Automation 给的 BoundingRectangle 是物理像素。
        两者混用会把鼠标事件打到别的地方（表现为拖选永远选不中）。
        在做任何窗口/鼠标操作之前调用一次即可。
    #>
    if (-not ('AiPdfDpi' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AiPdfDpi {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public static bool Enable() {
        // PER_MONITOR_AWARE_V2 = -4
        if (SetProcessDpiAwarenessContext(new IntPtr(-4))) {
            return true;
        }
        return SetProcessDPIAware();
    }
}
'@
    }
    return [AiPdfDpi]::Enable()
}

function Set-AiPdfForeground {
    <#
    .SYNOPSIS
        把主窗口切到前台（键盘与鼠标事件要送到前台窗口）。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [string]$TitlePattern = ''
    )

    if (-not ('AiPdfForeground' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AiPdfForeground {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hWnd);
}
'@
    }

    $handle = $Process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) {
        return 1.0
    }
    [AiPdfForeground]::ShowWindow($handle, 9) | Out-Null
    [AiPdfForeground]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 400
    $dpi = [AiPdfForeground]::GetDpiForWindow($handle)
    if ($dpi -le 0) {
        $dpi = 96
    }
    return ($dpi / 96.0)
}

function Set-AiPdfWindowGeometry {
    <#
    .SYNOPSIS
        把主窗口挪到屏幕左上角并设成确定尺寸（物理像素）。
    .DESCRIPTION
        拖选要按屏幕坐标算，窗口位置必须确定：窗口管理器可能把它放在
        任意位置，甚至屏幕外。调用方要保证脚本已经 DPI 感知。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [int]$Width = 1600,
        [int]$Height = 1000
    )

    if (-not ('AiPdfGeometry' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AiPdfGeometry {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    public const uint NOZORDER = 0x0004;
    public const uint NOACTIVATE = 0x0010;
}
'@
    }

    $handle = $Process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) {
        return $false
    }
    $ok = [AiPdfGeometry]::SetWindowPos(
        $handle, [IntPtr]::Zero, 0, 0, $Width, $Height,
        [AiPdfGeometry]::NOZORDER -bor [AiPdfGeometry]::NOACTIVATE)
    Start-Sleep -Milliseconds 400
    return $ok
}

function Stop-AiPdfOrphanWorker {
    <#
    .SYNOPSIS
        清理残留的 AI worker 进程。

    .DESCRIPTION
        验收脚本会用 Kill() 强杀主程序，此时 QProcess 析构函数不会执行，
        AI worker 会变成孤儿进程继续运行，并锁住 PdfAiWorker.exe，
        让后续构建链接失败（cannot open output file ... Permission denied）。

        产品侧已加入父进程看门狗（worker 跟随编辑器一起退出），
        这里作为兜底，保证旧版本遗留的孤儿进程不会污染后续验收。
    #>
    param(
        [switch]$Quiet
    )

    $workers = @(Get-Process -Name 'PdfAiWorker' -ErrorAction SilentlyContinue)
    if ($workers.Count -eq 0) {
        return 0
    }
    foreach ($worker in $workers) {
        Stop-Process -Id $worker.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 500
    if (-not $Quiet) {
        Write-Host "    清理残留 AI worker 进程 $($workers.Count) 个" -ForegroundColor DarkGray
    }
    return $workers.Count
}
