# Batch preview galleries

`scripts/preview-gallery.ps1` runs the existing native preview tool across plugin layouts, logical sizes,
themes, DPI scales and service scenarios. It does not compile plugins, collect live services or change the desktop.
Use PowerShell 7 and a developer kit installed from current SDK source.

```powershell
$sdk = 'D:/CyberDesktopSDK'
& "$sdk/share/cyberdesktop-sdk/scripts/preview-gallery.ps1" `
  -SdkDirectory $sdk -PluginDirectory 'D:/Plugins/Info','D:/Plugins/Media' `
  -OutputDirectory 'D:/Preview Runs/first' `
  -Sizes 320x200,320x320 -Themes light,dark -Dpi 1,2 `
  -Scenarios normal,loading,empty,error -Language zh-CN -Jobs 2
```

Open `index.html` directly in a browser; no server or network is required. The gallery provides text search and
plugin/result/theme/scenario filters, transparent thumbnails, full-resolution image links and process logs.
Labels are bilingual. Names and errors are HTML-escaped; the gallery contains no external scripts or fonts.

## Inputs and defaults

| Option | Default / behavior |
| --- | --- |
| `PluginDirectory` | One or more explicit built plugin directories; no recursive discovery |
| `OutputDirectory` | Required new directory outside all watched plugin roots; never overwrites an earlier gallery |
| `Layouts` | All layouts read through `cd-sdk inspect DIRECTORY`; supplied IDs must exist in each plugin |
| `Sizes` | `320x200,320x320` logical pixels |
| `Themes` | `light,dark` |
| `Dpi` | `1,2`; supported range 0.5–4; each physical dimension must remain ≤4096 |
| `Scenarios` | `normal,loading,empty,error` |
| `Language` | `en-US`, accepts a BCP47 tag |
| `TimeMs` / `UtcOffsetMinutes` | `1709208000000` / `0`, repeatable wall time independent of animation time |
| `Frames` | `2`; use more frames to examine animation or a loading timeout |
| `Jobs` | `2` preview processes concurrently, range 1–4; this is not compilation parallelism |
| `TimeoutSeconds` | `30` per descriptor inspection/capture, range 1–120 |
| `MaxCases` | `512`, range 1–4096; checked before launching captures |

The default matrix contains 32 captures per layout. Repeated dimension values are deduplicated. A CLI inspection
uses the same validated protobuf reader as preview, preferring editable `plugin.textproto` over `descriptor.pb`,
and never loads the DLL. Package inspection through `cd-sdk inspect FILE.cdpkg` continues to work.

Each capture starts an isolated hidden process with a structured argument list, including its chosen footprint.
Output names use numerical case IDs, so layout names cannot become paths and identical plugin IDs from different
directories cannot collide. Each process uses default plugin settings and a fresh instance; the script never shares
or overwrites an instance state file. Logical size does not change the descriptor's grid cells.

Named scenarios affect the mock services described in [PREVIEW.md](PREVIEW.md). A plugin must implement its own
loading/error display; a rendered error-state card can be a successful capture. Scenarios do not control private
plugin I/O or guarantee deterministic output from plugins that read their own clock or randomness.

## Reports and failure handling

- `report.json`: schema version, configuration, completion flag, summary, and every case's plugin/version/root,
  layout, size, theme, DPI, scenario, status, duration, renderer exit code, paths and exact argument array.
- `failures.json`: failed, timed-out and cancelled cases, with the stage (`inspect` or `capture`) and error detail.
- `images/`: one transparent PNG per successful capture. A zero exit code is accepted only if the PNG header and
  physical dimensions match the request. This header check supplements the renderer result; it is not a full
  independent image decode or a visual regression comparison.
- `logs/`: stdout/stderr for each inspected failure or executed capture. `descriptors/` retains inspected metadata.
- `index.html`: a portable offline index using relative links. Copy the entire output folder when sharing it.

The report and index are updated as cases finish. A descriptor failure or crashed renderer does not stop other
plugins/cases. A timeout stops only the process tree launched for that case. Script cleanup stops its remaining
children and marks uncompleted cases cancelled. A matrix-limit or other fatal setup failure is recorded separately.
`completed=true` means all planned work finished, not that every capture passed. Exit code 0 means the batch
completed with every case passed; failures/timeouts return nonzero, allowing CI to fail without losing the report.

There is no resume/overwrite mode in this version. Use a new directory for each run. For focused checks:

```powershell
& "$sdk/share/cyberdesktop-sdk/scripts/preview-gallery.ps1" `
  -PluginDirectory 'D:/Plugins/Info' -SdkDirectory $sdk `
  -OutputDirectory 'D:/Preview Runs/focused' -Layouts 2x4 `
  -Sizes 320x200 -Themes dark -Dpi 2 -Scenarios error
```

`tests/PreviewGalleryIntegration.ps1` validates an installed kit using real information/media DLLs, multiple layouts,
48 captures, Unicode/quoted paths, escaped metadata, independent failures, case limits, timeouts and invalid output.
Its isolated faulty-renderer fixture is compiled with the Windows .NET Framework C# compiler; the production
gallery script requires no C# compiler or additional dependency.

## Compare with a baseline

Use [preview baseline comparison](PREVIEW_COMPARISON.md) after recording a second gallery. The comparison matches
semantic case keys independently of directory, version and numbering, and exports portable before/after/difference
images with thresholds, added/missing cases and separate execution errors. It never updates a baseline automatically.
