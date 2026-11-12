<#
.SYNOPSIS
    第三方许可证全文的规范化处理与哈希计算（取件、校验、打包共用）。

.DESCRIPTION
    许可证全文的来源行尾并不统一：gnu.org 提供的 LGPL-3.0 / GPL-3.0 是 LF，
    MinGW-w64 工具包里的 licenses/gcc/COPYING.RUNTIME 是 CRLF；而仓库按
    .gitattributes（* text=auto eol=lf）以 LF 存对象库，检出时也按 LF 落地。

    如果直接对文件字节算哈希，同一份许可证会因为行尾不同而对不上：
    本地工作区可能是 CRLF，CI runner 检出的是 LF，两边算出两个哈希，
    而清单里只能记一个 —— 这会让远端流水线必然失败。

    这里统一按「规范化文本」处理：CRLF 与孤立 CR 全部折成 LF，再算 SHA-256。
    规范化后的哈希才是许可证内容的身份，与行尾、与检出方式无关。

    注意：PDFium 及其第三方组件的许可证原文不走这里。它们来自固定 SHA-256
    的官方 tarball，字节完全确定，用原始字节哈希才是更强的保证。
#>

function ConvertTo-AiPdfCanonicalTextBytes {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    $text = [System.Text.Encoding]::UTF8.GetString($Bytes)
    # 先把 CRLF 折成 LF，剩下的孤立 CR 也折成 LF。
    $text = $text -replace "`r`n", "`n"
    $text = $text -replace "`r", "`n"
    return [System.Text.Encoding]::UTF8.GetBytes($text)
}

function Get-AiPdfCanonicalTextHash {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    $canonical = ConvertTo-AiPdfCanonicalTextBytes -Bytes $Bytes
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return (($sha.ComputeHash($canonical) | ForEach-Object { $_.ToString('x2') }) -join '')
    } finally {
        $sha.Dispose()
    }
}

function Get-AiPdfCanonicalTextHashFromFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return ''
    }
    return Get-AiPdfCanonicalTextHash -Bytes ([System.IO.File]::ReadAllBytes($Path))
}

function Write-AiPdfCanonicalTextFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][byte[]]$Bytes
    )

    $canonical = ConvertTo-AiPdfCanonicalTextBytes -Bytes $Bytes
    [System.IO.File]::WriteAllBytes($Path, $canonical)
}
