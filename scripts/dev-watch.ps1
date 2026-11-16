[CmdletBinding()]
param(
    [string]$Preset = 'windows-debug',
    [switch]$NoLaunch,
    [int]$DebounceMilliseconds = 600
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build/$Preset"
$executable = Join-Path $buildDir 'bin/AiPdfEditor.exe'
$watchedExtensions = @('.cpp', '.h', '.hpp', '.c', '.cmake', '.qrc', '.ui', '.ts', '.svg')
$watchedNames = @('CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json')
$appProcess = $null

function Test-SourcePath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath.StartsWith($buildDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }
    $name = [System.IO.Path]::GetFileName($fullPath)
    $extension = [System.IO.Path]::GetExtension($fullPath).ToLowerInvariant()
    return $watchedNames -contains $name -or $watchedExtensions -contains $extension
}

function Stop-WatchedApp {
    if ($null -eq $script:appProcess) {
        return $true
    }
    if (-not $script:appProcess.HasExited) {
        Write-Host 'Stopping the dev instance...' -ForegroundColor DarkGray
        $script:appProcess.CloseMainWindow() | Out-Null
        if (-not $script:appProcess.WaitForExit(3000)) {
            Write-Host 'The app is still open (possibly waiting to save changes); build skipped.' -ForegroundColor Yellow
            return $false
        }
    }
    $script:appProcess = $null
    return $true
}

function Build-App {
    if (-not (Stop-WatchedApp)) {
        return $false
    }
    Write-Host "Building preset '$Preset'..." -ForegroundColor Cyan
    & cmake --build $buildDir --target AiPdfEditor -j 4
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'Build failed; waiting for the next source change.' -ForegroundColor Red
        return $false
    }
    Write-Host 'Build succeeded.' -ForegroundColor Green
    if (-not $NoLaunch) {
        $script:appProcess = Start-Process -FilePath $executable -PassThru
        Write-Host "Started PID $($script:appProcess.Id)." -ForegroundColor DarkGray
    }
    return $true
}

if (-not (Test-Path $buildDir)) {
    Write-Host "Configuring preset '$Preset'..." -ForegroundColor Cyan
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for preset '$Preset'."
    }
}

$watcher = [System.IO.FileSystemWatcher]::new($repoRoot)
$watcher.IncludeSubdirectories = $true
$watcher.NotifyFilter = [System.IO.NotifyFilters]'FileName, LastWrite, Size'
$watcher.EnableRaisingEvents = $true

$eventIds = @('Folixa.SourceChanged', 'Folixa.SourceCreated', 'Folixa.SourceRenamed', 'Folixa.SourceDeleted')
$subscriptions = @(
    Register-ObjectEvent $watcher Changed -SourceIdentifier $eventIds[0]
    Register-ObjectEvent $watcher Created -SourceIdentifier $eventIds[1]
    Register-ObjectEvent $watcher Renamed -SourceIdentifier $eventIds[2]
    Register-ObjectEvent $watcher Deleted -SourceIdentifier $eventIds[3]
)

try {
    Build-App | Out-Null
    Write-Host 'Watching source files. Press Ctrl+C to stop.' -ForegroundColor Yellow
    while ($true) {
        $event = Wait-Event
        if (-not (Test-SourcePath $event.SourceEventArgs.FullPath)) {
            Remove-Event -EventIdentifier $event.EventIdentifier
            continue
        }

        $changedPath = $event.SourceEventArgs.FullPath
        Remove-Event -EventIdentifier $event.EventIdentifier
        Start-Sleep -Milliseconds $DebounceMilliseconds

        # Editors commonly emit several write/rename events for one save. Drain them into one build.
        Get-Event | ForEach-Object { Remove-Event -EventIdentifier $_.EventIdentifier }
        Write-Host "Change detected: $changedPath" -ForegroundColor DarkYellow
        Build-App | Out-Null
    }
}
finally {
    Stop-WatchedApp
    foreach ($subscription in $subscriptions) {
        Unregister-Event -SubscriptionId $subscription.Id -ErrorAction SilentlyContinue
    }
    $watcher.Dispose()
}
