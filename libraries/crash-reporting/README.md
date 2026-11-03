# Windows crash reporting

This MIT-licensed adapter uses Google's Breakpad Windows client, statically
linked with the MSVC `/MT` runtime. Install vcpkg `breakpad:x64-windows-static-release`
(the tested port is `2024-02-16#1`). The upstream BSD license is in
`Breakpad.LICENSE`; distribute it with binaries using this adapter. Breakpad's
upstream source is https://github.com/google/breakpad.

Add this directory with `add_subdirectory`, with the static vcpkg prefix in
`CMAKE_PREFIX_PATH`. Cyber application executable targets are automatically
instrumented by the deferred CMake integration, along with the SDK CLI and preview targets. The bootstrap translation unit
is linked directly into each executable so static-library dead stripping cannot
remove registration. Other executable hosts can compile `Bootstrap.cpp` into
their target and link `cyber_crash_reporting` privately. Do not register an
exception handler in an OS compositor or another application's process.

The handler initializes before `main`/Qt, without creating a GUI or input hook.
For an executable in `<suite>/build/dist`, dumps go to
`<suite>/build/crashes/<lowercase executable stem>/<GUID>.dmp`. Nested SDK tools
under `dist/sdk/bin` resolve the same crash root outside the distribution. A caller creating
`CrashReporter` explicitly can provide another root. Each executable name, across
all its PIDs, retains the newest five owned GUID dumps by modification time.
Startup and dump completion prune old files under a bounded cross-process
file lease. Only owned GUID `.dmp` files are considered; unrelated files and
other executables' directories are preserved. There is no background scanning.

Windows unhandled exceptions, CRT invalid parameters/pure virtual calls,
`std::terminate` and `abort` are covered. Terminate/abort use exception code
`0xe043cd01`; pure virtual calls use `0xe043cd02`; invalid parameters retain
`0xc000000d`. `snapshot()` writes diagnostic thread/module/context information
without requiring a crash. Standard `MiniDumpNormal` retains contexts, stacks
and current modules. Optional indirect-memory, unloaded-module and thread-info
streams are disabled: enriched dumps intermittently returned Windows error
`0x800706f8` during short fault verification. Diagnostic snapshots retry transient
invalid-user-buffer/partial-copy failures at most twice, 10ms apart. Failed
partial dumps are removed; `last-error.txt` records the latest failure reason.
The reporter suppresses OS crash dialogs while installed. Symbols stay in the normal build directories, outside the
release package; use the matching executable's PDB when diagnosing a dump.

`ProcessWatchdog` is a separately scheduled, initially disarmed deadline. An
armed lease must be renewed by the monitored operation. On expiry it requests
a diagnostic dump and terminates its **own** process with `ERROR_TIMEOUT`.
Even if dumping stalls it terminates after an additional two seconds. Disarmed
watchdogs block on a condition variable, with no polling. Never arm a lease
over intentional idle waits; call `disarm()` when protected work ends. The owner
must outlive all callers. Dump-time termination is intentional and does not
attempt ordinary shutdown through an unresponsive event loop.

This is Breakpad's **in-process** client with its dedicated handler thread,
not a crash-server service. OS/kernel/DWM hangs, forced `TerminateProcess`,
fail-fast exceptions and arbitrary severe memory corruption are not guaranteed
to produce a dump. A dump failure must never prevent watchdog termination.
Permission errors or external locks can prevent writing/pruning; existing dumps
are preserved and pruning retries on subsequent initialization/writes. No dumps
or diagnostics are uploaded anywhere.

For console-only short verification, build `cd_crash_probe_a`,
`cd_crash_probe_b`, `cd_crash_probe_nested`, `cd_crash_probe_baseline`, then run
`python tests/verify.py <build>/Testing/crash`. The probes create no GUI windows
and inject no input. Dumps and `report.json` remain under that fixed test tree.
