# CyberCanvas 1.2

`CyberCanvas/Mechanical.h` 提供可复用的机械仪表部件：银钢机框、铭牌、黄铜紧固件、
带刻度的圆形表头、指针、凹入式读数窗、金属按钮、状态灯和进度滑轨。
所有部件使用调用者的 Canvas 和实际 DPI，不依赖 Qt、插件 SDK 或大尺寸材质图片；
字体和可选文字转换函数由调用者提供。表头接收独立的指针比例与准确数字，因此缓动不会延迟读数。
`CyberCanvas/InstrumentMotion.h` 的 `mechanical::NeedleBank<N>` 复用已有 Tweeny 缓动，
用调用者提供的单调时间在 600ms 内推进；支持运动中改目标、初次采样直接到位和减少动态。
不持有宿主定时器，调用者仅在 `active()` 为真时订阅动画，隐藏和销毁时释放订阅。

可选自然运动组件 `CyberCanvas::Noise` 使用 FastNoiseLite 1.1.1（MIT）。启用
`CYBERCANVAS_BUILD_NOISE=ON`，链接该静态目标并包含 `CyberCanvas/Noise.h`，即可使用二维/三维噪声、
连续时间采样和粒子风场。依赖由 SDK overlay port 管理，安装后的消费者不需要第三方头文件。
用法和参数边界见 [SDK 绘制扩展](../../sdk/docs/RENDERING_EXTENSIONS.md)，
本轮范围和后续能力见 [后续规划](../../docs/RENDERING_ENGINE_FOLLOW_UP.md)。

## 可选扩展（1.2，开发验证版）

新增 `Motion`、`Physics2D` / `PhysicsScene`、`Effects`、`Text`、`AnimatedImages`、`Vector`、
`Particles`、`PhysicsParticles`、`Fluid`、`Charts`、`Noise` 组件。`Layout` 的 Scene 支持子树变换、命中/裁剪同步和显式图层缓存。
引擎仍独立于 Qt 和 CyberDesktop；宿主负责时钟、输入、主题、实际 DPI 和局部重绘调度。

物理使用 Box2D 3.1.1；SVG/Lottie 使用裁剪后的 ThorVG 1.0.6；GIF/PNG 解码使用 WIC。
Godot 仅供架构参考，不是构建或运行依赖。`CYBERCANVAS_BUILD_PHYSICS`、
`CYBERCANVAS_BUILD_EFFECTS`、`CYBERCANVAS_BUILD_VECTOR`、`CYBERCANVAS_BUILD_NOISE` 默认关闭，SDK 完整构建显式开启。
消费安装包时通过 `CyberCanvas::PhysicsScene` 等目标链接所需组件，无需第三方头文件。

`PhysicsParticles` 沿用 Box2D，支持碰撞反弹、摩擦、旋转图集和方形碎屑，默认 64、最多 256 个刚体粒子。
`Fluid` 是单独开启的二维 Salva / Parry 后端，支持液体压力、黏性、表面张力、采样实体边界与 Box2D 双向冲量。
源码构建开启 `CYBERCANVAS_BUILD_FLUID=ON`，需要 Rust MSVC 工具链；消费已安装的 `/MT` 静态库无需 Rust。
流体默认 1024、最多 4096 个粒子，提供后台 `FluidWorker` 与不可变快照；暂停、隐藏、减少动态均停止推进。
两类效果共享实际 DPI 的 Canvas 绘制，但没有 GPU 粒子计算、折射水面或跨插件世界。
参数、线程及所有权约定见 [SDK 绘制扩展](../../sdk/docs/RENDERING_EXTENSIONS.md)。

`Physics2D::World::revolute` 可选传入 `RevoluteOptions`，设置相对创建姿态的角度限位（弧度）、
电机角速度（弧度/秒）和最大转矩（N·m）。`revoluteMotor(joint, speed, maximumTorque)` 可在运行中反向或
以零速度保持负载；参数先完整校验再更新。原四参数重载继续保留，无需修改旧调用。

公共头文件说明所有权和单位；SDK 的 `docs/RENDERING_EXTENSIONS.md` 给出完整契约、限制、
使用和分发说明。SDK 示例 `physics-sandbox` 与 `rendering-gallery` 可独立构建、预览和打包。
新的 C++ 类型和缓存布局要求消费者重新编译；插件 C ABI 1 保持不变。


## 可见字形测量与缩放（1.1）

`measureTextInk(value, style, available)` 返回相对于文字布局原点的可见字形矩形，包含字形伸出部分。
`fitTextInk(value, style, area, references, shrinkMargin)` 按可见字形缩小、对齐单行文字，返回原生布局、
绘制原点和目标中的字形边界。普通文本排版继续使用 `measureText`；需要铰链穿过数字中线的翻页时钟
使用可见字形边界。参考文本组决定统一字号，避免 `11` 比 `88` 更大：

```cpp
cybercanvas::TextStyle style;
style.size = 64;
style.align = cybercanvas::TextAlign::Center;
std::array<std::wstring, 2> references{L"00", L"88"};
const auto fitted = canvas.fitTextInk(L"11", style, {10, 10, 60, 80}, references);
canvas.target->DrawTextLayout(
    D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)), fitted.layout.Get(), canvas.brush.Get());
```

接口复用 Canvas 的 384 项文字布局缓存，不创建另一份无界缓存。通过 `fontCollection` 切换字体集合
时同步失效；布局使用逻辑坐标，不依赖绘制目标或 DPI。拟合后的字号独立入缓存，不修改原字号布局。
返回的原生布局只读，应在同一 Canvas 的下一次文字请求前完成测量和绘制，不跨帧保留作独立快照。
`shrinkMargin` 默认为 0.995，只在缩放比例小于此值时缩小并预留同等比例的余量；范围为 `(0, 1]`。
文字不放大，参考组最多 128 项；目标尺寸必须大于零，非有限坐标和非法字号会报错。

翻页时钟已使用公共拟合与外部字体集合；锦鲤祝福使用公共粗体、字距和中文布局缓存，仍在插件中绘制
金色渐变、描边、阴影及缓动。电子时钟的七段轮廓由公共 `Path` 创建。

## 可读回像素的离屏表面（1.1）

`CyberCanvas/ReadableSurface.h` 提供 `ReadableSurface`，用于透视变形等确实需要 CPU 像素的绘制。
普通图层继续使用 `OffscreenSurface`。它复用已有 WIC/Direct2D 软件绘制方案，不要求 GPU 读回：

```cpp
cybercanvas::ReadableSurface face;
face.render(canvas.factory.Get(), canvas.nativeImageFactory(), {160, 240, 2}, [&](auto* target) {
  // Draw in logical coordinates; use face.clipped(area, callback) for a scoped clip.
  target->Clear(cybercanvas::color(0x202830FF));
});
auto pixels = face.image();                    // Immutable premultiplied BGRA ImageData.
auto bitmap = face.bitmap(canvas.target.Get()); // Read-only bitmap, preserving 192 DPI.
```

尺寸使用 `SurfaceSize` 的物理像素和 DPI 比率，范围与公共离屏表面一致。相同工厂、尺寸和 DPI 会复用
WIC 绘制目标；每次成功绘制发布一个独立 `ImageData`，旧快照不受后续渲染或 `reset()` 影响。
通过 `bitmap(destination)` 按需上传并缓存一个目标，保留正确 DPI，支持用逻辑源矩形裁剪数字半片。
不同 Canvas 可以从同一 CPU 快照分别上传。保留的 ComPtr 只用于其原目标，且先于原 Canvas 销毁。

所有绘制和原生上传在拥有者线程执行；CPU 快照可共享只读。回调不能手动 BeginDraw/EndDraw，
`clipped` 在异常时自动弹出，最多嵌套 32 层。回调或 EndDraw 失败会丢弃当前不完整结果；非法参数在
开始绘制前拒绝，保留原有效结果。一次读取最多 4096×4096 BGRA 像素；外部快照数量由调用者管理，
不计入 ImageStore 解码预算。

翻页时钟已接入面片生成、CPU 快照和原生位图上传，保留逆透视采样及数字对齐。LCD、方点和圆点
电子时钟使用公共几何遮罩；字体造型与发光仍由插件控制。

## 已解码图片与统一上传（1.1）

已有后台解码器或宿主封面数据时，使用 `ImageData::fromPremultipliedBgra(width, height, std::move(pixels))`
将紧密排列的预乘 BGRA8 像素转为不可变资源。函数不访问文件、不创建 COM 对象；移动 vector 不复制像素。
宽高范围 1–4096，字节数必须恰好为 `width * height * 4`。调用者负责预乘格式，交出缓冲区后不得再修改。
每次创建获得独立资源 ID，与 ImageStore 后台解码使用同一 ID 分配器；同一资源可共享给多个绘制位置。

```cpp
auto photo = cybercanvas::ImageData::fromPremultipliedBgra(width, height, std::move(bgra));
// In an active frame: fit, sprite cell, cell count, opacity, rectangular clipping.
canvas.image(*photo, area, {cybercanvas::ImageFit::Cover, 0, 1, .5});
```

`ImagePlacement::opacity` 默认 1，支持 0–1；直接应用于图片，不创建组透明度图层。
`clip` 默认 true，限制 cover 溢出；已有圆角/圆形几何遮罩时可设为 false，避免重复边缘裁剪。
Contain、Cover、Stretch 共用现有布局计算及线性插值；旋转等变换沿用当前 Canvas 状态。

所有已解码资源复用原有 Canvas 上传 LRU：最多 64 项且最多 64 MiB，主目标尺寸/DPI 重建时清空，
下一帧从 CPU 资源重新上传。相同资源的淡入、旋转或跨帧绘制复用一次上传，不按图片槽位重复创建位图。
`clearImageUploads()` 可主动释放上传；调用者持有的 CPU 像素不计入上传预算。
外部导入资源也不计入 ImageStore 的解码预算，CPU 生命周期由调用者管理。

相册和唱片封面已使用这个入口，插件不再维护两槽位的 ID2D1Bitmap 缓存。
相册扫描、EXIF 方向与 WIC 解码仍在原后台线程，封面代次检查及 RGBA→BGRA 转换仍在宿主数据接入层。

## 离屏表面与几何遮罩（1.1）

`Canvas::masked(path, callback)` 或 `masked(nativeGeometry, callback, bounds)` 使用抗锯齿几何遮罩。
遮罩作用于回调内的绘制，异常退出也会自动弹出；可与 `clipped`、`withOpacity` 嵌套。
原生几何必须来自当前 Canvas 的工厂；默认 bounds 为无限区域，显式 bounds 使用当前逻辑坐标。
每个目标最多缓存 32 层遮罩，嵌套超限会抛出异常，尺寸/DPI 重建时释放图层。
遮罩本身不恢复画笔或变换，需要隔离这些状态时在回调内使用 `saved`。

`CyberCanvas/Surface.h` 中的 `OffscreenSurface` 管理独立兼容目标、透明清屏和 BeginDraw/EndDraw。
`SurfaceSize{width, height, devicePixelRatio}` 的宽高是物理像素（1–4096），DPI 比率为 0.5–4。
逻辑尺寸为物理尺寸除以 DPI；图形启用逐图元抗锯齿，透明文字使用灰度抗锯齿。

```cpp
// Retain this member after the owning Canvas, so it is destroyed first.
cybercanvas::OffscreenSurface cache;
// Inside an active Canvas frame; repaint only when the content is dirty.
cache.render(canvas.target.Get(), {320, 160, 2}, [&](auto* target) {
  auto fill = canvas.linearGradient({0, 0}, {160, 80}, stops, target);
  cache.masked(shape.geometry(canvas.factory.Get()), [&] {
    target->FillRectangle(D2D1::RectF(0, 0, 160, 80), fill.Get());
  });
});
canvas.target->DrawBitmap(cache.bitmap());
```

目标、尺寸和 DPI 不变时复用离屏目标；每次 `render` 清空并重新绘制，未调用时保留上次成功位图。
`invalidate()` 仅标记内容失效，`reset()` 释放全部离屏资源；调用者负责主题、内容及宿主目标变更时的
语义失效，不能在主目标已变更时直接使用旧位图。渲染异常或 EndDraw 失败会丢弃不完整画面，下一次可重建。
所有接口在 Canvas 所在线程使用，离屏对象及借用的原生资源必须先于 Canvas 销毁。
回调只负责绘制，不手动 BeginDraw/EndDraw，也不结束主 Canvas 帧；主画布句柄不会被替换。

锦鲤的池塘/祝福缓存、鱼身花纹遮罩、唱片裁剪和相册圆角过渡已接入。相册保留直接双图混合，
不额外分配离屏纹理；锦鲤保留只刷新祝福时复用池塘位图的策略。

矩阵频谱的静态背景和灯珠纹理也由 `OffscreenSurface` 生成。临时表面结束后可用
`ComPtr<ID2D1Bitmap>(surface.bitmap())` 保留成品位图，再释放表面对象；借用的裸指针不能跨表面生命周期。
这样无需为每行灯珠长期保存完整绘制上下文。缓存位图仍属于原主目标，主目标重建后必须重新生成；
矩阵的缓存键同时检查目标身份、尺寸、DPI、密度、形状和外观设置。

## 渐变画笔与缓存（1.1）

`Canvas::linearGradient(start, end, stops, destination)` 和
`radialGradient(center, radii, stops, offset, destination)` 返回可用于任意原生几何绘制的共享画笔。
这是 Windows 原生扩展接口，节点类型为 `D2D1_GRADIENT_STOP`，返回 `ComPtr`；默认目标为当前 Canvas。
显式传入兼容离屏目标时仍要求 Canvas 帧已开始。返回的画笔只读使用，不修改端点、透明度或变换：

```cpp
const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0x80C8A0FF)},
                                 {1, cybercanvas::color(0x204C7040)}};
auto fill = canvas.linearGradient({0, 0}, {100, 80}, stops);
canvas.target->FillRectangle(D2D1::RectF(0, 0, 100, 80), fill.Get());
```

节点使用非预乘浮点 RGBA（0..1），保留动态颜色的浮点精度；支持 2–32 个非递减位置节点，允许相同位置。
采用 Direct2D 的 gamma 2.2 和 clamp 模式，不自行实现颜色插值。半径必须为正，非法/非有限输入会抛出异常。

缓存按渲染目标隔离，完整几何与颜色参与画笔键；同时持有同色不同位置的画笔不会互相修改。
变化位置仍可复用节点集合，完全相同的请求可复用画笔。LRU 最多保留 4 个目标，每目标 32 个节点索引和
64 个画笔；画笔可能继续持有已从节点索引淘汰的集合，因此原生集合数量不能直接等同于索引数量。
缓存持有目标引用，避免 COM 指针地址复用；临时目标完成后可调用 `releaseGradientTarget(target)` 提前释放。
Canvas 尺寸/DPI 重建和销毁会清空缓存。外部仍持有的画笔由调用者负责释放。

`gradientCacheStats()` 提供当前目标、节点索引、画笔数量及累计创建/命中次数，便于专项检查；它不是
原生显存/内存字节统计。`gradientBox` 也复用此缓存。原始颜色持续变化或动画几何没有重复时仍会创建新画笔，
缓存上限保证不会无限增长。锦鲤、天气、空气质量、播放器同族频谱已接入此能力。

## 专有插件的公共绘制接入（1.1）

`Path::arcTo(end, radii, rotation, sweep, size)` 提供端点式椭圆弧，直接使用 Direct2D 的圆弧实现。
旋转角为度，半径必须为有限正数；`ArcSweep` 选择顺/逆时针，`ArcSize` 选择小/大弧。
`moveTo(point, false)` 开始仅描边、不参与填充的图形，默认仍为可填充图形。
路径继续支持多个子图形、DPI 切换复用以及工厂归属检查。示例：

```cpp
auto path = canvas.path();
path.moveTo({40, 20}).arcTo({40, 60}, {20, 20}).arcTo({40, 20}, {20, 20}).close();
canvas.fill(path, 0x80C8A0FF);
canvas.rectangle({10, 10, 8, 8}, 0xFFFFFFFF, .25);
```

`rectangle(area, color, opacity)` 绘制直角矩形，透明度直接乘入画笔颜色，不创建透明度图层。
多个图形需要整体透明度时继续使用 `withOpacity`。原有 `box` 圆角矩形行为保持不变。
锦鲤的曲线路径与荷叶缺口、盆栽叶片与花盆、唱片反光已使用公共 Path；沙漏的矩形、变换、
普通文字与背景通过 Canvas 绘制，沙粒模拟和像素数字仍属于插件逻辑。

## 内置插件控件迁移（1.1）

`CyberCanvas/DesktopWidgets.h` 为固定尺寸桌面插件提供组合组件。普通按钮、进度条和布局仍使用已有 Widgets/Components；复杂动画通过原生扩展保留。具体迁移清单与验收见仓库 `docs/PLUGIN_CONTROLS_MIGRATION.md`。

| 公共能力 | 使用方式 |
| --- | --- |
| Label | 字体、裁剪、省略、按区域缩小和可选投影；无默认背景 |
| MetricItem / StatusLine | 独立文字槽适配横排、纵排和紧凑布局；业务层提供单位、文案及缓存状态 |
| Checkbox / TaskRow / TextRow | 复选标记、任务/资讯/便签行，背景反馈与分隔线 |
| ProgressRing / SegmentedScale | 连续圆环与非等距业务阈值的等宽分级条；保持 MeterGauge 原有分段外观 |
| CalendarGrid / CalendarCell | 共用日期格尺寸、命中、选中/今天背景和两行文字；不包含任何日期换算算法 |
| cdsdk::Controls | 已有布局的按钮/复选项共享鼠标、禁用、焦点、Tab/Shift+Tab、Enter/Space 和宿主可访问性 |

下面的片段放在已有 Canvas 帧作用域内；保留 `ProgressRing` 实例可复用不变的路径：

```cpp
#include <CyberCanvas/DesktopWidgets.h>

auto theme = cybercanvas::WidgetTheme::light();
cybercanvas::ProgressRing ring;
ring.draw(canvas, {12, 12, 80, 80}, .65, theme);
cybercanvas::MetricItem metric;
metric.caption = {{104, 12, 120, 20}, L"下载", theme.font, theme.muted};
metric.number = {{104, 36, 120, 32}, L"12.8 MB/s", theme.font, theme.foreground};
metric.draw(canvas);
cybercanvas::ContentNotice{{12, 104, 212, 40}, {}, L"暂无内容",
                           cybercanvas::ContentState::Empty, false}.draw(canvas, theme);
```

使用外部字体时先调用 `Canvas::fontCollection()`，它持有 COM 引用并在集合变化时失效文字缓存。`TextStyle::tabularFigures`、`characterSpacing` 也参与缓存及 Scene 测量失效。`nativeTextLayout()` 仅供特殊基线/浮雕扩展测量和绘制，不允许修改缓存布局或跨帧持有。普通文字优先使用 Label/Canvas::text。

`cdsdk::Controls` 需要稳定非零 ID，传入与绘制相同的局部矩形和宿主 safe origin。在布局/设置切换或隐藏时 `cancel()`；输入前同步最新 items，因此首次绘制前点击也有效。内容替换时会取消旧条目的按压。旧宿主无键盘服务时继续提供鼠标操作；功能图标通过 SDK 的可选宿主 Lucide 服务提供，独立离线渲染器需绑定该服务才能显示图标。

例如在插件实例中长期持有 `cdsdk::Controls controls`，初始化时 `controls.bind(context, onActivate)`，然后让绘制和输入共用同一组矩形：

```cpp
// Run before rendering and before input; labels are localized by the plugin.
const cybercanvas::Rect buttonBounds{12, 12, 80, 28};
controls.items({{1, buttonBounds, "Refresh", !loading}}, {safe.x, safe.y});
// Inside the Canvas frame, which already translates by safe origin:
cybercanvas::Button button{1, buttonBounds, L"刷新"};
button.enabled = !loading;
button.draw(canvas, theme, controls.visual(1));
controls.drawFocus(canvas, theme);
// In the input callback, after updating items:
controls.input(event);
```

`onActivate(uint32_t id)` 只更新业务状态并申请当前插件重绘；数据拉取仍走已有后台服务。布局或设置变化、隐藏时调用 `cancel()` 会同步撤销宿主焦点，恢复显示后按正常输入流程重新获取。SDK 源码包与安装清单已包含新头文件。

## 后续开发：桌面组件主题与多实例验收

当前源码增加了可选的 `WidgetTheme::desktop(lightAppearance)`：7 px 圆角、10 px 内容留白、8 px 组件间距、圆形滑块 thumb，以及 32 px 按钮最小自然高度。大字体仍按文字实测高度扩展；显式布局尺寸优先。旧 `dark()/light()` 默认值保持不变，现有插件可逐个接入。

`WidgetTheme` 和 `ThemePatch` 增加 `controlHeight`、`disabledOpacity`。高度变化触发重新排版，透明度变化只触发重绘；局部覆盖遵守组件树继承。按钮、图标按钮、滑块、开关统一读取禁用透明度。C++ 使用者必须一起重新编译头文件与静态库，不混用旧布局库。

`DataWidgets.h` 的 `ContentNotice` 提供 Ready/Loading/Empty/Error 提示，标题和说明由调用方本地化，统一使用主题颜色、圆角、留白、真实文字测量与裁剪。组件没有定时器、窗口、网络请求或隐式重试；需要操作时组合已有 Button，业务决定刷新频率。比如：

```cpp
auto theme = cybercanvas::WidgetTheme::desktop(false);
scene.theme(theme);
cybercanvas::ContentNotice{{12, 12, 280, 100}, L"暂无内容", L"添加项目后将在这里显示",
                           cybercanvas::ContentState::Empty}.draw(canvas, theme);
```

`cybercanvas_theme_tests <output-directory>` 导出明暗主题与 DPI 1/1.5/2/3 状态图，验证禁用透明度不重排、最小高度、继承、非法值拒绝和小尺寸裁剪。SDK 打包脚本已将该测试加入源码白名单及验证目标。

私有宿主的专项入口为 `scripts/verify-canvas-development.ps1`，默认最多 8 个构建作业，仅跑相关组件测试和 `--verify-canvas-instances`。后者使用 2/6/12 个固定数据 Canvas 实例，通过真实 PluginTileItem、变化区域上传和 D3D11 合成，覆盖静止、单实例、部分动画、全动画、暂停和恢复；逐实例记录绘制次数、Canvas/像素转换耗时、受控资源和输出缓冲字节。稀疏更新检查局部合成，全卡片变化允许依赖区域覆盖全屏，但不允许不变卡片重绘。

报告分别给出 CPU 提交耗时和 D3D11 时间戳、P50/P95、上传字节与提交次数；两项计时不能相加当作屏幕延迟。它不含 DLL 初始化、外部数据、网络、DWM/显示延迟，也不是系统总内存统计。正确性与 16.7 ms 性能预算独立报告；传入 `-EnforceTiming` 才将超预算作为命令失败，避免把本机硬件数字变成所有机器的功能断言。Windows 10 22H2 与 Windows 11 25H2 两台机器已完成本次专项；远端 CI、其他系统版本和真实服务混合负载仍需单独验收。本次属于后续源码开发，没有覆盖已经审计的 1.0 发布归档。

独立、轻量的 C++20 二维绘制引擎，第一版面向 Windows 10/11 x64。底层使用 Direct2D、DirectWrite、WIC；不依赖 Qt、插件 SDK、protobuf 或浏览器，也没有额外运行时 DLL。MIT 许可。

### 内置组件绘制开销优化

完全不透明的内置按钮、滑块和开关只保存绘制状态，不再创建多余的透明度图层；反馈强度为零的背景和光圈直接跳过。半透明、禁用状态以及任意图标回调仍保留整体透明度合成，公共 `Canvas::withOpacity` 语义不变。`CanvasStats` 新增 `opacityLayers` 和 `rasterNanoseconds`，后者是已包含在帧耗时中的 `EndDraw` 耗时，不能重复相加。

同机同负载对比，12 实例全动画 CPU P95 从 18.26 ms 降到 13.48 ms，恢复动画从 19.26 ms 降到 13.55 ms；没有降低动画采样频率。18 个宿主场景通过正确性和可选耗时检查，静止及暂停仍零刷新。三张宿主截图逐像素一致，明暗主题与四种 DPI 的八张图最多相差 3/255 通道值。结果仅代表该次本机测量；已编译插件需重新编译才能使用头文件内的优化。

支持范围为 Windows 10 和 Windows 11 x64；兼容性和性能分别验收，无独显设备先验收基本功能，耗时仅记录。Company 的 Windows 11 25H2 / Intel UHD 630 已通过六组绘制库检查、工作台预览和 18 个宿主场景；测试不强制耗时门槛。首次专项包漏带视频运行库和材质资源，补齐后宿主通过，没有修改显卡驱动或降低渲染质量。详见 [两机验证记录](../../docs/verification/canvas-opacity-optimization.json)。

## 使用

```cmake
find_package(CyberCanvas 1.0 CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE CyberCanvas::CyberCanvas)
```

```cpp
#include <CyberCanvas/Canvas.h>

// Canvas is retained and reused on one rendering thread.
cybercanvas::Canvas canvas;
cybercanvas::FrameBuffer output{width, height, stride, capacity, rgba, dpi};
canvas.render(output, {}, [](auto& draw, auto area) {
  draw.box(0, 0, area.width, area.height, 16, 0x202B3CFF);
  cybercanvas::TextStyle title;
  title.size = 18;
  title.bold = true;
  draw.text(L"Hello", title, {16, 12, area.width - 32, 32}, 0xFFFFFFFF);
});
```

尺寸和 stride 使用物理像素与字节；绘图坐标使用逻辑像素。输出为预乘透明度 RGBA8，颜色常量为 `0xRRGGBBAA`。输出内存归调用方所有。每个 Canvas 在创建它的线程上使用和销毁，不在不同线程间共享。

## 绘制、测量和布局

- 圆角矩形、椭圆、线段、多边形、渐变、PNG 图片及精灵图。
- 可选字体、字号、语言、字重、对齐、换行、省略号；透明文字使用灰度抗锯齿。
- 自动 DPI 目标重建、透明清屏、安全区域和嵌套裁剪；异常或提前退出会结束当前绘制事务。
- 复用画布、画刷、字体布局和解码图片；布局缓存上限 384，图片缓存各 64，渐变缓存 32。
- 图片保留原始细节，超大素材最长边缩至 4096 物理像素；按目标 DPI 绘制并平滑缩放。
- WIC 拷贝与无损通道重排写入调用方像素缓冲；支持带行填充的输出和 PNG 导出。
- 保留路径、多轮廓填充规则、二次/三次贝塞尔曲线；描边可配置宽度、端点、连接和虚线。
- `saved(callback)` 保存并恢复变换、抗锯齿和共享画刷；`translate/rotate/scale` 使用局部坐标，旋转单位为度，正角度沿屏幕顺时针。
- `withOpacity(alpha, callback)` 对一组内容整体合成透明度，重叠区域不会重复变淡；图层、状态、描边均复用，状态嵌套上限 32。
- `measureText(text, style, size)` 可在帧外使用，返回 DirectWrite 的宽、高、含尾部空白宽度与行数。它和绘制共用字体、字重、换行、行距、方向及省略号设置；返回完整布局高度，可能超过指定高度，不等同于实际裁剪后的像素边界。
- `TextStyle` 支持 Normal/Medium/SemiBold/Bold、可选固定行高/基线、Auto/LTR/RTL 段落方向；Auto 使用首个强方向字符判定。
- `inset/aligned/Stack/divide` 提供内边距、对齐、从首尾分配横排/竖排空间和等分布局。超过可用空间的尺寸会收缩至边界；等分布局最多 4096 项。

```cpp
auto curve = canvas.path();
curve.moveTo({0, 30}).cubicTo({30, 0}, {60, 60}, {90, 20}).finish();
canvas.render(output, {}, [&](auto& draw, auto area) {
  cybercanvas::Stack column(cybercanvas::inset(area, {16, 16, 16, 16}), cybercanvas::Axis::Vertical, 8);
  cybercanvas::TextStyle style;
  style.wrap = true;
  auto measured = draw.measureText(L"Hello, world", style, {column.remaining().width, 100});
  draw.text(L"Hello, world", style, column.take(measured.height), 0xFFFFFFFF);
  auto plot = column.remaining();
  draw.withOpacity(.75, [&] {
    draw.translate(plot.x, plot.y);
    draw.rotate(5);
    draw.stroke(curve, 0x69D4B6FF, {.width = 2, .cap = cybercanvas::LineCap::Round});
  });
});
```

路径第一次绘制或 `finish()` 后不可再修改，归创建它的 Canvas factory 所有，可跨该 Canvas 的帧和 DPI 重建复用，不能跨不同 Canvas。`saved` 与透明度、裁剪回调只能在活动帧内使用；回调中不要手动结束或取消帧。异常会自动恢复作用域，但不会回滚已画出的内容；整帧放弃时不会写入输出缓冲。

当前后端使用 CPU 软件光栅化，适合小型二维界面和内容纹理。宿主负责窗口、输入事件分发、刷新调度以及最终 GPU 合成。自动布局支持常用 Flex 规则；Grid、多关键帧编排和配置解释器尚未包含。复杂原生绘制可通过公开的 Direct2D factory、target 和 brush 接口扩展；不要自行修改 Canvas 管理的 BeginDraw/EndDraw 或裁剪/图层栈。图层透明度和排版分别遵循 [Direct2D](https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-layers-overview) 与 [DirectWrite](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nn-dwrite-idwritetextlayout) 的行为。

## 后台图片资源

0.10 的 `<CyberCanvas/Images.h>` 提供独立静态 target `CyberCanvas::Resources`。`ImageStore` 使用一个后台工作线程和 Windows WIC 读取、缩放、解码本地图片；UI 线程请求只做路径词法规范化与有界队列操作。工作线程只发布不可变的预乘 BGRA8 像素，不访问 Canvas、Scene 或宿主，不传递 COM 对象、不调用 UI 回调。`ImageHandle::snapshot()` 返回 Empty/Loading/Ready/Failed/Cancelled，调用方在自己的事件循环中处理变化。

```cpp
#include <CyberCanvas/Images.h>

auto images = cybercanvas::ImageStore::shared();
auto cover = images->request(L"assets/cover.png", 0, 2048);
// Later, on the rendering thread, inside a frame:
auto result = cover.snapshot();
if (result.state == cybercanvas::ImageState::Ready) {
  canvas.image(*result.image, {12, 12, 120, 120}, {cybercanvas::ImageFit::Cover});
}
```

链接 `CyberCanvas::Resources`；使用 Scene 时另链接 `CyberCanvas::Layout`。相同词法绝对路径、revision、maximumEdge 共用一次请求和解码；路径大小写仍参与键比较。修改文件后递增 revision，旧句柄和新版本互不覆盖。图片最长边最多 4096，超大图片按比例缩小，不上采样。文件读取和存在性检查全部在工作线程；不存在、损坏和超限文件成为 Failed，错误文本供调试使用，不应原样作为产品提示。

默认解码像素预算 64 MiB、输入文件上限 64 MiB、最多 128 条缓存/排队记录。`ImageLimits` 可配置这些限制。按最近请求顺序回收无消费者且无外部快照持有的资源，仍被句柄或快照使用的像素不能强制回收；预算不足时新请求失败，释放资源后以新 revision 重试。预算包含正在解码时预留及外部快照保留的像素，不包含 WIC 编解码器内部临时内存和容器管理开销。`stats()` 返回解码/命中/失败次数、条目/待完成数、当前和峰值像素字节，`trim()` 回收可释放条目。

`cancel()` 释放该句柄的兴趣，复制的句柄和其他实例继续有效。所有消费者都离开后，尚未开始的任务可跳过；已进入 WIC 的文件 I/O/解码允许结束，取消不会强行中断系统编解码器。最后一个 store 所有者销毁时等待工作线程退出，未完成句柄变为 Cancelled；已获得的不可变像素可继续使用。宿主必须先销毁使用者再卸载插件 DLL。网络下载、文件监听和自动重试属于后续扩展。

`Canvas::image(ImageData, bounds, ImagePlacement)` 在 Canvas 所在线程上传，并提供 Contain/Cover/Stretch、横向精灵图 cell/cells 与目标区域裁剪。上传缓存单独限制为 64 MiB/64 张，按使用顺序回收；DPI/目标改变后从解码数据重新上传，不重新访问文件。`imageUploadBytes()` 用于统计，`clearImageUploads()` 主动释放该类上传。旧 `image(filename, ...)` 保留同步兼容行为；其原有缓存不计入新资源预算。

`Scene::imageResource(node, snapshot)` 只使绘制失效，保持已有几何；相同快照保持静止。Loading/Failed 使用占位，失败可通过 `IconPainter` 绘制 Lucide image-off。`View::imageFit` 控制适配，原有 imageCells/imageCell 继续支持主题精灵图；删除节点会释放其快照引用。

插件使用 `<sdk/Images.h>` 的 `cdsdk::Images`，SDK target 自动链接资源库。`bind(context)` 后 `set(slot, path, revision, edge)` 为实例管理资源槽，`get(slot)` 读取快照。完成只在宿主线程投递；33 ms 临时定时器仅在待投递期间存在，完成后释放。`onChange()` 可接入 Scene 失效，没有回调时默认请求当前实例重绘。隐藏时 `pause(true)` 停止投递/定时器，恢复后投递最新结果，销毁前自动退订；无宿主预览显式调用 `poll()`。白噪音两种尺寸已接入，绘制过程不再同步读取插画文件。

`examples/Images.cpp` 接收输入图片、输出 PNG 和可选精灵帧数，导出 Loading/Ready 或失败占位图，同时报告本机请求耗时、100 次完整纹理绘制均值、解码/上传字节及布局次数。该均值含输出缓冲分配、绘制与像素转换，不含 PNG 编码，仅作本机基线，不作为通用性能保证。

## 自动布局与组合组件

`CyberCanvas/Flex.h` 封装 [Yoga 3.2.1](https://www.yogalayout.dev/docs/getting-started/laying-out-a-tree)，支持横排/竖排、伸缩权重、换行、间距、内外边距、百分比、最小/最大尺寸、隐藏与 RTL。节点只保存布局，绘制、输入与窗口由调用方管理。

```cmake
target_link_libraries(my_app PRIVATE CyberCanvas::Layout)
```

```cpp
#include <CyberCanvas/Components.h>

cybercanvas::FlexStyle column;
column.padding = {12, 12, 12, 12};
column.gap = 8;
cybercanvas::FlexLayout layout(column);
cybercanvas::TextStyle font;
font.wrap = true;
auto title = layout.add(0, {}, cybercanvas::measuredText(canvas, L"Measured title", font));
cybercanvas::FlexStyle body;
body.grow = 1;
body.basis = cybercanvas::Length::points(0);
auto content = layout.add(0, body);
layout.calculate({0, 0, 288, 200}, 1.5);
auto titleBounds = layout.clippedBounds(title);
auto bodyBounds = layout.clippedBounds(content);
```

保留树后，`calculate()` 在样式、可用尺寸、DPI、方向均未变化时复用结果；返回 false 表示没有重新排版。文字内容变化后调用 `invalidate(measuredLeaf)`，或用 `measure(node, callback)` 替换测量函数。`measuredText()` 捕获文字与样式快照，Canvas 必须比布局树活得更久。测量叶节点不能再包含子节点，也不能在测量回调内修改树。

`bounds()` 返回布局本身，`clippedBounds()` 返回与祖先及视口相交后的区域；最小尺寸超过可用空间时保留布局约束，由裁剪区域限制绘制和输入。根尺寸由 `calculate()` 的视口决定；所有位置为逻辑世界坐标，边缘按实际 DPI 对齐物理像素。此模块最多 4096 个节点，不创建线程或定时器。

`CyberCanvas/Components.h` 提供 `IconLabel`、`TitleActions`、`LabelledSlider`，以及纯布局结果 `titleActions()`、`wrappingRow()`、`textSections()`。组合组件统一计算文字和控件位置，绘制、命中、滑块数值转换共用这些位置。文字采用 DirectWrite 的真实测量结果；标题可省略，标签可换行，操作按钮保留独立空间。白噪音声轨按空间换行，资讯来源按钮换行后自动下移文章区；文章标题、摘要和元数据分配各自空间。

基础绘制 target 仍是头文件接口；使用自动布局时额外链接静态 `CyberCanvas::Layout`。安装包包含布局库、静态 Yoga 和 MIT 许可，消费者无需另外安装 Yoga，也不增加 DLL。源码构建通过 vcpkg 解析 Yoga；许可安装于 `share/cybercanvas/third_party/yoga/LICENSE`，分发使用布局的二进制时一并附带。SDK target 自动链接布局库，C ABI 1 不变。

## 保留组件树和自动更新

`CyberCanvas/Scene.h` 把现有布局、控件、输入和主题连接为一棵轻量组件树，复用 Yoga 与 DirectWrite，没有新增依赖。Row/Column 负责排列，Text/Image/Button/Slider/Toggle 是叶节点；Decoration 可承载自定义绘制。每个节点有稳定句柄，控件的 `key` 对应业务操作。添加一次后，通过 `text/value/theme/style/visible/enabled/pose` 等 setter 同步状态。

```cpp
#include <CyberCanvas/Scene.h>

cybercanvas::View column;
column.layout.padding = {16, 16, 16, 16};
cybercanvas::Scene scene(column);
cybercanvas::View title;
title.kind = cybercanvas::ViewKind::Text;
title.text = L"Volume";
title.theme.fontSize = 18;
scene.add(0, title);
cybercanvas::View volume;
volume.kind = cybercanvas::ViewKind::Slider;
volume.key = 10;
volume.layout.height = cybercanvas::Length::points(4);
volume.value = .4;
auto slider = scene.add(0, volume);

scene.onInvalidate([&] { request_this_widget_redraw(); });
scene.arrange(canvas, {0, 0, 288, 200}, actual_dpr);
// Host input callback; coordinates are logical pixels.
auto event = scene.input({cybercanvas::PointerKind::Press, {100, 50}, true});
if (event.key == 10 && event.value) { save_volume(*event.value); }
// Timer: advance feedback only while scene.active(); stop when settled.
// Inside a freshly cleared Canvas frame:
scene.draw(canvas, icon_painter);
```

`ThemePatch` 未设置的字体、颜色、圆角、内边距和间距继承父节点。可单独覆盖字号；覆盖 `font` 时替换完整文字样式。容器默认继承主题 gap，显式非零 `layout.gap` 优先；`inheritGap=false` 可指定零间距。叶节点由实际文字测量或 `intrinsic` 首选尺寸决定自然大小，显式 Flex 约束仍优先。

数值、颜色、图片帧和装饰图片 Pose 改变只标记重绘；影响自然尺寸的文字、字体、间距、可见性或布局约束改变标记重新布局；宽高均已固定的控件更新文字只需重绘。重复设置同值保持静止；`arrange()` 的尺寸、DPI、方向和状态不变时跳过排版与文字测量。`stats()` 可观测布局、测量和绘制次数。通知只在干净变为脏时触发一次，绘制成功后清除；回调只安排后续刷新，不在回调内同步渲染。

组件自动注册父级裁剪内的命中区域，绘制与拖动使用同一位置。重排、禁用或隐藏会取消旧手势；宿主隐藏实例、丢失捕获和销毁前调用 `cancel()`。`advance(seconds)` 推进短暂反馈，`active()` 为 false 时停止帧订阅。只读 `view()` 暴露描述，修改使用 setter。Pose 仅作用于装饰 Image，输入不随图片姿态变化。

0.9 支持动态修改组件树，继续复用 Yoga 节点，不必替换整棵 Scene：

```cpp
// Move the complete group, including its descendants, before another child.
scene.move(soundGroup, mixerPanel, oceanButton);
scene.move(soundGroup, mixerPanel);  // Omit the sibling to append.
scene.remove(placeholder);          // Releases the complete subtree.
scene.clear(mixerPanel);            // Removes children, preserving the panel.
```

`move(node, parent, before=root)` 支持跨容器移动与同级排序，返回是否发生变化；同一位置不会取消手势或请求刷新。`before` 必须是目标父节点的直接子节点，root 表示末尾。循环、移动根节点、移入叶节点及无效句柄会抛出异常，不改变树。`remove(node)` 删除子树并释放其描述、测量回调和自定义绘制资源；`clear(parent=root)` 清空子节点，空容器保持静止。根节点始终存在，不能删除或移动。`contains/parent/children/size` 可查询存活结构，根的 parent 为 root。

存活节点上限为 4096（包含根），删除后容量立即回收；句柄单调增长且不复用，旧句柄不会操作到后来创建的控件。业务 key 在删除后可重新使用，`find()` 只返回存活节点。移动保留句柄、控件值、选择与显式主题覆盖，继承主题、可见性及禁用状态根据新祖先重新计算。删除/移动取消旧几何上的鼠标手势，合并当前实例重绘，下一次 `arrange()` 重排。未失效的测量继续交给 Yoga 缓存；移动或约束变化需要时仍可重新测量，稳定布局保持静止。

绘制与命中均按当前树的深度优先顺序执行：先父节点，再按 `children()` 顺序绘制各个完整子树。后绘制的可交互节点命中优先，因此同一区域的子控件优先于可点击的父容器。自定义绘制在节点裁剪内执行，期间不可修改树。`FlexLayout` 同样提供 `remove/move/contains/parent/children`，其 `size()` 返回存活节点数。`examples/Dynamic.cpp` 导出结构修改前后两张 PNG，展示移动声音组件后保留的音量、业务 key 与新主题。

Scene 没有后台线程、定时器或窗口，不保留输出像素。一次请求仍绘制当前卡片的完整内容，值变化会跳过排版；`needsLayout/needsPaint` 用于避免静止卡片继续请求帧。每次宿主请求新纹理都应正常调用 `draw()`，即使 Scene 本身没有变脏。Canvas 必须在最后一次使用 Scene 期间保持有效；同一渲染线程上使用所有接口。

插件使用 `sdk/Scene.h`：`bind(create_context)` 复用可选捕获服务，并将失效通知映射到当前实例的 `request_redraw`。在安全区局部坐标绘制时，`input(event, {safe.x, safe.y})` 映射宿主逻辑坐标。业务仍负责保存设置、音频和计时；白噪音示例已通过同一组件树处理绘制、命中、主题与反馈。`examples/Scene.cpp` 可独立导出宽/窄版 PNG，无需 Qt。

## 滚动容器与可见项列表

`CyberCanvas/List.h` 提供纵向 `ScrollView` 和 `ListView`，链接同一个 `CyberCanvas::Layout` 静态 target。它们复用 Tweeny、控件主题、命中裁剪与捕获，不引入新的依赖。`ScrollView` 限制滚动边界，合并连续滚轮距离，使用时间缓动；滚动条单独预留空间，支持轨道点击和边界外拖动。默认每个滚轮刻度移动 48 逻辑像素、缓动 180 ms，样式可覆盖。

```cpp
#include <CyberCanvas/List.h>

cybercanvas::ListView list;
list.items({{"story-a", 72, 1}, {"story-b", 90, 1}});
list.arrange({8, 48, 272, 300}, actual_dpr);
list.onInvalidate([&] { request_this_widget_redraw(); });
// Revisions describe content changes; IDs describe identity.
list.update("story-a", 2);
// Inside a fresh Canvas frame:
list.draw(canvas, theme, [&](auto& draw, const cybercanvas::ListRow& row) {
  draw.text(title_for(row.id), theme.font, row.bounds, theme.foreground);
});
```

每个 `ListItem` 有唯一且非空的字符串 ID、首选行高和内容版本。行高由调用方给定或预估，只有可见项需要文字排版；可在测量可见内容后用 `update(id, revision, newHeight)` 修正高度。首次快照或结构/行高变化构建累计高度表，通过二分定位第一条可见内容；逐条 `update()` 不测量屏幕外文字。相同快照不请求刷新，单条版本更新保留高度索引，屏幕外版本更新保持静止。可用 `stats()` 检查高度表构建和实际绘制条目数。

插入、删除或调整高度时尽量保留顶部可见 ID 及其行内偏移；该 ID 被移除时采用后续仍存在的条目，最后限制到合法边界。`select(id)` 按 ID 保留选中状态，删除后清除。`rows()` 只返回可见条目及视口裁剪，`draw()` 自动隔离每条自定义绘制的状态。`actions(provider)` 可给可见行增加最多 16 个操作区域；action 0 是整行点击，其余为每行唯一的非零 ID。点击结果复制业务 ID，数据改变前的手势不会激活另一条内容。内部控件 ID 缓存有界，业务 ID 不依赖这些临时控件 ID。

`ListStatus` 统一 Ready/Loading/Empty/Error；列表为空时绘制标题、说明和可选重试按钮。文字由调用方提供，图标通过公共 `IconPainter` 绘制，未提供图标时仍保留文字和操作。已有内容时继续展示内容，刷新/离线说明可放在调用方的页脚。行高至少 1 逻辑像素，最多 100 万条，累计内容高度不超过 10 亿逻辑像素。修改、绘制和输入都在同一 UI/渲染线程，自定义绘制与命中生成回调中不可修改列表。

宿主调用 `advance(seconds)` 推进滚动和悬停反馈，`active()` 为 false 时停止画面定时器。`cancel()` 结束捕获并停在当前位置，隐藏时不推进动画。无窗口、线程或定时器由列表创建；每次仍生成当前卡片纹理，只为可见项调用绘制回调。视口变化、数据身份变化、滚轮和隐藏会取消旧点击；滚动条使用父级裁剪和实际 DPI，输入与绘制共用同一位置。

插件使用 `sdk/List.h`，其 `bind(context)` 自动请求当前实例刷新并接入已有捕获服务；`input(event, {safe.x, safe.y})` 处理宿主逻辑坐标，120 单位滚轮刻度换算为上述逻辑距离。资讯默认使用滚动模式，可通过页脚按钮或 `list_mode` 设置切回分页；收藏、讨论和打开文章均按 URL 稳定 ID 操作。滚动位置保留在实例内，不写入插件持久状态。独立示例 `examples/List.cpp` 可导出列表及加载、空内容、错误状态 PNG。

## 时间驱动的动画

`<CyberCanvas/Animation.h>` 是不包含 Windows/Qt/插件 API 的时间与插值模块：

- `AnimationClock` 接收宿主单调毫秒时间，首次 `tick()` 只建立锚点；`advance(seconds)` 用于离线绘制。两种驱动方式择一使用。
- 暂停/恢复会重建锚点，排除隐藏期间的时间。`setSpeed()` 保留当前位置，倍率为 0 时停止推进；修改倍率前可先以旧倍率 `tick()` 至当前宿主时间。
- 默认每次最多推进 0.25 秒，避免长时间卡顿后突然跳跃；可通过 `setMaximumStep()` 调整。正常帧率下按时间累计，不按帧计数。
- `Tween<double/Point/Size/Pose>` 按绝对秒数采样；`Repeat::Once/Loop/PingPong` 支持单次、循环、往返。`cycles=0` 表示无限循环，PingPong 的一个周期包含一次往返。
- `Pose` 一次插值位移、旋转、缩放和透明度；`Canvas::posed()` 作用于局部坐标并在离开作用域后恢复，透明度限制在 0—1。
- Linear/InCubic/OutCubic/InOutCubic/InOutSine/OutBack 缓动复用 [Tweeny 3.2.1](https://github.com/mobius3/tweeny/tree/v3.2.1)。从 vcpkg 安装结果保留必要的单个头文件，源码、哈希及 MIT 许可位于 `third_party/tweeny`；安装包包含头文件和许可，不要求消费者安装 vcpkg，也不增加运行时 DLL。

```cpp
#include <CyberCanvas/Animation.h>

cybercanvas::AnimationClock clock;
cybercanvas::Tween<cybercanvas::Pose> pulse(
    {}, {.scale = {1.04, 1.04}},
    {.duration = 2.4, .easing = cybercanvas::Ease::InOutSine,
     .repeat = cybercanvas::Repeat::PingPong, .cycles = 0});

// Retain these objects in the instance; this code runs in the host timer.
const auto before = pulse.sample(clock.seconds());
const auto tick = clock.tick(host_monotonic_ms);
const auto after = pulse.sample(tick.seconds);
if (tick.delta > 0 && (before.active || after.active)) {
  request_this_widget_redraw();
}
// Inside the drawing callback:
draw.posed(after.value, [&] { draw.oval(20, 20, 10, 10, 0x69D4B6FF); });
```

动画模块不创建线程、定时器或窗口。宿主决定订阅频率与刷新区域；单次动画结束时应提交最终帧，然后停止请求帧。盆栽轻摆和白噪音呼吸共用这一接口，默认 0.75 倍、20 FPS；交互反馈使用独立的正常速度时钟。关闭常驻动画后，盆栽浇水仍会短暂播放反馈，结束后停止定时器；白噪音暂停或静音时仅低频查询共享音频状态，不重绘静止画面。隐藏卡片时取消画面定时器，声音继续播放。

SDK 的 `sdk/Canvas.h` 只做 `CdFrame`、`CdRect` 和插件语言适配，绘制实现都在本库。现有盆栽、白噪音、农历、资讯插件通过这一适配层共用引擎。

## 鼠标交互

`CyberCanvas/Interaction.h` 提供矩形、椭圆和保留路径命中、变换逆映射、裁剪交集、重叠优先级与按钮/拖动状态。路径使用 [Direct2D FillContainsPoint](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1geometry-fillcontainspoint(d2d1_point_2f_constd2d1_matrix_3x2_f_float_bool))，支持多轮廓孔洞。输入始终使用逻辑像素，物理坐标只通过 `logicalPointer(point, dpr)` 转换一次。

```cpp
#include <CyberCanvas/Interaction.h>

cybercanvas::InputRouter input;
input.regions({
    {1, cybercanvas::HitShape::ellipse({12, 12, 28, 28})},
    {2, cybercanvas::HitShape::rectangle({52, 18, 120, 20}), cybercanvas::PointerBehavior::Drag},
});
auto result = input.dispatch({cybercanvas::PointerKind::Press, {80, 25}, true});
// result.drag == Begin; input.captured() == true.
// Feed Move/Release even when the pointer leaves the target or window.
// Clamp slider values in the application, not the pointer position.
```

目标 ID 必须唯一且非零。较高 `priority` 优先，同级以最后注册者优先；禁用目标不参与命中。`HitShape::transformed(matrix)` 使用与绘图一致的 Direct2D 变换，`HitRegion::clips` 中各裁剪形状使用统一的世界逻辑坐标。`PointerResult::local` 返回目标形状变换前的坐标。普通按钮移动超过 5 个逻辑像素或收到 Leave 后取消点击；Drag 在按下时捕获，持续接收边界外移动，松开或 Cancel 后结束。

更新目标列表保留仍存在且启用的目标状态；删除/禁用目标会清除状态。布局、内容身份改变、暂停、丢失系统捕获和销毁前，调用方应先发送 Cancel 并释放宿主捕获。`InteractionFeedback` 提供 140 ms 的缓出过渡，重新定向从当前可见值开始；`active()` 结束后停止帧订阅。模块不创建窗口、线程或定时器。

插件使用 `sdk/Interaction.h` 的薄适配层映射 `CdInput`，并通过可选的 `ui.interaction` 服务申请捕获。启用后，按钮按下即时反馈；只有滑块等 Drag 控件占用拖动手势，其他区域仍可拖动卡片。旧宿主继续支持点击设置数值，连续拖动需要新版宿主。公开 C ABI 仍为版本 1。

## 可复用控件

`CyberCanvas/Widgets.h` 包含 `Button`、`IconButton`、`Toggle`、`Slider` 和 `ProgressBar`。控件是轻量值对象，绘制与命中使用同一份 `bounds`；不持有窗口，不创建线程或定时器。业务值由调用方保存，按钮返回点击，滑块/开关返回可选的新值。

```cpp
#include <CyberCanvas/Widgets.h>

cybercanvas::Slider volume(1, {16, 60, 180, 4}, 0, 100, 1);
cybercanvas::InputRouter input;
cybercanvas::WidgetState feedback;
double value = 25;
input.regions({volume.region()});

auto event = input.dispatch({cybercanvas::PointerKind::Press, {100, 62}, true});
if (auto next = volume.change(event)) {
  value = *next;
}
feedback.sync(input);
feedback.advance(.016);  // Advance on scheduled frames; stop when inactive.
// Inside an active Canvas frame:
volume.draw(canvas, cybercanvas::WidgetTheme::dark(), value, feedback.visual(1));
```

`WidgetTheme::light()/dark()` 提供字体、文字色、强调色、表面、边线、轨道、圆角、内边距、间距和滑块半径，可覆盖单项。布局继续使用 `Stack/divide/inset`，将 `theme.gap` 交给布局。标签自动居中、省略和裁剪，禁用状态整体透明且不响应输入。按钮有 Filled/Outline/Quiet 三种样式及选中状态。滑块支持自定义范围、步进、命中留白和边界外拖动；步进不能整除范围时，两个端点仍可到达。

`WidgetState::sync(router)` 同步悬停/按下状态，`advance(delta)` 推进可中断缓动，`active()` 为 false 后停止帧订阅。反馈最多保留 256 个 ID，结束且透明的记录会移除。布局或内容身份改变时先取消输入，再清空状态。调用 `region(origin)` 可映射到安全区；复杂变换和裁剪可继续设置返回的 `HitRegion`。

`IconButton` 通过 `IconPainter` 接收图标，提供者返回 false 时绘制文字标签。原生插件使用 `sdk/Icons.h`：它通过可选 `ui.icons` v1 服务调用宿主的 `CyberGlass/Icons.h`，共用仓库 Lucide SVG、主题着色和实际 DPI 缓存。插件本身不链接 Qt。旧宿主和独立预览使用文字后备，仍可操作。`Canvas::pixels(key, frame, bounds)` 支持不可变的预乘 RGBA 图片上传，最多缓存 64 项，目标重建时清除；内容变化需要新 key。

`examples/Widgets.cpp` 是独立 Win32 交互示例：按钮、禁用按钮、滑块、进度条、主题开关及图标后备。不带参数时打开窗口，传 PNG 路径时导出 2 倍 DPI 图片。白噪音的播放/定时/翻页与音量、资讯的收藏/刷新/分页/来源切换已接入这一层。

## 独立构建与验证

在 VS x64 开发终端执行。无需 Qt；源码构建需要 vcpkg 静态 Yoga 3.2.1：

```powershell
vcpkg install yoga:x64-windows-static-release --overlay-triplets=sdk/triplets
cmake -S libraries/cybercanvas -B build/cybercanvas -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/source/vcpkg/installed/x64-windows-static-release
cmake --build build/cybercanvas --target cybercanvas_tests cybercanvas_advanced_tests cybercanvas_animation_tests cybercanvas_interaction_tests cybercanvas_widget_tests cybercanvas_flex_tests cybercanvas_component_tests cybercanvas_scene_tests cybercanvas_list_tests cybercanvas_list_demo cybercanvas_scene_demo cybercanvas_dynamic_demo cybercanvas_image_tests cybercanvas_images_demo cybercanvas_widgets_demo cybercanvas_demo --parallel 10
ctest --test-dir build/cybercanvas --output-on-failure
build/cybercanvas/cybercanvas_demo.exe build/cybercanvas/demo.png
# Optional: sample the same animated badge at two seconds.
build/cybercanvas/cybercanvas_demo.exe build/cybercanvas/demo-2s.png 2
build/cybercanvas/cybercanvas_widgets_demo.exe build/cybercanvas/controls-dark.png
build/cybercanvas/cybercanvas_widgets_demo.exe build/cybercanvas/controls-light.png light
build/cybercanvas/cybercanvas_dynamic_demo.exe build/cybercanvas
# Run without arguments for the native interactive example.
cmake --install build/cybercanvas --prefix build/cybercanvas-install
```

示例和测试使用静态 MSVC CRT `/MT`。集成到自己的应用时可在创建目标前设置 `CMAKE_MSVC_RUNTIME_LIBRARY` 为 `MultiThreaded`，与项目统一。


## 1.0 长期任务与当前进度

目标是独立交付、持续维护、减少 C++ 插件公共代码。原生插件保留编译，核心独立于 Qt/SDK，采用 Windows x64 `/MT`；所有编译最多 10 个并行作业。日常按改动运行专项检查，不默认重跑完整 Controller/GPU 矩阵。版本随交付推进，按验收关闭阶段。

| 阶段 | 状态 | 交付与关闭条件 |
| --- | --- | --- |
| P0 稳定基线 | 已收口 0.9 | 接口/线程/所有权/失效约定见本指南；动态树专项、安装消费与源码导出通过，基线提交 `4b4a83d`；当前指标使用 SceneStats/ListStats，增加图片请求/绘制示例作为本机测量入口 |
| P1 图片资源 | 已完成（0.10） | 有界后台 WIC、句柄/取消/版本、像素与上传预算、占位、SDK 完成投递、白噪音接入、独立示例和导出验证；网络与文件监听另列扩展 |
| P2 更新与动画调度 | 已完成 | SDK 适配层统一临时帧、订阅所有权、暂停恢复和刷新合并；静止卡片不请求绘制，隐藏停止视觉订阅，音频继续由业务决定 |
| P3 常用组件 | 已完成 | 主题令牌、图片/标签页/选项/数值/折线图及 Scene/List 组合；至少两类真实插件验证，共用布局和命中几何 |
| P4 输入与可访问性 | 已完成 | 焦点、键盘导航、滚轮传播与嵌套滚动，宿主版本协商和旧版降级；常用交互具备名称、角色、状态 |
| P5 性能诊断 | 已完成 | 实例级布局/测量/绘制/解码/缓存/帧请求统计与可复现基准；以测量决定静态子树缓存，保留优化前后证据 |
| P6 开发工具 | 已完成 | 升级现有预览/CLI，支持尺寸/DPI/主题、模拟数据、固定时间、命中边界、截图和资源热重载；C++ 使用增量构建及预览进程重启 |
| P7 插件迁移 | 已完成（P7-01—04） | 分批迁移公共底层，保留专有绘制；逐批检查外观、交互、状态恢复与性能，保持已有设置 |
| P8 1.0 开发套件 | 已完成（本地交付） | 公共 API、模板、许可、安装包、源码导出及仓库外编译/正式宿主安装恢复验收完成；详见末尾 P8-01—04 与验收 JSON |

P7 最终盘点：公共主画布迁移已完成；白噪音使用 Scene，资讯使用 List。时钟、沙漏、系统监控、天气、空气质量、音乐/相册、效率工具和锦鲤的主帧均复用 Canvas。专有离屏缓存和视觉算法按末尾保留项清单维护。底层迁移不等于所有插件都已改成 Scene；后续控件迁移见本文开头。

1.0 完成后，配置式免编译插件可作为独立解释层，复用组件树、资源、数据与动作接口。GPU 后端、跨平台及完整应用框架按实际需求单独立项。

公共 C ABI 与引擎 C++ API 分别版本化：C ABI 1 的固定结构继续保持合约；使用静态引擎时必须配套同一版本的头文件和库，并重新编译使用方。0.x 阶段的 C++ 接口调整在 CHANGELOG 中记录，不承诺混用不同版本 C++ 二进制。

### 长期任务：完成 CyberCanvas 1.0 开发者套件

任务起点：P0/P1 已完成验证，0.9 基线已提交；0.10 已提交为 `20bb0aa`。T00/P2/P3/P4/P5/P6 已完成，当前执行阶段为 P8。以下清单记录剩余工作，勾选必须附带实现和验证依据，不能仅依据版本号或依赖链接判断完成。

最终交付：第三方开发者仅依赖开发套件及声明的构建工具，在本体仓库之外创建、编译、预览和打包支持主题、实际 DPI、后台图片、动画、交互与状态恢复的原生插件，并在正式宿主中安装使用。C++ 插件保留编译；资源变化可热重载，C++ 修改通过增量构建和预览进程重启生效。

### T00：收口当前版本

- [x] T00-01：核对 0.10 源码、SDK 导出白名单、版本、许可和验证报告，运行自身改动的格式检查，单独提交当前图片资源工作。
- [x] T00-02：记录可复现的基线命令、测试输入和当前设备上的请求/布局/绘制/缓存指标。性能报告标明是否包含缓冲分配、像素转换和编码，避免混用不同口径。

关闭条件：0.10 形成可追溯检查点，任务状态与实际仓库一致；其他工作区或会话的修改不纳入本任务提交。

### P2：统一更新与动画调度

- [x] P2-01：盘点 Images、Scene/List 反馈、白噪音、盆栽和资讯的定时器与刷新代码，定义帧动画、完成投递、业务轮询的职责及所有权。
- [x] P2-02：提供 SDK 层订阅句柄和实例级调度入口；重复启停不重复订阅，替换频率有明确失败行为，销毁自动退订。独立引擎继续由调用方提供时间。
- [x] P2-03：合并同一实例内的重绘请求，只在有可见变化时请求新纹理；持续动画、短暂反馈和后台完成可共存，单次动画结束后提交最终帧并停止帧订阅。
- [x] P2-04：统一隐藏、恢复、降帧和速度变化规则；隐藏停止视觉订阅，恢复重建时间锚点，排除隐藏时间。数据订阅和音频生命周期仍按各自业务规则处理。
- [x] P2-05：将图片完成投递及白噪音接入，随后用资讯或盆栽验证另一类场景，删除已被公共能力替代的定时器管理代码。
- [x] P2-06：增加独立示例和专项检查：多实例、重复暂停恢复、捕获取消、频率替换失败、加载期间销毁、终止帧与静止状态；验证安装消费和源码导出。

关闭条件：至少两类真实使用方接入；每个订阅都有明确所有者；无可见变化时不持续请求绘制；隐藏和销毁不发生过期回调；一个实例的停止不影响其他实例。

### P3：常用组件与组合方式

- [x] P3-01：整理共享主题令牌，统一正常、悬停、按下、选中、禁用、加载和错误状态，明确局部覆盖与继承规则。
- [x] P3-02：补齐真实插件需要的选项切换、标签页、数值指示和折线图，优先组合现有控件；图片组件复用资源系统。
- [x] P3-03：建立 Scene/List 的组合方式，明确视口裁剪、行内操作、稳定业务 ID 与内容更新规则，减少调用方同步多套几何的工作。
- [x] P3-04：通过数据卡片与交互卡片验证尺寸变化、明暗主题、长文本、RTL、实际 DPI 和可用操作区域，更新示例及 SDK 导出。

关闭条件：两类真实插件复用新增组件；视觉与命中使用同一几何；窄尺寸下关键操作仍可使用；组件没有各自的窗口、线程或永久定时器。

### P4：输入、焦点与可访问性

- [x] P4-01：设计宿主输入扩展与版本协商，明确焦点授予、失焦、销毁及旧宿主降级，保留现有 C ABI 合约。
- [x] P4-02：实现焦点顺序和 Tab/方向键/Enter/Esc 操作；隐藏、禁用、删除和移动节点后正确更新焦点。仅在宿主授予焦点后处理相应键盘事件。
- [x] P4-03：统一滚轮消费和传播规则，验证嵌套滚动、边界、拖动捕获与卡片拖动的协调。
- [x] P4-04：为常用组件提供名称、角色、值和状态语义，通过宿主桥接系统可访问接口，验证焦点通知、可操作状态与读屏访问。

关闭条件：常用控件可通过键盘完成主要操作；旧宿主仍可使用基础鼠标交互；无效或被移除的节点不会保留焦点、捕获和可访问对象。

### P5：实例级性能诊断与优化

- [x] P5-01：统一布局、文字测量、绘制、解码、上传、缓存和帧请求的统计口径，区分次数、耗时、驻留字节及峰值；每阶段保留基础指标。
- [x] P5-02：建立固定数据、固定时间、固定尺寸/DPI 的可复现基准，覆盖静止、交互、滚动和持续动画；记录设备与构建配置，确定各场景的预算。
- [x] P5-03：根据测量决定是否实现静态子树缓存，处理内容、主题、DPI、裁剪与资源版本失效，证明收益并限制额外内存。
- [x] P5-04：如需卡片内部脏区域绘制，先确认宿主上传和纹理合成合约，分别验证栅格化、上传、合成的收益；保持现有完整卡片绘制路径可用。

关闭条件：能定位到具体实例的主要开销；优化有可复现前后证据和缓存上限；不通过整屏刷新或超采样改善局部问题；正确性与静止开销不退化。

### P6：开发预览与调试工具

- [x] P6-01：升级现有预览和 CLI，支持尺寸、DPI、主题、语言、模拟数据和固定时间；真实服务与模拟服务有明确入口。
- [x] P6-02：增加节点边界、裁剪、命中区域、焦点及性能指标查看，诊断信息放在开发工具中。
- [x] P6-03：增加资源文件监听、版本失效和热重载；慢扫描放在后台，连续变化合并更新，失败保留清楚的诊断与可恢复状态。
- [x] P6-04：支持 C++ 增量构建、预览进程重启和状态恢复，处理正在加载的 DLL 与构建失败；提供截图导出及带容差的视觉比较。

关闭条件：独立开发者能在预览中完成日常调试；错误可定位到资源或节点；资源更新无需重新编译；原生代码更新后能可靠重启预览。

### P7：现有插件分批迁移

- [x] P7-01：迁移电子沙漏与普通数字时钟，验证基础绘制、实际 DPI 与调度。
- [x] P7-02：迁移系统监控、天气与空气质量，验证图表、资源和数据更新。
- [x] P7-03：迁移音乐/相册、频谱、翻页时钟及其余效率工具，验证高频动画、图片切换、缓存和状态恢复。
- [x] P7-04：迁移锦鲤等复杂装饰的公共底层，保留专有路径、运动、花纹和视觉效果；盘点剩余重复封装及保留原因。

关闭条件：每批都有迁移前后对比与相关专项检查；已有设置、业务行为和已确认效果保持一致；删除被替代的重复基础代码，保留必要的专有绘制扩展入口。

### P8：1.0 交付与维护

- [x] P8-01：审查并稳定公共 API、所有权、线程、资源限制、错误处理、版本协商和兼容性策略。
- [x] P8-02：完善模板、静态/数据/动画三类完整示例及从零教程；所有例子都通过独立 SDK 构建。
- [x] P8-03：验证安装型 SDK、源码包、开发工具、示例插件包、许可和版本/哈希清单；持续检查原生插件依赖与 C ABI 合约。
- [x] P8-04：在本体仓库之外，从空目录完成创建、编译、预览、打包、正式宿主安装、重启和状态恢复，并记录发布与回滚流程。

关闭条件：上述完整开发流程可按文档复现；交付材料齐备；只将已经通过验收的能力写入 1.0 发布说明。发布到远端沿用用户明确授权的范围。

### 执行与交付规则

按 T00 → P2 → P3 → P4 → P5 → P6 → P7 → P8 推进；各阶段通过真实使用方和独立交付验证后形成检查点，再进入下一阶段。若验证失败或性能证据不足，记录具体剩余项，不将阶段标记为完成。

每次交付记录变更目的、涉及接口/插件、验证命令与结果、截图或指标、兼容性限制和下一未完成任务 ID。优先复用现有成熟依赖；新增依赖先审查静态 `/MT` 构建、维护情况、许可和实际收益。编译最多 10 个并行作业；常规修改只运行相关专项验证。


T00 验证记录：0.10 检查点 `20bb0aa`，29 项图片资源检查、10 项 SDK 图片检查、48 项场景检查、396 次生活类渲染、两个插件包校验及安装/源码消费通过。复现命令为 `cmake --build build/cybercanvas --target cybercanvas_image_tests cybercanvas_images_demo --parallel 10`，运行图片测试，再执行 `build/cybercanvas/cybercanvas_images_demo.exe plugins/ambience-suite/assets/noise/rain.png build/image.png 2`。本机样本为 800×560、DPI 2、100 次暖帧，请求约 0.027 ms、平均完整帧约 3.83 ms，解码/上传各 6294152 字节、一次解码、两次布局；包含缓冲分配、绘制、像素转换，排除 PNG 编码，不作为其他设备的性能保证。


P2 职责盘点：图片完成投递使用短暂 33 ms 定时器并在完成后退订；Scene/List/按钮反馈使用短暂视觉帧；白噪音/盆栽仅在有运动或反馈时申请帧，静止时保留必要的音频/生长业务查询；资讯保留 500 ms 后台结果轮询及独立的短暂列表/控件动画。SDK 的 `Update.h` 统一订阅所有权、事务性频率替换、时间锚点与刷新合并，核心 Animation/Scene/List 继续只接收调用方提供的时间。图片完成通知、噪音场景和资讯列表都接到各自实例的刷新入口；实例在渲染开始释放刷新闩锁，隐藏时合并待刷新状态。

P2 验证记录：28 项调度合约、10 项 SDK 图片生命周期、396 次生活类及 288 次资讯渲染通过；四个资讯 DLL 在 NativeWidget 中完成交互、捕获、状态恢复和滚动检查；六个插件包校验/合约通过。安装型 SDK 与白名单源码在独立目录构建消费通过。`cd_sdk_update_demo` 输出 20 个变化帧后订阅归零；诊断材料位于本地 `build/cybercanvas-p2-work`。白噪音 1.0.9、资讯 1.0.5 已接入。


P3 组件约定：`DataWidgets.h` 提供受控 ChoiceGroup、MetricReadout、LineChart、MeterGauge；图片继续使用 Scene.imageResource/Images。ChoiceGroup 的绘制、禁用和命中共用选项边界；正常、悬停、按下沿用 Button 的颜色与反馈，选中使用 accent，禁用统一 0.4 不透明度且不响应输入。加载使用 muted 与省略符，错误使用可局部覆盖的 error 令牌。ThemePatch 未指定的字段沿祖先继承；error 等颜色变化仅重绘，字体和间距变化重排。折线最多 4096 点，非有限值断线，极值归一化避免溢出；几何按数据/边界/工厂缓存。

`sdk/SceneList.h` 组合标题/工具栏和虚拟列表：调用方在 arrange 后分发输入，body 节点的裁剪边界直接作为列表视口；行内操作与稳定 ID 沿用 ListView，数据变更保留滚动锚点。隐藏、禁用、移除 body 后停止其交互；重排取消过期捕获。该对象不创建窗口、线程或定时器，时间与刷新仍由 P2 调度。

P3 验证记录：16 项数据组件检查、48 项 Scene 检查、Scene/List 组合的 RTL/行内操作/滚轮/隐藏/移除/旧宿主鼠标检查通过。资讯四尺寸/类型累计 288 次渲染、四个 DLL 的 NativeWidget 交互检查通过；监控四尺寸 32 次渲染及 32 张参考图导出通过。资讯标签复用 ChoiceGroup，监控圆环复用 MeterGauge 并保留原有字体/基线。独立示例输出八张明暗/窄宽/RTL 卡片；安装型 SDK、C ABI 编译、白名单源码构建与组件运行通过。本地诊断目录为 `build/cybercanvas-p3-work`；复现目标为 `cybercanvas_data_widget_tests`、`cd_sdk_scene_list_tests`、`cd_sdk_scene_list_demo`、`cd_news_tests`、`cd_monitor_tests`。


P4 输入约定：Scene 的 focusGranted 仅由拥有焦点的宿主适配调用，旧宿主自动降级为鼠标。Tab 顺序跟随当前树；移动保留仍有效的焦点，隐藏/禁用/删除即时清除。键盘与辅助操作统一生成 SceneEvent，业务通过 onAction 处理；可访问名称使用 accessibleName，文字与加载/错误状态沿用现有内容。SceneList 以列表为一个焦点单位，提供上下选择、左右行内操作、Enter 激活及可读选择值。滚轮按内到外尝试，边界不消费，部分移动消费整次事件，滚动条捕获期间消费事件。

P4 验证记录：SDK 覆盖授予前拒绝、Tab/Shift-Tab、Enter/Space、滑块方向键及端点、树变化、长名称 UTF-8 边界、失焦/销毁和旧宿主；嵌套列表边界传播与捕获检查通过。Qt 桥接覆盖名称、角色、范围/步长、动作、禁用/隐藏/删除、焦点子对象及过期可访问 ID；另由真实 Windows UI Automation COM 客户端读取原生滑块名称/角色，未以模拟对象代替系统桥接。白噪音两个 DLL 在 NativeWidget 中通过系统可访问音量、键盘音量、辅助播放/暂停及 Enter 恢复；音频测试保持静音，396 次生活类渲染通过。安装型 SDK、C ABI 编译和源码消费通过，证据位于 `build/cybercanvas-p4-work`。专项目标为 cd_sdk_keyboard_tests、cd_sdk_list_tests、cd_native_ui_tests（Windows 平台加 --uia）、cd_living_host_tests；未运行完整 Controller/GPU 矩阵。


P5 统计口径：Canvas.stats 的完整帧时间包含目标准备、绘制命令、EndDraw 软件光栅化及 WIC 通道转换，不含调用方分配输出缓冲与 PNG 编码；pixelCopies 单列转换/拷贝。Scene.paintNanoseconds 仅统计命令提交，layout/measure 为包含下级调用的时间，不能相加当作互斥阶段。图片解码在单独 worker 统计，不含排队等待；槽位/场景的 referencedBytes 是对共享像素的引用，sharedStoreStats 才是解码库总体驻留，不能按实例重复累加。tracked/peak 是已知像素负载及其观察峰值，不含 DirectWrite、WIC 编解码器内部、驱动、调用方输出缓冲或整个进程工作集；各缓存同时报告条目数。

P5 诊断入口：SDK DiagnosticSource 通过可选 dev.diagnostics v1 注册 UI 线程采样，Scene 默认提供 Canvas/Scene 与边界、裁剪、命中包围盒、焦点、名称及资源失败信息，SceneList 补充虚拟行统计。白噪音与资讯补充图片、列表和刷新请求。正式 NativeWidget 宿主与 SDK 工具共用 DiagnosticHost 注册表；宿主只读入口 readNativePluginDiagnostics 不请求新纹理。`cd-sdk diagnostics DIRECTORY OUTPUT.json` 在独立进程生成逐实例报告，64 位计数使用十进制字符串，源码及安装包均导出该接口。

P5 基准：`cybercanvas_benchmark OUTPUT.json --enforce` 使用固定数据、1/60 秒时间、320×240 逻辑尺寸、DPI 1/2/3，每场景预热 20 次、测量 200 次，覆盖静止、拖动交互、10000 行虚拟滚动和持续动画。结构预算要求静止零帧/零请求、无重复布局/前缀构建、每帧最多 11 个可见行；计时预算为 P95 16.7 ms。参考机器/Windows build/MSVC/CRT/配置、完整指标和排除范围保存在 benchmarks/windows-x64-reference.json。这是本机工程预算，不保证其他机器的绝对耗时。

P5 缓存决策：对同一静态卡片比较直接绘制与保存位图，先验证像素完全一致；若直接绘制 P95 超过 8 ms 且缓存至少节省 20%，才考虑新增自动子树缓存。本机三个 DPI 均未触发该门槛，静止场景本就不绘制；位图方案需额外约 0.6–5.5 MB，故本阶段不增加该缓存。已有文字布局、路径和图片上传缓存继续生效，资源探针 100 帧仅解码/上传一次、命中 99 次。

P5 实测优化：诊断发现白噪音六张插画按原图解码，占 37764912 字节；改为按实际控件边界 × DPI、两格 sprite 和 8% 轻微摆动余量选择 64 像素档位后，同一 384×192/DPI 2 场景仅占 1769472 字节。可见图片位图负载从 25176608 降为 1179648 字节，约减少 95%。前后九组响应尺寸/DPI 图已对比，插画内容与布局保留，Fant 缩小时边线更平滑；不改变整卡输出分辨率。白噪音为 1.0.11，资讯为 1.0.6。

P5 宿主传输核对：现有 GpuRenderer::uploadTexture 已针对插件比较前后像素，以 D3D11_BOX 上传变化包围区域；合成继续使用已有卡片四边形与保留帧。C ABI CdFrame 仍要求完整卡片，NativeInstance 在内容变化时才生成新 QImage。当前 CPU 光栅化预算通过，本阶段不新增卡片内部局部栅格协议，也不把已有局部上传当作新的 GPU 收益。引擎基准明确排除宿主上传/合成时间，未重跑完整 GPU 矩阵。


P5 验证记录：12 项核心诊断、13 项 SDK 诊断、图片/调度原有专项检查通过；固定基准 12 场景全部满足结构及计时预算，参考最慢 P95 约 7.3 ms。两个白噪音和四个资讯 DLL/包通过校验，正式 NativeWidget 宿主验证每实例采样不触发绘制及虚拟列表统计；396 次生活类和 288 次资讯渲染通过。安装型 SDK、C ABI 编译与白名单源码构建运行通过。九组前后图片及内存报告、源码/安装消费日志在 build/cybercanvas-p5-work；基准参考与复现程序随库导出。资讯 DLL 自报版本改由 catalog 传入，与包描述保持一致。

P6 开发工具：独立预览支持逻辑尺寸、实际 DPI、明暗主题、BCP47 语言、固定 UTC 时间/偏移、受限 JSON 模拟数据、逐帧输入及独立设置/状态文件；实时服务明确通过正式宿主或 Plugin Studio 使用。可选 host.environment v1 保持 C ABI 1 原结构不变，旧宿主回退系统时间。F1 在保留图像上叠加节点/裁剪/命中/焦点与性能信息，不请求插件重画；Canvas.blit 有单帧 64 MiB 临时上传预算，并与插件统计隔离。

P6 重载与构建：后台 ReadDirectoryChangesW 合并 250 ms 内资源变化，保存状态后销毁/卸载模块，重新载入以失效模块内缓存。解码或加载失败保留上一帧，显示资源路径与错误，修复后恢复。dev-plugin.ps1 使用影子 DLL，增量编译原目录；编译失败保留原进程，资源更新保持 PID，C++ 编译成功通过会话事件握手退出旧进程、保存状态并启动新进程。预览原生状态 2 MiB、整个状态文件 5 MiB；PNG/BMP 使用 WIC，cd-sdk compare 支持通道容差、差异比例和差异图。

P6 验证记录：24 项预览/适配/图片/状态/文件监听专项、8 项环境专项通过；18 组真实 DLL 明暗/DPI 1/2/3/中英 RTL 截图通过，固定时间图逐像素一致。键盘回放、独立进程恢复、损坏图片再修复的两次合并重载均保留计数。安装型 SDK 在含空格路径中完成资源更新、故意编译失败、修复重编译及新进程恢复；正式 NativeWidget 生活类宿主检查通过。安装消费、C ABI C11 编译、白名单源码编译和源码预览合约通过，无 Qt/动态 MSVC 运行库导入。复现使用 sdk/tests/PreviewIntegration.ps1、DevLoopIntegration.ps1、cd_sdk_preview_tests 与 cd_sdk_environment_tests；本地证据在 build/cybercanvas-p6-work。下一任务为 P7-01，尚未将插件迁移或 1.0 正式交付标记完成。

原生绘制扩展：`nativeTextFactory()` 和 `nativeImageFactory()` 返回借用的 COM 接口，只能在 Canvas 所在线程使用；专用字体、字形、曲线等资源必须先于 Canvas 析构。Canvas 独占目标重建、BeginDraw/EndDraw、帧裁剪和 COM 初始化，扩展绘制在活动帧内借用 factory/target/brush，不能自行 Reset 这些所有者。`sdk/Canvas.save` 可在导出时合成预乘背景色，不改变实例显示缓冲。

P7-01 迁移：电子沙漏与普通/电子/模拟时钟使用公共 Canvas 帧与目标，删除各自 COM、WIC 目标和逐帧通道转换封装；保留专有沙粒/数字/表盘与文字测量。定时器切换为 SDK Timer 的事务性 RAII 所有权，时钟绑定 Environment；固定时间、UTC/本地偏移、隐藏恢复通过实际像素验证，没有可见时间变化时零重画请求。

迁移时发现 WIC PBGRA→PRGBA 格式转换会在部分半透明像素中损失一级色阶，已改为原样拷贝与单纯交换红蓝通道，并增加精确预乘透明度回归检查。沙漏 192 组原始帧逐字节一致；144 组沙漏、288 组时钟、电子字及翻页已有专项通过，16 张不透明时钟对照图逐像素一致。新旧 PNG 对透明边缘的直通 alpha 编码舍入最多差两级，实际帧不依赖此导出过程。核心帧/图片/诊断与固定数据时间基准通过，静止零请求、图片一次解码上传、最慢 P95 仍低于 16.7 ms。证据在 build/cybercanvas-p7-work；后续仍需 P7-02—P7-04 及 P8。

P7-01 收口：14 个时钟和 2 个沙漏真实 DLL 的 CLI 合约检查通过；新增 PNG 透明、半透明、不透明背景测试，验证预乘合成、源帧与行填充不变以及无效缓冲拒绝。P7-01 已完成，下一项为 P7-02。

P7-02 迁移：系统监控、天气、空气质量共 10 个布局复用 Canvas 目标、实际 DPI、灰度文字抗锯齿、帧作用域、预乘读回与 PNG 导出。保留专有字体测量、仪表、渐变、布局与数据服务；天气和空气质量的 256 像素原始图标改由有界 ImageStore 后台解码、Canvas 缓存上传，完成只请求当前插件重绘，隐藏暂停、恢复交付、静止零计时器/零资源重画均有专项验证。离线 renderReference 仅供工具等待图片，生产实例始终非阻塞 render。移除重复暂停注销，数据订阅与恢复时序保留原有行为。

P7-02 验证：监控 32 组设计导图与数据/订阅专项通过；天气及空气质量的定位、解析、缓存、离线、取消和尺寸测试及新增资源调度检查通过。32+9+12 张前后 PNG 对照布局与内容保留：监控/空气最大通道差 1；天气只有 3 张各 2 个图标边缘像素超过容差 2（最大 12，公共图片边界裁剪），其余像素在该容差内。4 个监控 CLI 生命周期、6 个天气/空气包完整性、正式 NativeWidget 六插件模拟数据、暂停、设置与持久化检查通过，证据在 build/cybercanvas-p7-work。

待 P8 工具审查修复：cd-sdk test 把第一项设置独立改动假定为有效，天气 auto_location 关闭同时 city 为空时会被插件正确拒绝，误报 runtime.settings。生产插件的领域校验必须保留，不能为通用探针放宽；工具应验证拒绝原子性并寻找可接受的合法变化。

P7-03 迁移：16 个音乐/相册/频谱/日历/输入统计、14 个效率工具、3 个翻页时钟布局接入公共主画布，删除各套 COM、主 WIC 目标、整帧通道转换和 PNG 编码。音乐与效率实例改用 SDK Timer 的事务性替换、隐藏停止和析构释放，播放中 33 ms / 暂停 1000 ms 的调度与进度、唱片旋转保持原有行为。图片仍在 Album/Player 后台获取，绘制中不新增文件扫描。

专有缓存保留：相册和唱片最多两个目标相关位图维持裁剪、透明渐变和旋转，Album 后台解码保留现有 EXIF 方向纠正及最多 1024 边长，Player 接收宿主内存封面；点阵静态底板和发光 sprite、翻页最多 32 个数字面与透视像素采样保留，DPI/尺寸变化重建。它们借用公共 Canvas 工厂并持有独立的离屏目标，不能替换/结束公共主目标；公共引擎统计不包含这些专有缓存的驱动内部内存。

P7-03 验证：144 组尺寸/DPI 与音乐静音、后台照片、媒体控制、输入计数、实例隔离/恢复通过；174 项效率检查通过；21 组翻页尺寸/DPI、字体、跨分钟动画、计时与暂停检查通过。48+9+42+42 张前后实际导图对照：播放器和翻页逐像素一致，其他 PNG 最大通道差 1（仅导出量化），没有超过容差 2 的像素。构建与证据在 build/cybercanvas-p7-work。

P7-04 迁移：锦鲤池的主目标、COM、工厂、实际 DPI、抗锯齿、帧作用域、预乘读回与 PNG 统一使用 Canvas，动画计时切换到 SDK Timer 的事务性所有权。鱼鳍/尾鳍、身体裁剪花纹、固定 2×4 鱼尺寸、随机开场、完全游出后重入、鼠标惊逃/跟随、数量、透明背景、昼夜、荷叶荷花缓动及微软雅黑粗体 50 个祝福词均保持原有设置与实现；默认 motion_speed 0.75 不变。

锦鲤的池面缓存和文字缓存继续分开：60 Hz 祝福合成不重算池面，主体仍按设定帧率更新。离屏目标使用局部 COM 别名，切换缓存绘制不会替换公共 Canvas 主目标。26 张前后导出图最大通道差 1；32 组 DPI/昼夜及鱼形、皮肤裁剪、边界、交互、暂停、失败回滚专项通过，三种尺寸真实 DLL 生命周期及包校验通过。640×320 主体均值约 11.7 ms、单独祝福合成约 0.55 ms，仅为本机参考，不宣称跨机器性能收益。

剩余原生实现盘点：

| 保留项 | 原因与边界 |
| --- | --- |
| widget-suite/Album.cpp 后台扫描与 EXIF WIC 解码 | 目录顺序、取消/代际、旋转照片与多格式支持属于相册行为；解码限 1024 边长、扫描 50000 条/深度 4，绘制线程不执行扫描 |
| 相册/唱片两个位图、MatrixViews.inl 离屏底板和发光 sprite | 保留双图切换/旋转/点阵效果，尺寸/DPI 变化失效，工厂借用主 Canvas；属于专有目标相关缓存 |
| clock-suite/FlipRenderer.cpp 离屏数字面 | 最多 32 个缓存，保持外部字体、字面分割和透视像素采样，主帧已统一 |
| koi-suite/Renderer.cpp 池面/祝福离屏目标 | 让流畅文字合成避免每帧重画池面；保留身体裁剪和叠层资源，主帧已统一 |
| productivity/news 的后台 COM 初始化 | 属于后台系统/网络/目录任务生命周期，不是重复的绘制后端 |
| sdk/examples/Drawing.h 旧 Win32 示例 | 作为 C ABI 兼容示例保留，独立编译不依赖 Qt；新制作方式使用 Canvas、Scene、SDK Timer 和新原生模板 |

P7 已收口，下一阶段为 P8-01 API 审查与 1.0 交付；仍需关闭已记录的通用设置探针问题及完整外部目录验收，不能据此标记 1.0 已发布。

P8-01 API 审查：稳定 CyberCanvas 1.0.0 / SDK 1.2.0 的源接口、C ABI 1/包格式 1 的兼容策略、创建线程归属、借用/拥有对象、数据/图片回调边界、实例失效、资源上限与错误处理，详见 SDK docs/CONTRACTS.md。C++ 头和静态库必须来自同一开发包，旧 DLL 的 C ABI 前缀不变。SDK CMake/生成 Version.h/CLI 版本统一来自 release.json。

审查修复了三项真实问题：最终布局完全裁剪或零尺寸的控件不再保留/复活焦点；通用设置探针允许合法的字段依赖拒绝，并检查全字段拒绝原子性及成功后的实例隔离；预览/生命周期优先读取当前 textproto，损坏编辑不能悄悄回退旧二进制描述。新增对应回归检查，C11 ABI 编译、28 项键盘/旧宿主、Scene/控件/诊断及 24 项预览检查通过。新的通用工具对全部 62 个已迁移插件 DLL 的生命周期/设置/绘制/暂停/状态检查通过，关闭 P7 留下的天气误报。1.0 交付包和仓库外验收仍待 P8-02—04。

P8-02：static/data/animated 三个 C++ 模板与 new-plugin.ps1、STARTERS.md 已完成，使用公共 CMake helper、系统字体、SDK/Yoga/tweeny 许可。安装型 SDK 独立创建/编译、三个 DLL 生命周期和包校验、18 张明暗/DPI 1/2/3 截图通过；静态点击计数跨进程恢复，动画前进与暂停后跨进程静止通过。复现入口 sdk/scripts/verify-starters.ps1，证据 build/cybercanvas-p8-work/starter-verification。剩余 P8-03/04。

P8-03 本地交付：CyberCanvas 1.0.0 / SDK 1.2.0 的 Windows x64 安装 ZIP、179 个清单源码文件的白名单源码 ZIP、七个完整示例与三个模板的十个 .cdpkg 已生成。安装包 132 个清单文件，逐文件大小/SHA-256 与 ZIP 实际内容核对通过；实际解包的十个插件均包含 SDK/Yoga/tweeny 原始许可且无链接/调试产物。13 个原生文件检查无 Qt、动态 MSVC/UCRT、protobuf/Abseil 导入。CI 配置上限改为 10，未执行远端工作流。

安装与解压源码两个构建分别通过 15 组 SDK 合约、12 组 Canvas 专项和固定数据/时间/实际 DPI 基准，静止零帧/零请求及 16.7 ms 预算全部通过；完整帧计时不含宿主 GPU 上传/合成。本地材料在 build/cybercanvas-1.0-delivery/artifacts，含 release-report.json、delivery-audit.json、两个基准和 SHA256SUMS.txt。发布/回滚流程见 sdk/docs/RELEASING.md。最终源码归档仅刷新了已验证预览快捷键的 README，编译源码与已独立构建的导出保持一致。

P8-04 仓库外验收：将已审计的最终 Windows ZIP 解压到系统 Temp 的全新目录，从空的 First Card 工程完成脚手架、MSVC /MT 编译、生命周期、六张明暗/DPI 截图、打包和验证。仅使用解压 SDK 前缀，无本体头文件。实际 PluginPackage 安装和 NativeWidget 在两个不同进程中保存/恢复业务与设置，实际鼠标使计数从 0→1，重启恢复 1 后继续到 2；微软雅黑、简体中文、0.8 不透明度和重新编码后的画面精确一致。无效替换保留原 DLL，关闭实例后重新安装通过。

复现入口 scripts/verify-canvas-sdk.ps1（私有宿主构建树可用 -QtDirectory 指定 Qt 运行库），验收目标 cd_canvas_sdk_acceptance_tests 不属于默认构建/公共 SDK。持久记录在 docs/verification/cybercanvas-1.0-acceptance.json；完整日志、状态和截图位于本地仓库外 cybercanvas-1.0-external-20261017，正式用户配置未修改。T00、P2—P8 共 32 项已全部完成，本次仅本地交付；没有 push、打标签或在线发布。
