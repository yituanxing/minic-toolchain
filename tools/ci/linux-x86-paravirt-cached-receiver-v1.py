#!/usr/bin/env python3
"""GNU Kbuild LD + real QEMU P0/P1 for an authenticated 12-owner x86 overlay.

2746 unchanged x86 C owners come from original MiniC 8972/GNU AS and only
12 patched PV owners from current MiniC/GNU AS. Never advertise all-current
compiler provenance. Reuse the original 2758-owner seven-runner source and
object verifier and the original GNU Kbuild/link/QEMU acceptance machinery.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import zipfile


# One authoritative bound for both the GNU-Kbuild live-source replay and its
# postlink receipt.  Official PR #674 runs proved exactly these three C owners
# are regenerated, including VDSO2C -> CC vdso-image-64.o.  A new owner must
# be investigated and explicitly added; no GNU-C fallback is ever admitted.
ALLOWED_POSTLINK_LIVE_C = frozenset({
    "arch/x86/boot/compressed/misc.o",
    "arch/x86/boot/compressed/kaslr.o",
    "arch/x86/entry/vdso/vdso-image-64.o",
})


def load(sibling: str, name: str):
    source = Path(__file__).with_name(sibling)
    spec = importlib.util.spec_from_file_location(name, source)
    if spec is None or spec.loader is None:
        raise ValueError(f"no strict source verifier: {source}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--old-object-zip-dir", required=True, type=Path)
    p.add_argument("--old-minic", required=True, type=Path)
    p.add_argument("--minic", required=True, type=Path,
                   help="current MiniC driver for generated live postlink owners")
    p.add_argument("--overlay", required=True, type=Path)
    p.add_argument("--gnu-work", required=True, type=Path)
    p.add_argument("--gnu-receipt", required=True, type=Path)
    p.add_argument("--gnu-boot-logs", required=True, type=Path)
    p.add_argument("--initramfs", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--firmware", type=Path)
    a = p.parse_args()
    c = load("linux-three-isa-minic-candidate-v1.py", "pv_strict_receiver_core")
    s = load("linux-three-isa-seven-runtime-v1.py", "pv_strict_seven")
    pv = load("linux-x86-paravirt-cached-overlay-v1.py", "pv_strict_overlay")
    if a.output.exists():
        raise ValueError("refuse stale GNU Kbuild/QEMU output directory")
    a.output.mkdir(parents=True)
    result = {"schema": "minic-x86-paravirt-two-compiler-gnu-kbuild-qemu-v1",
              "original_c_owners": 2758, "prelink_historical_8972_owners": 2746,
              "prelink_current_fixed_owners": 12,
              "verdict": "NOT_RUNTIME_CERTIFIED"}
    try:
        # Attest exact authentic source and all object ZIPs before lifting any
        # objects into an original GNU Kbuild tree.
        for shard, expected in pv.OBJECT_SHAS.items():
            path = a.old_object_zip_dir / f"shard{shard}.zip"
            pv.valid(pv.file_sha(path) == expected,
                     f"historical artifact ZIP digest mismatch: shard {shard}")
        overlay_receipt = json.loads((a.overlay / "overlay-receipt.json").read_text())
        pv.valid(overlay_receipt["schema"] == "minic-x86-paravirt-cached-overlay-v1" and
                 overlay_receipt["original_minic_object_run"] == 42384023426 and
                 overlay_receipt["original_gnu_source_run"] == 42366441631 and
                 overlay_receipt["affected_owners"] == 12 and
                 overlay_receipt["old_c_owners"] == 2758 and
                 overlay_receipt["frozen_manifest_sha256"] == pv.FROZEN_SHA and
                 len(overlay_receipt["objects"]) == 12,
                 "foreign/partial two-compiler PV overlay receipt")
        old_compiler_pair = c.compiler_pair(a.old_minic)
        current_compiler_pair = c.compiler_pair(a.minic)
        pv.valid(old_compiler_pair != current_compiler_pair and
                 old_compiler_pair["compiler_cc_sha256"] ==
                     "cf8ad7e804224f4a2c49665ce387b78855f8985f3170383b1f846bcaa04462f1" and
                 old_compiler_pair["compiler_driver_sha256"] ==
                     "598db29a47de69e04f785d0632c47448619c9576d9e99d2e85a284e1f5067ae9",
                 "historical 8972 compiler is not the pinned official binary pair")
        pv.valid(current_compiler_pair["compiler_cc_sha256"] ==
                 overlay_receipt["new_compiler_sha256"],
                 "current live MiniC epoch differs from 12-owner patched compiler")
        # Restore all 2758 cached objects into an isolated incoming folder.
        # The same original seven.collect() then independently authenticates
        # every ELF owner, original compiler pair, GNU AS and original GNU
        # frozen manifest/source receipt before any ELF overlay.
        incoming = a.output / "original-seven-incoming"
        incoming.mkdir()
        for shard in range(7):
            with zipfile.ZipFile(a.old_object_zip_dir / f"shard{shard}.zip") as archive:
                folder = incoming / f"minic-linux-seven-objects-x86_64-{shard}"
                folder.mkdir()
                archive.extractall(folder)
        golden = json.loads(a.gnu_receipt.read_text())
        rows, data = c.candidate_inputs("x86_64", a.gnu_work, a.minic,
                                        golden, a.initramfs)
        identity = {
            "frozen_manifest_sha256": pv.file_sha(a.gnu_work / "frozen/manifest.json"),
            "gnu_receipt_sha256": s.pinned_gnu_source_receipt_sha(
                incoming, "x86_64", golden["image_sha256"]),
            "gnu_c_owner_manifest_sha256": data["gnu_c_owner_manifest_sha256"],
            "original_gnu_image_sha256": golden["image_sha256"]}
        pv.valid(identity["frozen_manifest_sha256"] == pv.FROZEN_SHA,
                 "fresh original GNU Kbuild frozen inputs differ from official source")
        a.output.joinpath("gnu-qemu-receipt.json").write_bytes(a.gnu_receipt.read_bytes())
        class CollectArgs:
            pass
        args = CollectArgs()
        args.incoming, args.target, args.minic, args.output = (
            incoming, "x86_64", a.old_minic, a.output)
        all_results = s.collect(args, c, rows, identity)
        by_name = {row["object"]: row for row in all_results}
        frozen = {row["object"]: row for row in rows}
        patched = set()
        for row in overlay_receipt["objects"]:
            name = row["object"]
            pv.valid(name in pv.EXPECTED_SITE_COUNTS and name not in patched and
                     name in frozen and row["shard"] == frozen[name]["shard"] and
                     row["frozen_i_sha256"] == frozen[name]["sha256"] and
                     row["original_cmd_sha256"] == frozen[name]["cmd_sha256"],
                     f"foreign target or original input in replacement: {name}")
            patched.add(name)
            old_obj = a.output / "staged" / name
            new_obj = a.overlay / "new-objects/staged" / name
            new_asm = a.overlay / "new-objects/staged" / str(Path(name).with_suffix(".s"))
            pv.valid(pv.file_sha(old_obj) == row["previous_obj_sha256"] and
                     by_name[name]["obj_sha256"] == row["previous_obj_sha256"] and
                     pv.file_sha(new_obj) == row["new_obj_sha256"] and
                     pv.file_sha(new_asm) == row["new_asm_sha256"],
                     f"corrupt original or overlay C object: {name}")
            sites = pv.elf_paravirt_sites(new_obj.read_bytes())
            pv.valid(len(sites) == pv.EXPECTED_SITE_COUNTS[name] and
                     all(length >= 5 for _typ, length in sites),
                     f"unsafe PV replacement site: {name}")
            c.elf_machine(new_obj, data["machine"])
            c.validate_original_abi(new_obj, "x86_64", frozen[name])
            shutil.copyfile(new_obj, old_obj)
            by_name[name]["obj_sha256"] = row["new_obj_sha256"]
            by_name[name]["asm_sha256"] = row["new_asm_sha256"]
            by_name[name]["two_compiler_pv_overlay"] = True
        pv.valid(patched == set(pv.EXPECTED_SITE_COUNTS),
                 "missing one or more patched PV owner")
        all_results = [by_name[row["object"]] for row in rows]
        a.output.joinpath("all-c-owners-result.json").write_text(
            json.dumps(all_results, indent=2) + "\n")
        pv.valid(c.compiler_pair(a.old_minic) == old_compiler_pair,
                 "historical compiler identity drift")
        pv.valid(c.compiler_pair(a.minic) == current_compiler_pair,
                 "current compiler identity drift")
        print("PV_TWO_COMPILER_PRELINK=SHA_PASS old=2746 patched=12 sites=16 "
              "no_GCC_C_fallback=1", flush=True)
        image = c.relink(a.gnu_work, a.output, rows, all_results,
                         data, golden,
                         allowed_live_c_owners=ALLOWED_POSTLINK_LIVE_C)
        live = []
        for receipt_path in sorted(a.output.glob("postlink-live-gnu-e-minic-c*receipt.json")):
            receipt = json.loads(receipt_path.read_text())
            for entry in receipt["live_recompiled"]:
                name = entry["object"]
                pv.valid(name in ALLOWED_POSTLINK_LIVE_C and
                         entry["actual_logged_gnu_cc"] and
                         entry["live_minic_object_sha256"],
                         f"unauthenticated live GNU-E/current MiniC owner: {name}")
                live.append(name)
        result["postlink_current_live_owners"] = sorted(set(live))
        result["postlink_current_live_count"] = len(set(live))
        result["postlink_historical_8972_remaining"] = 2746 - len(set(live))
        result["postlink_current_total_owners"] = 12 + len(set(live))
        c.boot_and_compare(data, a.output, a.initramfs, a.firmware,
                           image, golden, a.gnu_boot_logs)
        result.update(verdict="PV_TWO_COMPILER_GNU_LD_QEMU_P0_P1_PASS",
                      image_sha256=pv.file_sha(image),
                      original_8972_compiler=old_compiler_pair,
                      current_compiler=current_compiler_pair,
                      source_run=42366441631, old_objects_run=42384023426)
    except (OSError, ValueError, KeyError, c.Rejected, zipfile.BadZipFile) as error:
        result.update(verdict="PV_TWO_COMPILER_GNU_LD_QEMU_FAIL",
                      error=str(error)[:4096])
        raise
    finally:
        a.output.joinpath("candidate-receipt.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n")
        print(f"PV_TWO_COMPILER_RECEIVER={result['verdict']}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        sys.exit(f"PV_TWO_COMPILER_GNU_LD_QEMU_FAIL error={error}")
