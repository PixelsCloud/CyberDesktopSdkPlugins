# System Info 1.0.0

四款正式插件，列×行占用为 1×1、2×1、4×1、2×2。使用 24 段刻度、14px 数字 / 8px 标签、48px 仪表，固定使用原有样式。机械系统监控由 `plugins/mechanical-instruments` 作为独立插件提供。

字体从宿主 `assets/fonts/SiYuanZhenHei-Heavy/GenShinGothic-Heavy-2.ttf` 加载，不编入 DLL 或插件包。主程序、Clock 和 System Info 构建脚本均自动复制字体到可执行文件旁的 `assets/fonts`。英文标签使用 RAM、CPU T；中文使用“内存”“温度”。CPU 温度缺失时仍显示 0°C。

## 构建

```powershell
./scripts/build-mechanical-theme.ps1 -Jobs 10
```

专项入口复用 `build/relwithdebinfo`，编译不超过 10 个逻辑核心和 10 个作业。正式发布沿用整套构建流程，运行入口为 `build/dist`。插件自身 /MT，无 Qt 或动态 VC 运行库依赖。

## 数据

通过公开 `subscribe_system` 服务订阅 CPU、内存和 GPU，占用以有效采样为准。可选 `CdSystemInfoEx` 的原始 56-byte 前缀不变，80-byte 扩展提供有效字段和 CPU 温度。缺 CPU 温度显示 0°C 占位；其他未知 / 过期指标显示 —。GPU 温度不能映射为 CPU 温度。

普通刷新默认 1000ms，取决于采集器实际上报频率。失去上报后最多 `max(5 秒, 刷新间隔 × 3)` 转为无数据。暂停时取消系统和定时订阅并冻结画面；恢复后等待新采样。销毁取消所有订阅。未变化时复用预乘 RGBA 缓存。

每实例设置：刷新间隔、跟随主题 / 石墨 / 浅色，1×1 另有主指标选择。配置由宿主标准设置页和实例状态封套持久化。

在控件列表搜索“机械”可找到独立的机械系统监控。原插件与机械插件的 ID、预览、模块和安装包分开，没有外观切换设置。

## 验证

```powershell
./scripts/verify-system-info-plugins.ps1
```

专项检查涉及四款插件、32 个尺寸 / DPR 内容样本、温度占位、旧 ABI 前缀、状态和订阅生命周期、真实宿主数据与设置、Release 包导入以及十张 GPU 预览。设计工具使用固定示例数值，发布插件只显示实际订阅数据。
