# 夏晴示范资源

人物设定为 22 岁成年女性。图片使用内置 image_gen 生成；人物自拍以 `xiaqing/neutral.png` 为外观参考，完整提示词保存在上级 `asset-prompts.json`。其他三个默认人物模板共用此视觉资源，提示词各自独立；Mao/Haru 是另行注明来源的 Live2D 演出示例。

`selfie_cafe.png`、`selfie_home.png`、`selfie_beach.png` 是有手持视角和完整背景的实际自拍构图。三个 `scene_*.png` 也作为“场景插画”分享素材，不能把它们标为人物自拍。

`scene_beach_demo.mp4` 是 6 秒、1280×720、24 FPS 的示范场景短片，由已生成海边插画作缓慢平移，通过 FFmpeg 编码为 H.264；没有人物动画或真实拍摄内容。封面由隔离工作进程使用 Player 媒体运行库从实际视频帧生成。

`resources.json` 记录实际尺寸、字节数、用途和 SHA-256。仅在明确增加或替换源码自有素材后运行 `scripts/update-character-chat-assets.py` 更新清单；正常加载不自动重算并接受被修改的素材。用户导入的资源不由此脚本修改。
