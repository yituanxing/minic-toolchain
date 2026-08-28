#!/usr/bin/env python3
import subprocess

for script in (
    "tools/dev/run-core-frontier-batch-m31.py",
    "tools/dev/patch-core-frontier-batch-m31b.py",
    "tools/dev/patch-core-frontier-batch-m31b-fixups.py",
):
    subprocess.run(["python3", script], check=True)
print("M31B_BATCH_APPLIED")
