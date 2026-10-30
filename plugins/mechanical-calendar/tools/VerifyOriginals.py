"""Check that the independent mechanical calendar leaves the old style intact."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def section(text, name):
    patterns = {"calendar_view": r"(?ms)^  void calendar\(.*?^  }",
                "calendar_api": r"(?s)struct Date \{.*?int monthStart\(Date, bool monday\);"}
    match = re.search(patterns[name], text)
    if not match:
        raise ValueError(f"Original calendar section not found: {name}")
    return match[0]


def digest(path, name=None):
    data = section(path.read_text(encoding="utf-8"), name).encode("utf-8") if name else path.read_bytes()
    return hashlib.sha256(data).hexdigest()


def main():
    mode, repository, filename = sys.argv[1:]
    root, report = Path(repository), Path(filename)
    if mode == "record":
        # Other independent tasks may update music in the shared widget suite.
        # Protect the actual calendar source, view, API, descriptor and runtime.
        files = [root / "plugins/widget-suite/Calendar.cpp"]
        files.extend(p for p in (root / "plugins/widget-suite/packages/calendar-2x4").rglob("*") if p.is_file())
        for directory in (root / "build/dist/plugins").glob("com.cyberdesktop.calendar.2x4"):
            files.extend(p for p in directory.rglob("*") if p.is_file())
        records = [{"path": p.relative_to(root).as_posix(), "sha256": digest(p)} for p in sorted(files)]
        for relative, name in [("plugins/widget-suite/Canvas.cpp", "calendar_view"),
                               ("plugins/widget-suite/Widget.h", "calendar_api")]:
            # The inspected shared-source changes are outside these sections.
            # Confirm the protected calendar sections still match the original.
            committed = subprocess.run(["git", "show", "HEAD:" + relative], cwd=root,
                                       capture_output=True, encoding="utf-8", check=True).stdout
            if section(committed, name) != section((root / relative).read_text(encoding="utf-8"), name):
                raise ValueError("Original calendar section changed: " + relative)
            records.append({"path": relative, "section": name, "sha256": digest(root / relative, name)})
        report.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
        print(f"Recorded {len(records)} original calendar source / runtime files")
        return
    if mode != "check":
        raise ValueError("Expected record or check")
    records = json.loads(report.read_text(encoding="utf-8"))
    if len(records) < 10:
        raise ValueError("Original calendar baseline is incomplete")
    changed = [entry["path"] for entry in records if not (root / entry["path"]).is_file()
               or digest(root / entry["path"], entry.get("section")) != entry["sha256"]]
    if changed:
        raise ValueError("Original calendar changed: " + ", ".join(changed))
    print(f"PASS {len(records)} original calendar source sections / runtime files remain byte-for-byte unchanged")


if __name__ == "__main__":
    main()
