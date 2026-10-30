CyberDesktop Weather 1.0.1 — Windows x64 /MT, C ABI 1

天气 2×1 / 4×1 / 4×2（列×行）。当前温度、天气状态和预报；大尺寸额外显示体感、湿度和风速。
“机械天气”为独立插件，可在控件列表搜索“机械”后添加。
本插件固定使用原有样式，没有皮肤切换设置；与机械天气各自保存实例及设置。
天气数据通过宿主 weather.forecast 服务订阅获取，插件不直接联网，也不读取凭据或维护网络缓存。
默认位置、提供商、请求合并、缓存和重试由宿主管理；插件保留手动城市/坐标、温标、语言与展示设置。
缺失数据显示 --；失败、过期缓存与定位错误按服务回调显示，不伪造默认城市或读数。
只在可见内容或图片资源变化时请求局部刷新；暂停和销毁释放订阅，不等待网络请求。

图标使用静态版本，不做持续动画以避免常驻 GPU 更新。

Weather data by Open-Meteo.com — https://open-meteo.com/ — CC BY 4.0
Location data based on GeoNames — https://www.geonames.org/ (via Open-Meteo geocoding).
https://creativecommons.org/licenses/by/4.0/
Current conditions use weather-model estimates, not a nearby station's instantaneous observations.
The public free endpoint is for NON-COMMERCIAL use. Commercial deployments require an appropriate
Open-Meteo subscription/service integration; see https://open-meteo.com/en/pricing and /en/terms.
Displayed values are rounded; Fahrenheit and mph are locally converted from Celsius and m/s.
IP geolocation by ipwho.is — https://ipwhois.io/documentation — no API key.
Automatic mode discloses your network IP to this service, and sends the approximate coordinates
to Open-Meteo. IP addresses are not written to the cache or displayed. GPS is not used.
Proxies / VPNs and mobile networks may locate an exit node instead of your physical location.
Turn off Automatic location to choose a city or coordinates; only manual geocoding / weather is
requested. Requests begin after saved settings are restored, not in the plugin constructor.
The host owns provider configuration and cache storage. The plugin consumes typed SDK callbacks.
Offline cached data and missing values are distinguished by the host service status.

Icons: Meteocons @meteocons/svg-static 0.1.0, fill style, by Bas Milius (MIT).
18 original SVGs retained with pinned package integrity and transparent PNG derivatives in assets/.
https://github.com/basmilius/meteocons
HTTP client: CPR 1.14.2 (MIT) + libcurl 8.20.0 (curl license), statically linked from vcpkg.
Compression: zlib 1.3.2 (zlib license). TLS verification stays enabled; connect/total timeouts,
progress cancellation and a 1 MiB decoded-body limit run on the worker. No native Windows HTTP API.
Forecast, geocoding and IP lookup share a 20s connection timeout and 60s total
timeout per attempt. A connection below 8 B/s is tolerated for 45s. Transient
transport failures and HTTP 408 / 425 / 5xx retry up to 20 times (21 attempts).
Retry delays are 2s, 4s, 8s, then capped at 10s per wait.
Retry waits can be cancelled immediately. Authentication, invalid inputs,
certificate errors and rate limits are not immediately retried. Persistent
failures keep cached data and use the existing 1 / 2 / 5 / 15 minute backoff.
JSON parser: nlohmann/json 3.12.0 (MIT), supplied by vcpkg; no runtime DLL dependency.
Font files are shared external application assets, never embedded into plugin DLL or .cdpkg.
All other required resources and licenses are included in each .cdpkg.

Build standalone with CyberDesktopSDK_DIR and vcpkg static-release CMAKE_PREFIX_PATH using MSVC /MT.
Targets cd_weather_1x2_package, cd_weather_1x4_package, cd_weather_2x4_package produce single-file .cdpkg.
Optional tools: cd_weather_tests <source-root> <output> checks presentation and image lifecycle.
Provider/network tests belong to the private host repository.
cd_weather_design <assets-folder> <output-folder> exports real renderer previews.
cd_weather_rasterize <assets/svg> <assets/icons> rebuilds PNGs (build-time QtSvg only).

绘制底层已迁移至公共 CyberCanvas：复用 COM/Direct2D 目标、实际 DPI 帧、透明抗锯齿、预乘读回与 PNG 导出；专用字体和布局保持原有实现。
图标由共享有界工作线程解码并缓存上传，只在资源变化时局部刷新，隐藏时暂停资源轮询；离线工具使用 renderReference 等待图片，不在生产绘制路径阻塞。
