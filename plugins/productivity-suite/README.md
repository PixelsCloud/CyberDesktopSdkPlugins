# Desktop productivity plugins

Seven types, fourteen fixed rectangular footprints. No battery plugin.
All outer cards use the spectrum's 4 logical pixel host padding and face bounds (1, 1, width-3, height-4). Background corner radius is independently configurable per instance as an integer, default 5 with a range of 0–32 logical pixels; 0 gives square corners.
See [concept layouts](../../design/productivity/index.html) and
[actual native render gallery](../../design/productivity/rendered.html).

| Type | Footprints | Interaction |
|---|---|---|
| Focus / countdown timer | 1×2, 2×2 | Start, pause, reset; optional automatic break |
| Tasks | 1×4, 2×4 | Click checkbox; edit multiline tasks; arrows or wheel to browse |
| Event countdown | 1×2, 1×4 | Target date, title, optional annual recurrence |
| Network speed | 1×2, 1×4 | Host upload/download sampling; optional 60-second graph |
| Disk space | 1×2, 2×2 | Choose local drive; background capacity sampling |
| Notes / snippets | 1×4, 2×4 | Click to copy all or individual lines; no clipboard history |
| RSS reader | 1×4, 2×4 | RSS / RDF / Atom; rotate, browse, open original headline |

Every instance saves its own settings. Fonts are shared external assets, never
embedded in packages. Only `zh-CN` and `en` presentation languages are supported.
User text and feed titles are not automatically translated. Runtime timers use
wall-clock deadlines; hiding an instance stops rendering, not elapsed real time.
Reset stops the current timer and returns to the configured focus/countdown time.

Network speed uses the existing host sampler; there is no second system collector.
Disk space uses the host `storage.volume` service and accepts local drive roots.
RSS subscribes to `content.feeds`; the host shares normalized queries, performs
HTTP and parsing off the UI thread, limits concurrency, and retains stale content
on refresh failures. Plugin instances do not own network threads or result polling
timers. RSS title rotation stays local to the card. Article content remains with
its original publisher; `CONTENT` capability is required for content and link commands.

Clicks are cancelled after movement >5 logical pixels or leaving the tile, so
dragging does not complete tasks, reset timers or open links. Notes report
clipboard success only after the OS accepts the data.

## Build and verify

Use the repository's static libcurl and nlohmann/json dependencies through vcpkg.
The public `libraries/content-model` dependency provides URL/text helpers; HTTP,
XML and HTML acquisition dependencies belong to the private host. Compile with
at most 16 jobs and 16 logical CPUs, using `/MT` and the provided scripts.
The root release script builds fourteen DLLs and self-contained protobuf packages
and deploys them to `build/dist`. No debug symbols are shipped.

```powershell
./scripts/build-release.ps1 -SkipTests -Jobs 16
cmake --build build/relwithdebinfo --target cd_productivity_tests cd_productivity_design --parallel 16
./build/relwithdebinfo/cd_productivity_tests.exe
# Real copy verification in a private window station; user clipboard untouched:
./build/relwithdebinfo/cd_productivity_tests.exe --clipboard-isolated
# Shared content provider, SDK and plugin lifecycle verification:
./scripts/verify-content-service.ps1 -Jobs 16
./build/relwithdebinfo/cd_productivity_design.exe plugins/productivity-suite/catalog.json design/productivity
$env:CYBERDESKTOP_VERIFY_PRODUCTIVITY_PLUGINS='1'
./build/dist/cyber_desktop.exe --verify-native-sdk build/productivity-host
```

`tools/GenerateCatalog.py` regenerates footprint descriptors and the concept SVG.
`cd_productivity_design` exports native-rendered PNGs for every footprint in
Chinese, English at 3× DPI and tall rectangular cell geometry. Test binaries are
excluded from normal builds. Release checks verify icon hashes and required
license files; icons come from pinned Lucide 1.55.0 with original SVGs included.
