# Preview gallery baselines

`scripts/compare-preview-gallery.ps1` compares two completed [preview galleries](PREVIEW_GALLERY.md).
It reuses `cd-sdk compare` and its existing WIC decoder/premultiplied RGBA pixel comparison. No plugin DLL is loaded,
no plugin is rebuilt, and no new image library is required. Use Windows and PowerShell 7 with an installed SDK.

```powershell
$sdk = 'D:/CyberDesktopSDK'
& "$sdk/share/cyberdesktop-sdk/scripts/compare-preview-gallery.ps1" `
  -SdkDirectory $sdk `
  -BaselineDirectory 'D:/Preview Runs/baseline' `
  -CandidateDirectory 'D:/Preview Runs/candidate' `
  -OutputDirectory 'D:/Preview Runs/comparison' `
  -Tolerance 3 -AllowedRatio 0.001
```

Open `index.html` directly in a browser. Each row shows baseline, candidate and difference images side by side,
with pixel metrics, versions and logs. Changed/error rows come first. Search and plugin/status filters work offline;
click any image to open it at its original resolution. The entire comparison folder is portable: it contains copies
of its input images and manifests, rather than links back to the original gallery directories.

## Matching and repeatability

The case-sensitive key is the tuple `(plugin ID, layout ID, logical size, theme, DPI, scenario)`. Versions, absolute
plugin directories, case IDs and array order are excluded so builds from different directories can be compared.
Numeric DPI values are normalized. Duplicate keys in either input are rejected instead of being paired arbitrarily;
compare multiple builds of the same plugin in separate runs.

Both inputs must contain a completed schema 1 gallery report without a fatal batch error. Their language, fixed
wall time, UTC offset and animation frames must match. Ordinary failed captures and descriptor failures are accepted
as inputs but remain errors in the result. Incomplete/empty reports, invalid dimensions and ambiguous keys fail
preflight before creating the output directory. Reports are limited to 16 MiB and 4096 input cases each.

Use the same OS/fonts/rendering environment when recording and comparing baselines. The recorded configuration
cannot identify every machine difference or control a plugin's private clock, randomness, files or network access.
The comparison does not normalize font differences, align images, apply perceptual scoring or ignore image regions.

## Thresholds and results

| Parameter | Default | Meaning |
| --- | --- | --- |
| `Tolerance` | `3` | Integer 0–255; a pixel differs when any premultiplied RGBA channel delta **exceeds** this value |
| `AllowedRatio` | `0.001` | Number 0–1; passing requires differing pixels / total pixels **≤** this fraction (default 0.1%) |
| `TimeoutSeconds` | `30` | Per image pair, range 1–120; comparisons run one at a time |

Use `-Tolerance 0 -AllowedRatio 0` for exact equality. The existing CLI calculates the verdict at full precision;
its printed metrics are display values and may be rounded. The opaque red difference map highlights pixels outside
the channel tolerance. A passed case can still have a nonzero maximum delta or mean absolute error.

| Status | Meaning |
| --- | --- |
| `passed` | Both captures decoded and their difference is within the thresholds |
| `changed` | Both captures decoded but their differing-pixel ratio exceeds the allowance |
| `added` | Candidate has a capture with no matching baseline key |
| `missing` | Baseline has a capture with no matching candidate key |
| `error` | Source capture/inspection failed, image is missing/invalid, or the comparison tool failed |
| `timeout` | The owned comparison child exceeded its deadline and was terminated |

Added and missing cases fail the run even if every matched pair passes; they require review before choosing a new
baseline. Renaming a plugin/layout or changing size/DPI produces missing/added pairs. No baseline is automatically
updated or overwritten. Images for unpaired cases receive header/dimension validation; full WIC decoding happens
for matched pairs. PNGs are bounded at 64 MiB and 4096 pixels per dimension. Input image paths must resolve inside
their gallery, including through existing symbolic links or junctions.

## Artifacts and exit codes

- `report.json`: schema version, `kind=preview-gallery-comparison`, thresholds, completion, counts, and case results
  with input IDs/versions, relative images, metrics (`pixels`, `differing`, `ratio`, `maximumDelta`, `meanAbsoluteError`),
  exit codes, durations and error messages. Original capture configuration is retained.
- `failures.json`: all cases whose status is not `passed`, including added/missing entries.
- `baseline/`, `candidate/`, `diff/`: numbered PNGs; user metadata is never used to construct output filenames.
- `inputs/`: copies of the original two gallery manifests. `logs/`: stdout/stderr for executed comparisons.
- `index.html`: escaped, bilingual, offline comparison viewer without remote scripts, fonts or services.

Output must be a new directory outside both inputs. Reports are refreshed after each pair. `completed=true` means
all planned comparisons finished, not that they passed. Exit **0** means every case passed, **1** means completed
with changed/added/missing/error/timeout cases, and **2** means a setup or fatal execution error. Invalid PowerShell
parameter binding also returns nonzero. A preflight failure produces a console error without an output folder;
fatal errors after setup are recorded in the report. Only this script's comparison child is stopped on timeout or
cleanup. Keep the process alive until completion to obtain final artifacts.

## Verification

From SDK source, run the focused integration check against an installed kit and a successful real gallery:

```powershell
./tests/PreviewComparisonIntegration.ps1 -SdkDirectory 'D:/CyberDesktopSDK' `
  -GalleryDirectory 'D:/Preview Runs/baseline' -OutputDirectory 'D:/Preview Runs/verification'
```

The check covers reordered real captures, different paths/versions, portable reports, metadata escaping, exact
threshold boundaries, corrupted images, added/missing/failed captures, incompatible configuration, duplicate keys,
path containment, no overwrite and timeout cleanup. Small deterministic image fixtures use Windows System.Drawing;
the isolated timeout probe uses the Windows .NET Framework C# compiler. These are test-only dependencies.
