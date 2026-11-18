<#
.SYNOPSIS
    把 src/<area> 下平铺的源文件按功能模块搬进子目录，并同步重写全仓库的引用路径。

.DESCRIPTION
    一个 area 一张划分表（PdfMap / AiMap），基名必须覆盖该目录下的全部文件，
    脚本先做完整性校验，缺一个或重一个都不动手。

    每个 area 做两件事：

      1. 移动：src/<area>/<Name>.{h,cpp} -> src/<area>/<module>/<Name>.{h,cpp}
         在 git 仓库里用 `git mv`，保留文件历史。
      2. 重写：.cpp/.h/.cmake/CMakeLists.txt/.ps1/.py/.ts 里出现的
         "<area>/<Name>.h" 与 "<area>/<Name>.cpp" 改成 "<area>/<module>/<Name>.h"。
         include 一律写成相对 src/ 的 "pdf/Xxx.h"，tests 里写的是
         ${CMAKE_SOURCE_DIR}/src/pdf/Xxx.cpp，同一处替换覆盖两种写法。
         文件按 UTF-8（含 BOM）与换行风格原样保留。

    默认 dry-run：只统计将要移动/改写的量，不动文件。加 -Apply 才执行。
    执行完会自检：每个 <area>/<module>/<file> 引用都能解析到真实文件，
    且没有漏改的 "<area>/<file>" 引用。

.PARAMETER Area
    要处理的目录，pdf 或 ai。默认 pdf。

.PARAMETER Root
    仓库根目录，默认是脚本所在目录的上一级。

.PARAMETER Apply
    真正执行移动与重写。

.EXAMPLE
    pwsh -File scripts/modularize-src.ps1 -Area ai
    pwsh -File scripts/modularize-src.ps1 -Area ai -Apply
#>
[CmdletBinding()]
param(
    [ValidateSet('pdf', 'ai', 'doc', 'app', 'ocr')]
    [string]$Area = 'pdf',
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 模块 -> 文件基名（不含扩展名）。划分依据见 docs/SOURCE_MODULES.md。
$Maps = @{
    pdf = [ordered]@{
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
    ai = [ordered]@{
        'chat'     = 'ChatTypes SseParser ModelProvider EmbeddingProvider'
        'provider' = 'OpenAiProvider MockProvider ProviderFactory'
        'settings' = 'ProviderConfig ProviderPreset ProviderSelection CredentialStore DataPolicy AuditLog UploadPreview'
        'rag'      = 'LocalHashEmbedding OfflineQa'
        'content'  = 'AiContentDetection AiImageGeneration AiNarration PptGeneration PptxExport'
    }
    doc = [ordered]@{
        'model'   = 'BlankDocument DocumentChunk ChunkBuilder DocumentStatistics'
        'edit'    = 'AnnotationModel EditCommand EditContext PageCommand UndoStack'
        'index'   = 'LocalIndex VectorIndex HybridRetriever CitationVerifier'
        'export'  = 'OfficeExport OfdExport PdfToOffice PptxWriter SpreadsheetExport XmlText ZipWriter'
        'convert' = 'UniversalConverter ConversionInputs InvoiceLayout FontFlattening'
        'print'   = 'PrintTemplate PrintBatch'
        'persist' = 'SafeSave RecoveryLog'
        'batch'   = 'BatchProcessor ActionWizard'
    }
    app = [ordered]@{
        'shell'       = 'main MainWindow WiringActions RibbonBar WelcomePage'
        'ui'          = 'AccessibilityPlan BrandMark Language LanguagePackInstaller ModalScrim ReadingScheme SystemCjkFont Theme TraceText'
        'editor'      = 'PdfView PageCanvas PageLayout PageTransform TextLayout PageTextLines PageChunkBuilder PdfPageOperations TileCache RenderThread ViewFit ZoomMemory NavigationHistory LinkHitTest InkGeometry MeasureGeometry MouseShortcuts ViewModes PresentationWindow'
        'controllers' = 'AiToolService AnnotationController AudioRecorder DigitalSignatureBackend DocumentConverter DocumentOutput DocumentQaController IndexController LastWaveBridge LastWaveWriter NavigationController OcrController OneNoteExporter PresentationLoop ProposalRunner ScannerCapture ScreenCapture TextController'
        'panels'      = 'AiPanel AttachmentPanel IndexPanel LastWavePanels PdfAPreflightPanel SearchPanel'
        'dialogs'     = 'AccessibilityWizard AiSettingsDialog AiToolsDialog AnnotationToolboxDialog BookmarkTreeDialog ConversionInputDialog ConversionToolsDialog DocumentCompareDialog EditProposalDialog EncryptionDialog FormDesignAssistantDialog FormScriptDialog FormToolSettingsDialog HiddenDataDialog PrintToolsDialog ReadingOptionsDialog RegionPicker SettingsDialog SignatureDialog StampNumberDialog UploadPreviewDialog'
        'util'        = 'BatchRename BlankPageDetector ConversionExports ConversionReports SearchMatcher ShareUtils'
    }
    ocr = [ordered]@{
        'core'     = 'OcrTypes OcrEngine OcrGeometry OcrTextUtils'
        'pipeline' = 'OcrService ImagePreprocess ScannedPageDetector OcrChunkBridge'
        'engines'  = 'TesseractCliEngine WindowsOcrEngine OcrEngineFactory'
    }
}

$Modules = $Maps[$Area]
if (-not $Modules) { throw "没有 $Area 的划分表" }

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

$areaDir = Join-Path $Root "src/$Area"
if (-not (Test-Path $areaDir)) { throw "找不到 $areaDir" }

# 只处理源文件：目录里还躺着 app.ico / app.svg 这类资源，不能参与划分。
$sourceExts = @('.h', '.cpp', '.hpp', '.cc', '.mm')

# --- 1. 校验覆盖完整性 -------------------------------------------------------
$onDisk = Get-ChildItem $areaDir -File | Where-Object { $sourceExts -contains $_.Extension } | ForEach-Object { $_.BaseName } | Sort-Object -Unique
$missing = $onDisk | Where-Object { -not $Owner.ContainsKey($_) }
$extra   = $Owner.Keys | Where-Object { $onDisk -notcontains $_ } | Sort-Object
if ($missing) { throw "这些文件没有分配模块：$($missing -join ', ')" }
if ($extra)   { throw "划分表里有 src/$Area 下不存在的文件：$($extra -join ', ')" }

$total = (Get-ChildItem $areaDir -File | Where-Object { $sourceExts -contains $_.Extension }).Count
Write-Host "覆盖校验通过：$total 个文件 / $($Owner.Count) 个基名 / $($Modules.Count) 个模块"
foreach ($module in $Modules.Keys) {
    $n = (Get-ChildItem $areaDir -File | Where-Object { $sourceExts -contains $_.Extension -and $Owner[$_.BaseName] -eq $module }).Count
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
            Old = "$Area/$name$ext"
            New = "$Area/$($Owner[$name])/$name$ext"
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
    New-Item -ItemType Directory -Path (Join-Path $areaDir $module) -Force | Out-Null
}
$moved = 0
foreach ($file in Get-ChildItem $areaDir -File | Where-Object { $sourceExts -contains $_.Extension }) {
    $module = $Owner[$file.BaseName]
    $dest = Join-Path (Join-Path $areaDir $module) $file.Name
    if ($inGit) {
        # 仓库里可能跑着自动提交，偶尔会占住 .git/index.lock；重试几十次再放弃。
        $ok = $false
        for ($try = 1; $try -le 40; $try++) {
            git -C $Root mv -- $file.FullName $dest 2>&1 | Out-Null
            if ($LASTEXITCODE -eq 0) { $ok = $true; break }
            Start-Sleep -Milliseconds 250
        }
        if (-not $ok) { throw "git mv 失败（重试 40 次）：$($file.Name)" }
    } else {
        Move-Item -LiteralPath $file.FullName -Destination $dest
    }
    $moved++
}
Write-Host "已移动 $moved 个文件"

# --- 6. 自检 -----------------------------------------------------------------
$targets = foreach ($dir in $scanDirs) {
    Get-ChildItem $dir -Recurse -File | Where-Object { $exts -contains $_.Extension }
}
$moduleAlt = ($Modules.Keys | Sort-Object -Descending) -join '|'
$qualified = "$Area/($moduleAlt)/[A-Za-z0-9_]+\.(h|cpp)"
$bare      = "$Area/[A-Za-z0-9_]+\.(h|cpp)"
$broken = @()
$stale = @()
foreach ($file in $targets) {
    if (-not (Test-Path $file.FullName)) { continue }
    $text = [IO.File]::ReadAllText($file.FullName)
    foreach ($m in [regex]::Matches($text, $qualified)) {
        if (-not (Test-Path (Join-Path (Join-Path $Root 'src') $m.Value))) {
            $broken += "$($m.Value)  <- $($file.FullName)"
        }
    }
    foreach ($m in [regex]::Matches($text, $bare)) {
        $stale += "$($m.Value)  <- $($file.FullName)"
    }
}
if ($broken) {
    Write-Warning "有引用指向不存在的文件："
    $broken | Select-Object -Unique | ForEach-Object { Write-Warning "  $_" }
    exit 1
}
if ($stale) {
    Write-Warning "有引用没有被改写（缺少模块段）："
    $stale | Select-Object -Unique | ForEach-Object { Write-Warning "  $_" }
    exit 1
}
Write-Host "自检通过：$qualified 全部可解析，且没有残留的 $bare 引用"
