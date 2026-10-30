CyberDesktop Air Quality 1.0.0 — Windows x64 /MT, C ABI 1

空气质量 1×2 / 1×4 / 2×4，支持长方形格子、简体中文 / English、独立字体与深浅配色。
默认自动 IP 定位，查询附近监测站，不回退到北京。可搜索城市 / 站点、指定坐标或站点 ID。
显示 US AQI；分项均为 WAQI iaqi 的污染物指数，绝非 μg/m³ 等原始浓度。
2×4 的预报是 PM2.5 分项指数的日均预报，不是未来综合 AQI，也不是历史实测曲线。
缺失分项显示 --，没有预报时显示 --，不伪造 0 或估算数据。
双击手动刷新，默认 15 分钟后台刷新；原站观测可能按小时更新。
只在内容变化时请求插件局部刷新，不维持动画或整屏重绘。
数据通过宿主 air.quality 服务订阅。网络、JSON 解析、凭据读取、共享缓存和重试都属于宿主；插件不构建这些实现。
暂停和销毁释放服务订阅，不在 UI 线程等待网络。缺失数据、失败和有效缓存按回调状态区分。

Credentials are managed by the host. The existing credentials_file setting is passed as a host
credential profile identifier, not read as a local file by the plugin. Never bundle tokens in a plugin.
The same resource can be shared by host subscriptions; only changed visible content requests redraw.
Automatic IP location is approximate and may reflect a proxy / VPN exit rather than physical location.

Data: World Air Quality Index Project (WAQI), https://waqi.info/, and originating monitoring agencies.
All originating agency names and URLs from each API response are retained in memory; the first
originating agency is displayed in the card footer. The underlying station's index is not a city average.
API docs / terms: https://aqicn.org/api/
The free API's terms restrict paid applications and services, cached-data redistribution, and public
for-profit usage without explicit agreement; non-profit public usage requires prior notification.
This integration is a local development prototype. A working token is not a publication or commercial licence.
Station readings can be revised by providers and are not a medical assessment.
The supplied map-tile API is an overlay for a map, not the station-report JSON API used here;
no map tiles or external map imagery are packaged.
IP location: https://ipwhois.io/documentation

Free imagery: Lucide static icons, pinned npm package provenance, ISC / MIT (Feather-derived).
Original SVGs and colored PNG derivatives are included with licenses, without runtime CDN requests.
Native card depth, state scale and metric layout are drawn by the plugin renderer.
JSON: nlohmann/json 3.12.0. Every dependency license is included in licenses/.
External shared font defaults to Gen Shin Gothic Heavy; font data is not embedded in the DLL or .cdpkg.

Build: use MSVC x64 /MT, CyberDesktopSDK and vcpkg static-release CMAKE_PREFIX_PATH.
Targets cd_air_1x2_package, cd_air_1x4_package, cd_air_2x4_package produce single .cdpkg files.
cd_air_tests <source-root> <output> checks presentation, station validation and image lifecycle.
Provider/network tests belong to the private host repository.
Design previews are exported by cd_air_design; images use fictional data clearly documented in design/air-quality.

绘制底层已迁移至公共 CyberCanvas：复用 COM/Direct2D 目标、实际 DPI 帧、透明抗锯齿、预乘读回与 PNG 导出；专用字体和布局保持原有实现。
图标由共享有界工作线程解码并缓存上传，只在资源变化时局部刷新，隐藏时暂停资源轮询；离线工具使用 renderReference 等待图片，不在生产绘制路径阻塞。
