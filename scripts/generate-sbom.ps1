<#
.SYNOPSIS
    为打包目录生成 CycloneDX 1.5 SBOM。

.DESCRIPTION
    逐个文件计算 SHA-256、读取文件版本号，并按文件名判定许可证，
    同时登记 Qt 与 MinGW-w64 两个组件本身（带 purl）。
    Qt 官方随包提供的 SPDX SBOM 作为外部引用挂到 Qt 组件上。
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [string]$Version = '0.3.0',
    [string]$QtVersion = '6.8.3',
    [string]$MinGWVersion = '13.1.0',
    [string]$PdfiumVersion = '',
    [string]$QtSbomPath = ''
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'LicenseMap.ps1')

function Get-FileHashHex {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$packageRoot = (Resolve-Path -LiteralPath $PackageDir).Path
$files = Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Sort-Object FullName

$components = New-Object System.Collections.Generic.List[object]
$dependencies = New-Object System.Collections.Generic.List[object]

$applicationRef = "ai-pdf-editor@$Version"
$dependencies.Add([pscustomobject]@{
    ref       = $applicationRef
    dependsOn = @()
})

$qtRef = "pkg:generic/qt@$QtVersion"
$components.Add([pscustomobject]@{
    type    = 'library'
    'bom-ref' = $qtRef
    name    = 'Qt'
    version = $QtVersion
    purl    = $qtRef
    licenses = @(@{ license = @{ id = 'LGPL-3.0-only' } })
    description = 'Qt Base (QtCore, QtGui, QtWidgets, QtNetwork), official binary kit for win64_mingw'
    externalReferences = @(
        @{ type = 'website'; url = 'https://www.qt.io/licensing/open-source-lgpl-obligations' },
        @{ type = 'distribution'; url = 'https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/' }
    )
})
$dependencies.Add([pscustomobject]@{
    ref       = $applicationRef
    dependsOn = @($qtRef)
})

$mingwRef = "pkg:generic/mingw-w64@$MinGWVersion"
$components.Add([pscustomobject]@{
    type    = 'library'
    'bom-ref' = $mingwRef
    name    = 'MinGW-w64 runtime'
    version = $MinGWVersion
    purl    = $mingwRef
    licenses = @(@{ license = @{ id = 'GPL-3.0-with-GCC-exception' } })
    description = 'MinGW-w64 GCC runtime libraries (libgcc, libstdc++, libwinpthread)'
})
$dependencies.Add([pscustomobject]@{
    ref       = $applicationRef
    dependsOn = @($mingwRef)
})

if ($PdfiumVersion) {
    $pdfiumRef = "pkg:generic/pdfium@$PdfiumVersion"
    $components.Add([pscustomobject]@{
        type    = 'library'
        'bom-ref' = $pdfiumRef
        name    = 'PDFium'
        version = $PdfiumVersion
        purl    = $pdfiumRef
        licenses = @(@{ license = @{ id = 'BSD-3-Clause' } })
        description = 'Chromium PDFium (PDFium API ' + $PdfiumVersion + '), BSD-3-Clause'
        externalReferences = @(
            @{ type = 'website'; url = 'https://pdfium.googlesource.com/pdfium/' },
            @{ type = 'vcs'; url = 'https://github.com/bblanchon/pdfium-binaries' }
        )
    })
    $dependencies.Add([pscustomobject]@{
        ref       = $applicationRef
        dependsOn = @($pdfiumRef)
    })
}

foreach ($file in $files) {
    $relative = $file.FullName.Substring($packageRoot.Length).TrimStart('\', '/')
    $versionInfo = $null
    try {
        $versionInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($file.FullName)
    } catch {
        $versionInfo = $null
    }
    $fileVersion = if ($versionInfo -and $versionInfo.FileVersion) { $versionInfo.FileVersion } else { '' }
    $license = Get-AiPdfLicense -Name $file.Name
    $ref = "file:$($relative -replace '\\','/')"

    $components.Add([pscustomobject]@{
        type    = 'file'
        'bom-ref' = $ref
        name    = $file.Name
        version = $fileVersion
        hashes = @(@{ alg = 'SHA-256'; content = (Get-FileHashHex -Path $file.FullName) })
        licenses = @(@{ license = @{ id = $license.id } })
        properties = @(
            @{ name = 'aipdf:packagePath'; value = ($relative -replace '\\', '/') },
            @{ name = 'aipdf:sizeBytes'; value = "$($file.Length)" }
        )
    })
    $dependencies.Add([pscustomobject]@{
        ref       = $applicationRef
        dependsOn = @($ref)
    })
}

if ($QtSbomPath -and (Test-Path -LiteralPath $QtSbomPath)) {
    $components | Where-Object { $_.'bom-ref' -eq $qtRef } | ForEach-Object {
        $_.externalReferences += @{
            type = 'other'
            url  = "file://sbom/$(Split-Path -Leaf $QtSbomPath)"
            comment = "Qt 官方随包提供的 SPDX SBOM：$(Split-Path -Leaf $QtSbomPath)"
        }
    }
}

$document = [ordered]@{
    bomFormat    = 'CycloneDX'
    specVersion  = '1.5'
    serialNumber = "urn:uuid:$([guid]::NewGuid())"
    version      = 1
    metadata     = [ordered]@{
        timestamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        tools = [ordered]@{
            components = @(
                [ordered]@{
                    type = 'application'
                    name = 'scripts/generate-sbom.ps1'
                    version = $Version
                }
            )
        }
        component = [ordered]@{
            type = 'application'
            'bom-ref' = $applicationRef
            name = 'AI PDF Editor'
            version = $Version
            licenses = @(@{ license = @{ id = 'LicenseRef-Project-Proprietary' } })
        }
    }
    components = $components
    dependencies = $dependencies
}

$json = $document | ConvertTo-Json -Depth 12
$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
[System.IO.File]::WriteAllText($OutputPath, $json, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "SBOM 已生成: $OutputPath ($($components.Count) 个组件)"
