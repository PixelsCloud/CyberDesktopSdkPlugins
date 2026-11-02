# Liquid Glass background capture

The React material is shared by all capture paths. Windows capture capabilities
are queried on the actual session and frame interfaces; the Windows build number
alone does not select an implementation.

- `windows-display-wgc-session-exclusion` uses monitor capture with session-local
  HWND exclusions when the session supports them.
- `windows-window-stack-wgc` composites bounded, visible window sources in GPU
  memory when session-local monitor exclusions are unavailable.
- `windows-window-stack-wgc-shared-gpu` also consumes an application's published
  desktop texture. `FramePublisher` copies only requested physical regions into
  a shared D3D11 BGRA texture; `FrameConsumer` opens it on the capture device.

`CaptureSource::backend()`, `capabilities()` and `error()` describe the current
source, independently from the requested material. A usable source is required
before a caller can report an actual Liquid presentation. Failure must remain
visible to the caller; a system blur fallback is not a successful Liquid render.

## Lifetime and limits

Capture sessions and GPU composition belong to the capture worker. WinRT
callbacks keep only the latest pending frame, then wake that worker. Window
enumeration is triggered by relevant WinEvents, not by a permanent polling loop.
There are at most 12 contributing window sources and a shared 256 MiB source
texture/frame-pool budget. Full source pools and both sides of shared GPU
textures are counted; the ROI size is not substituted for full pool costs.
Material-region textures have a separate bounded budget.

An optional `FramePublisher` belongs to the application's renderer and its
physical screen bounds. The request callback must schedule publication on that
renderer's owning thread. It must not synchronously re-enter the publisher or
consumer. Device, context and texture arguments are borrowed native interfaces.
No CPU pixel download, cross-process database or hidden capture window is used.
`setUnavailable()` releases the published texture and reports a reason; a later
valid publication resumes consumers. Subscribers release their requests and
shared resources when their lifetime ends.

The compatibility path does not modify global window display affinity. It also
does not hide the real glass windows to capture a frame. Optional cursor,
border, update-interval, dirty-region and frame-revision controls are probed
independently. Missing dirty regions use bounded GPU ROI change comparison;
missing update-interval control limits consumption, not Windows' capture rate.

## Verified scope and unsupported inputs

Windows 10 22H2 (19045), SDR, NVIDIA hardware has been used for targeted tests:
ordinary occluded windows, changing color identity, published textures between
separate D3D11 devices, unsupported-source fallback, and subsequent recovery.
This does not establish support on all Windows releases, display adapters,
HDR modes, remote capture tools or protected desktops.

Some native tool/acrylic windows on this Windows version reject WGC window
capture. They must produce an explicit fallback rather than missing layers,
black pixels or stale Liquid content. A desktop background composed by an
external wallpaper process is outside the application-published texture and
requires a separate valid source. Such cases are not claimed as working Liquid.
Relevant window changes or a resumed published source allow automatic recovery;
device failures retain the coordinator's bounded retry/reset policy.

GPU material timestamps measure the commands enclosed by the material timer.
They do not measure WGC's internal work, source composition, publisher copies,
DWM presentation time or total GPU utilization. Static material copy/upload and
draw counters must likewise not be presented as whole-pipeline counters.
