<#
.SYNOPSIS
    v0.6.0 目录、书签与页面链接验收。

.DESCRIPTION
    全部走真实界面路径，分五个场景：

      1. 目录树：点导航栏的「目录」→ 大纲树出现分层条目 →
         点子条目跳到对应页 → 缩略图列表跟着高亮当前页；
      2. 书签列表：点「书签」→ 同一份大纲的扁平列表 → 点条目跳到对应页；
      3. 内部链接：悬停出现手型光标 → 点击跳到目标页；
      4. 外部链接与重叠链接：外部链接被识别并拿到地址；
         两个完全重叠的链接里，上面那个（Z 序靠后）生效；
         点空白处不误触发；
      5. 没有大纲的文档 → 目录面板显示空状态。

    坐标换算：画布视口的屏幕矩形来自 UI Automation，
    页面在视口里水平居中（PageCanvas::pageLeft），
    页面坐标是点（1/72 英寸），缩放为 1 时 1 点 = 1 逻辑像素，
    屏幕像素还要乘以系统 DPI 比例。

    链接矩形在 PDF user space（左下原点），页面高 400 点：
      user [60 200 220 235] → 显示空间 (60,165) 160x35
      user [60 150 300 180] → 显示空间 (60,220) 240x30
      user [60 100 300 140] → 显示空间 (60,260) 240x40
    每个场景都重新启动应用，保证滚动位置是 0（第 1 页在视口顶部）。

    外部链接用 AIPDF_SUPPRESS_EXTERNAL_LINKS=1 抑制真的拉起浏览器：
    验收跑在 CI 上，拉起浏览器会打断脚本，也可能点到别的东西去。
    被断言的是「点击被识别成外部链接并拿到了正确地址」，
    真正调用系统浏览器只是最后一行系统调用。

    面板内容不进 UIA 文本（QStackedWidget 只暴露当前页），
    条目数由应用写进轨迹，脚本据此断言面板里真的有内容。

    这套脚本会抢前台窗口、置顶、并用合成鼠标事件操作桌面：
    **不要在别人正在用这台机器时跑**（见 README 的「界面验收会抢桌面」）。

    两条容易被忽略的前置条件：
      - 合成鼠标事件落在**光标下的那个窗口**上，机器上别的程序随时可能
        盖住我们的窗口，所以操作前把窗口置顶并断言它在前台。
      - 轨迹文件是追加写的，每个场景开始前必须清空，
        否则上一轮的记录会被当成本轮的证据。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\navigation-acceptance'),
    [string]$Python = 'python',
    [string]$ScreenshotPath = '',
    [int]$StartupSeconds = 10
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')

# 必须在任何窗口/鼠标操作之前：DPI 感知决定坐标用物理像素还是逻辑像素。
$null = Enable-AiPdfDpiAwareness

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

$qtRoot = if ($env:AIPDF_QT_ROOT) { $env:AIPDF_QT_ROOT } else { 'C:\Qt\6.8.3\mingw_64' }
$mingwRoot = if ($env:AIPDF_MINGW_ROOT) { $env:AIPDF_MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }
foreach ($extra in @(
    (Join-Path $qtRoot 'bin'),
    (Join-Path $mingwRoot 'bin'),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\pdfium\bin')
)) {
    if (Test-Path -LiteralPath $extra) {
        $env:PATH = "$extra;$env:PATH"
    }
}

# 外部链接不真的拉起浏览器（理由见脚本头部说明）。
$env:AIPDF_SUPPRESS_EXTERNAL_LINKS = '1'

Write-Host '==> 生成测试 PDF' -ForegroundColor Cyan
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
$fixtureDir = Join-Path $WorkDir 'fixtures'
& $Python (Join-Path $PSScriptRoot 'generate-test-pdfs.py') $fixtureDir --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host '生成测试 PDF 失败' -ForegroundColor Red
    exit 2
}
$navigationPdf = Join-Path $fixtureDir 'navigation.pdf'
$normalPdf = Join-Path $fixtureDir 'normal.pdf'

# 轨迹文件：大纲、书签与链接动作要能被脚本断言。
$tracePath = Join-Path $WorkDir 'trace.log'
$env:AIPDF_TRACE_FILE = $tracePath

# 记下用户真实的鼠标位置：验收要用合成鼠标事件点屏幕坐标，
# 跑完必须把指针放回去，不能把开发者正在做的事搅乱。
$script:CursorStart = Get-AiPdfCursorPosition

function Start-AiPdfApp {
    param(
        [Parameter(Mandatory = $true)][string]$Pdf,
        [int]$WaitSeconds = 10
    )
    $app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath `
        -ArgumentList @($Pdf, '--diagnostics') -PassThru
    # 等主窗口真的建好：句柄还没出来时，后面的 UIA 查找会全部落空，
    # 而且失败原因会被误读成「界面没做」。
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($app.HasExited) {
            break
        }
        if ($app.MainWindowHandle -ne [IntPtr]::Zero) {
            break
        }
        Start-Sleep -Milliseconds 300
    }
    Start-Sleep -Seconds 2
    return $app
}

function Stop-AiPdfApp {
    param([Parameter(Mandatory = $true)]$App)
    if ($App -and -not $App.HasExited) {
        $App.CloseMainWindow() | Out-Null
        if (-not $App.WaitForExit(5000)) {
            $App.Kill()
        }
    }
    Stop-AiPdfOrphanWorker -Quiet | Out-Null
    # 把指针还给用户：合成鼠标事件把真实指针挪走过。
    Restore-AiPdfCursorPosition -Position $script:CursorStart
}

# 把应用摆到确定的位置并置顶：合成鼠标事件只送到前台窗口。
function Prepare-AiPdfWindow {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $null = Set-AiPdfForeground -Process $Process
    Set-AiPdfWindowGeometry -Process $Process | Out-Null
    Set-AiPdfWindowTopmost -Process $Process -Enable $true | Out-Null
    return Set-AiPdfForeground -Process $Process
}

# 第 1 页上某个显示空间坐标对应的屏幕坐标。
#
# 顺序要紧：先把窗口摆定，再读视口矩形，最后才算坐标。
# 中间任何一步顺序颠倒（尤其是窗口在算完坐标之后又动了），
# 点击就会落到别的地方去——表现是「点了没反应」，
# 很容易被误判成产品缺陷。
function Get-AiPdfPagePoint {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][double]$DisplayX,
        [Parameter(Mandatory = $true)][double]$DisplayY
    )
    $null = Prepare-AiPdfWindow -Process $Process
    $viewport = Get-AiPdfElementById -Process $Process -IdPattern '*pdfCanvas*viewport*'
    if (-not $viewport) {
        return $null
    }
    $scale = Set-AiPdfForeground -Process $Process
    $rect = $viewport.Current.BoundingRectangle
    $viewportWidth = $rect.Width / $scale
    # 页面在视口里水平居中（PageCanvas::pageLeft 的算法）。
    $pageMargin = 24.0
    $available = $viewportWidth - 2 * $pageMargin
    $pageLeft = [Math]::Max($pageMargin, ($available - 612.0) / 2.0 + $pageMargin)
    # 每个场景都重新打开文档，滚动位置为 0：第 1 页的顶部就在视口顶部。
    return [pscustomobject]@{
        X = [int]($rect.Left + ($pageLeft + $DisplayX) * $scale)
        Y = [int]($rect.Top + $DisplayY * $scale)
    }
}

# 点页面上的一个位置（显示空间坐标）。
function Invoke-AiPdfPageClick {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][double]$DisplayX,
        [Parameter(Mandatory = $true)][double]$DisplayY
    )
    $point = Get-AiPdfPagePoint -Process $Process -DisplayX $DisplayX -DisplayY $DisplayY
    if ($null -eq $point) {
        return $false
    }
    Invoke-AiPdfMouseClick -X $point.X -Y $point.Y
    return $true
}

# 只把指针移到页面上的某个位置（测悬停用，不点击）。
function Invoke-AiPdfPageHover {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][double]$DisplayX,
        [Parameter(Mandatory = $true)][double]$DisplayY
    )
    $point = Get-AiPdfPagePoint -Process $Process -DisplayX $DisplayX -DisplayY $DisplayY
    if ($null -eq $point) {
        return $false
    }
    Invoke-AiPdfMouseMove -X $point.X -Y $point.Y
    return $true
}

# 点一个元素：优先走 InvokePattern，不支持就用真实鼠标点它的中心。
function Click-AiPdfElement {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]$Element
    )
    if ($null -eq $Element) {
        return $false
    }
    if (Invoke-AiPdfElement -Element $Element) {
        Start-Sleep -Milliseconds 500
        return $true
    }
    $bounds = $Element.Current.BoundingRectangle
    Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
        -Y ([int]($bounds.Top + $bounds.Height / 2))
    Start-Sleep -Milliseconds 500
    return $true
}

# 找导航栏的页签按钮；找不到时把相关 AutomationId 全部打出来——
# 只报「找不到」而不说找过哪些，排查时只能靠猜。
function Get-AiPdfNavButton {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$IdPattern
    )
    $button = Wait-AiPdfElementById -Process $Process -IdPattern $IdPattern -TimeoutSeconds 10
    if ($button) {
        return $button
    }
    Write-Host '    导航栏相关的 AutomationId：' -ForegroundColor DarkGray
    foreach ($element in (Get-AiPdfWindowElements -Process $Process)) {
        try {
            $id = $element.Current.AutomationId
            if ($id -like '*rail*' -or $id -like '*outlineTree*' -or $id -like '*bookmarkList*') {
                Write-Host "      $id" -ForegroundColor DarkGray
            }
        } catch {
        }
    }
    return $null
}

# 在某个容器（目录树 / 书签列表）里按可见文字找条目。
#
# 必须限定容器：目录树与书签列表里的条目同名（同一份大纲的两种看法），
# 不限定的话可能点到另一个面板里的同名条目，测的就不是想测的东西了。
function Get-AiPdfItemInContainer {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$Name,
        $Container
    )
    if ($null -eq $Container) {
        return $null
    }
    $containerRect = $Container.Current.BoundingRectangle
    foreach ($element in (Get-AiPdfWindowElements -Process $Process)) {
        try {
            if ($element.Current.Name -ne $Name) {
                continue
            }
            $rect = $element.Current.BoundingRectangle
            if ($rect.Width -le 0 -or $rect.Height -le 0) {
                continue
            }
            $inside = ($rect.Left -ge $containerRect.Left) -and ($rect.Right -le $containerRect.Right) -and
                      ($rect.Top -ge $containerRect.Top) -and ($rect.Bottom -le $containerRect.Bottom)
            if ($inside) {
                return $element
            }
        } catch {
            # 元素可能在读取瞬间失效，跳过。
        }
    }
    return $null
}

# 当前页码（工具栏上的「第 N / M 页」）。
function Get-AiPdfCurrentPage {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $probe = Get-AiPdfWindowProbe -Process $Process -TitlePattern 'AI PDF Editor'
    if (-not $probe) {
        return -1
    }
    if ($probe.Text -match '第\s*(\d+)\s*/\s*(\d+)\s*页') {
        return [int]$Matches[1]
    }
    return -1
}

# --------------------------------------------------------------- 场景 1 ------
Write-Host '==> 场景 1：目录树' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $navigationPdf -WaitSeconds $StartupSeconds

    Test-Check '大纲已从文档取回' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'outline ready items=2' -TimeoutSeconds 20) ''

    $button = Get-AiPdfNavButton -Process $app -IdPattern '*railButton.outline*'
    Test-Check '找到「目录」页签按钮' ($null -ne $button) ''
    if ($button) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $button | Out-Null
        Start-Sleep -Milliseconds 800
    }

    Test-Check '目录面板里有 2 个顶层条目' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'panel outline items=2' -TimeoutSeconds 10) `
        (Get-AiPdfTraceText -Path $tracePath)

    if ($ScreenshotPath) {
        $taken = Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath
        Test-Check '目录界面截图已保存' $taken $ScreenshotPath
    }

    # 目录树里的条目：点子条目应跳到它的目标页（第 3 页）。
    $tree = Wait-AiPdfElementById -Process $app -IdPattern '*outlineTree*' -TimeoutSeconds 10
    Test-Check '找到目录树' ($null -ne $tree) ''
    $item = Get-AiPdfItemInContainer -Process $app -Container $tree -Name '第二节 方法'
    Test-Check '目录树里找到「第二节 方法」' ($null -ne $item) ''
    if ($item) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        $bounds = $item.Current.BoundingRectangle
        Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
            -Y ([int]($bounds.Top + $bounds.Height / 2))
        Start-Sleep -Seconds 1
    }
    Test-Check '点目录条目跳到第 3 页' ((Get-AiPdfCurrentPage -Process $app) -eq 3) `
        "current=$(Get-AiPdfCurrentPage -Process $app)"
    Test-Check '跳转动作记进轨迹' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'navigate page=2' -TimeoutSeconds 5) ''
    # 缩略图列表跟着高亮当前页（轨迹里的页码从 0 开始）。
    Test-Check '缩略图列表高亮当前页' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'thumbnail current page=2' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 2 ------
Write-Host '==> 场景 2：书签列表' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $navigationPdf -WaitSeconds $StartupSeconds

    Test-Check '大纲已从文档取回' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'outline ready items=2' -TimeoutSeconds 20) ''

    $button = Get-AiPdfNavButton -Process $app -IdPattern '*railButton.bookmarks*'
    Test-Check '找到「书签」页签按钮' ($null -ne $button) ''
    if ($button) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $button | Out-Null
        Start-Sleep -Milliseconds 800
    }

    # 书签页签是同一份大纲的扁平列表：4 条（2 顶层 + 2 子条目）。
    Test-Check '书签面板里有 4 个条目' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'panel bookmarks items=4' -TimeoutSeconds 10) `
        (Get-AiPdfTraceText -Path $tracePath)

    $list = Wait-AiPdfElementById -Process $app -IdPattern '*bookmarkList*' -TimeoutSeconds 10
    Test-Check '找到书签列表' ($null -ne $list) ''
    $item = Get-AiPdfItemInContainer -Process $app -Container $list -Name '第一章 概述'
    Test-Check '书签列表里找到「第一章 概述」' ($null -ne $item) ''
    if ($item) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        $bounds = $item.Current.BoundingRectangle
        Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
            -Y ([int]($bounds.Top + $bounds.Height / 2))
        Start-Sleep -Seconds 1
    }
    Test-Check '点书签条目跳到第 1 页' ((Get-AiPdfCurrentPage -Process $app) -eq 1) `
        "current=$(Get-AiPdfCurrentPage -Process $app)"
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 3 ------
Write-Host '==> 场景 3：内部链接' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $navigationPdf -WaitSeconds $StartupSeconds

    Test-Check '文档已打开并显示总页数' `
        (Wait-AiPdfProcessTextMatch -Process $app -Pattern '第\s*1\s*/\s*3\s*页' -TimeoutSeconds 20) ''

    # 悬停：指针停在链接上时应当变成手型。
    $hovered = Invoke-AiPdfPageHover -Process $app -DisplayX 140.0 -DisplayY 182.0
    Test-Check '算出内部链接的屏幕坐标' $hovered ''
    Start-Sleep -Milliseconds 600
    Test-Check '悬停在链接上时指针变为手型' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'cursor link' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    Invoke-AiPdfPageClick -Process $app -DisplayX 140.0 -DisplayY 182.0 | Out-Null
    Start-Sleep -Seconds 1
    Test-Check '点内部链接跳到第 2 页' ((Get-AiPdfCurrentPage -Process $app) -eq 2) `
        "current=$(Get-AiPdfCurrentPage -Process $app)"
    Test-Check '内部链接动作记进轨迹' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'link goto from=0 to=1' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 4 ------
Write-Host '==> 场景 4：外部链接与重叠链接' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $navigationPdf -WaitSeconds $StartupSeconds

    # --- 外部链接 ---
    Invoke-AiPdfPageClick -Process $app -DisplayX 180.0 -DisplayY 235.0 | Out-Null
    Start-Sleep -Seconds 1
    Test-Check '外部链接被识别并拿到地址' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'link external uri=https://example\.com/folixa' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    # --- 重叠链接：Z 序靠后的那个（URI）生效 ---
    Reset-AiPdfTrace -Path $tracePath
    Invoke-AiPdfPageClick -Process $app -DisplayX 180.0 -DisplayY 280.0 | Out-Null
    Start-Sleep -Seconds 1
    Test-Check '重叠链接里上面那个（URI）生效' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'link click page=0 target=-1 uri=https://example\.com/overlay' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
    Test-Check '被盖住的链接没有生效' `
        ((Get-AiPdfTraceText -Path $tracePath) -notmatch 'target=2') `
        (Get-AiPdfTraceText -Path $tracePath)

    # --- 负例：点空白处不该触发任何链接 ---
    Reset-AiPdfTrace -Path $tracePath
    Invoke-AiPdfPageHover -Process $app -DisplayX 420.0 -DisplayY 340.0 | Out-Null
    Start-Sleep -Milliseconds 600
    Test-Check '移出链接后指针恢复默认' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'cursor default' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
    Invoke-AiPdfPageClick -Process $app -DisplayX 420.0 -DisplayY 340.0 | Out-Null
    Start-Sleep -Milliseconds 800
    Test-Check '点空白处不触发链接' `
        ((Get-AiPdfTraceText -Path $tracePath) -notmatch 'link click') `
        (Get-AiPdfTraceText -Path $tracePath)
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 5 ------
Write-Host '==> 场景 5：没有大纲的文档' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $normalPdf -WaitSeconds $StartupSeconds

    Test-Check '无大纲文档记为空' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'outline empty' -TimeoutSeconds 20) `
        (Get-AiPdfTraceText -Path $tracePath)

    $button = Get-AiPdfNavButton -Process $app -IdPattern '*railButton.outline*'
    if ($button) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $button | Out-Null
        Start-Sleep -Milliseconds 800
    }
    Test-Check '目录面板为空（0 个条目）' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'panel outline items=0' -TimeoutSeconds 10) `
        (Get-AiPdfTraceText -Path $tracePath)
} finally {
    Stop-AiPdfApp -App $app
}

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "导航验收失败：$($script:Failures) 项不通过" -ForegroundColor Red
    exit 1
}
Write-Host '导航验收全部通过' -ForegroundColor Green
exit 0
