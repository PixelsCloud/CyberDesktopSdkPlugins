# 原生组件的公共输出与缓存

公共 C ABI 保持版本 1。`render.output`、`render.memory`、`render.images` 为可选 v1 服务；旧宿主缺失新服务、表过短或协商失败时，新 SDK 使用原有 CPU 输出。旧插件的 `render.canvas.gpu` 服务仍由宿主兼容提供。

## 主输出

创建主绘制对象后调用 `canvas.bindGpu(context)`。`frame()/render()` 和 `begin()/end()` 使用相同会话选择规则。宿主只为当前主帧的像素地址、尺寸、步长和 DPI 提供 GPU 目标；临时帧、CPU 捕获及内层画布不取得该目标。

GPU 主输出的 ABI scratch 仅为兼容旧帧结构，不能作为本次绘制结果读取。普通 GPU 帧不回读，不做 RGBA/BGRA 转换。目标仅可在本次回调内绘制，目标相关缓存由 Canvas 在资源域变化时释放。

画布结束后报告实际结果；宿主不会仅因旧纹理存在就认可一次未绘制的输出。`CdRenderReport` 区分已绘制、内容未变、等待资源、需要 CPU、失败。输出分配代次、内容版本与布局版本相互独立。

## 内容与 CPU 捕获

`cdsdk::FrameCache` 适用于此前自行保存整帧 RGBA 的控制器：

```cpp
cache.bind(context);
return cache.render(frame, contentRevision, [&](const CdFrame& output) {
  return renderer.render(output) ? CD_OK : CD_ERROR_INTERNAL;
});
```

内容版本应覆盖数据、样式、字体、语言和图片依赖变化。缓存键另外包含后端输出代次、物理尺寸与 DPI。GPU 分支复用已绘制纹理；CPU 分支按需生成、保存及复制真实像素。普通绘制、截图和后端切换不发送假的布局事件，也不借此取消输入捕获或重置物理状态。

## 内层内容

复用另一个绘制器时，优先使用 `producer.drawIn(parent, area, paint)`。它在调用者的活跃画布中裁剪、定位并恢复状态，不开始、清空或结束主画面。例如 LCD 内部数字可直接绘制，免去 CPU 子帧与重新上传。

真正需要 CPU 内容的图片、视频和矢量源仍可使用 `blit()` 等入口；必要上传与整件组件回退分别计量。

## 图集、采样和切片

`SpriteList` 保留提交顺序、变换和透明度，`SpriteAtlas` 缓存小型材质，`SpritePalette` 合并已有材质帧。`PrimitiveSprites` 把重复矢量材质按实际显示密度缓存到小图集，材质命中时只提交实例。批次只合并同一图集、采样和合法绘制顺序内的实例。裁剪或透明层顺序不兼容时保留原有提交。

`Canvas::sprites()` 明确接受 `ImageSampling::Nearest` 或 `Smooth`；GPU 与 WIC 路径都支持实际源像素、透明度及变换。Pixels 使用最近邻；文字、功能 SVG 与真实媒体内容保持各自采样规则。引擎批次数不代表驱动最终 draw call 数。

`FrameSlices.h` 负责固定片段与连接区的映射。具体切分数据属于皮肤；命中区域使用相同映射。人物、表盘和固定装饰不随连接区拉伸。

## 资源预算与异步恢复

宿主以 `MemoryBudget` 提供共享分配预算，经 C 服务返回 `IUnknown` 租约。插件只持有和释放 COM 引用，不跨 `/MT` DLL 共享 C++ 容器、内存管理器或 `shared_ptr`。

解码图片、GPU 输出、离屏表面和整帧缓存的租约跟随实际资源。`OffscreenSurface::snapshot()` 返回 `SurfaceImage`，缓存它而不是从临时表面复制裸 bitmap 引用；CPU 图像也因此保持记账。GPU 纹理另附租约，即使外部继续持有纹理也不会提前减账。

`ImageStore` 保留既有条目、ROI、实际尺寸/DPI 解码和淘汰约束，并接入宿主总预算。预算不足先回收本地无使用者的图片；仍不足则记录具体失败，SDK 按退避重试。`Images::retry()` 可主动重试失败资源，同一失败请求的多个使用者一起恢复。资源状态变化继续触发局部失效。

`render.images` 汇总每个组件实际请求的就绪、等待和失败图片层，避免只检查外壳。宿主诊断另记录 GPU/CPU 帧、复制字节、精灵批次、缓存命中与共享资源占用；系统/驱动缓存不冒充自有资源记账。

## 验证

仓库入口为 `scripts/verify-rendering-unification.ps1`，按 Contracts、Coverage、Special、Benchmark、Stability 选择阶段。Special 保留 Live2D/聊天的实际宿主专项，并先准备着色器缓存。编译允许 20 核/20 作业；前后渲染采样保留原基线的 10 核亲和性。测试与报告固定放在 `build/relwithdebinfo/Testing/rendering-unification`，不进入发布目录。

公共契约覆盖硬件/WARP、主帧与嵌套帧、CPU 捕获、输出版本、状态恢复和租约。主题专项继续检查真实功能、最近邻、字体、图片层、占格和物理效果。最终发布仍使用统一构建脚本与 `build/dist`。
