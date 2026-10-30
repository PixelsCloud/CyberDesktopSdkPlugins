# Changelog

## SDK 1.4.0 / CyberCanvas 1.2.0 (development validation)

- Adds all 33 active public plugin families, family/ID dependency selection and an installable
  distribution declaring shared runtime, atlas and font dependencies. Adds basic/full public preview
  profiles, actual isolated character images and Live2D rendering, native dynamic settings and input,
  bounded rendering diagnostics and CPU composition. The public source inventory excludes private
  integration tests and application services. C ABI and package format remain version 1.
- Full task acceptance and private-core deployment are still in progress; this is not a public release.

- Adds optional `ui.settings_choices` for bounded per-instance choice inventories and setting scopes. Open host
  settings editors refresh when a character or model changes; undeclared fields and invalid inventories are rejected
  atomically. Existing instance and host ABI prefixes remain unchanged.

- Adds optional `ai.character_chat` and `ui.text_input` C ABI services for durable character conversations and
  host-owned native text input, including IME composition and explicit submission. Conversation snapshots are bounded;
  credentials and external import paths are never accepted through plugin JSON.
- Adds bounded decoded character images, immutable resource-pack references, local background/Live2D/foreground
  composition and actual model expression/motion discovery. Existing whole-card Live2D rendering and ABI prefixes remain
  compatible. Services are optional and must be checked before use.

- Package inspection recognizes the exact Windows system `icu.dll` import used for Unicode grapheme segmentation.
  Private/versioned ICU DLLs still require packaged dependencies; this does not broaden the System32 search rule.

- Adds `PrismaticOptions`, `World::prismatic` and `prismaticMotor` for constrained linear mechanisms,
  including normalized axes, translation limits, spring/damping parameters and force-limited motors.
  The beat machine uses them for physical hammers and its launcher; the native Box2D types remain private.

- Adds optional revolute joint angle limits and motor control (`RevoluteOptions`, `revoluteMotor`) for loaded
  mechanisms such as the marble clock's collection flap. Existing four-argument revolute calls remain available.

- Adds optional `CyberCanvas::Noise` backed by pinned FastNoiseLite 1.1.1: immutable 2D/3D fields,
  seamless time sampling and bounded particle wind. Koi, plant and ambient particles consume it.
  Includes the static target, source/overlay port, MIT notice and focused determinism/pause tests.

- Adds whole-scene transforms, transformed clips/input/semantics, explicit subtree caches and single-writer timelines.
- Adds optional static Box2D 3.1.1 worlds and scene binding, with fixed stepping, interpolated poses, capture-aware dragging,
  joints, copied events, queries, debug geometry and sleep-aware scheduling.
- Adds bounded D2D blur/shadow/glow/inner-shadow surfaces and immutable DirectWrite rich-text layouts.
- Adds shared pixel budgets, asynchronous GIF/PNG sequence/atlas decoding, and restricted ThorVG 1.0.6 SVG/Lottie sources.
  Decoder teardown occurs on the worker outside the snapshot mutex; package references cannot escape the package root.
- Adds deterministic bounded particles and bar/donut/heatmap charts, plus installable physics and rendering examples.
- Physics sandbox includes switchable spring suspension, visible coils, damped settling and mode-change capture cleanup.
- Existing C ABI 1 plugins remain binary compatible. C++ consumers must rebuild using matching 1.2 headers and libraries.
  Hardware/environment, endurance and release evidence are tracked separately from successful builds.

## SDK 1.3.0 / CyberCanvas 1.1.0 (local release candidate)

- Adds visible-glyph measurement and fitting to the existing bounded text cache, including reference strings and external font collections. Flip digits and koi blessings use shared text layouts; electronic seven-segment outlines use `Path`. Font size fitting does not mutate previously cached sizes; native layout handles remain borrowed/read-only.

- Adds `ReadableSurface` for WIC-backed raster painting with immutable BGRA readback, DPI-preserving cached uploads and exception-safe rectangular clips. Flip clocks use it for face caches while retaining inverse-perspective sampling; electronic clocks use shared geometry masks.

- Round and square matrix spectrum plugins use shared offscreen surfaces and scoped geometry masks for their background and light sprites. Finished bitmaps are retained independently of temporary surfaces, and target identity participates in cache invalidation; spectrum sampling and appearance remain plugin-owned.

- CyberCanvas accepts immutable decoded BGRA images, sharing identities and bounded uploads with ImageStore resources. Image placement supports opacity and caller-owned masking; album and record artwork now share upload caching and cover/contain placement. Rebuild C++ consumers with matching headers and static libraries; the plugin C ABI is unchanged.

- CyberCanvas adds reusable DPI-aware `OffscreenSurface` and scoped geometry masks. Koi pond/blessing caches and skin masks, player record masks and album crossfade clipping use the shared implementation. Native frame errors invalidate partial output and unwind layers; primary Canvas targets remain unchanged.

- CyberCanvas shares bounded, target-specific linear/radial gradient brush and stop caches, with explicit temporary-target release and cache counters. Koi, weather, air quality and spectrum rendering reuse this implementation; `gradientBox` uses it too. Immutable full-geometry keys preserve simultaneously held brushes and floating-point colors.

- CyberCanvas adds native endpoint elliptical arcs, hollow path figures and per-rectangle opacity without group layers. Koi, plant and record paths now use shared Path construction; the pixel hourglass uses Canvas drawing, transforms and text caching while retaining its simulation and pixel glyphs.

- Adds `compare-preview-gallery.ps1` to match completed galleries by plugin/layout/size/theme/DPI/scenario, reuse `cd-sdk compare`, and export portable side-by-side HTML plus JSON metrics/failures. Capture configuration and duplicate keys are checked; added/missing cases, decode failures and timeouts fail the run without overwriting baselines.

- Adds `preview-gallery.ps1` for bounded batch captures across declared layouts, sizes, themes, DPI and mock scenarios, with an offline HTML gallery, logs and JSON failure reports. `cd-sdk inspect` now also reads a plugin directory without executing its DLL.

- Preview gains a bilingual menu for service scenarios, common logical sizes and transparent PNG export (F2/F3/F9). Scenario reload preserves business state; exports exclude debug overlays and avoid watched package paths. `--scenario` supports repeatable headless captures, and `--export-dir` selects the interactive export destination.

- Adds `info`, `tasks`, `media` and `calendar` generator presets using the shared desktop components. Generated projects include editable example code and remain standalone C++20 /MT consumers.
- Info and media default to host telemetry/playback, with an explicit offline demo option. Owned watches, capability-gated media commands, request correlation, loading/empty/error/retry states, deadlines and pause/resume cleanup are shared with Workbench. Preview remains mock-only.
- Canvas Workbench exposes those same four examples through its page setting, transfers keyboard ownership on page switches, retains per-page state and reads its previous state format. Starter verification now exercises seven generated projects and 42 theme/DPI captures.

- Adds desktop `Label`, `MetricItem`, `StatusLine`, `TextRow`, `TaskRow`, `Checkbox`, `ProgressRing`, `SegmentedScale`, `CalendarGrid` and `CalendarCell` primitives. Explicit text slots preserve compact plugin layouts; status panels can omit their background.
- Adds `sdk/Typography.h` for existing external font collections and `sdk/Controls.h` for immediate-mode button/toggle pointer input, optional host keyboard focus and accessibility. It owns no timer; unchanged accessibility snapshots are not republished. `Scene` remains the retained-layout API.
- Shared text caching now supports retained DirectWrite font collections, tabular figures and character spacing. C++ consumers must rebuild with matching headers/static libraries; C ABI 1 is unchanged. No audited 1.0 archive is overwritten.

- Avoids redundant opacity layers in fully opaque built-in widget painters and skips zero-strength feedback. Fractional opacity, custom icon callbacks and public `Canvas::withOpacity` retain their group semantics. Diagnostics report layer counts and EndDraw time; Windows 10/11 compatibility is evaluated separately from hardware-dependent timing.
- Adds an opt-in desktop theme, minimum measured button height, inherited disabled opacity and static localized ready/loading/empty/error panels. Existing compact theme defaults and C ABI 1 stay unchanged; rebuild C++ consumers against matching headers and libraries.
- The workbench uses the shared desktop preset. Theme checks export light/dark galleries at DPI 1/1.5/2/3 and verify geometry, paint-only invalidation, inheritance, clipping and deterministic status rendering.
- The private host adds `--verify-canvas-instances`: 2/6/12 deterministic Canvas fixtures exercise the production plugin tile, damage upload and GPU composition paths. Reports distinguish correctness from the optional 16.7 ms timing gate. They do not represent native DLL/service load or display latency.
- `scripts/verify-canvas-development.ps1` in the host repository runs the scoped component and multi-instance checks. Existing 1.0 delivery archives are unchanged.

## SDK 1.2.0 / CyberCanvas 1.0.0 (local delivery)

- Stabilizes the reusable native Canvas, retained scene/layout/data controls, bounded image worker, input, accessibility, update ownership, diagnostics and preview workflows. C ABI 1 and protobuf package format 1 remain unchanged.
- Existing clock, hourglass, telemetry, weather, air-quality, music/photo, productivity and koi plugins share the primary Canvas while keeping their tested domain rendering and state.
- Fully clipped or zero-size controls cannot keep/reacquire focus. Valid pending rearrangements retain logical focus without publishing stale geometry.
- Lifecycle settings checks respect coupled domain rules and check rejection atomicity across all fields; editable metadata takes precedence over stale binary descriptors. The CLI version comes from release metadata.
- Public ownership, limits, compatibility and error contracts are documented in docs/CONTRACTS.md.
- Three complete static/data/animated C++ starters, a non-overwriting creation script and installed-only tutorial cover click state, owned telemetry watches and pausable motion.
- Local delivery independently builds installed clients and the whitelisted source archive, verifies ten licensed example/starter packages, twelve Canvas suites, fifteen SDK contracts and enforced fixed-data benchmarks. Release reports use portable relative artifact paths.

## Native development workbench (2026-10-17)

- Preview supports logical dimensions, actual DPI, theme/BCP47 locale, bounded JSON mock inputs, fixed wall time,
  frame-indexed input replay, semantic focus, drag capture, PNG/BMP export and read-only diagnostic overlays.
- Optional `host.environment` v1 supplies time/offset/theme/locale without changing C ABI 1. `PluginAdapter<T>`
  contains typed C++ hooks inside the existing C entry table, with RAII ownership and exception containment.
- Background directory notifications coalesce resource reloads; full module invalidation restores settings and
  versioned native state. Resource failures retain the last good frame, identify the path and recover after correction.
- The PowerShell development loop builds unlocked original outputs while previewing shadow DLLs; failed builds keep
  the active process, resource-only changes keep its PID, and successful C++ rebuilds restart with saved state.
- `cd-sdk compare` supports channel tolerance, allowed pixel ratio and a difference image; `cd-sdk state` inspects
  protobuf preview state. The native workbench and installed-SDK integration checks ship with source exports.
- Canvas adds bounded uncached frame blits for tooling and transient upload accounting. Overlay toggles reuse retained
  plugin pixels without requesting another plugin frame. No automatic subtree cache is added.

## Owned instance updates (2026-10-16)

- `sdk/Update.h` adds movable RAII timers, coalesced per-instance redraw requests and finite/continuous host-driven update loops. Failed replacements preserve the previous subscription and clean up partial registrations.
- Visual loops stop while hidden, exclude hidden time, support explicit monotonic anchors and playback speed, deliver the last changed frame before stopping, and suspend subscriptions at speed zero. Callback exceptions cannot cross the C boundary.
- Images, ambience and news use common timer ownership. Noise scene and news list invalidations join each instance's redraw latch; news transient feedback uses `UpdateLoop`. Audio/data ownership stays separate from visual work.
- `examples/Update.cpp` exports a deterministic final-frame PNG with no remaining timers. Contract checks cover replacement failures, repeated starts, pause/resume, callback retiming, old hosts and independent instances. Public C ABI 1 is unchanged.

## Background image resources (2026-10-16)

- CyberCanvas 0.10 adds a separate `CyberCanvas::Resources` static target using a bounded worker and WIC. Shared path/revision/size requests publish immutable premultiplied BGRA pixels; workers never call host or rendering APIs.
- Handles expose loading/ready/failed/cancelled states. Entry/file/decoded-memory limits, consumer pinning, eviction, explicit revisions and cancellation prevent stale results and unbounded cache growth.
- Canvas uploads decoded images on its own thread, with a separate bounded cache, contain/cover/stretch placement and sprite cells. Scene resources support paint-only updates and safe placeholders.
- `sdk/Images.h` manages per-instance slots and temporary UI-thread completion polling. Hidden/destroyed instances stop delivery, shared consumers remain independent, and settled resources unsubscribe their timer.
- Noise 1.0.8 uses background resources for both layouts. Independent examples, installed SDKs and source exports include the new APIs; C ABI 1 is unchanged.

## Dynamic scene structure (2026-10-16)

- CyberCanvas 0.9 adds subtree removal, child clearing, cross-parent moves and sibling ordering to retained scenes. Flex layouts reuse Yoga's structural operations with matching query APIs.
- Surviving handles and controlled values persist. Removed handles are never reused; live node capacity and captured resources are reclaimed immediately. Removed business keys may be used again.
- Layout, inherited themes, drawing order and hit priority follow the current tree. Structural changes cancel host capture and coalesce redraws for the current instance; no-op moves stay idle.
- Clean leaf measurements remain eligible for Yoga caching during structural updates. Explicit layout invalidation and changed content/font metrics invalidate measurement as needed.
- A standalone dynamic-scene example exports before/after PNGs. SDK installation and source export include the updated static engine and APIs; C ABI 1 is unchanged.

## Scrolling and virtual lists (2026-10-16)

- CyberCanvas 0.8 adds `ScrollView` and `ListView`, reusing Tweeny easing, widget themes and the existing input router. Vertical scrollbars support captured dragging and actual-DPI thumb geometry.
- Lists use stable string IDs and supplied row heights, preserve the top visible item across insertions/removals, and only paint/register targets for visible rows. Revision-only updates retain offset geometry; offscreen updates stay idle.
- Shared loading/empty/error states support a retry action. Row actions return stable IDs rather than viewport indices, and data/geometry changes cancel pending gestures.
- News defaults to smooth scrolling. A per-instance browse mode setting and footer button retain pagination. Native tests cover scrolling, capture cancellation, bookmarks and settings persistence.
- SDK headers, static libraries, examples and source export include list APIs; C ABI 1 remains unchanged.

## Retained components and invalidation (2026-10-16)

- CyberCanvas 0.7 adds a retained `Scene` containing rows, columns, text, images, buttons, sliders and toggles. It reuses Yoga, DirectWrite, the existing widgets and input router.
- Theme tokens inherit from parents with local overrides. Value/color/pose changes request paint; text/font/spacing/visibility/geometry changes request layout. Equal values remain idle and notifications coalesce until drawing.
- Nodes automatically register clipped hit regions and share their geometry with drawing. Reflow, disabling and hiding cancel gestures; feedback ends without permanent frame subscriptions.
- `sdk/Scene.h` shares the router with the optional capture service and requests redraw for the current plugin instance. Noise uses one tree for rendering and interaction, replacing its separate layout/control code.
- Static libraries, source exports and installed consumers include the scene API. C ABI 1 and Qt-free plugin rendering remain unchanged.

## Automatic layout and composite components (2026-10-16)

- CyberCanvas 0.6 wraps static Yoga 3.2.1 for measured flex rows/columns, wrapping, growth/shrinkage, spacing, min/max sizes, percentages, RTL and actual-DPI edge rounding.
- `Components.h` adds icon labels, title/action headers and labelled sliders with shared drawing and hit geometry. Text leaves use DirectWrite measurement; retained trees skip unchanged layouts.
- Noise tracks and news sources reflow with available space; article titles, summaries and metadata share measured layout. Narrow news hit regions stay inside the safe area.
- Installed SDKs include the static layout and Yoga libraries and MIT notices. Source exports declare Yoga in vcpkg. C ABI 1 remains unchanged; plugins remain native C++ and Qt-free.

## Reusable controls (2026-10-16)

- CyberCanvas 0.5 adds controlled buttons, icon buttons, toggles, sliders and progress bars, shared day/night tokens and bounded per-control feedback.
- Drawing and hit testing share geometry. Sliders handle range, step, local transforms and captured motion; disabled controls use group opacity and ignore input.
- Noise and news use these controls; a standalone interactive Win32 example and DPI/theme render tests are included.
- Optional `ui.icons` v1 rasterizes Lucide SVGs via the host's CyberGlass icon entry. `sdk/Icons.h` caches by name/color/size/DPI; plugins retain text fallback on older hosts and remain Qt-free.
- Installed headers, source exports and C ABI checks include controls and icons; ABI 1 layouts remain unchanged.

## Pointer interaction (2026-10-16)

- CyberCanvas 0.4 adds transformed/clipped rectangle, ellipse and native path hit testing, priority, button cancellation, captured drag phases and retargetable feedback.
- The optional `ui.interaction` v1 service enables real press/move/release delivery and synchronous per-instance capture. Existing ABI 1 structs and event values stay unchanged; `CD_INPUT_CANCEL` is additive.
- `sdk/Interaction.h` maps host input and releases capture on cancellation. Older hosts retain stationary clicks.
- Noise master/track sliders drag continuously beyond tile bounds; news buttons share the router and transient feedback. Moving a tile cancels pending button actions.

## Host-driven animation (2026-10-16)

- CyberCanvas 0.3 adds a monotonic host clock, pause/resume, playback speed, finite/infinite looping and ping-pong tracks.
- Scalar/point/size/pose interpolation reuses Tweeny 3.2.1 easing; its MIT license and required header are included in the SDK.
- Plant sway and transient watering feedback use separate clocks; settled ornamental cards stop their timer when continuous motion is disabled.
- Noise artwork breathes while audible playback is active, with per-instance animation/speed/frame-rate controls. Paused or muted cards poll audio without unnecessary redraws; hiding excludes visual time without stopping sound.

## Drawing paths, state and layout (2026-10-16)

- CyberCanvas 0.2 adds retained compound paths, quadratic/cubic curves, fills, strokes, caps, joins and dash styles.
- Scoped drawing state, local translation/rotation/scaling and true group opacity use Direct2D resources with bounded caches.
- DirectWrite measurement shares shaping and layout settings with rendering, including line spacing and automatic RTL direction.
- Small row/column cursors, padding, alignment and equal slots keep layout independent of the plugin ABI.
- News renderers share engine text, paths and layout instead of owning a second DirectWrite implementation.

## Reusable drawing engine (2026-10-16)

- Independent Windows C++20 CyberCanvas core with Direct2D/DirectWrite/WIC, no Qt or plugin ABI dependency.
- SDK Canvas adapter for frame validation, safe areas and widget language; exception-safe rendering scopes.
- Shared DPI-aware shape, gradient, image and text drawing with bounded resource caches.
- WIC output conversion replaces the shared widgets' manual per-pixel channel-copy loop.
- Ambience, lunar and news renderers use the shared core; C ABI 1 is unchanged.

## Interface languages (2026-10-16)

- Built-in widget settings and UI support Simplified Chinese, Traditional Chinese and English.
- `Language.h` provides instance-local render language scopes and a static UI phrase dictionary.
- The host follows its interface language by default and preserves explicit per-widget choices.
- Existing ABI layouts, package identifiers and persisted setting keys remain unchanged.

## Optional ambient audio service (2026-10-15)

- Additive `audio.ambient` v1 table in `services/AmbientAudio.h`, gated by `MEDIA_CONTROL`; existing ABI prefixes remain unchanged.
- Private and shared natural-sound scenes, six tracks/four-way mixing, atomic volumes, explicit play/pause/stop and UTC timer snapshots.
- Silent SDK mock and required-service validation. Playback assets and miniaudio implementation stay in the application.

## 1.1.0

- Optional 88-byte host extension for versioned service discovery; existing ABI 1 prefixes remain unchanged.
- Nine shared services for input, system, playback, player sessions, PCM, FFT, location, weather and air quality.
- Typed owned C++ snapshots, move-only RAII watches and self-unsubscription; C declarations remain Qt-free.
- Required-service protobuf declarations and capability validation before factory creation.
- Weather daily/hourly forecasts, source/time/error contracts and private host credential profile references.
- Correlated asynchronous media request IDs; audio device status and bounded-buffer drop diagnostics.
- Three complete examples, deterministic mock failures, installed and exported-source release verification.

## Optional input statistics

- `INPUT_STATS` capability and optional 80-byte `CdInputStatsHostApi` extension preserve both original host prefixes and ABI 1.
- Shared host counts physical keyboard presses and mouse-button clicks, with today/total/session aggregates, repeat suppression, local midnight rollover and local persistence.
- UI-thread change-only callbacks and local rendering at most once per second; recording continues while displays are paused. No per-event/key/text history is persisted or exposed.
- Native SDK preview uses mock counts and never monitors real input.

## Optional media playback service

- `MEDIA_CONTROL` descriptor capability and `CdMediaHostApi` extension: shared media subscriptions and asynchronous toggle/previous/next/seek commands.
- `CdMediaInfo` exposes capability flags, UTF-8 metadata, playback timeline, cached decoded cover pixels and command results; all borrowed views remain callback-scoped.
- Original 56-byte host prefix and C ABI 1 remain unchanged. Hosts without the 72-byte extension can be detected before creating a media plugin.
- Native preview/CLI use synthetic media and never control real players. Production host uses one background C++/WinRT service and instance-local refresh.

## 1.0.0

- New Windows x64 C ABI 1 with opaque instances and module-owned memory.
- Premultiplied RGBA rendering, input, Rect/Hex layouts, DPI, pause and instance state.
- Host timer, system information, FFT/PCM subscriptions and scoped cleanup.
- Protobuf package format 1 embeds all plugin DLLs/resources/private dependencies in a single `.cdpkg`.
- Validate hashes, bounded paths/sizes, actual normal/delay DLL imports, entry exports and dependency closure before extraction/loading; stage production updates with rollback.
- /MT tools and package runtime using pinned static vcpkg protobuf, Abseil and utf8-range.
- Independent template, two examples, mock preview, lifecycle tests, installed CMake integration, release scripts and public CI.
- MIT SDK independent of the closed application. Old Qt plugin ABI, JSON manifests and ZIP plugin package support removed completely.

## Optional system telemetry

- Plugin descriptors can declare optional `content_padding` in logical pixels (0–24, default 12); the native host uses it consistently for layout and compositing.

`CdSystemInfoEx` extends the unchanged 56-byte system-info prefix with explicit field validity, CPU temperature in Celsius and a monotonic sample timestamp. Check payload size before reading the extension; C ABI 1 and existing host/create/instance structures are unchanged. Missing CPU temperature remains zero with its validity bit clear.

## 必需的插件设置页

每个插件必须在 `PluginDescriptor.settings_page` 声明非空设置页，并实现 C ABI 的 `get_setting`、`apply_settings`。支持 TOGGLE、INTEGER、NUMBER、TEXT、CHOICE、COLOR；定义标签、说明、分组、默认值和范围。宿主提供标准编辑控件、恢复默认、应用、保存、取消。无设置页或缺接口的插件直接拒绝，不保留旧接口兼容。

配置按实例独立保存：同一插件的两块砖可有不同配置。宿主把配置与业务状态一起保存并在新实例恢复；插件的 opaque `save_state` 仅负责自己的业务状态。`apply_settings` 必须原子应用：任一值无效则全部保留。输入字符串仅在调用期间有效，插件需要自行复制。公共 `Settings.h` 提供不依赖 Qt/protobuf 的 C++ 辅助存储与验证。

桌面右键插件砖块 →“插件设置…”，Studio 预览提供同名按钮；公开原生预览按 S 打开插件自己的设置页。应用即时刷新，恢复默认仅修改编辑值，保存/应用后生效；取消不保存尚未应用的修改。SDK 检查覆盖缺声明、无效事务、配置生效与多实例独立，真实宿主额外检查设置 UI 与销毁重建后的恢复。

## Fonts and plugin localization

- FONT setting type and standard installed-font selector; shared external Heavy font loaded without font installation or binary embedding.
- Per-instance zh-CN / en content and localized settings-page metadata.
- Qt-free WindowsFonts DirectWrite helper and matching native preview font selector.


开发中：共享数据组件与 Scene/List 组合

- 增加 CyberCanvas/DataWidgets.h：受控选项、数值、带断线和缓存的折线、24 段仪表。
- 增加 sdk/SceneList.h：场景标题与虚拟列表使用同一视口，稳定行 ID 和操作区域自动配合。
- WidgetTheme/ThemePatch 增加 error 颜色令牌；静态 C++ 使用方需配套重编，C ABI 1 不变。
- 独立运行 cd_sdk_scene_list_demo [输出目录] 查看明暗、RTL 和窄尺寸示例。


开发中：键盘、焦点与系统可访问性

- 新增可选 ui.keyboard v1 服务及 SDK Keyboard 所有权适配；保留原 C ABI 1。
- Scene 支持焦点顺序、快捷操作、语义名称/角色/数值/状态；SceneList 支持键盘行导航和行内操作。
- Qt 宿主桥接 Windows UI Automation；隐藏/禁用/删除取消焦点和过期对象。
- 列表边界滚轮可传播，捕获中的滚动条保留事件所有权；白噪音 1.0.10 接入真实键盘与辅助操作。


开发中：实例诊断与可复现基准

- 增加 Canvas/Scene/List/Images/RedrawRequests 的次数、包含时间、缓存及已知像素负载统计。
- 增加可选 dev.diagnostics v1、DiagnosticSource 及正式宿主/工具共享的只读注册表；旧宿主自动忽略。
- 新增 cd-sdk diagnostics 和三 DPI 固定时间基准，源码/安装导出带参考报告。
- 白噪音按控件实际 DPI 解码插画，样本像素负载减少约 95%；未引入自动子树缓存或新的局部栅格协议。
