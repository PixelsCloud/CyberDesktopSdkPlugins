#requires -Version 7.0
[CmdletBinding()]
param([string]$OutputDirectory, [switch]$SdkOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = if ($OutputDirectory) {
    [IO.Path]::GetFullPath($OutputDirectory)
}
else {
    [IO.Path]::GetFullPath((Join-Path $root 'build/relwithdebinfo/Testing/sdk-independent/public-source'))
}
$inventory = Get-Content -LiteralPath "$root/sdk/public-source.json" -Raw | ConvertFrom-Json
$scope = if ($SdkOnly) {
    'sdk'
}
else {
    'sdk-and-plugins'
}

function Assert-CandidatePath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -ne $output -and !$full.StartsWith($output + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Candidate path escaped its fixed output: $Path"
    }
    for ($ancestor = $full; $ancestor; $ancestor = Split-Path $ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Candidate path cannot traverse a reparse point: $ancestor"
        }
    }
}

Assert-CandidatePath $output
if ($root -eq $output -or $root.StartsWith($output.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Public export cannot replace the checkout or its parent'
}
foreach ($tree in $inventory.trees) {
    $inputRoot = [IO.Path]::GetFullPath((Join-Path $root $tree.path))
    if ($output -eq $inputRoot -or $output.StartsWith($inputRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Public export cannot replace source: $inputRoot"
    }
}
if ((Test-Path -LiteralPath $output) -and @(Get-ChildItem -LiteralPath $output -Force).Count -and
    !(Test-Path -LiteralPath "$output/public-source-files.json")) {
    throw 'Existing export directory lacks its generated inventory; refusing to overwrite unrelated files'
}
if (Test-Path -LiteralPath "$output/public-source-files.json") {
    $previous = Get-Content -LiteralPath "$output/public-source-files.json" -Raw | ConvertFrom-Json
    if ($previous.scope -ne $scope) {
        throw 'Reuse a candidate directory only for the same export scope; SDK-only and full exports must stay separate'
    }
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
foreach ($tree in $inventory.trees) {
    if ($SdkOnly -and 'sdk' -notin $tree.consumers) {
        continue
    }
    $source = [IO.Path]::GetFullPath((Join-Path $root $tree.path))
    if (!$source.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Invalid public source path: $($tree.path)"
    }
    $destination = Join-Path $output $tree.path
    Assert-CandidatePath $destination
    $excluded = @('.git', 'design', '__pycache__')
    foreach ($path in $inventory.excludedSourceTrees) {
        $excluded += Join-Path $root $path
    }
    & robocopy $source $destination /MIR /COPY:DAT /DCOPY:DAT /R:1 /W:1 /XJ /XD @excluded /XF '*.pyc' /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "Public source export failed: $($tree.path)"
    }
}
foreach ($file in $inventory.rootFiles) {
    $destination = Join-Path $output $file
    Assert-CandidatePath $destination
    Copy-Item -LiteralPath "$root/$file" -Destination $destination -Force
}
foreach ($file in $inventory.repositoryFiles.PSObject.Properties) {
    if ($SdkOnly -and $file.Name.StartsWith('plugins/')) {
        continue
    }
    $destination = Join-Path $output $file.Value
    Assert-CandidatePath $destination
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    Copy-Item -LiteralPath "$root/$($file.Name)" -Destination $destination -Force
}
if ($SdkOnly) {
    Copy-Item -LiteralPath "$root/sdk/repository/SDK.CMakeLists.txt" -Destination "$output/CMakeLists.txt" -Force
    Copy-Item -LiteralPath "$root/sdk/repository/SDK.README.md" -Destination "$output/README.md" -Force
}
$allowedTrees = @($inventory.trees | Where-Object { !$SdkOnly -or 'sdk' -in $_.consumers } | ForEach-Object path)
$allowedFiles = @($inventory.rootFiles) + @($inventory.repositoryFiles.PSObject.Properties.Value)
$files = @(Get-ChildItem -LiteralPath $output -File -Recurse | ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($output, $_.FullName).Replace('\', '/')
        if ($relative -match '(^|/)(design|\.git)/' -or $_.Extension -in '.exe', '.dll', '.obj', '.pdb', '.sqlite3') {
            throw "Non-source material in public candidate: $relative"
        }
        if ($relative -ne 'public-source-files.json') {
            $underTree = @($allowedTrees | Where-Object { $relative.StartsWith($_ + '/', [StringComparison]::Ordinal) }).Count -gt 0
            $excluded = @($inventory.excludedSourceTrees | Where-Object { $relative.StartsWith($_ + '/', [StringComparison]::Ordinal) }).Count -gt 0
            if ((!$underTree -and $relative -notin $allowedFiles) -or $excluded) {
                throw "Unlisted or retired file in public candidate: $relative"
            }
            $relative
        }
    } | Sort-Object)
@{ format = 1
    scope = $scope
    files = $files
} | ConvertTo-Json -Depth 3 |
    Set-Content -LiteralPath "$output/public-source-files.json" -Encoding utf8
Write-Output "Public source candidate: $output ($($files.Count) files; no Git history or remote operation)"
