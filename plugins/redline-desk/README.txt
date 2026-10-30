RedLine · 赤色协议

45 个独立原生布局规格，使用 RedLine 标签筛选。尺寸统一为宽×高。
固定造型使用批准的黑灰金属、冷银与朱红素材；内容、按钮、文字与运动零件由原生组件实时绘制。
图片使用实际 DPI 与可见区域异步解码、共享缓存，四边至少 6px，更新只请求组件局部重绘。
天气、空气、系统指标、媒体、相册、输入统计、农历、日历与计时逻辑复用现有控制器。
4×2 擒纵摆锤与 6×2 双风扇由 Box2D 驱动；双风扇分别每 18 秒、22 秒缓慢反向旋转，读数使用系统时间。
两款节拍组件使用短行程击锤、弹性鼓面和踩镲接触，不包含弹珠轨道。
设计原图位于本地 design/redline-collection，不纳入 Git。发布资源独立保存在各插件 assets 内。
构建复用 build/relwithdebinfo，最多 10 核、10 个并行作业；专项证据位于 Testing/redline-desk。

构建与专项验证：scripts/build-redline.ps1 -Jobs 10；仅编译加 -CompileOnly。
CDPKG 输出：build/relwithdebinfo/redline-packages。作为内置主题运行时，需要主 EXE 同级的 cyberdesktop_redline_runtime.dll。
正式发布由 scripts/build-all.ps1 统一合并至 build/dist。
素材通过内置 imagegen 生成，来源与提示词在 materials/import.json 和 materials/clean-dials.prompt.txt；assets 内保存完整原生 PNG，构建不依赖生成服务。
物理实现边界：擒纵轮按实际中点事件步进；双风扇为限扭矩电机驱动的刚体；鼓面为短行程弹性刚体代理。
节拍直接读取系统音频低、中、高频起音，复用 AudioMotion 检测器，碰撞后回弹；静音不生成假节拍。
4×2 播放器的唱片随实际播放状态旋转，暂停或减少动画时停住；唱臂与外壳保持固定。
翻牌招牌的拉丁字母统一显示大写，保留用户原始输入和非拉丁文字。
4×2 天气左侧插画随当前天气和昼夜切换；不把雨云或雨滴烘焙在固定外壳中。
2×2 磁盘显示已用、总容量、可用空间及分段容量条，不使用表盘。
镂空精密时钟的七个可见齿轮按各自轴心和齿数旋转，桥板、刻度及指针独立绘制。
天气干净外壳由内置 imagegen 编辑生成，提示词见 materials/weather-clean-shell.prompt.txt；天气图标复用 Meteocons，许可随插件发布。
