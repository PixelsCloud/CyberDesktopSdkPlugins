"""Generate descriptors from explicit column/row counts; IDs keep the same meaning."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
for columns in (6, 4):
    layout = f"{columns}x2"
    cells = "\n".join(f"  cells {{ a: {x} b: {y} }}" for y in range(2) for x in range(columns))
    descriptor = f'''id: "com.cyberdesktop.clock.marble.{layout}"
name: "滚珠轨道时钟 {columns}×2"
version: "1.1.1"
author: "CyberDesktop"
abi_version: 1
entry: "plugin.dll"
description: "每秒一颗滚珠沿轨道折返，整分钟开启翻板回收。真实 Box2D 碰撞、可拖动滚珠，时分秒独立准确显示。"
layouts {{
  id: "{layout}"
  name: "{columns}×2（宽×高）"
{cells}
  allowed_rotations: 0
  is_default: true
}}
footprint_rules {{ min_cells: {columns * 2} max_cells: {columns * 2} max_columns: {columns} max_rows: 2 }}
settings_page {{
  title: "滚珠轨道时钟"
  description: "点击投珠器补球；点击金色转轴开仓；拖动滚珠并松手。隐藏和校时后直接同步，不补播离线动画。"
  fields {{
    id: "theme"
    label: "外观"
    type: CHOICE
    text_default: "dark"
    choices {{ value: "dark" label: "深色机械" localized_labels {{ key: "en" value: "Dark" }} }}
    choices {{ value: "light" label: "浅色机械" localized_labels {{ key: "en" value: "Light" }} }}
    choices {{ value: "system" label: "跟随系统" localized_labels {{ key: "en" value: "System" }} }}
    localized {{ key: "en" value {{ label: "Appearance" }} }}
  }}
  fields {{
    id: "show_background" label: "显示底板" type: TOGGLE bool_default: true
    localized {{ key: "en" value {{ label: "Show backplate" }} }}
  }}
  fields {{
    id: "running" label: "启用滚珠动画" type: TOGGLE bool_default: true
    localized {{ key: "en" value {{ label: "Enable physical motion" }} }}
  }}
  fields {{
    id: "reduced_motion" label: "减少动态" type: TOGGLE bool_default: false
    localized {{ key: "en" value {{ label: "Reduce motion" }} }}
  }}
  localized {{ key: "en" value {{ title: "Rolling Marble Clock" description: "Release a marble every second and empty the bin each minute. Click the dispenser or hinge; drag a marble to play." }} }}
}}
content_padding: 2
preview: "preview.png"
'''
    output = ROOT / "packages" / layout
    output.mkdir(parents=True, exist_ok=True)
    (output / "plugin.textproto").write_text(descriptor, encoding="utf-8")
print("Generated 6x2 and 4x2 marble clock descriptors")
