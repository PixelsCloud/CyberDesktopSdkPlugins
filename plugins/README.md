# CyberDesktop SDK and official plugins

The public entry supports all **33 active plugin families / 599 plugin IDs**. Select all,
a family, or individual IDs; the CMake dependency graph selects required shared code.
Inactive descriptions without an implementation fail explicitly when selected by ID.
Core collectors, credentials, business databases and Desktop composition remain private.

Use Windows x64 MSVC, CMake 3.26+, Ninja and static `/MT` vcpkg dependencies. All
plugin modules build without Qt, CPR, SQLite or a private application.
Enable the vcpkg manifest feature `official-plugins` when building all plugins;
it adds static libcurl and zlib for URL parsing in content views. The optional
full preview host has additional explicit Qt/Cubism dependencies described below.
Build with at most 16 jobs and restrict the build process affinity to those CPUs.

## Installed SDK

```powershell
cmake -S plugins -B build/public-plugins -G Ninja -DCMAKE_BUILD_TYPE=Release `
    '-DCMAKE_PREFIX_PATH=C:/path/to/sdk;C:/path/to/vcpkg/installed/x64-windows-static-release'
cmake --build build/public-plugins --target cd_public_plugin_packages --parallel 16
cmake --install build/public-plugins --prefix build/public-plugins/distribution --component PublicPlugins
```

The selected SDK supplies headers, libraries, the `cd-sdk` packaging tool and its
matching notices. `public-plugin-ids.txt` lists selected IDs; `public-selection.txt`
records family dependencies. Archives remain in their family package directories.

**Deliver the installed distribution as a unit.** Thin theme plugin DLLs share runtime
DLLs, fonts and atlases. Their `.cdpkg` archives alone do not contain those shared
files. The distribution contains `plugins/<id>/`, shared runtime DLLs at its root,
shared `assets/`, and `public-distribution.json` declaring each plugin's dependencies.
It contains no build cache, object files or PDBs. Shared files are installed once;
do not copy hundreds of duplicate atlases into individual packages. Existing Desktop
installations consume the matching official runtimes through the unified installer.

## Select a family or plugin ID

```powershell
cmake -S plugins -B build/public-plugins -DCD_PLUGIN_FAMILIES=character-chat -DCD_PLUGIN_IDS=
cmake --build build/public-plugins --target cd_public_plugin_packages --parallel 16
# Alternatively:
cmake -S plugins -B build/public-plugins -DCD_PLUGIN_FAMILIES=ALL `
    -DCD_PLUGIN_IDS=com.cyberdesktop.character-chat
```

Character chat selects the public Live2D presentation code and sample assets, not
private network providers or chat storage. Semicolon-separated lists select several
families or IDs. `ALL` selects enabled implementations only. Unknown names and IDs,
and described IDs without active implementations, are configuration errors.

## Source development and preview profiles

The public repository root builds SDK + plugins together. From its checkout use
`cmake -S . ...`, or select the plugin entry with
`cmake -S plugins ... -DCD_PLUGINS_SDK_MODE=SOURCE`.
`CD_PLUGINS_SDK_SOURCE` selects another SDK source root. `CD_PLUGINS_SDK_ONLY=ON`
builds only the SDK; set it to `OFF` for selected plugins. Keep the public source
layout declared in `sdk/public-source.json`; do not replace includes with `../`.

The basic SDK preview has deterministic data services, native settings/input,
Lucide icons, CPU render output and bounded image/memory diagnostics. Optional
character/model services return unsupported in this profile. For all plugins use:

```powershell
./sdk/scripts/build-sdk.ps1 -InstalledDirectory C:/path/to/vcpkg/installed/x64-windows-static-release `
    -BuildDirectory build/sdk -InstallDirectory build/sdk-kit -Jobs 16 -FullPreview `
    -QtDirectory C:/path/to/Qt/msvc2022_64 -CubismRoot C:/path/to/CubismSdkForNative-5-r.5
```

Full preview builds its own public image and model workers. It renders actual
Live2D models and bounded image layers, and supplies offline multilingual streaming
chat samples. It does not read private application databases, call a remote AI
provider or launch private EXEs. CPU model readback is explicit preview behavior;
Desktop keeps its GPU composition path. See `sdk/services.json` and
`sdk/docs/PREVIEW_SERVICES.md` for capability and downgrade details.

Qt Core/Gui/Network and Cubism are separately obtained dependencies under their own
terms. Public source does not contain their SDK installations. Binary kits retain
notices from the selected distributions. Models, fonts, Lucide and other third-party
assets retain original notices; project MIT licensing does not replace them.
First-party plugin artwork is included with its plugin. `design/` is always excluded.

## Validation and core integration

The private repository's `scripts/verify-sdk-independent.ps1 -Jobs 16` verifies
isolated builds, each SDK header alone, installed/source modes, all selected IDs,
relocated distributions, lifecycle/state/pause cleanup, service contracts and actual
model/image preview. Fixed output is `build/relwithdebinfo/Testing/sdk-independent`.
The report lists expected/generated/tested/packaged IDs; code changes require their
relevant checks even after an earlier report passed. Hosted CI and another physical
machine are separate, unperformed checks unless explicitly reported.

Public unit tests remain beside their modules. Desktop/CyberGlass integration tests
and private resource/network/storage fixtures live under core `tests/` and `cmake/`.
The private core consumes this repository through a pinned
`external/CyberDesktopSdkPlugins` submodule. This public repository has its own
history and never includes private core sources or their Git history.

The public CI runs SDK release tests and builds/installs all 599 plugins. Run the
same plugin verification locally with `./sdk/scripts/verify-public-plugins.ps1
-InstalledDirectory C:/path/to/vcpkg/installed/x64-windows-static-release`.
Hosted results are available in this repository's Actions page.

## Licenses

Code and first-party resources are MIT except where a directory has its own notice.
OFL-licensed fonts retain SIL OFL 1.1; ZQK has a separate notice. Live2D original characters retain Live2D terms;
Whale Girl assets remain CC BY-NC-SA 4.0 (non-commercial, attribution, share-alike).
Read [the complete notices](sdk/docs/THIRD_PARTY.md) before redistributing resources.
The root MIT license does not relicense those assets. ZQK remains unchanged at the project owner's request; its separate permission is handled by the project owner.

Family `build-*.ps1` and `deploy-*.ps1` entry points now share the independent
builder. Pass `-InstalledDirectory` for the static vcpkg prefix and optionally
`-SdkRoot` for an installed SDK. They stage a plugin distribution in the selected
fixed build directory; they do not access a private Desktop checkout or install
into a running application. The public root supports the same selection via CMake.
