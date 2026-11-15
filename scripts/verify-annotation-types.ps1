# v1.4.0 界面验收：批注工具栏与审阅入口是否真的接上了。
#
# 判据只有一条但很硬：每个工具按钮都必须能在 UI Automation 树里按 AutomationId
# 找到。按钮接错了配对时，控件仍然存在、编译也过，只有"点下去什么都不发生"——
# 所以这里先守住"存在且可寻址"，绘制动作本身由单测与人工抽查覆盖。
#
# 会抢前台，只应在 CI 或空闲机器上跑。
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [string]$ScreenshotPath = '',
    [string]$RepositoryRoot = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')

$script:failures = 0
function Test-Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    if ($Ok) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name $Detail" -ForegroundColor Red
        $script:failures++
    }
}

$exe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'AiPdfEditor.exe'
$fixture = Join-Path $RepositoryRoot 'build/pdf-fixtures/annotation.pdf'
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-anno-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$tracePath = Join-Path $workDir 'trace.log'

$process = $null
try {
    Test-Check '主程序可执行文件存在' (Test-Path -LiteralPath $exe) $exe
    Test-Check '样例文档存在' (Test-Path -LiteralPath $fixture) $fixture
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $fixture)) {
        throw '缺少可执行文件或样例'
    }
    if ($DryRun) {
        Write-Host 'DRY RUN：前置检查通过，未启动界面' -ForegroundColor Yellow
        exit 0
    }

    $cursor = Get-AiPdfCursorPosition
    $env:AIPDF_TRACE_FILE = $tracePath
    $env:AIPDF_DATA_DIR = (Join-Path $workDir 'data')
    $env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
    Reset-AiPdfTrace -Path $tracePath

    # 上一轮残留的实例会抢前台：先清掉，否则"窗口就绪"会随机失败。
    Get-Process -Name AiPdfEditor -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500

    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $fixture + '"') -PassThru
    Test-Check '主程序已启动' ($null -ne $process)

    $started = Get-Date
    $deadline = (Get-Date).AddSeconds(90)
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        if ($process.HasExited) { break }
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            Set-AiPdfWindowTopmost -Process $process -Enable $true -ErrorAction SilentlyContinue | Out-Null
            if (Test-AiPdfForeground -Process $process) { $ready = $true; break }
            # 前台切换被系统拒绝时（同机有别的程序在抢焦点），点一下标题栏把它顶上来。
            if (((Get-Date) - $started).TotalSeconds -gt 6) {
                $bounds = Get-AiPdfWindowRect -Process $process
                if ($null -ne $bounds) {
                    Invoke-AiPdfMouseClick -X ($bounds.Left + 400) -Y ($bounds.Top + 8)
                }
            }
        }
        Start-Sleep -Milliseconds 400
    }
    Test-Check '主窗口就绪并可切到前台' $ready

    if ($ready) {
        # 批注工具都在 Ribbon 的「注释」页；不切页，按钮不在 UIA 树里。
        $null = Select-AiPdfRibbonTab -Process $process -Name '注释'
        $tools = [ordered]@{
            '高亮'   = '*annotation.highlight*'
            '下划线' = '*annotation.underline*'
            '删除线' = '*annotation.strikethrough*'
            '波浪线' = '*annotation.squiggly*'
            '便签'   = '*annotation.note*'
            '矩形'   = '*annotation.square*'
            '椭圆'   = '*annotation.ellipse*'
            '直线'   = '*annotation.line*'
            '箭头'   = '*annotation.arrow*'
            '多边形' = '*annotation.polygon*'
            '云线'   = '*annotation.cloud*'
            '文本框' = '*annotation.freetext*'
            '图章'   = '*annotation.stamp*'
            '测量'   = '*annotation.measure*'
            '附件'   = '*annotation.attachment*'
            '图片图章' = '*annotation.imagestamp*'
            '声音'   = '*annotation.sound*'
            '橡皮'   = '*annotation.eraser*'
            '墨迹'   = '*annotation.ink*'
        }
        foreach ($entry in $tools.GetEnumerator()) {
            $element = Wait-AiPdfElementById -Process $process -IdPattern $entry.Value -TimeoutSeconds 10
            Test-Check ("工具栏有「" + $entry.Key + "」") ($null -ne $element)
        }

        # 审阅入口：这两个按钮在批注面板里，面板未展开时不在 UIA 树上是正常的。
        # 导航栏按钮不支持 InvokePattern，所以用 F7——它是给用户准备的快捷键，
        # 同时是唯一能被脚本可靠触发的"展开面板"手段。
        Send-AiPdfKey -Key 'F7'
        $panelOpen = Wait-AiPdfTraceMatch -Path $tracePath -Pattern 'open annotation panel' -TimeoutSeconds 10
        Test-Check 'F7 打开批注面板' $panelOpen
        if ($panelOpen) {
            $reply = Wait-AiPdfElementById -Process $process -IdPattern '*annotationReply*' -TimeoutSeconds 10
            Test-Check '批注面板有「回复所选」' ($null -ne $reply)
            $state = Wait-AiPdfElementById -Process $process -IdPattern '*annotationState*' -TimeoutSeconds 10
            Test-Check '批注面板有「设置审阅状态」' ($null -ne $state)
            # 面板里的「回复所选 / 设置审阅状态」按 UIA 找不到：它们所在的堆叠页
            # 只有用真实点击切过去才会进可访问树，而导航栏按钮不支持 InvokePattern。
            # 这两个入口改由人工抽查覆盖，文档里已注明。
        }

        if (-not [string]::IsNullOrWhiteSpace($ScreenshotPath)) {
            Save-AiPdfWindowScreenshot -Process $process -Path $ScreenshotPath -ErrorAction SilentlyContinue
        }
    }
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit(5000) | Out-Null
    }
    if ($null -ne $process) {
        Set-AiPdfWindowTopmost -Process $process -Enable $false -ErrorAction SilentlyContinue | Out-Null
    }
    Stop-AiPdfOrphanWorker -ErrorAction SilentlyContinue
    if ($null -ne $cursor) { Restore-AiPdfCursorPosition -Position $cursor }
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures -gt 0) {
    Write-Host "`n批注工具栏验收失败：$($script:failures) 项未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`n批注工具栏验收通过" -ForegroundColor Green
exit 0
