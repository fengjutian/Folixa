<#
.SYNOPSIS
    K3 / K4 / K6 性能与容量预算验收。

.DESCRIPTION
    三项预算都在这个脚本里量，阈值可以用命令行参数覆盖：

      K3 滚动帧率（>= 55 fps）
          驱动应用连续滚动一屏，同时在屏幕 DC 上高频抓取窗口中央区域，
          用像素校验和判断「一帧是否已经呈现出来」，由此算出帧率与 P95 帧时间。

          口径说明（很重要）：抓屏与屏幕刷新同步，单次抓取约 15ms，
          所以采样率天然被屏幕刷新率卡住（60Hz 屏约 60 次/秒）。
          测到的 fps 是「屏幕内容每秒变化多少次」，也就是**用户实际看到的
          有效呈现帧率**，不是应用内部的绘制调用次数。
          这正是「滚动帧率」该有的口径：应用画得再快，屏幕也只有 60 帧。
          因此本项**分辨不出 60fps 与 120fps**，但能分清 30fps 与 55fps。
          阈值依据：docs\FOXIT_BENCHMARK.md 的 K3（对齐福昕，>= 55fps）。
          60Hz 屏幕一帧 16.7ms，55fps 相当于允许偶发掉帧，但不能持续低于。

      K4 1 GB 级文件可开可翻
          打开大文件语料，测「打开耗时」与「翻页响应」。
          阈值依据：1 GiB 文件按 60 MB/s 的慢盘/网络盘估算，读盘本身就要 ~17s，
          60s 的打开预算留了 3 倍余量给解析与首屏渲染；
          翻页预算 2000ms 是「跳到第 N 页 + 渲染该页」，
          本地渲染一帧是几十毫秒量级，2000ms 给慢机器留足余量。

      K6 500 页文档峰值内存（< 400 MB）
          打开 500 页文档并翻一遍，采样进程峰值工作集
          （Process.PeakWorkingSet64，Windows 上与「峰值 RSS」对应的高水位）。
          阈值依据：docs\FOXIT_BENCHMARK.md 的 K6（超越，< 400MB）。

    语料由 scripts\gen-compat-corpus.py 生成：
        python scripts\gen-compat-corpus.py --out build\corpus --count 1000 --with-large

    本脚本会启动应用、合成鼠标滚轮事件并抓屏：跑之前请先把手头的事放一放。

.PARAMETER DryRun
    只打印计划（语料、阈值、采样步骤），不启动应用。
#>
param(
    [string]$BuildDir = 'build/windows-debug/bin',
    [string]$CorpusDir = 'build/corpus',
    [string]$RepositoryRoot = '',
    [string]$Python = 'python',
    [int]$MinFps = 55,
    [int]$MaxP95FrameMs = 0,
    [int]$MaxOpenMs = 60000,
    [int]$MaxPageTurnMs = 2000,
    [int]$MaxPeakWorkingSetMiB = 400,
    [int]$ScrollSampleMs = 5000,
    [int]$WheelIntervalMs = 8,
    [string]$ReportPath = '',
    [switch]$SelfTest,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
. (Join-Path $PSScriptRoot 'AcceptanceProbe.ps1')

$script:failures = 0
$script:rows = New-Object System.Collections.Generic.List[object]

function Add-BudgetRow {
    param([string]$Item, [string]$Measured, [string]$Budget, [bool]$Ok, [string]$Note = '')
    $script:rows.Add([pscustomobject]@{ Item = $Item; Measured = $Measured; Budget = $Budget; Ok = $Ok; Note = $Note })
    if ($Ok) {
        Write-Host ("  PASS  {0,-26} {1,-16} 预算 {2} {3}" -f $Item, $Measured, $Budget, $Note) -ForegroundColor Green
    } else {
        Write-Host ("  FAIL  {0,-26} {1,-16} 预算 {2} {3}" -f $Item, $Measured, $Budget, $Note) -ForegroundColor Red
        $script:failures++
    }
}

# --------------------------------------------------------------------------
# 采样用的原生代码
# --------------------------------------------------------------------------

if (-not ('AiPdfScrollProbe' -as [type])) {
    Add-Type @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

public static class AiPdfScrollProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct BITMAPINFOHEADER {
        public uint biSize; public int biWidth; public int biHeight;
        public ushort biPlanes; public ushort biBitCount; public uint biCompression;
        public uint biSizeImage; public int biXPelsPerMeter; public int biYPelsPerMeter;
        public uint biClrUsed; public uint biClrImportant;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct BITMAPINFO { public BITMAPINFOHEADER bmiHeader; public uint bmiColors; }

    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, int data, UIntPtr extra);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr hdc);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateDIBSection(IntPtr hdc, ref BITMAPINFO pbmi, uint usage, out IntPtr bits, IntPtr hSection, uint offset);
    [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr hdc, IntPtr hObject);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr hObject);
    [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr hdc);
    [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr hdcDest, int xDest, int yDest, int w, int h, IntPtr hdcSrc, int xSrc, int ySrc, uint rop);
    [DllImport("winmm.dll")] public static extern uint timeBeginPeriod(uint period);
    [DllImport("winmm.dll")] public static extern uint timeEndPeriod(uint period);

    public const uint SRCCOPY = 0x00CC0020;
    public const uint DIB_RGB_COLORS = 0;
    public const uint MOUSEEVENTF_WHEEL = 0x0800;

    public class Result {
        public int Samples;
        public int Frames;
        public int WheelNotches;
        public double DurationMs;
        public double Fps;
        public double P50Ms;
        public double P95Ms;
        public double MaxMs;
        public double SampleHz;
        public double CaptureMs;
        public double AvgSampleMs;
        public string Error;
    }

    // 连续滚轮 + 高频抓屏：抓到的像素校验和变了，就算「一帧已经呈现」。
    public static Result Measure(int left, int top, int width, int height,
                                  int durationMs, int wheelIntervalMs,
                                  int cursorX, int cursorY) {
        Result result = new Result();
        if (width < 32 || height < 32) {
            result.Error = "采样区域太小";
            return result;
        }
        IntPtr screenDc = GetDC(IntPtr.Zero);
        if (screenDc == IntPtr.Zero) {
            result.Error = "拿不到屏幕 DC（会话里没有桌面？）";
            return result;
        }
        IntPtr memDc = CreateCompatibleDC(screenDc);
        BITMAPINFO info = new BITMAPINFO();
        info.bmiHeader.biSize = (uint)Marshal.SizeOf(typeof(BITMAPINFOHEADER));
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;   // 负数 = 自上而下
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = 0;
        IntPtr bits;
        IntPtr dib = CreateDIBSection(memDc, ref info, DIB_RGB_COLORS, out bits, IntPtr.Zero, 0);
        if (dib == IntPtr.Zero) {
            ReleaseDC(IntPtr.Zero, screenDc);
            DeleteDC(memDc);
            result.Error = "建不了 DIB";
            return result;
        }
        IntPtr previous = SelectObject(memDc, dib);

        SetCursorPos(cursorX, cursorY);
        Thread.Sleep(150);

        bool stop = false;
        int notches = 0;
        Thread wheel = null;
        if (wheelIntervalMs > 0) {
            wheel = new Thread(delegate() {
                Stopwatch watch = Stopwatch.StartNew();
                int sent = 0;
                while (!stop) {
                    if (watch.ElapsedMilliseconds < sent * wheelIntervalMs) {
                        Thread.Sleep(1);
                        continue;
                    }
                    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, UIntPtr.Zero);
                    notches++;
                    sent++;
                    if (sent > 200000) break;
                }
            });
            wheel.IsBackground = true;
            wheel.Start();
        }

        int stride = width * 4;
        byte[] buffer = new byte[stride * height];
        List<double> frames = new List<double>();
        Stopwatch clock = Stopwatch.StartNew();
        ulong last = 0;
        bool haveLast = false;
        int samples = 0;
        double captureMs = 0;
        // 采样要够密才分得清 55fps 与 30fps：把系统计时器精度提到 1ms，
        // 睡 1ms 而不是 Thread.Sleep(0)（后者会把采样率压到 60Hz 上下）。
        timeBeginPeriod(1);
        try {
            while (clock.ElapsedMilliseconds < durationMs) {
                long captureStart = clock.ElapsedTicks;
                if (BitBlt(memDc, 0, 0, width, height, screenDc, left, top, SRCCOPY)) {
                    Marshal.Copy(bits, buffer, 0, buffer.Length);
                    ulong hash = Checksum(buffer, stride, height);
                    samples++;
                    if (!haveLast) {
                        haveLast = true;
                        last = hash;
                    } else if (hash != last) {
                        last = hash;
                        frames.Add(clock.Elapsed.TotalMilliseconds);
                    }
                }
                captureMs += (clock.ElapsedTicks - captureStart) * 1000.0 / Stopwatch.Frequency;
                Thread.Sleep(1);
            }
        } finally {
            timeEndPeriod(1);
        }
        stop = true;
        if (wheel != null) { wheel.Join(500); }
        clock.Stop();

        SelectObject(memDc, previous);
        DeleteObject(dib);
        DeleteDC(memDc);
        ReleaseDC(IntPtr.Zero, screenDc);

        result.Samples = samples;
        result.Frames = frames.Count;
        result.WheelNotches = notches;
        result.DurationMs = clock.Elapsed.TotalMilliseconds;
        result.SampleHz = result.DurationMs > 0 ? samples * 1000.0 / result.DurationMs : 0;
        result.CaptureMs = captureMs;
        result.AvgSampleMs = samples > 0 ? captureMs / samples : 0;
        FillStats(result, frames);
        return result;
    }

    // 帧时间统计：间隔 -> fps / P50 / P95 / 最长帧。
    public static void FillStats(Result result, List<double> frames) {
        if (frames.Count < 2) { return; }
        result.Frames = frames.Count;
        List<double> intervals = new List<double>();
        for (int i = 1; i < frames.Count; i++) {
            intervals.Add(frames[i] - frames[i - 1]);
        }
        intervals.Sort();
        double span = frames[frames.Count - 1] - frames[0];
        result.Fps = span > 0 ? 1000.0 * intervals.Count / span : 0;
        result.P50Ms = Percentile(intervals, 0.50);
        result.P95Ms = Percentile(intervals, 0.95);
        result.MaxMs = intervals[intervals.Count - 1];
    }

    // 已知答案的算例：11 帧、间隔九个 10ms 加一个 30ms。
    //   fps  = 10 * 1000 / 120 = 83.33
    //   P50  = 10.00   P95 = 21.00   最长 = 30.00
    public static string StatsSelfTest() {
        List<double> frames = new List<double>();
        double t = 0;
        for (int i = 0; i < 10; i++) { frames.Add(t); t += 10.0; }
        t += 20.0;
        frames.Add(t);
        Result result = new Result();
        FillStats(result, frames);
        return string.Format("frames={0} fps={1:F2} p50={2:F2} p95={3:F2} max={4:F2}",
            result.Frames, result.Fps, result.P50Ms, result.P95Ms, result.MaxMs);
    }

    static double Percentile(List<double> sorted, double ratio) {
        if (sorted.Count == 0) { return 0; }
        double index = ratio * (sorted.Count - 1);
        int low = (int)Math.Floor(index);
        int high = (int)Math.Ceiling(index);
        if (low == high) { return sorted[low]; }
        return sorted[low] + (sorted[high] - sorted[low]) * (index - low);
    }

    // 每 4 个像素取 1 个、每 2 行取 1 行：够灵敏，又不会把采样拖慢。
    static ulong Checksum(byte[] pixels, int stride, int height) {
        ulong hash = 1469598103934665603UL;
        for (int y = 0; y < height; y += 2) {
            int row = y * stride;
            for (int x = 0; x < stride; x += 16) {
                hash ^= pixels[row + x];
                hash *= 1099511628211UL;
                hash ^= pixels[row + x + 1];
                hash *= 1099511628211UL;
                hash ^= pixels[row + x + 2];
                hash *= 1099511628211UL;
            }
        }
        return hash;
    }
}
'@
}

# --------------------------------------------------------------------------
# 小工具
# --------------------------------------------------------------------------

function Start-AiPdfApp {
    param(
        [string]$Exe,
        [string]$BuildPath,
        [string]$Document,
        [string]$TracePath
    )
    $env:AIPDF_SKIP_RECOVERY_PROMPT = '1'
    $env:AIPDF_TRACE_FILE = $TracePath
    $arguments = @()
    if ($Document) { $arguments += ('"' + $Document + '"') }
    $arguments += '--diagnostics'
    return Start-Process -FilePath $Exe -WorkingDirectory $BuildPath -ArgumentList $arguments -PassThru
}

function Stop-AiPdfApp {
    param([System.Diagnostics.Process]$Process)
    if ($Process -and -not $Process.HasExited) {
        $Process.CloseMainWindow() | Out-Null
        if (-not $Process.WaitForExit(5000)) {
            $Process.Kill()
            $Process.WaitForExit(5000) | Out-Null
        }
    }
    Stop-AiPdfOrphanWorker -Quiet | Out-Null
}

# 等 trace 里出现 open 记录，返回 (kind, msg, 耗时毫秒)
function Wait-OpenOutcome {
    param(
        [System.Diagnostics.Process]$Process,
        [string]$TracePath,
        [int]$TimeoutSeconds = 120
    )
    $started = Get-Date
    $deadline = $started.AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return @{ Kind = 'exited'; Message = "进程退出，退出码 $($Process.ExitCode)"; ElapsedMs = 0 }
        }
        $trace = Get-AiPdfTraceText -Path $TracePath
        foreach ($line in ($trace -split "`n")) {
            if ($line -match 'open file=.* required=(\d+) msg=(.*)$') {
                $message = $Matches[2].Trim()
                return @{
                    Kind      = $(if ($message -eq '') { 'opened' } else { 'rejected' })
                    Message   = $message
                    ElapsedMs = [int]((Get-Date) - $started).TotalMilliseconds
                }
            }
        }
        Start-Sleep -Milliseconds 100
    }
    return @{ Kind = 'timeout'; Message = '没有等到 open 记录'; ElapsedMs = [int]((Get-Date) - $started).TotalMilliseconds }
}

# 当前页码（状态栏「第 X / N 页」）
function Get-CurrentPage {
    param([System.Diagnostics.Process]$Process)
    $probe = Get-AiPdfWindowProbe -Process $Process -TitlePattern 'AI PDF Editor'
    if ($probe -and $probe.Text -match '第\s*(\d+)\s*/\s*(\d+)\s*页') {
        return [int]$Matches[1]
    }
    return -1
}

# 竖直滚动条：UI Automation 暴露为 ScrollBar 控件，取值范围比视口高得多。
function Get-VerticalScrollBar {
    param([System.Diagnostics.Process]$Process)
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $pidCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $Process.Id)
    $window = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $pidCondition)
    if (-not $window) { return $null }
    $barCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::ScrollBar)
    foreach ($candidate in $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, $barCondition)) {
        $range = $null
        if ($candidate.TryGetCurrentPattern(
                [System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$range)) {
            if ($range.Current.Maximum - $range.Current.Minimum -gt 1000) {
                return $candidate
            }
        }
    }
    return $null
}

# 跳到滚动条的某个比例，等页码变了为止，返回耗时毫秒（-1 表示没等到）。
function Measure-PageTurn {
    param(
        [System.Diagnostics.Process]$Process,
        $ScrollBar,
        [double]$Ratio,
        [int]$TimeoutSeconds = 30
    )
    $before = Get-CurrentPage -Process $Process
    $range = $null
    if (-not $ScrollBar.TryGetCurrentPattern(
            [System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$range)) {
        return -1
    }
    $minimum = $range.Current.Minimum
    $maximum = $range.Current.Maximum
    $target = $minimum + ($maximum - $minimum) * $Ratio
    $started = Get-Date
    $range.SetValue([double]$target)
    $deadline = $started.AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $now = Get-CurrentPage -Process $Process
        if ($now -gt 0 -and $now -ne $before) {
            return [int]((Get-Date) - $started).TotalMilliseconds
        }
        Start-Sleep -Milliseconds 50
    }
    return -1
}

# --------------------------------------------------------------------------
# 路径与语料
# --------------------------------------------------------------------------

$exe = Join-Path (Join-Path $RepositoryRoot $BuildDir) 'AiPdfEditor.exe'
$buildPath = Join-Path $RepositoryRoot $BuildDir
# 坐标要物理像素：脚本自己得先 DPI aware，否则屏幕坐标会被虚拟化，
# 抓屏区域与滚轮落点会整体偏掉。
$null = Enable-AiPdfDpiAwareness
if (-not [System.IO.Path]::IsPathRooted($CorpusDir)) {
    $CorpusDir = Join-Path $RepositoryRoot $CorpusDir
}
$pagesDoc = Join-Path (Join-Path $CorpusDir 'pages') 'pages-500.pdf'
$largeDoc = $null
$largeDir = Join-Path $CorpusDir 'large'
if (Test-Path -LiteralPath $largeDir) {
    $found = @(Get-ChildItem -LiteralPath $largeDir -Filter 'large-*.pdf' | Sort-Object Name)
    if ($found.Count -gt 0) { $largeDoc = $found[0].FullName }
}

Write-Host '==> 性能预算' -ForegroundColor Cyan
Write-Host "    主程序      ：$exe"
Write-Host "    语料目录    ：$CorpusDir"
Write-Host "    500 页文档  ：$(if (Test-Path -LiteralPath $pagesDoc) { $pagesDoc } else { '（缺，先跑 gen-compat-corpus.py）' })"
Write-Host "    大文件语料  ：$(if ($largeDoc) { $largeDoc } else { '（缺，先跑 gen-compat-corpus.py --with-large）' })"
Write-Host "    阈值        ：fps >= $MinFps；P95 帧时间 <= $(if ($MaxP95FrameMs -gt 0) { "$MaxP95FrameMs ms" } else { '不设硬断言' })；打开 <= $MaxOpenMs ms；翻页 <= $MaxPageTurnMs ms；峰值工作集 < $MaxPeakWorkingSetMiB MiB"

# 离线自检：验帧时间统计的算术 + 验抓屏通路（只读 64x64 像素，不合成输入、不启动应用）。
if ($SelfTest) {
    Write-Host '==> SelfTest' -ForegroundColor Cyan
    $stats = [AiPdfScrollProbe]::StatsSelfTest()
    Write-Host "    统计算例    ：$stats"
    $statsOk = ($stats -match 'frames=11 fps=83\.33 p50=10\.00 p95=21\.00 max=30\.00')
    if ($statsOk) {
        Write-Host '  PASS  帧时间统计（fps / P50 / P95 / 最长）' -ForegroundColor Green
    } else {
        Write-Host "  FAIL  帧时间统计算例不对：$stats" -ForegroundColor Red
    }
    $cursor = Get-AiPdfCursorPosition
    $x = 0
    $y = 0
    if ($cursor) { $x = $cursor.X; $y = $cursor.Y }
    # wheelIntervalMs = 0 -> 不发滚轮事件；光标停在原地不动。
    $capture = [AiPdfScrollProbe]::Measure($x, $y, 64, 64, 200, 0, $x, $y)
    $captureOk = ($null -eq $capture.Error) -and ($capture.Samples -gt 0)
    if ($captureOk) {
        Write-Host ("  PASS  抓屏通路（采样 {0} 次 / {1:N0} Hz / 单次抓取 {2:N2} ms / 滚轮 {3} 次）" -f `
            $capture.Samples, $capture.SampleHz, $capture.AvgSampleMs, $capture.WheelNotches) -ForegroundColor Green
    } else {
        Write-Host ("  FAIL  抓屏通路：{0}（采样 {1} 次）" -f $capture.Error, $capture.Samples) -ForegroundColor Red
    }
    # 同样的区域再抓一次大块的：单次耗时几乎不变就说明瓶颈是垂直同步而不是拷贝。
    $captureBig = [AiPdfScrollProbe]::Measure($x, $y, 400, 300, 200, 0, $x, $y)
    Write-Host ("    诊断        ：64x64 单次 {0:N2} ms；400x300 单次 {1:N2} ms（采样率 {2:N0} Hz）" -f `
        $capture.AvgSampleMs, $captureBig.AvgSampleMs, $captureBig.SampleHz)
    Restore-AiPdfCursorPosition -Position $cursor
    if ($statsOk -and $captureOk) {
        Write-Host "`nSelfTest 通过：统计算术与抓屏通路都正常" -ForegroundColor Green
        exit 0
    }
    Write-Host "`nSelfTest 失败" -ForegroundColor Red
    exit 1
}

if ($DryRun) {
    Write-Host ''
    Write-Host '==> DryRun：不启动应用，只列计划' -ForegroundColor Yellow
    Write-Host '    K3：打开 500 页文档 -> 置顶窗口 -> 连续滚轮 5 秒 -> 高频抓屏算帧率'
    Write-Host '    K4：打开大文件语料 -> 测打开耗时 -> 滚动条跳页测翻页响应'
    Write-Host '    K6：打开 500 页文档 -> 翻 12 屏 -> 读峰值工作集'
    Write-Host ''
    Write-Host 'DryRun 结束：没有启动任何应用进程。' -ForegroundColor Green
    exit 0
}

if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "主程序不存在：$exe" -ForegroundColor Red
    exit 2
}

$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-perf-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$tracePath = Join-Path $workDir 'trace.log'
$cursorBefore = Get-AiPdfCursorPosition
$process = $null

try {
    # ---------------- K3 滚动帧率 ----------------
    Write-Host ''
    Write-Host '==> K3 滚动帧率' -ForegroundColor Cyan
    if (-not (Test-Path -LiteralPath $pagesDoc)) {
        Add-BudgetRow 'K3 滚动帧率' '无法采样' ">= $MinFps fps" $false '缺少 500 页语料'
    } else {
        $process = Start-AiPdfApp -Exe $exe -BuildPath $buildPath -Document $pagesDoc -TracePath $tracePath
        Start-Sleep -Seconds 12
        $probe = Wait-AiPdfWindowProbe -Process $process -TitlePattern 'AI PDF Editor' -TimeoutSeconds 30
        $rect = $null
        if ($probe) { $rect = Get-AiPdfWindowRect -Process $process }
        if (-not $probe) {
            Add-BudgetRow 'K3 滚动帧率' '无法采样' ">= $MinFps fps" $false '主窗口没出来'
        } elseif (-not $rect) {
            Add-BudgetRow 'K3 滚动帧率' '无法采样' ">= $MinFps fps" $false '拿不到窗口矩形'
        } else {
            # 置顶：抓的是屏幕，不置顶就可能抓到别人盖在上面的窗口。
            Set-AiPdfWindowTopmost -Process $process -Enable $true | Out-Null
            Set-AiPdfForeground -Process $process -TitlePattern 'AI PDF Editor' | Out-Null
            # 采样区域取窗口中央的一块：中央基本就是画布，滚动一定会让它变；
            # 区域小一点，采样率才上得去（每帧的抓取+校验要压在 2ms 以内）。
            $width = [Math]::Min(400, $rect.Width - 16)
            $height = [Math]::Min(300, $rect.Height - 80)
            if ($width -lt 64) { $width = 64 }
            if ($height -lt 64) { $height = 64 }
            $left = $rect.Left + [int](($rect.Width - $width) / 2)
            $top = $rect.Top + [int](($rect.Height - $height) / 2)
            $cursorX = $rect.Left + [int]($rect.Width / 2)
            $cursorY = $rect.Top + [int]($rect.Height / 2)
            Write-Host "    采样区域 ${width}x${height} @ ($left,$top)，采样窗口 $ScrollSampleMs ms"
            $sample = [AiPdfScrollProbe]::Measure(
                $left, $top, $width, $height, $ScrollSampleMs, $WheelIntervalMs, $cursorX, $cursorY)
            Set-AiPdfWindowTopmost -Process $process -Enable $false | Out-Null
            Restore-AiPdfCursorPosition -Position $cursorBefore

            Write-Host ("    采样 {0} 次 / 检出 {1} 帧 / 滚轮 {2} 次 / 采样率 {3:N0} Hz（与屏幕刷新同步，上限 = 刷新率）" -f `
                $sample.Samples, $sample.Frames, $sample.WheelNotches, $sample.SampleHz)
            if ($sample.Error) {
                Add-BudgetRow 'K3 滚动帧率' '无法采样' ">= $MinFps fps" $false $sample.Error
            } elseif ($sample.SampleHz -lt 30) {
                # 抓屏与屏幕刷新同步，采样率天然被刷新率卡住；
                # 低于 30Hz 就连「30fps」和「55fps」都分不出来，这种结果没有意义。
                Add-BudgetRow 'K3 滚动帧率' ("{0:N1} fps" -f $sample.Fps) ">= $MinFps fps" $false `
                    ("采样率只有 {0:N0} Hz，分不清帧率" -f $sample.SampleHz)
            } elseif ($sample.Frames -lt 10) {
                Add-BudgetRow 'K3 滚动帧率' ("{0:N1} fps" -f $sample.Fps) ">= $MinFps fps" $false `
                    ("只采到 {0} 帧，样本太少" -f $sample.Frames)
            } else {
                Add-BudgetRow 'K3 滚动帧率' ("{0:N1} fps" -f $sample.Fps) ">= $MinFps fps" ($sample.Fps -ge $MinFps) `
                    ("P50 {0:N1} ms / P95 {1:N1} ms / 最长 {2:N1} ms" -f $sample.P50Ms, $sample.P95Ms, $sample.MaxMs)
                if ($MaxP95FrameMs -gt 0) {
                    Add-BudgetRow 'K3 P95 帧时间' ("{0:N1} ms" -f $sample.P95Ms) "<= $MaxP95FrameMs ms" `
                        ($sample.P95Ms -le $MaxP95FrameMs)
                }
                if ($sample.SampleHz -gt 0 -and ($sample.Fps -gt ($sample.SampleHz * 0.95))) {
                    Write-Host ("    说明：实测帧率已接近屏幕刷新率上限（{0:N0} Hz），呈现已跟满刷新率" -f $sample.SampleHz) -ForegroundColor Yellow
                }
            }
        }
        Stop-AiPdfApp -Process $process
        $process = $null
    }

    # ---------------- K4 大文件 ----------------
    Write-Host ''
    Write-Host '==> K4 1 GB 级文件可开可翻' -ForegroundColor Cyan
    if (-not $largeDoc) {
        Add-BudgetRow 'K4 打开大文件' '缺少语料' "<= $MaxOpenMs ms" $false '先用 --with-large 生成大文件语料'
    } else {
        $sizeMiB = [Math]::Round((Get-Item -LiteralPath $largeDoc).Length / 1MB, 1)
        Write-Host "    文件：$largeDoc（$sizeMiB MiB）"
        Reset-AiPdfTrace -Path $tracePath
        $process = Start-AiPdfApp -Exe $exe -BuildPath $buildPath -Document $largeDoc -TracePath $tracePath
        $outcome = Wait-OpenOutcome -Process $process -TracePath $tracePath -TimeoutSeconds 180
        if ($outcome.Kind -eq 'rejected') {
            Add-BudgetRow 'K4 打开大文件' ('被拒绝（{0} ms）' -f $outcome.ElapsedMs) "<= $MaxOpenMs ms" $false `
                ("应用给出的原因：{0}" -f $outcome.Message)
        } elseif ($outcome.Kind -ne 'opened') {
            Add-BudgetRow 'K4 打开大文件' $outcome.Kind "<= $MaxOpenMs ms" $false $outcome.Message
        } else {
            Add-BudgetRow 'K4 打开大文件' ("{0} ms" -f $outcome.ElapsedMs) "<= $MaxOpenMs ms" `
                ($outcome.ElapsedMs -le $MaxOpenMs)
            $scrollBar = Get-VerticalScrollBar -Process $process
            if (-not $scrollBar) {
                Add-BudgetRow 'K4 翻页响应' '无滚动条' "<= $MaxPageTurnMs ms" $false '找不到竖直滚动条'
            } else {
                $turn = Measure-PageTurn -Process $process -ScrollBar $scrollBar -Ratio 0.5 -TimeoutSeconds 60
                if ($turn -lt 0) {
                    Add-BudgetRow 'K4 翻页响应' '没等到页码变化' "<= $MaxPageTurnMs ms" $false '页码没变'
                } else {
                    Add-BudgetRow 'K4 翻页响应' ("{0} ms" -f $turn) "<= $MaxPageTurnMs ms" ($turn -le $MaxPageTurnMs) `
                        '（含 UI Automation 读页码的延迟，是上界）'
                }
            }
        }
        Stop-AiPdfApp -Process $process
        $process = $null
    }

    # ---------------- K6 峰值内存 ----------------
    Write-Host ''
    Write-Host '==> K6 500 页文档峰值内存' -ForegroundColor Cyan
    if (-not (Test-Path -LiteralPath $pagesDoc)) {
        Add-BudgetRow 'K6 峰值工作集' '缺少语料' "< $MaxPeakWorkingSetMiB MiB" $false '缺少 500 页语料'
    } else {
        Reset-AiPdfTrace -Path $tracePath
        $process = Start-AiPdfApp -Exe $exe -BuildPath $buildPath -Document $pagesDoc -TracePath $tracePath
        $outcome = Wait-OpenOutcome -Process $process -TracePath $tracePath -TimeoutSeconds 180
        if ($outcome.Kind -ne 'opened') {
            Add-BudgetRow 'K6 峰值工作集' '打不开文档' "< $MaxPeakWorkingSetMiB MiB" $false $outcome.Message
        } else {
            $scrollBar = Get-VerticalScrollBar -Process $process
            if ($scrollBar) {
                for ($i = 1; $i -le 12; $i++) {
                    $range = $null
                    if ($scrollBar.TryGetCurrentPattern(
                            [System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$range)) {
                        $minimum = $range.Current.Minimum
                        $maximum = $range.Current.Maximum
                        $range.SetValue([double]($minimum + ($maximum - $minimum) * ($i / 13.0)))
                    }
                    Start-Sleep -Milliseconds 400
                }
            }
            $peakMiB = 0.0
            $currentMiB = 0.0
            try {
                $process.Refresh()
                $peakMiB = [Math]::Round($process.PeakWorkingSet64 / 1MB, 1)
                $currentMiB = [Math]::Round($process.WorkingSet64 / 1MB, 1)
            } catch {
                Add-BudgetRow 'K6 峰值工作集' '读不到' "< $MaxPeakWorkingSetMiB MiB" $false $_.Exception.Message
            }
            if ($peakMiB -gt 0) {
                Add-BudgetRow 'K6 峰值工作集' ("{0} MiB" -f $peakMiB) "< $MaxPeakWorkingSetMiB MiB" `
                    ($peakMiB -lt $MaxPeakWorkingSetMiB) ("当前工作集 {0} MiB" -f $currentMiB)
            }
        }
        Stop-AiPdfApp -Process $process
        $process = $null
    }
} finally {
    $ErrorActionPreference = 'Continue'
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    try { Stop-AiPdfOrphanWorker -Quiet | Out-Null } catch { }
    Restore-AiPdfCursorPosition -Position $cursorBefore
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host '==> 预算汇总' -ForegroundColor Cyan
foreach ($row in $script:rows) {
    $mark = $(if ($row.Ok) { 'PASS' } else { 'FAIL' })
    $color = $(if ($row.Ok) { 'Green' } else { 'Red' })
    Write-Host ("    {0}  {1,-26} {2,-16} 预算 {3}" -f $mark, $row.Item, $row.Measured, $row.Budget) -ForegroundColor $color
}

if ($ReportPath) {
    $report = [pscustomobject]@{
        minFps            = $MinFps
        maxOpenMs         = $MaxOpenMs
        maxPageTurnMs     = $MaxPageTurnMs
        maxPeakWorkingMiB = $MaxPeakWorkingSetMiB
        failures          = $script:failures
        rows              = $script:rows
    }
    $reportDir = Split-Path -Parent $ReportPath
    if ($reportDir -and -not (Test-Path -LiteralPath $reportDir)) {
        New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
    }
    [System.IO.File]::WriteAllText(
        $ReportPath,
        ($report | ConvertTo-Json -Depth 5),
        (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "    报告        ：$ReportPath"
}

if ($script:failures -gt 0) {
    Write-Host "`n性能预算未达标：$($script:failures) 项超预算" -ForegroundColor Red
    exit 1
}
Write-Host "`n性能预算通过：滚动帧率、大文件打开/翻页、500 页峰值内存都在预算内" -ForegroundColor Green
exit 0
