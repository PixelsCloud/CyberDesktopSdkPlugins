#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Destination,
    [Parameter(Mandatory)][ValidatePattern('^[a-z][a-z0-9_-]*(\.[a-z][a-z0-9_-]*)+$')][string]$Id,
    [Parameter(Mandatory)][string]$Name,
    [ValidateSet('static', 'data', 'animated', 'info', 'tasks', 'media', 'calendar')][string]$Template = 'static',
    [string]$SdkDirectory
)
$ErrorActionPreference = 'Stop'
$templateRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../templates'))
$componentKinds = @{ info = 0
    tasks = 1
    media = 2
    calendar = 3
}
$component = $componentKinds.ContainsKey($Template)
$folder = if ($component) {
    'components'
}
elseif ($Template -eq 'static') {
    'minimal'
}
else {
    $Template
}
$source = Join-Path $templateRoot $folder
if ([string]::IsNullOrWhiteSpace($Name) -or [Text.Encoding]::UTF8.GetByteCount($Name) -gt 512) {
    throw 'Name must be nonempty and no more than 512 UTF-8 bytes.'
}
if ($Id.Length -gt 128) {
    throw 'Id must be no more than 128 characters.'
}
if (!$SdkDirectory) {
    $SdkDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
}
$SdkDirectory = [IO.Path]::GetFullPath($SdkDirectory)
$tool = Join-Path $SdkDirectory 'bin/cd-sdk.exe'
if (!(Test-Path -LiteralPath $tool -PathType Leaf)) {
    throw 'Pass -SdkDirectory pointing to an installed developer kit with bin/cd-sdk.exe.'
}
$files = @('CMakeLists.txt', 'Plugin.cpp', 'plugin.textproto', 'README.md', 'LICENSE')
if ($component) {
    $files += @('templates/components/Card.h', 'templates/components/Card.cpp',
        'templates/components/ServiceModel.h', 'templates/components/ServiceModel.cpp')
}
foreach ($file in $files) {
    if (!(Test-Path -LiteralPath (Join-Path $source (Split-Path $file -Leaf)) -PathType Leaf)) {
        throw "Template file is missing: $file"
    }
}
$target = [IO.Path]::GetFullPath($Destination)
if ((Test-Path -LiteralPath $target) -and
    (!(Test-Path -LiteralPath $target -PathType Container) -or
    @(Get-ChildItem -LiteralPath $target -Force).Count)) {
    throw 'Destination must be missing or an empty directory; existing files are never overwritten.'
}
$parent = Split-Path $target -Parent
[IO.Directory]::CreateDirectory($parent) | Out-Null
$stage = Join-Path $parent ('.cdsdk-create-' + (Split-Path $target -Leaf))
if (Test-Path -LiteralPath $stage) {
    throw "Another creation or unfinished staging directory exists: $stage"
}
[IO.Directory]::CreateDirectory($stage) | Out-Null
$createdTarget = $false
$copied = [Collections.Generic.List[string]]::new()
$createdStageFiles = [Collections.Generic.List[string]]::new()
try {
    $original = Get-Content -LiteralPath (Join-Path $source 'plugin.textproto') -Raw
    $match = [regex]::Match($original, '(?m)^id:\s*"([^"]+)"')
    if (!$match.Success) {
        throw 'Template does not declare an ID.'
    }
    $oldId = $match.Groups[1].Value
    $quotedName = ConvertTo-Json -InputObject $Name -Compress
    foreach ($file in $files) {
        $contents = Get-Content -LiteralPath (Join-Path $source (Split-Path $file -Leaf)) -Raw
        $contents = $contents.Replace($oldId, $Id)
        if ($component) {
            $contents = $contents.Replace('CD_COMPONENT_KIND=0', "CD_COMPONENT_KIND=$($componentKinds[$Template])")
            if ($file -eq 'CMakeLists.txt') {
                $contents = $contents.Replace('SOURCES Plugin.cpp Card.cpp ServiceModel.cpp',
                    'SOURCES Plugin.cpp templates/components/Card.cpp templates/components/ServiceModel.cpp')
            }
        }
        if ($file -eq 'plugin.textproto') {
            if ($component) {
                $contents = [regex]::Replace($contents, '(?m)^(capabilities: SYSTEM_INFO|required_services \{ id: "system.telemetry" version: 1 \})\r?\n', '')
            }
            if ($preset = @{info = 'SYSTEM_INFO'
                    media = 'MEDIA_CONTROL'
                }[$Template]) {
                $service = @{info = 'system.telemetry'
                    media = 'media.playback'
                }[$Template]
                $contents += "`ncapabilities: $preset`nrequired_services { id: `"$service`" version: 1 }`n"
            }
            $contents = [regex]::Replace($contents, '(?m)^name:\s*"[^\r\n]*"',
                [Text.RegularExpressions.MatchEvaluator] { param($entry) 'name: ' + $quotedName })
        }
        $stageFile = Join-Path $stage $file
        [IO.Directory]::CreateDirectory((Split-Path $stageFile)) | Out-Null
        $createdStageFiles.Add($stageFile)
        Set-Content -LiteralPath $stageFile -Value $contents -Encoding utf8 -NoNewline
    }
    $descriptor = Join-Path $stage 'descriptor.pb'
    $createdStageFiles.Add($descriptor)
    & $tool descriptor (Join-Path $stage 'plugin.textproto') $descriptor
    if ($LASTEXITCODE) {
        throw 'Generated descriptor failed SDK validation.'
    }
    if (!(Test-Path -LiteralPath $target)) {
        [IO.Directory]::CreateDirectory($target) | Out-Null
        $createdTarget = $true
    }
    if (@(Get-ChildItem -LiteralPath $target -Force).Count) {
        throw 'Destination changed during creation.'
    }
    foreach ($file in $files) {
        $output = Join-Path $target $file
        [IO.Directory]::CreateDirectory((Split-Path $output)) | Out-Null
        [IO.File]::Copy((Join-Path $stage $file), $output, $false)
        $copied.Add($output)
    }
}
catch {
    foreach ($path in $copied) {
        Remove-Item -LiteralPath $path -Force
    }
    foreach ($relative in @('templates/components', 'templates')) {
        $directory = Join-Path $target $relative
        if ((Test-Path -LiteralPath $directory) -and !(Get-ChildItem -LiteralPath $directory -Force)) {
            Remove-Item -LiteralPath $directory
        }
    }
    if ($createdTarget -and !(Get-ChildItem -LiteralPath $target -Force)) {
        Remove-Item -LiteralPath $target -Force
    }
    throw
}
finally {
    # Remove only the known files created by this invocation, never recurse.
    foreach ($path in $createdStageFiles) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
        }
    }
    foreach ($relative in @('templates/components', 'templates')) {
        $directory = Join-Path $stage $relative
        if ((Test-Path -LiteralPath $directory) -and !(Get-ChildItem -LiteralPath $directory -Force)) {
            Remove-Item -LiteralPath $directory
        }
    }
    if ((Test-Path -LiteralPath $stage) -and !(Get-ChildItem -LiteralPath $stage -Force)) {
        Remove-Item -LiteralPath $stage -Force
    }
}
function Quote-Literal([string]$Text) {
    "'" + $Text.Replace("'", "''") + "'"
}
$build = Join-Path $target 'build'
$package = Join-Path $build "plugins/$Id"
Write-Output "Created $Template plugin: $target"
Write-Output ('cmake -S ' + (Quote-Literal $target) + ' -B ' + (Quote-Literal $build) +
    ' -G Ninja -DCMAKE_BUILD_TYPE=Release ' + (Quote-Literal "-DCMAKE_PREFIX_PATH=$SdkDirectory"))
Write-Output ('cmake --build ' + (Quote-Literal $build) + ' --parallel 16')
Write-Output ('& ' + (Quote-Literal $tool) + ' test ' + (Quote-Literal $package))
Write-Output ('& ' + (Quote-Literal (Join-Path $SdkDirectory 'bin/cd-sdk-preview.exe')) + ' ' +
    (Quote-Literal $package) + ' --size 320x200 --dpr 2')
