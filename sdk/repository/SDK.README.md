# CyberDesktop SDK source

This SDK-only export retains `sdk/`, its public shared libraries and declared
resources. Private application code, user data and plugin implementations are not
included. Code uses Windows x64 MSVC with static `/MT` dependencies.

From this directory, using the existing vcpkg static dependency prefix:

```powershell
./sdk/scripts/build-sdk.ps1 -InstalledDirectory C:/path/to/vcpkg/installed/x64-windows-static-release `
    -BuildDirectory build/sdk -InstallDirectory build/sdk-kit -Jobs 16
```

The build script limits compiler CPU affinity and concurrent jobs. Reuse these
directories for subsequent builds. The root `CMakeLists.txt` is also an SDK-only
entry; it does not require a `plugins/` directory.

See the [SDK guide](sdk/README.md), [quick start](sdk/docs/QUICKSTART.zh-CN.md),
[preview profiles](sdk/docs/PREVIEW_SERVICES.md), and [release procedure](sdk/docs/RELEASING.md).
The basic preview has no Qt requirement. Full character/Live2D preview uses the
explicit Qt/Cubism profile and separately acquired dependencies.

SDK code is MIT licensed. Fonts, models and other third-party resources retain
their [original notices](sdk/docs/THIRD_PARTY.md); project licensing does not
replace their terms. Official plugin sources belong to the full SDK/plugins
candidate, rather than this SDK-only export.
