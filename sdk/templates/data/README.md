# Native telemetry starter

Receives CPU and memory through the host system.telemetry v1 service. It owns its subscription, releases it while hidden and requests a local frame only when displayed values change. No private collector or network client is included.

Use an installed SDK 1.3 kit and an MSVC x64 developer terminal:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/CyberDesktopSDK
cmake --build build --parallel 10
C:/CyberDesktopSDK/bin/cd-sdk.exe test build/plugins/com.example.data
C:/CyberDesktopSDK/bin/cd-sdk-preview.exe build/plugins/com.example.data --size 320x200 --dpr 2
C:/CyberDesktopSDK/bin/cd-sdk.exe pack build/plugins/com.example.data starter.cdpkg
```

Edit the ID in both Plugin.cpp and plugin.textproto, or use the kit's scripts/new-plugin.ps1 to create a named project. Keep ID/version consistent. Resources belong in the staged package. The compiler needs neither Qt, protobuf nor the desktop sources.

Canvas handles the primary frame, DPI, antialiasing and pixel output. PluginAdapter keeps C callbacks and exceptions at the ABI boundary. All state and subscriptions are per-instance; workers never draw. The three system-font choices need no bundled font files. For richer interfaces, use the installed Canvas Workbench example and SDK Scene/Images/Icons APIs.

This template and its source are MIT licensed; retain LICENSE and the staged dependency notices when distributing binaries.
