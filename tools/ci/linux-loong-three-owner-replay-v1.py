#!/usr/bin/env python3
"""Opt-in, mixed-compiler-epoch Loong vDSO repair (never a strict 2723-current-HEAD claim).

Seven immutable ACE5 MiniC/GNU-AS C-object shards are reused verbatim; only
vgetcpu.o and vgettimeofday.o are built from original frozen GNU -E by current
MiniC. Original GNU Kbuild may regenerate ONLY kernel/vdso.o from a live GNU -E
epoch, also with current MiniC. Actual GNU link, full postlink SHA and GNU/QEMU
P0/P1 differential must pass; no GNU-compiled C object can be accepted.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import urllib.request


OLD_RUN = 42378020139
SOURCE_RUN = 42366441631
OLD_CC_SHA = "f9b5b0df6a7b0ba6c6419f6c467f921c0ef1ee1b2ada4ffa795ab66194c2ad8e"
REPAIRED = ("arch/loongarch/vdso/vgetcpu.o", "arch/loongarch/vdso/vgettimeofday.o")
LIVE_OWNER = "arch/loongarch/kernel/vdso.o"
COUNT = 2723
ARTIFACTS = {
    "minic-linux-seven-compiler": (13576945126, "61226efc1e940c4080484ab648b1ee8329e39dfff789a4088121f64422089b79"),
    "minic-linux-seven-objects-loongarch64-0": (13575833950, "a453258db76ebe13cafd98e3603a6186160c6688a96e3faa39ed051ed371ea5a"),
    "minic-linux-seven-objects-loongarch64-1": (13576292959, "3e5a19746a181eba9ee46bfadfd6aff90685beb3f15111ff2a5c747f68abfc5b"),
    "minic-linux-seven-objects-loongarch64-2": (13575704526, "40e1d6749fb18e641deafa74981b5425b257f11aa3ab513776276f656c314d58"),
    "minic-linux-seven-objects-loongarch64-3": (13575998576, "3d2588e2de011db9d0c9a5e03c41837e4789c1dea0b8728403195de8811afecb"),
    "minic-linux-seven-objects-loongarch64-4": (13576666982, "1b015d9c775ea77db3bc96056d46dff3ab68981946089467c41c804ded411cf2"),
    "minic-linux-seven-objects-loongarch64-5": (13576576488, "2e6cd683dbf8737ba270c0332d2fed07084f50ff92badfbdb712bbbbd932e0b3"),
    "minic-linux-seven-objects-loongarch64-6": (13576442748, "15c5354d3aac4903978acb8d201e157d7547b513c2f1793d9d7df7a41c3a5bc7"),
}


def load(filename: str, name: str):
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError(f"missing strict reusable Linux oracle: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pinned_artifact_metadata(artifacts: list[dict]) -> dict:
    by_name = {item.get("name"): item for item in artifacts}
    if len(by_name) != len(artifacts):
        raise ValueError("duplicate official artifact names")
    for name, (identifier, digest) in ARTIFACTS.items():
        item = by_name.get(name)
        if (item is None or item.get("id") != identifier or
                item.get("digest") != "sha256:" + digest or item.get("expired") is not False or
                item.get("workflow_run", {}).get("id", OLD_RUN) != OLD_RUN):
            raise ValueError(f"old compiler/object ZIP metadata unavailable or changed: {name}")
    return {name: {"id": identifier, "zip_sha256": digest}
            for name, (identifier, digest) in ARTIFACTS.items()}


def fetch_official_metadata(repository: str, token: str) -> dict:
    if not re.fullmatch(r"[\w.-]+/[\w.-]+", repository) or not token:
        raise ValueError("explicit GitHub repository and Actions token required")
    url = f"https://api.github.com/repos/{repository}/actions/runs/{OLD_RUN}/artifacts?per_page=100"
    req = urllib.request.Request(url, headers={"Authorization": "Bearer " + token,
                                              "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=25) as stream:
        payload = json.load(stream)
    if not isinstance(payload.get("artifacts"), list):
        raise ValueError("incomplete official GitHub artifacts response")
    return pinned_artifact_metadata(payload["artifacts"])


def exact_repair_rows(rows: list[dict]) -> list[dict]:
    if len(rows) != COUNT or len({row["object"] for row in rows}) != COUNT:
        raise ValueError("not the official original 2723 Loong C-owner census")
    by_name = {row["object"]: row for row in rows}
    if not all(name in by_name for name in (*REPAIRED, LIVE_OWNER)):
        raise ValueError("missing original vDSO C source owners")
    return [by_name[name] for name in REPAIRED]


def require_live_one(output: Path, c) -> dict:
    files = sorted(output.glob("postlink-live-gnu-e-minic-c*-receipt.json"))
    if not files:
        raise ValueError("no authenticated generated vdso-offsets.h C-owner epoch")
    names = []
    receipts = []
    for file in files:
        receipt = json.loads(c.required(file).read_text())
        if receipt.get("verdict") != "LIVE_MINIC_C_SHA_POSTLINK_PASS":
            raise ValueError("live MiniC C-owner source epoch not SHA-certified")
        for entry in receipt["live_recompiled"]:
            names.append(entry["object"])
            if (not entry["actual_logged_gnu_cc"] or
                    not re.fullmatch(r"[0-9a-f]{64}", entry["live_gnu_i_sha256"])):
                raise ValueError("live GNU-E/MiniC-S origin not authenticated")
        receipts.append(c.sha(file))
    if names != [LIVE_OWNER]:
        raise ValueError(f"live C-owner count outside exact 2+1 repair: {names}")
    return {"live_owner": LIVE_OWNER, "live_receipt_sha256": receipts,
            "live_owner_source_epoch": 1}


def require_one_page(work: Path, output: Path, c) -> dict:
    cohort = load("linux-three-isa-boot-cohort-diagnostic-v1.py", "loong_vdso_page_oracle")
    report = {}
    for label, file in (("vdso.so", work / "build/arch/loongarch/vdso/vdso.so"),
                        ("vdso.so.dbg", work / "build/arch/loongarch/vdso/vdso.so.dbg")):
        entry = cohort.vdso_elf_layout(c.required(file))
        if (entry.get("verdict") != "ELF64_PT_LOAD_STRUCTURALLY_PARSED_NOT_RUNTIME_MAPPING_PROOF" or
                entry.get("machine") != 258 or entry.get("required_16k_mapping_pages") != 1):
            raise ValueError(f"new Loong vDSO exceeds fixed 16KiB page or invalid PT_LOAD: {label}: {entry}")
        report[label] = {"sha256": c.sha(file), "pt_load_span_bytes": entry["pt_load_span_bytes"],
                         "required_16k_mapping_pages": 1}
    (output / "one-page-vdso-elf-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def run(a) -> None:
    c = load("linux-three-isa-minic-candidate-v1.py", "loong_mixed_relink_candidate")
    seven = load("linux-three-isa-seven-runtime-v1.py", "loong_mixed_seven_origins")
    if a.output.exists():
        raise ValueError("refuse stale mixed-source-epoch replay output")
    a.output.mkdir(parents=True)
    verdict = {"schema": "minic-loong-ace5-current-two-live-one-v1",
               "strict_current_head_linux_certified": False,
               "mixed_source_epochs": True, "old_compiler_run": OLD_RUN,
               "frozen_source_run": SOURCE_RUN, "old_owners": COUNT - 3,
               "current_frozen_owners": list(REPAIRED),
               "current_live_owners": [LIVE_OWNER], "status": "NOT_CERTIFIED"}
    try:
        verdict["official_artifact_zip_metadata"] = fetch_official_metadata(
            a.repository, os.environ.get("GH_TOKEN", ""))
        old_pair = c.compiler_pair(a.old_minic)
        if old_pair["compiler_cc_sha256"] != OLD_CC_SHA:
            raise ValueError("old minic-cc binary differs from independently attested ACE5")
        current_pair = c.compiler_pair(a.minic)
        if current_pair == old_pair:
            raise ValueError("current MiniC must be a distinct compiler epoch")
        verdict["old_compiler_pair"] = old_pair
        verdict["current_compiler_pair"] = current_pair
        seven.preflight(argparse.Namespace(target="loongarch64", minic=a.old_minic,
                                            incoming=a.incoming), c)
        golden = json.loads(c.required(a.gnu_receipt).read_text())
        rows, data = c.candidate_inputs("loongarch64", a.gnu_work, a.minic,
                                         golden, a.initramfs)
        new_rows = exact_repair_rows(rows)
        identity = {"frozen_manifest_sha256": c.sha(a.gnu_work / "frozen/manifest.json"),
                    "gnu_receipt_sha256": seven.pinned_gnu_source_receipt_sha(
                        a.incoming, "loongarch64", golden["image_sha256"]),
                    "gnu_c_owner_manifest_sha256": data["gnu_c_owner_manifest_sha256"],
                    "original_gnu_image_sha256": golden["image_sha256"]}
        historical = seven.collect(argparse.Namespace(target="loongarch64", minic=a.old_minic,
                                                       incoming=a.incoming, output=a.output),
                                   c, rows, identity)
        current_output = a.output / "current-frozen-two"
        results = c.make_objects(new_rows, a.gnu_work, a.minic, current_output, data, 2, 240)
        if any(row["status"] != "MINIC_S_GNU_AS_PASS" for row in results):
            raise ValueError(f"current MiniC C/GNU AS vDSO compilation failed: {results}")
        source_evidence = a.output / "evidence/frozen-current-two"
        source_evidence.mkdir(parents=True)
        verdict["exact_original_gnu_e_and_cmd"] = []
        for row in new_rows:
            owner = Path(row["object"])
            frozen = c.required(a.gnu_work / "frozen" / row["input"])
            cmd = c.required(a.gnu_work / "build" / owner.parent / ("." + owner.name + ".cmd"))
            if c.sha(frozen) != row["sha256"] or c.sha(cmd) != row["cmd_sha256"]:
                raise ValueError("old producer frozen GNU .i or exact original .o.cmd SHA changed")
            shutil.copy2(frozen, source_evidence / (owner.stem + ".gnu.i"))
            shutil.copy2(cmd, source_evidence / (owner.stem + ".o.cmd"))
            verdict["exact_original_gnu_e_and_cmd"].append({
                "object": row["object"], "gnu_e_sha256": row["sha256"],
                "original_cmd_sha256": row["cmd_sha256"],
                "minic_flags": c.effective_minic_flags(row, "loongarch64"),
                "gnu_as_flags": row["gnu_as_flags"]})
        candidate_map = {r["object"]: r for r in historical}
        for result in results:
            name = result["object"]
            source = current_output / "staged" / name
            assembly = source.with_suffix(".s")
            if (c.sha(source) != result["obj_sha256"] or
                    c.sha(assembly) != result["asm_sha256"]):
                raise ValueError(f"current MiniC SHA drift: {name}")
            shutil.copy2(source, a.output / "staged" / name)
            shutil.copy2(assembly, (a.output / "staged" / name).with_suffix(".s"))
            candidate_map[name] = result
        ordered = [candidate_map[row["object"]] for row in rows]
        (a.output / "mixed-c-owner-results.json").write_text(json.dumps(ordered, indent=2) + "\n")
        verdict["frozen_manifest_sha256"] = identity["frozen_manifest_sha256"]
        verdict["source_producer_gnu_receipt_sha256"] = identity["gnu_receipt_sha256"]
        verdict["new_frozen_two"] = results
        verdict["mixed_2723_input_object_results_sha256"] = c.sha(
            a.output / "mixed-c-owner-results.json")
        shutil.copy2(a.gnu_receipt, a.output / "gnu-qemu-receipt.json")
        image = c.relink(a.gnu_work, a.output, rows, ordered, data, golden,
                         allowed_live_c_owners=frozenset({LIVE_OWNER}))
        live = require_live_one(a.output, c)
        live["final_object_sha256"] = c.sha(a.gnu_work / "build" / LIVE_OWNER)
        verdict.update(live)
        for row in rows:
            name = row["object"]
            if name in (*REPAIRED, LIVE_OWNER):
                continue
            if c.sha(c.required(a.gnu_work / "build" / name)) != candidate_map[name]["obj_sha256"]:
                raise ValueError(f"one of 2720 frozen ACE5 original objects changed: {name}")
        binary_evidence = a.output / "evidence/postlink-binaries"
        binary_evidence.mkdir(parents=True)
        for source, name in ((image, "candidate-vmlinux.efi"),
                             (a.gnu_work / "build/arch/loongarch/vdso/vdso.so", "candidate-vdso.so"),
                             (a.gnu_work / "build/arch/loongarch/vdso/vdso.so.dbg", "candidate-vdso.so.dbg")):
            shutil.copy2(c.required(source), binary_evidence / name)
        verdict["one_page_vdso"] = require_one_page(a.gnu_work, a.output, c)
        c.compiler_pair(a.minic, current_pair)
        qemu = c.boot_and_compare(data, a.output, a.initramfs, a.firmware,
                                  image, golden, a.gnu_boot_logs,
                                  source_epoch_metadata={
                                      "mixed_source_epochs": True,
                                      "strict_current_head_linux_certified": False,
                                      "old_ace5_c_owners": COUNT - 3,
                                      "current_frozen_c_owners": list(REPAIRED),
                                      "current_live_c_owners": [LIVE_OWNER],
                                      "diagnostic_only": True})
        if qemu.get("verdict") != c.MINIC_VERDICT:
            raise ValueError("actual GNU-vs-MiniC QEMU P0/P1 differential did not pass")
        verdict["image_sha256"] = c.sha(image)
        verdict["qemu_p0_p1_actual"] = "PASS"
        verdict["status"] = "MIXED_ACE5_2720_CURRENT_3_GNU_QEMU_P0_P1_PASS_NOT_CURRENT_HEAD_2723"
    except Exception as exc:
        verdict["status"] = "REPLAY_NOT_CERTIFIED"
        verdict["error"] = str(exc)
        raise
    finally:
        (a.output / "mixed-compiler-epoch-receipt.json").write_text(
            json.dumps(verdict, indent=2, sort_keys=True) + "\n")
        print("LOONG_THREE_OWNER_REPLAY=" + verdict["status"], flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--verify-official-artifacts", action="store_true")
    for name in ("old-minic", "minic", "gnu-work", "gnu-receipt", "gnu-boot-logs",
                 "initramfs", "firmware", "incoming", "output"):
        p.add_argument("--" + name, type=Path)
    p.add_argument("--repository")
    a = p.parse_args()
    if a.self_test:
        fake = [{"object": f"kernel/owner-{i}.o"} for i in range(COUNT - 3)]
        fake += [{"object": name} for name in (*REPAIRED, LIVE_OWNER)]
        assert len(exact_repair_rows(fake)) == 2
        for bad in (fake[:-1], fake[:-3] + fake[-2:] + [fake[-2]]):
            try:
                exact_repair_rows(bad)
                raise AssertionError("partial/duplicated original owner census accepted")
            except ValueError:
                pass
        items = [{"name": name, "id": identifier, "digest": "sha256:" + digest,
                  "expired": False} for name, (identifier, digest) in ARTIFACTS.items()]
        pinned_artifact_metadata(items)
        items[1]["digest"] = "sha256:" + "0" * 64
        try:
            pinned_artifact_metadata(items)
            raise AssertionError("tampered old object ZIP metadata accepted")
        except ValueError:
            pass
        with tempfile.TemporaryDirectory(prefix="loong-exact-live-owner-") as temp:
            evidence = Path(temp)
            stub = load("linux-three-isa-minic-candidate-v1.py", "loong_test_receipt_sha")
            try:
                require_live_one(evidence, stub)
                raise AssertionError("missing live C-owner epoch accepted")
            except ValueError:
                pass
            receipt = evidence / "postlink-live-gnu-e-minic-c-receipt.json"
            receipt.write_text(json.dumps({"verdict": "LIVE_MINIC_C_SHA_POSTLINK_PASS",
                "live_recompiled": [{"object": "fs/exec.o", "actual_logged_gnu_cc": True,
                                     "live_gnu_i_sha256": "a" * 64}]}))
            try:
                require_live_one(evidence, stub)
                raise AssertionError("unrelated generated C owner accepted")
            except ValueError:
                pass
            receipt.write_text(json.dumps({"verdict": "LIVE_MINIC_C_SHA_POSTLINK_PASS",
                "live_recompiled": [{"object": LIVE_OWNER, "actual_logged_gnu_cc": True,
                                     "live_gnu_i_sha256": "a" * 64}]}))
            assert require_live_one(evidence, stub)["live_owner"] == LIVE_OWNER
        print("LOONG_THREE_OWNER_REPLAY_SELFTEST=PASS frozen=2 live=1 old=2720")
        return
    if a.verify_official_artifacts:
        result = fetch_official_metadata(a.repository or "", os.environ.get("GH_TOKEN", ""))
        print("LOONG_THREE_OWNER_OFFICIAL_ZIP=PASS original_run=42378020139 count=" + str(len(result)))
        return
    if any(getattr(a, name.replace("-", "_")) is None for name in
           ("old-minic", "minic", "gnu-work", "gnu-receipt", "gnu-boot-logs",
            "initramfs", "firmware", "incoming", "output")) or not a.repository:
        p.error("run requires every provenance, GNU oracle, compiler and output argument")
    run(a)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"LOONG_THREE_OWNER_REPLAY=FAIL {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
