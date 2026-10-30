#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$SdkDirectory,
    [Parameter(Mandatory)][string]$WorkDirectory,
    [ValidateRange(1, 16)][int]$Jobs = 16
)
$ErrorActionPreference = 'Stop'
$verificationProcess = [Diagnostics.Process]::GetCurrentProcess()
$verificationAffinity = $verificationProcess.ProcessorAffinity
[long]$mask = 0
$count = 0
for ($index = 0; $index -lt 63 -and $count -lt $Jobs; $index++) {
    [long]$bit = [long]1 -shl $index
    if ($verificationAffinity.ToInt64() -band $bit) {
        $mask = $mask -bor $bit
        $count++
    }
}
if (!$mask) {
    throw 'No build CPU available'
}
try {
    $verificationProcess.ProcessorAffinity = [IntPtr]$mask
    $sdk = (Resolve-Path -LiteralPath $SdkDirectory).Path
    $work = [IO.Path]::GetFullPath($WorkDirectory)
    if ((Test-Path -LiteralPath $work) -and @(Get-ChildItem -LiteralPath $work -Force).Count -and
        !(Test-Path -LiteralPath "$work/starter-workspace.json") -and !(Test-Path -LiteralPath "$work/verification.json")) {
        throw 'Existing starter work directory does not belong to this verification'
    }
    for ($ancestor = $work; $ancestor; $ancestor = Split-Path $ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Starter work directory must not traverse a reparse point'
        }
    }
    [IO.Directory]::CreateDirectory($work) | Out-Null
    @{ format = 1
        owner = 'CyberDesktopSDK starter verification'
    } | ConvertTo-Json | Set-Content "$work/starter-workspace.json" -Encoding utf8
    $cli = Join-Path $sdk 'bin/cd-sdk.exe'
    $preview = Join-Path $sdk 'bin/cd-sdk-preview.exe'
    $fixed = @('--size', '320x200', '--time', '1709208000000')
    $mock = Join-Path $work 'mock.json'
    [IO.File]::WriteAllText($mock, '{"cpuPercent":62}')
    $results = @()
    function Preview([string]$Package, [string]$Output, [string[]]$Options) {
        & $preview $Package @fixed @Options --screenshot $Output *> "$Output.log"
        if ($LASTEXITCODE) {
            throw "Starter preview failed: $Output"
        }
    }
    function Equal-Pixels([string]$First, [string]$Second, [string]$Diff, [bool]$Expected) {
        & $cli compare $First $Second $Diff 0 *> "$Diff.log"
        if (($LASTEXITCODE -eq 0) -ne $Expected) {
            throw "Unexpected starter pixel comparison: $First / $Second"
        }
    }
    foreach ($preset in @('static', 'data', 'animated', 'info', 'tasks', 'media', 'calendar')) {
        $project = Join-Path $work $preset
        $id = "com.example.starter.$preset"
        $generated = [IO.Path]::GetFullPath((Join-Path $work "generated-$preset"))
        if (!$generated.StartsWith($work + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Generated fixture escaped verification workspace'
        }
        if (Test-Path -LiteralPath $generated) {
            throw "Unfinished generated fixture remains: $generated"
        }
        & "$sdk/share/cyberdesktop-sdk/scripts/new-plugin.ps1" -Destination $generated -Id $id -Name "Canvas $preset 示例" -Template $preset -SdkDirectory $sdk *> "$work/$preset-create.log"
        try {
            New-Item -ItemType Directory -Force -Path $project | Out-Null
            foreach ($file in Get-ChildItem -LiteralPath $generated -File -Recurse) {
                $destination = Join-Path $project ([IO.Path]::GetRelativePath($generated, $file.FullName))
                New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
                Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
            }
        }
        finally {
            foreach ($file in Get-ChildItem -LiteralPath $generated -File -Recurse) {
                Remove-Item -LiteralPath $file.FullName -Force
            }
            foreach ($relative in @('templates/components', 'templates')) {
                $directory = Join-Path $generated $relative
                if ((Test-Path -LiteralPath $directory) -and !(Get-ChildItem -LiteralPath $directory -Force)) {
                    Remove-Item -LiteralPath $directory
                }
            }
            Remove-Item -LiteralPath $generated
        }
        & cmake -S $project -B "$project/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DCMAKE_PREFIX_PATH=$sdk" *> "$work/$preset-build.log"
        if ($LASTEXITCODE) {
            throw "$preset configure failed"
        }
        & cmake --build "$project/build" --parallel $Jobs *>> "$work/$preset-build.log"
        if ($LASTEXITCODE) {
            throw "$preset build failed"
        }
        $package = "$project/build/plugins/$id"
        & $cli test $package *> "$work/$preset-test.log"
        if ($LASTEXITCODE) {
            throw "$preset lifecycle failed"
        }
        foreach ($theme in @('light', 'dark')) {
            foreach ($dpi in @(1, 2, 3)) {
                Preview $package "$work/$preset-$theme-$dpi.png" @('--theme', $theme, '--dpr', "$dpi", '--mock', $mock, '--frames', '2')
            }
        }
        foreach ($notice in @('LICENSE', 'licenses/yoga-LICENSE.txt', 'licenses/tweeny-LICENSE.txt')) {
            if (!(Test-Path -LiteralPath "$package/$notice" -PathType Leaf)) {
                throw "Missing $preset notice: $notice"
            }
        }
        if ($preset -eq 'static') {
            Preview $package "$work/static-idle.png" @('--frames', '30')
            Preview $package "$work/static-initial.png" @('--frames', '1')
            Equal-Pixels "$work/static-idle.png" "$work/static-initial.png" "$work/static-diff.png" $true
            $click = Join-Path $work 'click.json'
            [IO.File]::WriteAllText($click, '{"events":[{"frame":0,"kind":1,"x":100,"y":70},{"frame":1,"kind":2,"x":100,"y":70}]}')
            $state = Join-Path $work 'static-state.pb'
            Preview $package "$work/static-click.png" @('--frames', '3', '--replay', $click, '--state', $state)
            $saved = & $cli state $state | ConvertFrom-Json
            if ($LASTEXITCODE -or [Convert]::FromBase64String($saved.nativeState)[4] -ne 1) {
                throw 'Click did not save counter 1'
            }
            Preview $package "$work/static-restored.png" @('--state', $state)
            Equal-Pixels "$work/static-click.png" "$work/static-restored.png" "$work/static-state-diff.png" $true
            Equal-Pixels "$work/static-initial.png" "$work/static-click.png" "$work/static-click-diff.png" $false
        }
        if ($preset -eq 'animated') {
            Preview $package "$work/animated-first.png" @('--frames', '1')
            Preview $package "$work/animated-moving.png" @('--frames', '30')
            Equal-Pixels "$work/animated-first.png" "$work/animated-moving.png" "$work/animated-motion-diff.png" $false
            $state = Join-Path $work 'animated-state.pb'
            Preview $package "$work/animated-paused.png" @('--frames', '3', '--replay', "$work/click.json", '--state', $state)
            Preview $package "$work/animated-restored.png" @('--frames', '30', '--state', $state)
            Equal-Pixels "$work/animated-paused.png" "$work/animated-restored.png" "$work/animated-pause-diff.png" $true
        }
        if ($preset -in @('tasks', 'calendar')) {
            $point = switch ($preset) {
                'tasks' {
                    @(60, 70)
                }
                'calendar' {
                    @(280, 30)
                }
            }
            $replay = "$work/$preset-click.json"
            @{events = @(@{frame = 0
                        kind = 1
                        buttons = 1
                        x = $point[0]
                        y = $point[1]
                    }, @{frame = 1
                        kind = 2
                        buttons = 0
                        x = $point[0]
                        y = $point[1]
                    })
            } |
                ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $replay -Encoding utf8
            $state = "$work/$preset-state.pb"
            Preview $package "$work/$preset-initial.png" @('--frames', '1')
            Preview $package "$work/$preset-click.png" @('--frames', '3', '--replay', $replay, '--state', $state)
            $saved = & $cli state $state | ConvertFrom-Json
            $bytes = [Convert]::FromBase64String($saved.nativeState)
            $valid = switch ($preset) {
                'tasks' {
                    $bytes[4] -eq 1
                }
                'calendar' {
                    ($bytes[8] + 256 * $bytes[9]) -eq 1201
                }
            }
            if (!$valid) {
                throw "$preset action did not update its business state"
            }
            Preview $package "$work/$preset-restored.png" @('--state', $state)
            $restored = & $cli state $state | ConvertFrom-Json
            if ($restored.nativeState -ne $saved.nativeState) {
                throw "$preset state changed across preview restart"
            }
            Equal-Pixels "$work/$preset-initial.png" "$work/$preset-click.png" "$work/$preset-click-diff.png" $false
        }
        if ($preset -in @('info', 'media')) {
            $state = "$work/$preset-live-state.pb"
            Preview $package "$work/$preset-live.png" @('--frames', '3', '--mock', $mock, '--state', $state)
            $saved = & $cli state $state | ConvertFrom-Json
            Preview $package "$work/$preset-restored.png" @('--frames', '3', '--mock', $mock, '--state', $state)
            $restored = & $cli state $state | ConvertFrom-Json
            if ($saved.nativeState -ne $restored.nativeState) {
                throw "$preset must not persist live telemetry/playback state"
            }
            $variant = "$work/$preset-variant.json"
            $payload = if ($preset -eq 'info') {
                '{"cpuPercent":12}'
            }
            else {
                '{"hasMediaSession":false}'
            }
            [IO.File]::WriteAllText($variant, $payload)
            Preview $package "$work/$preset-variant.png" @('--frames', '3', '--mock', $variant)
            Equal-Pixels "$work/$preset-live.png" "$work/$preset-variant.png" "$work/$preset-live-diff.png" $false
        }
        & $cli pack $package "$work/$preset.cdpkg" *> "$work/$preset-pack.log"
        if ($LASTEXITCODE) {
            throw "$preset pack failed"
        }
        & $cli validate "$work/$preset.cdpkg" *> "$work/$preset-validate.log"
        if ($LASTEXITCODE) {
            throw "$preset validate failed"
        }
        $results += @{preset = $preset
            id = $id
            directory = $package
            package = "$work/$preset.cdpkg"
            captures = 6
            lifecycle = $true
            notices = $true
        }
    }
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$work/verification.json" -Encoding utf8
    Write-Output 'PASS: seven independent native starters, 42 DPI/theme captures, live service views, component interactions, state restart and animation pause/restart.'
}
finally {
    $verificationProcess.ProcessorAffinity = $verificationAffinity
}
