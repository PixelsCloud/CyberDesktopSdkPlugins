"""Require a lifecycle fault to be rejected by the real controller tests."""

import subprocess
import sys

result = subprocess.run([sys.argv[1]], capture_output=True, text=True, timeout=30)
expected = "partial startup retained callbacks or audio group"
if result.returncode != 1 or expected not in result.stdout + result.stderr:
    raise SystemExit(f"Lifecycle negative control failed: exit={result.returncode}\n{result.stdout}{result.stderr}")
print("PASS: removing destructor audio cleanup is detected during partial startup")
