# Native plugin contract

## Environment and typed C ABI entry point

`sdk/Environment.h` binds optional `host.environment` v1. Its 64-byte snapshot includes UTC Unix milliseconds,
monotonic milliseconds, local UTC offset, light/dark/unknown theme, and a 32-byte terminated BCP47 locale. Values
are copied synchronously on the UI thread; the service creates no subscription. `localTime()` uses C++20 calendar
conversion, including leap days and epoch-crossing offsets, without changing process timezone. Unsupported/invalid
host snapshots fall back to system time/locale with unknown theme. Fixed preview wall time does not stop animation
time. This is an optional table; C ABI 1 and its existing structure sizes remain unchanged. See [preview](PREVIEW.md).

`sdk/cpp/Plugin.h` provides `PluginAdapter<T>::query(abi, out, size, id, version)`. T owns its RAII resources, exposes
`Settings settings`, and implements `CdResult render(const CdFrame&)`. Optional hooks are `input`, `layoutChanged`,
`setPaused`, `applySettings`, `invalidate`, `saveState()->vector<uint8_t>` and
`restoreState(span<const uint8_t>)->CdResult`. The adapter catches exceptions at C callbacks, suppresses duplicate
pause transitions and paused input, bounds native state at 2 MiB, and delegates settings validation atomically to
Settings unless overridden. T still owns timer cancellation, worker joins, state versioning and layout validation.
The adapter never puts C++ objects across the exported C table. IDs/version strings passed to query need static
lifetime; instance strings retained from the create context must be copied. The workbench is a complete usage example.

`sdk/List.h` adapts `CyberCanvas/List.h`: bind once, supply stable IDs/heights/revisions with `items()`, arrange the viewport, and draw visible rows with a callback. `update(id, revision)` changes one item without rebuilding offset geometry; offscreen revision updates do not request a frame. Pass the safe-area origin to `input()`. The adapter converts Windows wheel units (120 per notch) into logical scroll distance and reuses the optional capture service. Keep advancing while `active()` and cancel on hide/layout changes. Row actions return stable string IDs; use the ID to resolve current business data.

`sdk/Scene.h` adapts the retained `CyberCanvas/Scene.h` API. Bind once with the create context, add nodes once, update them through setters, arrange in logical coordinates at the actual DPI, then draw into each requested fresh frame. `SceneEvent` returns business keys and optional values. Pass safe-area origin to `input()` when the Canvas frame uses local safe-area coordinates. Geometry changes automatically cancel capture; hiding or destroying an instance must also cancel. The adapter coalesces per-instance redraw requests. Advance feedback on host time only while `active()`; there are no engine timers or worker threads.

CyberCanvas 0.9 supports `remove(node)`, `clear(parent=root)` and `move(node, parent, before=root)` without replacing the scene. Move before a direct child of the destination, or omit `before` to append; a no-op returns false and stays idle. Root cannot be removed/moved and cycles are rejected. Surviving handles, values and explicit themes persist; inheritance, layout, drawing and hit priority follow the current tree. Deleted handles are never reused and their subtrees release capacity/resources immediately. The 4096-node limit counts live nodes including root. `contains/parent/children/size` query the live structure. Mutate only outside measurement/drawing callbacks, arrange before input/drawing, and stop using deleted handles. Structural changes release optional host capture automatically. See the engine guide and `examples/Dynamic.cpp` for a complete example.

`CyberCanvas/Flex.h` provides renderer-independent automatic layout backed by static Yoga. `CyberCanvas/Components.h` adds measured text, wrapping rows, icon labels, title/action headers and labelled sliders. `CyberDesktopSDK::SDK` links `CyberCanvas::Layout` automatically. Retain layout trees, call `calculate()` when geometry changes and `invalidate()` when measured content changes; cancel pointer capture before replacing control geometry. Installed kits bundle the layout libraries and Yoga MIT notice; public C ABI layouts do not change.

## Background images

`sdk/Images.h` exposes UI-thread resource slots backed by a shared, independent `CyberCanvas::Resources` store. Bind with the instance create context, call `set(slot, path, revision=0, maximumEdge=4096)`, and pass `get(slot)` to `Scene::imageResource()` during rendering. File I/O and WIC decoding happen on a bounded worker; completion is polled through a temporary host timer and delivered only on the host thread. `onChange()` may invalidate a scene; without a callback, the adapter requests this instance's redraw. `pause(true)` suspends delivery and timers, and resume delivers the latest result. Destruction unsubscribes; unbound previews call `poll()` explicitly.

Snapshots have Empty/Loading/Ready/Failed/Cancelled states and immutable premultiplied BGRA pixels. Equal path/revision/maximum-edge keys share decoding; bump revision to reload a replaced file. Different instances' cancellation is independent. Store defaults are 64 MiB decoded pixels, 64 MiB input files and 128 entries. Live handles and snapshots pin decoded memory; new loads fail rather than exceed the budget. Canvas uploads have a separate 64 MiB/64-image cache and support contain/cover/stretch and sprite cells. The legacy filename API retains synchronous behavior. In-progress WIC operations are not forcibly interrupted; final store destruction joins the worker. See the engine guide for ownership and limits; no public C ABI structures change.

## Controls and icons

`CyberCanvas/Widgets.h` supplies `Button`, `IconButton`, `Toggle`, `Slider`, `ProgressBar`, `WidgetTheme` and `WidgetState`. Use the same descriptor for `region()` and `draw()`; keep values in the plugin model. Feed input through `sdk/Interaction.h`, pass its result to `clicked()`/`change()`, synchronize feedback with `WidgetState::sync`, and schedule local redraws while its feedback is active. Cancel input and clear feedback when control identity/layout changes.

`services/Icons.h` defines optional `ui.icons` v1. Its UI-thread `raster` callback writes a bundled Lucide icon to caller-owned premultiplied RGBA `CdFrame` pixels, at the requested device pixel ratio. Each physical edge is limited to 256 pixels, names to 64 ASCII lowercase letters/digits/hyphens, and colors use `0xRRGGBBAA`. Unknown names return `CD_ERROR_UNSUPPORTED`; no capability is needed. Existing ABI 1 structures stay unchanged.

Bind `sdk/Icons.h` to the creation context and pass `icons.painter()` to `IconButton::draw`. The host uses `CyberGlass/Icons.h`; plugins do not link Qt or implement an SVG parser. The SDK source cache and Canvas upload cache each hold at most 64 entries, keyed by content identity, physical size and DPI. Provide a readable text fallback for older hosts and standalone previews. Low-level `Canvas::pixels` uploads immutable RGBA data: use a new key whenever its contents change.

## Optional pointer streaming

`services/Interaction.h` exposes `ui.interaction` version 1. Query through `CdServiceHostApi::query_service`, then call `enable(context, 1)` during creation. Discovery alone does not enable streaming. Calls run on the UI thread. No extra capability is required because the service controls only this instance's gesture.

Streaming sends real press/move/release events. Call `capture(context, 1)` synchronously inside a left press to reserve a slider gesture; otherwise the host may start dragging the tile and sends `CD_INPUT_CANCEL`. Captured moves and release continue outside the tile. `capture(context, 0)` releases the claim. The host cancels on lost capture, hide and layout change. Ordinary buttons should not capture. Existing ABI 1 layouts stay unchanged; Cancel is additive event 6.

The C++ `Interaction` adapter combines this service with the standalone `cybercanvas::InputRouter`. Call `cancel()` before changing control identity/layout or hiding/destroying the instance. Do not reuse a target ID for another item during a press. Old hosts retain stationary clicks. The router and feedback never schedule redraws: request a local redraw only when state changes, and unsubscribe transient frames after the final frame.

`PluginApi.h` defines the public DLL boundary; `services/DataServices.h` defines the versioned optional data tables. Both are usable from C or C++, with x64 layout and `__cdecl`. Export exactly `cd_plugin_query`. Query ABI 1, validate output sizes before writing, and return matching descriptor identity. Every required function must be present. Never throw C++ exceptions across a callback.

SDK 1.1 service discovery, lifecycle, units, owned C++ wrappers, errors and examples are documented in [Data services](DATA_SERVICES.md) and the [中文指南](DATA_SERVICES.zh-CN.md).

The plugin creates its opaque instance and must free it in its own destroy function. The host never deletes a plugin C++ object. Struct/pointer data are borrowed for the current call unless explicitly stated otherwise. Host services/context remain alive until destroy returns. Copy create-context strings if retaining them.

Frame pixels are premultiplied RGBA8, physical dimensions/stride, with explicit capacity and DPR. Input and safe layout coordinates are logical pixels. The host owns/clears the destination buffer. Render synchronously; do not retain or resize/free the pointer. Keep alpha zero outside content; desktop materials and animations belong to the host.

Layout callbacks provide footprint cells, topology, rotation, canvas/safe area, DPR and text/accent colors. The host clips the final footprint. Do not read private desktop classes, window handles or GPU devices.

Create/render/input/layout/pause/state and service registration happen on the UI thread. Data/timer callbacks are on that thread and borrowed for their duration. Work elsewhere must not access shared UI state unsafely. `request_redraw` may be called from a worker, but all work must finish before destroy; no callbacks after destruction.

System updates are clamped to at least 500 ms. Timer interval is 16 ms to 1 hour. Audio subscriptions use FFT band count 1..128, or zero for PCM. PCM count is frames, with `count * channels` samples; FFT count is bands and sample rate/channels are zero. All subscriptions yield tokens and must be released. Paused instances stop their own work and the host stops data/timer distribution.

State is instance-local. Ask for size using NULL/0; host allocates; save copies into that buffer. State is limited to 2 MiB. Empty/corrupt/unsupported state must safely restore defaults or return an error. Repeat pause/resume without creating duplicate subscriptions.

All SDK tools/examples use /MT and static vcpkg dependencies. C ABI ownership keeps their allocations inside their own module. The package runtime likewise exposes opaque handles and borrowed UTF-8 views; close handles inside that runtime, never free their views in the caller.

The ABI uses explicit 8-byte struct packing; the header restores the caller's packing afterward. Button/modifier values are `CD_BUTTON_*` and `CD_MOD_*` constants, independent of Qt. Wheel delta uses Windows units (120 per notch). Timer callback time is monotonic milliseconds, not wall-clock time.

## Input statistics extension

Declare `capabilities: INPUT_STATS`. Check `host->struct_size >= sizeof(CdInputStatsHostApi)` before casting. The extended host is 80 bytes and preserves the 56-byte host and 72-byte media prefixes at offset zero; ABI 1 remains unchanged. `subscribe_input_stats(context, callback, user, &token)` delivers borrowed 72-byte `CdInputStats` snapshots on the UI thread. Copy retained data and release the token through the original `unsubscribe` before pause/destroy. The host's Raw Input registration belongs only to the application: plugins must not independently register keyboard/mouse Raw Input because each device class allows only one target window per process.

`keyboard_today`, `mouse_today`, `keyboard_total`, `mouse_total`, `keyboard_session` and `mouse_session` are uint64 aggregate counters. Keyboard counts are physical press transitions, suppressing autorepeat independently per keyboard and extended scan code. Mouse counts include left/right/middle/X1/X2 DOWN transitions; double clicks count twice, wheel/movement do not count. No typed text, key identities, pointer locations or individual-event history cross the ABI or enter storage. `day` is local YYYYMMDD; today resets at local midnight, totals persist in local AppData, session starts when the host first enables recording. `sampled_at_ms` is a monotonic Windows timestamp. `CD_INPUT_STATS_ACTIVE` marks a running receiver; `CD_INPUT_STATS_STORAGE_ERROR` marks an inability to read/save aggregate state. Do not display an inactive receiver as confirmed zero activity.

The host delivers only changed values, at most once per second. Display unsubscribe/fullscreen pause never stops recording once enabled; background recording ends when the host exits. Aggregate state is saved atomically every ten seconds when dirty and on normal shutdown. Nothing can be counted retrospectively while the host is closed. The SDK mock host provides demo counters without monitoring the real keyboard/mouse. SDK tools can reject hosts lacking the optional extension with `CD_ERROR_UNSUPPORTED`.

## Media playback extension

Declare `capabilities: MEDIA_CONTROL` in the protobuf descriptor. Check `context->host->struct_size >= sizeof(CdMediaHostApi)` before casting the host pointer; otherwise return `CD_ERROR_UNSUPPORTED`. `CdHostApi` stays 56 bytes and C ABI 1 is unchanged. The extended table is 72 bytes with the original host prefix at offset zero. `subscribe_media(context, filter, callback, user, &token)` selects `"auto"` (current system session) or a case-insensitive source-ID substring, such as `"qqmusic"`, `"cloudmusic"`, `"kugou"` or `"spotify"`. The host owns one background C++/WinRT reader. The native SDK preview supplies mock media, never real playback commands.

Callbacks deliver a 112-byte `CdMediaInfo` on the UI thread. Copy retained UTF-8 strings and cover pixels before returning. Covers are premultiplied RGBA8 with explicit width, height and stride, at most 256 pixels per dimension; a changed `cover_generation` or source ID invalidates the previous cover. Empty `source_id` means no matching session and clears metadata and capabilities. Status is `CD_MEDIA_PLAYING`, `CD_MEDIA_PAUSED` or zero. Position and duration are seconds relative to the timeline start; `sampled_at_ms` is monotonic Windows milliseconds. Playing progress may be extrapolated using `playback_rate`, bounded by duration; idle content should not redraw.

`media_command(context, token, command, value)` targets that subscription's last delivered session. `CD_MEDIA_TOGGLE`, `CD_MEDIA_PREVIOUS`, `CD_MEDIA_NEXT` and `CD_MEDIA_SEEK` map to capability bits in `controls`. Seek value is seconds relative to timeline start; other commands pass zero. Disable unavailable controls. `CD_OK` means queued, not executed: `command_result` subsequently reports `CD_ERROR_BUSY` while pending, then `CD_OK`, `CD_ERROR_UNSUPPORTED` or `CD_ERROR_INTERNAL`. The host rechecks the current session and capabilities on the worker and bounds its queue. Release media tokens with the original `unsubscribe` before pause/destroy; changing players should replace the subscription. Only registered sessions on Windows 10 1809+ / Windows 11 are accessible, and supported commands depend on each client version.

The production music-control plugins in `plugins/widget-suite` demonstrate the service, copied cover ownership, adaptive layouts and input hit testing. Microsoft's [GSMTC reference](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession) documents the underlying system contract.

## System telemetry extension

`subscribe_system` may deliver `CdSystemInfoEx` through its unchanged `const CdSystemInfo*` callback. Inspect `info->struct_size >= sizeof(CdSystemInfoEx)` before casting. The original `CdSystemInfo` prefix remains 56 bytes; existing plugins can read that prefix without adopting the extension. Create/query/render interfaces and ABI number remain unchanged.

The extension supplies `valid_fields`, `cpu_temperature_celsius` and `sampled_at_ms` (Windows monotonic milliseconds). Test `CD_SYSTEM_CPU_VALID`, `CD_SYSTEM_MEMORY_VALID`, `CD_SYSTEM_GPU_VALID` and `CD_SYSTEM_CPU_TEMPERATURE_VALID` before treating a field as a real measurement. Zero temperature with a clear validity bit is a placeholder; it must not be interpreted as a measured CPU temperature. Copy the callback data before returning. GPU temperature is not exposed as CPU temperature.

## 必需的插件设置页

插件描述可声明 `content_padding`（每边的逻辑像素留白，范围 0–24）；未声明时宿主默认 12。小格子插件可以用 `content_padding: 3`，扩大绘制区域。宿主布局和 GPU 内容抓取使用相同留白，插件继续按实际传入的画布宽高和安全区绘制。此属性不改变 C ABI。

带外层卡片背景的插件统一使用 `content_padding: 4`，与音乐频谱保持一致。卡片主体在安全画布中采用 `(left=1, top=1, right=width-3, bottom=height-4)`，右侧与下方的剩余空间用于紧凑阴影。文字、图表的内部留白单独计算，不要再次缩小整个外层背景；透明内容插件仍按自身布局声明留白。横排或竖排的多卡片组合也使用同一组外围边界。外层背景的 `background_corner_radius` 设置为 INTEGER，统一默认 5，范围 0–32 逻辑像素；0 表示直角，较小的卡片按半边长限制实际圆角。

每个插件必须在 `PluginDescriptor.settings_page` 声明非空设置页，并实现 C ABI 的 `get_setting`、`apply_settings`。支持 TOGGLE、INTEGER、NUMBER、TEXT、CHOICE、COLOR；定义标签、说明、分组、默认值和范围。宿主提供标准编辑控件、恢复默认、应用、保存、取消。无设置页或缺接口的插件直接拒绝，不保留旧接口兼容。

配置按实例独立保存：同一插件的两块砖可有不同配置。宿主把配置与业务状态一起保存并在新实例恢复；插件的 opaque `save_state` 仅负责自己的业务状态。`apply_settings` 必须原子应用：任一值无效则全部保留。输入字符串仅在调用期间有效，插件需要自行复制。公共 `Settings.h` 提供不依赖 Qt/protobuf 的 C++ 辅助存储与验证。

桌面右键插件砖块 →“插件设置…”，Studio 预览提供同名按钮；公开原生预览按 S 打开插件自己的设置页。应用即时刷新，恢复默认仅修改编辑值，保存/应用后生效；取消不保存尚未应用的修改。SDK 检查覆盖缺声明、无效事务、配置生效与多实例独立，真实宿主额外检查设置 UI 与销毁重建后的恢复。


## Instance updates

`sdk/Update.h` contains `Timer`, `RedrawRequests` and `UpdateLoop`. These adapters own optional host subscriptions on the UI thread; the independent engine still owns no timers. Bind to the instance create context before use. `Timer::replace(interval, callback, user)` retains the old subscription on failure, reuses identical registrations, cleans partial failures and transfers ownership when moved. Stop/destruction unsubscribes. Callbacks and user data must outlive the timer, and the host must stop callback delivery synchronously when unsubscribed.

`RedrawRequests::invalidate()` coalesces until `beginFrame()`, which belongs at the start of rendering. `pause(true)` defers invalidations, and resume delivers one pending request. `UpdateLoop::onUpdate` returns `{changed, active}`; changed requests a new texture, while active controls whether frames continue. The final changed frame is requested before stopping. Call `beginFrame()` during rendering when using its default redraw path, or connect `onInvalidate()` to the instance's shared redraw latch.

`start(interval, optional_monotonic_ms)` starts or replaces a cadence transactionally. A fresh/faster cadence resets its anchor; an optional anchor avoids delaying the first step. `pause` cancels subscriptions and excludes hidden time. Resume failures return a result and may be retried. `speed(0)` suspends the visual timer while preserving its desired cadence; restoring speed resumes without including suspended time. The clock clamps long frame deltas as documented in the engine animation guide. Callback retiming is protected against a stale completion result.

Use raw owned `Timer` subscriptions for data/service polling according to business visibility rules. Cancel captured view input when hiding an interactive instance. Stop work before destroying its callback state. Noise and news are complete production examples; `examples/Update.cpp` supplies a deterministic standalone host and PNG export. No public C ABI structures change.


## Optional keyboard and accessibility service

`services/Keyboard.h` defines `ui.keyboard` version 1. It is discovered through the existing `CdServiceHostApi.query_service`; no original C ABI 1 structure changes. Check the function table size/version and every callback before use. An unknown service keeps pointer-only operation. The new table is 32 bytes, events are 32 bytes and semantic nodes are 104 bytes under the published packing.

`Keyboard` owns its registration; the bound engine Scene must outlive it. `cdsdk::Scene` owns this adapter automatically. Register `scene.onAction(...)` to route keyboard and accessible actions to the same business logic used by pointer `SceneEvent`s. Values are controlled and updated in Scene before notification. Host registration and all callbacks run on the owner UI thread, never a worker. Destruction unregisters before releasing the scene.

Requesting focus does not grant it. Only a `CD_UI_FOCUS` event with value 1 enables keyboard actions; loss, hide, pause, deactivation and destruction revoke it. Tab/Shift-Tab follow current visible enabled tree order (wrapping inside a focused card); arrows navigate controls or change sliders, Home/End select slider endpoints, Enter/Space activate buttons/toggles and Escape cancels the gesture and current control focus. Control/Alt/Meta combinations are left to the host. Hidden, disabled and removed controls lose focus immediately; moving a valid focused node preserves identity and updates traversal order.

Full semantic snapshots contain stable scene handles, parent-before-child hierarchy, logical bounds, UTF-8 names/values, roles, states and numeric ranges/steps. The host copies all data during publish, supports at most 4096 nodes and 4096 UTF-8 bytes per string, and removes objects omitted from the next snapshot. Names longer than the limit are truncated at a UTF-8 boundary. Use `accessibleName` for icon-only buttons and sliders; translation belongs to the plugin. Decorative containers are nonfocusable. The production Qt host maps these nodes to its Windows accessibility backend, including focus-child resolution, action and slider value interfaces. No Qt types cross the plugin DLL boundary.

Scene coordinates are logical pixels. When painting in a translated safe-area scope, set `scene.keyboard().origin(...)` before rendering; pointer input also updates this origin. `SceneList` exposes a single focusable list with a readable current selection: Up/Down/Home/End navigate stable IDs and scroll the selected row into view, Left/Right select enabled row actions, Enter/Space activate. Provide `label(...)` for readable selection text and `onRowAction(...)` for business handling.

Wheel propagation is explicit: a list consumes a wheel event only when it can change its scroll target. At a boundary it returns `handled=false`, allowing the caller to try its parent list. A partially clamped event is consumed as one event; no residual delta is propagated. While a scrollbar drag owns capture, wheel events are consumed by that owner. Old hosts retain the stationary-click fallback and never receive global keyboard hooks.


## Optional instance diagnostics

`Diagnostics.h` supplies owner-thread counters and DiagnosticSource; `services/Diagnostics.h` defines the optional dev.diagnostics v1 table (24 bytes), stats (520 bytes) and nodes (1008 bytes). All original C ABI 1 layouts remain unchanged. A source registration borrows its callbacks until unregister, which must finish before those objects or their DLL are released. Hosts permit 16 sources per instance and 4096 nodes per sample. `cpp/DiagnosticHost.h` is the shared native-host/tool registry; invalid snapshots and DLL callback faults fail sampling without requesting render work.

`cdsdk::Scene` automatically samples its most recently arranged Canvas and its scene; that Canvas must remain alive while this registration can be queried. `diagnostics().extras(...)` adds instance-specific Images/List/RedrawRequests data. Sampling reports layout rectangles, effective clips and hit bounding boxes; these are layout diagnostics, not replacements for path/ellipse testing or image poses. Labels, resource paths and errors are bounded UTF-8 strings (128/512/256 bytes including the terminator). Legacy hosts require no diagnostic service.

Durations use inclusive monotonic nanoseconds. Canvas frames include target preparation, software rasterization at EndDraw, conversion and output copy; Scene paint time records drawing commands. Caller buffer allocation, PNG export and host upload/composition are separate. Pixel payload bytes are tracked explicitly, while native text/codec/driver allocations and process RSS are excluded. Shared store totals and per-slot/per-scene references describe overlapping ownership and must not be summed as unique memory. Peak values retain observed maxima. `cd-sdk diagnostics DIRECTORY OUTPUT.json` exports per-instance metrics as decimal strings to preserve uint64 precision.
