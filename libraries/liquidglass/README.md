# CyberLiquidGlass

MIT-licensed GPU optical material rendering and the client protocol for the native
DWM adapter. The DWM adapter is a separate GPL component under `libraries/dwmglass`.

The screenshot-based material backend has been removed. This library no longer
provides desktop/window capture sessions, Magnification/BitBlt samplers, window
stack reconstruction, application background-frame publication, capture workers
or capture statistics. It has no Magnification dependency.

`Effect` renders optical materials from an explicitly supplied D3D texture.
`CompositionMaterial` copies only the supplied compositor region and sampling
margin, then runs the GPU optical passes. It owns intermediate textures; this is
not zero-copy or zero-cost. Its opt-in metrics distinguish GPU texture payload,
copy bytes and generated shadow readback from total process/system memory.

`NativeSurface` and `NativeServiceConnection` register explicit client windows
and exchange bounded descriptions with the native adapter. The client does not
capture background windows. Native injection remains subject to the incident
safety hold and lifecycle restrictions documented in `libraries/dwmglass/README.md`.
Removing the screenshot backend does not authorize enabling or replacing that
adapter in a live compositor.

The private application's settings no longer expose liquid material choices or
optical parameters. Persisted retired selections resolve to a supported material;
there is no automatic switch from a removed capture backend to DWM injection.

Build using the existing parent CMake project or install the `CyberGlass::Liquid`
CMake target. Public headers and corresponding MIT notices are installed together.
