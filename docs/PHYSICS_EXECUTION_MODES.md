# Physics execution modes

The normal official plugin mode requires the optional `host.background/v1`
service and `BACKGROUND_COMPUTE` capability. A missing service is an unsupported
configuration. Plugins do not create replacement threads or silently compute
on the UI thread.

| Shared model | Official specifications | Normal scheduling |
| --- | ---: | --- |
| Physics Toys | 35 | Fixed owner-lane deadline; UI business clock stays independent |
| Physical Sand | 18 | Business-timer checkpoints; metering and flip samples preserve order |
| Marble Clock | 12 | Fixed owner-lane deadline; UI sends business time and interaction |
| Beat Machine | 8 | Audio checkpoints; discrete beat counters form coalescing barriers |
| Brick Clock | 6 | Fixed owner-lane deadline; immutable chip geometry changes only at birth/removal |
| Streetlink Physics | 4 | Audio/business checkpoints; discrete impulses preserve order |
| RedLine Kinetics | 4 | Original 16 ms owner-lane deadline; ordered strikes and drag boundaries |
| Deep Whale BeatScene | 2 | Audio checkpoints; beat/accent events preserve order |

These 89 specifications publish pointer-free motion/statistics frames. Native
painting, images, audio envelopes, business clocks and input routing stay on
the UI owner. Four related Mecha animation specifications remain UI animations
and do not report invented Box2D steps. `tests/physics/inventory.py` resolves each
official ID through its actual factory/source model and records its mode.

The desktop host owns a fixed process-wide lane pool. The public SDK's optional
`CyberDesktopSDK::BackgroundReference` target supplies a separate Qt-free host:
interactive previews share one reference lane for the process; screenshots and
accelerated tests explicitly use deterministic owner-thread execution. This
reference host is independent of the private desktop implementation. It supports
the same bounded commands, read leases, pause/reset and cancellation contract.
Preview hot reload waits asynchronously for disposal and the last read lease
before releasing callback-module references and reopening the entry DLL.

For controlled comparisons only, set `CD_PHYSICS_EXECUTION_MODE=owner-reference`
before loading plugins. Context constructors then select the same direct
synchronous model operations and UI scheduler used by the reference path. This
mode provides **no UI isolation**. The default value is `host-background`;
unrecognized values fail explicitly. Non-context model constructors are also
explicit synchronous mathematical references for tests/design tools. Neither
path is selected because a background service failed.

| SDK building block / example | Execution mode and boundary |
| --- | --- |
| PhysicsSandbox | Explicit owner-thread teaching/reference example; its small World and SceneBinding run with its UI update loop |
| SceneBinding | Owner-thread adapter for a directly owned World/scene; do not use it to query a worker-owned World from UI |
| PhysicsParticleEmitter / PhysicsParticles | Synchronous caller-owned mathematical building block; create, step and destroy on one selected owner thread |
| Fluid/rigid-body coupling | Both coupled solvers must share one owner and their existing solve order; do not wrap an existing independent fluid worker with another per-instance thread |

Reference examples are not background-isolation or desktop performance acceptance
results. Their direct owner-thread cost must be measured for the intended workload;
they are not automatically selected by official plugins.

Ordinary commands are limited to 64 entries/64 KiB including in-flight input.
Continuous values coalesce only after the last discrete barrier. Control has
reserved storage. Paused initialization/reset may publish one static frame with
zero elapsed time; it does not enable ordinary input or periodic simulation.
Four leased snapshot slots never overwrite a reader. Immutable geometry is
retained in the same host-selected slots as its motion frame,
and readers keep their lease until both have been acquired. Cancellation is nonblocking;
in-flight work, held readers and quarantined destruction still consume quotas.
A permanently blocked callback can stop peers on its lane; thread execution does
not isolate arbitrary native crashes or provide an unlimited replacement pool.

Useful checks are `cd_background_models_tests`, `cd_physics_toys_tests`,
`cd_physics_plugins_tests`, `cd_sdk_background_tests`, and
`cd_sdk_background_reference_tests`. Deterministic trajectory checks compare the
same fixed step and preserve all existing solver/body/substep settings; they are
separate from real Native-host responsiveness and wall-clock endurance tests.

Legacy component lifecycle fixtures explicitly select `owner-reference` because
their manual hosts implement the immediate timer contract. Threaded reference
and actual Native-host fixtures separately verify background operation. Business
second-boundary subscriptions use at least 16 ms, including pause/resume just
before the next second; they do not request unsupported sub-16 ms timers.
