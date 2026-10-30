CyberDesktop · 吸血鬼.Alice

45 个独立组件规格。标签“吸血鬼”和“Alice”独立存储，组合展示为“吸血鬼.Alice”。
设计来源：design/vampire-muse-suite/index.html，人物及外框使用批准的原始 PNG 素材。
采用五款最新自然融合构图，11 个含人物布局共用成年角色 Alice。
四边各至少 6px 的逻辑留白，按实际 DPI 绘制；所有物件等比显示。

动态模型和数据：复用当前组件的设置、环境、媒体、相册、天气、空气质量、系统信息、
输入统计、农历、倒计时及 Box2D 实现。控制器适配来源见 controller-sources.json。
外框与人物是独立静态层，读数、按钮、关节、弹簧、钢珠与频谱由真实数据及模型驱动。
图标使用 SDK 公共 Lucide 服务；图片由 SDK Images 后台解码。

小型 plugin.dll 各有独立不可变 ID；共同依赖应用提供的 cyberdesktop_alice_runtime.dll，
只发布一份共享运行时代码，避免将全部模型与数据依赖重复链接进 45 个模块。
所有 C++ 模块保持 Windows x64 /MT。第三方依赖沿用现有静态库与许可。

开发与验证：scripts/build-alice.ps1 -Jobs 10。
外观迭代：加 -PreviewOnly，仅覆盖固定 Testing/vampire-alice 中的 45 张原生预览，不发布。
单个规格专项验证：加 -Only air-4x2，只测试该规格的 15 种尺寸/DPI 组合，不编译主程序或发布。
正常构建验证 675 次 DPI 渲染、45 个 SDK 包及生命周期、分类搜索后，增量发布 build/dist。
发布前核对固定 Testing/vampire-alice/visual-review.json 的全部原生预览指纹，避免发布未校对的外观。
文字按实际字形居中，数字按相同参考字形拟合；保留原图仪表刻度，空白仪表补充 60 等分刻度。
专项验证检查月历标题字形像素边界、播放器及计时器按钮圆心、月份导航的实际输入命中。
金属刻纹与文字颜色相近的区域采用逐项原生对照，不通过 RGB 阈值误判文字边界。
Testing/vampire-alice/comparison.html 可筛选全部 45 项，comparison-01..08.png 每页最多 6 项并排对照。
runtime-plates.provenance.json 记录清除示例内容后的素材来源与哈希；原设计素材不可变。
运行时保留绒面、玻璃、金属、原生轨道，使用原稿透明蒙版消除衍生素材外沿底色。
钢珠轨道与节拍弹珠分别沿用原模型，并在独立 Alice 副本中登记实际造型碰撞几何；
发布验证额外模拟两种尺寸各 125 秒，检查接触、球数上限、分钟回收及节拍击球。
相册测试只读取仓库 assets/wallpapers 固定素材，预览不使用用户个人相册。
prepare.py、register.py、adapt.py 是一次性导入记录，不用于日常构建；calibrate.py 维护源图像素坐标。
风铃沿用现有 Box2D 模型，按 Alice 挂件尺寸调整形状、锚点及尾坠，原有主题插件不变。
本组件集是 CyberDesktop 内置套件，独立安装包仍需要应用目录中的同版本 Alice 共享运行时。
