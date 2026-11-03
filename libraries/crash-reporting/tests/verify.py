"""Run console-only fault probes; never install hooks or create input windows."""

import concurrent.futures
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import statistics
import struct
import subprocess
import sys
import time


root = Path(sys.argv[1]).resolve()
assert root.name == "crash" and root.parent.name == "Testing", root
checks = []
samples = []
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4


def check(ok, name):
    checks.append({"name": name, "passed": bool(ok)})
    if not ok:
        raise AssertionError(name)


def run(name, mode):
    started = time.perf_counter()
    proc = subprocess.Popen([str(root / "bin" / (name + ".exe")), mode],
                            creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        code = proc.wait(timeout=8)
        times = [wintypes.FILETIME() for _ in range(4)]
        assert kernel.GetProcessTimes(int(proc._handle), *(ctypes.byref(v) for v in times))
        cpu = sum((v.dwHighDateTime << 32) | v.dwLowDateTime for v in times[2:]) / 10000
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        raise AssertionError(f"probe timed out: {name} {mode}")
    return {"pid": proc.pid, "code": code, "elapsed_ms": (time.perf_counter() - started) * 1000,
            "cpu_ms": cpu, "mode": mode, "name": name}


def dumps(name):
    return set((root / "crashes" / name).glob("????????-????-????-????-????????????.dmp"))


def validate(path, pid, exception=None):
    content = path.read_bytes()
    magic, version, count, directory = struct.unpack_from("<4I", content)
    assert magic == 0x504D444D and version & 0xFFFF == 0xA793, path
    streams = {}
    for index in range(count):
        kind, size, offset = struct.unpack_from("<3I", content, directory + index * 12)
        assert offset + size <= len(content)
        streams[kind] = offset
    assert struct.unpack_from("<I", content, streams[3])[0] > 0  # threads
    assert struct.unpack_from("<I", content, streams[4])[0] > 0  # modules
    assert struct.unpack_from("<I", content, streams[15] + 8)[0] == pid
    if exception is not None:
        actual = struct.unpack_from("<I", content, streams[6] + 8)[0]
        assert actual == exception, hex(actual)


try:
    for name in ("cd_crash_probe_a", "cd_crash_probe_b"):
        directory = root / "crashes" / name
        directory.mkdir(parents=True, exist_ok=True)
        marker = directory / "unowned.dmp"
        marker.write_text("preserve unrelated files", encoding="utf-8")
        for mode, exception in (("access", 0xC0000005), ("terminate", 0xE043CD01),
                                ("abort", 0xE043CD01), ("invalid", 0xC000000D), ("purecall", 0xE043CD02),
                                ("watchdog", None)):
            before = dumps(name)
            result = run(name, mode)
            samples.append(result)
            new = dumps(name) - before
            check(result["code"] != 0 and len(new) == 1, f"{name}: {mode} saves dump and exits")
            validate(next(iter(new)), result["pid"], exception)
            check(True, f"{name}: {mode} dump contains matching process, threads and modules")
            if mode == "watchdog":
                check(150 <= result["elapsed_ms"] < 3000 and result["code"] == 1460,
                      f"{name}: independent watchdog handles blocked thread within deadline")
        for index in range(7):
            result = run(name, "snapshot")
            samples.append(result)
            check(result["code"] == 0 and len(dumps(name)) == 5, f"{name}: retention after write {index}")
        check(marker.read_text(encoding="utf-8") == "preserve unrelated files", f"{name}: unrelated dump preserved")
        for mode in ("disarm", "renew", "idle"):
            result = run(name, mode)
            samples.append(result)
            check(result["code"] == 0, f"{name}: watchdog {mode} exits normally")
            if mode == "idle":
                check(result["cpu_ms"] <= 100, f"{name}: idle watchdog has bounded CPU cost")

    before_b = dumps("cd_crash_probe_b")
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        results = list(pool.map(lambda _: run("cd_crash_probe_a", "snapshot"), range(12)))
    samples.extend(results)
    check(all(r["code"] == 0 for r in results) and len(dumps("cd_crash_probe_a")) == 5,
          "concurrent PIDs retain five complete dumps in shared executable bucket")
    for path in dumps("cd_crash_probe_a"):
        # Concurrent dump headers still need valid thread and module streams.
        content = path.read_bytes()
        count, directory = struct.unpack_from("<II", content, 8)
        misc = next(struct.unpack_from("<III", content, directory + i * 12)[2]
                    for i in range(count) if struct.unpack_from("<I", content, directory + i * 12)[0] == 15)
        validate(path, struct.unpack_from("<I", content, misc + 8)[0])
    check(dumps("cd_crash_probe_b") == before_b, "retention never removes another executable's dumps")

    for index in range(10):
        for name in ("cd_crash_probe_baseline", "cd_crash_probe_a"):
            result = run(name, "startup")
            check(result["code"] == 0, f"{name}: startup sample {index}")
            samples.append(result)
finally:
    report = {"checks": checks, "samples": samples, "long_duration_validation": False,
              "input_injection": False, "native_windows_created": False}
    report["startup_median_ms"] = {
        name: statistics.median(r["elapsed_ms"] for r in samples if r["mode"] == "startup" and r["name"] == name)
        for name in sorted({r["name"] for r in samples if r["mode"] == "startup"})}
    (root / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({"checks": len(checks), "passed": sum(c["passed"] for c in checks),
                  "startup_median_ms": report["startup_median_ms"]}))
