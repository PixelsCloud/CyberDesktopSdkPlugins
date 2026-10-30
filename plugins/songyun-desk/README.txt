宋韵 · 青瓷文房

45 个独立桌面组件，统一标签「宋」。原稿：design/songyun-desk-suite/index.html。
青瓷、竹木、宣纸、山水与仙鹤保持批准设计的比例和原始材质。
正文、按钮、指针、进度、频谱和物理零件单独实时绘制，图标使用公共 Lucide 服务。
每侧至少保留 6 个逻辑像素，按实际 DPI 绘制，图片后台解码、缓存，局部刷新。
图集按组件实际区域裁切后解码，底图及零件同样按显示尺寸加载，避免多组件挤满共享缓存。
数据、设置、媒体控制及 Box2D 逻辑复用现有组件，插件 ID 和保存状态独立。

构建与验证：scripts/build-songyun.ps1 -Jobs 10。
仅生成原生对照预览：加 -PreviewOnly；单个组件：加 -Only <catalog folder>。
本套件依赖应用提供的 cyberdesktop_songyun_runtime.dll，各模块统一 x64 /MT。
专项报告与对照预览固定放在 build/relwithdebinfo/Testing/songyun-desk。
验收包含真实宿主同时运行全部 45 种布局、重建实例、缩放、DPI 切换、暂停恢复和保存状态恢复。
图像来源、派生素材和生成提示见 materials/import.json；不在 UI 显示实现技术名称。
