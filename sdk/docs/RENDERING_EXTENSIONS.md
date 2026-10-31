# Rendering extensions — SDK 1.4 / CyberCanvas 1.2

This development kit retains native C++ compilation, Windows x64 `/MT`, and the existing C ABI 1.
It adds reusable rendering components without a game engine runtime, Qt dependency, scripting VM or global frame loop.
Rebuild C++ code with the matching headers and static libraries. Previously compiled C ABI 1 plugins remain loadable.

## Link only the components you use

```cmake
find_package(CyberDesktopSDK 1.4 CONFIG REQUIRED)
cyberdesktop_add_plugin(my_plugin ID com.example.physics
  DESCRIPTOR "${CMAKE_CURRENT_SOURCE_DIR}/plugin.textproto" SOURCES Plugin.cpp)
target_link_libraries(my_plugin PRIVATE CyberCanvas::PhysicsScene)
```

| Target | Header | Purpose |
| --- | --- | --- |
| `CyberCanvas::Layout` | `Scene.h` | Retained layout, groups, transforms, controls and opt-in subtree caches |
| `CyberCanvas::Motion` | `Motion.h` | Keyframes, append/join, seek, reverse, replacement, weak ownership |
| `CyberCanvas::Physics2D` | `Physics2D.h` | Box2D world without Windows/Canvas types in its public interface |
| `CyberCanvas::PhysicsScene` | `PhysicsScene.h` | Scene poses, picking, mouse joints and debug drawing |
| `CyberCanvas::Effects` | `Effects.h`, `EffectsDrawing.h` | Content blur, soft shadow, glow, inner shadow, reusable ellipse shadow |
| `CyberCanvas::Text` | `Text.h` | Mixed spans, glyph outlines/gradients, links, inline images and snapshots |
| `CyberCanvas::AnimatedImages` | `AnimatedImages.h` | WIC GIF, PNG sequences/atlases and an explicit background decoder store |
| `CyberCanvas::Vector` | `Vector.h` | Restricted SVG and Lottie through CPU ThorVG |
| `CyberCanvas::Particles` | `Particles.h` | Fixed-step bounded particles, curves, atlas cells and optional wind |
| `CyberCanvas::PhysicsParticles` | `PhysicsParticles.h` | Box2D collision, restitution, friction, rotated sprites and box debris |
| `CyberCanvas::Fluid` | `Fluid.h` | Optional Salva 2D liquid, viscosity, surface tension, solid boundaries, Box2D reactions and background snapshots |
| `CyberCanvas::Noise` | `Noise.h` | Optional FastNoiseLite scalar fields, seamless time sampling and particle wind |
| `CyberCanvas::Charts` | `Charts.h` | Bars, donuts, heatmaps, shared numeric axes, hits, keyboard and semantics |

The full SDK builds extensions by default. For a smaller source build use
`-DCD_SDK_BUILD_RENDERING_EXTENSIONS=OFF` with manifest feature `rendering-extensions` disabled.
Independent CyberCanvas builds opt in using `CYBERCANVAS_BUILD_PHYSICS`, `CYBERCANVAS_BUILD_EFFECTS`, and
`CYBERCANVAS_BUILD_VECTOR`. Installed clients do not need vcpkg, Box2D or ThorVG headers.
No component downloads dependencies during an installed-client build.

## Coordinate, rendering and ownership rules

Scene/layout/Canvas/input coordinates are logical DIP. Physical image dimensions are actual DPI-scaled pixels.
`View::transform` moves the entire subtree; its pivot is relative to the node's untransformed layout bounds.
`View::pose` remains the earlier image-only transform. `paint` uses layout coordinates; `paintLocal` starts at `(0,0)`.
Use `layoutBounds()` for layout/physics, `visualBounds()` for transformed clipped diagnostics, and `worldTransform()`
for explicit conversion. Parent clips and group opacity affect descendants. Transparent groups do not receive input.

Set `cacheSubtree=true` only for content that is expensive and mostly static. Position/opacity reuse the cached raster;
scale/DPI changes rebuild at effective DPI. Call `invalidate()` when custom paint data changes. The scene limits all
cached layers to 16 MiB, 4096 pixels per edge; extreme effective DPI uses direct painting. Cached content does not
replace the primary Canvas target. Nested drawing still respects Canvas's bounded 32 state/mask scopes.

All Canvas, Scene, Timeline, Effects and physics operations belong to their creating UI thread. Do not mutate a scene
inside layout/paint callbacks or reenter a particle wind callback. Rendering callbacks may not call BeginDraw/EndDraw.
Host invalidation requests are local to the plugin texture; use `RedrawRequests`/`UpdateLoop` to coalesce them.
Stop decorative updates when inactive, hidden or asleep. Data service subscriptions have their own business cadence.

## Time and physics

`Timeline` uses seconds. Property IDs identify one writer; retarget with `animate()` to start at the current value.
`append()` sequences tracks, `join()` runs at the last appended start. Infinite tracks cannot be followed by a finite
append. `seek()` samples without completion events; run `dispatch()` at an update boundary, outside painting.
Provide a weak owner token for callbacks bound to objects whose lifetime is shorter than the timeline.

Physics uses meters/seconds/radians, positive X right and Y down. Defaults: 1/60 s step, four substeps, at most five
catch-up steps. Large elapsed gaps are dropped and counted. Never feed hidden elapsed time on resume. Speed is 0–2;
zero/pause excludes elapsed time. Render `interpolated()` poses; `teleport()` resets interpolation.

Default world caps are 160 bodies (128 dynamic), 256 shapes and 64 joints. Bodies, shapes and joints are validated handles
local to one linked module. They cannot cross worlds or DLLs. Body destruction invalidates attached shapes/joints.
Distance/revolute anchors are body-local; mouse targets are world coordinates. Contacts/sensors are copied events;
no cross-version event ordering is promised. Use `enqueue()` for mutations originating from event handling.

Drain copied events after each `advance()`: use `takeEvents()` when the model needs them, or `discardEvents()` for
visual-only scenes. Discard retains the buffer capacity for reuse and enforces the same owning-thread rule.
Unconsumed events accumulate up to 65,536 entries; advancing beyond that limit throws, rather than growing memory
without a bound. An SDK `UpdateLoop` stops after an update exception, so failing to drain events can stop animation
even while audio callbacks continue. `discardEvents()` requires matching updated headers and the Physics2D library;
the plugin C ABI remains 1. Use `active()` to stop idle scheduling; a stationary kinematic body alone is inactive,
while a new velocity, awake dynamic body or queued command can resume simulation.

The optional `RevoluteOptions` overload adds relative angle limits (radians), a motor speed (radians/second),
and a maximum torque (N*m). Limits are relative to the pose when the joint is created. The original four-argument
overload remains available. `revoluteMotor(joint, speed, maximumTorque)` changes the drive atomically; zero speed
holds a load within the torque budget. A continuously driven hard stop has Box2D angular solver slop; servo motion
should slow near the target rather than continuously pushing against its stop. These options require matching
updated C++ headers and the static Physics2D library; the plugin C ABI remains 1.

`SceneBinding` must die before both Scene and World. Bound visual nodes are direct children of a physical root, with
uniform positive ancestor scale. DPI alone does not resize the meter world. Logical container resizing is a plugin
policy; the sandbox rebuilds the contained scene. Drag views set `draggable=true`; dispatch through `cdsdk::Scene`
to acquire host capture, then pass the same safe-content origin to `beginDrag()`. Cancel the joint on release,
cancel, lost capture, hide, reset and teardown. `debugDraw()` shows shapes, joints, centers, velocities and query results.

## Effects and text

`EffectSurface` returns immutable CPU pixels. `Automatic` prefers WARP when the padded output contains at most
512 × 512 physical pixels (1 MiB of BGRA pixels), avoiding costly synchronous hardware readback for small snapshots.
This is an area limit, not a limit on each edge. Larger outputs prefer WARP on unified-memory (UMA) adapters and
hardware on discrete adapters; hardware is retained when the UMA query is unavailable. Crossing the area limit
rebuilds native resources as needed, while previously returned CPU snapshots remain valid. Explicit `Hardware`
and `Warp` requests keep their requested backend, and `stats().backend` reports the actual path. This policy is
independent of the Canvas drawing backend. Gaussian/shadow quality remains `QUALITY` on both paths.

Effects retain their native graph and skip unchanged uploads. Output clipping skips transparent margins while
keeping the complete filter input, padding and output dimensions. WARP can round 8-bit filter intermediates
slightly differently when the evaluated region changes; cached results remain immutable and exact.

Effects accept immutable premultiplied BGRA and produce immutable CPU images. RGBA style colors remain straight alpha,
matching Canvas. Three sigma of padding is added per stage, including offsets; draw at `EffectResult::offset`.
Automatic mode tries the other backend if creation of the preferred Direct3D device fails.
This blurs plugin content, not the desktop behind the window. A failed device/operation throws and drops its native
resources; a later render can rebuild. Keep a previous CPU snapshot for a plugin-owned failure fallback.

At most eight effects, sigma 0–64 DIP, offsets ±256 DIP and 4096-pixel edges are accepted. Cache keys include immutable
image identity, effect parameters and DPI. `includeSource=false` produces an effect-only shadow/glow layer.
`ShadowPainter` reuses eight ellipse variants, so position-only movement does not rerun the effect.

`StyledText` is an immutable DirectWrite layout: recreate it when text, width, font collection, span styles or language
change. Spans support weight, character spacing, tabular figures, underline, color/gradient, outline, opaque link IDs and
inline images. The application chooses what a link does; the renderer never opens a URL. Draw links/hit regions from the
same layout. DirectWrite shapes bidi/complex scripts. COLR glyph palettes are preserved; bitmap/SVG color emoji may fall
back to monochrome. Snapshot padding must include outlines and overhang. Transparent text uses grayscale antialiasing.

## Animation assets and budgets

Share a `MemoryBudget` explicitly between `ImageStore`, `AnimationStore`, and `EffectSurface` in one plugin runtime.
The default is 16 MiB. Published snapshots retain their lease after a store is destroyed. Native parser/driver internals,
process RSS and Canvas's separate caches are not falsely included in this pixel accounting. Multiple static plugin DLLs
do not share a hidden singleton. Aggregate instance costs through host diagnostics.

`AnimationStore` owns one worker, up to 128 requests, and three decoded frame variants per source. The worker alone owns
decoder objects, including their destruction. `AnimationHandle::seek` coalesces requests; stale generations never replace
newer snapshots. The previous frame remains visible while another frame is loading. `FramePlayer` is timer-free: call
`advance`/`tick`, set physical `size`, inspect `snapshot` and `active`. While a first decode is Loading, schedule bounded
polling; stop polling on Ready/static, Failed or Cancelled. Pause/hide resets the clock anchor. Unload joins the worker.

| Format | Supported subset | Rejected/limited |
| --- | --- | --- |
| PNG sequence / atlas | Fixed cells, explicit unequal durations, smooth DPI scaling | Inconsistent dimensions, invalid grids |
| GIF | Palette, transparency, partial frames, disposal 1/2/3, replay for backward seek | 10000-frame cap; zero delay normalized to 10 ms |
| SVG | Shapes, groups, transforms, gradients, clips, masks, local `use` | Scripts, DTD, CSS stylesheets, text, filters, image tags, external references |
| Lottie | 2D shapes/transforms/masks/gradients, package or embedded PNG/JPEG images | Expressions, fonts/text, audio, camera, 3D and layer effects |

Assets stay inside the canonical package root; absolute paths, URLs, ADS and escaping links/`..` are rejected.
Files are capped at 8 MiB by default, images at 4096 per edge, animations at 10000 frames/24 hours, and parser structure
has depth/count limits. Lottie images use WIC and a resolver; synthetic internal asset names never perform network IO.
Unsupported assets fail visibly instead of silently executing another loader. Use PNG sequences for unsupported artwork.

Particles use a fixed 120 Hz step with bounded catch-up, deterministic seeds, maximum 1024 particles (default 256),
local/world space and normalized lifetime curves. A stopped emitter remains active until particles die; hidden/reduced
motion freezes it. This is a small CPU particle renderer, not a GPU batch engine. Charts accept stable nonzero IDs,
bounded data (4096, or 64 donut sectors), missing values, shared axes and a formatter. Use the same bounds for draw/hit/
semantics. `append` evicts old samples without changing surviving IDs. Negative donut values are rejected.

## Physical particles and liquids

These are separate optional simulation backends. `PhysicsParticles` uses the existing static Box2D;
`Fluid` uses [Salva](https://github.com/dimforge/salva) 0.10.0 and Parry 0.30.2. All public APIs remain C++20,
Windows x64 `/MT`, with no Qt, scripting VM, runtime DLL or extra plugin ABI requirement.
The private fluid backend is a Rust static library: source builds require the stable Rust MSVC toolchain and Cargo;
installed consumers link the packaged static libraries without Rust or third-party headers.

Enable `CYBERCANVAS_BUILD_PHYSICS=ON` for rigid particles. Enable `CYBERCANVAS_BUILD_FLUID=ON`
(or `CD_SDK_BUILD_FLUID=ON` in an SDK source build) for fluids. Fluid is opt-in and supports Release /
RelWithDebInfo `/MT` source builds; the packaged backend is not a `/MTd` Debug library.

```cmake
find_package(CyberCanvas 1.2 CONFIG REQUIRED COMPONENTS PhysicsParticles Fluid)
target_link_libraries(my_plugin PRIVATE CyberCanvas::PhysicsParticles CyberCanvas::Fluid)
```

```cpp
#include <CyberCanvas/PhysicsParticles.h>
#include <CyberCanvas/Fluid.h>

cybercanvas::PhysicsParticleSettings settings;
settings.particles.maximum = 64;
settings.particles.gravity = {0, 500};  // DIP/s², y points down.
settings.restitution = .7;
cybercanvas::PhysicsParticleEmitter sparks(settings);
// Props use METERS. Never call sparks.world().advance() separately.
auto floor = sparks.world().createBody({.type = cybercanvas::physics::BodyType::Static,
                                        .position = {1, 1.5}});
sparks.world().box(floor, {1, .05});
sparks.emitter().origin({100, 30});
sparks.emitter().burst(20);
// Per local animation tick: sparks.emitter().advance(elapsed), then draw(canvas).
```

Rigid particles share the existing emission, seed, wind, lifetime, atlas, size, color and opacity curves.
Each particle has a stable ID and a clockwise rotation in degrees. Collision geometry stays fixed while visuals
fade; use `ParticleCollider::Box` and `halfExtent` for rotating debris, or circles for sparks/rain/snow.
World-space simulation is required; default 64 particles, hard cap 256. The emitter owns its backend and clock
and is noncopyable/nonmovable. Particle death/reset destroys only owned particle bodies, preserving props.
Wind and gravity are accelerations in DIP/s²; conversion to Box2D force includes the actual circle/box mass.
For intentionally spinning sprites, set `sleepingAllowed=false`; tiny rotating circles can otherwise satisfy
Box2D's sleep threshold while still appearing to rotate visually.

```cpp
// Independent liquid effect: construct once, enqueue bounded initialization.
cybercanvas::FluidWorker liquid({.radius = 2, .viscosity = .02});
liquid.submit([](cybercanvas::FluidSystem& simulation) {
  std::vector<cybercanvas::FluidSample> drops;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 12; ++x) {
      drops.push_back({{20. + x * 4, 20. + y * 4}, {}});
    }
  }
  simulation.add(drops);
  std::vector<cybercanvas::FluidSample> floor;
  for (int layer = 0; layer < 3; ++layer) {
    for (int x = 0; x <= 200; x += 4) {
      floor.push_back({{double(x), 140. + layer * 4}, {}});
    }
  }
  simulation.boundary(floor);
});
// Tick: liquid.advance(elapsed); render only when the published revision changes.
// Render: auto view = liquid.snapshot(); view->draw(canvas);
// Visibility/preferences: liquid.activity(visible, paused, reducedMotion);
// Surface errors: view->error; recreate a faulted worker rather than retrying forever.
```

Fluid positions, velocities, radius and gravity use DIP/seconds. `pixelsPerMeter` defaults to 100; density uses
kg/m² in the 2D model. Viscosity (0–1), surface tension and adhesion are solver coefficients, not SI material
calibration. Salva IISPH solves pressure; XSPH viscosity and Akinci surface tension alter real particle motion.
The fixed outer clock is 120 Hz, with 4 equal substeps by default (configurable 1–16) and at most 30 pressure
iterations per substep. Equal steps preserve pressure warm starts; there is no adaptive CFL loop.
Catch-up is limited to 8 outer steps by default (configurable 1–30), with dropped steps counted in stats.

Default fluid capacity is 1024, hard cap 4096; each scene allows 64 boundary objects and 8192 total boundary
samples. Oversized/invalid requests reject before solver mutation. Liquids persist until `reset()`: they do not
have the rigid emitter's lifetime/atlas behavior. Sample solids no farther apart than `2*radius`, preferably
two or three layers. Each boundary sample contributes a solid disc, inflated by the liquid radius for contacts;
Parry's BVH and swept ray tests prevent isolated/high-speed spray from tunneling through sampled walls.
This approximates sampled solids rather than reconstructing an arbitrary mesh surface.

`FluidBodyBinding` is available when Physics2D was built. Keep the fluid, World and binding on one simulation
thread. For every 1/120 second: synchronize boundary poses/point velocities, advance fluid, apply reactions,
then advance Box2D. Local samples use DIP relative to the body origin; the adapter converts linear impulses
and moments about the body's center of mass. `takeReaction` consumes accumulated SI impulses once.
Both solvers must outlive a binding; reset invalidates its boundary and requires recreating the binding.
This is per-scene coupling, not a shared desktop world or cross-plugin collision service.

`FluidSystem` is timer-free and confined to one owning thread. `FluidWorker` provides one owned simulation
thread, up to 64 pending FIFO commands, coalesced elapsed time and immutable snapshots. Check submission
return values under load. Commands must not capture UI objects, Canvas or a UI-owned Box2D World. To run
a coupled scene on a worker, construct its World/bindings in the owning worker command and destroy them there.
Pause/hidden/reduced-motion changes discard pending elapsed time and suppress stale in-flight publication;
destruction joins the worker. Solvers stop on a contained backend error; synchronous instances can `reset()`.
Use snapshot revisions for local plugin invalidation, actual-DPI Canvas output and host antialiasing.
Do not create an independent desktop-wide frame loop or request full-screen refresh.

Fluid snapshots currently draw antialiased overlapping colored particles. Smooth fluid-surface reconstruction,
refraction/caustics, GPU simulation, non-Newtonian materials and 3D fluids are not implemented.
Use small pools and profile the chosen particle/substep count before selecting the animation rate.
`cybercanvas_particle_effects_demo <output>` creates a 12-effect gallery: sparks, rain, snow, petals, box debris,
elastic balls, fireflies, buoyant smoke particles, water, syrup, a fountain and a motorized fluid stirrer.
It exports 240 native samples per effect at 60 FPS to `effects-data.js`, plus 36 native PNG posters at
100/150/200% DPI. The offline `index.html` draws those trajectories with pause, speed, seek, category,
individual pause and enlarged views; browser playback contains no replacement physics solver.
Smoke/fireflies use lightweight particle advection and visual gradients, not a volumetric gas solver.
The sample exporter reuses optional nlohmann_json and repository Lucide SVGs; these are not fluid-library
runtime dependencies. The executable/output stay in the fixed build/Testing directories.
The install includes the backend source, locked dependencies, exact source links, third-party notices and Rust
standard-library notices. Refresh these with `scripts/update-fluid-licenses.ps1` when changing dependencies/toolchain.

## Coherent noise and wind

Build the standalone library with `CYBERCANVAS_BUILD_NOISE=ON`; SDK rendering extensions enable it by default.
Source builds require FastNoiseLite 1.1.1 from `overlay-ports/fastnoiselite` in the SDK manifest. For classic vcpkg:

```powershell
vcpkg install fastnoiselite:x64-windows-static-release --overlay-ports=./sdk/overlay-ports --overlay-triplets=./sdk/triplets
```

Installed consumers only need the exported static target; FastNoiseLite headers and a runtime DLL are not required:

```cmake
find_package(CyberCanvas 1.2 CONFIG REQUIRED COMPONENTS Noise)
target_link_libraries(my_plugin PRIVATE CyberCanvas::Noise CyberCanvas::Particles)
```

```cpp
#include <CyberCanvas/Noise.h>
#include <CyberCanvas/Particles.h>

cybercanvas::NoiseField breeze({.seed = 42, .frequency = .02, .octaves = 2});
// Use paused/scaled animation time, not wall-clock time. Position is in DIP.
double angle = 3.0 * breeze.animated({100, 50}, animationSeconds);

cybercanvas::ParticleEmitter sparks;
sparks.wind(cybercanvas::NoiseWind({.seed = 42, .strength = 8}));
// Continue using the existing emitter pause/visible/reducedMotion/advance API.
```

`sample(x,y)` and `sample(x,y,z)` return [-1,1]. Algorithms are OpenSimplex2S, Perlin and Value;
fractal modes are None, FBm and Ridged. Settings are immutable after construction and copies share immutable state.
Sampling is allocation-free and const sampling may run concurrently. No timers, textures, global state or redraws are created.
Frequency is 0.000001–1000, octaves 1–8, lacunarity 1–4 and gain 0–1. Nonfinite/invalid arguments throw
`std::invalid_argument`; coordinates must stay within `4,000,000 / (frequency * lacunarity^(octaves-1))`
(None uses one octave), protecting the upstream integer lattice.

`animated(position, seconds, speed)` follows a circular path through the 3D field, rather than allowing the time
coordinate to grow indefinitely. Its period is `64*pi/speed` seconds (about 670 seconds at the default 0.3);
both value and motion are continuous at the loop boundary. Speed 0 freezes the field. Time must be finite and
within 0–1e9 seconds, speed within 0–10. Leave room for a horizontal offset of `64/frequency` when sampling near coordinate limits.
`NoiseWind` bounds the acceleration vector's magnitude by strength (0–10000 DIP/s²); spatial frequency is
0.000001–10 and temporal frequency 0–10. It plugs into the existing fixed-step simulation without changing scheduling.

Koi water/plant drift, potted plant sway and forest/fire particles now consume this module. Existing day/night,
speed, transparent background and pause controls remain authoritative. Noise is blended with the original motion
to retain the established appearance. Distribute `share/cybercanvas/third_party/fastnoiselite/LICENSE` with consuming plugins.
The repository's `cybercanvas_noise_tests` covers bounds, continuity, determinism, invalid inputs, uptime, concurrency,
refresh-rate-independent particles and pause/visibility/reduced-motion behavior.

## Examples, validation and distribution

### Physics scheduling, events and diagnostics

Each instance retains its own World, created, advanced, queried and destroyed on
one owner thread. A host may share deadline scheduling between UI subscriptions;
this preserves the existing UI callback contract and does not combine Worlds.
Pause excludes hidden elapsed time. An already-running UpdateLoop must not be
restarted on each audio callback, because that can postpone updates indefinitely.

`WorldOptions::collectEvents` defaults to true. Consumers must regularly call
`takeEvents()`; `discardEvents()` clears already-copied notifications while
retaining capacity. Visual-only scenes should set `collectEvents=false`, which
avoids creating unused contact, sensor and hit notifications without changing
collision response. `PhysicsParticleSettings::collisionEvents` defaults to false;
set it explicitly to true before construction when consuming emitter collision
events through `world().takeEvents()`. Advancing the emitter's World separately
is still forbidden. Brick, marble and percussion gameplay event consumers retain
event collection and their existing processing.

UpdateLoop exceptions stop only that loop and produce one bounded error log.
`failed()` exposes the fault; repeated `start()` calls do not retry it. After
correcting or rebuilding the model, explicitly call `resetFailure()` and then
start it again. Resetting alone does not start a timer.

Optional physics diagnostics occupy previously unused `dev.diagnostics/v1`
slots, without changing its struct size. A PluginAdapter implementation can
provide `physicsCounters()` returning `std::array<uint64_t,4>`: completed fixed
steps, cumulative advance wall nanoseconds, awake bodies and maximum advance
wall nanoseconds. Registration belongs to the adapter and is removed before
the plugin object is destroyed. No callbacks may outlive module unload.
Manual adapters can use `plugin_detail::PluginDiagnostics` with the same
lifetime order. Sampling occurs only on request on the owner thread.

Timing requires `WorldOptions::measureTimings=true` or the explicit test process
environment `CYBERDESKTOP_PHYSICS_PROFILE=1` before World creation. Otherwise
duration counters stay zero and no per-advance clocks are read. These measure
CPU-side wall time including possible preemption, not process CPU or GPU time;
maximums must not be summed across Worlds.

`tests/physics/inventory.py` resolves official catalogs through source/family
models and separates inactive design-only entries. `cd_physics_plugins_tests`
loads real DLLs and verifies progress, audio pulses, pause/resume, image loading
and teardown. Its optional 1/2/4 worker argument is a headless execution-lane
experiment; it does not make production UI callbacks asynchronous.

`examples/physics-sandbox` is an installable 4×4 playground with dragging, count/speed, pause, reduced motion, debug draw
and versioned state. Its optional spring mode suspends the first ball from a visible damped distance joint; switching
modes cancels dragging and rebuilds the world. `examples/rendering-gallery` combines Lottie, particles, timelines,
rich text and live CPU charts.
Its Lottie artwork was authored for this repository and is covered by the SDK MIT license.
The real koi, music and plant plugins consume text/effects/groups; forest/fire noise scenes consume particles.

Build examples from the installed `share/cyberdesktop-sdk/examples` directory with only the SDK in `CMAKE_PREFIX_PATH`.
Run `cd-sdk test`, `cd-sdk-preview --frames 120 --screenshot preview.png`, then package with `cd-sdk pack` and `validate`.
The release script performs installed and exported-source builds and includes the correct dependency notices.
Box2D and ThorVG are static; distribute their licenses with consuming plugin packages. ThorVG's notice includes RapidJSON.
The rendering-gallery also includes nlohmann-json's MIT notice. Godot is a reference only and contributes no linked code.

Core contract tests are selected by `scripts/verify-rendering-engine.ps1` in the application checkout. The SDK adds
`physics_sandbox_contract`, plus `cd_sdk_rendering_endurance PHYSICS GALLERY OUTPUT SECONDS` for real wall-clock observation
after 500 lifecycle cycles. Output records private bytes/handles separately from logical resource budgets. A short run
does not satisfy the 30-minute/two-hour gate. The repository execution record lists passed, unrun and external-environment
checks; generating archives is not public publication or evidence of every hardware/OS combination.
