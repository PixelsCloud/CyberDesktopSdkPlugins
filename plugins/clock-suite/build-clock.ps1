#requires -Version 7.0
# Public developer entry: builds and installs this family into its distribution.
# Installing into a running Desktop is deliberately handled by the host.
[CmdletBinding()]
param(
    [string]$SdkRoot,
    [string]$InstalledDirectory = $env:CYBERDESKTOP_STATIC_PREFIX,
    [string]$BuildDirectory = "$PSScriptRoot/../../build/public-plugins-clock-suite",
    [ValidateRange(1, 16)][int]$Jobs = 16
)
$ErrorActionPreference = 'Stop'
if (!$InstalledDirectory -and $env:VCPKG_ROOT) {
    $InstalledDirectory = Join-Path $env:VCPKG_ROOT 'installed/x64-windows-static-release'
}
if (!$InstalledDirectory) {
    throw 'Pass -InstalledDirectory for the vcpkg static /MT dependency prefix, or set CYBERDESKTOP_STATIC_PREFIX.'
}
& "$PSScriptRoot/../../sdk/scripts/verify-public-plugins.ps1" -InstalledDirectory $InstalledDirectory -SdkDirectory $SdkRoot -PluginFamilies 'clock-suite' -WorkDirectory $BuildDirectory -Jobs $Jobs
