# SDK 1.4 / CyberCanvas 1.2 delivery

The native boundary remains C ABI 1 and protobuf package format 1. Use matching C++ headers and libraries from one kit. `release.json` defines versions; `services.json` describes the 32 queryable services and their preview profiles.

## Source and binary layouts

`sdk/public-source.json` is the source inventory for both the public repository candidate and SDK-only exports. `scripts/export-public-source.ps1` preserves `sdk/`, `plugins/` and required public `libraries/` paths. Pass `-SdkOnly` for an SDK-only tree. Exported source contains no private application history, `src/`, credentials, user databases or `design/` material. Existing generated exports are updated in place; unrelated directories are rejected.

The private compatibility entry `scripts/export-plugin-sdk.ps1` calls the same exporter. The old `release-files.txt` list is retired. Font, Lucide, calendar and model notices remain under their original licenses; see [THIRD_PARTY.md](THIRD_PARTY.md). The public candidate is a local staging artifact, not an announcement of publication.

## Reproduce a local SDK release

1. Use the pinned vcpkg baseline with Windows x64 static `/MT`. Run `sdk/scripts/release-sdk.ps1 -InstalledDirectory PATH_TO_STATIC_VCPKG_PREFIX -Jobs 16` from the public source root. The tool reuses one fixed workspace, limits compiler children by CPU affinity, and retains incremental build caches.
2. The release verifies installed SDK headers, native contracts, all seven generated starters and seven installed examples. Canvas contract suites and structural benchmarks run against the checkout and exported source. Static CRT import checks apply to this **basic preview** kit. Full preview is a separate profile with declared Qt/Cubism runtime dependencies.
3. Inspect `artifacts/release-report.json`, starter reports and benchmark JSON. This explicit release verification produces inventories; normal Desktop compile/deploy does not run it. No ZIP is generated unless `-Archive` is supplied. Explicit archives use fixed names and are rebuilt and tested from their extracted source.
4. To build the full preview, use `sdk/scripts/build-sdk.ps1 -FullPreview -QtDirectory QT_PREFIX -CubismRoot CUBISM_ROOT`. It requires separately obtained Qt and Cubism, original notices from that Qt installation, and original Cubism notices. It installs the isolated worker, shaders and required Qt DLLs. No external SDK or application binary is copied into the public source candidate.

The private integration acceptance entry is `scripts/verify-sdk-independent.ps1`. It uses isolated public sources, no PCH, a relocated SDK, all 33 plugin families and the exact enabled-ID manifest. Each of the 599 currently enabled packages is validated and exercised before and after distribution relocation; it also checks single-family/single-ID selection, SDK-only/source mode, source dependency boundaries, header compilation and unchanged incremental builds. Lifecycle checks do not substitute for mixed-theme image/performance or actual Desktop integration tests.

## Plugin distribution

Build `cd_public_plugin_packages`, then install CMake component `PublicPlugins`. `public-distribution.json` lists the selected plugin directories, transitive runtime DLLs and shared assets. Ship that complete distribution. A thin `.cdpkg` does not embed shared theme DLLs and is not a standalone replacement for the distribution.

Before replacing a live DLL, unload its instances. Installation keeps the existing validation, transaction and rollback behavior; preserve the matching settings/state for the version being restored. SDK C++ consumers rebuild against one kit; C ABI additions remain optional and version-queried.

## Deferred remote publication

The planned remote is `https://github.com/PixelsCloud/CyberDesktopSdkPlugins.git`, with new public history. The private core retains its own origin and will pin a public commit through `external/CyberDesktopSdkPlugins`. The core already accepts a configurable `CD_PUBLIC_SOURCE_ROOT` so this boundary can be tested before activating a submodule.

No export/build script adds a remote, pushes, tags, or publishes. Remote publication and actual submodule activation remain deferred until explicitly requested. The workflow in the candidate can build a basic SDK release and create a draft for an authorized version tag. Hosted CI and another physical machine are not claimed by local acceptance.
