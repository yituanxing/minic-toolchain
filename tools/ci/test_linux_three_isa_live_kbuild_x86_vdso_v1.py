#!/usr/bin/env python3
"""Check real x86 GNU postlink C regeneration without accepting GCC fallback.

Optional --artifact is original run #41105364733's exact x86 Actions ZIP.
Only the producer's *second* GNU Kbuild + final complete C SHA census can
certify the repair; this diagnostic does not claim QEMU/Linux acceptance.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import zipfile


SCRIPT = Path(__file__).with_name("linux-three-isa-minic-candidate-v1.py")
spec = importlib.util.spec_from_file_location("minic_live_kbuild", SCRIPT)
assert spec and spec.loader
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)

OWNERS = [
    "arch/x86/boot/compressed/kaslr.o",
    "arch/x86/boot/compressed/misc.o",
    "arch/x86/entry/vdso/vdso-image-64.o",
]
PREFIX = "minic-linux-gnu-runtime-x86_64/candidate/"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path)
    args = parser.parse_args()
    report = {"target": "x86_64", "unexpected_gnu_c_owners": [],
              "missing_minic_c_owners": [], "c_owner_hash_mismatches": OWNERS}
    link = ("  VDSO2C  arch/x86/entry/vdso/vdso-image-64.c\n"
            "  CC      arch/x86/entry/vdso/vdso-image-64.o\n"
            "  LD      vmlinux\n"
            "  OBJCOPY arch/x86/boot/compressed/vmlinux.bin\n"
            "  CC      arch/x86/boot/compressed/kaslr.o\n"
            "  CC      arch/x86/boot/compressed/misc.o\n"
            "  BUILD   arch/x86/boot/bzImage\n")
    if args.artifact:
        with zipfile.ZipFile(args.artifact) as archive:
            report = json.loads(archive.read(PREFIX + "postlink-c-owner-census.json"))
            link = archive.read(PREFIX + "gnu-ld-relink.log").decode()
            receipt = json.loads(archive.read(PREFIX + "candidate-receipt.json"))
        assert receipt["minic_s_gnu_as_pass"] == receipt["gnu_c_owners"] == 2758
        assert report["expected_c_owners"] == report["actual_c_owners"] == 2758
        assert report["c_owner_hash_mismatches"] == OWNERS
        assert report["unexpected_gnu_c_owners"] == report["missing_minic_c_owners"] == []
        assert link.index("VDSO2C  arch/x86/entry/vdso/vdso-image-64.c") < link.index(
            "CC      arch/x86/entry/vdso/vdso-image-64.o")
        assert link.index("LD      vmlinux\n") < link.index(
            "CC      arch/x86/boot/compressed/kaslr.o")
        assert "BUILD   arch/x86/boot/bzImage" in link
    expected = {name: "a" * 64 for name in OWNERS}
    rows = [{"object": name} for name in OWNERS]
    assert candidate.live_kbuild_recompiled_owner_names(
        report, rows, expected, "x86_64", link) == OWNERS
    tests = 0
    for edited, log, owners, rows_here, target in [
        (report, "  LD      vmlinux\n", expected, rows, "x86_64"),
        (dict(report, unexpected_gnu_c_owners=["kernel/gnu.o"]), link,
         expected, rows, "x86_64"),
        (dict(report, missing_minic_c_owners=[OWNERS[0]]), link,
         expected, rows, "x86_64"),
        (dict(report, c_owner_hash_mismatches=OWNERS + OWNERS[:1]), link,
         expected, rows, "x86_64"),
        (report, link, expected, rows, "aarch64"),
        (report, link, {OWNERS[0]: expected[OWNERS[0]]}, rows, "x86_64"),
        (report, link, expected, rows[:1], "x86_64"),
        (dict(report, c_owner_hash_mismatches=["unknown/hidden.o"]), link,
         expected, rows, "x86_64"),
    ]:
        try:
            candidate.live_kbuild_recompiled_owner_names(
                edited, rows_here, owners, target, log)
        except candidate.Rejected:
            tests += 1
        else:
            raise AssertionError("unsafe postlink source regeneration accepted")
    print("X86_LIVE_VDSO_COMPRESSED_C_REGEN=PASS "
          f"derived={len(OWNERS)} negative={tests} "
          f"artifact={'AUTHENTIC_41105364733' if args.artifact else 'MODEL'} "
          "GNU_LD_PASS_POSTLINK_SHA_BLOCKED second_relink_NOT_YET_PROVEN")


if __name__ == "__main__":
    main()
