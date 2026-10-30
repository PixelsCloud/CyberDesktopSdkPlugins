"""Audit stale MSVC/Ninja objects that recorded no include dependencies.

Uses the configured Ninja and CMake compile database. --repair retires only
identified first-party .obj files; sources, caches and third-party objects stay.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess


OWNED = {"src", "apps", "sdk", "plugins", "libraries", "runtime", "tools", "tests", "templates"}


def normalized(value):
    return value.replace("\\", "/").lower()


def zero_records(output):
    return {normalized(match) for match in re.findall(r'^(.+\.obj): #deps 0,.*\(VALID\)$', output, re.MULTILINE)}


def first_party(source, roots):
    for root in roots:
        if source.is_relative_to(root):
            relative = source.relative_to(root)
            if (relative.parts and relative.parts[0] in OWNED and "third_party" not in relative.parts
                    and not source.name.endswith((".pb.cc", ".pb.cpp"))):
                return True
    return False


def object_path(build, value):
    candidate = (build / value).resolve()
    if candidate.suffix.lower() != ".obj" or not candidate.is_relative_to(build) or "dist" in candidate.parts:
        raise ValueError(f"Object output escaped the compiler cache: {value}")
    return candidate


def audit(build, roots):
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8-sig")
    match = re.search(r'^CMAKE_MAKE_PROGRAM:[^=]+=(.+)$', cache, re.MULTILINE)
    if not match or Path(match[1]).stem.lower() != "ninja":
        raise ValueError("Use an existing Ninja cache")
    ninja = match[1]
    result = subprocess.run([ninja, "-C", str(build), "-t", "deps"], capture_output=True,
                            text=True, encoding="utf-8", errors="replace", check=True)
    zero = zero_records(result.stdout)
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8-sig"))
    invalid = []
    seen = set()
    for entry in entries:
        output = entry.get("output", "")
        if normalized(output) not in zero:
            continue
        if normalized(output) in seen:
            continue
        seen.add(normalized(output))
        source = Path(entry["file"]).resolve()
        if not first_party(source, roots) or not source.is_file():
            continue
        text = source.read_text(encoding="utf-8-sig")
        if not re.search(r'^\s*#\s*include\s*["<]', text, re.MULTILINE):
            continue
        path = object_path(build, output)
        if path.is_file():
            invalid.append({"source": str(source), "object": str(path)})
    return ninja, invalid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, action="append", required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--repair", action="store_true")
    args = parser.parse_args()
    build = args.build.resolve()
    if "dist" in build.parts or not (build / "CMakeCache.txt").is_file():
        parser.error("Select the existing compiler cache, not a distribution or source directory")
    roots = [root.resolve() for root in args.source_root]
    ninja, invalid = audit(build, roots)
    if args.repair:
        for item in invalid:
            # Re-resolve before each mutation; no recursive removal or shell command.
            object_path(build, item["object"]).unlink()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({"build": str(build), "ninja": ninja, "invalidObjects": invalid,
                                      "retiredObjects": len(invalid) if args.repair else 0}, indent=2) + "\n",
                           encoding="utf-8")
    print(f"{'REPAIRED' if args.repair else 'CHECKED'}: {len(invalid)} first-party objects with zero header dependencies")
    if invalid and not args.repair:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
