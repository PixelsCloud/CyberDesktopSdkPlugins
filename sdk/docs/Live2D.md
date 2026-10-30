# Optional Live2D service

Query `render.live2d`, version 1, using the `CdLive2DApi` table from
`sdk/services/Live2D.h`. This is an additive service; existing plugin ABI layouts
are unchanged. Calls belong to the instance's owner UI thread. The host copies
option strings during `configure`; it never retains plugin-owned string pointers.

`configure` accepts a local exported Cubism 3+ model directory (one `.model3.json`)
or a specific `.model3.json` file. Legacy `.model.json`/`.moc` and Editor project
files are unsupported. Empty directory removes the current model. Model parsing,
PNG decoding, Cubism animation and drawing happen in an isolated worker process.
The call returns after configuration is queued, not after model loading succeeds.
Use `snapshot` to read empty/loading/ready/failed state and a bounded error message.
An explicit configuration submission can retry a failed model. There is no automatic
restart loop. Host pause/visibility policy combines with the plugin's pause option.

The host displays the model through its GPU compositor, with per-instance damage,
actual output DPI, a retained shared GPU frame and an antialiased shape mask. The
plugin's ordinary `render` callback supplies the empty/loading/error placeholder.
Software-only SDK previews show that placeholder; they do not execute Cubism.

Motion names use `group/index`; `auto` (the default) cycles authored Idle clips.
Explicit names such as `Idle/0` repeat that clip. A tap cycles the `Tap*` motions;
if no such group exists, it cycles non-Idle motions. Actions play once and return
to the selected idle mode. Cubism crossfades motion changes over 0.25 seconds.
Parameters unused by the incoming clip fade from the current base pose to their
initial values, before expression, pointer, breath, physics and pose are layered.
This clears unrelated prop switches without resetting the entire pose on click.
Rapid-click overlap is bounded to eight motion entries. Pointer tracking uses
existing modern or legacy parameter IDs and never creates missing parameters.
Expressions use their exported
names. `snapshot` supplies comma-separated name lists, truncated to their fixed
buffers. `pointer` takes normalized coordinates in [-1, 1] and a tap flag. Input is
coalesced at the host to avoid an unbounded mouse-event queue.
Coordinates specify a target: CubismTargetPoint smooths the applied direction
at a fixed 60-Hz simulation cadence, independently of 30/60-FPS rendering.
Send (0, 0) on pointer leave/cancel for a smooth return to center. New targets
preserve the current position and velocity; paused still frames do not advance it.

Model atlases use immutable premultiplied-alpha mip chains and 4x anisotropic
sampling. Mips are generated once with Qt's smooth image scaler inside the worker.
Clipping masks scale to output density (256 to 1024 pixels), with two command
buffers and mip storage included in the GPU budget. Local 4x MSAA is retained;
no whole-screen supersampling or extra full-screen refresh is used.

Limits: eight workers, 2048 output pixels per edge, 4096 texture pixels per edge,
sixteen textures, 256 assets, 32 MiB per asset, 8 MiB JSON, depth 32, 250000 JSON
nodes, 256 MiB total asset bytes and decoded texture bytes (each), 256 motions,
128 expressions, 4096 parameters/drawables, one million vertices, six million
indices and 256 blend offscreens, subject to a conservative 512 MiB model GPU
allocation estimate (textures, geometry, output and blend surfaces). Larger
outputs can therefore reject complex models that fit at smaller sizes.
Worker private commit is capped at 1 GiB.
Paths may not escape the model root or traverse resource reparse points. The
opened file's final path is checked before reading. Network paths are rejected.

Load-progress and total deadlines are 10 and 60 seconds. An active worker with
no progress for five seconds is terminated, except while shared surfaces are
under deliberate host backpressure. IPC is bounded to 64 KiB packets. Shared
texture locks are polled without waiting on the UI thread. Worker exits, hangs
and abandoned mutexes become a failed state; Windows error dialogs are suppressed
using a private, never activated desktop, process-local WER settings and fatal
exception supervision. A background thread creates the suspended child, assigns
its job before execution and handles Windows debug events. Fatal/second-chance
exceptions terminate that child before Windows can display hard-error UI. Normal
first-chance exception handling is preserved. The UI thread does not wait for
debug events. Process and thread error modes are asserted after graphics setup
and before rendering: a thread error mode can override the process setting.
Private desktop handles and pending windows are released on stop.
The fault-injection worker is test-only and must
never be deployed.

The runtime uses the pinned official Cubism Native 5-r.5 Framework/Core and D3D11.
Build-time shader preparation creates the pinned 488-entry bytecode cache; runtime
model loads do not compile hundreds of shaders on the interactive path.
