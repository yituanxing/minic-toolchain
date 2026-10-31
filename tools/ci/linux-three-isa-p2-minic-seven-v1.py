#!/usr/bin/env python3
"""Strict P2 adapter for the original seven-runner MiniC/GNU-AS/GNU-LD pipeline.

The P1/broad identity in linux-three-isa-minic-candidate-v1.py is deliberately
NOT accepted here. Each phase revalidates one original 6.6.143 GNU P2 build,
the exact 4377/other target C-owner universe, and an independently booted,
strictly passing GNU full-P2 reference before replacing any C object.

Modes: certify -> export -> existing seven-runtime.py produce(0..6) -> receive.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CI = Path(__file__).resolve().parent
P2_CHECKER = ROOT / "tests/external/linux/runtime_v2_log_check.py"
P2_RUNNER = CI / "linux-three-isa-p2-gnu-boot-v1.sh"
P2_DIFF = CI / "linux-runtime-p2-diff-v1.py"
SOURCE_SHA = "dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932"
VERDICT = "GNU_QEMU_P2_FULL_PASS_NOT_MINIC"


def imported(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, CI / filename)
    if spec is None or spec.loader is None:
        raise ValueError("missing original seven-runner component: " + filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


C = imported("p2_original_minic_candidate", "linux-three-isa-minic-candidate-v1.py")
SEVEN = imported("p2_original_seven_runner", "linux-three-isa-seven-runtime-v1.py")
FREEZE = imported("p2_original_gnu_freeze", "linux-three-isa-freeze-i-v1.py")


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(C.required(path).read_text())


def strict_gnu_receipt(a: argparse.Namespace) -> dict:
    out, evidence = a.gnu_work / "build", a.gnu_work / "evidence"
    arch, cross, gcc, build_target, image_name, machine = C.TARGETS[a.target]
    image = C.required(out / image_name)
    config = C.required(out / ".config")
    initrd = C.required(a.initramfs)
    rootfs = C.required(a.rootfs)
    nvme = C.required(a.nvme)
    log = C.required(a.gnu_boot_logs / "qemu.log")
    command_path = C.required(a.gnu_boot_logs / "qemu-command.txt")
    result_path = C.required(a.gnu_boot_logs / "result.txt")
    digest_path = C.required(a.gnu_boot_logs / "input.sha256")
    discovery = read_json(evidence / "discovery.json")
    source_tar = C.required(a.gnu_work / "linux-6.6.143.tar.xz")
    source = C.required(a.gnu_work / "linux-6.6.143" / "Makefile")
    if sha(source_tar) != SOURCE_SHA or "VERSION = 6" not in source.read_text()[:256]:
        raise ValueError("GNU P2 source must be unmodified upstream 6.6.143 tar identity")
    if (discovery.get("schema") != "minic-linux-three-isa-p2-gnu-build-v1" or
            discovery.get("verdict") != "GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC" or
            discovery.get("config_profile") != "p2" or
            discovery.get("target") != a.target or
            discovery.get("config_sha256") != sha(config) or
            discovery.get("kernel_sha256") != sha(image)):
        raise ValueError("P2 GNU discovery/image/config mismatch")
    if (discovery.get("gnu_c_owners", 0) < 200 or
            sha(C.required(evidence / "full-c-owners.txt")) !=
            discovery.get("owner_manifest_sha256")):
        raise ValueError("incomplete P2 C-owner census")
    # Every functional Kconfig feature, including netns, must be built in.
    required = (
        "IPV6 OVERLAY_FS BTRFS_FS BLK_DEV_DM EXT4_FS VIRTIO_BLK VIRTIO_PCI "
        "VIRTIO_MMIO NETFILTER NF_CONNTRACK IP_NF_IPTABLES IP6_NF_IPTABLES "
        "IP_NF_FILTER IP6_NF_FILTER BRIDGE BRIDGE_NETFILTER VETH "
        "CRYPTO_USER_API_HASH CRYPTO_SHA256 CRYPTO_SHA512 CRYPTO_SHA3 "
        "CRYPTO_BLAKE2B KVM DRM_VIRTIO_GPU BLK_DEV_NVME NET_NS NAMESPACES"
    ).split()
    resolved = set(config.read_text().splitlines())
    missing = [symbol for symbol in required if f"CONFIG_{symbol}=y" not in resolved]
    if missing:
        raise ValueError("P2 built-in config unresolved: " + ",".join(missing))

    result_values = dict(re.findall(r"^(qemu_rc|oracle_rc)=(\d+)$",
                                    result_path.read_text(), re.M))
    if result_values != {"qemu_rc": "0", "oracle_rc": "0"}:
        raise ValueError("P2 GNU QEMU/strict oracle not both zero")
    strict = subprocess.run(
        [sys.executable, str(P2_CHECKER), "--profile", "p2", "--qemu-rc", "0",
         "--log", str(log)], capture_output=True, text=True)
    if strict.returncode != 0 or "checks=40 fatal=0" not in strict.stdout:
        raise ValueError("GNU full-P2 guest did not pass the 40/40 strict checker: " +
                         strict.stderr[-500:])
    # Tie the QEMU launch itself to all four real fixture files. One SHA line
    # per input and exact resolved paths: no remembered or foreign P1 image.
    expected_inputs = {str(p.resolve()): sha(p) for p in (image, initrd, rootfs, nvme)}
    received_inputs = {}
    for line in digest_path.read_text().splitlines():
        parts = line.split(maxsplit=1)
        if len(parts) != 2 or not re.fullmatch(r"[0-9a-f]{64}", parts[0]):
            raise ValueError("malformed actual GNU QEMU input SHA receipt")
        received_inputs[parts[1].lstrip(" *")] = parts[0]
    if any(received_inputs.get(path) != value for path, value in expected_inputs.items()):
        raise ValueError("GNU QEMU did not boot the exact attested P2 fixture files")
    command = shlex.split(command_path.read_text())
    devices = [command[index + 1] for index, value in enumerate(command[:-1])
               if value == "-device"]
    drives = [command[index + 1] for index, value in enumerate(command[:-1])
              if value == "-drive"]
    def option_value(option: str) -> str:
        if option not in command or command.index(option) + 1 == len(command):
            raise ValueError("GNU QEMU missing mandated argument: " + option)
        return command[command.index(option) + 1]
    if (option_value("-kernel") != str(image.resolve()) or
            option_value("-initrd") != str(initrd.resolve()) or
            "nf_conntrack.enable_hooks=1" not in option_value("-append") or
            "minic_runtime=p2" not in option_value("-append") or
            f"if=none,id=p2root,file={rootfs.resolve()},format=raw,snapshot=on" not in drives or
            f"if=none,id=p2nvmedisk,file={nvme.resolve()},format=raw,snapshot=on" not in drives or
            "virtio-blk-pci,drive=p2root" not in devices or
            "virtio-gpu-pci" not in devices or
            "nvme,id=p2nvme,serial=MINICP2" not in devices or
            "nvme-ns,drive=p2nvmedisk,bus=p2nvme" not in devices):
        raise ValueError("GNU P2 QEMU command omitted its real strict devices")
    if a.target == "aarch64" and option_value("-machine") != "virt,virtualization=on":
        raise ValueError("AArch64 KVM needs guest EL2 from original QEMU fixture")
    return {
        "schema": "minic-linux-p2-gnu-receipt-v1", "verdict": VERDICT,
        "target": a.target, "source_sha256": SOURCE_SHA,
        "config_sha256": sha(config), "image_sha256": sha(image),
        "initramfs_sha256": sha(initrd), "rootfs_sha256": sha(rootfs),
        "nvme_sha256": sha(nvme), "gnu_c_owners": discovery["gnu_c_owners"],
        "gnu_c_owner_manifest_sha256": discovery["owner_manifest_sha256"],
        "gnu_log_sha256": sha(log), "gnu_qemu_command_sha256": sha(command_path),
        "gnu_boot_log": str(log.resolve()),
        "gnu_image": str(image.resolve()), "qemu_rc": 0, "oracle_rc": 0,
        "strict_profile": "p2", "strict_checks": 40,
    }


def candidate_inputs_p2(target: str, work: Path, compiler: Path,
                        receipt: dict, initramfs: Path) -> tuple[list[dict], dict]:
    if receipt.get("schema") != "minic-linux-p2-gnu-receipt-v1" or receipt.get("verdict") != VERDICT:
        raise ValueError("no original GNU full-P2 PASS attestation")
    if target != receipt.get("target") or sha(C.required(initramfs)) != receipt.get("initramfs_sha256"):
        raise ValueError("MiniC P2 initrd/target changed from GNU baseline")
    out, evidence, frozen = work / "build", work / "evidence", work / "frozen"
    arch, cross, gcc, build_target, image_name, machine = C.TARGETS[target]
    if (sha(C.required(out / ".config")) != receipt["config_sha256"] or
            sha(C.required(out / image_name)) != receipt["image_sha256"] or
            sha(C.required(work / "linux-6.6.143.tar.xz")) != SOURCE_SHA):
        raise ValueError("P2 GNU original source/config/Image changed before MiniC")
    discovery = read_json(evidence / "discovery.json")
    if (discovery.get("schema") != "minic-linux-three-isa-p2-gnu-build-v1" or
            discovery.get("config_profile") != "p2" or
            discovery.get("target") != target or
            discovery.get("gnu_c_owners") != receipt["gnu_c_owners"] or
            discovery.get("owner_manifest_sha256") != receipt["gnu_c_owner_manifest_sha256"] or
            discovery.get("kernel_sha256") != receipt["image_sha256"]):
        raise ValueError("P2 original owner/ELF profile not certified")
    FREEZE.verify(evidence, frozen, target, gcc)
    frozen_manifest = read_json(frozen / "manifest.json")
    rows = frozen_manifest["rows"]
    if (frozen_manifest["identity"]["profile"] != "p2" or
            len(rows) != receipt["gnu_c_owners"] or
            len({row["object"] for row in rows}) != len(rows)):
        raise ValueError("wrong or incomplete P2 source freeze")
    option_contract = C.verify_gnu_kbuild_commands(out, rows, gcc, target)
    for row in rows:
        C.required(out / row["object"])
    identity = C.compiler_pair(compiler)
    return rows, {
        "target": target, "gnu_c_owners": len(rows),
        "compiler_executable": str(compiler.resolve(strict=True)),
        "golden_image_sha256": receipt["image_sha256"],
        "source_sha256": SOURCE_SHA, "config_sha256": receipt["config_sha256"],
        "initramfs_sha256": receipt["initramfs_sha256"],
        "firmware_sha256": receipt.get("firmware_sha256"),
        "gnu_c_owner_manifest_sha256": discovery["owner_manifest_sha256"],
        **identity, **option_contract,
        "arch": arch, "cross": cross, "gcc": gcc,
        "build_target": build_target, "image_name": image_name,
        "machine": machine,
    }


def audit_producers(a: argparse.Namespace) -> int:
    """Require exact seven-way MiniC+GNU-AS coverage; preserve all failures."""
    if a.incoming is None or a.output.exists():
        raise ValueError("audit requires fresh output and --incoming producer path prefix")
    manifest = read_json(a.gnu_work / "frozen/manifest.json")
    rows = manifest["rows"]
    if manifest["identity"]["profile"] != "p2" or len(rows) < 200:
        raise ValueError("audit requires complete P2 GNU frozen owner identities")
    shards = []
    failures = []
    total_pass = 0
    for shard in range(7):
        subset = rows[shard::7]
        producer = Path(str(a.incoming) + str(shard))
        path = producer / "results.json"
        produced = read_json(path)
        if not isinstance(produced, list) or len(produced) != len(subset):
            raise ValueError("incomplete MiniC producer results in shard %d" % shard)
        passed = 0
        failed = []
        for expected, row in zip(subset, produced):
            if (row.get("object") != expected["object"] or
                    row.get("input_sha256") != expected["sha256"]):
                raise ValueError("unmatched/fabricated original GNU C owner in shard %d" % shard)
            if row.get("status") == "MINIC_S_GNU_AS_PASS":
                obj = C.required(producer / "staged" / row["object"])
                if sha(obj) != row.get("obj_sha256"):
                    raise ValueError("corrupt MiniC object: " + row["object"])
                C.elf_machine(obj, C.TARGETS[a.target][5])
                passed += 1
            else:
                failed.append({"object": row["object"],
                               "status": row.get("status"),
                               "detail": row.get("detail", "")})
        total_pass += passed
        failures.extend(failed)
        shards.append({"shard": shard, "total": len(subset), "pass": passed,
                       "fail": len(failed), "results_sha256": sha(path),
                       "failures": failed})
    verdict = "MINIC_ALL_OWNERS_GNU_AS_READY" if not failures else "BLOCKED_NO_GNU_C_FALLBACK"
    report = {"schema": "minic-p2-seven-owner-census-v1", "target": a.target,
              "verdict": verdict, "owners": len(rows), "mini_c_gnu_as_pass": total_pass,
              "failed": len(failures), "shards": shards, "failures": failures,
              "explicit_rule": "No GNU C object may fill any failed MiniC owner"}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print("P2_SEVEN_AUDIT=%s target=%s MiniC_GNU_AS=%d/%d failed=%d" %
          (verdict, a.target, total_pass, len(rows), len(failures)))
    return 0 if not failures else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("certify", "export", "audit", "receive"))
    parser.add_argument("--target", choices=("x86_64", "aarch64", "loongarch64"), required=True)
    parser.add_argument("--gnu-work", type=Path, required=True)
    parser.add_argument("--gnu-receipt", type=Path)
    parser.add_argument("--gnu-boot-logs", type=Path, required=True)
    parser.add_argument("--initramfs", type=Path, required=True)
    parser.add_argument("--rootfs", type=Path, required=True)
    parser.add_argument("--nvme", type=Path, required=True)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--minic", type=Path)
    parser.add_argument("--incoming", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    a = parser.parse_args()
    try:
        if a.mode == "certify":
            if a.output.exists():
                raise ValueError("refuse to overwrite GNU P2 receipt")
            receipt = strict_gnu_receipt(a)
            a.output.parent.mkdir(parents=True, exist_ok=True)
            a.output.write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n")
            print("P2_GNU_RECEIPT=PASS target=%s checks=40/40 owners=%d" %
                  (a.target, receipt["gnu_c_owners"]))
            return 0
        if a.mode == "audit":
            if a.gnu_receipt is None or a.minic is None:
                raise ValueError("P2 producer audit requires frozen GNU receipt and MiniC executable")
            if read_json(a.gnu_receipt) != strict_gnu_receipt(a):
                raise ValueError("P2 producer audit requires identical strict GNU QEMU receipt")
            FREEZE.verify(a.gnu_work / "evidence", a.gnu_work / "frozen",
                          a.target, C.TARGETS[a.target][2])
            C.compiler_pair(a.minic)
            return audit_producers(a)
        if a.minic is None or a.gnu_receipt is None:
            raise ValueError("P2 seven-runner requires --minic and --gnu-receipt")
        receipt = read_json(a.gnu_receipt)
        actual = strict_gnu_receipt(a)
        if receipt != actual:
            raise ValueError("GNU full-P2 reference is not identical to its frozen receipt")
        C.candidate_inputs = candidate_inputs_p2
        if a.mode == "export":
            SEVEN.export(a, C)
            print("P2_GNU_SEVEN_EXPORT=PASS target=%s owners=%d" %
                  (a.target, receipt["gnu_c_owners"]))
            return 0
        if not a.incoming:
            raise ValueError("P2 receive requires --incoming seven MiniC object artifacts")

        def p2_boot_compare(data, output, initramfs, firmware, image, golden, logs):
            if sha(C.required(initramfs)) != golden["initramfs_sha256"]:
                raise ValueError("P2 MiniC QEMU initrd is not the original GNU one")
            qemu_evidence = output / "minic-p2-qemu"
            command = ["bash", str(P2_RUNNER), a.target, str(image), str(initramfs),
                       str(a.rootfs), str(a.nvme), str(qemu_evidence)]
            if a.target == "loongarch64":
                if firmware is None:
                    raise ValueError("LoongArch P2 missing original EFI firmware")
                command.append(str(firmware))
            run = subprocess.run(command, capture_output=True, text=True)
            (output / "minic-p2-qemu-runner.txt").write_text(run.stdout + run.stderr)
            result = qemu_evidence / "result.txt"
            if not result.is_file():
                raise ValueError("MiniC P2 QEMU did not produce an actual process receipt")
            matches = dict(re.findall(r"^(qemu_rc|oracle_rc)=(\d+)$", result.read_text(), re.M))
            qemu_rc = int(matches.get("qemu_rc", "-1"))
            difference = [sys.executable, str(P2_DIFF), "--target", a.target,
                          "--gnu-log", golden["gnu_boot_log"],
                          "--minic-log", str(qemu_evidence / "qemu.log"),
                          "--gnu-image", golden["gnu_image"],
                          "--minic-image", str(image), "--initrd", str(initramfs),
                          "--gnu-rc", "0", "--minic-rc", str(qemu_rc),
                          "--json-out", str(output / "minic-p2-diff.json")]
            diff = subprocess.run(difference, capture_output=True, text=True)
            (output / "minic-p2-diff.txt").write_text(diff.stdout + diff.stderr)
            if run.returncode or matches.get("oracle_rc") != "0" or diff.returncode:
                raise ValueError("MiniC P2 QEMU/strict diff FAILED: " +
                                 (run.stdout + run.stderr + diff.stdout + diff.stderr)[-1000:])
            print("MINIC_P2_GNU_AS_LD_QEMU_DIFF=PASS target=%s owners=%d checks=40/40" %
                  (a.target, data["gnu_c_owners"]))
            return {"verdict": "MINIC_P2_GNU_AS_LD_QEMU_DIFF_PASS"}

        C.boot_and_compare = p2_boot_compare
        SEVEN.receive(a, C)
        print("P2_SEVEN_MINIC_RECEIVE=PASS target=%s real-QEMU=true" % a.target)
        return 0
    except (OSError, KeyError, ValueError) as exc:
        print("P2_SEVEN_MINIC=FAIL reason=" + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
