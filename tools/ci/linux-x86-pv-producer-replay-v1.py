#!/usr/bin/env python3
"""DIAGNOSTIC only: one historical x86 MiniC cohort with current PV producers.

The previous 2758 C objects are *not* recompiled by the current MiniC.  All
seven authenticated old producer shards are reused, and only actual ELF
.parainstructions owners with stale 3-byte patch sites may change epoch.
Never classify the mixed compiler cohort as current-HEAD Linux acceptance.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys


OWNER = "arch/x86/kernel/alternative.o"  # Consumer; MUST NOT be a producer.
OLD_RUN = 42384023426
OLD_HEAD = "8972cd6"
MAX_PRODUCERS = 32


def modules():
    import importlib.util

    def load(name: str, filename: str):
        path = Path(__file__).with_name(filename)
        spec = importlib.util.spec_from_file_location(name, path)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"missing local original verifier: {path}")
        obj = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(obj)
        return obj

    return (load("x86_pv_old_seven", "linux-three-isa-seven-runtime-v1.py"),
            load("x86_pv_old_candidate", "linux-three-isa-minic-candidate-v1.py"),
            load("x86_pv_gnu_oracle", "linux-three-isa-gnu-qemu-v1.py"),
            load("x86_pv_semantic_diff", "linux-four-isa-runtime-diff-v1.py"))


def pv_lengths(path: Path) -> Counter[int]:
    """Read the ELF64 x86 .parainstructions (16-byte aligned ABI records)."""
    raw = path.read_bytes()
    if len(raw) < 64 or raw[:6] != b"\x7fELF\x02\x01":
        raise ValueError(f"invalid ELF64 little endian object: {path}")
    if struct.unpack_from("<H", raw, 18)[0] != 62:
        raise ValueError(f"non-x86_64 ELF: {path}")
    off = struct.unpack_from("<Q", raw, 40)[0]
    entsize, count, strings = struct.unpack_from("<HHH", raw, 58)
    if entsize < 64 or not 0 < count < 65535 or strings >= count:
        raise ValueError(f"malformed ELF section table: {path}")
    if off + entsize * count > len(raw):
        raise ValueError(f"out-of-range ELF section table: {path}")

    def header(index: int) -> tuple[int, int, int]:
        at = off + index * entsize
        name = struct.unpack_from("<I", raw, at)[0]
        kind = struct.unpack_from("<I", raw, at + 4)[0]
        dataoff, size = struct.unpack_from("<QQ", raw, at + 24)
        # SHT_NOBITS (.bss) consumes virtual bytes but no on-disk payload.
        if kind != 8 and dataoff + size > len(raw):
            raise ValueError(f"out-of-range ELF section bytes: {path}")
        return name, dataoff, size

    _, stringoff, stringsize = header(strings)
    names = raw[stringoff:stringoff + stringsize]
    for index in range(count):
        nameoff, dataoff, size = header(index)
        if nameoff >= len(names):
            raise ValueError(f"corrupt ELF string table: {path}")
        end = names.find(b"\x00", nameoff)
        if end < 0:
            raise ValueError(f"unterminated ELF section name: {path}")
        if names[nameoff:end] != b".parainstructions":
            continue
        if size % 16:
            raise ValueError(f"non-16-byte PV records: {path}")
        return Counter(raw[dataoff + i + 9] for i in range(0, size, 16))
    return Counter()


def select_producers(rows: list[dict], staged: Path, selected_file: Path | None) -> tuple[list[str], dict]:
    old = {}
    for row in rows:
        name = row["object"]
        counts = pv_lengths(staged / name)
        if counts:
            old[name] = dict(sorted(counts.items()))
    observed = sorted(name for name, counts in old.items() if counts.get(3, 0))
    if not observed or len(observed) > MAX_PRODUCERS or OWNER in observed:
        raise ValueError(f"PV producer census unsafe: count={len(observed)} "
                         f"contains_consumer={OWNER in observed}")
    selected = observed
    if selected_file is not None:
        proposed = json.loads(selected_file.read_text())
        if (not isinstance(proposed, list) or
                any(not isinstance(name, str) for name in proposed) or
                len(proposed) != len(set(proposed)) or set(proposed) != set(observed)):
            raise ValueError(f"explicit producer list differs from old ELF 3B PV site census: "
                             f"observed={observed!r}, proposed={proposed!r}")
        selected = proposed
    return selected, {"old_site_lengths": old, "old_3byte_producers": observed}


def compile_producers(a, seven, candidate, rows, data, results, selected, receipt):
    by_name = {row["object"]: row for row in rows}
    by_result = {row["object"]: row for row in results}
    new_pair = candidate.compiler_pair(a.new_minic)
    old_pair = candidate.compiler_pair(a.old_minic, data)
    if new_pair == old_pair:
        raise ValueError("current and historical compiler binary pairs are identical")
    receipt.update(new_compiler=new_pair, old_compiler=old_pair)
    original = a.gnu_work / "build"
    source = a.gnu_work / "frozen"
    rebuild = a.output / "new-producers"
    rebuild.mkdir()
    oracle = candidate.frozen_kbuild_oracle()
    for name in selected:
        row = by_name[name]
        owner = Path(name)
        prefix = rebuild / owner.with_suffix("")
        prefix.parent.mkdir(parents=True, exist_ok=True)
        fresh_i = prefix.with_suffix(".gnu.i")
        fresh_s = prefix.with_suffix(".minic.s")
        fresh_o = prefix.with_suffix(".minic.o")
        cmd = original / owner.parent / ("." + owner.name + ".cmd")
        if candidate.sha(candidate.required(cmd)) != row["cmd_sha256"]:
            raise ValueError(f"historical GNU .o.cmd SHA changed: {name}")
        args, flags, as_flags, _ = oracle.gnu_argv(
            cmd.read_text(), data["gcc"], name, fresh_i, "x86_64")
        if (flags != candidate.effective_minic_flags(row, "x86_64") or
                as_flags != row["gnu_as_flags"]):
            raise ValueError(f"original GNU-E/MiniC/AS ABI flags drift: {name}")
        old_i = candidate.required(source / row["input"])
        if candidate.sha(old_i) != row["sha256"]:
            raise ValueError(f"historical original frozen GNU-E SHA drift: {name}")
        preprocessed = candidate.run_owner_process(args, 180, cwd=original)
        if preprocessed.returncode or not fresh_i.exists() or candidate.sha(fresh_i) != row["sha256"]:
            raise ValueError(f"fresh original GNU -E changed frozen input for {name}: "
                             f"{preprocessed.stderr[-300:]}")
        cc = candidate.run_owner_process(
            [str(a.new_minic), "--target=x86_64-linux", *flags,
             "-S", str(fresh_i), "-o", str(fresh_s)], 180)
        if cc.returncode or not fresh_s.is_file() or not fresh_s.stat().st_size:
            raise ValueError(f"current MiniC -S failed for {name}: {cc.stderr[-300:]}")
        assembler = candidate.run_owner_process(
            candidate.gnu_assembler_command(data["gcc"], row, fresh_s, fresh_o),
            180, cwd=original)
        if assembler.returncode or not fresh_o.is_file() or not fresh_o.stat().st_size:
            raise ValueError(f"original GNU AS failed for {name}: {assembler.stderr[-300:]}")
        candidate.elf_machine(fresh_o, data["machine"])
        candidate.validate_original_abi(fresh_o, "x86_64", row)
        before = pv_lengths(a.output / "staged" / name)
        after = pv_lengths(fresh_o)
        # This must be a real repaired producer, not the alternative.o
        # consumer nor an unrelated C owner falsely credited with the fix.
        if not before[3] or after[3] >= before[3] or after[5] <= before[5]:
            raise ValueError(f"current MiniC did not repair old PV 3B to 5B owner {name}: "
                             f"old={dict(before)}, new={dict(after)}")
        prior = by_result[name]["obj_sha256"]
        staged = a.output / "staged" / name
        shutil.copy2(fresh_o, staged)
        by_result[name] = {
            "object": name, "input_sha256": row["sha256"],
            "status": "MINIC_S_GNU_AS_PASS", "asm_sha256": candidate.sha(fresh_s),
            "obj_sha256": candidate.sha(staged),
        }
        receipt.setdefault("current_compiler_producers", []).append({
            "object": name, "historical_object_sha256": prior,
            "current_object_sha256": candidate.sha(staged),
            "gnu_e_sha256": candidate.sha(fresh_i),
            "original_cmd_sha256": row["cmd_sha256"],
            "old_pv_site_lengths": dict(before), "new_pv_site_lengths": dict(after),
        })
    return [by_result[row["object"]] for row in rows]


def run(a) -> int:
    seven, candidate, qemu, diff = modules()
    if a.output.exists():
        raise ValueError("refuse stale x86 PV diagnostic output directory")
    a.output.mkdir(parents=True)
    receipt = {
        "schema": "minic-x86-historical-pv-producer-replay-v1",
        "target": "x86_64", "historical_run": OLD_RUN,
        "historical_commit_prefix": OLD_HEAD,
        "current_head": a.new_source_sha,
        "diagnostic_only": True,
        "mixed_compiler_epochs": True,
        "current_head_strict_acceptance_allowed": False,
        "verdict": "DIAGNOSTIC_INCOMPLETE",
    }
    try:
        current_head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], text=True, cwd=Path(__file__).resolve().parents[2]).strip()
        if current_head != a.new_source_sha or not re.fullmatch(r"[0-9a-f]{40}", current_head):
            raise ValueError("current compiler source HEAD is not the explicit CI checkout SHA")
        old = argparse.Namespace(target="x86_64", minic=a.old_minic, incoming=a.incoming,
                                 gnu_work=a.gnu_work, output=a.output)
        seven.preflight(old, candidate)
        golden = seven.json_file(a.gnu_receipt)
        rows, data = candidate.candidate_inputs("x86_64", a.gnu_work,
                                                a.old_minic, golden, a.initramfs)
        identity = {
            "frozen_manifest_sha256": seven.file_sha(a.gnu_work / "frozen/manifest.json"),
            "gnu_receipt_sha256": seven.pinned_gnu_source_receipt_sha(
                a.incoming, "x86_64", golden["image_sha256"]),
            "gnu_c_owner_manifest_sha256": data["gnu_c_owner_manifest_sha256"],
            "original_gnu_image_sha256": golden["image_sha256"],
        }
        original_results = seven.collect(old, candidate, rows, identity)
        selected, census = select_producers(rows, a.output / "staged", a.producer_owner_file)
        receipt.update(old_shard_identity=identity, owner_count=len(rows),
                       old_epoch_owner_count=len(rows) - len(selected),
                       new_epoch_owner_count=len(selected),
                       selected_producer_owners=selected,
                       pre_relink_pv_census=census)
        print(f"X86_PV_OLD_COHORT=PASS old_owners={len(rows)} "
              f"current_producers={len(selected)} producers={selected!r}", flush=True)
        results = compile_producers(a, seven, candidate, rows, data,
                                    original_results, selected, receipt)
        (a.output / "mixed-compiler-C-object-results.json").write_text(
            json.dumps(results, indent=2) + "\n")
        shutil.copy2(a.gnu_receipt, a.output / "gnu-qemu-receipt.json")
        # Existing original GNU Kbuild relinker rearchives all authenticated
        # old objects and the few new producers; all later generated C epochs
        # are recompiled by the historical MiniC (never GCC as C fallback).
        # A minimal genuine Kbuild generated-source wave is admitted only for
        # compressed boot owners and these PV producers. An unexpected large
        # new C compilation cohort must stop here instead of silently growing
        # into a 2758-owner rerun or accepting a GNU C fallback.
        allowed_live = frozenset(
            row["object"] for row in rows
            if row["object"].startswith("arch/x86/boot/compressed/") or
            row["object"] in selected)
        if len(allowed_live) > 50:
            raise ValueError("unbounded postlink generated-source whitelist")
        receipt["allowed_historical_live_C_owners"] = sorted(allowed_live)
        image = candidate.relink(a.gnu_work, a.output, rows, results,
                                 data, golden, compiler=a.old_minic,
                                 allowed_live_c_owners=allowed_live)
        selected_sha = {r["object"]: r["obj_sha256"] for r in results
                        if r["object"] in selected}
        for name, expected in selected_sha.items():
            if candidate.sha(candidate.required(a.gnu_work / "build" / name)) != expected:
                raise ValueError(f"current PV producer was overwritten by live GNU Kbuild: {name}")
        # GNU C regens of unrelated owners are admitted only when existing
        # relink() wrote a complete authenticated original-MiniC epoch receipt.
        receipt["historical_live_owner_epochs"] = sorted(path.name for path in
            a.output.glob("postlink-live-gnu-e-minic-c*-receipt.json"))
        receipt["candidate_image_sha256"] = candidate.sha(image)
        boot = a.output / "boot"
        boot.mkdir()
        try:
            lane = qemu.run_lane("x86_64", qemu.executable("qemu-system-x86_64"),
                                 image, a.initramfs, None, "p0", "2G",
                                 a.qemu_timeout, boot)
            receipt["p0_lane"] = lane
            events = diff.compare((a.gnu_boot_logs / "p0.log").read_text(errors="replace"),
                                  (boot / "p0.log").read_text(errors="replace"),
                                  "p0", "6.6.143")
            receipt["p0_semantic_events"] = events
            receipt["verdict"] = "DIAGNOSTIC_MIXED_COMPILER_P0_MATCHES_GNU_NOT_STRICT"
            print("X86_PV_PRODUCER_REPLAY=P0_MATCHES_GNU diagnostic_only=true "
                  f"new_producers={len(selected)} historical_owners={len(rows)-len(selected)}",
                  flush=True)
            return 0
        except (qemu.CertificationError, diff.RuntimeMismatch) as exc:
            receipt["p0_error"] = str(exc)
            receipt["verdict"] = "DIAGNOSTIC_MIXED_COMPILER_P0_FAIL"
            return 2
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError,
            candidate.Rejected) as exc:
        receipt["error"] = str(exc)
        receipt["verdict"] = "DIAGNOSTIC_INVALID_OR_INCOMPLETE"
        return 2
    finally:
        (a.output / "x86-pv-producer-replay-receipt.json").write_text(
            json.dumps(receipt, indent=2, sort_keys=True) + "\n")
        print(f"X86_PV_PRODUCER_REPLAY={receipt['verdict']} "
              "strict_current_head_linux=false", flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gnu-work", required=True, type=Path)
    parser.add_argument("--gnu-receipt", required=True, type=Path)
    parser.add_argument("--gnu-boot-logs", required=True, type=Path)
    parser.add_argument("--initramfs", required=True, type=Path)
    parser.add_argument("--incoming", required=True, type=Path)
    parser.add_argument("--old-minic", required=True, type=Path)
    parser.add_argument("--new-minic", required=True, type=Path)
    parser.add_argument("--new-source-sha", required=True)
    parser.add_argument("--producer-owner-file", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--qemu-timeout", type=int, default=240)
    a = parser.parse_args(argv)
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
