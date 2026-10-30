#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InstalledDirectory,
    [string]$SdkDirectory,
    [string]$PluginFamilies = 'ALL',
    [ValidateSet('Release', 'RelWithDebInfo')][string]$Configuration = 'Release',
    [string]$WorkDirectory = "$PWD/build/public-plugins",
    [ValidateRange(1, 16)][int]$Jobs = 16
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work = [IO.Path]::GetFullPath($WorkDirectory)
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
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$environment = & cmd.exe /d /c "call `"$vsRoot/VC/Auxiliary/Build/vcvars64.bat`" >nul && set"
if ($LASTEXITCODE) {
    throw 'MSVC environment initialization failed'
}
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
$env:VSLANG = '1033'
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    $process.ProcessorAffinity = [IntPtr]$mask
    $mode = if ($SdkDirectory) {
        'INSTALLED'
    }
    else {
        'SOURCE'
    }
    $prefix = if ($SdkDirectory) {
        "$SdkDirectory;$InstalledDirectory"
    }
    else {
        $InstalledDirectory
    }
    & cmake -S $root -B $work -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON "-DCMAKE_PREFIX_PATH=$prefix" "-DCD_PLUGINS_SDK_MODE=$mode" "-DCD_PLUGIN_FAMILIES=$PluginFamilies" '-DCD_PLUGIN_IDS=' *> "$work/configure.log"
    if ($LASTEXITCODE) {
        throw "Public plugins configure failed: $work/configure.log"
    }
    & cmake --build $work --target cd_public_plugin_packages --parallel $Jobs *> "$work/build.log"
    if ($LASTEXITCODE) {
        throw "Public plugins build failed: $work/build.log"
    }
    & cmake --install $work --prefix "$work/distribution" --component PublicPlugins *> "$work/install.log"
    if ($LASTEXITCODE) {
        throw "Public plugins install failed: $work/install.log"
    }
    $expected = @(Get-Content "$work/public-plugin-ids.txt" | Where-Object { $_ })
    $manifest = Get-Content "$work/public-distribution.json" -Raw | ConvertFrom-Json
    $delivered = @($manifest.plugins | ForEach-Object id)
    if (!$expected.Count -or @(Compare-Object ($expected | Sort-Object) ($delivered | Sort-Object)).Count) {
        throw 'Expected and installed plugin sets differ'
    }
    @{ passed = $true
        plugins = $expected.Count
        noPrivateSources = $true
        pch = $false
        crt = 'MT'
    } |
        ConvertTo-Json | Set-Content "$work/verification.json" -Encoding utf8
    Write-Output "Public plugins: $($expected.Count) built and installed without private application sources."
}
finally {
    $process.ProcessorAffinity = $originalAffinity
}
