# 用 Windows 自带的 PDF 渲染器（Windows.Data.Pdf）把某一页画成 PNG。
#
# 用途：跨引擎对照。PDFium 的渲染结果不能代表别的阅读器（它还会按 /QuadPoints
# 自己合成一层底色），所以"批注在别人那儿看不看得见"必须用第二个引擎验证。
# Windows.Data.Pdf 是微软自带的实现，与我们的 PDFium 路径完全独立。
#
# 例：
#   powershell -ExecutionPolicy Bypass -File scripts\render-pdf-windows.ps1 `
#       -PdfPath out.pdf -OutPng page.png -PageIndex 0 -Width 612
param(
    [Parameter(Mandatory = $true)][string]$PdfPath,
    [Parameter(Mandatory = $true)][string]$OutPng,
    [int]$PageIndex = 0,
    [int]$Width = 612
)

$ErrorActionPreference = 'Stop'

function Show-ExceptionChain($exception) {
    $current = $exception
    $depth = 0
    while ($current -and $depth -lt 6) {
        Write-Host ("  [" + $depth + "] " + $current.GetType().FullName + ": " + $current.Message)
        $current = $current.InnerException
        $depth++
    }
}

Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null
$methods = [System.WindowsRuntimeSystemExtensions].GetMethods()
$asTaskGeneric = ($methods | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
    })[0]
$asTaskAction = ($methods | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction'
    })[0]

# WinRT 的异步调用在 PowerShell 里没有 await，只能转成 Task 再等。
function Wait-Operation($operation, [Type]$resultType) {
    $task = $asTaskGeneric.MakeGenericMethod($resultType).Invoke($null, @($operation))
    try {
        $task.Wait()
    } catch {
        Show-ExceptionChain $_.Exception
        throw "渲染失败：等待异步调用时出错"
    }
    return $task.Result
}

function Wait-Action($operation) {
    $task = $asTaskAction.Invoke($null, @($operation))
    try {
        $task.Wait()
    } catch {
        Show-ExceptionChain $_.Exception
        throw "渲染失败：等待异步操作时出错"
    }
}

[Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
[Windows.Data.Pdf.PdfDocument, Windows.Data.Pdf, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.Streams.InMemoryRandomAccessStream, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null

$absolute = (Resolve-Path -LiteralPath $PdfPath).Path
$file = Wait-Operation ([Windows.Storage.StorageFile]::GetFileFromPathAsync($absolute)) ([Windows.Storage.StorageFile])
$document = Wait-Operation ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($file)) ([Windows.Data.Pdf.PdfDocument])

if ($PageIndex -lt 0 -or $PageIndex -ge $document.PageCount) {
    throw "页码越界：文档共 $($document.PageCount) 页，请求第 $PageIndex 页"
}

$page = $document.GetPage($PageIndex)
$stream = New-Object Windows.Storage.Streams.InMemoryRandomAccessStream
$options = New-Object Windows.Data.Pdf.PdfPageRenderOptions
$options.DestinationWidth = [uint32]$Width
Wait-Action ($page.RenderToStreamAsync($stream, $options))

$reader = New-Object Windows.Storage.Streams.DataReader($stream.GetInputStreamAt(0))
$size = [uint32]$stream.Size
Wait-Operation ($reader.LoadAsync($size)) ([uint32]) | Out-Null
$bytes = New-Object byte[] $size
$reader.ReadBytes($bytes)
[System.IO.File]::WriteAllBytes($OutPng, $bytes)

Write-Host ("OK " + $OutPng + " (" + $bytes.Length + " bytes, page " + $PageIndex + " of " + $document.PageCount + ")")
