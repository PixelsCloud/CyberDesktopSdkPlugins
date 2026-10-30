"""Register approved local artwork; never edits pixels or publishes draft widgets."""

import hashlib
import importlib.util
import subprocess
from google.protobuf import text_format
import json
import re
import shutil
from pathlib import Path
from PIL import Image

from Authoring import registration

ROOT = Path(__file__).resolve().parents[3]
SUITE = ROOT / "plugins/streetlink-desk"
DESIGN = ROOT / "design/streetlink-desk-suite/redesign"
PRODUCTION = {
    6: "live-face.png",
    10: "tall-face.png",
    14: "empty-face.png",
    15: "empty-face.png",
    16: "empty-face.png",
    18: "wide-pods.png",
    39: "live-face.png",
    54: "clean-face.png",
    68: "topdown.png",
    70: "topdown.png",
    71: "topdown.png",
}

# Display interiors measured in the visible alpha-bounds coordinate system.
# The original frame is retained; display backgrounds and live values are native.
SCENES = {
    "clock-square-4x1": {
        "regions": {
            "plot": [0.185, 0.255, 0.39, 0.43],
            "weekday": [0.618, 0.255, 0.055, 0.10],
            "day": [0.615, 0.455, 0.062, 0.18],
        },
        "screens": [[0.18, 0.25, 0.40, 0.45], [0.61, 0.25, 0.07, 0.12], [0.61, 0.45, 0.075, 0.20]],
    },
    "clock-round-4x1": {
        "regions": {"plot": [0.255, 0.32, 0.52, 0.37]},
        "screens": [[0.253, 0.318, 0.525, 0.375]],
    },
}


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(value, ensure_ascii=False, indent=2) + "\n"
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


def copy(source, destination):
    if not destination.exists() or source.read_bytes() != destination.read_bytes():
        shutil.copyfile(source, destination)


def descriptor_type():
    schema = ROOT / "sdk/schemas/plugin_package.proto"
    generated = ROOT / "build/relwithdebinfo/Testing/streetlink-desk/protobuf"
    generated.mkdir(parents=True, exist_ok=True)
    target = generated / "plugin_package_pb2.py"
    if not target.exists() or target.stat().st_mtime < schema.stat().st_mtime:
        subprocess.run(
            ["protoc", f"--proto_path={schema.parent}", f"--python_out={generated}", str(schema)], check=True
        )
    spec = importlib.util.spec_from_file_location("streetlink_descriptors", target)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.PluginDescriptor


def main():
    design = json.loads((DESIGN / "manifest.json").read_text(encoding="utf-8"))
    shared = json.loads((ROOT / "plugins/mecha-desk/catalog.json").read_text(encoding="utf-8"))
    controllers = {item["source"]: item for item in shared}
    descriptor_class = descriptor_type()
    inventory, catalog, seen = [], [], set()
    for board in design["boards"]:
        for entry in board["entries"]:
            if entry["key"] in seen:
                continue
            seen.add(entry["key"])
            art = design["artwork"][str(entry["order"])]
            source = DESIGN / art["file"]
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            if digest != art["sha256"]:
                raise ValueError(f"Approved artwork changed: {source}")
            item = {
                key: entry[key]
                for key in ("key", "name", "pixelId", "sourceDescriptor", "family", "columns", "rows", "order")
            }
            item.update(
                id=entry["pixelId"].replace(".pixeldesk.", ".streetlink."),
                sourceSha256=digest,
                sourceFile=art["file"],
                sourceBounds=art["bounds"],
                sourceImageSize=art["imageSize"],
                native=[entry["width"], entry["height"]],
                minimumInset=6,
                samplingInset=7,
                status="native-verification",
            )
            inventory.append(item)
            controller = controllers[entry["sourceDescriptor"]]
            destination = SUITE / "assets" / entry["key"]
            destination.mkdir(parents=True, exist_ok=True)
            production = source
            production_art = art
            if entry["order"] in PRODUCTION:
                production = destination / PRODUCTION[entry["order"]]
                with Image.open(production) as image:
                    left, top, right, bottom = image.getchannel("A").point(lambda p: 255 if p > 4 else 0).getbbox()
                    production_art = {**art, "bounds": [left, top, right - left, bottom - top]}
            copy(production, destination / "body.png")
            if entry["key"] in SCENES:
                scene = {**SCENES[entry["key"]], "bounds": art["bounds"], "native": item["native"]}
                write(destination / "scene.json", scene)
            else:
                reference = json.loads(
                    (ROOT / "plugins/mecha-desk/assets" / controller["folder"] / "skin.json").read_text(
                        encoding="utf-8"
                    )
                )
                write(destination / "skin.json", registration(item, production_art, reference))
            write(
                destination / "provenance.json",
                {
                    "sha256": digest,
                    "source": art["file"],
                    "productionSha256": hashlib.sha256(production.read_bytes()).hexdigest(),
                    "pixelOperations": (
                        (
                            "imagegen: remove clock hands, ticks and sample values; preserve city and housing"
                            if entry["order"] == 54
                            else "imagegen: revised empty display or orthographic overhead pond; live content rendered separately"
                        )
                        if production != source
                        else "none"
                    ),
                    "theme": "STREETLINK",
                },
            )
            package = SUITE / "packages" / entry["key"]
            package.mkdir(parents=True, exist_ok=True)
            descriptor_source = ROOT / "plugins/mecha-desk/packages" / controller["folder"] / "plugin.textproto"
            native_physics = entry["order"] in (5, 6, 39, 40)
            if native_physics:
                descriptor_source = ROOT / entry["sourceDescriptor"]
            if entry["key"] in SCENES:
                descriptor_source = ROOT / "plugins/pixel-desk/packages" / entry["key"] / "plugin.textproto"
            descriptor = descriptor_source.read_text(encoding="utf-8")
            descriptor = (
                descriptor.replace(entry["pixelId"], item["id"])
                .replace(controller["id"], item["id"])
                .replace(controller["sourceId"], item["id"])
            )
            descriptor = re.sub(r'^version: ".*"', 'version: "1.0.0"', descriptor, count=1, flags=re.MULTILINE)
            descriptor = re.sub(
                r'^name: ".*"', f'name: "STREETLINK · {entry["name"]}"', descriptor, count=1, flags=re.MULTILINE
            )
            descriptor = descriptor.replace('tags: "像素"', 'tags: "赛博朋克"\ntags: "STREETLINK"')
            descriptor = descriptor.replace('tags: "AERIS"', 'tags: "STREETLINK"').replace(
                'tags: "机械"', 'tags: "赛博朋克"'
            )
            descriptor = descriptor.replace('tags: "机甲"', 'tags: "赛博朋克"\ntags: "STREETLINK"')
            descriptor = descriptor.replace("color_default: 2465052159", "color_default: 4294967295")
            for tag in ("赛博朋克", "STREETLINK"):
                if f'tags: "{tag}"' not in descriptor:
                    descriptor += f'\ntags: "{tag}"\n'
            if native_physics:
                metadata = text_format.Parse(descriptor, descriptor_class())
                page = metadata.settings_page
                for i in range(len(page.fields) - 1, -1, -1):
                    if page.fields[i].id == "theme":
                        del page.fields[i]
                metadata.description = (
                    "每秒增加一颗粒子，分钟切换同步清空；粒子沿回路运动，支持拖动和双击重置。"
                    if entry["order"] in (5, 6)
                    else "系统音频驱动双腔粒子碰撞和实时频谱；静音后平滑衰减。"
                )
                page.title = entry["name"]
                page.description = metadata.description
                for localized in page.localized.values():
                    localized.title = "STREETLINK"
                    localized.description = (
                        "Audio-driven particle chambers and live spectrum. Smooth decay after silence."
                        if entry["order"] in (39, 40)
                        else "One particle per second. Drag particles; double-click to reset."
                    )
                descriptor = text_format.MessageToString(metadata, as_utf8=True)
            metadata = text_format.Parse(descriptor, descriptor_class())
            if native_physics and entry["order"] in (5, 6):
                text_format.Merge("capabilities: AUDIO_FFT", metadata)
                metadata.description = "每秒增加粒子并显示时间；系统音频驱动粒子和实时频谱，静音后频谱归零。"
                metadata.settings_page.description = metadata.description
                gain = metadata.settings_page.fields.add()
                gain.id, gain.label, gain.type = "gain", "音乐灵敏度", 3
                gain.minimum, gain.maximum, gain.step, gain.number_default = 0.25, 5, 0.1, 3.5
            for field in metadata.settings_page.fields:
                if field.id == "gain":
                    field.number_default = 3.5
                    field.maximum = max(5, field.maximum)
            descriptor = text_format.MessageToString(metadata, as_utf8=True)
            descriptor_path = package / "plugin.textproto"
            if not descriptor_path.exists() or descriptor_path.read_text(encoding="utf-8") != descriptor:
                descriptor_path.write_text(descriptor, encoding="utf-8")
            copy(production, package / "preview.png")
            catalog.append(
                {
                    "id": item["id"],
                    "folder": entry["key"],
                    "name": entry["name"],
                    "columns": entry["columns"],
                    "rows": entry["rows"],
                    **{key: controller[key] for key in ("family", "sourceId", "kind")},
                    "nativeClock": entry["key"] in SCENES,
                    "nativePhysics": native_physics,
                }
            )
    assert len(inventory) == design["layoutCount"] == 75
    write(SUITE / "inventory.json", inventory)
    write(SUITE / "catalog.json", catalog)
    dock = SUITE / "assets/shared"
    dock.mkdir(parents=True, exist_ok=True)
    copy(DESIGN / design["dock"]["file"], dock / "taskbar.png")
    write(dock / "provenance.json", design["dock"])
    print(f"Registered {len(inventory)} designs; {len(catalog)} native packages under verification")


if __name__ == "__main__":
    main()
