#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$PluginDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$SdkDirectory,
    [string[]]$Layouts = @(),
    [string[]]$Sizes = @('320x200', '320x320'),
    [ValidateSet('light', 'dark')][string[]]$Themes = @('light', 'dark'),
    [ValidateRange(0.5, 4)][double[]]$Dpi = @(1, 2),
    [ValidateSet('normal', 'loading', 'empty', 'error')][string[]]$Scenarios = @('normal', 'loading', 'empty', 'error'),
    [ValidatePattern('^[A-Za-z0-9-]{1,31}$')][string]$Language = 'en-US',
    [ValidateRange(0, 32503680000000)][long]$TimeMs = 1709208000000,
    [ValidateRange(-840, 840)][int]$UtcOffsetMinutes = 0,
    [ValidateRange(1, 36000)][int]$Frames = 2,
    [ValidateRange(1, 4)][int]$Jobs = 2,
    [ValidateRange(1, 120)][int]$TimeoutSeconds = 30,
    [ValidateRange(1, 4096)][int]$MaxCases = 512
)
$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture
$utf8 = [Text.UTF8Encoding]::new($false)
if (!$SdkDirectory) {
    $SdkDirectory = Join-Path $PSScriptRoot '../../..'
}
$sdk = (Resolve-Path -LiteralPath $SdkDirectory).Path
$cli = Join-Path $sdk 'bin/cd-sdk.exe'
$preview = Join-Path $sdk 'bin/cd-sdk-preview.exe'
foreach ($tool in @($cli, $preview)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Missing SDK tool: $tool"
    }
}
if (!$PluginDirectory.Count -or !$Sizes.Count -or !$Themes.Count -or !$Dpi.Count -or !$Scenarios.Count) {
    throw 'Plugin directories and all matrix dimensions must be nonempty.'
}
# Resolve existing junction/symlink ancestors before comparing watched paths.
function Physical-Path([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $current = [IO.Path]::GetPathRoot($full)
    foreach ($part in $full.Substring($current.Length).Split([char[]]'\/', [StringSplitOptions]::RemoveEmptyEntries)) {
        $current = Join-Path $current $part
        if (Test-Path -LiteralPath $current) {
            $info = Get-Item -LiteralPath $current -Force
            if ($info.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                $current = $info.ResolveLinkTarget($true).FullName
            }
        }
    }
    return [IO.Path]::TrimEndingDirectorySeparator($current)
}
$outputRoot = Physical-Path $OutputDirectory
if (Test-Path -LiteralPath $outputRoot) {
    throw 'OutputDirectory must not exist; previous galleries are never overwritten.'
}
$roots = @($PluginDirectory | ForEach-Object { Physical-Path $_ } | Select-Object -Unique)
foreach ($root in $roots) {
    if ($outputRoot.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
        $outputRoot.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'OutputDirectory must be outside every watched plugin directory.'
    }
}
$Sizes = @($Sizes | Select-Object -Unique)
$Themes = @($Themes | Select-Object -Unique)
$Dpi = @($Dpi | Select-Object -Unique)
$Scenarios = @($Scenarios | Select-Object -Unique)
$Layouts = @($Layouts | Select-Object -Unique)
foreach ($size in $Sizes) {
    if ($size -notmatch '^([1-9][0-9]{0,3})x([1-9][0-9]{0,3})$') {
        throw "Invalid logical size: $size"
    }
    foreach ($scale in $Dpi) {
        if (![double]::IsFinite($scale) -or [int]$matches[1] * $scale -gt 4096 -or [int]$matches[2] * $scale -gt 4096) {
            throw "Physical size exceeds 4096: $size at $scale DPI"
        }
    }
}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
foreach ($folder in @('images', 'logs', 'descriptors')) {
    [IO.Directory]::CreateDirectory((Join-Path $outputRoot $folder)) | Out-Null
}
$cases = [Collections.Generic.List[object]]::new()
$active = [Collections.Generic.List[object]]::new()
$inspection = $null
$started = [DateTime]::UtcNow
$finished = $false
$fatal = ''

function Start-OwnedProcess([string]$Executable, [string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new($Executable)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    foreach ($argument in $Arguments) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($start)
    return @{process = $process
        stdout = $process.StandardOutput.ReadToEndAsync()
        stderr = $process.StandardError.ReadToEndAsync()
        clock = [Diagnostics.Stopwatch]::StartNew()
    }
}
function Finish-OwnedProcess($Run, [bool]$Stop) {
    $process = $Run.process
    try {
        if ($Stop -and !$process.HasExited) {
            $process.Kill($true)
        }
        if (!$process.WaitForExit(5000)) {
            throw 'Owned preview process did not exit.'
        }
        $stdout = if ($Run.stdout.Wait(5000)) {
            $Run.stdout.GetAwaiter().GetResult()
        }
        else {
            'Output stream did not close.'
        }
        $stderr = if ($Run.stderr.Wait(5000)) {
            $Run.stderr.GetAwaiter().GetResult()
        }
        else {
            'Error stream did not close.'
        }
        return @{exitCode = $process.ExitCode
            stdout = $stdout
            stderr = $stderr
            durationMs = $Run.clock.ElapsedMilliseconds
        }
    }
    finally {
        $process.Dispose()
    }
}
function New-Case([string]$Root, [string]$Plugin, [string]$Name, [string]$Version, [string]$Stage) {
    $id = '{0:D5}' -f ($cases.Count + 1)
    $item = [ordered]@{id = $id
        plugin = $Plugin
        name = $Name
        version = $Version
        directory = $Root
        stage = $Stage
        layout = ''
        size = ''
        theme = ''
        dpi = 0
        scenario = ''

        width = 0
        height = 0
        status = 'pending'
        exitCode = $null
        durationMs = 0
        message = ''
        image = "images/$id.png"

        stdout = "logs/$id.stdout.log"
        stderr = "logs/$id.stderr.log"
        arguments = @()
    }
    $cases.Add($item)
    return $item
}
function Save-ProcessResult($Item, $Result) {
    $Item.exitCode = $Result.exitCode
    $Item.durationMs = $Result.durationMs
    [IO.File]::WriteAllText((Join-Path $outputRoot $Item.stdout), $Result.stdout, $utf8)
    [IO.File]::WriteAllText((Join-Path $outputRoot $Item.stderr), $Result.stderr, $utf8)
}
function Valid-Png($Item) {
    $path = Join-Path $outputRoot $Item.image
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        return $false
    }
    $stream = [IO.File]::OpenRead($path)
    try {
        $header = [byte[]]::new(24)
        if ($stream.Length -lt 33 -or $stream.Read($header, 0, 24) -ne 24) {
            return $false
        }
        if ([Convert]::ToHexString($header[0..15]) -ne '89504E470D0A1A0A0000000D49484452') {
            return $false
        }
        $width = [uint64]$header[16] * 16777216 + [uint64]$header[17] * 65536 + [uint64]$header[18] * 256 + $header[19]
        $height = [uint64]$header[20] * 16777216 + [uint64]$header[21] * 65536 + [uint64]$header[22] * 256 + $header[23]
        return $width -eq $Item.width -and $height -eq $Item.height
    }
    finally {
        $stream.Dispose()
    }
}
function E($Value) {
    return [Net.WebUtility]::HtmlEncode([string]$Value)
}
function Write-Reports {
    $passed = @($cases | Where-Object status -eq 'passed').Count
    $bad = @($cases | Where-Object { $_.status -in @('failed', 'timeout', 'cancelled') })
    $report = [ordered]@{schemaVersion = 1
        startedUtc = $started.ToString('O')
        completed = $finished
        fatalError = $fatal

        summary = @{total = $cases.Count
            passed = $passed
            failed = $bad.Count
        }

        configuration = @{sdk = $sdk
            language = $Language
            timeMs = $TimeMs
            utcOffsetMinutes = $UtcOffsetMinutes
            frames = $Frames
            jobs = $Jobs
            timeoutSeconds = $TimeoutSeconds

            sizes = $Sizes
            themes = $Themes
            dpi = $Dpi
            scenarios = $Scenarios
            layouts = $Layouts
        }
        cases = @($cases.ToArray())
    }
    [IO.File]::WriteAllText((Join-Path $outputRoot 'report.json'), ($report | ConvertTo-Json -Depth 12), $utf8)
    [IO.File]::WriteAllText((Join-Path $outputRoot 'failures.json'), (ConvertTo-Json -InputObject $bad -Depth 8), $utf8)
    $cards = [Text.StringBuilder]::new()
    foreach ($item in $cases) {
        $title = E "$($item.name) · $($item.plugin) · $($item.version)"
        $detail = E "$($item.layout) / $($item.size) / $($item.dpi)× / $($item.theme) / $($item.scenario)"
        $body = if ($item.status -eq 'passed') {
            "<a class='image' href='$(E $item.image)' target='_blank'><img loading='lazy' src='$(E $item.image)' alt='$title — $detail'></a>"
        }
        else {
            "<div class='error'>$(E $item.message)</div>"
        }
        $links = foreach ($kind in @('stdout', 'stderr')) {
            if (Test-Path -LiteralPath (Join-Path $outputRoot $item[$kind]) -PathType Leaf) {
                "<a href='$(E $item[$kind])'>$kind</a>"
            }
        }
        [void]$cards.AppendLine("<article data-status='$(E $item.status)' data-plugin='$(E $item.plugin)' data-theme='$(E $item.theme)' data-scenario='$(E $item.scenario)'><header><b>$title</b><span class='status $(E $item.status)'>$(E $item.status)</span></header>$body<p>$detail</p><footer>#$(E $item.id) · $(E $item.durationMs) ms · $($links -join ' · ')</footer></article>")
    }
    $pluginOptions = (@($cases | ForEach-Object plugin | Select-Object -Unique) | ForEach-Object { "<option value='$(E $_)'>$(E $_)</option>" }) -join ''
    $html = @'
<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'">
<title>Plugin preview gallery · 插件预览集</title>
<style>
:root{color-scheme:dark light;font:14px system-ui,sans-serif;background:#10151d;color:#e9edf4}*{box-sizing:border-box}body{margin:0;padding:28px;max-width:1800px;margin-inline:auto}h1{font-size:26px;margin:0 0 12px}a{color:#8ac8ff}nav{display:flex;gap:10px;flex-wrap:wrap;margin:20px 0;position:sticky;top:0;padding:12px;background:#10151def;z-index:1}input,select{font:inherit;padding:8px;border:1px solid #465365;border-radius:6px;background:#202a38;color:inherit}input{flex:1;min-width:180px}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:18px}article{border:1px solid #374559;border-radius:12px;overflow:hidden;background:#1a2330}article[hidden]{display:none}header{padding:14px;display:flex;gap:12px;align-items:start;justify-content:space-between}header b{overflow-wrap:anywhere}p,footer{padding:0 14px;overflow-wrap:anywhere}footer{padding-bottom:14px;color:#aab7ca}.status{border-radius:4px;padding:2px 6px;font-size:12px;background:#723c3c}.passed{background:#265f50}.image{display:flex;height:300px;align-items:center;justify-content:center;background:repeating-conic-gradient(#9da3aa 0% 25%,#bbc1c8 0% 50%) 0/20px 20px}.image img{max-width:100%;max-height:100%;object-fit:contain}.error{height:160px;padding:18px;color:#ffc1b9;overflow:auto;white-space:pre-wrap}.summary{color:#b8c6d9}.fatal{color:#ffc1b9;white-space:pre-wrap}#visible{align-self:center;min-width:50px}
</style>
<h1>Plugin preview gallery · 插件预览集</h1><div class="summary">__SUMMARY__</div><p class="fatal">__FATAL__</p>
<p><a href="report.json">Full report / 完整报告</a> · <a href="failures.json">Failures / 失败报告</a></p>
<nav><input id="search" placeholder="Search / 搜索布局、尺寸或插件" aria-label="Search"><select id="plugin" aria-label="Plugin"><option value="">All plugins / 全部插件</option>__PLUGINS__</select><select id="status" aria-label="Status"><option value="">All results / 全部结果</option><option value="passed">Passed / 通过</option><option value="failed">Failed / 失败</option><option value="timeout">Timeout / 超时</option><option value="cancelled">Cancelled / 取消</option></select><select id="theme" aria-label="Theme"><option value="">All themes / 全部主题</option><option>light</option><option>dark</option></select><select id="scenario" aria-label="Scenario"><option value="">All scenarios / 全部场景</option><option>normal</option><option>loading</option><option>empty</option><option>error</option></select><span id="visible"></span></nav>
<main>__CARDS__</main>
<script>
const cards=[...document.querySelectorAll('article')], search=document.querySelector('#search'), filters=['plugin','status','theme','scenario'].map(id=>document.getElementById(id));
function filter(){let count=0;const query=search.value.toLocaleLowerCase();for(const card of cards){card.hidden=!card.textContent.toLocaleLowerCase().includes(query)||filters.some(f=>f.value&&card.dataset[f.id]!==f.value);if(!card.hidden)count++;}document.querySelector('#visible').textContent=count+' / '+cards.length;}
search.addEventListener('input',filter);filters.forEach(f=>f.addEventListener('change',filter));filter();
</script></html>
'@
    # A single replacement pass prevents metadata that resembles a placeholder
    # from being interpreted as another HTML insertion point.
    $slots = @{SUMMARY = (E "$passed / $($cases.Count) passed · $($bad.Count) failed · $($started.ToString('u'))")
        FATAL = (E $fatal)
        PLUGINS = $pluginOptions
        CARDS = $cards.ToString()
    }
    $html = [regex]::Replace($html, '__(SUMMARY|FATAL|PLUGINS|CARDS)__', [Text.RegularExpressions.MatchEvaluator] { param($match) $slots[$match.Groups[1].Value] })
    [IO.File]::WriteAllText((Join-Path $outputRoot 'index.html'), $html, $utf8)
}

try {
    # Descriptor inspection does not load DLLs. Use the SDK protobuf parser, not a
    # second textproto parser in this script. A bad plugin does not block others.
    foreach ($root in $roots) {
        $inspection = Start-OwnedProcess $cli @('inspect', $root)
        $timeout = !$inspection.process.WaitForExit($TimeoutSeconds * 1000)
        $result = Finish-OwnedProcess $inspection $timeout
        $inspection = $null
        try {
            if ($timeout -or $result.exitCode -ne 0) {
                throw "Descriptor inspection failed: $($result.stderr)"
            }
            $descriptor = $result.stdout | ConvertFrom-Json
            if (!$descriptor.id -or !$descriptor.layouts.Count) {
                throw 'Descriptor has no ID or layouts.'
            }
            $selected = @($descriptor.layouts | Where-Object { !$Layouts.Count -or $_.id -in $Layouts })
            foreach ($layout in $Layouts) {
                if ($layout -notin $descriptor.layouts.id) {
                    throw "Unknown requested layout: $layout"
                }
            }
            $total = [long]$selected.Count * $Sizes.Count * $Themes.Count * $Dpi.Count * $Scenarios.Count
        }
        catch {
            $item = New-Case $root $root (Split-Path $root -Leaf) '' 'inspect'
            $item.status = if ($timeout) {
                'timeout'
            }
            else {
                'failed'
            }
            $item.message = $_.Exception.Message
            Save-ProcessResult $item $result
            continue
        }
        if ($cases.Count + $total -gt $MaxCases) {
            throw "Matrix exceeds MaxCases=$MaxCases. Narrow the dimensions or raise the explicit limit."
        }
        [IO.File]::WriteAllText((Join-Path $outputRoot "descriptors/$($cases.Count + 1).json"), $result.stdout, $utf8)
        foreach ($layout in $selected) {
            foreach ($size in $Sizes) {
                foreach ($theme in $Themes) {
                    foreach ($scale in $Dpi) {
                        foreach ($scene in $Scenarios) {
                            $item = New-Case $root $descriptor.id $descriptor.name $descriptor.version 'capture'
                            $dimensions = $size.Split('x')
                            $item.layout = $layout.id
                            $item.size = $size
                            $item.theme = $theme
                            $item.dpi = $scale
                            $item.scenario = $scene
                            $item.width = [int][Math]::Round([int]$dimensions[0] * $scale, [MidpointRounding]::AwayFromZero)
                            $item.height = [int][Math]::Round([int]$dimensions[1] * $scale, [MidpointRounding]::AwayFromZero)
                            $item.arguments = @($root, '--layout', $layout.id, '--size', $size, '--theme', $theme, '--dpr', $scale.ToString('R', $invariant),
                                '--scenario', $scene, '--language', $Language, '--time', $TimeMs.ToString($invariant), '--utc-offset', $UtcOffsetMinutes.ToString($invariant),
                                '--frames', $Frames.ToString($invariant), '--screenshot', (Join-Path $outputRoot $item.image))
                        }
                    }
                }
            }
        }
    }
    Write-Reports
    $pending = [Collections.Generic.Queue[object]]::new()
    foreach ($item in $cases) {
        if ($item.status -eq 'pending') {
            $pending.Enqueue($item)
        }
    }
    while ($pending.Count -or $active.Count) {
        while ($pending.Count -and $active.Count -lt $Jobs) {
            $item = $pending.Dequeue()
            try {
                $run = Start-OwnedProcess $preview $item.arguments
                $run.item = $item
                $item.status = 'running'
                $active.Add($run)
            }
            catch {
                $item.status = 'failed'
                $item.message = $_.Exception.Message
            }
        }
        foreach ($run in @($active.ToArray())) {
            $timeout = $run.clock.Elapsed.TotalSeconds -ge $TimeoutSeconds
            if (!$timeout -and !$run.process.HasExited) {
                continue
            }
            $item = $run.item
            $result = Finish-OwnedProcess $run $timeout
            [void]$active.Remove($run)
            Save-ProcessResult $item $result
            $item.status = if ($timeout) {
                'timeout'
            }
            elseif ($result.exitCode -ne 0) {
                'failed'
            }
            elseif (Valid-Png $item) {
                'passed'
            }
            else {
                'failed'
            }
            if ($timeout) {
                $item.message = "Preview exceeded $TimeoutSeconds seconds."
            }
            elseif ($item.status -eq 'failed') {
                $item.message = "Exit $($result.exitCode); missing/invalid output or plugin failure. $($result.stderr)"
            }
            Write-Host "[$($item.id)/$($cases.Count)] $($item.plugin) $($item.layout) $($item.size) $($item.theme) $($item.dpi)x $($item.scenario): $($item.status)"
            Write-Reports
        }
        if ($active.Count) {
            Start-Sleep -Milliseconds 40
        }
    }
    $finished = $true
}
catch {
    $fatal = $_.Exception.Message
}
finally {
    if ($inspection) {
        try {
            Finish-OwnedProcess $inspection $true | Out-Null
        }
        catch {
            Write-Warning $_
        }
    }
    foreach ($run in @($active.ToArray())) {
        try {
            Save-ProcessResult $run.item (Finish-OwnedProcess $run $true)
        }
        catch {
            $run.item.message = $_.Exception.Message
        }
    }
    foreach ($item in $cases) {
        if ($item.status -in @('pending', 'running')) {
            $item.status = 'cancelled'
            $item.message = 'Batch stopped before this case completed.'
        }
    }
    Write-Reports
}
Write-Host "Gallery: $(Join-Path $outputRoot 'index.html')"
if (!$finished -or @($cases | Where-Object status -ne 'passed').Count) {
    if ($fatal) {
        Write-Warning $fatal
    }
    exit 1
}
exit 0
