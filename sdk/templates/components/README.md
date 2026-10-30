# Shared component starter

This standalone C++20 project uses the installed CyberDesktop SDK. Build with
`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<sdk>`
and `cmake --build build --parallel 8` in an MSVC x64 developer terminal.

`CD_COMPONENT_KIND` in CMake selects information (0), tasks (1), media controls
(2), or calendar (3). `Plugin.cpp` exports the typed C ABI adapter. `Card.h` is
editable example code shared with Canvas Workbench, not a required application
framework. Delete the other example branches when specializing your plugin.

Info uses `system.telemetry` to display CPU and memory. Media uses
`media.playback` to display and control the current player. The generator declares
SYSTEM_INFO or MEDIA_CONTROL and the corresponding required service only for
those presets. When manually changing CD_COMPONENT_KIND, update these declarations.
`ServiceModel.h` owns subscriptions, copied values, request correlation and a
15-second initial-delivery/command deadline. Loading, empty, unavailable, error
and timeout states remain distinct. Retry reconnects; it never replays a failed
command automatically. Keep collection and slow operations outside `render()`.

`data_source=host` is the default; `demo` selects the old local interactive model.
The SDK preview supplies mock host data and never controls real applications.
Install in the desktop host to use real system/player services. Media controls
respect the current player's capability flags and wait for host acknowledgement.

Paint and hit testing share `arrange()`. Controls provide Lucide icons when the
host offers them, keyboard focus and accessibility, cancelled gestures on
resize/hide, and translated names. `saveState()` uses a validated versioned
format. Live service values and outstanding commands are not persisted. Hidden
cards release watches and deadlines; resuming fetches current values. Identical
display values do not invalidate the texture. Media pauses its demo clock while
hidden; calendars refresh at local day changes. `language` and `show_background`
settings are instance-local. Small allocations show a resize hint instead of overflowing.

Run `cd-sdk test build/plugins/<id>`, preview with `cd-sdk-preview`, and package
with `cd-sdk pack` as described in the SDK starter guide. Preserve LICENSE and
the generated dependency notices when distributing your plugin.
