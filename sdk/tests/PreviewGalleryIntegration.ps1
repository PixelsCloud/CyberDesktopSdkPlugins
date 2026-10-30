#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$SdkDirectory,
    [Parameter(Mandatory)][string]$InformationDirectory,
    [Parameter(Mandatory)][string]$MediaDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$kit = (Resolve-Path -LiteralPath $SdkDirectory).Path
$info = (Resolve-Path -LiteralPath $InformationDirectory).Path
$media = (Resolve-Path -LiteralPath $MediaDirectory).Path
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) {
    throw 'Use a new verification directory.'
}
[IO.Directory]::CreateDirectory($root) | Out-Null
$scriptPath = Join-Path $kit 'share/cyberdesktop-sdk/scripts/preview-gallery.ps1'
$checks = [Collections.Generic.List[string]]::new()
function Check([bool]$Value, [string]$Message) {
    if (!$Value) {
        throw $Message
    }
    $checks.Add($Message)
    Write-Host "PASS $Message"
}
$wrapper = Join-Path $root 'invoke.ps1'
@'
param($ScriptPath,$ParametersFile)
$ErrorActionPreference='Stop'
try {
    $parameters=Get-Content -LiteralPath $ParametersFile -Raw | ConvertFrom-Json -AsHashtable
    & $ScriptPath @parameters
    exit $LASTEXITCODE
} catch { Write-Error $_ -ErrorAction Continue; exit 2 }
'@ | Set-Content -LiteralPath $wrapper -Encoding utf8
function Run-Batch([string]$Name, [hashtable]$Options) {
    $Options.SdkDirectory = if ($Options.SdkDirectory) {
        $Options.SdkDirectory
    }
    else {
        $kit
    }
    if (!$Options.OutputDirectory) {
        $Options.OutputDirectory = Join-Path $root $Name
    }
    $parameters = Join-Path $root "$Name-parameters.json"
    $Options | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $parameters -Encoding utf8
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('-NoProfile', '-File', $wrapper, $scriptPath, $parameters)) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(180000)) {
            $process.Kill($true)
            throw "Batch hung: $Name"
        }
        [IO.File]::WriteAllText((Join-Path $root "$Name.stdout.log"), $stdout.GetAwaiter().GetResult())
        [IO.File]::WriteAllText((Join-Path $root "$Name.stderr.log"), $stderr.GetAwaiter().GetResult())
        return $process.ExitCode
    }
    finally {
        if (!$process.HasExited) {
            $process.Kill($true)
        }
        $process.Dispose()
    }
}

# Exercise real DLLs and the SDK descriptor parser, including editable metadata
# taking precedence over a previously compiled descriptor.pb.
$multi = Join-Path $root "插件 'spaces'"
Copy-Item -LiteralPath $info -Destination $multi -Recurse
$descriptor = Get-Content -LiteralPath (Join-Path $multi 'plugin.textproto') -Raw
$descriptor = [regex]::Replace($descriptor, '(?m)^name:.*$', 'name: "<script>alert(1)</script> & __CARDS__"')
$descriptor += @'

layouts {
  id: "compact"
  name: "Compact"
  topology: RECT
  allowed_rotations: 0
  cells { a: 0 b: 0 }
}
'@
Set-Content -LiteralPath (Join-Path $multi 'plugin.textproto') -Value $descriptor -Encoding utf8
$inspected = & "$kit/bin/cd-sdk.exe" inspect $multi | ConvertFrom-Json
Check ($LASTEXITCODE -eq 0 -and $inspected.layouts.Count -eq 2) 'directory inspection reads current textproto without loading its DLL'
$code = Run-Batch 'matrix' @{PluginDirectory = @($multi, $media)
    Sizes = @('320x200', '320x320')
    Themes = @('light', 'dark')
    Dpi = @(1, 2)
    Scenarios = @('normal', 'empty')
    Jobs = 2
    MaxCases = 64
    Language = 'zh-CN'
}
$report = Get-Content -LiteralPath "$root/matrix/report.json" -Raw | ConvertFrom-Json
Check ($code -eq 0 -and $report.completed -and $report.summary.total -eq 48 -and $report.summary.passed -eq 48) 'all 48 real plugin combinations complete'
Check (@($report.cases.layout | Select-Object -Unique).Count -eq 2 -and @($report.cases.plugin | Select-Object -Unique).Count -eq 2) 'gallery discovers all layouts from both plugin directories'
Check (@(Get-ChildItem -LiteralPath "$root/matrix/images" -Filter '*.png').Count -eq 48) 'each combination has its own PNG'
$html = Get-Content -LiteralPath "$root/matrix/index.html" -Raw
Check (!$html.Contains('<script>alert(1)</script>') -and $html.Contains('&lt;script&gt;alert(1)&lt;/script&gt;') -and $html.Contains('__CARDS__')) 'gallery escapes metadata and does not recursively expand placeholder-like names'
Check ($report.cases[0].arguments.Contains($multi) -and (Test-Path -LiteralPath "$root/matrix/logs/00001.stdout.log")) 'Unicode and quoted paths survive structured arguments and logs'
$before = (Get-FileHash -LiteralPath "$root/matrix/report.json").Hash
$code = Run-Batch 'existing' @{PluginDirectory = @($info)
    OutputDirectory = "$root/matrix"
}
Check ($code -ne 0 -and (Get-FileHash -LiteralPath "$root/matrix/report.json").Hash -eq $before) 'an existing gallery is never overwritten'
$code = Run-Batch 'inside' @{PluginDirectory = @($multi)
    OutputDirectory = (Join-Path $multi 'gallery')
}
Check ($code -ne 0 -and !(Test-Path -LiteralPath (Join-Path $multi 'gallery'))) 'output inside a watched plugin is rejected before writing'
$code = Run-Batch 'oversized' @{PluginDirectory = @($info)
    Sizes = @('4096x4096')
    Dpi = @(2)
}
Check ($code -ne 0 -and !(Test-Path -LiteralPath "$root/oversized")) 'invalid physical dimensions fail before allocating a gallery'
$code = Run-Batch 'limit' @{PluginDirectory = @($multi)
    MaxCases = 1
}
$limit = Get-Content -LiteralPath "$root/limit/report.json" -Raw | ConvertFrom-Json
Check ($code -ne 0 -and !$limit.completed -and $limit.fatalError.Contains('MaxCases') -and !(Get-ChildItem -LiteralPath "$root/limit/images")) 'case limit aborts before launching the capture matrix'

$bad = Join-Path $root 'missing-dll'
[IO.Directory]::CreateDirectory($bad) | Out-Null
Copy-Item -LiteralPath (Join-Path $info 'plugin.textproto') -Destination (Join-Path $bad 'plugin.textproto')
$empty = Join-Path $root 'missing-descriptor'
[IO.Directory]::CreateDirectory($empty) | Out-Null
$code = Run-Batch 'failures' @{PluginDirectory = @($empty, $bad, $media)
    Sizes = @('320x200')
    Themes = @('dark')
    Dpi = @(1)
    Scenarios = @('normal')
    Jobs = 2
}
$failed = Get-Content -LiteralPath "$root/failures/report.json" -Raw | ConvertFrom-Json
$errors = @(Get-Content -LiteralPath "$root/failures/failures.json" -Raw | ConvertFrom-Json)
Check ($code -eq 1 -and $failed.completed -and $failed.summary.passed -eq 1 -and $errors.Count -eq 2) 'descriptor and renderer failures do not stop a valid plugin'
Check (@($errors.stage | Select-Object -Unique).Count -eq 2 -and $errors[1].exitCode -ne 0) 'failure report distinguishes discovery and capture stages with exit codes'

# A deliberately faulty renderer makes timeout and invalid-file checks
# deterministic. It is isolated from the actual SDK and never loads a plugin.
$fixture = Join-Path $root 'fixture-sdk'
[IO.Directory]::CreateDirectory((Join-Path $fixture 'bin')) | Out-Null
Copy-Item -LiteralPath "$kit/bin/cd-sdk.exe" -Destination "$fixture/bin/cd-sdk.exe"
$source = Join-Path $root 'RendererFixture.cs'
@'
using System;
using System.IO;
using System.Diagnostics;
using System.Threading;
class RendererFixture {
  static void Main(string[] args) {
    File.WriteAllText(Path.Combine(Path.GetDirectoryName(Process.GetCurrentProcess().MainModule.FileName), "pid.txt"), Process.GetCurrentProcess().Id.ToString());
    if (Environment.GetEnvironmentVariable("CD_GALLERY_TEST_MODE") == "timeout") { Thread.Sleep(30000); return; }
    int index = Array.IndexOf(args, "--screenshot");
    if (index >= 0) File.WriteAllText(args[index + 1], "not a PNG");
  }
}
'@ | Set-Content -LiteralPath $source -Encoding utf8
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $compiler /nologo /target:exe "/out:$fixture/bin/cd-sdk-preview.exe" $source
if ($LASTEXITCODE) {
    throw 'Fixture compiler failed.'
}
$oldMode = $env:CD_GALLERY_TEST_MODE
try {
    $env:CD_GALLERY_TEST_MODE = 'timeout'
    $code = Run-Batch 'timeout' @{SdkDirectory = $fixture
        PluginDirectory = @($info)
        Sizes = @('320x200')
        Themes = @('dark')
        Dpi = @(1)
        Scenarios = @('normal')
        TimeoutSeconds = 2
    }
    $timeout = Get-Content -LiteralPath "$root/timeout/report.json" -Raw | ConvertFrom-Json
    $ownedId = [int](Get-Content -LiteralPath "$fixture/bin/pid.txt")
    Check ($code -eq 1 -and $timeout.cases[0].status -eq 'timeout' -and !(Get-Process -Id $ownedId -ErrorAction SilentlyContinue)) 'timed out child is stopped and recorded without leaving a process'
    $env:CD_GALLERY_TEST_MODE = 'invalid'
    $code = Run-Batch 'invalid-png' @{SdkDirectory = $fixture
        PluginDirectory = @($info)
        Sizes = @('320x200')
        Themes = @('dark')
        Dpi = @(1)
        Scenarios = @('normal')
    }
    $invalid = Get-Content -LiteralPath "$root/invalid-png/report.json" -Raw | ConvertFrom-Json
    Check ($code -eq 1 -and $invalid.cases[0].status -eq 'failed' -and $invalid.cases[0].exitCode -eq 0) 'zero renderer exit without a valid PNG is still a failure'
}
finally {
    $env:CD_GALLERY_TEST_MODE = $oldMode
}
@{checked = @($checks.ToArray())
    captures = 48
    timestamp = [DateTime]::UtcNow.ToString('O')
} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'verification.json') -Encoding utf8
