# Native drawing starter guide

Use a developer kit installed from the current SDK source. These seven complete projects compile without Qt, protobuf or desktop source; they use the same public Canvas and typed C ABI adapter as the migrated plugins. SDK 1.3 includes all seven presets; SDK 1.2 archives contain the original three.

| Preset | Behavior | Scheduling and state |
| --- | --- | --- |
| `static` | Themed card, selectable system font, accent/opacity, click counter | No timer; versioned counter state and instance-local redraw |
| `data` | CPU/memory card from `system.telemetry` v1 | Owned host watch, rounded visible values, no unchanged redraw, unsubscribe while hidden |
| `animated` | Moving decoration, click play/pause, adjustable speed including zero | Owned update loop, hidden time excluded, versioned phase state |
| `info` | CPU ring, memory metric, status and reconnect action | Host system.telemetry subscription; rounded values suppress unchanged redraw |
| `tasks` | Four toggleable task rows | Stable IDs, checked semantics, pointer/keyboard input and saved completion state |
| `media` | Current title/artist, previous/play/pause/next and playhead | Host media.playback subscription; capability-gated, correlated asynchronous commands |
| `calendar` | Real Gregorian month, weekday headings, date selection and navigation | C++20 chrono date arithmetic, local-day refresh and saved month/selection |

The `static` preset lives in `templates/minimal` for compatibility with the starter folder name. The data preset declares SYSTEM_INFO and its required service. Preview supplies deterministic mock values; actual collection stays in the host.

## Create and build

Open an MSVC x64 developer terminal. Set `$sdk` to the extracted kit, and run the installed script:

```powershell
$sdk = 'D:/CyberDesktopSDK'
& "$sdk/share/cyberdesktop-sdk/scripts/new-plugin.ps1" `
  -Destination 'D:/My Plugins/First Card' -Id 'com.example.first-card' `
  -Name 'First Card' -Template static
cmake -S 'D:/My Plugins/First Card' -B 'D:/My Plugins/First Card/build' `
  -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$sdk"
cmake --build 'D:/My Plugins/First Card/build' --parallel 10
$package = 'D:/My Plugins/First Card/build/plugins/com.example.first-card'
& "$sdk/bin/cd-sdk.exe" test $package
```

The destination must be missing or empty. The script updates the ID in C++, CMake and textproto, quotes the display name, validates the descriptor and copies only the five known source/license files (plus `Card.h` and `ServiceModel.h` for component presets). Existing files are never overwritten. When using the script directly from an SDK source checkout, pass `-SdkDirectory` pointing to an installed kit.

Change `-Template` to any preset in the table. For example, `-Template tasks` generates an editable list card. The four component presets share `templates/components`; the generator sets `CD_COMPONENT_KIND` in the generated CMake file. They copy all required example code into the new project, without retaining a path back to the SDK's example sources. Alternatively copy the corresponding template folder and edit the ID in `Plugin.cpp`, `CMakeLists.txt` and `plugin.textproto` consistently. The public `cyberdesktop_add_plugin()` helper owns `/MT`, compiler options, descriptor compilation, declared assets and the per-ID output directory. Original SDK, Yoga and tweeny notices are staged with each template package.

Component presets expose `language`, `show_background` and `data_source` (used by info/media). Edit `Card.h`: `arrange()` supplies both control geometry and semantic names, drawing functions compose public widgets, and `activate()` is the business-action hook. `ServiceModel.h` owns info/media subscriptions. The generator declares SYSTEM_INFO/system.telemetry or MEDIA_CONTROL/media.playback only for the corresponding preset. Update declarations too when changing the kind manually.

The default `host` source uses real services in the desktop, and deterministic mock services in the SDK preview. Select `demo` in settings for the original offline models. The preview never sends commands to real players. Live values and pending requests are never restored from saved state. Hiding cancels subscriptions and deadline timers; resuming fetches fresh data. Loading, empty data, unavailable service, invalid data, initial-delivery timeout and command failure have distinct feedback. Reconnect retries the subscription; commands require an explicit user action, respect player capabilities and wait for correlated acknowledgement. Initial data and command acknowledgements have a 15-second deadline. There is no automatic command retry or polling inside rendering.

## Preview and iterate

```powershell
& "$sdk/bin/cd-sdk-preview.exe" $package --size 320x200 --dpr 2 --theme dark
& "$sdk/bin/cd-sdk-preview.exe" $package --size 320x200 --dpr 3 --theme light `
  --time 1709208000000 --screenshot 'preview.png'
& "$sdk/share/cyberdesktop-sdk/scripts/dev-plugin.ps1" `
  -SourceDirectory 'D:/My Plugins/First Card' `
  -BuildDirectory 'D:/My Plugins/First Card/build' -PluginDirectory $package `
  -PreviewExecutable "$sdk/bin/cd-sdk-preview.exe" -Jobs 10
```

The loop uses shadow DLLs; a failed C++ build leaves the current preview working. Resource edits coalesce on a worker and preserve state. See [preview commands](PREVIEW.md) for fixed time, replay, mock JSON, state files and F1 diagnostics.

For a deterministic data capture, create `data.json` containing `{"cpuPercent":62}` and add `--mock data.json`. For the animated preset, compare `--frames 1` and `--frames 30` at the same fixed time. Setting speed to zero or hiding the instance stops its visual work. For the static preset, click once, save with `--state state.pb`, then reopen using the same state file.

The drawing coordinate system is logical pixels. Call `Canvas::frame()`/`render()` with the actual host frame and safe content; transparent text uses grayscale AA. `layoutChanged()` only updates geometry, input updates model state, and drawing reads it. Avoid scans, HTTP requests and image decoding inside `render()`. The system-font choices are deliberately self-contained; custom external font collections use the documented native extension instead.

## Package and install

```powershell
& "$sdk/bin/cd-sdk.exe" pack $package 'first-card.cdpkg'
& "$sdk/bin/cd-sdk.exe" validate 'first-card.cdpkg'
& "$sdk/bin/cd-sdk.exe" inspect 'first-card.cdpkg'
```

Import the single `.cdpkg` through CyberDesktop plugin settings. The host verifies package hashes and ABI, extracts declared files, loads the native DLL, and stores per-instance settings/state separately. Keep `plugin.textproto` authoritative in development; generated `descriptor.pb` is rebuilt by CMake. Bump the plugin version when distributing updates, and retain an earlier package and state backup before replacing a deployed version.

The installed **Canvas Workbench** example adds retained layout, asynchronous PNG resources, Lucide icons, buttons, sliders, keyboard/accessibility, diagnostics and state-restoring hot reload. Its **Component example** setting switches between the original canvas and the four new cards. Each page retains its own model; inactive pages release timers and keyboard registration. Workbench state format 2 includes the four card states and still reads format 1. The cards use exactly the same source as the generator presets. Preview hosts without the optional Lucide service use text fallbacks; the desktop host supplies the real icons.

`scripts/verify-starters.ps1` creates all seven projects from an installed kit, builds with at most 10 jobs, captures 42 theme/DPI previews, compares changed telemetry and empty-player views, replays task/calendar actions, restores state in new processes, and validates packages and license notices. The Workbench preview contract additionally tests live and demo page switching, semantic commands, service failures/deadlines, hidden cleanup, focus recovery and legacy state. Review [API contracts](CONTRACTS.md) and the [release/rollback checklist](RELEASING.md) before distributing a kit or plugin.
