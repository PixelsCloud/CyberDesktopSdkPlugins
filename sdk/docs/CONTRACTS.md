# CyberCanvas 1.2 / SDK 1.4 contracts

This document describes the supported source API and the existing native C boundary. It does not announce an online release.

## Versions and compatibility

| Layer | Version | Compatibility rule |
| --- | --- | --- |
| CyberCanvas | 1.2.0 | Source APIs remain compatible within 1.x. Rebuild C++ users with one matching set of headers and static libraries; do not mix libraries from different kits. |
| CyberDesktop SDK | 1.4.0 | `release.json` supplies the SDK package, CMake and generated `<sdk/Version.h>` version. Tools print the same value. |
| Plugin C ABI | 1 | Existing prefixes remain unchanged: host 56 bytes, extended service host 88, instance 88, frame 40 and input 40 on x64. C11 packing assertions are part of verification. |
| Protobuf package | 1 | One typed `.cdpkg`, embedded files, sizes and SHA-256. It is not the old ZIP format. |
| Optional host services | 1 | Discover each service separately and validate its table size/version. Unknown services return unsupported and never authorize reading beyond the provided prefix. |

The C ABI isolates each plugin's compiler, allocator and internal C++ layout. No STL objects, Qt types, exceptions or owning COM interfaces cross that boundary. Frame memory belongs to the caller. Strings, callback arrays and host payload pointers are borrowed for the call; copy values before retaining them. The host table/context remains valid until instance destruction returns. The typed `Services` watches make owned snapshots and release subscriptions through RAII.

The base 56-byte host keeps ordinary pointer-based plugins working. Data templates declare their required data service explicitly. Interaction, icons, keyboard, diagnostics and environment are optional: an older host can retain pointer drawing without those features; environment falls back to the system clock/locale and controls retain safe defaults. Query/create must validate ABI and caller buffer sizes before writing output. ABI changes require a new ABI number; adding an optional service does not.

## Ownership and threads

`Canvas`, `Scene`, flex trees, input routers, SDK image slots, timers and update loops belong to one UI/creation thread. Runtime thread checking is not promised; callers must honor this affinity. Native host callbacks are delivered on the UI thread. Slow network/file scans and WIC decoding belong on workers. Workers publish immutable data and never use the drawing target or call UI callbacks.

A `Canvas` owns COM setup, its primary WIC/Direct2D target, brush, target recreation and frame begin/end. Prefer `frame()` or `render()` so exceptions cancel unfinished work. Borrowed factories and public legacy COM handles are native extensions: never reset/replace them, retain them past the owning Canvas, change the primary frame's begin/end, or use them from workers. Custom fonts and explicit offscreen caches must be destroyed before Canvas; independently owned offscreen targets may have their own begin/end. Keep their native memory accounting separate from the core statistics.

`ImageStore` is a shared, bounded worker. Distinct handle copies have independent cancellation interests; published `ImageData` is immutable and can cross threads. As with a shared pointer, do not mutate/cancel a handle while another thread operates on that same handle object. Handles and snapshots pin pixel memory. A released slot does not revoke another snapshot. The last store destructor joins in-progress WIC work; WIC calls are not forcibly interrupted. Slot delivery pauses while hidden, then delivers the latest result on resume.

Stop timers, watches and callbacks before destroying their owner. `PluginAdapter<T>` contains exceptions at the C boundary; implement render/settings/state behavior on the typed class. Native access violations and arbitrary DLL code are outside that exception guarantee. No callback may destroy the object currently executing it; defer owner destruction to the host event loop.

`Path` belongs to its creating Canvas factory and survives target/DPI changes. `OffscreenSurface` and
`ReadableSurface` own their drawing transactions: callbacks must not call BeginDraw/EndDraw. Their scoped clips
and masks unwind on exceptions; failed renders discard incomplete results. Keep surfaces and native uploads on
the owner thread and destroy them before Canvas. `ReadableSurface::image()` publishes an immutable CPU snapshot
that survives rerender/reset; retained native bitmaps remain usable only with their original destination target.

`nativeTextLayout` and `fitTextInk().layout` are read-only borrowed cache entries, not independent snapshots.
Measure/draw them before the next text request on that Canvas; do not mutate them or retain them across frames.
`fontCollection()` retains a custom collection and invalidates layouts when it changes. Ink fitting adds separate
font-size cache entries rather than modifying existing sizes. Text geometry uses logical coordinates, while frame
and offscreen allocations use physical pixels at the actual DPI.

## Frames, resources and limits

| Resource | Supported bound |
| --- | --- |
| Canvas output | Nonzero dimensions up to 4096 x 4096, actual DPI 0.5..4, sufficient stride/capacity, total addressed bytes fit the WIC buffer argument |
| Pixels | Premultiplied RGBA8 to the host, immutable premultiplied BGRA8 inside image resources; row padding stays caller-owned |
| Safe content | Logical coordinates; arrange and draw at the supplied actual DPI, with grayscale text AA on transparent targets and linear image sampling |
| Scene/flex tree | 4096 live nodes including root; stable surviving handles, deleted handles never reused |
| Canvas images | 64 MiB / 64 uploaded images by default, distinct from decoded store memory |
| Image store | Defaults: 64 MiB decoded, 64 MiB input file, 128 records; maximum requested edge 4096. Live references pin memory and over-budget loads fail. |
| SDK image slots | 128 slots, temporary completion timer only while results await UI delivery |
| Drawing state | 32 nested saved states; text layouts/strokes/images have bounded caches |
| Shared text | 384 layout entries; ink fitting accepts at most 128 reference strings |
| Readable surface | Up to 4096 x 4096 premultiplied BGRA pixels, DPI 0.5..4; one current snapshot and one cached native destination; externally retained snapshots are caller-owned |
| Preview state | Native payload 2 MiB, whole state file 5 MiB; bounded settings and mock input parsing |
| Plugin package | 256 MiB total, 64 MiB per embedded file, 4096 files |
| Production host timer | 16..3,600,000 ms; a host may reject an interval and replacement must remain transactional |

These are component budgets, not a process-wide memory cap. Engine metrics exclude caller output buffers, WIC/DirectWrite/driver internals, host upload/composition and dedicated plugin caches. Shared decoded bytes must not be added once per referencing instance. No full-screen supersampling or periodic full-screen repaint is needed.

## Invalidation and input

Keep scenes and resources alive across frames. Layout changes invalidate measured geometry; color/value changes normally invalidate painting. Arrange before drawing, hit testing, focusing or reading geometry. Never mutate structure inside measurement/drawing callbacks. Structural changes cancel stale pointer capture and preserve surviving business keys and values.

Pending layout exposes no stale focused geometry. A control that is disabled, hidden, removed, zero-size or fully clipped cannot receive focus. A valid rearrangement can preserve logical focus; once final arranged bounds remove it, restoring size does not resurrect focus. Keyboard focus remains subject to the optional host grant.

`Timer` replaces a subscription only after the candidate succeeds. `UpdateLoop` excludes hidden time and suspends subscriptions at speed zero. A finite animation delivers its last changed frame before stopping. `RedrawRequests` coalesces changes until the next instance frame; unchanged data or retained resources should request no new texture. Host composition and public C ABI frame upload remain separate from core draw statistics.

## Failures and validation

Invalid arguments/buffers return the corresponding C result; typed C++ graphics operations may throw and the adapter contains them. Check return values, retain usable state on failed settings/subscription replacement, and use resource Loading/Failed placeholders. PNG export may quantize straight-alpha boundary pixels; normal frame readback only reorders channels and preserves premultiplied color levels. Exporting a background never modifies source pixels or row padding.

The settings schema describes fields, not every cross-field rule. The lifecycle tool validates current-value transactions and whole-instance rejection atomicity, tries other fields when a coupled candidate is legitimately rejected, and verifies accepted changes and instance isolation. A fixed/coupled-only schema can have no independently accepted candidate; the tool does not claim to exhaustively validate its domain rules. Keep plugin-specific tests for those rules. Editable `plugin.textproto` is authoritative for dev test/preview/packing; malformed edits fail rather than loading a stale compiled descriptor.

Development, preview and package checks execute native DLLs. SHA-256 verifies package bytes, not publisher identity or a sandbox. Ship only reviewed code and declared files. Local validation does not replace a separately performed hosted CI run or testing on a second physical machine.
