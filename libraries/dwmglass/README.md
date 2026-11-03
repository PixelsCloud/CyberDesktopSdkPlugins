# Native DWM glass adapter

Native GPU compositor adapter for explicitly registered CyberGlass surfaces.

This Windows x64 module integrates the pinned OpenGlass compositor engine with CyberGlass's MIT-licensed material
renderer. It registers only explicitly opted-in application surfaces. It does not enumerate or capture background windows.
The renderer supports the React liquid material and the original OpenGlass Aero material as separate selections.

This adapter and the combined native DLL are distributed under GPL-3.0, unlike the MIT client API and other SDK libraries.
OpenGlass remains an unmodified Git submodule at `third_party/OpenGlass`, retaining its own license and attribution.
All corresponding adapter sources are in this public repository. This module is not linked into the private Desktop EXE.

Build with the existing parent Ninja directory and `CYBERGLASS_BUILD_DWM_ENGINE=ON`; build target `cyber_glass_dwm`.
The adapter uses the Legacy DWM projections for supported Windows 10 and Windows 11 versions. New MILComp generations
are not automatically supported. Exact module symbols and projection validation are required before installing hooks.
Compilation alone does not establish OS compatibility, correctness or performance.

## Integration and verification

`cyber_glass_host.exe` runs as the `CyberDesktopNativeGlass` service. Its DLL, helper and application are deployed
together; only the service loads `cyber_glass_dwm.dll` into the DWM process belonging to a connected client session.
The MIT client exchanges a bounded surface description and a backend generation acknowledgement through shared memory
and a local named pipe. Service changes and disconnects use notifications, not steady-state polling.

The native window must not have a `SetWindowRgn` mask. On the verified Windows 10 installation this changes the native
surface coverage and replaces the transmitted background with an opaque fill. Liquid uses analytic shader coverage;
Aero uses an antialiased Direct2D path generated from the same contour as the UI foreground. The optical bounds come
from the complete original geometry, independently of the current dirty or unoccluded drawing rectangles.
An empty, color-keyed GDI client with `WS_EX_LAYERED | WS_EX_TRANSPARENT` gives the helper native input transparency
without clipping its compositor child. This is not a captured background or a CPU image-processing path.
The UI foreground keeps alpha 1/255 across its optical contour so Windows routes clicks on blank material areas to
the UI instead of the underlying desktop. The helper itself remains input-transparent, including outside the contour.

Native `overLight` mixing adapts to local background luminance on the GPU: dark input retains transmission and bright
input receives the contrast overlay. The reference `Effect` API keeps the original React policy by default, while
`CompositionMaterial` explicitly selects the adaptive policy. Tests compare dark/bright output and bounded GPU
timestamp batches for the two policies; these timings exclude DWM presentation.

Live verification on Windows 10 (udwm 10.0.19041.4522, dwmcore 10.0.19041.1) covers both materials, narrow taskbar
geometry, actual foreground controls, background transmission and input through the transparent corners. Supported
Legacy projection metadata also exists for Windows 11, but Windows 11, HDR and the complete multi-monitor matrix have
not been validated by that Windows 10 run. Exact symbol/projection validation remains mandatory.
