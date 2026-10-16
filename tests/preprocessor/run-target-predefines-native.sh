#!/usr/bin/env bash
# Native C MiniPP target macro producer vs pinned Python reference algorithm.
# Python is TEST ORACLE ONLY. Production must invoke minic-cpp directly.
set -Eeuo pipefail
if (( $# != 1 )); then
  echo "usage: $0 </path/to/minic-cpp>" >&2
  exit 64
fi
mini_cpp=$(realpath "$1")
repo=$(cd "$(dirname "$0")/../.." && pwd)
"$mini_cpp" --dump-target-predefines --self-check
python3 - "$repo" "$mini_cpp" <<'PY'
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

root = Path(sys.argv[1])
mini_cpp = sys.argv[2]
oracle = runpy.run_path(str(root / "preprocessor/target/riscv_linux_predefines.py"))
snapshot = oracle["load_snapshot"](
    root / "tests/preprocessor/profiles/rv64-linux-6.6.143-gcc13.json.gz.b64"
)
profile_count = 0
with tempfile.TemporaryDirectory(prefix="minipp-native-target-") as td:
    output = Path(td) / "macros.h"
    for row in snapshot["profiles"]:
        args = [
            mini_cpp, "--dump-target-predefines",
            "--language", row["language"], "--output", str(output),
            "--", *row["flags"]
        ]
        proc = subprocess.run(args, text=True, capture_output=True)
        assert proc.returncode == 0, (row["flags"], proc.stderr)
        native = output.read_text()
        expected, _policy = oracle["synth"](
            snapshot, row["language"], row["flags"]
        )
        assert native == expected, (
            "native-C-vs-Python-reference-bytes",
            row["language"], row["flags"],
            hashlib.sha256(native.encode()).hexdigest(),
            hashlib.sha256(expected.encode()).hexdigest(),
        )
        assert oracle["macro_map"](native) == oracle["macro_map"](row["content"])
        profile_count += 1

    negative = [
        ("c", ["-march=rv32g"]),
        ("c", list(oracle["C_COMMON"]) + ["-O2", "-O2"]),
        ("c", list(oracle["C_COMMON"]) + ["-fPIC", "-fpie"]),
        ("assembler-with-cpp", list(oracle["S64"]) + ["-march=rv64i"]),
    ]
    for lang, flags in negative:
        proc = subprocess.run(
            [mini_cpp, "--dump-target-predefines",
             "--language", lang, "--output", str(output),
             "--", *flags],
            text=True, capture_output=True,
        )
        assert proc.returncode != 0, (lang, flags)
        assert "MINIPP_NATIVE_MACRO_RULES=FAIL" in proc.stderr
print(
    f"MINIPP_NATIVE_C_RULES=PASS contexts={profile_count} "
    f"invalid_contexts={len(negative)} "
    "byte_identical_to_certified_Python=true "
    "GNU_processes=0 native_runtime_Python_processes=0"
)
PY
