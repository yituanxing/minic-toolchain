#!/usr/bin/env python3
"""Diagnostic-only x86 Linux object swaps using a previously accepted GNU tree.

This is the RV cohort experiment, *not* another Linux acceptance path.  Call
``prepare`` on the pristine, SHA-attested GNU Kbuild output before the official
all-MiniC receiver changes it.  Following an official P0 failure, ``probe``
replays its already authenticated staged MiniC objects against an independent
copy of that GNU tree.  No MiniC compilation, preprocessing or assembler run
occurs here.  GNU Kbuild may regenerate derived C owners; every such owner and
its final hash is reported.  Mixed GCC/MiniC images can NEVER obtain a strict
MiniC acceptance verdict from this tool.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys


TARGET = "x86_64"
COMPRESSED_PREFIX = "arch/x86/boot/compressed/"
COMPRESSED_COUNT = 12
MODES = ("gnu-plus-compressed", "minic-except-compressed")
SCHEMA = "minic-three-isa-boot-cohort-diagnostic-v1"


class DiagnosticError(ValueError):
    pass


def load_sibling(filename: str, label: str):
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(label, path)
    if spec is None or spec.loader is None:
        raise DiagnosticError(f"missing existing GNU/MiniC oracle: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def file(path: Path) -> Path:
    if path.is_symlink() or not path.is_file() or not path.stat().st_size:
        raise DiagnosticError(f"missing, empty or symlinked evidence: {path}")
    return path


def directory(path: Path) -> Path:
    path = path.absolute()
    if path.is_symlink() or not path.is_dir() or path.resolve(strict=True) != path:
        raise DiagnosticError(f"missing or unsafe directory: {path}")
    return path


def read_json(path: Path):
    return json.loads(file(path).read_text())


def safe_owner(name: str) -> str:
    if not isinstance(name, str):
        raise DiagnosticError("non-string C owner")
    p = PurePosixPath(name)
    if p.is_absolute() or ".." in p.parts or len(p.parts) < 2 or p.suffix != ".o":
        raise DiagnosticError(f"unsafe original GNU C owner: {name!r}")
    return p.as_posix()


def source_rows(gnu_work: Path) -> list[dict]:
    manifest = read_json(gnu_work / "frozen/manifest.json")
    rows = manifest.get("rows")
    if not isinstance(rows, list) or len(rows) < 200:
        raise DiagnosticError("missing original complete Linux C-owner manifest")
    names = [safe_owner(row["object"]) for row in rows]
    if len(names) != len(set(names)):
        raise DiagnosticError("duplicate original GNU C owner")
    for index, row in enumerate(rows):
        if (row.get("index") != index or row.get("shard") != index % 7 or
                not re.fullmatch(r"[0-9a-f]{64}", row.get("sha256", ""))):
            raise DiagnosticError("noncanonical seven-way original C-owner manifest")
    return rows


def compressed_owners(rows: list[dict]) -> list[str]:
    """Refuse a guessed 12-owner selection if the genuine corpus differs."""
    chosen = [safe_owner(row["object"]) for row in rows
              if row["object"].startswith(COMPRESSED_PREFIX)]
    if len(chosen) != COMPRESSED_COUNT:
        raise DiagnosticError(f"expected exactly {COMPRESSED_COUNT} original x86 "
                              f"compressed C owners; found {len(chosen)}: {chosen[:20]}")
    return chosen


def select_owners(rows: list[dict], mode: str) -> list[str]:
    if mode not in MODES:
        raise DiagnosticError(f"unsupported diagnostic selection: {mode}")
    compressed = set(compressed_owners(rows))
    all_names = [safe_owner(row["object"]) for row in rows]
    selected = [name for name in all_names if
                (name in compressed if mode == "gnu-plus-compressed"
                 else name not in compressed)]
    if len(selected) != (COMPRESSED_COUNT if mode == MODES[0]
                         else len(rows) - COMPRESSED_COUNT):
        raise DiagnosticError("incomplete controlled mixed object selection")
    return selected


def copy_independent_tree(source: Path, target: Path) -> None:
    """Copy Kbuild output at the same eventual O= path without hardlinking."""
    directory(source)
    if target.exists() or target.is_symlink():
        raise DiagnosticError(f"refuse to overwrite an existing Kbuild snapshot: {target}")
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cp", "-a", "--reflink=auto", str(source), str(target)], check=True)
    for relative in (".config",):
        a, b = file(source / relative), file(target / relative)
        if (digest(a) != digest(b) or
                (a.stat().st_dev, a.stat().st_ino) == (b.stat().st_dev, b.stat().st_ino)):
            raise DiagnosticError("Kbuild snapshot shares an inode or altered bytes")


def verify_staged(rows: list[dict], staged: Path, result_path: Path,
                  machine_check=None) -> dict[str, str]:
    """Require the exact already-collected seven-shard result, never compile."""
    results = read_json(result_path)
    if not isinstance(results, list) or len(results) != len(rows):
        raise DiagnosticError("partial or missing official seven-shard object census")
    if machine_check is None:
        candidate = load_sibling("linux-three-isa-minic-candidate-v1.py", "boot_cohort_candidate")
        machine_check = lambda p, row: candidate.elf_machine(p, 62)
    verified = {}
    for row, result in zip(rows, results):
        name = safe_owner(row["object"])
        if (result.get("object") != name or
                result.get("status") != "MINIC_S_GNU_AS_PASS" or
                result.get("input_sha256") != row["sha256"] or
                not re.fullmatch(r"[0-9a-f]{64}", result.get("obj_sha256") or "")):
            raise DiagnosticError(f"unauthenticated seven-runner object receipt: {name}")
        obj = file(staged / name)
        if digest(obj) != result["obj_sha256"]:
            raise DiagnosticError(f"MiniC ELF object SHA changed: {name}")
        machine_check(obj, row)
        verified[name] = result["obj_sha256"]
    return verified


def verify_gnu_owner_snapshot(snapshot: Path, original: dict[str, str]) -> None:
    directory(snapshot)
    for name, expected in original.items():
        if digest(file(snapshot / safe_owner(name))) != expected:
            raise DiagnosticError(f"original pristine GNU C object drifted: {name}")


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    if tmp.exists() or path.exists():
        raise DiagnosticError(f"refuse to overwrite diagnostic receipt: {path}")
    tmp.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n")
    os.replace(tmp, path)


def prepare(a: argparse.Namespace) -> None:
    work = directory(a.gnu_work)
    snapshot_dir = a.snapshot_dir.resolve(strict=False)
    if snapshot_dir.exists() or snapshot_dir.is_symlink() or snapshot_dir.is_relative_to(work):
        raise DiagnosticError("snapshot must be a new directory outside GNU_WORK")
    out = directory(work / "build")
    golden_receipt = file(a.gnu_receipt.absolute())
    initramfs = file(a.initramfs.absolute())
    minic = file(a.minic.absolute())
    candidate = load_sibling("linux-three-isa-minic-candidate-v1.py", "boot_cohort_candidate")
    verifier = load_sibling("linux-three-isa-gnu-cache-verify-v1.py", "boot_cohort_gnu_cache")
    checked = verifier.verify_cache(TARGET, work, "gcc", golden_receipt)
    rows, details = candidate.candidate_inputs(TARGET, work, minic,
                                                read_json(golden_receipt), initramfs)
    if checked["owners"] != len(rows):
        raise DiagnosticError("GNU golden census differs from original seven-way owners")
    compressed = compressed_owners(rows)
    original = {safe_owner(row["object"]): digest(file(out / row["object"])) for row in rows}
    snapshot_dir.mkdir(parents=True)
    snapshot = snapshot_dir / "pristine-gnu-build"
    copy_independent_tree(out, snapshot)
    verify_gnu_owner_snapshot(snapshot, original)
    receipt = {"schema": SCHEMA, "phase": "prepared-pristine-gnu",
               "official_runtime_certified": False, "diagnostic_only": True,
               "target": TARGET, "gnu_work": str(work),
               "gnu_receipt": str(golden_receipt),
               "gnu_receipt_sha256": digest(golden_receipt),
               "gnu_p0_log": str(file(golden_receipt.parent / "p0.log")),
               "gnu_p0_log_sha256": digest(golden_receipt.parent / "p0.log"),
               "initramfs": str(initramfs), "initramfs_sha256": digest(initramfs),
               "source_sha256": details["source_sha256"],
               "config_sha256": details["config_sha256"],
               "gnu_image_sha256": details["golden_image_sha256"],
               "compiler_driver_sha256": details["compiler_driver_sha256"],
               "compiler_cc_sha256": details["compiler_cc_sha256"],
               "original_manifest_sha256": digest(work / "frozen/manifest.json"),
               "original_owner_count": len(rows),
               "compressed_owners": compressed,
               "original_gnu_c_owner_sha256": original}
    write_json(snapshot_dir / "prepare.json", receipt)
    print(f"MINIC_BOOT_COHORT_PREPARE=PASS target={TARGET} owners={len(rows)} "
          f"compressed={len(compressed)} snapshot=independent no_mini_compilation=true")


def reconcile_owners(rows: list[dict], original: dict[str, str],
                     minic: dict[str, str], selected: list[str],
                     actual: dict[str, str], make_log: str) -> dict:
    wanted = set(original)
    chosen = set(selected)
    current = set(actual)
    missing, added = sorted(wanted - current), sorted(current - wanted)
    selected_overwritten = sorted(name for name in chosen & current
                                  if actual[name] != minic[name])
    other_changed = sorted(name for name in (wanted - chosen) & current
                           if actual[name] != original[name])
    cc = re.findall(r"(?m)^\s*CC\s+(\S+\.o)\s*$", make_log)
    cc_set = set(cc)
    unlogged = sorted(name for name in other_changed if name not in cc_set)
    origin = {}
    for name, sha in sorted(actual.items()):
        if name not in wanted:
            origin[name] = "UNEXPECTED_C_OWNER"
        elif sha == minic.get(name) == original[name]:
            origin[name] = "BYTE_IDENTICAL_GNU_AND_MINIC"
        elif sha == minic.get(name):
            origin[name] = "MINIC_STAGED_SHA_MATCH"
        elif sha == original[name]:
            origin[name] = "ORIGINAL_GNU_SHA_MATCH"
        else:
            origin[name] = "LIVE_REBUILT_OR_UNRECOGNIZED_SHA"
    selected_actual_minic = sorted(name for name in chosen & current
                                   if origin[name] == "MINIC_STAGED_SHA_MATCH")
    selected_actual_gnu = sorted(name for name in chosen & current
                                 if origin[name] == "ORIGINAL_GNU_SHA_MATCH")
    selected_rebuilt_or_unknown = sorted(name for name in chosen & current
                                         if origin[name] == "LIVE_REBUILT_OR_UNRECOGNIZED_SHA")
    return {"original_owner_count": len(rows),
            "selected_minic_owner_count": len(selected),
            "mixed_gnu_owner_count": len(rows) - len(selected),
            "selected_overwritten": selected_overwritten,
            "other_rebuilt_c_owners": other_changed,
            "kbuild_logged_cc_owners": sorted(cc_set),
            "kbuild_cc_count": len(cc),
            "unlogged_other_owner_changes": unlogged,
            "missing_original_c_owners": missing,
            "unexpected_c_owners": added,
            "effective_postlink_c_owner_origin": origin,
            "selected_actual_minic_sha_matched": selected_actual_minic,
            "selected_actual_original_gnu_sha_matched": selected_actual_gnu,
            "selected_actual_rebuilt_or_unknown": selected_rebuilt_or_unknown,
            "final_c_owner_sha256": actual,
            "no_compiler_reinvocation_by_diagnostic": True,
            "zero_gnu_c_fallback": False,
            "strict_qualification_allowed": False}


def live_census(out: Path, evidence: Path) -> dict[str, str]:
    """Use the existing canonical Kbuild C-source classifier, not heuristics."""
    candidate = load_sibling("linux-three-isa-minic-candidate-v1.py", "boot_cohort_candidate")
    golden = candidate.module("boot_cohort_gnu_census", "linux-three-isa-golden-v1.py")
    common = candidate.module("boot_cohort_kbuild_owners", "linux-runtime-gcc-full-c-objects-v1.py")
    lines = []
    for obj in sorted(out.rglob("*.o")):
        if not obj.is_file() or obj.is_symlink():
            continue
        rel = obj.relative_to(out)
        parts = rel.parts
        if ((parts and parts[0] in ("tools", "scripts")) or
                (len(parts) >= 3 and parts[0] == "arch" and parts[2] == "tools")):
            continue
        if golden.classify_object(obj, out, TARGET) == "main":
            lines.append(f"{digest(obj)}  {rel.as_posix()}\n")
    artifact = evidence / "postlink-target-elf64-objects.sha256"
    artifact.write_text("".join(lines))
    owners, _ = common.examine(out, artifact)
    return {safe_owner(name): digest(file(out / name)) for name in owners}


def probe(a: argparse.Namespace) -> int:
    work = directory(a.gnu_work)
    snapshot_dir = directory(a.snapshot_dir)
    info = read_json(snapshot_dir / "prepare.json")
    if (info.get("schema") != SCHEMA or info.get("target") != TARGET or
            info.get("phase") != "prepared-pristine-gnu" or
            info.get("gnu_work") != str(work) or
            info.get("diagnostic_only") is not True):
        raise DiagnosticError("foreign/non-diagnostic pristine GNU snapshot")
    trial = a.trial_dir.resolve(strict=False)
    if trial.exists() or trial.is_relative_to(work):
        raise DiagnosticError("trial output must be new and outside GNU_WORK")
    if digest(file(Path(info["gnu_receipt"]))) != info["gnu_receipt_sha256"]:
        raise DiagnosticError("original actual GNU QEMU P0/P1 receipt changed")
    if (digest(file(Path(info["gnu_p0_log"]))) != info["gnu_p0_log_sha256"] or
            digest(file(Path(info["initramfs"]))) != info["initramfs_sha256"]):
        raise DiagnosticError("original GNU P0 log or initramfs changed")
    if digest(file(work / "frozen/manifest.json")) != info["original_manifest_sha256"]:
        raise DiagnosticError("original GNU C-owner manifest changed")
    candidate = load_sibling("linux-three-isa-minic-candidate-v1.py", "boot_cohort_candidate")
    compiler = candidate.compiler_pair(file(a.minic.absolute()))
    if any(compiler[k] != info[k] for k in ("compiler_driver_sha256", "compiler_cc_sha256")):
        raise DiagnosticError("MiniC producer revision differs from snapshot")
    rows = source_rows(work)
    if len(rows) != info["original_owner_count"] or compressed_owners(rows) != info["compressed_owners"]:
        raise DiagnosticError("changed original GNU x86 compressed C-owner domain")
    original = info["original_gnu_c_owner_sha256"]
    snapshot = directory(snapshot_dir / "pristine-gnu-build")
    if (digest(file(snapshot / ".config")) != info["config_sha256"] or
            digest(file(snapshot / candidate.TARGETS[TARGET][4])) != info["gnu_image_sha256"]):
        raise DiagnosticError("snapshot no longer contains pristine original GNU Image/config")
    verify_gnu_owner_snapshot(snapshot, original)
    staged = directory(a.staged)
    if staged.is_relative_to(work / "build") or a.results.resolve().is_relative_to(work / "build"):
        raise DiagnosticError("staged MiniC objects/results must remain outside replaced O=build")
    minic_objects = verify_staged(rows, staged, file(a.results),
                                  lambda path, row: candidate.elf_machine(path, 62))
    selected = select_owners(rows, a.mode)
    trial.mkdir(parents=True)
    original_backup = work / "build-before-cohort-diagnostic"
    if original_backup.exists() or original_backup.is_symlink():
        raise DiagnosticError("original official Kbuild backup exists; recover it first")
    record = {"schema": SCHEMA, "phase": "diagnostic-only-mixed-C-objects",
              "target": TARGET, "mode": a.mode, "diagnostic_only": True,
              "strict_qualification_allowed": False, "zero_gnu_c_fallback": False,
              "gnu_source_image_sha256": info["gnu_image_sha256"],
              "original_manifest_sha256": info["original_manifest_sha256"],
              "selected_minic_owners": selected,
              "selected_minic_sha256": {name: minic_objects[name] for name in selected},
              "original_official_build_restored": False,
              "verdict": "DIAGNOSTIC_INCOMPLETE"}
    original_moved = False
    try:
        # Kbuild .cmd contains original absolute O= paths. A trial in another
        # output directory would silently invalidate the real owner commands.
        # Never destroy the official P0-failed build: move it aside atomically
        # on the SAME filesystem, temporarily link in its original O= path,
        # and restore it in finally even when Kbuild or QEMU fails.
        out = directory(work / "build")
        if (out != work / "build" or out.is_symlink() or snapshot.is_relative_to(out)
                or original_backup.exists()):
            raise DiagnosticError("unsafe GNU Kbuild output replacement")
        out.rename(original_backup)
        original_moved = True
        copy_independent_tree(snapshot, out)
        for name in selected:
            destination = file(out / name)
            shutil.copyfile(file(staged / name), destination)
            os.utime(destination, None)  # force original incremental GNU Kbuild link
            if digest(destination) != minic_objects[name]:
                raise DiagnosticError(f"mixed MiniC object copy failed: {name}")
        record["before_link_selected_sha256"] = {
            name: digest(file(out / name)) for name in selected}
        command = ["make", "-C", str(work / "linux-6.6.143"), "O=" + str(out),
                   "ARCH=x86", "CROSS_COMPILE=", "CC=gcc", "-j4", "bzImage"]
        env = dict(os.environ, KBUILD_BUILD_USER="minic", KBUILD_BUILD_HOST="minic-ci",
                   KBUILD_BUILD_TIMESTAMP="Thu Jan  1 00:00:00 UTC 1970")
        link = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, env=env, timeout=a.link_timeout)
        (trial / "gnu-kbuild-mixed-link.log").write_text(link.stdout)
        record.update(link_rc=link.returncode, link_command=command,
                      link_log_sha256=digest(trial / "gnu-kbuild-mixed-link.log"))
        # The former GNU Image cannot accidentally qualify a no-op make.
        record["original_gnu_kbuild_relinked"] = bool(re.search(
            r"\bLD\b.*(?:vmlinux|Image)|\bAR\b.*built-in", link.stdout))
        actual = live_census(out, trial)
        record.update(reconcile_owners(rows, original, minic_objects, selected,
                                       actual, link.stdout))
        record["candidate_image_sha256"] = (digest(out / "arch/x86/boot/bzImage")
                                             if (out / "arch/x86/boot/bzImage").is_file() else None)
        if link.returncode:
            record["verdict"] = "DIAGNOSTIC_GNU_KBUILD_LINK_FAIL"
            return 2
        if not record["original_gnu_kbuild_relinked"]:
            record["verdict"] = "DIAGNOSTIC_INCONCLUSIVE_NO_GNU_LINK"
            return 2
        # Even if a live GNU-Kbuild C regeneration overwrites a selected
        # MiniC object, RUN P0: the resulting semantic markers are useful.
        # But the intended 12-vs-rest partition was no longer the tested
        # partition and must never be reported as a causal, passing cohort.
        drift = bool(record["selected_overwritten"] or
                     record["other_rebuilt_c_owners"] or
                     record["missing_original_c_owners"] or
                     record["unexpected_c_owners"] or
                     record["unlogged_other_owner_changes"] or
                     record["kbuild_cc_count"] > a.max_live_cc)
        record["intended_cohort_preserved_postlink"] = not drift
        record["owner_drift_detected"] = drift
        if record["candidate_image_sha256"] == info["gnu_image_sha256"]:
            record["verdict"] = "DIAGNOSTIC_INCONCLUSIVE_UNCHANGED_IMAGE"
            return 2
        oracle = load_sibling("linux-three-isa-gnu-qemu-v1.py", "boot_cohort_gnu_qemu")
        diff = load_sibling("linux-four-isa-runtime-diff-v1.py", "boot_cohort_semantic_diff")
        (trial / "boot").mkdir()
        try:
            lane = oracle.run_lane(TARGET, oracle.executable("qemu-system-x86_64"),
                                   file(out / "arch/x86/boot/bzImage"),
                                   Path(info["initramfs"]), None, "p0", "2G",
                                   a.qemu_timeout, trial / "boot")
            record["p0_lane"] = lane
            events = diff.compare(Path(info["gnu_p0_log"]).read_text(errors="replace"),
                                  (trial / "boot/p0.log").read_text(errors="replace"),
                                  "p0", "6.6.143")
            record["semantic_events"] = events
            record["p0_observation"] = "MATCHES_GNU_SEMANTIC_MARKERS"
            record["verdict"] = ("DIAGNOSTIC_INCONCLUSIVE_OWNER_DRIFT" if drift
                                 else "DIAGNOSTIC_P0_MATCHES_GNU_NOT_ACCEPTANCE")
            return 2 if drift else 0
        except (oracle.CertificationError, diff.RuntimeMismatch) as exc:
            record["p0_error"] = str(exc)
            record["p0_observation"] = "FAIL_OR_SEMANTIC_MISMATCH"
            if getattr(exc, "lane_receipt", None):
                record["p0_lane"] = exc.lane_receipt
            record["verdict"] = ("DIAGNOSTIC_INCONCLUSIVE_OWNER_DRIFT" if drift
                                 else "DIAGNOSTIC_P0_FAIL_OR_INCONCLUSIVE")
            return 2
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        record["error"] = str(exc)
        record["verdict"] = "DIAGNOSTIC_INVALID_OR_INCOMPLETE"
        return 2
    finally:
        if original_moved:
            # This newly copied mixed build is expendable; the official build
            # stays byte-for-byte intact in original_backup until restored.
            if (work / "build").exists():
                shutil.rmtree(work / "build")
            original_backup.rename(work / "build")
            record["original_official_build_restored"] = True
        write_json(trial / "diagnostic-receipt.json", record)
        print(f"MINIC_BOOT_COHORT_{a.mode.upper().replace('-', '_')}="
              f"{record['verdict']} selected={len(selected)} "
              f"strict_acceptance=false")
        # A 1 MiB owner census is retained in the artifact, but the failure
        # frontier must also be visible without downloading that ZIP. Only
        # bounded observed names are printed; no inferences or relaxed gates.
        print(f"MINIC_BOOT_COHORT_P0 mode={a.mode} "
              f"observation={record.get('p0_observation', 'NOT_RUN')} "
              f"owner_drift={str(record.get('owner_drift_detected', 'UNKNOWN')).lower()} "
              f"intended_partition={str(record.get('intended_cohort_preserved_postlink', 'UNKNOWN')).lower()} "
              f"kbuild_cc={record.get('kbuild_cc_count', 'UNKNOWN')} "
              "diagnostic_only=true")
        for label in ("selected_overwritten", "other_rebuilt_c_owners",
                      "selected_actual_rebuilt_or_unknown", "missing_original_c_owners",
                      "unexpected_c_owners"):
            owners = record.get(label, [])
            if not isinstance(owners, list):
                owners = []
            print(f"MINIC_BOOT_COHORT_OWNER_DRIFT mode={a.mode} category={label} "
                  f"count={len(owners)} first15="
                  f"{json.dumps(owners[:15], separators=(',', ':'))}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare", help="snapshot independently certified GNU Kbuild before official overlay")
    prep.add_argument("--gnu-work", type=Path, required=True)
    prep.add_argument("--gnu-receipt", type=Path, required=True)
    prep.add_argument("--initramfs", type=Path, required=True)
    prep.add_argument("--minic", type=Path, required=True)
    prep.add_argument("--snapshot-dir", type=Path, required=True)
    test = sub.add_parser("probe", help="never certify: mixed GNU/MiniC C cohort and P0")
    test.add_argument("--gnu-work", type=Path, required=True)
    test.add_argument("--snapshot-dir", type=Path, required=True)
    test.add_argument("--minic", type=Path, required=True)
    test.add_argument("--staged", type=Path, required=True)
    test.add_argument("--results", type=Path, required=True)
    test.add_argument("--trial-dir", type=Path, required=True)
    test.add_argument("--mode", choices=MODES, required=True)
    test.add_argument("--link-timeout", type=int, default=900)
    test.add_argument("--qemu-timeout", type=int, default=240)
    test.add_argument("--max-live-cc", type=int, default=32)
    a = p.parse_args(argv)
    try:
        if a.command == "prepare":
            prepare(a)
            return 0
        if not 1 <= a.link_timeout <= 3600 or not 30 <= a.qemu_timeout <= 900:
            raise DiagnosticError("invalid bounded GNU link/QEMU timeout")
        if not 0 <= a.max_live_cc <= 64:
            raise DiagnosticError("invalid maximum Kbuild derived C recompilation count")
        return probe(a)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"MINIC_BOOT_COHORT_DIAGNOSTIC=FAIL {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
