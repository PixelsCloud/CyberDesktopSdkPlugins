param([string]$VcpkgRoot = $env:VCPKG_ROOT, [string]$InstalledDirectory,
    [string]$WorkDirectory = "$PWD/build/relwithdebinfo/Testing/sdk-release",
    [ValidateRange(1, 16)][int]$Jobs = 16, [switch]$Archive)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path "$PSScriptRoot/..").Path
$metadata = Get-Content "$source/release.json" -Raw | ConvertFrom-Json
$sdkVersion = $metadata.sdk
if ($sdkVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Invalid SDK release version'
}
$canvasSource = Join-Path $source '../libraries/cybercanvas'
if (!(Test-Path -LiteralPath "$canvasSource/CMakeLists.txt")) {
    $canvasSource = Join-Path $source 'vendor/cybercanvas'
}
$canvasProject = Get-Content "$canvasSource/CMakeLists.txt" -Raw
if ($canvasProject -notmatch 'project\(CyberCanvas VERSION ([0-9.]+)' -or $Matches[1] -ne $metadata.cyberCanvas) {
    throw 'CyberCanvas project and release.json versions differ'
}
if (@(Get-CimInstance Win32_Process -Filter "Name='cmake.exe'" | Where-Object CommandLine -Match '\s--build\s').Count) {
    throw 'Wait for other builds before starting the release; maximum combined parallelism is 16.'
}
$releaseProcess = [Diagnostics.Process]::GetCurrentProcess()
$releaseAffinity = $releaseProcess.ProcessorAffinity
[long]$mask = 0
$count = 0
for ($index = 0; $index -lt 63 -and $count -lt $Jobs; $index++) {
    [long]$bit = [long]1 -shl $index
    if ($releaseAffinity.ToInt64() -band $bit) {
        $mask = $mask -bor $bit
        $count++
    }
}
if (!$mask) {
    throw 'No build CPU available'
}
try {
    $releaseProcess.ProcessorAffinity = [IntPtr]$mask
    $work = [IO.Path]::GetFullPath($WorkDirectory)
    if ((Test-Path -LiteralPath $work) -and @(Get-ChildItem -LiteralPath $work -Force).Count -and
        !(Test-Path -LiteralPath "$work/sdk-release-workspace.json")) {
        throw 'Existing release directory is not a generated SDK workspace'
    }
    for ($ancestor = $work; $ancestor; $ancestor = Split-Path $ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Release directory must not traverse a reparse point'
        }
    }
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    @{ format = 1
        owner = 'CyberDesktopSDK release tool'
    } | ConvertTo-Json | Set-Content "$work/sdk-release-workspace.json" -Encoding utf8
    $build = "$work/build"
    $kit = "$work/windows-x64"
    $artifacts = "$work/artifacts"
    New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
    & "$PSScriptRoot/build-sdk.ps1" -VcpkgRoot $VcpkgRoot -InstalledDirectory $InstalledDirectory -BuildDirectory $build -InstallDirectory $kit -Jobs $Jobs *> "$work/build.log"
    if ($LASTEXITCODE) {
        Get-Content "$work/build.log" -Tail 20
        throw 'Release build failed'
    }
    $toolVersion = & "$kit/bin/cd-sdk.exe" --version
    if ($LASTEXITCODE -or "$toolVersion".Trim() -ne $sdkVersion) {
        throw 'Installed CLI and release.json versions differ'
    }
    & ctest --test-dir $build --output-on-failure *> "$work/tests.log"
    if ($LASTEXITCODE) {
        Get-Content "$work/tests.log"
        throw 'SDK contract failed'
    }
    # Exercise the same public creation/build/preview/package path used by developers.
    & "$PSScriptRoot/verify-starters.ps1" -SdkDirectory $kit -WorkDirectory "$work/starters" -Jobs $Jobs *> "$work/starters.log"
    $starters = @(Get-Content "$work/starters/verification.json" -Raw | ConvertFrom-Json)
    if ($starters.Count -ne 7) {
        throw 'Release must contain all seven verified starter presets'
    }
    foreach ($starter in $starters) {
        Copy-Item -LiteralPath $starter.package -Destination "$artifacts/starter-$($starter.preset).cdpkg"
    }
    # Build the installed examples without dependency prefixes or application sources.
    & cmake -S "$kit/share/cyberdesktop-sdk/examples" -B "$work/example-build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$kit" *> "$work/examples.log"
    if ($LASTEXITCODE) {
        throw 'Installed examples configure failed'
    }
    & cmake --build "$work/example-build" --parallel $Jobs *>> "$work/examples.log"
    if ($LASTEXITCODE) {
        Get-Content "$work/examples.log" -Tail 20
        throw 'Installed examples build failed'
    }
    $examples = @()
    $exampleNames = @('hello', 'spectrum', 'hexclock', 'input-stats', 'data-dashboard', 'media-spectrum', 'workbench', 'physics-sandbox', 'rendering-gallery')
    foreach ($name in $exampleNames) {
        $directory = if ($name -in 'workbench', 'physics-sandbox', 'rendering-gallery') {
            "$work/example-build/plugins/org.cyberdesktop.sdk.$name"
        }
        else {
            "$work/example-build/$name"
        }
        & "$kit/bin/cd-sdk.exe" test $directory *> "$work/$name-test.log"
        if ($LASTEXITCODE) {
            throw "$name lifecycle failed"
        }
        & "$kit/bin/cd-sdk-preview.exe" $directory --dpr 2 --screenshot "$work/$name-preview.png" *> "$work/$name-preview.log"
        if ($LASTEXITCODE) {
            throw "$name footprint preview failed"
        }
        # Stage only distributable files, never linker/debug outputs.
        $stage = "$work/package-$name"
        [IO.Directory]::CreateDirectory($stage) | Out-Null
        foreach ($file in Get-ChildItem -LiteralPath $directory -Recurse -File) {
            if ($file.Extension -in '.pdb', '.ilk', '.obj', '.lib', '.exp') {
                continue
            }
            $relative = [IO.Path]::GetRelativePath($directory, $file.FullName)
            $target = Join-Path $stage $relative
            [IO.Directory]::CreateDirectory((Split-Path $target)) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $target
        }
        [IO.Directory]::CreateDirectory("$stage/licenses") | Out-Null
        Copy-Item -LiteralPath "$source/LICENSE" -Destination "$stage/LICENSE"
        $notices = @('yoga', 'tweeny')
        if ($name -eq 'physics-sandbox') {
            $notices += 'box2d'
        }
        if ($name -eq 'rendering-gallery') {
            $notices += @('thorvg', 'nlohmann-json')
        }
        foreach ($dependency in $notices) {
            Copy-Item -LiteralPath "$kit/share/cybercanvas/third_party/$dependency/LICENSE" -Destination "$stage/licenses/$dependency-LICENSE.txt"
        }
        $packet = "$artifacts/$name.cdpkg"
        & "$kit/bin/cd-sdk.exe" pack $stage $packet *> "$work/$name-pack.log"
        if ($LASTEXITCODE) {
            throw "$name pack failed"
        }
        & "$kit/bin/cd-sdk.exe" validate $packet *> "$work/$name-validate.log"
        if ($LASTEXITCODE) {
            throw "$name validate failed"
        }
        $examples += @{name = $name
            nativeLifecycle = $true
            footprintPreview = $true
            protobufPackage = $true
            notices = $true
        }
    }
    $nativeFiles = @(Get-ChildItem "$kit/bin" -File | Where-Object Extension -in '.exe', '.dll') + @(Get-ChildItem "$work/example-build" -Recurse -Filter plugin.dll -File)
    foreach ($starter in $starters) {
        $nativeFiles += Get-Item -LiteralPath "$($starter.directory)/plugin.dll"
    }
    $importReport = @()
    foreach ($file in $nativeFiles) {
        $imports = & dumpbin.exe /nologo /dependents $file.FullName
        if ($LASTEXITCODE -or ($imports -join "`n") -match '(?im)^\s*(msvcp\d[^\s]*|vcruntime\d[^\s]*|libprotobuf[^\s]*|absl[^\s]*|Qt[56][^\s]*|ucrtbase|api-ms-win-crt[^\s]*)\.dll\s*$') {
            throw "Static-runtime/dependency check failed: $($file.Name)"
        }
        $importReport += @{file = [IO.Path]::GetRelativePath($work, $file.FullName).Replace('\', '/')
            imports = @($imports | Where-Object { $_ -match '(?i)\.dll\s*$' } | ForEach-Object { $_.Trim() })
        }
    }
    function Verify-Core([string]$Directory, [string]$Label) {
        $names = @('tests', 'advanced_tests', 'animation_tests', 'interaction_tests', 'widget_tests', 'flex_tests', 'component_tests', 'scene_tests', 'list_tests', 'image_tests', 'data_widget_tests', 'diagnostic_tests', 'theme_tests', 'motion_tests', 'scene_transform_tests', 'physics_tests', 'physics_scene_tests', 'effects_tests', 'text_tests', 'animated_images_tests', 'vector_tests', 'particles_tests', 'charts_tests', 'noise_tests')
        $targets = @($names | ForEach-Object { "cybercanvas_$_" }) + @('cybercanvas_benchmark')
        & cmake --build $Directory --target @targets --parallel $Jobs *> "$work/$Label-core-build.log"
        if ($LASTEXITCODE) {
            Get-Content "$work/$Label-core-build.log" -Tail 20
            throw 'Canvas verification build failed'
        }
        $core = if (Test-Path "$Directory/cybercanvas/cybercanvas_tests.exe") {
            "$Directory/cybercanvas"
        }
        else {
            "$Directory/vendor/cybercanvas"
        }
        Push-Location $Directory
        try {
            foreach ($name in $names) {
                & "$core/cybercanvas_$name.exe" *> "$work/$Label-core-$name.log"
                if ($LASTEXITCODE) {
                    throw "Canvas $name failed"
                }
            }
            & "$core/cybercanvas_benchmark.exe" "$artifacts/$Label-benchmark.json" --enforce *> "$work/$Label-benchmark.log"
            if ($LASTEXITCODE) {
                throw 'Canvas structural/performance budget failed'
            }
        }
        finally {
            Pop-Location
        }
    }
    Verify-Core $build 'installed-build'
    Copy-Item "$source/LICENSE" "$kit/LICENSE"
    @("CyberDesktop SDK $sdkVersion - Windows x64 /MT", '', 'Start with share/cyberdesktop-sdk/docs/QUICKSTART.zh-CN.md or share/cyberdesktop-sdk/README.md.', 'The bin tools, include headers and lib/cmake package need no Qt/protobuf install.', 'This developer kit does not contain the application or Studio.', 'SDK code is MIT. Third-party licenses are in share/cyberdesktop-sdk/third_party.') | Set-Content "$kit/README.txt" -Encoding utf8
    function Inventory([string]$Root) {
        $absolute = (Resolve-Path $Root).Path
        @(Get-ChildItem -LiteralPath $absolute -Recurse -File | Sort-Object FullName | ForEach-Object {
                @{path = $_.FullName.Substring($absolute.Length + 1).Replace('\', '/')
                    size = $_.Length
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            })
    }
    Inventory $kit | ConvertTo-Json -Depth 4 | Set-Content "$kit/inventory.json" -Encoding utf8
    $export = "$work/source"
    & "$PSScriptRoot/export-public-source.ps1" -OutputDirectory $export -SdkOnly
    $publicFiles = @(Get-Content "$export/public-source-files.json" -Raw | ConvertFrom-Json).files
    $sourceCheck = $export
    if ($Archive) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        # Explicit archival replaces only fixed artifacts owned by this workspace.
        foreach ($pair in @(@($export, "$artifacts/CyberDesktopSDK-source.zip"),
                @($kit, "$artifacts/CyberDesktopSDK-windows-x64.zip"))) {
            $archivePath = [IO.Path]::GetFullPath($pair[1])
            if (!$archivePath.StartsWith($work + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Archive escaped release workspace'
            }
            if (Test-Path -LiteralPath $archivePath) {
                Remove-Item -LiteralPath $archivePath -Force
            }
            [IO.Compression.ZipFile]::CreateFromDirectory($pair[0], $archivePath)
        }
        $sourceCheck = "$work/source-check"
        & "$PSScriptRoot/export-public-source.ps1" -OutputDirectory $sourceCheck -SdkOnly
        [IO.Compression.ZipFile]::ExtractToDirectory("$artifacts/CyberDesktopSDK-source.zip", $sourceCheck, $true)
    }
    # Build the manifest-selected source tree, including the public shared libraries.
    & "$sourceCheck/sdk/scripts/build-sdk.ps1" -VcpkgRoot $VcpkgRoot -InstalledDirectory $InstalledDirectory -BuildDirectory "$work/source-build" -Jobs $Jobs *> "$work/source-build.log"
    if ($LASTEXITCODE) {
        Get-Content "$work/source-build.log" -Tail 20
        throw 'Exported source archive build failed'
    }
    & ctest --test-dir "$work/source-build" --output-on-failure *> "$work/source-tests.log"
    if ($LASTEXITCODE) {
        Get-Content "$work/source-tests.log"
        throw 'Exported source contract failed'
    }
    foreach ($name in $exampleNames) {
        $directory = if ($name -in 'workbench', 'physics-sandbox', 'rendering-gallery') {
            "$work/source-build/plugins/org.cyberdesktop.sdk.$name"
        }
        else {
            "$work/source-build/examples/$name"
        }
        & "$work/source-build/cd-sdk.exe" test $directory *> "$work/source-$name-test.log"
        if ($LASTEXITCODE) {
            throw "Exported source $name lifecycle failed"
        }
    }
    Verify-Core "$work/source-build" 'exported-source'
    $starterReport = @($starters | ForEach-Object { @{preset = $_.preset
                id = $_.id
                directory = [IO.Path]::GetRelativePath($work, $_.directory).Replace('\', '/')
                package = [IO.Path]::GetRelativePath($work, $_.package).Replace('\', '/')
                captures = $_.captures
                lifecycle = $_.lifecycle
                notices = $_.notices
            } })
    @{version = $metadata
        toolVersion = "$toolVersion".Trim()
        contract = $true
        independentStarters = $starterReport
        installedExamples = $examples
        pluginPackages = ($starters.Count + $examples.Count)
        maximumBuildJobs = $Jobs
        canvasContractSuites = 24
        canvasBenchmarksEnforced = $true
        nativeImports = $importReport
        staticRuntimeImports = $true
        sourceFiles = $publicFiles.Count
        sourceInventory = 'sdk/public-source.json'
        archivesGenerated = [bool]$Archive
        exportedSourceBuild = $true
        exportedSourceContract = $true
        ciHostedRun = 'not run by local release'
        createdUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json -Depth 8 | Set-Content "$artifacts/release-report.json" -Encoding utf8
    Get-ChildItem $artifacts -File | Sort-Object Name | ForEach-Object { "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)" } | Set-Content "$artifacts/SHA256SUMS.txt" -Encoding ascii
    Write-Output "SDK release artifacts: $artifacts"
}
finally {
    $releaseProcess.ProcessorAffinity = $releaseAffinity
}
