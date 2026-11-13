<#
.SYNOPSIS
    v0.7.0 批注与撤销重做验收。

.DESCRIPTION
    全部走真实界面路径，分五个场景：

      1. 高亮：拖选一段文字 → 点工具栏的「高亮」→ 批注创建并写进文档；
      2. 撤销与重做：点「撤销」→ 批注从文档里消失 → 点「重做」→ 又回来；
      3. 矩形与墨迹：切到画布工具 → 在页面上拖出矩形 / 笔画；
      4. 便签：切到便签工具 → 点页面 → 弹输入框 → 填内容 → 确定；
      5. 批注面板：导航栏「批注」页签列出全部批注 → 点条目跳页 → 删除按钮删掉一条。

    坐标换算与 verify-navigation.ps1 相同：先把窗口摆定，再读视口矩形，
    最后才算屏幕坐标——顺序颠倒的话，点击会落到别处。

    annotation.pdf 的页面是 612x400 点，第 1 页四行文字的位置固定
    （y 是 PDF user space 的基线位置，显示空间 y = 400 - 基线 y）：

      "Annotation demo"                        28pt  基线 330 → 显示 y 70
      "Select this sentence to highlight it."  14pt  基线 260 → 显示 y 140
      "Drag with the square tool to draw a box."14pt 基线 212 → 显示 y 188
      "Drag with the ink tool to draw a stroke."14pt 基线 164 → 显示 y 236

    批注的证据来自两条轨迹：
      - annotation add type=N page=P id=I     模型里加了一条批注
      - annotation doc sync page=P count=N    批注已投影进 PDFium 文档
    批注面板的条目数由应用写进轨迹（面板内容不进 UIA 文本）。

    这套脚本会抢前台窗口、置顶、并用合成鼠标事件操作桌面：
    **不要在别人正在用这台机器时跑**（见 README 的「界面验收会抢桌面」）。
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build\windows-debug\bin'),
    [string]$WorkDir = (Join-Path $PSScriptRoot '..\build\annotation-acceptance'),
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

Write-Host '==> 生成测试 PDF' -ForegroundColor Cyan
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
$fixtureDir = Join-Path $WorkDir 'fixtures'
& $Python (Join-Path $PSScriptRoot 'generate-test-pdfs.py') $fixtureDir --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host '生成测试 PDF 失败' -ForegroundColor Red
    exit 2
}
$annotationPdf = Join-Path $fixtureDir 'annotation.pdf'

$tracePath = Join-Path $WorkDir 'trace.log'
$env:AIPDF_TRACE_FILE = $tracePath
# 恢复日志单独放一份：验收里改过文档，启动时会问要不要恢复，
# 问询框会挡住验收界面。指到临时目录就不会互相干扰。
$env:AIPDF_RECOVERY_DIR = Join-Path $WorkDir 'recovery'

# 记下用户真实的鼠标位置：验收要用合成鼠标事件点屏幕坐标，跑完必须放回去。
$script:CursorStart = Get-AiPdfCursorPosition

function Start-AiPdfApp {
    param(
        [Parameter(Mandatory = $true)][string]$Pdf,
        [int]$WaitSeconds = 10
    )
    $app = Start-Process -FilePath $appExe -WorkingDirectory $buildPath `
        -ArgumentList @($Pdf, '--diagnostics') -PassThru
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
    Restore-AiPdfCursorPosition -Position $script:CursorStart
}

function Prepare-AiPdfWindow {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $null = Set-AiPdfForeground -Process $Process
    Set-AiPdfWindowGeometry -Process $Process | Out-Null
    Set-AiPdfWindowTopmost -Process $Process -Enable $true | Out-Null
    return Set-AiPdfForeground -Process $Process
}

# 第 1 页上某个显示空间坐标对应的屏幕坐标。
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
    $pageMargin = 24.0
    $available = $viewportWidth - 2 * $pageMargin
    $pageLeft = [Math]::Max($pageMargin, ($available - 612.0) / 2.0 + $pageMargin)
    return [pscustomobject]@{
        X = [int]($rect.Left + ($pageLeft + $DisplayX) * $scale)
        Y = [int]($rect.Top + $DisplayY * $scale)
    }
}

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

# 在页面上拖一段（起点与终点都是显示空间坐标）。
function Invoke-AiPdfPageDrag {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][double]$FromX,
        [Parameter(Mandatory = $true)][double]$FromY,
        [Parameter(Mandatory = $true)][double]$ToX,
        [Parameter(Mandatory = $true)][double]$ToY
    )
    $null = Prepare-AiPdfWindow -Process $Process
    $start = Get-AiPdfPagePoint -Process $Process -DisplayX $FromX -DisplayY $FromY
    $end = Get-AiPdfPagePoint -Process $Process -DisplayX $ToX -DisplayY $ToY
    if ($null -eq $start -or $null -eq $end) {
        return $false
    }
    Invoke-AiPdfMouseDrag -X1 $start.X -Y1 $start.Y -X2 $end.X -Y2 $end.Y
    return $true
}

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

# 批注工具栏上的按钮（AutomationId 形如 annotation.highlight）。
function Get-AiPdfToolButton {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$IdPattern
    )
    $button = Wait-AiPdfElementById -Process $Process -IdPattern $IdPattern -TimeoutSeconds 10
    if ($button) {
        return $button
    }
    Write-Host '    批注工具栏相关的 AutomationId：' -ForegroundColor DarkGray
    foreach ($element in (Get-AiPdfWindowElements -Process $Process)) {
        try {
            $id = $element.Current.AutomationId
            if ($id -like '*annotation*') {
                Write-Host "      $id" -ForegroundColor DarkGray
            }
        } catch {
        }
    }
    return $null
}

function Get-AiPdfNavButton {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$IdPattern
    )
    return (Wait-AiPdfElementById -Process $Process -IdPattern $IdPattern -TimeoutSeconds 10)
}

# --------------------------------------------------------------- 场景 1 ------
Write-Host '==> 场景 1：高亮（选区 → 工具栏）' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $annotationPdf -WaitSeconds $StartupSeconds

    # 文本层要先提取出来，否则拖选命不中字。
    Test-Check '第 1 页文本层已就绪' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'text page=0' -TimeoutSeconds 20) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 拖选第 2 行文字（显示空间 y ≈ 145）。
    $dragged = Invoke-AiPdfPageDrag -Process $app -FromX 62.0 -FromY 135.0 -ToX 200.0 -ToY 135.0
    Test-Check '拖选了一段文字' $dragged ''
    Start-Sleep -Milliseconds 600
    Test-Check '选区里有文字' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'selection page=0 chars=[1-9]' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    $button = Get-AiPdfToolButton -Process $app -IdPattern '*annotation.highlight*'
    Test-Check '找到「高亮」按钮' ($null -ne $button) ''
    if ($button) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $button | Out-Null
        Start-Sleep -Milliseconds 800
    }

    Test-Check '高亮批注已创建' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation add type=0 page=0' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
    Test-Check '高亮批注已写进文档' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation doc sync page=0 count=1' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    if ($ScreenshotPath) {
        $taken = Save-AiPdfWindowScreenshot -Process $app -Path $ScreenshotPath
        Test-Check '批注界面截图已保存' $taken $ScreenshotPath
    }
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 2 ------
Write-Host '==> 场景 2：撤销与重做' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $annotationPdf -WaitSeconds $StartupSeconds
    Test-Check '第 1 页文本层已就绪' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'text page=0' -TimeoutSeconds 20) ''

    Invoke-AiPdfPageDrag -Process $app -FromX 62.0 -FromY 135.0 -ToX 200.0 -ToY 135.0 | Out-Null
    Start-Sleep -Milliseconds 600
    $button = Get-AiPdfToolButton -Process $app -IdPattern '*annotation.highlight*'
    if ($button) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $button | Out-Null
        Start-Sleep -Milliseconds 800
    }
    Test-Check '先有一条高亮批注' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation doc sync page=0 count=1' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 撤销：批注应当从文档里消失。
    #
    # 轨迹是追加写的：断言前先清空，否则「撤销后 count=0」可能匹配到
    # 撤销之前就存在的旧行，断言就成了空转。
    $undo = Get-AiPdfToolButton -Process $app -IdPattern '*annotation.undo*'
    Test-Check '找到「撤销」按钮' ($null -ne $undo) ''
    if ($undo) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Reset-AiPdfTrace -Path $tracePath
        Click-AiPdfElement -Process $app -Element $undo | Out-Null
        Start-Sleep -Milliseconds 800
    }
    Test-Check '撤销后文档里没有批注了' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation doc sync page=0 count=0' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 重做：批注应当回来。
    $redo = Get-AiPdfToolButton -Process $app -IdPattern '*annotation.redo*'
    Test-Check '找到「重做」按钮' ($null -ne $redo) ''
    if ($redo) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Reset-AiPdfTrace -Path $tracePath
        Click-AiPdfElement -Process $app -Element $redo | Out-Null
        Start-Sleep -Milliseconds 800
    }
    Test-Check '重做后批注又回来了' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation doc sync page=0 count=1' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 3 ------
Write-Host '==> 场景 3：矩形与墨迹工具' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $annotationPdf -WaitSeconds $StartupSeconds
    Test-Check '第 1 页文本层已就绪' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'text page=0' -TimeoutSeconds 20) ''

    # 矩形工具：拖一个框（第 3 行附近，显示空间 y ≈ 185）。
    $square = Get-AiPdfToolButton -Process $app -IdPattern '*annotation.square*'
    Test-Check '找到「矩形」按钮' ($null -ne $square) ''
    if ($square) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $square | Out-Null
        Start-Sleep -Milliseconds 500
    }
    Invoke-AiPdfPageDrag -Process $app -FromX 70.0 -FromY 180.0 -ToX 320.0 -ToY 200.0 | Out-Null
    Start-Sleep -Milliseconds 800
    Test-Check '矩形批注已创建' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation add type=4 page=0' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 墨迹工具：拖一笔（第 4 行附近，显示空间 y ≈ 233）。
    $ink = Get-AiPdfToolButton -Process $app -IdPattern '*annotation.ink*'
    Test-Check '找到「墨迹」按钮' ($null -ne $ink) ''
    if ($ink) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $ink | Out-Null
        Start-Sleep -Milliseconds 500
    }
    Invoke-AiPdfPageDrag -Process $app -FromX 80.0 -FromY 233.0 -ToX 300.0 -ToY 240.0 | Out-Null
    Start-Sleep -Milliseconds 800
    Test-Check '墨迹批注已创建' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation add type=5 page=0' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    Test-Check '两条批注都写进了文档' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation doc sync page=0 count=2' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 4 ------
Write-Host '==> 场景 4：便签（模态输入框）' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $annotationPdf -WaitSeconds $StartupSeconds

    $note = Get-AiPdfToolButton -Process $app -IdPattern '*annotation.note*'
    Test-Check '找到「便签」按钮' ($null -ne $note) ''
    if ($note) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $note | Out-Null
        Start-Sleep -Milliseconds 500
    }
    Invoke-AiPdfPageClick -Process $app -DisplayX 400.0 -DisplayY 120.0 | Out-Null
    Start-Sleep -Milliseconds 800

    $filled = Enter-AiPdfDialogText -Process $app -Text '验收便签' -TitlePattern '新建便签' -TimeoutSeconds 10
    Test-Check '便签输入框已填写并确定' $filled ''
    Start-Sleep -Milliseconds 800

    Test-Check '便签批注已创建' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation add type=3 page=0' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
    Test-Check '便签写进了文档' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation doc sync page=0 count=1' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
} finally {
    Stop-AiPdfApp -App $app
}

# --------------------------------------------------------------- 场景 5 ------
Write-Host '==> 场景 5：批注面板与删除' -ForegroundColor Cyan
$app = $null
try {
    Reset-AiPdfTrace -Path $tracePath
    $app = Start-AiPdfApp -Pdf $annotationPdf -WaitSeconds $StartupSeconds
    Test-Check '第 1 页文本层已就绪' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'text page=0' -TimeoutSeconds 20) ''

    # 做两条批注：高亮 + 矩形。
    Invoke-AiPdfPageDrag -Process $app -FromX 62.0 -FromY 135.0 -ToX 200.0 -ToY 135.0 | Out-Null
    Start-Sleep -Milliseconds 600
    $highlight = Get-AiPdfToolButton -Process $app -IdPattern '*annotation.highlight*'
    if ($highlight) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $highlight | Out-Null
        Start-Sleep -Milliseconds 600
    }
    $square = Get-AiPdfToolButton -Process $app -IdPattern '*annotation.square*'
    if ($square) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $square | Out-Null
        Start-Sleep -Milliseconds 400
    }
    Invoke-AiPdfPageDrag -Process $app -FromX 70.0 -FromY 180.0 -ToX 320.0 -ToY 200.0 | Out-Null
    Start-Sleep -Milliseconds 800
    Test-Check '文档里有两条批注' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation doc sync page=0 count=2' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)

    # 打开批注面板。
    $navButton = Get-AiPdfNavButton -Process $app -IdPattern '*railButton.annotations*'
    Test-Check '找到「批注」页签按钮' ($null -ne $navButton) ''
    if ($navButton) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        Click-AiPdfElement -Process $app -Element $navButton | Out-Null
        Start-Sleep -Milliseconds 800
    }
    Test-Check '批注面板里有 2 条批注' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'panel annotations items=2' -TimeoutSeconds 10) `
        (Get-AiPdfTraceText -Path $tracePath)

    $list = Wait-AiPdfElementById -Process $app -IdPattern '*annotationList*' -TimeoutSeconds 10
    Test-Check '找到批注列表' ($null -ne $list) ''

    # 删除：选中第一条 → 点删除按钮。
    if ($list) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        $items = $list.FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition)
        if ($items.Count -gt 0) {
            $bounds = $items.Item(0).Current.BoundingRectangle
            Invoke-AiPdfMouseClick -X ([int]($bounds.Left + $bounds.Width / 2)) `
                -Y ([int]($bounds.Top + $bounds.Height / 2))
            Start-Sleep -Milliseconds 600
        }
    }
    $remove = Get-AiPdfToolButton -Process $app -IdPattern '*annotationRemove*'
    Test-Check '找到「删除所选批注」按钮' ($null -ne $remove) ''
    if ($remove) {
        Prepare-AiPdfWindow -Process $app | Out-Null
        # 删除前清空轨迹：否则「只剩一条」可能匹配到删除前就存在的旧行。
        Reset-AiPdfTrace -Path $tracePath
        Click-AiPdfElement -Process $app -Element $remove | Out-Null
        Start-Sleep -Milliseconds 800
    }
    Test-Check '删除后文档里只剩一条批注' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'annotation doc sync page=0 count=1' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
    Test-Check '批注面板同步刷新为 1 条' `
        (Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'panel annotations items=1' -TimeoutSeconds 5) `
        (Get-AiPdfTraceText -Path $tracePath)
} finally {
    Stop-AiPdfApp -App $app
}

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "批注验收失败：$($script:Failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host '批注验收全部通过' -ForegroundColor Green
exit 0
