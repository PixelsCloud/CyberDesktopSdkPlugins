# 插件开发快速开始

需要键鼠、媒体、频谱、天气或 AQI 数据时，从 [公共数据服务指南](DATA_SERVICES.zh-CN.md) 和三个数据服务示例开始；采集和凭据由本体统一管理。

SDK 1.3.0 面向 Windows 10/11 x64。插件默认 /MT，只需要 Visual Studio C++ 桌面开发组件、Windows SDK、CMake 3.26+、Ninja。模板编译不需要 Qt、protobuf 或本体源码。

先从三种完整工程中选择：`static` 是无需定时器的静态卡片和点击计数，`data` 使用宿主系统遥测，`animated` 提供可调速、隐藏停止和版本化状态的动画。下面的脚本会同步改好三个文件中的 ID、验证描述，并保留现有目录内容：

```powershell
$sdk = 'D:/CyberDesktopSDK'
& "$sdk/share/cyberdesktop-sdk/scripts/new-plugin.ps1" `
  -Destination 'D:/My Plugins/First Card' -Id 'com.example.first-card' `
  -Name '我的第一张卡片' -Template static
```

目标目录须不存在或为空。把 `-Template` 换为 `data` 或 `animated` 即可切换工程。创建后脚本会打印对应的编译、检查和预览命令；输出目录为 `build/plugins/你的插件ID`。完整制作、预览和打包步骤见 [原生起步教程](STARTERS.md)。也可以手动复制模板：

解压开发包到例如 `D:/CyberDesktopSDK`，把 `share/cyberdesktop-sdk/templates/minimal` 复制到自己的工程。在 VS x64 开发终端执行：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/CyberDesktopSDK
cmake --build build --parallel 10
D:/CyberDesktopSDK/bin/cd-sdk.exe test build/plugins/com.example.hello
D:/CyberDesktopSDK/bin/cd-sdk-preview.exe build/plugins/com.example.hello
D:/CyberDesktopSDK/bin/cd-sdk.exe pack build/plugins/com.example.hello hello.cdpkg
D:/CyberDesktopSDK/bin/cd-sdk.exe validate hello.cdpkg
```

修改 `Plugin.cpp`、`CMakeLists.txt` 与 `plugin.textproto` 中的 ID，C++ 与描述中的版本也要保持一致。`plugin.textproto` 是编辑文件，每次构建生成 `descriptor.pb`；有 textproto 的目录打包时以它为准，避免旧二进制描述覆盖修改。

DLL、图标、图片、字体、配置等资源都放在 `build/plugins/com.example.hello` 内。私有依赖 DLL 放包根目录；资源可用子目录。静态、延迟加载依赖自动检查，代码里 `LoadLibrary` 等动态加载的私有 DLL 需额外在 descriptor 的 `dependencies` 声明为 EMBEDDED。不同插件的私有 DLL 使用带插件前缀的名称，以免 Windows 模块名称冲突。Windows 自带 DLL 不入包；MSVC 可再发行运行库不是系统自带依赖，采用 /MT 可以避免依赖它们。

发布只交付一个 `.cdpkg`，无需在旁边附 manifest、DLL 或 assets 目录。在 CyberDesktop 设置中导入；更新前停用插件或退出桌面，仍被加载的 DLL 不会被覆盖。用户实例状态由宿主单独保存，插件包只保存只读资源。

预览按 P 暂停、L 切换布局、S 打开实例设置页；可指定 `--layout ID`、`--dpr 2`、`--screenshot preview.bmp`。预览使用 mock CPU/GPU/音频数据，材质和桌面集成使用官方 Studio。原生插件是本机代码，校验工具检查包完整性，不验证发布者身份，也不构成执行沙箱。

在已安装 SDK 上构建全部参考示例：

```powershell
cmake -S D:/CyberDesktopSDK/share/cyberdesktop-sdk/examples -B examples-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/CyberDesktopSDK
cmake --build examples-build --parallel 10
```

示例输出在 `examples-build/spectrum`、`hexclock`、`input-stats`、`data-dashboard`、`media-spectrum` 和 `examples-build/plugins/org.cyberdesktop.sdk.workbench`。接口详见 [API](API.md)，包规范见 [PACKAGE_FORMAT](PACKAGE_FORMAT.md)。当前 ABI、架构或包版本不支持时必须重新编译；不提供旧 Qt SDK 或 ZIP 包兼容。

每个插件必须声明并实现设置页。模板与五个示例已包含可实际生效的独立配置，参考 Settings.h 和 get_setting/apply_settings。完整设置规则见 [API](API.md)。

绘制时按宿主 CdFrame 的物理像素尺寸和 device_pixel_ratio 建立目标。Direct2D 可包含 <sdk/WindowsRendering.h> 并调用 cdsdk::configureAntialiasing(target)，开启图形抗锯齿和透明文字所需的灰度抗锯齿。图像缩放使用线性或更高质量插值；刷新只请求当前插件内容区，不能触发整个桌面重绘。

新的 C++ 插件可直接使用 `<sdk/Canvas.h>`。它适配独立的 CyberCanvas 绘制引擎，统一目标创建、DPI、透明输出、缓存和异常清理：

```cpp
// Keep one canvas per widget instance on its rendering thread.
cdsdk::Canvas canvas;
canvas.render(*frame, layout.safe_content, [&](auto& draw, auto area) {
  draw.box(0, 0, area.width, area.height, 16, 0x202B3CFF);
  draw.text(L"Hello", 18, {16, 12, area.width - 32, 32}, 0xFFFFFFFF, false, true);
});
```

`Canvas::render()` 可能抛出异常；在插件 C ABI 的 render 回调中捕获并返回相应 `CdResult`，不要让 C++ 异常越过 ABI。也可使用 `auto painting = canvas.frame(*frame, safe)`，完成后调用 `painting.finish()`；提前返回和异常都会自动取消未完成帧。`clipped(rect, callback)` 提供嵌套裁剪。

CyberCanvas 本身通过 `CyberCanvas::CyberCanvas` CMake target 使用，不依赖 SDK 或 Qt。SDK 安装包包含这个引擎的头文件和 CMake 配置。现有 C ABI 保持版本 1；仍然需要编译插件。

CyberCanvas 0.2 增加保留路径、贝塞尔曲线、描边、`saved()`、`translate/rotate/scale`、整体 `withOpacity()`，以及 `measureText()`、`inset/aligned/Stack/divide` 基础布局。完整示例与限制见安装后的 `share/cybercanvas/README.md`。使用 `draw.text(value, TextStyle, CdRect, color)` 时，内容原样绘制，与 `measureText()` 保持一致；传统的字号参数重载会通过 `displayText()` 翻译 SDK 界面词条。

CyberCanvas 0.3 提供 `<CyberCanvas/Animation.h>`：宿主时钟、缓动、暂停恢复、速度倍率、单次/循环/往返和 `Tween<Pose>`。在实例中保留时钟和轨道，用宿主 timer 的单调毫秒时间推进；render 只采样，不推进时间。隐藏时暂停并取消画面 timer，恢复后以新锚点继续；结束时画出最终帧再停止请求重绘。`Canvas::posed(pose, callback)` 对内容整体应用动画。引擎不自行创建线程、窗口或定时器；只调用当前实例的 `request_redraw`。Tweeny 的许可随安装包位于 `share/cybercanvas/third_party/tweeny/LICENSE`，分发使用动画模块的插件时也应附带该许可。

鼠标控件使用 `sdk/Interaction.h`：创建实例时 `interaction.bind(*context)`，布局时注册唯一 ID 的命中区域，输入回调调用 `interaction.input(*event)`。普通按钮读取 `clicked`；滑块使用 `PointerBehavior::Drag` 并处理 Begin/Update/End。暂停、换页、重排和销毁前调用 `interaction.cancel()`。适配层自动申请和释放可选宿主捕获，拖动滑块不会移动卡片；旧宿主保留点击设置数值。坐标规则和示例见 CyberCanvas README。

常用控件见 `CyberCanvas/Widgets.h`，包括按钮、图标按钮、开关、滑块和进度条；同一描述对象生成命中区域并绘制。`WidgetState` 统一维护短暂反馈。图标用 `sdk/Icons.h` 绑定宿主，将 `icons.painter()` 传给图标按钮；插件无需链接 Qt，旧宿主会显示提供的文字标签。独立示例为 CyberCanvas 的 `examples/Widgets.cpp`。

0.6 的 `CyberCanvas/Flex.h` 提供基于静态 Yoga 的自动布局：伸缩、换行、间距、最小/最大尺寸、百分比与 RTL。`CyberCanvas/Components.h` 提供图标加标签、标题加操作按钮、带标签的滑块，并接入 DirectWrite 文字测量。SDK target 已链接 `CyberCanvas::Layout`；安装包包含静态库，使用者无需另外安装 Yoga。布局树可复用，文字改变后标记测量节点失效；重排前取消输入捕获，绘制和命中使用同一份布局。源码构建需要 vcpkg 的 Yoga 3.2.1，插件分发附带安装包中的 `share/cybercanvas/third_party/yoga/LICENSE`。

0.7 的 `sdk/Scene.h` 将这些能力组合为可保留的组件树。实例创建时 `scene.bind(*context)`，添加 Row/Column/Text/Image/Button/Slider/Toggle 节点，之后只通过 setter 同步状态。`arrange(canvas, bounds, dpr)` 排版，`draw(canvas, icons.painter())` 绘制；`input(event, {safe.x, safe.y})` 返回业务 key 和可选的新值。SDK 自动申请捕获、合并当前实例的重绘请求。主题逐级继承，值和颜色只标记重绘，文字、字体、尺寸改变才重新布局。宿主继续负责计时，在 `active()` 时推进反馈，隐藏时 `cancel()`。完整的独立示例见 `share/cybercanvas/README.md` 与源码 `examples/Scene.cpp`。

0.8 的 `sdk/List.h` 提供 `ListView`：添加稳定 ID、行高、内容版本，设定视口，使用绘制回调呈现可见行。`update(id, revision)` 更新单条数据，屏幕外版本变化不请求刷新。绑定宿主后自动处理滚轮单位、滚动条捕获和当前卡片刷新；输入传入安全区偏移。在 `active()` 时推进 `advance(seconds)`，隐藏时 `cancel()`。加载、空内容、失败重试通过 `ListStatus` 统一呈现。资讯插件已支持滚动/分页切换，C ABI 仍为 1。

0.9 支持动态组件树：`scene.remove(node)` 删除子树，`clear(parent)` 保留容器并清空其子节点，`move(node, parent, before)` 在目标子节点前移动整棵子树，省略 before 则放在末尾。剩余节点句柄、业务 key 与控件值保持不变；继承主题、绘制和命中顺序使用新结构。结构改变会取消宿主捕获并请求当前卡片重绘，同一位置的移动保持静止。删除后的句柄不复用，业务 key 可复用；4096 上限只计算存活节点。通过 `contains/parent/children/size` 查询结构，修改后先 `arrange()` 再处理输入与绘制。独立示例 `examples/Dynamic.cpp` 可导出前后对比图。

0.10 的 `<sdk/Images.h>` 提供后台本地图片资源：实例创建时绑定宿主，`set(slot, path, revision, maximumEdge)` 发起加载，绘制时把 `get(slot)` 交给 `scene.imageResource(node, snapshot)`。`onChange()` 接入场景失效；没有回调时默认请求当前实例重绘。加载结束会释放临时完成定时器，隐藏时 `pause(true)`、恢复时 `pause(false)`，无宿主预览显式 `poll()`。同一模块内可共享 store；复制句柄/快照会保留像素，释放后可回收。文件改变后递增 revision 重新加载；加载失败使用占位，不会在绘制时打开文件。完整限制和生命周期见引擎指南。


`<sdk/Update.h>` 统一实例更新：`Timer` 管理定时器的创建、事务性替换和销毁；失败保留旧订阅。`RedrawRequests` 合并刷新，渲染开始时 `beginFrame()`，隐藏时 `pause(true)`。`UpdateLoop` 的 `onUpdate` 返回 `{changed, active}`，有变化才刷新，结束时提交最终帧并退订；暂停排除隐藏时间，速度 0 停止视觉定时器，恢复速度重新建立锚点。多个刷新来源应接到同一个实例刷新入口，业务数据/音频生命周期单独管理。完整独立示例为 `examples/Update.cpp`。

原生开发预览现在支持尺寸/DPI/主题/语言、固定时间、模拟数据、命中与焦点边界、资源重载和截图比较。完整命令与增量编译流程见 [开发预览指南](PREVIEW.md)；参考 `examples/Workbench.cpp` 中的公共 Canvas、Scene、后台图片、定时器及 `PluginAdapter<T>`。
