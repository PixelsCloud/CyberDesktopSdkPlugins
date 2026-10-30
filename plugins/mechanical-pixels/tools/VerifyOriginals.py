"""Fingerprint the original styles before and after an independent plugin build."""
import hashlib
import json
from pathlib import Path
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    mode, repository, filename = sys.argv[1:]
    root, report = Path(repository), Path(filename)
    if mode == "record":
        files = [path for suite in ("clock-suite", "widget-suite")
                 for path in (root / "plugins" / suite).rglob("*") if path.is_file()]
        for directory in (root / "build/dist/plugins").glob("com.cyberdesktop.*"):
            if ".mechanical." not in directory.name and (
                    ".clock.pixel" in directory.name or ".music.matrix." in directory.name):
                files.extend(path for path in directory.rglob("*") if path.is_file())
        records = [{"path": path.relative_to(root).as_posix(), "sha256": digest(path)} for path in sorted(files)]
        report.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
        print(f"Recorded {len(records)} original source / runtime file fingerprints")
        return
    if mode != "check":
        raise ValueError("Expected record or check")
    records = json.loads(report.read_text(encoding="utf-8"))
    if len(records) < 100:
        raise ValueError("Original style baseline is incomplete")
    changed = [entry["path"] for entry in records if not (root / entry["path"]).is_file()
               or digest(root / entry["path"]) != entry["sha256"]]
    if changed:
        raise ValueError("Original style files changed: " + ", ".join(changed[:10]))
    print(f"PASS {len(records)} original source / runtime files remain byte-for-byte unchanged")


if __name__ == "__main__":
    main()
