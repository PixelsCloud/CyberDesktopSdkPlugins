# Native plugin development preview

`cd-sdk-preview DIRECTORY` loads one C ABI 1 plugin per process. The plugin remains native C++. Basic preview requires no Qt; the optional full
character/model preview ships its declared Qt/Cubism worker dependencies. See [service coverage](PREVIEW_SERVICES.md). Dimensions are logical pixels; output uses the requested actual DPI. Each physical dimension is limited
to 4096. Resizing and monitor DPI changes rebuild the target.

```powershell
cd-sdk-preview package --size 320x360 --dpr 2 --theme light --language zh-CN
cd-sdk-preview package --size 320x360 --time 1709208000000 --utc-offset 0 --frames 60 --screenshot fixed.png
cd-sdk-preview package --services mock --mock data.json --overlay --diagnostics nodes.json --screenshot debug.png
cd-sdk-preview package --scenario empty --size 320x200 --dpr 2 --screenshot empty.png
cd-sdk-preview package --export-dir ./preview-images
cd-sdk compare baseline.png candidate.png difference.png 3 0.001
```

The last command permits a maximum channel delta of 3 and at most 0.1% differing pixels. It writes an opaque red
difference map, reports maximum delta/mean absolute error, and returns nonzero outside tolerance. Compare equal
physical dimensions. Inputs use WIC and a 64 MiB file limit; PNG screenshots preserve transparency; BMP compatibility
can discard alpha. Encoding is excluded from plugin drawing statistics.

## Services and deterministic inputs

The default is explicitly **mock** data, silent simulated ambient audio, and host-driven timers. `--services none`
disables data capabilities; required unavailable services fail at creation. For real system/media/weather services,
install a built package in CyberDesktop or open it in Plugin Studio. The standalone preview does not provide the
application's live services. Plugin-private network/file access remains its responsibility; mock mode is not a sandbox.

`--mock FILE.json` accepts numeric `cpuPercent`/`gpuPercent` (0–100), `temperature` (−100–100), `humidity` (0–100),
`aqi` (0–1000), `dataError` (0–10000), and boolean `hasNetworkData`/`hasAudioDevice`/`hasMediaSession`. Other fields
and wrong types are errors. JSON uses the existing protobuf library; files are limited to 1 MiB.

`--scenario normal|loading|empty|error` selects a named service fixture, also available from the interactive
**Preview → Service scenario** menu. It requires mock services and takes precedence over conflicting mock JSON
values. Normal preserves the existing mock behavior. The other fixtures affect telemetry, media playback,
location, weather and air quality; private plugin I/O, input statistics and audio are unchanged.

| Scenario | System / media | Location / weather / air |
| --- | --- | --- |
| normal | Normal mock samples and player | Normal mock result / JSON overrides |
| loading | Withhold producer callbacks | Withhold producer callbacks |
| empty | No valid telemetry / no player | Unavailable status, no valid data |
| error | Subscription returns an error | Error status, no cached valid data |

Loading leaves timers running, allowing plugins to show their own timeout state (the component starters use
15 seconds). These fixtures cannot add error UI to a plugin that does not implement it. Selecting a scenario
recreates the preview instance with its settings and business state restored; current footprint, size, render
DPI and pause status are retained. Live services are never selected by these controls.

`--time UNIX_MS` fixes wall time independently of the monotonic animation clock. Fixed time defaults to UTC;
`--utc-offset` changes the local calendar offset in minutes (−840…840). Otherwise the system wall clock origin and
current offset are used. Bind `<sdk/Environment.h>` and use `snapshot()`/`localTime()` rather than reading the machine
clock directly. Legacy time, randomness and private I/O must opt in before their output becomes deterministic.
`--language` supplies a BCP47 environment tag and applies a compatible descriptor language setting, if present.

Headless `--frames N` advances 1/60-second steps, then settles image completion without repeatedly progressing
animation time. Fix time, data, frames, theme, language and DPI for visual baselines. Fonts, text shaping and codecs
can differ between machines; keep platform-specific baselines.

`--replay FILE.json` accepts `{"events":[{"frame":1,"key":1},{"frame":2,"key":6}]}`. Keys 1…10 are Tab, Left,
Right, Up, Down, Enter, Space, Escape, Home, End. Pointer entries contain `kind` (C ABI input kind), logical `x`/`y`,
optional `buttons` (default left), and `wheel`. Keyboard delivery requires host-granted focus; headless replay grants
it explicitly. This is a development fixture, not a persisted business action format.

## Interactive inspection

For multiple plugins and combinations, use the [batch gallery script](PREVIEW_GALLERY.md). It reads declared
layouts, runs isolated captures and writes a browsable HTML index plus machine-readable success/failure reports.

- The **Preview** menu exposes named service scenarios, five common logical sizes, export, settings, pause and
  debug bounds. Labels follow the preview language (Chinese or English).
- F2 cycles normal/loading/empty/error; F3 cycles 160×160, 320×200, 320×320, 320×480 and 480×320 logical pixels.
  These are allocation sizes, not grid footprint declarations; F7 still changes the plugin's actual footprint.
  Nonclient window chrome follows monitor DPI independently of the requested render scale.
- F9 exports one transparent PNG of the current plugin content. It excludes checkerboard, window chrome and
  debug overlays, even if F1 is enabled. Export does not advance the simulation clock or resume a paused plugin.
  The default directory is `PACKAGE_PARENT/preview-exports/PLUGIN_ID`; override it with `--export-dir`.
  Timestamped filenames include physical dimensions, theme and scenario and do not overwrite earlier exports.
  The window title and stdout show the saved path. Export inside the watched package is rejected to prevent
  a screenshot from triggering resource reload. CLI `--screenshot` retains its existing explicit-overlay behavior.
- F1 toggles blue node bounds, yellow clips, purple hit bounds, white focus and counters. It composites onto the
  retained plugin image without requesting a plugin frame. Hit diagnostics are bounding rectangles.
- F5 reloads the native module with settings/state restoration. F6 opens descriptor settings.
- F7 switches footprint layouts. F8 pauses/resumes, revoking keyboard focus and drag capture when paused.
- Tab/arrows/Enter/Space/Home/End/Escape use the same optional keyboard service as production.

`--diagnostics FILE.json` writes detailed headless diagnostics, resource paths and complete errors. Counters belong to
the plugin; overlay drawing and allocations are separate. Inclusive times cannot be added together. The preview's
UI polling timer does not request a plugin texture when content is clean.

## Resource reload and recovery

The background watcher uses overlapped `ReadDirectoryChangesW`, coalesces changes for 250 ms, and retains at most
64 path names. It watches PNG/JPEG/BMP/GIF/WebP/SVG, TTF/OTF, WAV/FLAC/OGG, JSON and textproto. The selected
decoder/backend must support the format. DLL/build/git/state files are ignored. `--watch-ms N` keeps a headless
capture alive for reload checks. Put capture outputs outside the watched package.

Reload captures settings/state, destroys the instance, joins its workers, unloads the module, then creates and
restores it. This invalidates module-private and shared image caches. Plugins must release subscriptions and workers
before destroy returns. Load/decode failures report path/error, retain the last good frame and state, and accept a
corrected resource or F5 retry. Resource reload needs no C++ build. Changing plugin identity requires a new process.

`--state FILE.pb` saves changed settings/state at most twice per second and on graceful close, using protobuf and
atomic replacement. Limits: 5 MiB file, 2 MiB native state, 512 settings, 4096 bytes/string. Other plugin IDs are
rejected; version migration belongs to `restore_state`. Removed setting IDs are skipped; incompatible values fail
explicitly and retain the disk file. Inspect with `cd-sdk state FILE.pb`. Keep personal state out of source control.

## Incremental C++ builds

Configure with the installed SDK, then run PowerShell 7 in an MSVC x64 developer terminal:

```powershell
pwsh SDK/share/cyberdesktop-sdk/scripts/dev-plugin.ps1 `
  -SourceDirectory ./my-plugin -BuildDirectory ./my-build `
  -PluginDirectory ./my-build/plugins/org.example.plugin `
  -PreviewExecutable SDK/bin/cd-sdk-preview.exe -Jobs 16
```

`-BuildTarget` optionally selects the descriptor/assets target. Source changes are coalesced before incremental
CMake builds, capped at 16 parallel jobs (default 2), with inherited compiler CPU affinity. The original build output stays unlocked because the preview
uses a shadow package. Build failure preserves the running preview. Unchanged DLLs update shadow resources;
changed DLLs stage the inactive fixed slot, gracefully close the old process and restart with the same state file. Failure
to close within five seconds cancels replacement and leaves the old process alone. Paths use argument lists;
copies/removals stay under the work directory. Two fixed shadow slots are reused; only an inactive, SDK-owned
slot is refreshed. Close an existing preview before restarting its controller. This helper does not rebuild the desktop. `-Once`/`-MaxBuilds` support bounded automation.

`cyberdesktop_add_plugin(... ASSETS art.png subfolder/data.json)` stages changed assets without recompiling the DLL.
`examples/Workbench.cpp` demonstrates images, keyboard, animation, settings and versioned state. The technical PNG
fixture is original CyberDesktop artwork, under the SDK MIT license. `tests/PreviewIntegration.ps1` verifies real
process restart and resource corruption recovery.

## Plugin catalog images

Add `preview: "preview.png"` to `PluginDescriptor` and place that static image inside the plugin directory before
`cd-sdk pack`. The protobuf package embeds it with other resources; unsafe/missing paths are rejected. Prefer real
default plugin content at 2× resolution, without personal photos or live data. The desktop catalog loads previews
asynchronously, preserves aspect ratio, and never executes plugins or requests network data to generate list images.
It falls back to the embedded `icon`, then a text placeholder. Catalog preview images are limited to 8 MiB encoded
and 4096×4096 pixels. Preview metadata is optional and does not change the native C ABI.

The incremental helper passes an owned `--stop-event Local\\CyberDesktop.SdkPreview.NAME` event to each process.
Signalling it requests a graceful state-saving exit in interactive and headless modes. It is a Windows session-local
development control, not a production plugin capability. `session.json` in the work directory records the current
PID, generation, shadow package, state path and event name for inspection. `tests/DevLoopIntegration.ps1` exercises
the installed SDK, including asset update, failed build, successful C++ restart, state restore and bounded shadow slots.
