#!/usr/bin/env python3
"""Original AArch64 6.6.143 postlink vDSO-derived-owner forensic replay.

Optional real input: GitHub Actions run 41105364733, artifact 13002525967.
The archived initial GNU link already succeeded; four C objects were rebuilt
by Kbuild after a MiniC-linked vDSO regenerated vdso-offsets.h.  Tests below
exercise the generic fail-closed C-owner/log gate against that real artifact.
This test intentionally cannot certify the yet-to-run second GNU link/QEMU.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile


ARTIFACT_SHA256 = "7171dcf56ae3377c5852f167b108a9e714de2c8c1f8022871a2dacf534e0a70a"
BASE = "minic-linux-gnu-runtime-aarch64/candidate/"
RECOMPILED = (
    "arch/arm64/kernel/alternative.o",
    "arch/arm64/kernel/signal.o",
    "arch/arm64/kernel/signal32.o",
    "arch/arm64/kernel/vdso.o",
)


def candidate_module():
    path = Path(__file__).with_name("linux-three-isa-minic-candidate-v1.py")
    spec = importlib.util.spec_from_file_location("candidate_postlink_fixture", path)
    if spec is None or spec.loader is None:
        raise AssertionError("candidate module absent")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rejects(func, *args):
    try:
        func(*args)
    except ValueError:
        return
    raise AssertionError("forged or unproven GNU postlink C owner was accepted")


def check_gate(candidate, first: dict, rows: list[dict], expected: dict,
               first_log: str) -> int:
    assert tuple(candidate.live_kbuild_recompiled_owner_names(
        first, rows, expected, "aarch64", first_log)) == RECOMPILED
    checked = 0
    for name in RECOMPILED:
        # An object hash mismatch alone is NOT a genuine Kbuild recompile.
        fake = first_log.replace(f"CC      {name}", f"AR      {name}")
        rejects(candidate.live_kbuild_recompiled_owner_names,
                first, rows, expected, "aarch64", fake)
        checked += 1
    fake = copy.deepcopy(first)
    fake["c_owner_hash_mismatches"].append(RECOMPILED[0])
    rejects(candidate.live_kbuild_recompiled_owner_names,
            fake, rows, expected, "aarch64", first_log)
    checked += 1
    fake = copy.deepcopy(first)
    fake["c_owner_hash_mismatches"].append("arch/arm64/kernel/not-an-owner.o")
    rejects(candidate.live_kbuild_recompiled_owner_names,
            fake, rows, expected, "aarch64", first_log)
    checked += 1
    fake = copy.deepcopy(first)
    fake["unexpected_gnu_c_owners"] = ["arch/arm64/kernel/new-gnu-owner.o"]
    rejects(candidate.live_kbuild_recompiled_owner_names,
            fake, rows, expected, "aarch64", first_log)
    checked += 1
    fake = copy.deepcopy(first)
    fake["missing_minic_c_owners"] = [RECOMPILED[0]]
    rejects(candidate.live_kbuild_recompiled_owner_names,
            fake, rows, expected, "aarch64", first_log)
    checked += 1
    fake = copy.deepcopy(first)
    fake["target"] = "x86_64"
    rejects(candidate.live_kbuild_recompiled_owner_names,
            fake, rows, expected, "aarch64", first_log)
    checked += 1
    return checked


def symbol_offset(blob: bytes, label: str) -> int:
    # GNU readelf understands the exact original ELF64 objects in the archive.
    with tempfile.TemporaryDirectory(prefix="aarch-kbuild-vdso-") as td:
        path = Path(td) / "vdso.o"
        path.write_bytes(blob)
        listing = subprocess.check_output(["readelf", "-W", "-s", str(path)],
                                          text=True)
    addresses = []
    for line in listing.splitlines():
        cols = line.split()
        if len(cols) >= 8 and cols[-1] == label and cols[3] == "FUNC" and cols[4] == "GLOBAL":
            addresses.append(int(cols[1], 16))
    if len(addresses) != 1:
        raise AssertionError(f"expected exactly one original {label} ELF symbol")
    return addresses[0]


def real_artifact(path: Path) -> None:
    if hashlib.sha256(path.read_bytes()).hexdigest() != ARTIFACT_SHA256:
        raise AssertionError("unexpected source SHA: not run 41105364733 AArch64 artifact")
    with zipfile.ZipFile(path) as archive:
        first = json.loads(archive.read(BASE + "postlink-c-owner-census.json"))
        results = json.loads(archive.read(BASE + "all-c-owners-result.json"))
        log = archive.read(BASE + "gnu-ld-relink.log").decode("utf-8")
        prefix = BASE + "vdso-diagnostics/arch/arm64/kernel/vdso/vgettimeofday/"
        gnu = archive.read(prefix + "gnu.o")
        minic = archive.read(prefix + "minic.o")
    assert len(results) == 4108
    assert all(row["status"] == "MINIC_S_GNU_AS_PASS" for row in results)
    assert tuple(first["c_owner_hash_mismatches"]) == RECOMPILED
    assert (first["expected_c_owners"], first["actual_c_owners"]) == (4108, 4108)
    assert (first["unexpected_gnu_c_owners"], first["missing_minic_c_owners"]) == ([], [])
    assert "VDSOSYM include/generated/vdso-offsets.h" in log
    assert "LD      arch/arm64/kernel/vdso/vdso.so.dbg" in log
    assert "OBJCOPY arch/arm64/boot/Image" in log
    assert log.index("VDSOSYM include/generated/vdso-offsets.h") < log.index(
        "CC      arch/arm64/kernel/signal.o")
    rows = [{"object": row["object"]} for row in results]
    expected = {row["object"]: row["obj_sha256"] for row in results}
    negatives = check_gate(candidate_module(), first, rows, expected, log)
    # This proves the linked object layout is NOT interchangeable with GNU's;
    # VDSO_sigtramp in sigreturn.o is linked after vgettimeofday.o.
    gnu_offset = symbol_offset(gnu, "__kernel_gettimeofday")
    minic_offset = symbol_offset(minic, "__kernel_gettimeofday")
    assert (gnu_offset, minic_offset) == (0x294, 0x511c)
    print("AARCH64_VDSOSYM_ORIGINAL_ARTIFACT=PASS run=41105364733 "
          f"owners=4108 derived=4 GNU_text_offset=0x{gnu_offset:x} "
          f"MiniC_text_offset=0x{minic_offset:x} log_negatives={negatives} "
          "second_GNU_relink_NOT_YET_PROVEN")


def synthetic() -> None:
    # Distinct generated-header epochs must not silently reuse the frozen i.
    original_header = b"#define vdso_offset_sigtramp 0x180\n"
    newly_linked_header = b"#define vdso_offset_sigtramp 0x5120\n"
    sample = b'#include "vdso-offsets.h"\nunsigned long sigreturn_offset = vdso_offset_sigtramp;\n'
    with tempfile.TemporaryDirectory(prefix="aarch-vdso-epoch-") as td:
        root = Path(td)
        (root / "input.c").write_bytes(sample)
        generated = root / "vdso-offsets.h"
        inputs = []
        for head in (original_header, newly_linked_header):
            generated.write_bytes(head)
            result = subprocess.run(["gcc", "-E", "-P", "input.c"], cwd=root,
                                    text=True, capture_output=True, check=True)
            inputs.append(result.stdout.encode())
    assert inputs[0] != inputs[1]
    assert b"0x180" in inputs[0] and b"0x5120" in inputs[1]
    assert hashlib.sha256(inputs[0]).hexdigest() != hashlib.sha256(inputs[1]).hexdigest()
    print("AARCH64_VDSOSYM_DERIVED_SOURCE_FIXTURE=PASS source_epochs=2 GNU_E=2")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--artifact", type=Path,
                   help="optional original GitHub Actions artifact 13002525967 ZIP")
    args = p.parse_args()
    synthetic()
    if args.artifact:
        real_artifact(args.artifact)


if __name__ == "__main__":
    main()
