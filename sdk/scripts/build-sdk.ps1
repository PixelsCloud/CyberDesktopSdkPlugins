param([string]$VcpkgRoot = $env:VCPKG_ROOT, [string]$InstalledDirectory,
    [string]$BuildDirectory = "$PWD/build/sdk-native",
    [string]$InstallDirectory, [ValidateRange(1, 16)][int]$Jobs = 16,
    [switch]$LibraryOnly, [switch]$SkipTests, [switch]$SkipExamples,
    [switch]$DisablePrecompiledHeaders,
    [switch]$FullPreview, [string]$QtDirectory, [string]$CubismRoot)
$ErrorActionPreference = 'Stop'
if (!$VcpkgRoot) {
    $v = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
    if ($v) {
        $VcpkgRoot = Split-Path $v.Source
    }
}
if (!$VcpkgRoot -and !$InstalledDirectory) {
    throw 'Set VCPKG_ROOT or pass -VcpkgRoot / -InstalledDirectory.'
}
if (!$InstalledDirectory) {
    foreach ($triplet in @('x64-windows-static-release', 'x64-windows-static')) {
        $candidate = Join-Path $VcpkgRoot "installed/$triplet"
        if (Test-Path "$candidate/share/protobuf/protobuf-config.cmake") {
            $InstalledDirectory = $candidate
            break
        }
    }
}
if (!$InstalledDirectory -or !(Test-Path "$InstalledDirectory/share/protobuf/protobuf-config.cmake")) {
    throw 'Install protobuf with a static /MT vcpkg triplet first.'
}
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vsRoot 'VC/Auxiliary/Build/vcvars64.bat'
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64' -or !(Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue)) {
    $environment = & cmd.exe /d /c "call `"$vcvars`" >nul && set"
    if ($LASTEXITCODE) {
        throw 'MSVC environment initialization failed'
    }
    foreach ($line in $environment) {
        $split = $line.IndexOf('=')
        if ($split -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0, $split), $line.Substring($split + 1), 'Process')
        }
    }
}
$env:VSLANG = '1033'
$sdkRoot = (Resolve-Path "$PSScriptRoot/..").Path
$toolsValue = if ($LibraryOnly) {
    'OFF'
}
else {
    'ON'
}
$examplesValue = if ($LibraryOnly -or $SkipExamples) {
    'OFF'
}
else {
    'ON'
}
$testsValue = if ($LibraryOnly -or $SkipTests) {
    'OFF'
}
else {
    'ON'
}
$pchValue = if ($DisablePrecompiledHeaders) {
    'ON'
}
else {
    'OFF'
}
$process = [Diagnostics.Process]::GetCurrentProcess()
$originalAffinity = $process.ProcessorAffinity
[long]$mask = 0
$count = 0
for ($index = 0; $index -lt 63 -and $count -lt $Jobs; $index++) {
    [long]$bit = [long]1 -shl $index
    if ($originalAffinity.ToInt64() -band $bit) {
        $mask = $mask -bor $bit
        $count++
    }
}
if (!$mask) {
    throw 'No build CPU available'
}
$prefix = @($InstalledDirectory)
$previewArguments = @('-DCD_SDK_PREVIEW_LIVE2D=OFF')
if ($FullPreview) {
    if ($LibraryOnly -or !$QtDirectory -or !$CubismRoot) {
        throw 'FullPreview requires QtDirectory and CubismRoot and cannot be combined with LibraryOnly.'
    }
    $prefix += $QtDirectory
    $previewArguments = @('-DCD_SDK_PREVIEW_LIVE2D=ON', "-DCD_CUBISM_ROOT=$CubismRoot")
}
try {
    $process.ProcessorAffinity = [IntPtr]$mask
    & cmake -S $sdkRoot -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$($prefix -join ';')" @previewArguments "-DCD_SDK_BUILD_TOOLS=$toolsValue" "-DCD_SDK_BUILD_EXAMPLES=$examplesValue" "-DCD_SDK_BUILD_TESTS=$testsValue" "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=$pchValue"
    if ($LASTEXITCODE) {
        throw 'SDK configure failed'
    }
    & cmake --build $BuildDirectory --parallel $Jobs *> "$BuildDirectory/build.log"
    if ($LASTEXITCODE) {
        Get-Content "$BuildDirectory/build.log" | Where-Object { $_ -match 'error|FAILED:|LNK' -and $_ -notmatch '包含文件' } | Select-Object -First 20
        throw 'SDK build failed'
    }
    Write-Output 'SDK native /MT build passed.'
    if ($InstallDirectory) {
        & cmake --install $BuildDirectory --prefix $InstallDirectory
        if ($LASTEXITCODE) {
            throw 'SDK install failed'
        }
    }
}
finally {
    $process.ProcessorAffinity = $originalAffinity
}
