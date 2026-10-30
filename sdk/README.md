# CyberDesktop SDK 1.4.0

Native Windows x64 plugins, CyberCanvas 1.2 and C ABI 1. Plugins use protobuf `.cdpkg` archives;
shared theme runtimes/fonts/atlases are delivered with the declared public distribution. See [plugin builds](../plugins/README.md).

GPU primary output, independent CPU capture, shared resource budgets and sprite batching are described in [RenderOutput](docs/RenderOutput.md). These capabilities use optional services and preserve C ABI 1 compatibility.
The SDK is MIT licensed. The desktop application and its GPU rendering are separate closed-source products.

SDK 1.4 adds optional static components for scene transforms and animation timelines, Box2D physics, D2D effects,
styled text, background GIF/PNG/ThorVG animation, particles and charts. See [Rendering extensions](docs/RENDERING_EXTENSIONS.md).
The installable `physics-sandbox` and `rendering-gallery` examples demonstrate interaction, pause, settings and cleanup.
This is a development validation version until the repository acceptance record closes its environment and release gates.

SDK 1.1 adds nine discoverable host data services, owned C++ snapshots and RAII subscriptions. See [Data services](docs/DATA_SERVICES.md) or [中文指南](docs/DATA_SERVICES.zh-CN.md). Three new examples show input counts, system/weather/AQI composition and media controls with system spectrum. Producers and private credentials remain in the application.

`<sdk/Canvas.h>` provides reusable C++ drawing for shapes, text, gradients, images, clipping and DPI-aware transparent frames. It adapts the independent MIT-licensed CyberCanvas engine, exported as `CyberCanvas::CyberCanvas`. The engine can also be used without the SDK; its guide is installed at `share/cybercanvas/README.md`. See the [Chinese quick start](docs/QUICKSTART.zh-CN.md) for plugin usage. Plugins continue to compile as native C++. `<sdk/Update.h>` supplies owned subscriptions, coalesced redraws and host-driven finite or continuous update loops. `<sdk/Images.h>` adds background WIC loading, shared immutable pixels, bounded caches and instance-local UI-thread completion delivery, backed by `CyberCanvas::Resources`. `<sdk/List.h>` supplies smooth scrolling, stable-ID virtual lists and shared loading/empty/retry states. `<sdk/Scene.h>` adds retained components, inherited themes, shared input geometry and coalesced per-instance invalidation. Scene subtrees can be removed, cleared or moved without losing surviving handles and values; drawing and input follow the current child order. Automatic layout and composite controls use the static `CyberCanvas::Layout` target, linked automatically by the SDK; installed kits bundle Yoga and its MIT notice.

See [preview service profiles](docs/PREVIEW_SERVICES.md) and the installed `services.json` inventory.
Full character/Live2D preview builds its own public workers and uses separately acquired Qt/Cubism dependencies.

See [development preview](docs/PREVIEW.md) for fixed time, mock inputs, diagnostic overlays, resource reload, incremental C++ builds and state-restoring preview restart. The complete workbench uses the optional environment service and typed C ABI adapter.

Use [batch preview galleries](docs/PREVIEW_GALLERY.md) to capture all declared layouts across size/theme/DPI/service scenarios, with bounded parallel preview processes, an offline HTML index and per-case failure reports.

Use [preview baseline comparison](docs/PREVIEW_COMPARISON.md) to match two galleries, review baseline/current/difference images, and detect threshold violations, added/missing captures and execution errors.

See [API and lifetime contracts](docs/CONTRACTS.md) for compatibility, thread ownership, resource budgets and failure behavior.

Seven complete [starter projects](docs/STARTERS.md) cover static, data, animation, information, task lists, media controls and calendars. SDK 1.3 includes all seven presets, shared component services and the completed drawing migration. The installed `scripts/new-plugin.ps1` creates a named, validated project and never overwrites existing files. Canvas Workbench includes the same four component examples with independent page state.

See [drawing API selection](docs/DRAWING.md) for shapes, shared controls, visible-glyph text, gradients, image resources and offscreen surfaces.

## Create a plugin

Install the SDK developer package. Copy `share/cyberdesktop-sdk/templates/minimal` into your own project directory.
Use MSVC x64 with CMake 3.26+ and Ninja. Plugins default to the static `/MT` runtime. No Qt or protobuf dependency is needed to compile this template.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/path/to/sdk
cmake --build build --parallel 10
C:/path/to/sdk/bin/cd-sdk.exe test build/plugins/com.example.hello
C:/path/to/sdk/bin/cd-sdk-preview.exe build/plugins/com.example.hello
C:/path/to/sdk/bin/cd-sdk.exe pack build/plugins/com.example.hello hello.cdpkg
C:/path/to/sdk/bin/cd-sdk.exe validate hello.cdpkg
```

Edit the ID in `plugin.textproto`, `Plugin.cpp` and `CMakeLists.txt`, then edit the plugin implementation. Query's ID/version must agree with the descriptor. The template has independent per-instance state and a transparent RGBA surface; two reference examples demonstrate FFT and hex clock footprints; the system-data template demonstrates system subscriptions.

## Build SDK tools from source

Install protobuf using a vcpkg static-CRT/static-library triplet (`x64-windows-static`). Protobuf 33.4 is the current verified toolchain; protoc and the C++ runtime must match. The SDK does not download or build hidden dependencies.

```powershell
./scripts/build-sdk.ps1 -VcpkgRoot C:/path/to/vcpkg -Jobs 10 -InstallDirectory C:/path/to/sdk-install
ctest --test-dir build/sdk-native --output-on-failure
```

When a developer has only the installed SDK and template, protobuf is not needed; its runtime is statically linked inside the supplied tools/package runtime.

## Plugin packaging

`schemas/plugin_package.proto` defines the release format. An actual `.cdpkg` is one protobuf `PluginPackage`, embedding its strongly typed descriptor and every DLL/resource byte, with file sizes and SHA-256. Runtime resources are extracted from that file. No external manifest is needed beside the release file.

Optional descriptor tags make a plugin discoverable by style or content, for example `tags: "机械"` and
`tags: "时钟"`. Declare at most 16 unique, nonempty tags, each up to 96 UTF-8 bytes without surrounding
spaces or control characters. The catalog displays tags and searches names, IDs, authors and tags together;
multiple search terms must all match. Built-in style tags also recognize aliases such as `mechanical`,
`cyberpunk` and `tech`. Existing descriptors without tags remain valid.

The packer includes all files in the build's package directory, checks normal/delay-load DLL imports and reports missing private dependencies. Explicitly declare dependencies loaded dynamically by name. Windows OS dependencies are not bundled. VC redistributables are private dependencies unless avoided with /MT. Keep private dependency DLLs in the package root; resources may use subdirectories.

Limits: 256 MiB package, 64 MiB per file, 4096 files, 64 layouts, 64 cells and 16x16 span per layout. Each topology needs one default layout. Invalid paths, duplicates, mismatched hashes, missing declared resources/dependencies, and unsupported ABI/format versions are rejected.

Only the new C ABI/protobuf format is supported. Old Qt SDK 5 plugins, `manifest.json`, and ZIP `.cdpkg` files are not accepted.

## Tools

- `cd-sdk descriptor plugin.textproto descriptor.pb`: compile/validate an editable description.
- `cd-sdk pack DIRECTORY OUTPUT.cdpkg`: create the single release file.
- `cd-sdk inspect PACKAGE.cdpkg`: print metadata without running plugin code.
- `cd-sdk validate PACKAGE.cdpkg`: verify description and contents without running code.
- `cd-sdk extract PACKAGE.cdpkg EMPTY_DIRECTORY`: verify and unpack into an empty directory.
- `cd-sdk test DIRECTORY`: execute the plugin in a dedicated tool process and check ABI, rendering, state, multiple instances and subscription cleanup.
- `cd-sdk-preview DIRECTORY`: preview content with mock data. The Preview menu provides service scenarios (F2), common logical sizes (F3) and transparent PNG export (F9). F1 toggles diagnostics, F5 reloads, F6 opens settings, F7 switches footprints and F8 pauses/resumes. Supports `--scenario normal|loading|empty|error`, `--export-dir`, `--layout ID` and `--dpr 0.5..4`.
- `cd-sdk-preview DIRECTORY --screenshot OUTPUT.bmp`: export a content preview without opening a visible window.

Native DLL execution is not a security sandbox. The preview/test runs separately from the desktop, while the production host's process model is documented by the application. Use the official desktop application for real material, shadow and desktop integration checks. Plugin Studio is archived and is no longer shipped.

See [API contract](docs/API.md), [package format](docs/PACKAGE_FORMAT.md), [standard settings pages](docs/SETTINGS.md), and [third-party notices](docs/THIRD_PARTY.md).

中文入门：[快速开始](docs/QUICKSTART.zh-CN.md)。发布：[Release guide](docs/RELEASING.md)。

## 必需的插件设置页

每个插件必须在 `PluginDescriptor.settings_page` 声明非空设置页，并实现 C ABI 的 `get_setting`、`apply_settings`。支持 TOGGLE、INTEGER、NUMBER、TEXT、CHOICE、COLOR；定义标签、说明、分组、默认值和范围。宿主提供标准编辑控件、恢复默认、应用、保存、取消。无设置页或缺接口的插件直接拒绝，不保留旧接口兼容。

配置按实例独立保存：同一插件的两块砖可有不同配置。宿主把配置与业务状态一起保存并在新实例恢复；插件的 opaque `save_state` 仅负责自己的业务状态。`apply_settings` 必须原子应用：任一值无效则全部保留。输入字符串仅在调用期间有效，插件需要自行复制。公共 `Settings.h` 提供不依赖 Qt/protobuf 的 C++ 辅助存储与验证。

桌面右键插件砖块 →“插件设置…”；公开原生预览按 F6 打开插件自己的设置页。应用即时刷新，恢复默认仅修改编辑值，保存/应用后生效；取消不保存尚未应用的修改。SDK 检查覆盖缺声明、无效事务、配置生效与多实例独立，真实宿主额外检查设置 UI 与销毁重建后的恢复。

## Include paths and build components

Use declared include roots, for example `<sdk/services/DataServices.h>` and
`"plugins/mechanical-calendar/Calendar.h"`. Do not climb parent directories in C/C++ includes or
embed a developer machine's absolute path. Link the owning CMake target to receive its include paths.
SDK source tools use `tools/...` and `templates/...` with their own explicit source include root.

An installed kit supports `find_package(CyberDesktopSDK CONFIG REQUIRED COMPONENTS Core)` without
loading Canvas. Rendering consumers select `SDK`, `Rendering`, `Images` or `Layout`. Packaging uses
`CyberDesktopSDK::Tool` from the selected installation, including after relocating the kit.
`build-sdk.ps1` accepts up to 16 jobs with CPU affinity; `-SkipTests -SkipExamples` builds the developer
tools without the reference fixtures, and `-DisablePrecompiledHeaders` checks direct includes.
