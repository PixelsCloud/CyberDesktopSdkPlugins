节拍弹珠台 6×2 / 4×2 — Beat Machine 1.1.0

提供宽版 6×2 和紧凑版 4×2 两个独立控件。紧凑版重新安排撞锤、拨片、碰撞灯与轨道，
两种机壳分别制作，包内仅附对应尺寸；钢珠保持相同直径与 24/36/48 数量选项。
4×2 为 4 列 2 行；两种尺寸均支持系统混音与手动交互，各自独立保存设置。
左上角不显示 BEAT MACHINE 标题或铭牌，机壳保持连续的金属边框。
整机在宿主安全区内保留短边 4.5% 的留白，保持机械结构比例居中；未占用区域透明。
不另加铺满砖块的矩形面板或 SYSTEM AUDIO 底牌，圆形螺丝、钢珠和灯座保持圆形。
发射器使用较平缓的远投路线，避开右侧撞击灯；撞锤下方慢速输送，斜坡再加速回流。
轨道牵引力有上限，仅靠近工作轨道时施加；静音停止，不在空中转向或重置钢珠。

系统混音驱动的机械弹珠装置：低音击锤、中频拨片、真实碰撞短闪和右侧发射器。
四个主活塞从左到右分别响应 30–60、60–95、95–140、140–220 Hz。
右下喷射活塞响应更宽的 25–280 Hz，采用更低的起音门槛及更短的触发间隔。
五个活塞均由低频能量的上升沿触发，短促抬升后回位；持续低音不会把活塞一直顶住。
检测的是低频起音，并非鼓乐音源分离，低音合成器等同频段声音也能触发。
复用宿主 30 FPS 共享频谱；活动时以约 60 FPS 更新局部物理画面，静止后停止定时刷新。
使用宿主共享音频频谱，不播放自己的音乐，不是独立播放器的专属音轨。
默认 36 颗钢珠，可设 24/36/48；旧版 12/16 球实例自动升级为 36 球。
降低重力和球体密度，撞锤下方使用封闭碰撞体防止钻入；每颗球使用 Box2D 碰撞。
倾斜发射器在真实接触时释放一次有限弹射冲量，将钢珠投回左侧工作区；不在空中改轨或瞬移。
频谱兼顾峰值与平均能量，使用相对门限，适配真实音乐的稀疏低频。点击机构、拖动钢珠可手动互动。
静音停止施力并自然停稳。隐藏时释放音频订阅，恢复不补播旧节拍。
设备不可用时提示状态，仍可手动互动。减少动态与运行开关冻结物理画面。
机壳、活塞帽、拨片、灯座和镂空黄铜座使用独立 PNG 层，弹簧和轨道按实际 DPI 绘制。
钢珠材质复用本项目机械时钟；各部件按可见轮廓定位，拨杆绕真实支点转动。
机壳按实际物理像素宽度分档解码，静态缓存只在布局、主题或素材变化时更新。
源代码：plugins/beat-machine；专项入口：scripts/build-beat-machine.ps1 -Jobs 10。
构建输出为中间产物；正式发布统一使用 scripts/build-all.ps1 和 build/dist。

System output mix drives four bass hammers, two midrange paddles and a spring launcher.
Main pistons use 30–60, 60–95, 95–140 and 140–220 Hz; the more sensitive launcher uses 25–280 Hz.
Low-frequency attacks trigger short strokes, with fast return instead of a sustained bass hold.
The complete mechanism keeps its aspect ratio, with transparent space around the shaped chassis.
Bumper rings react to physical hits. No synthetic beat is mixed into live input.
Click mechanisms or drag a ball. Hide releases subscriptions; resume excludes hidden elapsed time.
Default 36 marbles, with 24/36/48 options; legacy 12/16 counts upgrade to 36.
Lighter gravity, bounded impulses and closed plungers keep the playfield circulating.
Theme, backplate, sensitivity, ball count, running and reduced motion are per instance.
Engine/Box2D licenses and generated-material provenance are included in the package.
