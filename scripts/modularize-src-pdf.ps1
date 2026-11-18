<#
.SYNOPSIS
    把 src/pdf 下 130 个平铺文件按功能模块搬进子目录，并同步重写全仓库的引用路径。

.DESCRIPTION
    划分方案见 docs/SOURCE_MODULES.md。脚本做两件事：

      1. 移动：src/pdf/<Name>.{h,cpp} -> src/pdf/<module>/<Name>.{h,cpp}
         在 git 仓库里用 `git mv`，保留文件历史。
      2. 重写：所有 .cpp/.h/.cmake/CMakeLists.txt/.ps1/.py 里出现的
         "pdf/<Name>.h" 与 "pdf/<Name>.cpp" 改成 "pdf/<module>/<Name>.h"。
         因为 include 一律写成 "pdf/Xxx.h"（相对 src/），tests 里写的是
         ${CMAKE_SOURCE_DIR}/src/pdf/Xxx.cpp，同一处替换覆盖两种写法。
         文件按字节之外的 UTF-8（含 BOM）与换行风格原样保留。

    默认 dry-run：只统计将要移动/改写的量，不动文件。加 -Apply 才执行。

.PARAMETER Root
    仓库根目录，默认是脚本所在目录的上一级。

.PARAMETER Apply
    真正执行移动与重写。

.EXAMPLE
    pwsh -File scripts/modularize-src-pdf.ps1
    pwsh -File scripts/modularize-src-pdf.ps1 -Apply
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 模块 -> 文件基名（不含扩展名）。基名必须覆盖 src/pdf 下的全部文件，
# 脚本会先做完整性校验，缺一个就不动手。
$Modules = [ordered]@{
    'kernel'     = 'PdfiumAdapter TextPage StandardFonts'
    'document'   = 'DocumentOpen DocumentWriter IncrementalWriter QpdfBackend PdfRevisionCounter DocumentProperties Navigation NamedDestination'
    'content'    = 'ContentStream TextReflow TableStructure TableIntegration ParagraphFormat TrueTypeSubset InvisibleTextLayer PageDecorations'
    'annot'      = 'Annotation AnnotationDictionary AnnotationFilter AnnotationShapes AnnotationSummary AnnotationToolkit AppearanceStream Xfdf SoundFormat PageAnnotsInjector RichMedia3d'
    'forms'      = 'FormData FormFieldWriter FormJsEvaluator FormScriptStore FormTemplateStore FormToolSettings FormDesignParser FormAutoTag BarcodeEncoder XfaSupport'
    'security'   = 'PdfSecurity SignatureVerifier SignatureInspector SignatureWorkflow RsaVerifier Redaction'
    'compliance' = 'PdfAChecker PdfUaChecker PreflightChecker TagTreeWriter StructTreeReader'
    'ops'        = 'DocumentMerger DocumentComparer DocumentReflow PageSplitPlanner PageLabels OptionalContent BatchReplace SpellChecker OcrSuspicion OutlineEditor OutlineGenerator OutlineWriter'
    'convert'    = 'ExportFormats ImageToPdf PortfolioWriter'
}

# 基名 -> 模块，同时检查重复。
$Owner = @{}
foreach ($module in $Modules.Keys) {
    foreach ($name in ($Modules[$module] -split '\s+' | Where-Object { $_ })) {
        if ($Owner.ContainsKey($name)) {
            throw "文件 $name 被同时分配给了 $($Owner[$name]) 和 $module"
        }
        $Owner[$name] = $module
    }
}

$pdfDir = Join-Path $Root 'src/pdf'
if (-not (Test-Path $pdfDir)) { throw "找不到 $pdfDir" }

# --- 1. 校验覆盖完整性 -------------------------------------------------------
$onDisk = Get-ChildItem $pdfDir -File | ForEach-Object { $_.BaseName } | Sort-Object -Unique
$missing = $onDisk | Where-Object { -not $Owner.ContainsKey($_) }
$extra   = $Owner.Keys | Where-Object { $onDisk -notcontains $_ } | Sort-Object
if ($missing) { throw "这些文件没有分配模块：$($missing -join ', ')" }
if ($extra)   { throw "划分表里有 src/pdf 下不存在的文件：$($extra -join ', ')" }

$total = (Get-ChildItem $pdfDir -File).Count
Write-Host "覆盖校验通过：$total 个文件 / $($Owner.Count) 个基名 / $($Modules.Count) 个模块"
foreach ($module in $Modules.Keys) {
    $n = (Get-ChildItem $pdfDir -File | Where-Object { $Owner[$_.BaseName] -eq $module }).Count
    Write-Host ("  {0,-11} {1,3} 个文件" -f $module, $n)
}

# --- 2. 收集要重写引用的文件 -------------------------------------------------
$exts = @('.cpp', '.h', '.hpp', '.cc', '.mm', '.cmake', '.txt', '.ps1', '.py', '.ts')
$scanDirs = @('src', 'tests', 'worker', 'scripts', 'cmake', '.github') |
    ForEach-Object { Join-Path $Root $_ } |
    Where-Object { Test-Path $_ }
$targets = foreach ($dir in $scanDirs) {
    Get-ChildItem $dir -Recurse -File | Where-Object { $exts -contains $_.Extension }
}

$tokens = @()
foreach ($name in $Owner.Keys) {
    foreach ($ext in @('.h', '.cpp')) {
        $tokens += [pscustomobject]@{
            Old = "pdf/$name$ext"
            New = "pdf/$($Owner[$name])/$name$ext"
        }
    }
}

# --- 3. dry-run 统计 ---------------------------------------------------------
$rewrites = @{}   # path -> 命中次数
foreach ($file in $targets) {
    $text = [IO.File]::ReadAllText($file.FullName)
    $hits = 0
    foreach ($t in $tokens) {
        if ($text.Contains($t.Old)) { $hits += ([regex]::Matches($text, [regex]::Escape($t.Old))).Count }
    }
    if ($hits -gt 0) { $rewrites[$file.FullName] = $hits }
}

$totalHits = ($rewrites.Values | Measure-Object -Sum).Sum
Write-Host ("待移动文件：{0}；待重写文件：{1}；待替换引用：{2} 处" -f $total, $rewrites.Count, $totalHits)

if (-not $Apply) {
    Write-Host ''
    Write-Host '（dry-run，未改动任何文件。确认无误后加 -Apply 执行。）'
    return
}

# --- 4. 重写引用（保留 BOM 与换行风格） --------------------------------------
# 先改内容再搬文件：$rewrites 里的键是移动前的路径。
$changedFiles = 0
foreach ($path in $rewrites.Keys) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $text = [IO.File]::ReadAllText($path)
    $new = $text
    foreach ($t in $tokens) { $new = $new.Replace($t.Old, $t.New) }
    if ($new -ne $text) {
        [IO.File]::WriteAllText($path, $new, [Text.UTF8Encoding]::new($hasBom))
        $changedFiles++
    }
}
Write-Host "已重写 $changedFiles 个文件"

# --- 5. 移动文件 -------------------------------------------------------------
$inGit = Test-Path (Join-Path $Root '.git')
foreach ($module in $Modules.Keys) {
    $destDir = Join-Path $pdfDir $module
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
}
$moved = 0
foreach ($file in Get-ChildItem $pdfDir -File) {
    $module = $Owner[$file.BaseName]
    $dest = Join-Path (Join-Path $pdfDir $module) $file.Name
    if ($inGit) {
        git -C $Root mv -- $file.FullName $dest 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "git mv 失败：$($file.Name)" }
    } else {
        Move-Item -LiteralPath $file.FullName -Destination $dest
    }
    $moved++
}
Write-Host "已移动 $moved 个文件"

# --- 6. 自检：每个 pdf/ 引用都指向真实存在的文件 -----------------------------
$targets = foreach ($dir in $scanDirs) {
    Get-ChildItem $dir -Recurse -File | Where-Object { $exts -contains $_.Extension }
}
$broken = @()
foreach ($file in $targets) {
    if (-not (Test-Path $file.FullName)) { continue }
    foreach ($m in [regex]::Matches([IO.File]::ReadAllText($file.FullName), 'pdf/[A-Za-z0-9_]+/[A-Za-z0-9_]+\.(h|cpp)')) {
        $rel = $m.Value
        if (-not (Test-Path (Join-Path (Join-Path $Root 'src') $rel))) {
            $broken += "$rel  <- $($file.FullName)"
        }
    }
}
if ($broken) {
    Write-Warning "有引用指向不存在的文件："
    $broken | Select-Object -Unique | ForEach-Object { Write-Warning "  $_" }
    exit 1
}
Write-Host '自检通过：所有 pdf/<module>/<file> 引用都能解析到实际文件'
