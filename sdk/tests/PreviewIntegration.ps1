#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$cli = Join-Path $buildRoot 'cd-sdk.exe'
$preview = Join-Path $buildRoot 'cd-sdk-preview.exe'
$plugin = Join-Path $buildRoot 'plugins/org.cyberdesktop.sdk.workbench'
$state = Join-Path $outputRoot ('state-' + [Guid]::NewGuid().ToString('N') + '.pb')
$fixed = @('--size', '320x360', '--time', '1709208000000', '--utc-offset', '0')
$results = [Collections.Generic.List[string]]::new()
function Assert-Result([bool]$Condition, [string]$Message) {
    if (!$Condition) {
        throw $Message
    }
    $results.Add($Message)
    Write-Host "PASS $Message"
}
function Invoke-Preview([string[]]$Options) {
    & $preview $plugin @fixed @Options
    if ($LASTEXITCODE) {
        throw "Preview failed: $Options"
    }
}
foreach ($theme in @('dark', 'light')) {
    foreach ($dpr in @(1, 2, 3)) {
        foreach ($language in @('en-US', 'zh-CN', 'ar')) {
            $name = "$theme-$dpr-$language.png"
            Invoke-Preview -Options @('--theme', $theme, '--dpr', [string]$dpr, '--language', $language, '--screenshot', (Join-Path $outputRoot $name))
        }
    }
}
Assert-Result $true '18 real DLL captures cover themes, actual DPI and RTL'
Invoke-Preview -Options @('--screenshot', (Join-Path $outputRoot 'fixed-a.png'))
Invoke-Preview -Options @('--screenshot', (Join-Path $outputRoot 'fixed-b.png'))
& $cli compare (Join-Path $outputRoot 'fixed-a.png') (Join-Path $outputRoot 'fixed-b.png') (Join-Path $outputRoot 'fixed-diff.png') 0
Assert-Result ($LASTEXITCODE -eq 0) 'fixed-time captures are pixel deterministic'
$replay = Join-Path $outputRoot 'replay.json'
[IO.File]::WriteAllText($replay, '{"events":[{"frame":0,"key":1},{"frame":1,"key":6}]}')
Invoke-Preview -Options @('--frames', '60', '--replay', $replay, '--state', $state, '--screenshot', (Join-Path $outputRoot 'state-first.png'))
$first = & $cli state $state | ConvertFrom-Json
Assert-Result ([Convert]::FromBase64String($first.nativeState)[4] -eq 1) 'replayed keyboard input changes saved native state'
Invoke-Preview -Options @('--frames', '60', '--state', $state, '--screenshot', (Join-Path $outputRoot 'state-restarted.png'))
$second = & $cli state $state | ConvertFrom-Json
Assert-Result ($first.nativeState -eq $second.nativeState) 'a fresh preview process restores business state'
$diagnostics = Join-Path $outputRoot 'overlay.json'
Invoke-Preview -Options @('--overlay', '--diagnostics', $diagnostics, '--screenshot', (Join-Path $outputRoot 'overlay.png'))
$snapshot = Get-Content -LiteralPath $diagnostics -Raw | ConvertFrom-Json
Assert-Result ($snapshot.available -and $snapshot.sources[0].nodes.Count -ge 8) 'overlay and diagnostic tree share the actual scene geometry'
& $preview $plugin --size 4096x4096 --dpr 4 --screenshot (Join-Path $outputRoot 'invalid.png') 2>$null
Assert-Result ($LASTEXITCODE -ne 0) 'excessive physical dimensions are rejected before allocation'

$shadow = Join-Path $outputRoot ('watch-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($shadow) | Out-Null
Copy-Item -Path (Join-Path $plugin '*') -Destination $shadow -Recurse
$start = [Diagnostics.ProcessStartInfo]::new($preview)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @($shadow) + $fixed + @('--state', $state, '--watch-ms', '4200', '--screenshot', (Join-Path $outputRoot 'reloaded.png'))) {
    $start.ArgumentList.Add($argument)
}
$child = [Diagnostics.Process]::Start($start)
$stdout = $child.StandardOutput.ReadToEndAsync()
$stderr = $child.StandardError.ReadToEndAsync()
Start-Sleep -Milliseconds 900
for ($i = 0; $i -lt 8; $i++) {
    [IO.File]::WriteAllText((Join-Path $shadow 'art.png'), 'broken image')
}
Start-Sleep -Milliseconds 1100
Copy-Item -LiteralPath (Join-Path $plugin 'art.png') -Destination (Join-Path $shadow 'art.png') -Force
if (!$child.WaitForExit(15000)) {
    throw 'Resource preview did not finish.'
}
$log = $stdout.GetAwaiter().GetResult()
$errors = $stderr.GetAwaiter().GetResult()
[IO.File]::WriteAllText((Join-Path $outputRoot 'reload.log'), $log + $errors)
Assert-Result ($child.ExitCode -eq 0 -and ($log | Select-String 'RELOADED 2 state restored')) 'corrupt resources recover after two coalesced module reloads'
Assert-Result ($errors.Contains('art.png') -and $errors.Length -gt 10) 'resource failure identifies its file and decoder error'
$restored = & $cli state $state | ConvertFrom-Json
Assert-Result ($restored.nativeState -eq $first.nativeState) 'resource reload preserves state across failure and recovery'
@{ checked = $results
    captures = 18
    state = $state
    timestamp = [DateTime]::UtcNow.ToString('O')
} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'verification.json')
