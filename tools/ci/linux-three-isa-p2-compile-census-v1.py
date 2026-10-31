#!/usr/bin/env python3
"""P2 full GNU-C-owner MiniC-S/GNU-AS compile census, even without GNU P2 QEMU.

This is deliberately a BUILD-ONLY tool. In particular an x86 TCG host with
no VMX and no /dev/kvm can audit all 2939 GNU-C owners, but 17/18 real GNU
guest checks cannot authorize an independently passing P2 MiniC runtime.

Unlike the 7-runner release export/receive workflow, this diagnostic reads
one SHA-frozen GNU -E corpus without a second 11GiB copy. No shared hardlinks,
GNU C fallback, relinked candidate, or forged runtime receipt are produced.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import sys


CI = Path(__file__).resolve().parent
SOURCE_SHA = "dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932"


def import_original():
    path = CI / "linux-three-isa-minic-candidate-v1.py"
    spec = importlib.util.spec_from_file_location("p2_direct_compile_original", path)
    if spec is None or spec.loader is None:
        raise ValueError("missing original GNU-C-owner MiniC compiler verifier")
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


C = import_original()


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(C.required(path).read_text())


def census_inputs(a: argparse.Namespace) -> tuple[list[dict], dict]:
    out, frozen = a.work / "build", a.work / "frozen"
    evidence = a.evidence
    discovery = read_json(evidence / "discovery.json")
    manifest = read_json(frozen / "manifest.json")
    arch, cross, gcc, build_target, image_name, machine = C.TARGETS[a.target]
    config = C.required(out / ".config")
    image = C.required(out / image_name)
    owner_list = C.required(evidence / "full-c-owners.txt")
    identity = manifest.get("identity", {})
    owners = manifest.get("rows", [])
    if (discovery.get("schema") != "minic-linux-three-isa-p2-gnu-build-v1" or
            discovery.get("verdict") != "GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC" or
            discovery.get("config_profile") != "p2" or
            discovery.get("target") != a.target or
            discovery.get("config_sha256") != sha(config) or
            discovery.get("kernel_sha256") != sha(image) or
            discovery.get("owner_manifest_sha256") != sha(owner_list)):
        raise ValueError("GNU P2 Kconfig/Image/full owner manifest altered")
    if (manifest.get("schema") != "three-isa-gnu-i-v1" or
            identity.get("target") != a.target or
            identity.get("profile") != "p2" or
            identity.get("source") != SOURCE_SHA or
            identity.get("config") != discovery["config_sha256"] or
            identity.get("image") != discovery["kernel_sha256"] or
            identity.get("objects") != sha(owner_list) or
            len(owners) != discovery["gnu_c_owners"] or len(owners) < 200 or
            len({row.get("object") for row in owners}) != len(owners) or
            manifest.get("errors")):
        raise ValueError("P2 original GNU -E frozen manifest missing or partial")
    # Same Kconfig source policy as the P2 GNU build (not evidence of QEMU).
    mandatory = ("IPV6 OVERLAY_FS BTRFS_FS BLK_DEV_DM EXT4_FS VIRTIO_BLK "
                 "VIRTIO_PCI VIRTIO_MMIO NETFILTER NF_CONNTRACK IP_NF_IPTABLES "
                 "IP6_NF_IPTABLES IP_NF_FILTER IP6_NF_FILTER BRIDGE "
                 "BRIDGE_NETFILTER VETH CRYPTO_USER_API_HASH CRYPTO_SHA256 "
                 "CRYPTO_SHA512 CRYPTO_SHA3 CRYPTO_BLAKE2B KVM DRM_VIRTIO_GPU "
                 "BLK_DEV_NVME NET_NS NAMESPACES").split()
    if a.target == "x86_64":
        mandatory.append("KVM_INTEL")
    effective = set(config.read_text().splitlines())
    if any(f"CONFIG_{symbol}=y" not in effective for symbol in mandatory):
        raise ValueError("GNU P2 builtin Kconfig not fully enabled")
    for index, row in enumerate(owners):
        if (row.get("index") != index or row.get("shard") != index % 7 or
                not isinstance(row.get("input"), str) or
                not row["input"].startswith("i/") or
                row["input"] != "i/" + row["object"][:-2] + ".i"):
            raise ValueError("invalid seven-way GNU C-owner partition/identity")
    return owners, {"target": a.target, "gcc": gcc, "machine": machine,
                    "config_sha256": sha(config), "image_sha256": sha(image),
                    "owner_manifest_sha256": sha(owner_list),
                    "frozen_manifest_sha256": sha(frozen / "manifest.json")}


def produce(a: argparse.Namespace) -> int:
    if a.shard is None or not 0 <= a.shard < 7 or a.minic is None:
        raise ValueError("produce requires --shard 0..6 and --minic")
    if a.output.exists():
        raise ValueError("refuse to reuse previous MiniC P2 C-owner results")
    if shutil.disk_usage(a.work).free < 8 * 1024**3:
        raise ValueError("insufficient free disk: preserve at least 8GiB")
    owners, identity = census_inputs(a)
    selected = owners[a.shard::7]
    for row in selected:
        inp = C.required(a.work / "frozen" / row["input"])
        cmd_name = Path(row["object"])
        cmd = C.required(a.work / "build" / cmd_name.parent / ("." + cmd_name.name + ".cmd"))
        if sha(inp) != row["sha256"] or sha(cmd) != row["cmd_sha256"]:
            raise ValueError("changed GNU source or original .o.cmd: " + row["object"])
    pair = C.compiler_pair(a.minic)
    a.output.mkdir(parents=True)
    results = C.make_objects(selected, a.work, a.minic, a.output,
                             identity, workers=a.workers, timeout=a.timeout)
    (a.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    failed = [row for row in results if row["status"] != "MINIC_S_GNU_AS_PASS"]
    receipt = {"schema": "minic-p2-direct-seven-producer-v1",
               **identity, **pair, "shard": a.shard,
               "total": len(selected), "pass": len(selected) - len(failed),
               "fail": len(failed), "results_sha256": sha(a.output / "results.json"),
               "failures": [{"object": r["object"], "status": r["status"],
                             "detail": r.get("detail", "")} for r in failed],
               "verdict": ("MINIC_S_GNU_AS_BUILD_ONLY_PASS" if not failed else
                           "BLOCKED_NO_GNU_C_FALLBACK"),
               "runtime_certified": False, "gnu_c_fallback": 0}
    (a.output / "receipt.json").write_text(json.dumps(receipt, indent=2,
                                                       sort_keys=True) + "\n")
    print("P2_DIRECT_SHARD=%s target=%s shard=%d MiniC_GNU_AS=%d/%d failures=%d "
          "GNU_P2_RUNTIME=NOT_CERTIFIED" % (receipt["verdict"], a.target,
          a.shard, receipt["pass"], receipt["total"], receipt["fail"]))
    return 0 if not failed else 1


def audit(a: argparse.Namespace) -> int:
    if a.producer_prefix is None or a.minic is None or a.output.exists():
        raise ValueError("audit requires --producer-prefix, pinned --minic and a fresh output")
    owners, identity = census_inputs(a)
    pinned_compiler = C.compiler_pair(a.minic)
    total_pass = 0
    shards = []
    failures = []
    for shard in range(7):
        root = Path(str(a.producer_prefix) + str(shard))
        receipt = read_json(root / "receipt.json")
        result_file = root / "results.json"
        results = read_json(result_file)
        selected = owners[shard::7]
        if (receipt.get("schema") != "minic-p2-direct-seven-producer-v1" or
                receipt.get("shard") != shard or
                any(receipt.get(k) != v for k, v in identity.items()) or
                any(receipt.get(k) != v for k, v in pinned_compiler.items()) or
                receipt.get("results_sha256") != sha(result_file) or
                len(results) != len(selected) or
                receipt.get("total") != len(selected) or
                receipt.get("runtime_certified") is not False or
                receipt.get("gnu_c_fallback") != 0):
            raise ValueError("incomplete/foreign MiniC P2 producer shard %d" % shard)
        passed = 0
        failed = []
        for original, row in zip(selected, results):
            if (row.get("object") != original["object"] or
                    row.get("input_sha256") != original["sha256"]):
                raise ValueError("noncanonical P2 C owner in shard %d" % shard)
            name = Path(original["object"])
            frozen_input = C.required(a.work / "frozen" / original["input"])
            original_cmd = C.required(a.work / "build" / name.parent /
                                      ("." + name.name + ".cmd"))
            if (sha(frozen_input) != original["sha256"] or
                    sha(original_cmd) != original["cmd_sha256"]):
                raise ValueError("changed GNU owner input or command: " + original["object"])
            if row.get("status") == "MINIC_S_GNU_AS_PASS":
                asm = C.required(root / "staged" / name.with_suffix(".s"))
                if sha(asm) != row.get("asm_sha256"):
                    raise ValueError("MiniC assembly SHA mismatch: " + row["object"])
                obj = C.required(root / "staged" / row["object"])
                if sha(obj) != row.get("obj_sha256"):
                    raise ValueError("MiniC/GNU-AS object SHA mismatch: " + row["object"])
                C.elf_machine(obj, identity["machine"])
                passed += 1
            else:
                if row.get("status") not in ("MINIC_S_FAIL", "GNU_TARGET_AS_FAIL",
                                             "SOURCE_AS_ERROR"):
                    raise ValueError("unrecognized MiniC owner failure: " + row["object"])
                C.required(root / "staged" / name.with_suffix(".tool-stderr.txt"))
                failed.append({"object": row["object"], "status": row.get("status"),
                               "detail": row.get("detail", "")})
        expected_verdict = ("MINIC_S_GNU_AS_BUILD_ONLY_PASS" if not failed else
                            "BLOCKED_NO_GNU_C_FALLBACK")
        if (receipt["pass"] != passed or receipt["fail"] != len(failed) or
                receipt.get("failures") != failed or
                receipt.get("verdict") != expected_verdict):
            raise ValueError("contradictory MiniC shard receipt %d" % shard)
        total_pass += passed
        failures.extend(failed)
        shards.append({"shard": shard, "pass": passed, "fail": len(failed),
                       "total": len(selected), "results_sha256": sha(result_file),
                       "receipt_sha256": sha(root / "receipt.json")})
    verdict = "MINIC_S_GNU_AS_BUILD_ONLY_PASS" if not failures else "BLOCKED_NO_GNU_C_FALLBACK"
    record = {"schema": "minic-p2-direct-seven-census-v1", **identity,
              **pinned_compiler,
              "owners": len(owners), "mini_c_gnu_as_pass": total_pass,
              "failed": len(failures), "failures": failures, "shards": shards,
              "verdict": verdict, "runtime_certified": False, "gnu_c_fallback": 0}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(record, sort_keys=True, indent=2) + "\n")
    print("P2_DIRECT_CENSUS=%s target=%s MiniC_GNU_AS=%d/%d failures=%d "
          "GNU_P2_RUNTIME=NOT_CERTIFIED" % (verdict, a.target,
          total_pass, len(owners), len(failures)))
    return 0 if not failures else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("produce", "audit"))
    parser.add_argument("--target", choices=tuple(C.TARGETS), required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--minic", type=Path)
    parser.add_argument("--shard", type=int)
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--producer-prefix", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if not 1 <= args.workers <= 8:
            raise ValueError("workers outside audited range 1..8")
        return produce(args) if args.mode == "produce" else audit(args)
    except (OSError, KeyError, ValueError, C.Rejected) as exc:
        print("P2_DIRECT_CENSUS=FAIL reason=" + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
