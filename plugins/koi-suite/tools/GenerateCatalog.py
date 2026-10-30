"""Regenerate fixed footprints and bilingual settings; previews come from C++."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIELDS = [
    ("lighting", "昼夜模式", "Lighting", "CHOICE", 'text_default: "theme"',
     [("theme", "跟随桌面主题", "Follow desktop theme"), ("day", "白天", "Day"), ("night", "黑夜", "Night")],
     "跟随模式依据主题文字明暗选择昼夜；也可手动固定。", "Theme mode follows the desktop text contrast; manual day and night are available."),
    ("fish_count", "锦鲤数量", "Koi count", "INTEGER", "minimum: 1 maximum: 12 step: 1 integer_default: {count}", [],
     "只调整当前池塘的鱼群。", "Adjusts only this pond."),
    ("mouse_behavior", "鼠标交互", "Mouse interaction", "CHOICE", 'text_default: "flee"',
     [("flee", "点击吓跑", "Flee on click"), ("follow", "游向鼠标", "Swim toward pointer")],
     "默认点击水面时鱼群加速逃离；游向鼠标模式跟随池塘内的鼠标，移出后恢复游动。拖动不触发惊吓。",
     "Clicks startle the school by default. Follow mode swims toward the pointer inside the pond; leaving resumes roaming. Dragging does not startle fish."),
    ("show_background", "显示背景", "Show background", "TOGGLE", "bool_default: true", [],
     "关闭池水底色、水底石子与边框，保留锦鲤、水面波光、荷叶和荷花。",
     "Hide the water fill, submerged stones and border; retain fish, shimmer, lily pads and lotus blossoms."),
    ("blessings", "显示祝福", "", "TOGGLE", "bool_default: true", [],
     "50 个简体中文祝福词随机轮换；调试期间每约 6 秒出现一次，淡入、轻浮、淡出。", ""),
    ("swim_speed", "游动速度", "Swim speed", "NUMBER", "minimum: 0.3 maximum: 2 step: 0.1 number_default: 1", [], "", ""),
    ("shimmer", "水面波光强度", "Water shimmer", "INTEGER", "minimum: 0 maximum: 100 step: 1 integer_default: 65", [], "", ""),
    ("color", "锦鲤花纹颜色", "Koi marking color", "COLOR", "color_default: 0xDE593CFF", [], "保留白、金和墨色，仅调整红色花纹。", "Adjusts red markings; white, gold and black remain."),
    ("leaves", "显示荷叶", "Lily pads", "TOGGLE", "bool_default: true", [], "", ""),
    ("flowers", "显示荷花", "Lotus blossoms", "TOGGLE", "bool_default: true", [], "", ""),
    ("animation", "启用动画", "Animate pond", "TOGGLE", "bool_default: true", [],
     "关闭后保留静止池塘，停止动画定时器。", "Disabled keeps a still pond and removes its animation timer."),
    ("frame_rate", "动画帧率", "Animation frame rate", "CHOICE", 'text_default: "20"',
     [("15", "15 FPS", "15 FPS"), ("20", "20 FPS", "20 FPS"), ("30", "30 FPS", "30 FPS")],
     "默认 20 帧；只刷新池塘区域。", "Defaults to 20 fps; redraws only the pond content."),
    ("seed", "池塘布局编号", "Pond variation", "INTEGER", "minimum: 1 maximum: 9999 step: 1 integer_default: 7", [],
     "每次启动随机分布和朝向；改变编号可重新随机排列当前鱼群。", "Every startup randomizes positions and headings. Changing this value reshuffles the current school."),
    ("background_corner_radius", "背景圆角", "Corner radius", "INTEGER", "minimum: 0 maximum: 32 step: 1 integer_default: 5", [], "", ""),
    ("language", "设置语言", "Settings language", "CHOICE", 'text_default: "zh-CN"',
     [("zh-CN", "简体中文", "简体中文"), ("en", "English", "English")], "", ""),
]


def quote(value):
    return json.dumps(value, ensure_ascii=False)


def generate():
    catalog = []
    for rows, columns, count in [(2, 4, 5), (4, 4, 8), (3, 6, 9)]:
        size = f"{rows}x{columns}"
        plugin_id = f"com.cyberdesktop.koi.{size}"
        catalog.append(dict(id=plugin_id, folder=size, rows=rows, columns=columns, fishCount=count))
        text = [f'id: "{plugin_id}"', f'name: "锦鲤池 {columns}×{rows}"', 'version: "1.1.1"',
                'author: "CyberDesktop / argonaut (procedural fish)"', 'abi_version: 1', 'entry: "plugin.dll"',
                'preview: "preview.png"', 'content_padding: 4',
                'description: "程序化锦鲤、水面波光、荷叶和荷花；昼夜随主题变化，独立设置与局部刷新。"',
                f'footprint_rules {{ topology: RECT min_cells: {rows*columns} max_cells: {rows*columns} max_columns: {columns} max_rows: {rows} }}',
                f'layouts {{ id: "{size}" name: "{columns}×{rows}（宽×高）" topology: RECT is_default: true allowed_rotations: 0']
        text.extend(f'  cells {{ a: {x} b: {y} }}' for y in range(rows) for x in range(columns))
        text.extend(['}', 'settings_page {', f'  title: "锦鲤池 {columns}×{rows}设置"',
                     '  description: "每个池塘独立保存。可调整鱼数量、点击吓跑或游向鼠标，并可关闭背景。"',
                     '  localized { key: "en" value { title: "Koi pond settings" description: "Independent koi count, flee/follow interaction and optional transparent background." } }'])
        for key, label, english, kind, default, choices, description, en_description in FIELDS:
            text.extend(['  fields {', f'    id: "{key}"', f'    label: {quote(label)}', '    section: "池塘"',
                         f'    type: {kind}', '    ' + default.replace('{count}', str(count))])
            if description:
                text.append(f'    description: {quote(description)}')
            for value, name, en_name in choices:
                text.append(f'    choices {{ value: {quote(value)} label: {quote(name)} localized_labels {{ key: "en" value: {quote(en_name)} }} }}')
            if english:
                text.append(f'    localized {{ key: "en" value {{ label: {quote(english)} section: "Pond" description: {quote(en_description)} }} }}')
            text.append('  }')
        text.append('}')
        output = ROOT / 'packages' / size
        output.mkdir(parents=True, exist_ok=True)
        (output / 'plugin.textproto').write_text('\n'.join(text) + '\n', encoding='utf-8')
    (ROOT / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    generate()
