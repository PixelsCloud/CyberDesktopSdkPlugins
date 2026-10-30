"""Check include roots and the controller/model composition boundaries.

This is a source policy check, not a C++ parser or an ownership certificate.
Compile the separate no-PCH headers and run lifecycle tests as well.
"""

import argparse
import json
from pathlib import Path, PureWindowsPath
import re
import subprocess


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl"}
EXCLUDED = ("third_party/", "libraries/cybercanvas/third_party/", "build/", ".design/", ".backup/")
GENERATED = {
    "plugins/ambience-suite/tools/Design.cpp": {"AmbienceCatalog.h"},
    "plugins/ambience-suite/tools/Tests.cpp": {"AmbienceCatalog.h"},
    "plugins/news-suite/tools/Design.cpp": {"NewsCatalog.h"},
    "plugins/news-suite/tools/Tests.cpp": {"NewsCatalog.h"},
    "plugins/widget-suite/tools/Design.cpp": {"WidgetCatalog.h"},
    "plugins/widget-suite/tools/Tests.cpp": {"WidgetCatalog.h"},
}
INCLUDE = re.compile(r'^\s*#\s*include\s*(["<])([^">]+)[">]', re.MULTILINE)
THEMES = ("deep-whale", "mecha-desk", "mechanical-desk", "redline-desk", "songyun-desk", "vampire-alice")


def source_files(root):
    if not (root / ".git").exists():
        inventory = json.loads((root / "public-source-files.json").read_text(encoding="utf-8-sig"))
        return sorted(name for name in inventory["files"] if Path(name).suffix in SOURCE_SUFFIXES
                      and not name.startswith(EXCLUDED) and (root / name).is_file())
    names = subprocess.check_output(
        ["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard"],
        text=True, encoding="utf-8"
    ).splitlines()
    return sorted({name for name in names if Path(name).suffix in SOURCE_SUFFIXES
                   and not name.startswith(EXCLUDED) and not name.endswith((".pb.h", ".pb.cc"))
                   and (root / name).is_file()})


def include_errors(name, source, public=True):
    errors = []
    for match in INCLUDE.finditer(source):
        quote, include = match.groups()
        parts = include.replace("\\", "/").split("/")
        line = source.count("\n", 0, match.start()) + 1
        if ".." in parts or PureWindowsPath(include).is_absolute() or include.startswith("/"):
            errors.append(f"{name}:{line}: parent/absolute include: {include}")
            continue
        if not public or quote != '"':
            continue
        if include in GENERATED.get(name, set()) or include.endswith(".pb.h"):
            continue
        prefixes = ("sdk/", "plugins/", "runtime/", "libraries/", "CyberCanvas/", "CharacterResources/")
        if name.startswith("sdk/"):
            prefixes += ("tools/", "support/", "tests/", "templates/", "examples/")
        if "/tools/" in name:
            prefixes += ("tools/",)
        if name.startswith("libraries/cybercanvas/"):
            prefixes += ("src/", "tests/")
        if not include.startswith(prefixes):
            errors.append(f"{name}:{line}: use an explicit include root: {include}")
    return errors


def adapters():
    rules = {}
    for theme in THEMES:
        for family in ("widget", "productivity"):
            rules[f"plugins/{theme}/controllers/{family}/Instance.cpp"] = ("themed::create", 80)
        if theme != "mechanical-desk":
            rules[f"plugins/{theme}/controllers/monitor/Instance.cpp"] = ("themed::create", 80)
    for theme in ("deep-whale", "mecha-desk"):
        rules[f"plugins/{theme}/AmbienceInstance.cpp"] = ("themed::create", 80)
    rules["plugins/mechanical-desk/controllers/ambience/Instance.cpp"] = ("themed::create", 80)
    for theme in ("deep-whale", "mecha-desk", "pixel-desk"):
        rules[f"plugins/{theme}/PlayerInstance.cpp"] = ("widgetplugin::media::Controller", 100)
    for theme in ("vampire-alice", "redline-desk", "songyun-desk"):
        for model, base in (("Beat", "beatmachine"), ("Marble", "marbleclock"), ("Toys", "toys")):
            rules[f"plugins/{theme}/{model}Model.cpp"] = (f"{base}::Model", 80)
    for theme in ("mechanical-desk", "mecha-desk"):
        rules[f"plugins/{theme}/ToysModel.cpp"] = ("toys::Model", 80)
    return rules


def adapter_errors(name, source, marker, maximum):
    errors = []
    if marker not in source:
        errors.append(f"{name}: must delegate to {marker}")
    if len(source.splitlines()) > maximum:
        errors.append(f"{name}: adapter exceeds {maximum} lines; review duplicated business logic")
    if re.search(r'\b(subscribe_timer|subscribe_media|subscribe_system_info|subscribe_audio)\b', source):
        errors.append(f"{name}: subscriptions belong in the shared controller")
    return errors


def check(root, core=None):
    errors = []
    files = source_files(root)
    for name in files:
        errors += include_errors(name, (root / name).read_text(encoding="utf-8-sig"))
    for name, (marker, maximum) in adapters().items():
        path = root / name
        if not path.is_file():
            errors.append(f"missing required adapter: {name}")
        else:
            errors += adapter_errors(name, path.read_text(encoding="utf-8-sig"), marker, maximum)
    pixel_cmake = (root / "plugins/pixel-desk/CMakeLists.txt").read_text(encoding="utf-8-sig")
    for forbidden in ("pixel_brick_model", "pixel_beat_model", "pixel_chime_model", "pixel_marble_model", "PixelAmbienceInstance", "../productivity-suite/Instance.cpp", "../system-info-suite/Monitor.cpp"):
        if forbidden in pixel_cmake:
            errors.append(f"pixel-desk/CMakeLists.txt: duplicated/generated controller: {forbidden}")
    if core:
        for name in source_files(core):
            errors += include_errors(name, (core / name).read_text(encoding="utf-8-sig"), public=False)
    return files, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--core-root", type=Path)
    args = parser.parse_args()
    files, errors = check(args.root.resolve(), args.core_root.resolve() if args.core_root else None)
    if errors:
        print("\n".join(errors))
        raise SystemExit(1)
    print(f"PASS: {len(files)} public sources, {len(adapters())} shared adapters; include and composition policy")


if __name__ == "__main__":
    main()
