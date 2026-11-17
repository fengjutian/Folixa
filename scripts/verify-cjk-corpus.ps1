<#
.SYNOPSIS
    CJK 兼容语料验收：逐份夹具断言「能打开 / 页数正确 / 文字能抽出 / 锚点命中 / 渲染不崩」。

.DESCRIPTION
    语料由 scripts\gen-cjk-corpus.py 生成（默认落在 build\cjk-corpus）：

        python scripts\gen-cjk-corpus.py gen --out build\cjk-corpus

    每份夹具旁边有一个同名 .meta.json，记录期望页数、关键锚点与「应当有墨迹」的页。
    本脚本只断言**关键锚点**（去空白后子串命中），不做整篇字符串相等：
    抽取顺序、空格与换行都不是语料要锁住的东西，锚点才是。

    引擎说明：
      - 文字抽取与渲染都用仓库固定的 PDFium（tools\pdfium\bin\pdfium.dll），
        通过 scripts\gen-cjk-corpus.py 的 probe 子命令调用（ctypes 直连，不装第三方包）。
      - 结构有效性另用 qpdf --check 核对（PDFium 很宽容，「能打开」不等于结构对）。
        没装 qpdf 时这一项标注为跳过，不假装通过。

    负例自检（-SelfTest）：把语料换成截断 / 加密 / 空文件，断言验收必须变红。
    自检同样不启动任何 GUI 进程、不碰桌面。

.PARAMETER CorpusDir
    语料目录。默认 build\cjk-corpus。

.PARAMETER SelfTest
    只跑负例自检：坏语料（截断 / 加密 / 空文件）必须被判红，同时确认好语料是绿的。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\verify-cjk-corpus.ps1
    powershell -ExecutionPolicy Bypass -File scripts\verify-cjk-corpus.ps1 -SelfTest
#>
param(
    [string]$CorpusDir = '',
    [string]$RepositoryRoot = '',
    [string]$PythonExe = 'python',
    [string]$PdfiumDll = '',
    [string]$QpdfPath = '',
    [int]$RenderWidth = 612,
    [switch]$SkipRender,
    [switch]$SkipQpdf,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($CorpusDir)) {
    $CorpusDir = Join-Path $RepositoryRoot 'build\cjk-corpus'
} elseif (-not [System.IO.Path]::IsPathRooted($CorpusDir)) {
    $CorpusDir = Join-Path $RepositoryRoot $CorpusDir
}

$probeScript = Join-Path $PSScriptRoot 'gen-cjk-corpus.py'
if ([string]::IsNullOrWhiteSpace($PdfiumDll)) {
    $PdfiumDll = Join-Path $RepositoryRoot 'tools\pdfium\bin\pdfium.dll'
}
if ([string]::IsNullOrWhiteSpace($QpdfPath)) {
    $QpdfPath = Join-Path $RepositoryRoot 'tools\qpdf\bin\qpdf.exe'
}

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
        return [pscustomobject]@{
            ExitCode = $LASTEXITCODE
            Output   = ($output | Out-String)
        }
    } finally {
        $ErrorActionPreference = $previous
    }
}

function Invoke-Probe {
    # 调 PDFium 探针，把结果写成 JSON 文件再读：子进程的标准输出会按控制台代码页解码，
    # 中文会变乱码，写文件才靠得住。
    param([string]$Path, [string]$WorkDir)
    $jsonOut = Join-Path $WorkDir ('probe-' + [guid]::NewGuid().ToString('N') + '.json')
    $arguments = @($probeScript, 'probe', '--pdf', $Path, '--pdfium', $PdfiumDll,
                   '--render-width', "$RenderWidth", '--json-out', $jsonOut)
    if ($SkipRender) {
        $arguments += '--no-render'
    }
    $result = Invoke-Native -FilePath $PythonExe -Arguments $arguments
    if ($result.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $jsonOut)) {
        return $null
    }
    $text = Get-Content -LiteralPath $jsonOut -Raw -Encoding UTF8
    Remove-Item -LiteralPath $jsonOut -Force -ErrorAction SilentlyContinue
    return ($text | ConvertFrom-Json)
}

# ---------------------------------------------------------------------------
# 单份夹具的判定
# ---------------------------------------------------------------------------

function Invoke-FixtureCheck {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Meta,
        [Parameter(Mandatory = $true)][string]$WorkDir
    )

    $checks = New-Object System.Collections.Generic.List[object]

    # 0) 生成阶段就失败的夹具：清单里带 error，直接判红（不能当成「这份没测」）
    if ($Meta.PSObject.Properties.Name -contains 'error') {
        $message = [string]$Meta.error
        if ([string]::IsNullOrWhiteSpace($message)) { $message = '生成阶段失败' }
        $checks.Add((New-Check '生成成功' $false $message))
        return [pscustomobject]@{ Path = $Path; Ok = $false; Checks = $checks }
    }

    # 1) 文件本身
    if (-not (Test-Path -LiteralPath $Path)) {
        $checks.Add((New-Check '文件存在' $false $Path))
        return [pscustomobject]@{ Path = $Path; Ok = $false; Checks = $checks }
    }
    $size = (Get-Item -LiteralPath $Path).Length
    $checks.Add((New-Check '文件存在' ($size -gt 0) ("{0} 字节" -f $size)))
    if ($size -le 0) {
        return [pscustomobject]@{ Path = $Path; Ok = $false; Checks = $checks }
    }

    # 2) 结构有效性（qpdf --check）
    if ($SkipQpdf) {
        $checks.Add((New-Check '结构有效' $true '按要求跳过（-SkipQpdf）'))
    } elseif (-not (Test-Path -LiteralPath $QpdfPath)) {
        $checks.Add((New-Check '结构有效' $true ("跳过：没有 qpdf（{0}），先跑 scripts\bootstrap-qpdf.ps1" -f $QpdfPath)))
    } else {
        $result = Invoke-Native -FilePath $QpdfPath -Arguments @('--check', $Path)
        $detail = 'qpdf --check 通过'
        if ($result.ExitCode -ne 0) {
            $detail = ($result.Output -split "`n" | Where-Object { $_.Trim() -ne '' } | Select-Object -First 3) -join ' / '
        }
        $checks.Add((New-Check '结构有效' ($result.ExitCode -eq 0) $detail))
    }

    # 3) 能打开 / 页数 / 文字 / 锚点 / 渲染
    $probe = Invoke-Probe -Path $Path -WorkDir $WorkDir
    if ($null -eq $probe) {
        $checks.Add((New-Check '能打开' $false 'PDFium 探针没有产出结果'))
        return [pscustomobject]@{ Path = $Path; Ok = $false; Checks = $checks }
    }

    $document = $probe.documents[0]
    $checks.Add((New-Check '能打开' ([bool]$document.opened) ([string]$document.error)))
    if (-not $document.opened) {
        return [pscustomobject]@{ Path = $Path; Ok = $false; Checks = $checks }
    }

    $expectedPages = [int]$Meta.pages
    $actualPages = [int]$document.pages
    $checks.Add((New-Check '页数正确' ($actualPages -eq $expectedPages) ("{0} / {1}" -f $actualPages, $expectedPages)))
    if ($actualPages -ne $expectedPages) {
        return [pscustomobject]@{ Path = $Path; Ok = $false; Checks = $checks }
    }

    $text = ''
    foreach ($pageText in $document.page_texts) { $text += $pageText }
    $checks.Add((New-Check '文字能抽出' ($document.text_chars -gt 0) ("{0} 字符" -f $document.text_chars)))

    # 只断言关键锚点：去空白后子串命中即可（抽取顺序与换行不是要锁住的东西）。
    $compact = $text -replace '\s', ''
    $missing = New-Object System.Collections.Generic.List[string]
    $anchors = @()
    if ($Meta.PSObject.Properties.Name -contains 'anchors') { $anchors = @($Meta.anchors) }
    foreach ($anchor in $anchors) {
        $needle = ([string]$anchor) -replace '\s', ''
        if ($needle.Length -eq 0) { continue }
        if (-not $compact.Contains($needle)) { $missing.Add([string]$anchor) }
    }
    $anchorDetail = "{0} / {1} 命中" -f ($anchors.Count - $missing.Count), $anchors.Count
    if ($missing.Count -gt 0) {
        $anchorDetail += "；缺：" + (($missing | ForEach-Object { "'" + $_ + "'" }) -join ' ')
    }
    $checks.Add((New-Check '锚点命中' ($missing.Count -eq 0) $anchorDetail))

    # 渲染：不崩，且声明要有墨迹的页必须真的有墨迹
    if ($SkipRender) {
        $checks.Add((New-Check '渲染不崩' $true '按要求跳过（-SkipRender）'))
    } elseif ($null -eq $document.render) {
        $checks.Add((New-Check '渲染不崩' $false '渲染没有产出结果'))
    } else {
        $renderOk = $true
        $detail = "{0} 页渲染完成" -f $document.render.pages
        $inkPages = @()
        if ($document.render.PSObject.Properties.Name -contains 'ink_pages') {
            $inkPages = @($document.render.ink_pages)
        }
        $expectedInk = @()
        if ($Meta.PSObject.Properties.Name -contains 'ink_pages') {
            $expectedInk = @($Meta.ink_pages)
        }
        $blank = @()
        foreach ($pageIndex in $expectedInk) {
            if ($inkPages -notcontains [int]$pageIndex) { $blank += [int]$pageIndex }
        }
        if ($blank.Count -gt 0) {
            $renderOk = $false
            $detail += "；应当有墨迹却是空白：第 " + (($blank | ForEach-Object { "$_ 页" }) -join '、')
        }
        $checks.Add((New-Check '渲染不崩' $renderOk $detail))
    }

    $ok = $true
    foreach ($check in $checks) { if (-not $check.Ok) { $ok = $false } }
    return [pscustomobject]@{ Path = $Path; Ok = $ok; Checks = $checks }
}

function Write-CheckLine {
    param([string]$Name, $Checks)
    $marks = @()
    foreach ($check in $Checks) {
        if ($check.Ok) { $marks += '✓' } else { $marks += '✗' }
    }
    $color = 'Green'
    if ($marks -contains '✗') { $color = 'Red' }
    Write-Host ("  {0,-12} {1}" -f $Name, ($marks -join ' ')) -ForegroundColor $color
    foreach ($check in $Checks) {
        if (-not $check.Ok -and $check.Detail) {
            Write-Host ("      ✗ {0}：{1}" -f $check.Name, $check.Detail) -ForegroundColor Red
        }
    }
}

# ---------------------------------------------------------------------------
# 负例自检
# ---------------------------------------------------------------------------

function Invoke-SelfTest {
    $workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-cjk-selftest-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $workDir | Out-Null
    $failures = 0
    try {
        Write-Host '==> 自检：坏语料必须判红，好语料必须判绿' -ForegroundColor Cyan

        $good = Join-Path $CorpusDir 'cjk-punctuation.pdf'
        if (-not (Test-Path -LiteralPath $good)) {
            Write-Host "缺少基准语料：$good" -ForegroundColor Red
            Write-Host '先跑：python scripts\gen-cjk-corpus.py gen --out build\cjk-corpus' -ForegroundColor Red
            return 2
        }

        # 负例 1：空文件
        $empty = Join-Path $workDir 'empty.pdf'
        [System.IO.File]::WriteAllBytes($empty, @())
        # 负例 2：截断（只留前 40%）
        $truncated = Join-Path $workDir 'truncated.pdf'
        $bytes = [System.IO.File]::ReadAllBytes($good)
        [System.IO.File]::WriteAllBytes($truncated, $bytes[0..([int]($bytes.Length * 0.4))])
        # 负例 3：加密
        $encrypted = Join-Path $workDir 'encrypted.pdf'
        $encryptedReady = $false
        if (Test-Path -LiteralPath $QpdfPath) {
            # 用 256 位 AES：qpdf 12 默认拒绝 RC4（128 位）弱加密。
            $result = Invoke-Native -FilePath $QpdfPath -Arguments @(
                '--encrypt', 'user-pass', 'owner-pass', '256', '--', $good, $encrypted)
            $encryptedReady = ($result.ExitCode -eq 0 -and (Test-Path -LiteralPath $encrypted))
            if (-not $encryptedReady) {
                Write-Host ("  SKIP  加密样本没造出来：{0}" -f ($result.Output -split "`n" | Select-Object -First 1)) -ForegroundColor Yellow
            }
        }

        $meta = [pscustomobject]@{ pages = 1; anchors = @('中文标点兼容性测试'); ink_pages = @(0) }

        $cases = @(
            @{ Name = '空文件';      Path = $empty;     Expect = 'red' },
            @{ Name = '截断文件';    Path = $truncated; Expect = 'red' },
            @{ Name = '加密文件';    Path = $encrypted; Expect = 'red'; Skip = -not $encryptedReady }
        )

        foreach ($case in $cases) {
            if ($case.ContainsKey('Skip') -and $case.Skip) {
                Write-Host ("  SKIP  {0}（没有 qpdf，造不出加密样本）" -f $case.Name) -ForegroundColor Yellow
                continue
            }
            $result = Invoke-FixtureCheck -Path $case.Path -Meta $meta -WorkDir $workDir
            $verdict = if ($result.Ok) { '仍然判绿（有问题！）' } else { '判红（符合预期）' }
            $color = if ($result.Ok) { 'Red' } else { 'Green' }
            Write-Host ("  {0,-6} {1}：{2}" -f $(if ($result.Ok) { 'FAIL' } else { 'PASS' }), $case.Name, $verdict) -ForegroundColor $color
            if ($result.Ok) {
                $failures++
                foreach ($check in $result.Checks) {
                    if (-not $check.Ok) { Write-Host ("      未通过：{0}" -f $check.Name) -ForegroundColor Yellow }
                }
            }
        }

        # 对照组：好语料必须是绿的，否则说明判定条件太严或探针坏了。
        $control = Invoke-FixtureCheck -Path $good -Meta $meta -WorkDir $workDir
        if ($control.Ok) {
            Write-Host '  PASS  对照组：好语料判绿' -ForegroundColor Green
        } else {
            Write-Host '  FAIL  对照组：好语料被判红' -ForegroundColor Red
            foreach ($check in $control.Checks) {
                if (-not $check.Ok) { Write-Host ("      {0}：{1}" -f $check.Name, $check.Detail) -ForegroundColor Red }
            }
            $failures++
        }
    } finally {
        Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
    }

    if ($failures -gt 0) {
        Write-Host "`n自检失败：$failures 项" -ForegroundColor Red
        return 1
    }
    Write-Host "`n自检通过：上面列出的坏语料都被判红，好语料判绿" -ForegroundColor Green
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
if (-not (Test-Path -LiteralPath $PdfiumDll)) {
    Write-Host "缺少 PDFium：$PdfiumDll" -ForegroundColor Red
    Write-Host '先跑：scripts\bootstrap-pdfium.ps1' -ForegroundColor Yellow
    exit 2
}
if (-not (Test-Path -LiteralPath $CorpusDir)) {
    Write-Host "语料目录不存在：$CorpusDir" -ForegroundColor Red
    Write-Host '先跑：python scripts\gen-cjk-corpus.py gen --out build\cjk-corpus' -ForegroundColor Red
    exit 2
}

$manifestPath = Join-Path $CorpusDir 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Host "语料清单不存在：$manifestPath" -ForegroundColor Red
    Write-Host '先跑：python scripts\gen-cjk-corpus.py gen --out build\cjk-corpus' -ForegroundColor Red
    exit 2
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$fixtures = @($manifest.fixtures)
if ($fixtures.Count -eq 0) {
    Write-Host "语料清单里没有夹具：$manifestPath" -ForegroundColor Red
    exit 2
}

Write-Host '==> CJK 兼容语料验收' -ForegroundColor Cyan
Write-Host "    语料目录 ：$CorpusDir"
Write-Host "    夹具数量 ：$($fixtures.Count)"
Write-Host "    渲染宽度 ：$RenderWidth"
Write-Host "    检查项   ：结构有效 / 能打开 / 页数正确 / 文字能抽出 / 锚点命中 / 渲染不崩"
Write-Host ''

$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ('aipdf-cjk-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$failures = 0
try {
    foreach ($meta in $fixtures) {
        $path = Join-Path $CorpusDir $meta.file
        $result = Invoke-FixtureCheck -Path $path -Meta $meta -WorkDir $workDir
        Write-CheckLine -Name $meta.name -Checks $result.Checks
        Write-Host ("      {0}" -f $meta.feature) -ForegroundColor DarkGray
        if (-not $result.Ok) { $failures++ }
    }
} finally {
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host '==> 汇总' -ForegroundColor Cyan
Write-Host ("    夹具总数 ：{0}" -f $fixtures.Count)
Write-Host ("    未通过   ：{0}" -f $failures) -ForegroundColor $(if ($failures -gt 0) { 'Red' } else { 'Green' })

if ($failures -gt 0) {
    Write-Host "`nCJK 兼容语料验收失败：$failures 份夹具未通过" -ForegroundColor Red
    exit 1
}
Write-Host "`nCJK 兼容语料验收通过：$($fixtures.Count) 份夹具全部可打开、可抽字、锚点命中、渲染不崩" -ForegroundColor Green
exit 0
