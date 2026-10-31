"""Resolve every official physics specification through its shared source model."""

import argparse
import json
from pathlib import Path


BASE_MODELS = {
    "beat-machine": "beat-machine",
    "brick-clock": "brick-clock",
    "marble-clock": "marble-clock",
    "physics-toys": "physics-toys",
    "hourglass-suite": "physical-sand",
    "mechanical-hourglass": "physical-sand",
}


def inventory(root):
    entries = {}
    inactive = []
    for catalog in sorted((root / "plugins").glob("*/catalog.json")):
        data = json.loads(catalog.read_text(encoding="utf-8-sig"))
        if not isinstance(data, list):
            continue
        active_path = catalog.parent / "active.json"
        active = set(json.loads(active_path.read_text(encoding="utf-8-sig"))) if active_path.exists() else None
        for spec in data:
            if "id" not in spec:
                continue
            if spec["id"] in entries:
                raise ValueError(f"Duplicate plugin ID: {spec['id']}")
            entries[spec["id"]] = {**spec, "suite": catalog.parent.name,
                                   "implemented": active is None or spec["id"] in active}

    def model(spec, chain=()):
        identifier = spec["id"]
        if identifier in chain:
            raise ValueError(f"Cyclic source chain: {chain + (identifier,)}")
        suite, family = spec["suite"], spec.get("family", "")
        if suite in BASE_MODELS:
            return BASE_MODELS[suite]
        if suite == "streetlink-desk" and spec.get("nativePhysics"):
            return "streetlink-physics"
        if suite == "redline-desk" and family == "kinetic":
            return "redline-kinetics"
        if suite == "deep-whale" and family == "beat":
            return "deep-whale-beat"
        if suite == "mecha-desk" and family == "armour":
            return "audio-mechanism"
        if suite == "mecha-desk" and family == "basic" and "chrono" in identifier:
            return "clock-animation"
        source = entries.get(spec.get("sourceId"))
        if source:
            return model(source, chain + (identifier,))
        # Some theme catalogs route by family without recording sourceId.
        return {"spring": "physics-toys", "chime": "physics-toys", "toys": "physics-toys",
                "marble": "marble-clock", "beat": "beat-machine", "armour": "beat-machine",
                "brick": "brick-clock", "hourglass": "physical-sand"}.get(family)

    result = []
    for identifier, spec in sorted(entries.items()):
        resolved = model(spec)
        if resolved:
            if not spec["implemented"]:
                inactive.append({"id": identifier, "model": resolved, "status": "design-only"})
                continue
            result.append({"id": identifier, "suite": spec["suite"], "folder": spec["folder"],
                           "model": resolved, "sourceId": spec.get("sourceId", identifier),
                           "box2d": resolved not in {"audio-mechanism", "clock-animation"},
                           "columns": spec.get("columns"), "rows": spec.get("rows"),
                           "music": "beat" in identifier or spec.get("family") == "kinetic",
                           "continuousMusic": "beat" in identifier and resolved != "redline-kinetics"})
    if not result:
        raise ValueError("No official physics specifications found")
    return {"version": 1, "packages": result, "designOnly": inactive, "backendModels": ["physics-particles"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = inventory(args.root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Physics inventory: {len(data['packages'])} official specifications")


if __name__ == "__main__":
    main()
