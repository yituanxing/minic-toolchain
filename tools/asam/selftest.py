#!/usr/bin/env python3
"""Smoke test the read-only ASAM scanner against a throwaway C repository."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

scanner = Path(__file__).with_name("scan.py").resolve()
with tempfile.TemporaryDirectory() as td:
    repo = Path(td)
    (repo / "src" / "unit").mkdir(parents=True)
    source = repo / "src" / "unit" / "sample.c"
    original = (
        "static int asam_unused_sample(int x) { return x + 1; }\n"
        "static int asam_live_sample(int x) { return x * 2; }\n"
        "int asam_main_sample(void) { return asam_live_sample(3); }\n"
    )
    source.write_text(original, encoding="utf-8")
    process = subprocess.run(
        [sys.executable, str(scanner), "--repo", str(repo),
         "--scope", "src/unit", "--output", "out"],
        capture_output=True, text=True, check=False
    )
    if process.returncode:
        raise SystemExit("scan failed: " + process.stdout + process.stderr)
    data = json.loads((repo / "out" / "report.json").read_text())
    assert data["scope_files"] == 1, data
    assert data["functions"] >= 3, data
    assert data["global_reference_files"] == 1, data
    assert source.read_text() == original, "scanner changed production source"
    assert (repo / "out" / "symbols.json").is_file()
    assert (repo / "out" / "report.md").is_file()
print("ASAM selftest PASS: symbol inventory, reports, source unchanged")
