# Choosing the drawing APIs in SDK 1.3

Start with one of the seven [starter projects](STARTERS.md). They compile as C++20 with the installed SDK;
no application source, Qt installation or protobuf installation is needed by a plugin consumer.

| Content | Public entry point |
| --- | --- |
| Transparent frame, actual DPI, shapes and transformations | `sdk/Canvas.h`, `frame()`/`render()`, `saved()` |
| Curves and custom silhouettes | `canvas.path()`, `Path::lineTo/cubicTo/arcTo`, `fill`/`stroke` |
| Normal text and visible-glyph fitting | `text`, `measureText`, `measureTextInk`, `fitTextInk` |
| Shared information and interaction controls | `sdk/Controls.h`, `sdk/Typography.h`, `CyberCanvas/DesktopWidgets.h` |
| Retained layout, focus and accessibility | `sdk/Scene.h`, `sdk/Keyboard.h`, public components |
| Function icons | `sdk/Icons.h`, host Lucide service; keep content/brand artwork separate |
| Background image decoding and completion | `sdk/Images.h`, `ImageStore`; do not decode in `render()` |
| Existing decoded pixels | `ImageData::fromPremultipliedBgra`, `canvas.image`, cover/contain |
| Gradient brushes | `canvas.linearGradient`/`radialGradient`; retained bounded target-specific caches |
| Cached layer and clipping | `OffscreenSurface`, `canvas.masked`, scoped clips |
| CPU pixels for a custom warp | `ReadableSurface`; immutable `ImageData` snapshots |
| Animation and data refresh | `sdk/Update.h`, owned host subscriptions, local redraw coalescing |

Keep reusable drawing objects as instance members, declared after the Canvas so they are destroyed first.
Layout callbacks update geometry; input callbacks update the model; rendering consumes that state. Release watches
and timers when hidden, and avoid requesting redraws when visible content has not changed. Network/file scans run
on workers; native drawing, layouts, UI callbacks and uploads stay on the owning thread.

Use logical coordinates for content and allocate textures at the host's actual DPI. Transparent text uses grayscale
antialiasing, paths use primitive antialiasing and images use linear sampling. Do not add full-screen supersampling
or repaint the whole desktop to improve a small plugin.

For specialized styles, borrowed native targets/layouts remain available. A plugin may implement fish skeletons,
particle simulation, perspective sampling or outlined text itself. Reuse the public resource lifetime and cache
facilities rather than making another private canvas or per-frame font cache. Native target pointers and shared
layouts must not be mutated outside their documented scope.

The complete API examples and bounds are installed in `share/cybercanvas/README.md`. Read
[ownership and compatibility contracts](CONTRACTS.md) before retaining resources. Rebuild C++ plugins with the
headers and static libraries from one SDK version; the DLL boundary remains C ABI 1. Existing SDK 1.2-built ABI 1
plugins do not need to be rebuilt just to coexist with SDK 1.3 plugins.

Before distribution, run lifecycle validation, capture fixed-time DPI/theme previews, inspect the `.cdpkg`, and
follow the [release and rollback procedure](RELEASING.md). A preview with mock services does not prove real network,
audio or desktop integration; perform those checks for plugins that require them.
