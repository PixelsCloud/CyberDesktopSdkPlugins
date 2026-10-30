#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$SdkDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$kit = (Resolve-Path -LiteralPath $SdkDirectory).Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$source = Join-Path $outputRoot 'source'
$build = Join-Path $outputRoot 'build'
$work = Join-Path $build '.preview'
if (Test-Path -LiteralPath "$work/session.json") {
    $previous = Get-Content -LiteralPath "$work/session.json" -Raw | ConvertFrom-Json
    $running = Get-Process -Id $previous.pid -ErrorAction SilentlyContinue
    if ($running -and $running.Path -eq (Join-Path $kit 'bin/cd-sdk-preview.exe')) {
        throw 'The previous test preview must exit before reusing this test workspace.'
    }
    foreach ($name in @('state.pb', 'session.json')) {
        $generated = [IO.Path]::GetFullPath((Join-Path $work $name))
        if (!$generated.StartsWith($outputRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Generated test state escaped the test workspace.'
        }
        if (Test-Path -LiteralPath $generated) {
            Remove-Item -LiteralPath $generated -Force
        }
    }
}
[IO.Directory]::CreateDirectory($source) | Out-Null
$share = Join-Path $kit 'share/cyberdesktop-sdk'
$cppSource = Get-Content -LiteralPath (Join-Path $share 'examples/Workbench.cpp') -Raw
$cppSource | Set-Content -LiteralPath (Join-Path $source 'Plugin.cpp') -Encoding utf8
$components = Join-Path $source 'templates/components'
[IO.Directory]::CreateDirectory($components) | Out-Null
foreach ($file in @('Card.h', 'Card.cpp', 'ServiceModel.h', 'ServiceModel.cpp')) {
    Copy-Item -LiteralPath (Join-Path $share "templates/components/$file") -Destination (Join-Path $components $file)
}
Copy-Item -LiteralPath (Join-Path $share 'examples/workbench/art.png') -Destination (Join-Path $source 'art.png')
Copy-Item -LiteralPath (Join-Path $share 'examples/workbench/plugin.textproto') -Destination (Join-Path $source 'plugin.textproto')
@'
cmake_minimum_required(VERSION 3.21)
project(InstalledWorkbench LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
find_package(CyberDesktopSDK CONFIG REQUIRED)
cyberdesktop_add_plugin(example ID org.cyberdesktop.sdk.workbench DESCRIPTOR "${CMAKE_CURRENT_SOURCE_DIR}/plugin.textproto"
    SOURCES Plugin.cpp templates/components/Card.cpp templates/components/ServiceModel.cpp ASSETS art.png)
target_include_directories(example PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
'@ | Set-Content -LiteralPath (Join-Path $source 'CMakeLists.txt')
& cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$kit"
if ($LASTEXITCODE) {
    throw 'External development client configure failed.'
}
$replay = Join-Path $outputRoot 'replay.json'
[IO.File]::WriteAllText($replay, '{"events":[{"frame":1,"key":1},{"frame":2,"key":6}]}')
# ArgumentList passes each string directly; the wrapper preserves the array-valued parameter.
$wrapper = Join-Path $outputRoot 'run-dev.ps1'
$parameterFile = Join-Path $outputRoot 'dev-parameters.json'
@{ SourceDirectory = $source
    BuildDirectory = $build
    PluginDirectory = (Join-Path $build 'plugins/org.cyberdesktop.sdk.workbench')

    PreviewExecutable = (Join-Path $kit 'bin/cd-sdk-preview.exe')
    Jobs = 2
    BuildTarget = 'example_descriptor'
    MaxBuilds = 4

    PreviewArguments = @('--size', '320x360', '--time', '1709208000000', '--frames', '60', '--replay', $replay,
        '--watch-ms', '30000', '--screenshot', (Join-Path $outputRoot 'last.png'))
} |
    ConvertTo-Json | Set-Content -LiteralPath $parameterFile
@'
param($ParametersFile, $DevScript)
$parameters = Get-Content -LiteralPath $ParametersFile -Raw | ConvertFrom-Json -AsHashtable
& $DevScript @parameters
'@ | Set-Content -LiteralPath $wrapper
$start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @('-NoProfile', '-File', $wrapper, $parameterFile, (Join-Path $share 'scripts/dev-plugin.ps1'))) {
    $start.ArgumentList.Add($argument)
}
$controller = [Diagnostics.Process]::Start($start)
$stdout = $controller.StandardOutput.ReadToEndAsync()
$stderr = $controller.StandardError.ReadToEndAsync()
$results = [Collections.Generic.List[string]]::new()
function Wait-For([scriptblock]$Condition, [string]$Message) {
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (& $Condition) {
            $results.Add($Message)
            Write-Host "PASS $Message"
            return
        }
        if ($controller.HasExited -and $controller.ExitCode -ne 0) {
            throw "Development controller failed: $($stderr.GetAwaiter().GetResult())"
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out: $Message"
}
function Session {
    $path = Join-Path $work 'session.json'
    if (Test-Path -LiteralPath $path) {
        try {
            return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        }
        catch {
            return $null
        }
    }
    return $null
}
try {
    Wait-For { (Session).generation -eq 1 } 'installed SDK builds and starts a shadow preview'
    $first = Session
    Wait-For { Test-Path -LiteralPath $first.state } 'running preview saves its instance state'
    $asset = Join-Path $source 'art.png'
    $bytes = [IO.File]::ReadAllBytes($asset)
    [IO.File]::WriteAllBytes($asset, [byte[]]($bytes + [byte]0))
    $hash = (Get-FileHash -LiteralPath $asset).Hash
    Wait-For { (Get-FileHash -LiteralPath (Join-Path $first.package 'art.png')).Hash -eq $hash } 'asset changes stage without replacing the loaded DLL'
    if ((Session).pid -ne $first.pid) {
        throw 'Resource-only change restarted the preview process.'
    }
    Start-Sleep -Milliseconds 700
    $cpp = Join-Path $source 'Plugin.cpp'
    $original = [IO.File]::ReadAllText($cpp)
    [IO.File]::WriteAllText($cpp, $original + "`n#error deliberate_build_failure`n")
    Wait-For { (Get-Content -LiteralPath (Join-Path $work 'build.log') -Raw) -match 'deliberate_build_failure' } 'deliberately broken C++ produces a visible build error'
    if (!(Get-Process -Id $first.pid -ErrorAction SilentlyContinue)) {
        throw 'Build failure terminated the working preview.'
    }
    $results.Add('build failure preserves the working preview process')
    Start-Sleep -Milliseconds 700
    [IO.File]::WriteAllText($cpp, $original.Replace('L"Canvas Workbench"', 'L"Canvas Workbench v2"'))
    Wait-For { (Session).generation -eq 2 } 'successful C++ rebuild restarts the preview with a new shadow DLL'
    $second = Session
    if ($second.pid -eq $first.pid -or (Get-Process -Id $first.pid -ErrorAction SilentlyContinue)) {
        throw 'Old preview did not exit gracefully.'
    }
    Wait-For {
        $saved = & (Join-Path $kit 'bin/cd-sdk.exe') state $second.state | ConvertFrom-Json
        [Convert]::FromBase64String($saved.nativeState)[4] -eq 2
    } 'replacement process restores the previous count before replaying another action'
    if (!$controller.WaitForExit(5000) -or $controller.ExitCode -ne 0) {
        throw 'Bounded development loop failed.'
    }
    $signal = [Threading.EventWaitHandle]::OpenExisting($second.stopEvent)
    $signal.Set() | Out-Null
    $signal.Dispose()
    $child = [Diagnostics.Process]::GetProcessById([int]$second.pid)
    if (!$child.WaitForExit(5000)) {
        throw 'Preview stop handshake failed.'
    }
    if (@(Get-ChildItem -LiteralPath $work -Directory -Filter 'preview-*').Count -ne 2) {
        throw 'Development loop must reuse exactly two fixed shadow slots.'
    }
    $results.Add('two fixed shadow slots bound the development workspace')
    [IO.File]::WriteAllText((Join-Path $outputRoot 'dev.log'), $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult())
    @{ checked = $results
        timestamp = [DateTime]::UtcNow.ToString('O')
    } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $outputRoot 'verification.json')
}
finally {
    $latest = Session
    if ($latest) {
        try {
            $signal = [Threading.EventWaitHandle]::OpenExisting($latest.stopEvent)
            $signal.Set() | Out-Null
            $signal.Dispose()
        }
        catch {
        }
    }
    if (!$controller.HasExited) {
        $controller.Kill()
    }
    $controller.Dispose()
}
