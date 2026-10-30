#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$ToolsDirectory,
    [Parameter(Mandatory)][string]$InformationDirectory,
    [Parameter(Mandatory)][string]$MediaDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$preview = Join-Path (Resolve-Path -LiteralPath $ToolsDirectory).Path 'cd-sdk-preview.exe'
$cli = Join-Path (Resolve-Path -LiteralPath $ToolsDirectory).Path 'cd-sdk.exe'
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) {
    throw 'Use a new output directory.'
}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$results = @()
foreach ($entry in @(@{name = 'info'
            path = $InformationDirectory
        }, @{name = 'media'
            path = $MediaDirectory
        })) {
    $plugin = (Resolve-Path -LiteralPath $entry.path).Path
    $captures = @()
    foreach ($scene in @('normal', 'loading', 'empty', 'error')) {
        $file = Join-Path $outputRoot "$($entry.name)-$scene.png"
        & $preview $plugin --scenario $scene --size 320x200 --dpr 2 --language zh-CN --time 1709208000000 --frames 2 --screenshot $file *> "$file.log"
        if ($LASTEXITCODE) {
            throw "Scenario capture failed: $file"
        }
        foreach ($other in $captures) {
            $diff = "$file-$(Split-Path $other -Leaf)-diff.png"
            & $cli compare $other $file $diff 0 *> "$diff.log"
            if ($LASTEXITCODE -ne 1 -or !(Test-Path -LiteralPath $diff)) {
                throw "Scenario images are equal or comparison failed: $other / $file"
            }
        }
        $captures += $file
        $results += @{plugin = $entry.name
            scenario = $scene
            image = $file
            passed = $true
        }
    }
}
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputRoot 'verification.json') -Encoding utf8
Write-Output 'PASS: eight CLI captures and pairwise distinct service states for information/media starters.'
