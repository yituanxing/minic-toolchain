#!/usr/bin/env python3
"""Self-test for tools/ci/check-linux-runtime-log.py."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
GATE = ROOT / "tools/ci/check-linux-runtime-log.py"


def run_case(text: str, *args: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory() as tmp:
        log = Path(tmp) / "qemu.log"
        log.write_text(text)
        return subprocess.run(
            [sys.executable, str(GATE), str(log), *args],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


good_init = """[    0.400000] Run /init as init process
USER_SHELL_OK
DONE_COMMANDS
"""
proc = run_case(good_init, "--profile", "initramfs-init")
require(proc.returncode == 0, proc.stderr)

bad_init = good_init + "Kernel panic - not syncing: test\n"
proc = run_case(bad_init, "--profile", "initramfs-init")
require(proc.returncode == 1, "panic must fail runtime gate")

good_module = """MODULE_PASS=145
FUNCTION_PASS=203
FUNCTION_FAIL=0
M38_FINAL145_V028_END pass=203 fail=0
reboot: Power down
"""
proc = run_case(
    good_module,
    "--profile",
    "poweroff",
    "--endpoint",
    "M38_FINAL145_V028_END",
    "--expect-module-pass",
    "145",
    "--expect-function-pass",
    "203",
    "--expect-function-fail",
    "0",
    "--qemu-rc",
    "0",
)
require(proc.returncode == 0, proc.stderr)

wrong_count = good_module.replace("FUNCTION_PASS=203", "FUNCTION_PASS=202")
proc = run_case(
    wrong_count,
    "--expect-function-pass",
    "203",
)
require(proc.returncode == 1, "wrong functional count must fail")

print("PASS linux runtime log gate self-test")
