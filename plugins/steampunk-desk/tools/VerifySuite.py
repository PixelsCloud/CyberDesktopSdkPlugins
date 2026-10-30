"""Check immutable originals and Steamworks identity, tag and footprint contracts."""

import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
SUITE = ROOT / "plugins/steampunk-desk"
LOGS = ROOT / "build/relwithdebinfo/Testing/steampunk-desk"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def preserve(mode):
    baseline = LOGS / "originals-before.json"
    if mode == "record":
        if baseline.exists():
            preserve("check")
            return
        files = [path for path in (ROOT / "plugins").rglob("*")
                 if path.is_file() and SUITE not in path.parents]
        manifest = json.loads((ROOT / "design/steampunk-desk-suite/manifest.json").read_text(encoding="utf-8"))
        for board in manifest["boards"]:
            for entry in board["entries"]:
                directory = ROOT / "build/dist/plugins" / entry["id"]
                files += [path for path in directory.rglob("*") if path.is_file()]
                package = ROOT / "build/dist/plugin-packages" / (entry["id"] + ".cdpkg")
                if package.is_file():
                    files.append(package)
        records = [{"path": path.relative_to(ROOT).as_posix(), "sha256": digest(path)} for path in sorted(set(files))]
        LOGS.mkdir(parents=True, exist_ok=True)
        baseline.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
        print(f"PASS recorded {len(records)} existing plugin source/runtime fingerprints")
        return
    records = json.loads(baseline.read_text(encoding="utf-8"))
    assert len(records) > 100, "Original baseline incomplete"
    changed = [entry["path"] for entry in records if not (ROOT / entry["path"]).is_file()
               or digest(ROOT / entry["path"]) != entry["sha256"]]
    assert not changed, f"Original plugin changed: {changed[:10]}"
    print(f"PASS {len(records)} existing plugin source/runtime files remain unchanged")


def contract(folder):
    catalog = json.loads((SUITE / "catalog.json").read_text(encoding="utf-8"))
    task = json.loads((ROOT / "design/steampunk-desk-suite/implementation-task.json").read_text(encoding="utf-8"))
    assert len(task["entries"]) == len({entry["id"] for entry in task["entries"]}) == 45
    entry = next(entry for entry in catalog if entry["folder"] == folder)
    planned = next(item for item in task["entries"] if item["id"] == entry["id"])
    assert planned["family"] == entry["family"], "Catalog/task family mismatch"
    descriptor = (SUITE / "packages" / folder / "plugin.textproto").read_text(encoding="utf-8")
    assert f'id: "{entry["id"]}"' in descriptor
    tags = re.findall(r'^tags:\s*"([^"]+)"', descriptor, re.M)
    assert "蒸汽朋克" in tags and "像素" not in tags and "机械" not in tags, tags
    assert re.search(r'^name:\s*"蒸汽朋克[^?]+"$', descriptor, re.M), "Invalid UTF-8 plugin name"
    assert 'version: "1.0.0"' in descriptor
    assert 'preview: "preview.png"' in descriptor
    cells = []
    for block in re.findall(r"\bcells\s*\{([^}]*)\}", descriptor):
        cells.append(tuple(int(match[1]) if (match := re.search(r"\b" + axis + r":\s*(-?\d+)", block)) else 0
                           for axis in ("a", "b")))
    assert len(cells) == planned["columns"] * planned["rows"]
    assert len(set(cells)) == len(cells)
    assert max(x for x, _ in cells) - min(x for x, _ in cells) + 1 == planned["columns"]
    assert max(y for _, y in cells) - min(y for _, y in cells) + 1 == planned["rows"]
    assert entry["id"] != re.search(r'^id:\s*"([^"]+)"', (ROOT / planned["source"]).read_text(encoding="utf-8-sig"), re.M)[1]
    if entry.get("renderer") == "procedural":
        assert (SUITE / "SteamParts.h").is_file()
        assert (SUITE / "SpringClock.cpp").is_file()
    else:
        provenance = json.loads((SUITE / "assets" / entry["art"] / "provenance.json").read_text(encoding="utf-8"))
        assert provenance["sha256"] == digest(SUITE / "assets" / entry["art"] / "body.png")
    if entry.get("scene"):
        scene = SUITE / "assets" / entry["scene"]
        provenance = json.loads((scene / "city.provenance.json").read_text(encoding="utf-8"))
        assert provenance["sha256"] == digest(scene / "city.png")
    if entry["family"] == "album":
        art = SUITE / "assets" / entry["art"]
        sample = json.loads((art / "preview-photo.provenance.json").read_text(encoding="utf-8"))
        assert sample["sha256"] == digest(art / "preview-photo.png")
    print(f"PASS {folder}: independent identity, steampunk tag, actual footprint, provenance and sequential order")


if __name__ == "__main__":
    if sys.argv[1] in ("record", "check"):
        preserve(sys.argv[1])
    else:
        assert sys.argv[1] == "contract"
        contract(sys.argv[2])
