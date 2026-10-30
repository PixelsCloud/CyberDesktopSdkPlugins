#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceDirectory,
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$PluginDirectory,
    [Parameter(Mandatory)][string]$PreviewExecutable,
    [string]$WorkDirectory,
    [string]$BuildTarget,
    [ValidateRange(1, 16)][int]$Jobs = 2,
    [string[]]$PreviewArguments = @(),
    [switch]$Once,
    [ValidateRange(0, 100000)][int]$MaxBuilds = 0
)

$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath($SourceDirectory)
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
$pluginRoot = [IO.Path]::GetFullPath($PluginDirectory)
$previewPath = (Resolve-Path -LiteralPath $PreviewExecutable).Path
if (!$WorkDirectory) {
    $WorkDirectory = Join-Path $buildRoot '.preview'
}
$workRoot = [IO.Path]::GetFullPath($WorkDirectory)
if (!(Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt'))) {
    throw 'Configure the CMake build first using the SDK tutorial.'
}
if ($workRoot -eq $pluginRoot -or $pluginRoot.StartsWith($workRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'PluginDirectory must be outside WorkDirectory.'
}
[IO.Directory]::CreateDirectory($workRoot) | Out-Null
$statePath = Join-Path $workRoot 'state.pb'
$env:VCPKG_MAX_CONCURRENCY = [string]$Jobs
$env:CMAKE_BUILD_PARALLEL_LEVEL = [string]$Jobs
$env:CMAKE_AUTOGEN_PARALLEL = [string]$Jobs
$script:preview = $null
$script:activeRoot = $null
$script:inventory = @{}
$script:generation = 0
$script:stopSignal = $null

if (Test-Path -LiteralPath (Join-Path $workRoot 'session.json')) {
    $previous = Get-Content -LiteralPath (Join-Path $workRoot 'session.json') -Raw | ConvertFrom-Json
    $running = Get-Process -Id $previous.pid -ErrorAction SilentlyContinue
    if ($running -and $running.Path -eq $previewPath) {
        throw 'Close the existing preview before restarting its development controller.'
    }
}

function Initialize-PreviewSlot([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -notin @((Join-Path $workRoot 'preview-a'), (Join-Path $workRoot 'preview-b')) -or
        $full -eq $script:activeRoot) {
        throw 'Only an inactive fixed preview slot may be refreshed.'
    }
    for ($ancestor = $full; $ancestor; $ancestor = Split-Path $ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Preview slots must not traverse a reparse point.'
        }
    }
    $marker = Join-Path $full '.sdk-preview-slot'
    if (Test-Path -LiteralPath $full) {
        if (!(Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne 'CyberDesktopSDK') {
            throw 'Existing preview slot is not owned by the SDK development tool.'
        }
        Remove-Item -LiteralPath $full -Recurse -Force
    }
    [IO.Directory]::CreateDirectory($full) | Out-Null
    Set-Content -LiteralPath $marker -Value 'CyberDesktopSDK'
}

function Get-PackageInventory {
    if (!(Test-Path -LiteralPath (Join-Path $pluginRoot 'plugin.dll'))) {
        throw 'Build did not produce plugin.dll.'
    }
    $result = @{}
    foreach ($file in Get-ChildItem -LiteralPath $pluginRoot -File -Recurse) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Preview package cannot contain links.'
        }
        if ($file.Extension -in @('.pdb', '.ilk', '.lib', '.exp')) {
            continue
        }
        $relative = [IO.Path]::GetRelativePath($pluginRoot, $file.FullName)
        if ($relative.StartsWith('..')) {
            throw 'Package file escaped its directory.'
        }
        if ($result.Count -ge 512) {
            throw 'Preview package exceeds 512 files.'
        }
        $result[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    return $result
}

function Copy-PackageFile([string]$Relative, [string]$DestinationRoot) {
    $destination = [IO.Path]::GetFullPath((Join-Path $DestinationRoot $Relative))
    if (!$destination.StartsWith($workRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Preview destination escaped WorkDirectory.'
    }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $pluginRoot $Relative) -Destination $destination -Force
}

function Stop-Preview {
    if (!$script:preview -or $script:preview.HasExited) {
        return $true
    }
    if ($script:stopSignal) {
        $script:stopSignal.Set() | Out-Null
    }
    else {
        $script:preview.CloseMainWindow() | Out-Null
    }
    if (!$script:preview.WaitForExit(5000)) {
        Write-Warning 'Preview did not close; keeping the existing process and loaded package. Retry after closing it.'
        return $false
    }
    return $true
}

function Invoke-IncrementalBuild {
    $buildArguments = @('--build', $buildRoot, '--parallel', [string]$Jobs)
    if ($BuildTarget) {
        $buildArguments += @('--target', $BuildTarget)
    }
    $log = Join-Path $workRoot 'build.log'
    $buildProcess = [Diagnostics.Process]::GetCurrentProcess()
    $originalAffinity = $buildProcess.ProcessorAffinity
    [long]$mask = 0
    $count = 0
    for ($index = 0; $index -lt 63 -and $count -lt $Jobs; $index++) {
        [long]$bit = [long]1 -shl $index
        if ($originalAffinity.ToInt64() -band $bit) {
            $mask = $mask -bor $bit
            $count++
        }
    }
    try {
        $buildProcess.ProcessorAffinity = [IntPtr]$mask
        & cmake @buildArguments *> $log
        $buildExit = $LASTEXITCODE
    }
    finally {
        $buildProcess.ProcessorAffinity = $originalAffinity
        $buildProcess.Dispose()
    }
    if ($buildExit) {
        Get-Content -LiteralPath $log -Tail 20 | Write-Host
        Write-Warning 'Build failed; the previous preview, settings and state remain available.'
        return $false
    }
    try {
        $next = Get-PackageInventory
    }
    catch {
        Write-Warning $_
        return $false
    }
    $binaryChanged = !$script:activeRoot -or !$script:preview -or $script:preview.HasExited
    foreach ($file in @($next.Keys) + @($script:inventory.Keys) | Sort-Object -Unique) {
        if ([IO.Path]::GetExtension($file) -eq '.dll' -and $next[$file] -ne $script:inventory[$file]) {
            $binaryChanged = $true
        }
    }
    if (!$binaryChanged) {
        foreach ($file in $next.Keys) {
            if ($next[$file] -ne $script:inventory[$file]) {
                Copy-PackageFile $file $script:activeRoot
            }
        }
        foreach ($file in $script:inventory.Keys) {
            if (!$next.ContainsKey($file)) {
                $obsolete = [IO.Path]::GetFullPath((Join-Path $script:activeRoot $file))
                if (!$obsolete.StartsWith($script:activeRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                    throw 'Obsolete resource escaped the active package.'
                }
                Remove-Item -LiteralPath $obsolete -Force
            }
        }
        $script:inventory = $next
        Write-Host 'Resources updated; the existing preview will reload and restore its state.'
        return $true
    }
    # Build against the original output while the old process uses a shadow DLL.
    # Stage before closing it, so copy failures do not interrupt the working preview.
    $script:generation++
    $generationRoot = Join-Path $workRoot $(if ($script:activeRoot -eq (Join-Path $workRoot 'preview-a')) {
            'preview-b'
        }
        else {
            'preview-a'
        })
    Initialize-PreviewSlot $generationRoot
    foreach ($file in $next.Keys) {
        Copy-PackageFile $file $generationRoot
    }
    if (!(Stop-Preview)) {
        return $false
    }
    $start = [Diagnostics.ProcessStartInfo]::new($previewPath)
    $start.UseShellExecute = $false
    $start.ArgumentList.Add($generationRoot)
    $start.ArgumentList.Add('--state')
    $start.ArgumentList.Add($statePath)
    if ($script:stopSignal) {
        $script:stopSignal.Dispose()
    }
    $stopName = 'Local\CyberDesktop.SdkPreview.' + [Guid]::NewGuid().ToString('N')
    $script:stopSignal = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $stopName)
    $start.ArgumentList.Add('--stop-event')
    $start.ArgumentList.Add($stopName)
    foreach ($argument in $PreviewArguments) {
        $start.ArgumentList.Add($argument)
    }
    if ('--screenshot' -in $PreviewArguments) {
        $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $start.CreateNoWindow = $true
    }
    $script:preview = [Diagnostics.Process]::Start($start)
    $script:activeRoot = $generationRoot
    $script:inventory = $next
    @{ pid = $script:preview.Id
        stopEvent = $stopName
        package = $generationRoot
        state = $statePath
        generation = $script:generation
    } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $workRoot 'session.json')
    Write-Host "Preview generation $script:generation started; state: $statePath"
    return $true
}

$watcher = [IO.FileSystemWatcher]::new($sourceRoot)
$watcher.IncludeSubdirectories = $true
$watcher.NotifyFilter = [IO.NotifyFilters]'FileName, DirectoryName, LastWrite, Size'
$prefix = 'CyberDesktop.Dev.' + [Guid]::NewGuid().ToString('N')
$registrations = @()
try {
    foreach ($eventName in @('Changed', 'Created', 'Deleted', 'Renamed', 'Error')) {
        $registrations += Register-ObjectEvent -InputObject $watcher -EventName $eventName -SourceIdentifier "$prefix.$eventName"
    }
    $watcher.EnableRaisingEvents = $true
    $attempts = 1
    $success = Invoke-IncrementalBuild
    if ($Once) {
        if (!$success) {
            exit 1
        }
        if ('--screenshot' -in $PreviewArguments) {
            $script:preview.WaitForExit()
            exit $script:preview.ExitCode
        }
        exit 0
    }
    $pending = $false
    $lastChange = [DateTime]::UtcNow
    while (!$MaxBuilds -or $attempts -lt $MaxBuilds) {
        foreach ($event in @(Get-Event | Where-Object SourceIdentifier -Like "$prefix.*")) {
            Remove-Event -EventIdentifier $event.EventIdentifier
            $path = $event.SourceEventArgs.FullPath
            if ($path) {
                $full = [IO.Path]::GetFullPath($path)
                if ($full.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
                    $full.StartsWith($workRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
                    $full -match '[/\\](\.git|\.vs)[/\\]') {
                    continue
                }
            }
            $pending = $true
            $lastChange = [DateTime]::UtcNow
        }
        if ($pending -and ([DateTime]::UtcNow - $lastChange).TotalMilliseconds -ge 350) {
            $pending = $false
            $attempts++
            $success = Invoke-IncrementalBuild
        }
        Start-Sleep -Milliseconds 100
    }
    if (!$success) {
        exit 1
    }
}
finally {
    $watcher.EnableRaisingEvents = $false
    foreach ($registration in $registrations) {
        Unregister-Event -SubscriptionId $registration.SubscriptionId
    }
    Get-Event | Where-Object SourceIdentifier -Like "$prefix.*" | Remove-Event
    $watcher.Dispose()
}
