# 公共数据服务快速使用（SDK 1.1.0）

本体负责采集，插件只订阅数据。键鼠、系统、媒体、音频、位置、天气和空气质量都是本体共享能力；开发者不需要自己监听设备或实现 HTTP。SDK 开源内容只有接口、工具、模拟数据和示例，不含闭源采集实现或真实凭据。

完整字段、单位、状态、线程和版本合约见 [Data services](DATA_SERVICES.md)。C 使用 `sdk/services/DataServices.h`；C++20 使用 `sdk/cpp/Services.h`。先检查宿主 ABI 1 和 88 字节服务扩展，再查询版本 1 的服务表。必要服务在 protobuf 中声明 `required_services` 和对应 `capabilities`；可选服务则自行处理 `CD_ERROR_UNSUPPORTED`。

| 数据 | 服务 ID | 能力 |
| --- | --- | --- |
| 键鼠聚合计数 | `input.stats` | `INPUT_STATS` |
| CPU、内存、GPU、CPU 温度 | `system.telemetry` | `SYSTEM_INFO` |
| 播放信息与异步命令 | `media.playback` | `MEDIA_CONTROL` |
| 播放器会话列表 | `media.sessions` | `MEDIA_CONTROL` |
| 系统混音频谱 / PCM | `audio.spectrum` / `audio.pcm` | `AUDIO_FFT` / `AUDIO_PCM` |
| 自然白噪音播放与场景联动 | `audio.ambient` | `MEDIA_CONTROL` |
| 位置 | `location.current` | `LOCATION` |
| 当前天气、逐日与逐时预报 | `weather.forecast` | `WEATHER` |
| 站点 AQI、污染物分指数和预报 | `air.quality` | `AIR_QUALITY` |

C++ 的 `Services` 返回可移动的 RAII Watch。检查 `.result()`；回调得到已复制的快照，字符串、数组和封面可安全保存。暂停时 `.reset()`，恢复时重新订阅，析构自动注销。回调内注销自己也安全。C 回调中的指针只在当次回调有效，需要自己复制。注册、刷新、媒体命令和注销都在 UI 线程；网络和采集由本体后台处理。可见内容变化时调用 `request_redraw`，只更新自己的砖块。

三个完整示例位于 `examples/input-stats`、`examples/data-dashboard`、`examples/media-spectrum`。分别展示键鼠计数、组合系统 / 天气 / AQI，以及媒体控制配合系统频谱。都提供每实例设置、字体、简体中文 / 英文内容、可调默认 5 px 圆角、暂停恢复和销毁。SDK 安装后可直接编译 `share/cyberdesktop-sdk/examples`，不需要本体或 Qt。

网络快照要分别检查状态与有效位：`READY` 正常，`STALE` 保留刷新失败前的数据，`LOADING` 仍在加载，`ERROR` 没有有效数据。不能把缺测当作真实 0。观测 / 获取时间用 UTC Unix 毫秒，未知时间为 0；系统、媒体、音频采样时间用 Windows 单调毫秒。天气温度为摄氏度、风速为 m/s、湿度与降水概率为百分比。AQI 污染物值是分指数，不是浓度，并且各项有独立有效位。

位置 `DEFAULT` 跟随本体设置；`AUTO` 显式按 IP 自动定位；也可选城市、国家过滤、经纬度。仅 AQI 支持指定站点。同资源共享请求，语言和显示单位不重复发 HTTP。刷新间隔限制 5 分钟至 3 小时，手动刷新有 30 秒冷却；`CD_ERROR_BUSY` 不应立即循环重试。逐日预报支持最多 14 天，逐时最多 168 小时，实际数组可能更短。自动定位失败不会回退北京，代理可能影响 IP 位置。

在设置→数据服务配置私有凭据，插件只保存其不透明 ID，默认 `default`。Token 不进入 SDK、插件包、回调或日志。缺凭据、认证、限流、无站点等错误可区分。继续展示数据源与监测机构，并遵守 Open-Meteo、WAQI 的使用条款。

媒体命令先检查能力位。成功排队会返回请求编号，随后快照携带该编号和等待 / 完成结果。播放器切换后旧命令不会作用于新会话。音频是系统混音，不能当作所选播放器的独占声音；每设备共享捕获和 FFT，最大派发频率 30 fps，积压只保留最新结果。音频状态接口可区分静音与设备不可用，并报告丢弃帧数。

CLI / 预览全部使用模拟数据，不联网、不需要真实 Token、不控制实际播放器或监听键鼠。真实本体的专项集成验收另行进行。

2026-10-15 宿主增加 `sdk/services/AmbientAudio.h` 的 `CdAmbientApi` v1。空分组键创建实例私有声音，`rain` / `fire` / `stars` 关联共享场景；六轨顺序为雨、海浪、溪流、鸟鸣、壁炉、风，最多四轨同时混音。主音量与单轨音量使用 0—1，定时 0—240 分钟（0 不限时），默认停止、25% 主音量。播放、暂停、停止需显式命令，快照报告错误与 UTC 秒截止时间；隐藏或暂停画面不关闭声音，定时保持计数。实例销毁时关闭音频令牌，最后一个场景使用者销毁后停止播放，重启程序不会自动恢复声音。SDK 预览为静音模拟；实际播放由本体 miniaudio 后台完成，素材与授权在宿主 `assets/ambient`。

## 天气展示辅助函数

可选 C++ 辅助接口位于 `sdk/WeatherPresentation.h`，链接 `CyberDesktopSDK::Core`
或 SDK 汇总目标即可使用。`cdsdk::weather::iconName(code, day)` 把 WMO 天气代码映射为
Meteocons 资源基本名；`condition(code, english)` 返回既有中文或英文描述。未知代码返回
`not-available` 及未知天气描述。函数不联网、不加载图片、不内置图标资源；资源授权和其他语言
本地化由调用方处理。HTTP 客户端、JSON 解析、凭据及网络测试属于私有宿主，插件通过
`weather.forecast` / `air.quality` 订阅数据并维护展示状态。

## 宿主回调表生命周期

`cdsdk::Services` 复制经过校验的宿主回调，不再长期保存调用者的 API 表地址。
`Subscription` 保存取消回调，同一个令牌最多取消一次，包括取消过程中重入的情况。
宿主上下文仍为借用；所有关联订阅销毁前，上下文与回调代码必须保持有效。复制回调不延长宿主生命周期。
