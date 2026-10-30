#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$SdkDirectory,
    [Parameter(Mandatory)][string]$GalleryDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$kit = (Resolve-Path -LiteralPath $SdkDirectory).Path
$gallery = (Resolve-Path -LiteralPath $GalleryDirectory).Path
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) {
    throw 'Use a new verification directory.'
}
[IO.Directory]::CreateDirectory($root) | Out-Null
$scriptPath = Join-Path $kit 'share/cyberdesktop-sdk/scripts/compare-preview-gallery.ps1'
$checks = [Collections.Generic.List[string]]::new()
function Check([bool]$Value, [string]$Message) {
    if (!$Value) {
        throw $Message
    }
    $checks.Add($Message)
    Write-Host "PASS $Message"
}
function Save($Value, [string]$Path) {
    ConvertTo-Json -InputObject $Value -Depth 20 | Set-Content -LiteralPath $Path -Encoding utf8
}
function Read([string]$Path) {
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable
}
$wrapper = Join-Path $root 'invoke.ps1'
@'
param($ScriptPath,$ParametersFile)
$ErrorActionPreference='Stop'
try { $parameters=Get-Content -LiteralPath $ParametersFile -Raw | ConvertFrom-Json -AsHashtable; & $ScriptPath @parameters; exit $LASTEXITCODE }
catch { Write-Error $_ -ErrorAction Continue; exit 2 }
'@ | Set-Content -LiteralPath $wrapper -Encoding utf8
function Run([string]$Name, [hashtable]$Options) {
    if (!$Options.SdkDirectory) {
        $Options.SdkDirectory = $kit
    }
    if (!$Options.OutputDirectory) {
        $Options.OutputDirectory = Join-Path $root $Name
    }
    $parameters = Join-Path $root "$Name-parameters.json"
    Save $Options $parameters
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($arg in @('-NoProfile', '-File', $wrapper, $scriptPath, $parameters)) {
        $start.ArgumentList.Add($arg)
    }
    $child = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        if (!$child.WaitForExit(60000)) {
            $child.Kill($true)
            throw "Comparison hung: $Name"
        }
        [IO.File]::WriteAllText((Join-Path $root "$Name.stdout.log"), $stdout.GetAwaiter().GetResult())
        [IO.File]::WriteAllText((Join-Path $root "$Name.stderr.log"), $stderr.GetAwaiter().GetResult())
        return $child.ExitCode
    }
    finally {
        if (!$child.HasExited) {
            $child.Kill($true)
        }
        $child.Dispose()
    }
}

# The real gallery is moved and reordered; case numbers, versions and absolute plugin paths must not be keys.
$moved = Join-Path $root "中文 'candidate'"
[IO.Directory]::CreateDirectory($moved) | Out-Null
Copy-Item -LiteralPath (Join-Path $gallery 'images') -Destination $moved -Recurse
$real = Read (Join-Path $gallery 'report.json')
Check ($real.completed -and $real.summary.passed -eq $real.cases.Count) 'real gallery fixture contains only successful captures'
$originalHash = (Get-FileHash -LiteralPath (Join-Path $gallery 'report.json')).Hash
[array]::Reverse($real.cases)
for ($i = 0; $i -lt $real.cases.Count; $i++) {
    $real.cases[$i].id = "new-$i"
    $real.cases[$i].version = '99.0.0'
    $real.cases[$i].directory = 'Z:/moved'
}
Save $real (Join-Path $moved 'report.json')
$code = Run 'reordered' @{BaselineDirectory = $gallery
    CandidateDirectory = $moved
    Tolerance = 0
    AllowedRatio = 0
}
$report = Read "$root/reordered/report.json"
Check ($code -eq 0 -and $report.completed -and $report.summary.passed -eq $real.cases.Count) 'real captures match across order, directory, numbering and version changes'
Check (@(Get-ChildItem "$root/reordered/diff" -Filter '*.png').Count -eq $real.cases.Count -and $report.cases[0].metrics.differing -eq 0) 'real decoded images produce a difference map and numeric metrics per pair'
Check ((Get-FileHash -LiteralPath (Join-Path $gallery 'report.json')).Hash -eq $originalHash) 'input gallery remains unchanged'

# Small generated fixtures exercise exact threshold boundaries; these are test data, not product artwork.
Add-Type -AssemblyName System.Drawing
function Make-Image([string]$Path, [bool]$Changed) {
    $bitmap = [Drawing.Bitmap]::new(8, 8)
    try {
        for ($y = 0; $y -lt 8; $y++) {
            for ($x = 0; $x -lt 8; $x++) {
                $bitmap.SetPixel($x, $y, [Drawing.Color]::FromArgb(255, 100, 100, 100))
            }
        }
        if ($Changed) {
            $bitmap.SetPixel(0, 0, [Drawing.Color]::FromArgb(255, 110, 100, 100))
        }
        $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }
}
function Fixture([string]$Name, [string[]]$Layouts) {
    $dir = Join-Path $root $Name
    [IO.Directory]::CreateDirectory((Join-Path $dir 'images')) | Out-Null
    $items = foreach ($layout in $Layouts) {
        Make-Image (Join-Path $dir "images/$layout.png") $false
        @{id = $layout
            plugin = 'com.example.compare'
            name = '<script>unsafe</script> & __CARDS__'
            version = '1.0.0'
            stage = 'capture'

            layout = $layout
            size = '8x8'
            theme = 'dark'
            dpi = 1
            scenario = 'normal'
            width = 8
            height = 8
            status = 'passed'
            image = "images/$layout.png"
            message = ''
        }
    }
    $data = @{schemaVersion = 1
        completed = $true
        fatalError = ''
        configuration = @{language = 'zh-CN'
            timeMs = 1709208000000
            utcOffsetMinutes = 0
            frames = 2
        }
        cases = @($items)
    }
    Save $data (Join-Path $dir 'report.json')
    return $dir
}
$before = Fixture 'baseline' @('equal', 'changed', 'removed', 'broken')
$after = Fixture 'candidate' @('equal', 'changed', 'added', 'broken')
Make-Image "$after/images/changed.png" $true
$bytes = [IO.File]::ReadAllBytes("$after/images/broken.png")
[IO.File]::WriteAllBytes("$after/images/broken.png", $bytes[0..32])
$code = Run 'mixed' @{BaselineDirectory = $before
    CandidateDirectory = $after
    Tolerance = 0
    AllowedRatio = 0
}
$report = Read "$root/mixed/report.json"
Check ($code -eq 1 -and $report.completed -and $report.summary.passed -eq 1 -and $report.summary.changed -eq 1 -and $report.summary.added -eq 1 -and $report.summary.missing -eq 1 -and $report.summary.error -eq 1) 'changed, added, missing and corrupt captures are distinct failures without stopping valid pairs'
Check (@(Read "$root/mixed/failures.json").Count -eq 4) 'machine-readable failures include all nonpassing cases'
$html = Get-Content "$root/mixed/index.html" -Raw
Check (!$html.Contains('<script>unsafe</script>') -and $html.Contains('&lt;script&gt;unsafe&lt;/script&gt;') -and $html.Contains('__CARDS__')) 'HTML escapes metadata and does not recursively expand placeholders'
Check (@(Get-ChildItem "$root/mixed/baseline" -Filter '*.png').Count -eq 4 -and (Test-Path "$root/mixed/inputs/baseline.json")) 'comparison bundles its own images and input manifests for offline sharing'

$one = Fixture 'one-before' @('only')
$two = Fixture 'one-after' @('only')
Make-Image "$two/images/only.png" $true
$code = Run 'tolerance-equal' @{BaselineDirectory = $one
    CandidateDirectory = $two
    Tolerance = 10
    AllowedRatio = 0
}
Check ($code -eq 0) 'channel delta equal to tolerance passes'
$code = Run 'tolerance-below' @{BaselineDirectory = $one
    CandidateDirectory = $two
    Tolerance = 9
    AllowedRatio = 0
}
Check ($code -eq 1 -and (Read "$root/tolerance-below/report.json").cases[0].metrics.maximumDelta -eq 10) 'channel delta above tolerance fails and preserves maximum delta'
$code = Run 'ratio-equal' @{BaselineDirectory = $one
    CandidateDirectory = $two
    Tolerance = 0
    AllowedRatio = (1.0 / 64)
}
Check ($code -eq 0) 'pixel ratio equal to the allowance passes'
$code = Run 'ratio-below' @{BaselineDirectory = $one
    CandidateDirectory = $two
    Tolerance = 0
    AllowedRatio = 0.0156249
}
Check ($code -eq 1) 'verdict uses full precision near a rounded printed ratio'

$original = Get-Content "$two/report.json" -Raw
foreach ($field in @('language', 'timeMs', 'utcOffsetMinutes', 'frames')) {
    $data = $original | ConvertFrom-Json -AsHashtable
    if ($field -eq 'language') {
        $data.configuration[$field] = 'en-US'
    }
    else {
        $data.configuration[$field]++
    }
    Save $data "$two/report.json"
    $code = Run "mismatch-$field" @{BaselineDirectory = $one
        CandidateDirectory = $two
    }
    Check ($code -eq 2 -and !(Test-Path "$root/mismatch-$field")) "mismatched $field is rejected before writing output"
}
$data = $original | ConvertFrom-Json -AsHashtable
$data.cases += @($data.cases[0])
Save $data "$two/report.json"
$code = Run 'duplicate' @{BaselineDirectory = $one
    CandidateDirectory = $two
}
Check ($code -eq 2 -and !(Test-Path "$root/duplicate")) 'ambiguous duplicate keys are rejected'
$data = $original | ConvertFrom-Json -AsHashtable
$data.completed = $false
Save $data "$two/report.json"
$code = Run 'incomplete' @{BaselineDirectory = $one
    CandidateDirectory = $two
}
Check ($code -eq 2) 'incomplete capture runs cannot pass comparison'
[IO.File]::WriteAllText("$two/report.json", $original)
$code = Run 'inside' @{BaselineDirectory = $one
    CandidateDirectory = $two
    OutputDirectory = "$one/compare"
}
Check ($code -eq 2 -and !(Test-Path "$one/compare")) 'comparison cannot write inside an input gallery'
$hash = (Get-FileHash "$root/mixed/report.json").Hash
$code = Run 'existing' @{BaselineDirectory = $one
    CandidateDirectory = $two
    OutputDirectory = "$root/mixed"
}
Check ($code -eq 2 -and (Get-FileHash "$root/mixed/report.json").Hash -eq $hash) 'existing output is not overwritten'

$data = $original | ConvertFrom-Json -AsHashtable
$data.cases[0].image = '../one-before/images/only.png'
Save $data "$two/report.json"
$code = Run 'escape' @{BaselineDirectory = $one
    CandidateDirectory = $two
}
Check ($code -eq 1 -and (Read "$root/escape/report.json").summary.error -eq 1 -and !(Test-Path "$root/escape/candidate/00001.png")) 'manifest images cannot escape the input gallery'
$data = $original | ConvertFrom-Json -AsHashtable
$data.cases[0].status = 'failed'
$data.cases[0].message = 'capture failed'
Save $data "$two/report.json"
$code = Run 'failed-capture' @{BaselineDirectory = $one
    CandidateDirectory = $two
}
Check ($code -eq 1 -and (Read "$root/failed-capture/report.json").summary.error -eq 1) 'a failed source capture is an error rather than a pixel regression'
$data = $original | ConvertFrom-Json -AsHashtable
$data.cases += @(@{id = 'inspect'
        plugin = 'broken-plugin'
        name = 'Broken'
        stage = 'inspect'
        status = 'failed'
        message = 'missing descriptor'
    })
Save $data "$two/report.json"
$code = Run 'inspection' @{BaselineDirectory = $one
    CandidateDirectory = $two
    Tolerance = 255
}
$report = Read "$root/inspection/report.json"
Check ($code -eq 1 -and $report.summary.passed -eq 1 -and $report.summary.error -eq 1) 'descriptor inspection failures remain visible alongside passing captures'
[IO.File]::WriteAllText("$two/report.json", $original)

# An isolated child exercises deadline cleanup; never replace the installed CLI or touch other processes.
$fake = Join-Path $root 'timeout-kit'
[IO.Directory]::CreateDirectory("$fake/bin") | Out-Null
$source = Join-Path $root 'Timeout.cs'
@'
using System;
using System.IO;
using System.Diagnostics;
using System.Threading;
class TimeoutProbe { static void Main() { File.WriteAllText(Environment.GetEnvironmentVariable("CD_COMPARE_PID"), Process.GetCurrentProcess().Id.ToString()); Thread.Sleep(30000); } }
'@ | Set-Content -LiteralPath $source -Encoding utf8
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $compiler /nologo /target:exe "/out:$fake/bin/cd-sdk.exe" $source
if ($LASTEXITCODE -ne 0) {
    throw 'Timeout fixture compile failed.'
}
$saved = $env:CD_COMPARE_PID
try {
    $env:CD_COMPARE_PID = Join-Path $root 'timeout.pid'
    $code = Run 'timeout' @{BaselineDirectory = $one
        CandidateDirectory = $two
        SdkDirectory = $fake
        TimeoutSeconds = 1
    }
    $childId = [int](Get-Content -LiteralPath $env:CD_COMPARE_PID)
    Check ($code -eq 1 -and (Read "$root/timeout/report.json").summary.timeout -eq 1 -and !(Get-Process -Id $childId -ErrorAction SilentlyContinue)) 'timed-out comparison is reported and its owned process is stopped'
}
finally {
    $env:CD_COMPARE_PID = $saved
}

Save @{timestamp = [DateTime]::UtcNow.ToString('O')
    realCaptures = $real.cases.Count
    checked = @($checks.ToArray())
} "$root/verification.json"
Write-Host "PASS $($checks.Count) preview comparison checks"
