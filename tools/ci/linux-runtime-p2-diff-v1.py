#!/usr/bin/env python3
"""Compare *real* GNU and MiniC Linux P2 QEMU evidence without hiding skips.

This is an evidence classifier, not a replacement for a guest or QEMU run.
The caller must supply each QEMU return code and the exact Image/initramfs
files used. An incomplete GNU reference cannot certify a MiniC regression.

Exit 0 ONLY if both full 18-check P2 logs pass the canonical strict oracle;
all incomplete, skipped, differing or failed runs exit 1; input errors exit 2.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tests/external/linux/runtime_v2_log_check.py"
CANONICAL_P2_IDS = frozenset({
    "rootfs-rw", "overlay-copy-up", "iptables4", "iptables6",
    "conntrack", "bridge", "br-netfilter", "veth", "ipv6-tcp",
    "sha256", "sha512", "sha3", "blake2b", "btrfs", "kvm",
    "dm", "virtio-gpu", "nvme",
})
OUTCOMES = ("PASS", "FAIL", "SKIP", "UNAVAILABLE")
FUNCTION_RECORD = re.compile(r"^FUNCTION_(PASS|FAIL|SKIP|UNAVAILABLE)[ \t]+([^ \t\r\n]+)")
OTHER_NEGATIVE = re.compile(
    r"^(?:FUNCTION|MODULE)_(?:FAIL|SKIP|UNAVAILABLE)\b|"
    r"^INIT_P2_PROBE=FAIL\b|^RUNTIME_V2_P2_END.*\bskip=[1-9]",
)
KERNEL_FATAL = re.compile(
    r"Kernel panic|\bOops:|\bBUG:|soft lockup|hard LOCKUP|"
    r"workqueue lockup|Unable to handle kernel|Kernel stack overflow|"
    r"Unknown symbol|invalid module format", re.I,
)
RV64_EXPLICIT_INITRD = re.compile(
    r"^QEMU_EXPLICIT_INITRD=PASS\s+image_bytes=(\d+)\s+initrd_bytes=(\d+)\b"
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def load_manifest(path: Path) -> list[tuple[str, str]]:
    checks: list[tuple[str, str]] = []
    for line in path.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 4:
            raise ValueError("invalid manifest row: " + line)
        tier, check_id, marker, _fault_class = fields
        if tier == "p2":
            checks.append((check_id, marker))
    if len(checks) != 18 or {check_id for check_id, _ in checks} != CANONICAL_P2_IDS:
        raise ValueError("P2 manifest changed: require the canonical 18 distinct IDs")
    for check_id, marker in checks:
        if not (marker.startswith("FUNCTION_PASS ") or
                marker.startswith("RUNTIME_V2_") and marker.endswith("=PASS")):
            raise ValueError(f"unsupported P2 marker {check_id}: {marker}")
    return checks


def collect_check(lines: list[str], check_id: str, marker: str) -> dict:
    """Exactly one genuine check record, no prefix-matching of an ID-extra."""
    records = []
    if marker.startswith("FUNCTION_PASS "):
        token = marker.removeprefix("FUNCTION_PASS ")
        pattern = re.compile(
            r"^FUNCTION_(PASS|FAIL|SKIP|UNAVAILABLE)[ \t]+" +
            re.escape(token) + r"(?:[ \t]|$)"
        )
        records = [(m.group(1), line) for line in lines
                   if (m := pattern.match(line))]
    else:
        prefix = marker.removesuffix("PASS")
        pattern = re.compile(r"^" + re.escape(prefix) +
                             r"(PASS|FAIL|SKIP|UNAVAILABLE)(?:[ \t]|$)")
        records = [(m.group(1), line) for line in lines
                   if (m := pattern.match(line))]
        # The original ext4 probe emits a separate FUNCTION_* line even when
        # the required RUNTIME_V2_ROOTFS_RW marker is unavailable. Similarly,
        # IPv6 uses a custom PASS marker. Negative aliases provide honest
        # diagnostics but can never replace the mandated custom PASS marker.
        negative_alias = {
            "rootfs-rw": "ext4-block-persist",
            "ipv6-tcp": "ipv6-tcp-loopback-p2",
        }.get(check_id)
        if negative_alias:
            for line in lines:
                match = FUNCTION_RECORD.match(line)
                if (match and match.group(2) == negative_alias and
                        match.group(1) != "PASS"):
                    records.append((match.group(1), line))

    if not records:
        status = "MISSING"
    elif len(records) != 1:
        # A custom PASS plus a negative alias is a contradiction, not PASS.
        status = "CONFLICT" if len({r[0] for r in records}) > 1 else "DUPLICATE"
    else:
        status = records[0][0]
    return {"status": status, "evidence": [line for _state, line in records]}


def inspect_log(path: Path, checks: list[tuple[str, str]], rc: int,
                timed_out: bool, manifest: Path, image_size: int,
                initrd_size: int) -> dict:
    lines = path.read_text(errors="replace").splitlines()
    wrapper_records = [m for line in lines
                       if (m := RV64_EXPLICIT_INITRD.match(line))]
    wrapper_sizes_match = (
        None if not wrapper_records else
        len(wrapper_records) == 1 and
        int(wrapper_records[0].group(1)) == image_size and
        int(wrapper_records[0].group(2)) == initrd_size
    )
    features = {key: collect_check(lines, key, marker) for key, marker in checks}
    counts = dict(sorted(Counter(x["status"] for x in features.values()).items()))
    command = [sys.executable, str(CHECKER), "--profile", "p2",
               "--log", str(path), "--manifest", str(manifest),
               "--qemu-rc", str(rc)]
    if timed_out:
        command.append("--qemu-timeout")
    oracle = subprocess.run(command, capture_output=True, text=True, check=False)
    negative = [line for line in lines if OTHER_NEGATIVE.match(line)]
    kernel_faults = [line for line in lines if KERNEL_FATAL.search(line)]
    return {
        "log_path": str(path.resolve()),
        "log_sha256": sha256_file(path),
        "qemu_rc": rc,
        "qemu_timeout": timed_out,
        "strict_p2_pass": oracle.returncode == 0 and wrapper_sizes_match is not False,
        "strict_oracle_rc": oracle.returncode,
        "strict_oracle_summary": oracle.stdout.strip() if oracle.returncode == 0
                                 else oracle.stderr.strip().splitlines()[:6],
        "p2_counts": counts,
        "p2_checks": features,
        "all_negative_markers": negative,
        "kernel_faults_or_module_abi_errors": kernel_faults,
        "qemu_explicit_initrd_logged_sizes_match": wrapper_sizes_match,
        "qemu_explicit_initrd_records": len(wrapper_records),
        "p1_12_12_marker": sum(line == "RUNTIME_V2_SYSCALL_END pass=12 fail=0"
                                for line in lines) == 1,
        "normal_powerdown": sum("reboot: Power down" in line
                                for line in lines) == 1,
    }


def compare(gnu: dict, minic: dict) -> dict:
    g = gnu["p2_checks"]
    m = minic["p2_checks"]
    gnu_pass_minic_not_pass = [name for name in g if g[name]["status"] == "PASS"
                               and m[name]["status"] != "PASS"]
    minic_pass_gnu_not_pass = [name for name in g if m[name]["status"] == "PASS"
                               and g[name]["status"] != "PASS"]
    shared_skip = [name for name in g if
                   g[name]["status"] in ("SKIP", "UNAVAILABLE") and
                   m[name]["status"] in ("SKIP", "UNAVAILABLE")]
    statuses_equal = all(g[name]["status"] == m[name]["status"] for name in g)
    evidence_equal = all(g[name]["evidence"] == m[name]["evidence"] for name in g)

    gnu_execution_bad = (gnu["qemu_rc"] != 0 or gnu["qemu_timeout"] or
                         bool(gnu["kernel_faults_or_module_abi_errors"]))
    minic_execution_bad = (minic["qemu_rc"] != 0 or minic["qemu_timeout"] or
                           bool(minic["kernel_faults_or_module_abi_errors"]))

    if (gnu["qemu_explicit_initrd_logged_sizes_match"] is False or
            minic["qemu_explicit_initrd_logged_sizes_match"] is False):
        verdict = "QEMU_ARTIFACT_IDENTITY_MISMATCH"
    elif gnu["strict_p2_pass"] and minic["strict_p2_pass"]:
        verdict = "BOTH_FULL_P2_PASS"
    elif gnu_execution_bad:
        verdict = "GNU_BASELINE_NOT_QUALIFIED"
    elif minic_execution_bad and gnu["strict_p2_pass"]:
        verdict = "MINIC_RUNTIME_FAILURE_CANDIDATE"
    elif minic_execution_bad:
        verdict = "DIFFERENTIAL_RUNTIME_FAILURE_GNU_BASELINE_INCOMPLETE"
    elif gnu["strict_p2_pass"] and gnu_pass_minic_not_pass:
        verdict = "MINIC_REGRESSION_CANDIDATE"
    elif gnu_pass_minic_not_pass:
        verdict = "DIFFERENTIAL_SUSPECT_GNU_BASELINE_INCOMPLETE"
    elif statuses_equal and evidence_equal and shared_skip:
        verdict = "COMMON_ENVIRONMENT_GAP"
    elif not gnu["strict_p2_pass"]:
        verdict = "GNU_BASELINE_NOT_QUALIFIED"
    else:
        verdict = "MINIC_P2_INCOMPLETE"

    return {
        "verdict": verdict,
        "gnu_pass_minic_not_pass": gnu_pass_minic_not_pass,
        "minic_pass_gnu_not_pass": minic_pass_gnu_not_pass,
        "shared_skip_or_unavailable": shared_skip,
        "statuses_identical": statuses_equal,
        "feature_evidence_identical": evidence_equal,
        "important_boundary": (
            "Only BOTH_FULL_P2_PASS certifies full P2. A GNU PASS / MiniC "
            "non-PASS identifies a differential candidate, not a proven "
            "compiler bug until target/config/module/QEMU parity is audited."
        ),
    }


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--target", required=True)
    p.add_argument("--gnu-log", type=Path, required=True)
    p.add_argument("--minic-log", type=Path, required=True)
    p.add_argument("--gnu-image", type=Path, required=True)
    p.add_argument("--minic-image", type=Path, required=True)
    p.add_argument("--initrd", type=Path, required=True,
                   help="the one exact initrd input supplied to both QEMU commands")
    p.add_argument("--gnu-rc", type=int, required=True)
    p.add_argument("--minic-rc", type=int, required=True)
    p.add_argument("--gnu-timeout", action="store_true")
    p.add_argument("--minic-timeout", action="store_true")
    p.add_argument("--manifest", type=Path,
                   default=ROOT / "tests/external/linux/runtime_v2_checks.tsv")
    p.add_argument("--json-out", type=Path,
                   help="write a machine-readable report in addition to one-line verdict")
    return p


def main() -> int:
    args = parser().parse_args()
    try:
        files = (args.gnu_log, args.minic_log, args.gnu_image,
                 args.minic_image, args.initrd, args.manifest)
        for path in files:
            if not path.is_file():
                raise ValueError("missing input: " + str(path))
        checks = load_manifest(args.manifest)
        gnu = inspect_log(args.gnu_log, checks, args.gnu_rc,
                          args.gnu_timeout, args.manifest,
                          args.gnu_image.stat().st_size, args.initrd.stat().st_size)
        minic = inspect_log(args.minic_log, checks, args.minic_rc,
                            args.minic_timeout, args.manifest,
                            args.minic_image.stat().st_size, args.initrd.stat().st_size)
        comparison = compare(gnu, minic)
        report = {
            "target": args.target,
            "source_contract": "Linux 6.6.143 runtime_v2_checks.tsv P2 18 IDs",
            "fixture_provenance": {
                "gnu_image_path": str(args.gnu_image.resolve()),
                "gnu_image_sha256": sha256_file(args.gnu_image),
                "minic_image_path": str(args.minic_image.resolve()),
                "minic_image_sha256": sha256_file(args.minic_image),
                "common_initrd_path": str(args.initrd.resolve()),
                "common_initrd_sha256": sha256_file(args.initrd),
                "warning": "Hashes identify supplied files; QEMU command/config parity must also be audited independently.",
            },
            "gnu": gnu,
            "minic": minic,
            "comparison": comparison,
        }
        if args.json_out:
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(json.dumps(report, indent=2) + "\n")
        print(
            f"LINUX_P2_DIFFERENTIAL={comparison['verdict']} target={args.target} "
            f"gnu={gnu['p2_counts']} minic={minic['p2_counts']} "
            f"shared_skip={len(comparison['shared_skip_or_unavailable'])} "
            f"gnu_pass_minic_nonpass={len(comparison['gnu_pass_minic_not_pass'])} "
            f"full_p2_both_pass={gnu['strict_p2_pass'] and minic['strict_p2_pass']}"
        )
        return 0 if comparison["verdict"] == "BOTH_FULL_P2_PASS" else 1
    except (OSError, ValueError) as err:
        print("LINUX_P2_DIFFERENTIAL_INPUT_ERROR " + str(err), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
