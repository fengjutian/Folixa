<#
.SYNOPSIS
    优化保真验收：优化前后体积不增，且渲染结果一致。

.DESCRIPTION
    做的事：拿一份 PDF 跑一次「优化」，然后断言两件事——
      1. 体积不增（上限可用 -MaxSizeGrowthPercent 调）；
      2. 渲染结果一致（逐像素相同，或差异比例不超过 -MaxDiffRatio），
         并列出有差异的页码。

    优化器有两种：
      - gs   ：界面「优化压缩」的真实实现（Ghostscript pdfwrite，参数与
               src\app\DocumentConverter.cpp 里的 ConversionKind::OptimizePdf 一致）；
      - qpdf ：仓库自带的结构优化（tools\qpdf），本机没装 Ghostscript 时的等价路径。
    -Optimizer auto（默认）优先用 Ghostscript，没有就用 qpdf，并用 -ExpectOptimizer
    钉死期望值：要求 gs 而本机没有 gs 时直接失败，不静默降级。

    渲染比对用仓库固定的 PDFium（tools\pdfium\bin\pdfium.dll），经由
    scripts\gen-cjk-corpus.py 的 probe 子命令调用。整条链路都是命令行，
    不启动 GUI、不碰桌面。

    负例自检（-SelfTest）：渲染不一致 / 体积变大 / 产物等于输入 / 产物是坏文件，
    四种情况必须被判红，同时确认正常优化是绿的。

.PARAMETER Input
    要优化的输入 PDF。默认 build\pdf-fixtures\text.pdf（缺失时用仓库的
    scripts\generate-test-pdfs.py 现生成）。

.PARAMETER Before
    直接指定「优化前」的文件（负例用）。不给就等于 -Input。

.PARAMETER After
    直接指定「优化后」的文件（负例用）。给了就不跑优化器。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\verify-optimize-fidelity.ps1
    powershell -ExecutionPolicy Bypass -File scripts\verify-optimize-fidelity.ps1 -SelfTest
#>
param(
    [string]$Input = '',
    [string]$Before = '',
    [string]$After = '',
    [string]$Optimizer = 'auto',
    [string]$ExpectOptimizer = '',
    [double]$MaxSizeGrowthPercent = 0.0,
    [double]$MaxDiffRatio = 0.0,
    [int]$RenderWidth = 612,
    [int]$MaxPages = 0,
    [string]$RepositoryRoot = '',
    [string]$PythonExe = 'python',
    [string]$QpdfPath = '',
    [string]$GhostscriptPath = '',
    [switch]$AllowIdenticalOutput,
    [switch]$KeepArtifacts,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($QpdfPath)) {
    $QpdfPath = Join-Path $RepositoryRoot 'tools\qpdf\bin\qpdf.exe'
}
$probeScript = Join-Path $PSScriptRoot 'gen-cjk-corpus.py'
$pdfiumDll = Join-Path $RepositoryRoot 'tools\pdfium\bin\pdfium.dll'
$fixtureDir = Join-Path $RepositoryRoot 'build\pdf-fixtures'

# ---------------------------------------------------------------------------
# 小工具
# ---------------------------------------------------------------------------

function New-Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    return [pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail }
}

function Invoke-Native {
    # 原生程序往 stderr 写东西时，$ErrorActionPreference='Stop' 会把它当异常抛出来。
    # 这一层只关心退出码，所以临时放宽，跑完还原。
    param([string]$FilePath, [string[]]$Arguments)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $FilePath @Arguments 2>&1
        return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output | Out-String) }
    } finally {
        $ErrorActionPreference = $previous
    }
}

function Find-Ghostscript {
    # 与 src\app\DocumentConverter.cpp 的 findGhostscript 同一条查找顺序：
    # 先看 PATH，再看 %ProgramFiles%\gs\*\bin。
    if (-not [string]::IsNullOrWhiteSpace($GhostscriptPath)) {
        if (Test-Path -LiteralPath $GhostscriptPath) { return $GhostscriptPath }
        return ''
    }
    foreach ($name in @('gswin64c', 'gswin32c', 'gs')) {
        $found = Get-Command $name -ErrorAction SilentlyContinue
        if ($found) { return $found.Source }
    }
    $gsRoot = Join-Path $env:ProgramFiles 'gs'
    if (Test-Path -LiteralPath $gsRoot) {
        foreach ($version in (Get-ChildItem -LiteralPath $gsRoot -Directory | Sort-Object Name -Descending)) {
            foreach ($exe in @('gswin64c.exe', 'gswin32c.exe')) {
                $candidate = Join-Path $version.FullName (Join-Path 'bin' $exe)
                if (Test-Path -LiteralPath $candidate) { return $candidate }
            }
        }
    }
    return ''
}

function Get-DefaultInput {
    # 仓库夹具：build\pdf-fixtures\text.pdf。缺了就用仓库自己的生成器现生成。
    $candidate = Join-Path $fixtureDir 'text.pdf'
    if (Test-Path -LiteralPath $candidate) { return $candidate }
    $generator = Join-Path $PSScriptRoot 'generate-test-pdfs.py'
    if (-not (Test-Path -LiteralPath $generator)) { return '' }
    Write-Host "生成仓库夹具：$fixtureDir" -ForegroundColor Cyan
    $result = Invoke-Native -FilePath $PythonExe -Arguments @($generator, $fixtureDir, '--quiet')
    if ($result.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $candidate)) { return '' }
    return $candidate
}

function Invoke-Probe {
    # 探针结果写文件再读：子进程标准输出按控制台代码页解码，中文会乱码。
    param([string]$PathA, [string]$PathB, [string]$WorkDir)
    $jsonOut = Join-Path $WorkDir ('probe-' + [guid]::NewGuid().ToString('N') + '.json')
    $arguments = @($probeScript, 'probe', '--pdf', $PathA, '--pdf', $PathB,
                   '--pdfium', $pdfiumDll, '--render-width', "$RenderWidth",
                   '--json-out', $jsonOut)
    if ($MaxPages -gt 0) { $arguments += @('--max-pages', "$MaxPages") }
    $result = Invoke-Native -FilePath $PythonExe -Arguments $arguments
    if ($result.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $jsonOut)) { return $null }
    $text = Get-Content -LiteralPath $jsonOut -Raw -Encoding UTF8
    Remove-Item -LiteralPath $jsonOut -Force -ErrorAction SilentlyContinue
    return ($text | ConvertFrom-Json)
}

# ---------------------------------------------------------------------------
# 优化器
# ---------------------------------------------------------------------------

function Invoke-Optimizer {
    # 返回 @{ Engine='gs'|'qpdf'; Output=<路径>; Error='' }
    param([string]$Kind, [string]$SourcePath, [string]$TargetPath)

    if ($Kind -eq 'gs') {
        $program = Find-Ghostscript
        if ([string]::IsNullOrWhiteSpace($program)) {
            return @{ Engine = 'gs'; Output = ''; Error = '没有找到 Ghostscript（gswin64c / gswin32c / gs）' }
        }
        # 参数与 src\app\DocumentConverter.cpp 的 ConversionKind::OptimizePdf 保持一致。
        $arguments = @('-dBATCH', '-dNOPAUSE', '-dSAFER', '-sDEVICE=pdfwrite',
                       '-dPDFSETTINGS=/ebook', '-dDetectDuplicateImages=true',
                       '-dCompressFonts=true', "-sOutputFile=$TargetPath", $SourcePath)
        $result = Invoke-Native -FilePath $program -Arguments $arguments
        if ($result.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $TargetPath)) {
            return @{ Engine = 'gs'; Output = ''; Error = ("Ghostscript 退出码 {0}：{1}" -f $result.ExitCode, $result.Output.Trim()) }
        }
        return @{ Engine = 'gs'; Output = $TargetPath; Error = '' }
    }

    if ($Kind -eq 'qpdf') {
        if (-not (Test-Path -LiteralPath $QpdfPath)) {
            return @{ Engine = 'qpdf'; Output = ''; Error = ("没有找到 qpdf：{0}（先跑 scripts\bootstrap-qpdf.ps1）" -f $QpdfPath) }
        }
        $arguments = @('--object-streams=generate', '--compress-streams=y',
                       '--recompress-flate', $SourcePath, $TargetPath)
        $result = Invoke-Native -FilePath $QpdfPath -Arguments $arguments
        if ($result.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $TargetPath)) {
            return @{ Engine = 'qpdf'; Output = ''; Error = ("qpdf 退出码 {0}：{1}" -f $result.ExitCode, $result.Output.Trim()) }
        }
        return @{ Engine = 'qpdf'; Output = $TargetPath; Error = '' }
    }

    return @{ Engine = ''; Output = ''; Error = ("未知的优化器：{0}" -f $Kind) }
}

# ---------------------------------------------------------------------------
# 断言
# ---------------------------------------------------------------------------

function Invoke-FidelityCheck {
    param(
        [Parameter(Mandatory = $true)][string]$BeforePath,
        [Parameter(Mandatory = $true)][string]$AfterPath,
        [Parameter(Mandatory = $true)][string]$WorkDir
    )

    $checks = New-Object System.Collections.Generic.List[object]

    if (-not (Test-Path -LiteralPath $BeforePath)) {
        $checks.Add((New-Check '优化前文件存在' $false $BeforePath))
        return [pscustomobject]@{ Ok = $false; Checks = $checks }
    }
    if (-not (Test-Path -LiteralPath $AfterPath)) {
        $checks.Add((New-Check '优化后文件存在' $false $AfterPath))
        return [pscustomobject]@{ Ok = $false; Checks = $checks }
    }

    $beforeSize = (Get-Item -LiteralPath $BeforePath).Length
    $afterSize = (Get-Item -LiteralPath $AfterPath).Length

    # 1) 体积不增
    $limit = $beforeSize * (1.0 + $MaxSizeGrowthPercent / 100.0)
    $growth = 0.0
    if ($beforeSize -gt 0) { $growth = 100.0 * ($afterSize - $beforeSize) / $beforeSize }
    $sizeOk = $afterSize -le $limit
    $sizeDetail = "{0} -> {1} 字节（{2:N2}%，上限 {3:N2}%）" -f $beforeSize, $afterSize, $growth, $MaxSizeGrowthPercent
    $checks.Add((New-Check '体积不增' $sizeOk $sizeDetail))

    # 2) 产物确实变了（优化真的跑过）
    if ($AllowIdenticalOutput) {
        $checks.Add((New-Check '产物与输入不同' $true '按要求跳过（-AllowIdenticalOutput）'))
    } else {
        $beforeHash = (Get-FileHash -LiteralPath $BeforePath -Algorithm SHA256).Hash
        $afterHash = (Get-FileHash -LiteralPath $AfterPath -Algorithm SHA256).Hash
        $different = ($beforeHash -ne $afterHash)
        $hashDetail = "{0} vs {1}" -f $beforeHash.Substring(0, 12), $afterHash.Substring(0, 12)
        $checks.Add((New-Check '产物与输入不同' $different $hashDetail))
    }

    # 3) 结构有效（qpdf --check）：两个文件都要过
    if (Test-Path -LiteralPath $QpdfPath) {
        $beforeCheck = Invoke-Native -FilePath $QpdfPath -Arguments @('--check', $BeforePath)
        $afterCheck = Invoke-Native -FilePath $QpdfPath -Arguments @('--check', $AfterPath)
        $structureOk = ($beforeCheck.ExitCode -eq 0 -and $afterCheck.ExitCode -eq 0)
        $structureDetail = "优化前 qpdf --check 退出码 {0}，优化后 {1}" -f $beforeCheck.ExitCode, $afterCheck.ExitCode
        $checks.Add((New-Check '结构有效' $structureOk $structureDetail))
    } else {
        $checks.Add((New-Check '结构有效' $true ("跳过：没有 qpdf（{0}）" -f $QpdfPath)))
    }

    # 4) 渲染一致
    $probe = Invoke-Probe -PathA $BeforePath -PathB $AfterPath -WorkDir $WorkDir
    if ($null -eq $probe) {
        $checks.Add((New-Check '渲染一致' $false 'PDFium 探针没有产出结果（文件可能打不开）'))
    } else {
        $before = $probe.documents[0]
        $after = $probe.documents[1]
        if (-not $before.opened) {
            $checks.Add((New-Check '渲染一致' $false ("优化前打不开：{0}" -f $before.error)))
        } elseif (-not $after.opened) {
            $checks.Add((New-Check '渲染一致' $false ("优化后打不开：{0}" -f $after.error)))
        } else {
            $diff = $probe.diff
            $pagesBefore = [int]$before.pages
            $pagesAfter = [int]$after.pages
            if ($diff.page_count_mismatch) {
                $checks.Add((New-Check '渲染一致' $false ("页数不同：{0} vs {1}" -f $pagesBefore, $pagesAfter)))
            } else {
                $maxRatio = [double]$diff.max_diff_ratio
                $differing = @($diff.differing_pages)
                $renderOk = ($maxRatio -le $MaxDiffRatio)
                $detail = "最大差异比例 {0:P4}（上限 {1:P4}）" -f $maxRatio, $MaxDiffRatio
                if ($differing.Count -gt 0) {
                    $detail += "；有差异的页：" + (($differing | ForEach-Object { "$_ 页" }) -join '、')
                } else {
                    $detail += "；逐像素完全一致"
                }
                $checks.Add((New-Check '渲染一致' $renderOk $detail))
            }
        }
    }

    $ok = $true
    foreach ($check in $checks) { if (-not $check.Ok) { $ok = $false } }
    return [pscustomobject]@{ Ok = $ok; Checks = $checks }
}

function Write-CheckLines {
    param($Checks)
    foreach ($check in $Checks) {
        if ($check.Ok) {
            Write-Host ("  PASS  {0}：{1}" -f $check.Name, $check.Detail) -ForegroundColor Green
        } else {
            Write-Host ("  FAIL  {0}：{1}" -f $check.Name, $check.Detail) -ForegroundColor Red
        }
    }
}

# ---------------------------------------------------------------------------
# 负例自检
# ---------------------------------------------------------------------------

function Invoke-SelfTest {
    $workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-opt-selftest-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $workDir | Out-Null
    $failures = 0
    try {
        Write-Host '==> 自检：四种坏情况必须判红，正常优化必须判绿' -ForegroundColor Cyan

        $source = Get-DefaultInput
        if ([string]::IsNullOrWhiteSpace($source)) {
            Write-Host '拿不到仓库夹具（build\pdf-fixtures\text.pdf）' -ForegroundColor Red
            return 2
        }
        $fixtureDirPath = Split-Path -Parent $source

        # 对照组：真的跑一次优化，必须全绿。
        $target = Join-Path $workDir 'optimized.pdf'
        $kind = $Optimizer
        if ($kind -eq 'auto') {
            $kind = if (-not [string]::IsNullOrWhiteSpace((Find-Ghostscript))) { 'gs' } else { 'qpdf' }
        }
        $optimized = Invoke-Optimizer -Kind $kind -SourcePath $source -TargetPath $target
        if ([string]::IsNullOrWhiteSpace($optimized.Output)) {
            Write-Host ("对照组跑不起来（{0}）：{1}" -f $kind, $optimized.Error) -ForegroundColor Red
            return 2
        }
        $control = Invoke-FidelityCheck -BeforePath $source -AfterPath $optimized.Output -WorkDir $workDir
        if ($control.Ok) {
            Write-Host ("  PASS  对照组：{0} 优化后体积不增且渲染一致" -f $kind) -ForegroundColor Green
        } else {
            Write-Host '  FAIL  对照组：正常优化被判红' -ForegroundColor Red
            foreach ($check in $control.Checks) {
                if (-not $check.Ok) { Write-Host ("      {0}：{1}" -f $check.Name, $check.Detail) -ForegroundColor Red }
            }
            $failures++
        }

        # 负例 1：渲染不一致（换成内容完全不同但页数相同的文档）
        $other = Join-Path $fixtureDirPath 'cjk.pdf'
        if (Test-Path -LiteralPath $other) {
            $result = Invoke-FidelityCheck -BeforePath $source -AfterPath $other -WorkDir $workDir
            $renderFailed = $false
            foreach ($check in $result.Checks) {
                if (-not $check.Ok -and $check.Name -eq '渲染一致') { $renderFailed = $true }
            }
            if ($renderFailed) {
                Write-Host '  PASS  负例 1：渲染不一致被判红' -ForegroundColor Green
            } else {
                Write-Host '  FAIL  负例 1：渲染不一致没有被判红' -ForegroundColor Red
                $failures++
            }
        } else {
            Write-Host '  SKIP  负例 1：缺少 cjk.pdf' -ForegroundColor Yellow
        }

        # 负例 2：体积变大
        $small = Join-Path $fixtureDirPath 'empty.pdf'
        if (Test-Path -LiteralPath $small) {
            $result = Invoke-FidelityCheck -BeforePath $small -AfterPath $source -WorkDir $workDir
            $sizeFailed = $false
            foreach ($check in $result.Checks) {
                if (-not $check.Ok -and $check.Name -eq '体积不增') { $sizeFailed = $true }
            }
            if ($sizeFailed) {
                Write-Host '  PASS  负例 2：体积变大被判红' -ForegroundColor Green
            } else {
                Write-Host '  FAIL  负例 2：体积变大没有被判红' -ForegroundColor Red
                $failures++
            }
        } else {
            Write-Host '  SKIP  负例 2：缺少 empty.pdf' -ForegroundColor Yellow
        }

        # 负例 3：产物就是输入本身（优化其实没跑）
        $result = Invoke-FidelityCheck -BeforePath $source -AfterPath $source -WorkDir $workDir
        $sameFailed = $false
        foreach ($check in $result.Checks) {
            if (-not $check.Ok -and $check.Name -eq '产物与输入不同') { $sameFailed = $true }
        }
        if ($sameFailed) {
            Write-Host '  PASS  负例 3：产物等于输入被判红' -ForegroundColor Green
        } else {
            Write-Host '  FAIL  负例 3：产物等于输入没有被判红' -ForegroundColor Red
            $failures++
        }

        # 负例 4：产物是坏文件
        $broken = Join-Path $fixtureDirPath 'corrupt.pdf'
        if (Test-Path -LiteralPath $broken) {
            $result = Invoke-FidelityCheck -BeforePath $source -AfterPath $broken -WorkDir $workDir
            if (-not $result.Ok) {
                Write-Host '  PASS  负例 4：坏产物被判红' -ForegroundColor Green
            } else {
                Write-Host '  FAIL  负例 4：坏产物没有被判红' -ForegroundColor Red
                $failures++
            }
        } else {
            Write-Host '  SKIP  负例 4：缺少 corrupt.pdf' -ForegroundColor Yellow
        }
    } finally {
        Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
    }

    if ($failures -gt 0) {
        Write-Host "`n自检失败：$failures 项" -ForegroundColor Red
        return 1
    }
    Write-Host "`n自检通过：渲染不一致 / 体积变大 / 产物等于输入 / 坏产物，四种情况都被判红" -ForegroundColor Green
    return 0
}

# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

if ($SelfTest) {
    if (-not (Test-Path -LiteralPath $probeScript)) {
        Write-Host "缺少探针脚本：$probeScript" -ForegroundColor Red
        exit 2
    }
    exit (Invoke-SelfTest)
}

if (-not (Test-Path -LiteralPath $probeScript)) {
    Write-Host "缺少探针脚本：$probeScript" -ForegroundColor Red
    exit 2
}
if (-not (Test-Path -LiteralPath $pdfiumDll)) {
    Write-Host "缺少 PDFium：$pdfiumDll" -ForegroundColor Red
    Write-Host '先跑：scripts\bootstrap-pdfium.ps1' -ForegroundColor Yellow
    exit 2
}

$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-opt-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$exitCode = 0
try {
    Write-Host '==> 优化保真验收' -ForegroundColor Cyan

    $beforePath = $Before
    $afterPath = $After
    $engineUsed = ''

    if ([string]::IsNullOrWhiteSpace($beforePath)) {
        $beforePath = Get-DefaultInput
        if ([string]::IsNullOrWhiteSpace($beforePath)) {
            Write-Host '拿不到输入 PDF（build\pdf-fixtures\text.pdf）' -ForegroundColor Red
            exit 2
        }
    }
    if (-not [System.IO.Path]::IsPathRooted($beforePath)) {
        $beforePath = Join-Path $RepositoryRoot $beforePath
    }

    if ([string]::IsNullOrWhiteSpace($afterPath)) {
        $kind = $Optimizer
        if ($kind -eq 'auto') {
            $kind = if (-not [string]::IsNullOrWhiteSpace((Find-Ghostscript))) { 'gs' } else { 'qpdf' }
        }
        if ($kind -eq 'none') {
            Write-Host '给了 -Optimizer none 却没有 -After：没有产物可比' -ForegroundColor Red
            exit 2
        }
        $target = Join-Path $workDir 'optimized.pdf'
        $optimized = Invoke-Optimizer -Kind $kind -SourcePath $beforePath -TargetPath $target
        if ([string]::IsNullOrWhiteSpace($optimized.Output)) {
            Write-Host ("优化失败（{0}）：{1}" -f $kind, $optimized.Error) -ForegroundColor Red
            exit 1
        }
        $afterPath = $optimized.Output
        $engineUsed = $optimized.Engine
        Write-Host ("    优化器    ：{0}" -f $engineUsed)
        if ($engineUsed -eq 'qpdf') {
            Write-Host '    注意      ：本机没有 Ghostscript，跑的是 qpdf 结构优化；' -ForegroundColor Yellow
            Write-Host '                界面「优化压缩」的 Ghostscript 路径需要装了 gs 才能覆盖。' -ForegroundColor Yellow
        }
    } elseif (-not [System.IO.Path]::IsPathRooted($afterPath)) {
        $afterPath = Join-Path $RepositoryRoot $afterPath
    }

    if (-not [string]::IsNullOrWhiteSpace($ExpectOptimizer)) {
        if ($engineUsed -ne $ExpectOptimizer) {
            Write-Host ("优化器不符：期望 {0}，实际 '{1}'" -f $ExpectOptimizer, $engineUsed) -ForegroundColor Red
            exit 1
        }
    }

    Write-Host ("    优化前    ：{0}（{1} 字节）" -f $beforePath, (Get-Item -LiteralPath $beforePath).Length)
    Write-Host ("    优化后    ：{0}（{1} 字节）" -f $afterPath, (Get-Item -LiteralPath $afterPath).Length)
    Write-Host ("    判定      ：体积上限 {0:N2}%，渲染差异上限 {1:P4}" -f $MaxSizeGrowthPercent, $MaxDiffRatio)
    Write-Host ''

    $result = Invoke-FidelityCheck -BeforePath $beforePath -AfterPath $afterPath -WorkDir $workDir
    Write-CheckLines -Checks $result.Checks

    if (-not $result.Ok) {
        Write-Host "`n优化保真验收失败：上面标红的断言没过" -ForegroundColor Red
        $exitCode = 1
    } else {
        Write-Host "`n优化保真验收通过：体积不增，渲染一致" -ForegroundColor Green
        $exitCode = 0
    }
} catch {
    Write-Host ("优化保真验收异常：{0}" -f $_.Exception.Message) -ForegroundColor Red
    $exitCode = 1
} finally {
    if ($KeepArtifacts) {
        Write-Host ("产物保留在：{0}" -f $workDir) -ForegroundColor DarkGray
    } else {
        Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

exit $exitCode
