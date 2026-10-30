#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BaselineDirectory,
    [Parameter(Mandatory)][string]$CandidateDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$SdkDirectory,
    [ValidateRange(0, 255)][int]$Tolerance = 3,
    [ValidateRange(0, 1)][double]$AllowedRatio = 0.001,
    [ValidateRange(1, 120)][int]$TimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture
if (![double]::IsFinite($AllowedRatio)) {
    throw 'AllowedRatio must be finite.'
}
if (!$SdkDirectory) {
    $SdkDirectory = Join-Path $PSScriptRoot '../../..'
}
$cli = Join-Path (Resolve-Path -LiteralPath $SdkDirectory).Path 'bin/cd-sdk.exe'
if (!(Test-Path -LiteralPath $cli -PathType Leaf)) {
    throw "Missing SDK tool: $cli"
}

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
function Inside([string]$Path, [string]$Root) {
    return $Path.Equals($Root, [StringComparison]::OrdinalIgnoreCase) -or
    $Path.StartsWith($Root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}
function Read-Gallery([string]$Root) {
    $reportPath = Join-Path $Root 'report.json'
    if ((Get-Item -LiteralPath $reportPath).Length -gt 16MB) {
        throw 'Gallery report exceeds 16 MiB.'
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json -AsHashtable
    if ($report.schemaVersion -ne 1 -or $report.completed -ne $true -or $report.fatalError) {
        throw "Expected a completed schema 1 gallery without a fatal error: $Root"
    }
    if (!$report.cases.Count -or $report.cases.Count -gt 4096) {
        throw 'Gallery must contain 1..4096 cases.'
    }
    foreach ($field in @('language', 'timeMs', 'utcOffsetMinutes', 'frames')) {
        if (!$report.configuration.Contains($field)) {
            throw "Missing gallery configuration: $field"
        }
    }
    $map = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    $errors = [Collections.Generic.List[object]]::new()
    foreach ($item in $report.cases) {
        if ($item.stage -eq 'inspect') {
            $errors.Add($item)
            continue
        }
        if ($item.stage -ne 'capture') {
            throw "Unknown gallery stage: $($item.stage)"
        }
        foreach ($field in @('plugin', 'layout', 'size', 'theme', 'scenario', 'status')) {
            if ($item[$field] -isnot [string] -or !$item[$field]) {
                throw "Missing capture field: $field"
            }
        }
        $dpi = [double]$item.dpi
        if (![double]::IsFinite($dpi) -or $dpi -lt 0.5 -or $dpi -gt 4 -or $item.size -cnotmatch '^([1-9][0-9]{0,3})x([1-9][0-9]{0,3})$') {
            throw 'Invalid capture size or DPI.'
        }
        $w = [Math]::Round([int]$matches[1] * $dpi, [MidpointRounding]::AwayFromZero)
        $h = [Math]::Round([int]$matches[2] * $dpi, [MidpointRounding]::AwayFromZero)
        if ($w -gt 4096 -or $h -gt 4096 -or $item.width -ne $w -or $item.height -ne $h) {
            throw 'Invalid capture physical dimensions.'
        }
        # JSON tuples avoid separator collisions. Paths, order and plugin versions are intentionally excluded.
        $key = ConvertTo-Json -InputObject @($item.plugin, $item.layout, $item.size, $item.theme, $dpi.ToString('R', $invariant), $item.scenario) -Compress
        if (!$map.TryAdd($key, $item)) {
            throw "Ambiguous duplicate capture in $Root : $key"
        }
    }
    return @{root = $Root
        report = $report
        map = $map
        errors = $errors
    }
}
$baselineRoot = Physical-Path $BaselineDirectory
$candidateRoot = Physical-Path $CandidateDirectory
$outputRoot = Physical-Path $OutputDirectory
if (Test-Path -LiteralPath $outputRoot) {
    throw 'OutputDirectory must not exist; earlier comparisons are never overwritten.'
}
foreach ($root in @($baselineRoot, $candidateRoot)) {
    if (Inside $outputRoot $root) {
        throw 'OutputDirectory must be outside both input galleries.'
    }
}
$baseline = Read-Gallery $baselineRoot
$candidate = Read-Gallery $candidateRoot
foreach ($field in @('language', 'timeMs', 'utcOffsetMinutes', 'frames')) {
    if ($baseline.report.configuration[$field] -cne $candidate.report.configuration[$field]) {
        throw "Incompatible capture configuration '$field'; recapture with matching settings."
    }
}

[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
foreach ($folder in @('baseline', 'candidate', 'diff', 'logs', 'inputs')) {
    [IO.Directory]::CreateDirectory((Join-Path $outputRoot $folder)) | Out-Null
}
Copy-Item -LiteralPath (Join-Path $baselineRoot 'report.json') -Destination (Join-Path $outputRoot 'inputs/baseline.json')
Copy-Item -LiteralPath (Join-Path $candidateRoot 'report.json') -Destination (Join-Path $outputRoot 'inputs/candidate.json')
$cases = [Collections.Generic.List[object]]::new()
$started = [DateTime]::UtcNow
$finished = $false
$fatal = ''

function New-Result($Before, $After) {
    $source = if ($null -ne $After) {
        $After
    }
    else {
        $Before
    }
    $item = [ordered]@{id = ('{0:D5}' -f ($cases.Count + 1))
        plugin = $source.plugin
        name = $source.name
        layout = $source.layout

        size = $source.size
        theme = $source.theme
        dpi = $source.dpi
        scenario = $source.scenario
        status = 'error'
        message = ''

        baselineVersion = $Before.version
        candidateVersion = $After.version
        baselineCase = $Before.id
        candidateCase = $After.id

        baseline = ''
        candidate = ''
        diff = ''
        metrics = $null
        exitCode = $null
        durationMs = 0
        stdout = ''
        stderr = ''
    }
    $cases.Add($item)
    return $item
}
function Copy-Capture($Gallery, $Source, [string]$Side, $Item) {
    if ($null -eq $Source -or $Source.status -ne 'passed') {
        return
    }
    $relative = [string]$Source.image
    if (!$relative -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') -or [IO.Path]::GetExtension($relative) -ine '.png') {
        throw "Invalid $Side image path."
    }
    $path = Physical-Path (Join-Path $Gallery.root $relative)
    if (!(Inside $path $Gallery.root) -or !(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing or out-of-gallery $Side image."
    }
    # Bound allocation before the existing WIC decoder is invoked, and check dimensions against the manifest.
    $stream = [IO.File]::OpenRead($path)
    try {
        $header = [byte[]]::new(24)
        if ($stream.Length -gt 64MB -or $stream.Length -lt 33 -or $stream.Read($header, 0, 24) -ne 24 -or
            [Convert]::ToHexString($header[0..15]) -ne '89504E470D0A1A0A0000000D49484452') {
            throw "Invalid $Side PNG header."
        }
        $width = [uint64]$header[16] * 16777216 + [uint64]$header[17] * 65536 + [uint64]$header[18] * 256 + $header[19]
        $height = [uint64]$header[20] * 16777216 + [uint64]$header[21] * 65536 + [uint64]$header[22] * 256 + $header[23]
        if ($width -ne $Source.width -or $height -ne $Source.height) {
            throw "$Side PNG dimensions do not match the manifest."
        }
    }
    finally {
        $stream.Dispose()
    }
    $destination = "$Side/$($Item.id).png"
    Copy-Item -LiteralPath $path -Destination (Join-Path $outputRoot $destination)
    $Item[$Side] = $destination
}
function Compare-Capture($Item) {
    $Item.stdout = "logs/$($Item.id).stdout.log"
    $Item.stderr = "logs/$($Item.id).stderr.log"
    $diff = "diff/$($Item.id).png"
    $start = [Diagnostics.ProcessStartInfo]::new($cli)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    foreach ($argument in @('compare', (Join-Path $outputRoot $Item.baseline), (Join-Path $outputRoot $Item.candidate),
            (Join-Path $outputRoot $diff), $Tolerance.ToString($invariant), $AllowedRatio.ToString('R', $invariant))) {
        $start.ArgumentList.Add($argument)
    }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        while (!$process.WaitForExit(100)) {
            if ($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                $Item.status = 'timeout'
                $process.Kill($true)
                break
            }
        }
        if (!$process.WaitForExit(5000)) {
            throw 'Comparison process did not exit.'
        }
        if (!$stdout.Wait(5000) -or !$stderr.Wait(5000)) {
            throw 'Comparison output streams did not close.'
        }
        $text = $stdout.GetAwaiter().GetResult()
        $errorText = $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $outputRoot $Item.stdout), $text, $utf8)
        [IO.File]::WriteAllText((Join-Path $outputRoot $Item.stderr), $errorText, $utf8)
        $Item.exitCode = $process.ExitCode
        if ($Item.status -eq 'timeout') {
            $Item.message = "Comparison exceeded $TimeoutSeconds seconds."
            return
        }
        $metric = [regex]::Match($text.Trim(), '\Apixels=(\d+) differing=(\d+) ratio=([0-9.eE+-]+) maxDelta=(\d+) mae=([0-9.eE+-]+)\z')
        if ($process.ExitCode -notin @(0, 1) -or !$metric.Success -or !(Test-Path -LiteralPath (Join-Path $outputRoot $diff) -PathType Leaf)) {
            throw "Image comparison failed (exit $($process.ExitCode)): $errorText"
        }
        $Item.metrics = @{pixels = [long]$metric.Groups[1].Value
            differing = [long]$metric.Groups[2].Value

            ratio = [double]::Parse($metric.Groups[3].Value, $invariant)
            maximumDelta = [int]$metric.Groups[4].Value

            meanAbsoluteError = [double]::Parse($metric.Groups[5].Value, $invariant)
        }
        $Item.diff = $diff
        # The CLI uses full precision for its verdict; printed metrics are for display only.
        $Item.status = if ($process.ExitCode -eq 0) {
            'passed'
        }
        else {
            'changed'
        }
    }
    finally {
        if (!$process.HasExited) {
            $process.Kill($true)
            [void]$process.WaitForExit(5000)
        }
        $process.Dispose()
        $Item.durationMs = $watch.ElapsedMilliseconds
    }
}
function E($Value) {
    return [Net.WebUtility]::HtmlEncode([string]$Value)
}
function Write-Reports {
    $summary = [ordered]@{total = $cases.Count
        passed = 0
        changed = 0
        added = 0
        missing = 0
        error = 0
        timeout = 0
    }
    foreach ($item in $cases) {
        $summary[$item.status]++
    }
    $report = [ordered]@{schemaVersion = 1
        kind = 'preview-gallery-comparison'
        startedUtc = $started.ToString('O')
        completed = $finished
        fatalError = $fatal

        baselineDirectory = $baselineRoot
        candidateDirectory = $candidateRoot
        summary = $summary

        configuration = @{tolerance = $Tolerance
            allowedRatio = $AllowedRatio
            timeoutSeconds = $TimeoutSeconds
            capture = $baseline.report.configuration
        }
        cases = @($cases.ToArray())
    }
    [IO.File]::WriteAllText((Join-Path $outputRoot 'report.json'), ($report | ConvertTo-Json -Depth 15), $utf8)
    $failures = @($cases | Where-Object status -ne 'passed')
    [IO.File]::WriteAllText((Join-Path $outputRoot 'failures.json'), (ConvertTo-Json -InputObject $failures -Depth 12), $utf8)
    $cards = [Text.StringBuilder]::new()
    $display = @($cases | Sort-Object @{Expression = { @{changed = 0
                    error = 1
                    timeout = 2
                    added = 3
                    missing = 4
                    passed = 5
                }[$_.status] }
        }, plugin, layout)
    foreach ($item in $display) {
        $views = foreach ($side in @('baseline', 'candidate', 'diff')) {
            $label = @{baseline = 'Baseline / 基线'
                candidate = 'Candidate / 当前'
                diff = 'Difference / 差异'
            }[$side]
            $image = if ($item[$side]) {
                "<a href='$(E $item[$side])' target='_blank'><img loading='lazy' src='$(E $item[$side])' alt='$label'></a>"
            }
            else {
                '<div class="absent">—</div>'
            }
            "<figure><figcaption>$label</figcaption>$image</figure>"
        }
        $metrics = if ($null -ne $item.metrics) {
            E ("Differing / 差异像素: {0} / {1} ({2:P3}) · max Δ {3} · MAE {4}" -f $item.metrics.differing, $item.metrics.pixels, $item.metrics.ratio, $item.metrics.maximumDelta, $item.metrics.meanAbsoluteError)
        }
        else {
            ''
        }
        $links = foreach ($kind in @('stdout', 'stderr')) {
            if ($item[$kind] -and (Test-Path -LiteralPath (Join-Path $outputRoot $item[$kind]))) {
                "<a href='$(E $item[$kind])'>$kind</a>"
            }
        }
        [void]$cards.AppendLine("<article data-status='$(E $item.status)' data-plugin='$(E $item.plugin)'><header><b>$(E $item.name) · $(E $item.plugin)</b><span class='status $(E $item.status)'>$(E $item.status)</span></header><p>$(E "$($item.layout) / $($item.size) / $($item.dpi)× / $($item.theme) / $($item.scenario)")</p><div class='views'>$($views -join '')</div><p>$metrics</p><p class='message'>$(E $item.message)</p><footer>$(E "$($item.baselineVersion) → $($item.candidateVersion)") · $($links -join ' · ')</footer></article>")
    }
    $plugins = (@($cases | ForEach-Object plugin | Select-Object -Unique) | ForEach-Object { "<option value='$(E $_)'>$(E $_)</option>" }) -join ''
    $html = @'
<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'">
<title>Preview comparison · 预览基线对比</title><style>
:root{color-scheme:dark;font:14px system-ui,sans-serif;background:#10151d;color:#e9edf4}*{box-sizing:border-box}body{margin:auto;padding:28px;max-width:1600px}h1{font-size:26px}a{color:#8ac8ff}nav{display:flex;gap:10px;flex-wrap:wrap;position:sticky;top:0;background:#10151df5;padding:14px 0;z-index:1}input,select{font:inherit;background:#202a38;color:inherit;border:1px solid #465365;border-radius:6px;padding:8px}input{flex:1;min-width:160px}article{background:#1a2330;border:1px solid #374559;border-radius:12px;margin:18px 0;overflow:hidden}article[hidden]{display:none}header{display:flex;justify-content:space-between;gap:12px;padding:16px}header b,p,footer{overflow-wrap:anywhere}p,footer{margin:0;padding:8px 16px}footer{color:#aab7ca;padding-bottom:16px}.status{background:#723c3c;border-radius:4px;padding:2px 8px}.passed{background:#265f50}.added,.missing{background:#66502a}.views{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:2px}figure{margin:0}figcaption{padding:8px 16px;color:#b8c6d9}figure a,.absent{display:flex;height:280px;align-items:center;justify-content:center;background:repeating-conic-gradient(#9da3aa 0% 25%,#bbc1c8 0% 50%) 0/20px 20px}img{max-width:100%;max-height:100%;object-fit:contain}.message,.fatal{color:#ffc1b9;white-space:pre-wrap}#visible{align-self:center}@media(max-width:650px){body{padding:12px}.views{grid-template-columns:1fr}}
</style><h1>Preview comparison · 预览基线对比</h1><p>__SUMMARY__</p><p>__THRESHOLD__</p><p class="fatal">__FATAL__</p>
<p><a href="report.json">Full report / 完整报告</a> · <a href="failures.json">Failures / 失败报告</a></p>
<nav><input id="search" aria-label="Search" placeholder="Search / 搜索插件、布局或场景"><select id="plugin" aria-label="Plugin"><option value="">All plugins / 全部插件</option>__PLUGINS__</select><select id="status" aria-label="Status"><option value="">All results / 全部结果</option><option value="passed">Passed / 通过</option><option value="changed">Changed / 超出阈值</option><option value="added">Added / 新增</option><option value="missing">Missing / 缺失</option><option value="error">Error / 错误</option><option value="timeout">Timeout / 超时</option></select><span id="visible"></span></nav><main>__CARDS__</main>
<script>
const cards=[...document.querySelectorAll('article')],search=document.getElementById('search'),filters=['plugin','status'].map(id=>document.getElementById(id));
function filter(){let count=0;const query=search.value.toLocaleLowerCase();for(const card of cards){card.hidden=!card.textContent.toLocaleLowerCase().includes(query)||filters.some(f=>f.value&&card.dataset[f.id]!==f.value);if(!card.hidden)count++;}document.getElementById('visible').textContent=count+' / '+cards.length;}
search.addEventListener('input',filter);filters.forEach(f=>f.addEventListener('change',filter));filter();
</script></html>
'@
    $slots = @{SUMMARY = (E (($summary.GetEnumerator() | ForEach-Object { "$($_.Key): $($_.Value)" }) -join ' · '))
        THRESHOLD = (E "Channel tolerance / 通道容差: $Tolerance · Allowed pixel ratio / 允许差异比例: $($AllowedRatio.ToString('P3',$invariant))")
        FATAL = (E $fatal)
        PLUGINS = $plugins
        CARDS = $cards.ToString()
    }
    $html = [regex]::Replace($html, '__(SUMMARY|THRESHOLD|FATAL|PLUGINS|CARDS)__', [Text.RegularExpressions.MatchEvaluator] { param($match) $slots[$match.Groups[1].Value] })
    [IO.File]::WriteAllText((Join-Path $outputRoot 'index.html'), $html, $utf8)
}
try {
    foreach ($side in @('baseline', 'candidate')) {
        $gallery = if ($side -eq 'baseline') {
            $baseline
        }
        else {
            $candidate
        }
        foreach ($failure in $gallery.errors) {
            $item = if ($side -eq 'baseline') {
                New-Result $failure $null
            }
            else {
                New-Result $null $failure
            }
            $item.message = "$side descriptor inspection failed: $($failure.message)"
        }
    }
    $keys = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
    $keys.UnionWith($baseline.map.Keys)
    $keys.UnionWith($candidate.map.Keys)
    foreach ($key in $keys) {
        $before = $null
        $after = $null
        [void]$baseline.map.TryGetValue($key, [ref]$before)
        [void]$candidate.map.TryGetValue($key, [ref]$after)
        $item = New-Result $before $after
        try {
            Copy-Capture $baseline $before 'baseline' $item
            Copy-Capture $candidate $after 'candidate' $item
            if (($null -ne $before -and $before.status -ne 'passed') -or ($null -ne $after -and $after.status -ne 'passed')) {
                throw "Capture did not pass: baseline=$($before.status) $($before.message); candidate=$($after.status) $($after.message)"
            }
            if ($null -eq $before) {
                $item.status = 'added'
                $item.message = 'No matching baseline / 没有对应基线'
            }
            elseif ($null -eq $after) {
                $item.status = 'missing'
                $item.message = 'Candidate capture missing / 当前预览缺失'
            }
            else {
                Compare-Capture $item
            }
        }
        catch {
            $item.status = 'error'
            $item.message = $_.Exception.Message
        }
        Write-Host "[$($item.id)] $($item.plugin) $($item.layout) $($item.theme) $($item.scenario): $($item.status)"
        Write-Reports
    }
    $finished = $true
}
catch {
    $fatal = $_.Exception.Message
}
finally {
    Write-Reports
}
Write-Host "Comparison: $(Join-Path $outputRoot 'index.html')"
if (!$finished) {
    Write-Warning $fatal
    exit 2
}
if (@($cases | Where-Object status -ne 'passed').Count) {
    exit 1
}
exit 0
