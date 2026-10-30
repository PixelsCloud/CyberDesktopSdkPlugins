CyberDesktop Productivity 1.0 — Windows x64 /MT, C ABI 1

14 fixed rectangular layouts: timer, tasks, countdown, network, disk, notes, RSS.
Each instance has independent bilingual settings and external selectable fonts.
Install the self-contained .cdpkg through CyberDesktop's plugin settings.

Mechanical network, storage and focus timers are separate plugins supplied by
plugins/mechanical-instruments, with their own IDs, modules, previews and packages.
Search for the Mechanical tag in the catalog to add them. These original plugins
retain their original style and do not offer a runtime skin selector.

Timer: click Start/Pause or Reset. Focus completion stays visible; automatic
break is optional. Running timers use a persisted wall-clock deadline, so they
continue while the desktop is hidden or restarted. No full-screen redraw.
Tasks: edit one task per line (max 20). Click to check, arrows/wheel to browse.
Matching task text retains completion, including duplicates in original order.
Countdown: strict YYYY-MM-DD. Annual Feb 29 events use the next leap-year date.
Network: existing host system sampler, bytes/second, 60 samples of history.
Disk: local drive root only (C:\). Background reads every 30s by default;
shows quota-available free space and total drive usage separately.
Notes: multiline text, click to copy all or individual lines. No clipboard spying.
RSS: RSS 2.0, RSS 1.0/RDF and Atom; default Qt Blog; custom HTTP(S) feed URL.
Click headline opens the original article. Arrows/wheel browse. Background
host content.feeds service; up to 4 MiB per decoded response, bounded redirects/timeouts.
The host parses RSS/Atom without DTDs or external entities. Failed refresh retains headlines
in memory and marks them stale. Feed content is not bundled or redistributed.

Icons: Lucide, ISC/MIT. URL validation: libcurl. JSON: nlohmann/json.
HTTP and XML/HTML parsing run in the host, not in the plugin DLL.
Dependency notices and original SVG icons are included in each package.

主画布、实际 DPI、透明抗锯齿、预乘读回和 PNG 导出统一使用 CyberCanvas；计时器由 SDK Timer 事务性管理，专用排版、视图、后台数据/图片与实例状态保持原有行为。
