# Preview service coverage

`services.json` is the installed service inventory. Each entry links its C ABI header,
version, capability and preview profile. `release.json` lists the same IDs. A plugin
declares its own `required_services`; the preview rejects unsupported requirements
before creation. Service presence is not an authorization to access private storage.

## Profiles

The basic tools need Windows and the static SDK dependencies. Full preview adds
Qt Core/Gui/Network and a separately acquired Cubism Native SDK through
`build-sdk.ps1 -FullPreview -QtDirectory ... -CubismRoot ...`. Full preview builds
and installs its own image/model workers. It does not require Desktop, Player or
Image executables, credentials, or a user database.

| Service group | Actual preview behavior | Boundaries / verification |
| --- | --- | --- |
| Input, telemetry, media, location, weather, AQI, volumes, content and library | Deterministic snapshots and observable commands | Normal/loading/empty/error scenarios; typed owned snapshots and unsubscribe lifetime tests |
| `ui.icons` | Embedded Lucide SVGs, ThorVG alpha raster and recoloring | 128 masks / 2 MiB cache, requested edges up to 256; transparency and anti-aliasing tests |
| `ui.text_input` | Native EDIT, Unicode/IME, focus and submit | At most 2,000 code points / 16 KiB UTF-8; synchronous disconnect and owner destruction tests |
| `ui.settings_choices` | Validated atomic publication and live native settings refresh | Undeclared fields and duplicates rejected; reorder retains selected value, context change rereads only affected field, unrelated unsaved text preserved |
| `app.image_viewer` | Bounded observable command record | No private process launch; caller can inspect `openedImages()` in public tests |
| `render.images` | Per-source diagnostic registration | This is not a decoder; bounded entries and explicit unregister |
| `render.memory` | COM leases against a shared 64 MiB process budget | At most 16 MiB per lease; outstanding leases stay charged after instance teardown |
| `render.image_skins` | Validated original package image, revision zero | Optional replacement skins are not simulated. Originals remain available; traversal and capability checks, bounded subscriptions and synchronous cancellation are tested |
| `render.output` | CPU output session, allocation/layout/content versions | Reject nested, stale, duplicate and out-of-frame reports; GPU output tested separately on hardware/WARP |
| `render.canvas.gpu` | Explicit unsupported result in CPU preview | Plugins use their verified CPU path. This preview result says nothing about Desktop GPU capability |
| `render.character_images`, `.versioned` | Full profile: actual-size isolated decoding from public TOML configuration | Pinned resource paths, immutable revision/hash, bounded queue/output/global image leases; stale and invalid requests rejected |
| `ai.character_chat` | Full profile: deterministic multilingual streaming, role/mode/renderer commands | Offline samples, loading/empty/error, idle no repeated snapshots, cancellation and destruction from callback tested |
| `render.live2d`, `.live2d_actions` | Full profile: actual Cubism worker, real images and model motions | Owned model roots only; DPI/size, load failure, actions and pause/resume tested with actual models |
| `render.composition` | Full profile: background, bounded model frame and foreground CPU layers | Explicit preview readback, shared memory lease and no per-frame resource recreation; actual layered character screenshot verified |
| Interaction, keyboard, diagnostics and environment | Existing deterministic owner-thread adapters | Their C ABI headers define registration, snapshot and cancellation contracts |

Callbacks borrow payloads for the duration of the call. Retained data must be copied.
Watch objects revoke subscriptions before their context is destroyed. Optional missing
features return unsupported; decoding/model failures remain observable failures and
are never reported as successful empty renders. COM resource leases have independent
ownership. Bounds and exact return codes are specified in each linked header.

Image fixture metadata is `sdk-preview.json` inside the character package, naming
public `characters.toml` and a resource TOML file. Video playback, external attachment
opening, real network replies, persistent history and private image-skin installation
are not simulated. Core integration tests cover those host responsibilities separately.

## Evidence and limits

Public tests include `cd_sdk_preview_services_tests`, `cd_sdk_settings_window_tests`,
`cd_sdk_character_preview_tests` and `cd_sdk_live2d_preview_tests`. The private unified
acceptance script builds them from isolated public sources, runs every selected plugin
lifecycle, then reruns those lifecycles from an installed distribution with its shared
DLL/font/atlas dependencies. `result.json` records selected, generated, tested and
packaged IDs. A passed earlier report does not validate later source changes.

Preview CPU readback is intended for development and export. It is not a measurement
of Desktop GPU composition cost. GPU utilization, VRAM and GPU device time must be
reported only when actually measured. Hosted CI and a second physical machine have
not been validated by these local tests.
