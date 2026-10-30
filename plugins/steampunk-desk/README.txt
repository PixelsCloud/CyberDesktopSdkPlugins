CyberDesktop Steamworks / 蒸汽工坊

当前实现首批 6 个时钟规格：方点阵、圆点阵、弹簧 4×1 / 4×2、滚珠 4×2 / 6×2。
其余 39 个已批准规格保留在 catalog.json 和设计任务清单中，未作为可用插件发布。
原有机械、pixels 插件身份、数据、配置与行为保持独立。

材质采用透明 PNG 独立零件；时间、数字、弹簧、滚珠、轨道和铰链按实时数据和物理坐标绘制。
复用 SDK 后台图片服务、局部刷新和 Box2D 模型，按实际 DPI 生成组件纹理。
编译最多 10 个逻辑核心 / 10 个并行作业，复用 build/relwithdebinfo，增量发布至 build/dist。

构建：scripts/build-steampunk-desk.ps1 -Plugin <catalog folder> -Jobs 10
已复核且截图未变的构建自动增量发布；视觉发生变化时由开发流程完成截图复核后运行部署脚本。
全部 45 项进度与逐项证据：design/steampunk-desk-suite/implementation-task.json。
