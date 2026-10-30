"""Check that the independent mechanical hourglass leaves the old style intact."""
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
        files = [p for p in (root / "plugins/hourglass-suite").rglob("*") if p.is_file()]
        for directory in (root / "build/dist/plugins").glob("com.cyberdesktop.hourglass.pixel.*"):
            files.extend(p for p in directory.rglob("*") if p.is_file())
        records = [{"path": p.relative_to(root).as_posix(), "sha256": digest(p)} for p in sorted(files)]
        report.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
        print(f"Recorded {len(records)} original hourglass source / runtime files")
        return
    if mode != "check":
        raise ValueError("Expected record or check")
    records = json.loads(report.read_text(encoding="utf-8"))
    if len(records) < 25:
        raise ValueError("Original hourglass baseline is incomplete")
    changed = [entry["path"] for entry in records if not (root / entry["path"]).is_file()
               or digest(root / entry["path"]) != entry["sha256"]]
    if changed:
        raise ValueError("Original hourglass changed: " + ", ".join(changed))
    print(f"PASS {len(records)} original hourglass source / runtime files remain byte-for-byte unchanged")


if __name__ == "__main__":
    main()
