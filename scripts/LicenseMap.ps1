<#
.SYNOPSIS
    AI PDF Editor 打包产物与 SBOM 共用的许可证判定表。

.DESCRIPTION
    按文件名判定随包分发的第三方二进制使用哪份许可证。
    scripts\generate-sbom.ps1 与 scripts\package.ps1 共用这份判定，
    避免 NOTICE 与 SBOM 对同一文件给出不同结论。
#>

function Get-AiPdfLicense {
    param([Parameter(Mandatory = $true)][string]$Name)

    $lower = $Name.ToLowerInvariant()
    if ($lower -match '^(qt6|qt\.dll|qwindows|qmodernwindowsstyle|qxcbsupport|qtquick|qml)') {
        return @{ id = 'LGPL-3.0-only'; name = 'GNU Lesser General Public License v3.0 only' }
    }
    if ($lower -match '^(libgcc|libstdc\+\+|libwinpthread|libgomp|libssp)') {
        return @{ id = 'GPL-3.0-with-GCC-exception'; name = 'GNU General Public License v3.0 with GCC Runtime Library Exception' }
    }
    if ($lower -match '^pdfium\.dll$') {
        return @{ id = 'BSD-3-Clause'; name = 'BSD 3-Clause License (PDFium)' }
    }
    return @{ id = 'LicenseRef-Project-Proprietary'; name = 'AI PDF Editor build output, all rights reserved' }
}
