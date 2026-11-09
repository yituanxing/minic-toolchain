#!/usr/bin/env python3
"""Fail-closed LoongArch64 Linux 6.6.143 P2 17/18 structural-block oracle.

An accepted result is explicitly STRUCTURAL_BLOCKED, NEVER a full P2 PASS.
The 17 live checks must each execute once; no SKIP, synthetic KVM success,
kernel fault, timeout or missing QEMU return code can be accepted.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/external/linux"))
import runtime_v2_log_check as canonical  # noqa: E402

EXPECTED_BLOCK = (
    "STRUCTURAL_BLOCKED kvm-device-created "
    "reason=linux-6.6.143-loongarch-no-kvm"
)
EXPECTED_END = (
    "RUNTIME_V2_P2_PARTIAL_END "
    "pass=17 fail=0 skip=0 structural_blocked=1"
)
BUILTINS = (
    "IPV6 OVERLAY_FS BTRFS_FS BLK_DEV_DM EXT4_FS VIRTIO_BLK VIRTIO_PCI "
    "VIRTIO_MMIO NETFILTER NF_CONNTRACK IP_NF_IPTABLES IP6_NF_IPTABLES "
    "IP_NF_FILTER IP6_NF_FILTER BRIDGE BRIDGE_NETFILTER VETH "
    "CRYPTO_USER_API_HASH CRYPTO_SHA256 CRYPTO_SHA512 CRYPTO_SHA3 "
    "CRYPTO_BLAKE2B DRM_VIRTIO_GPU BLK_DEV_NVME NET_NS NAMESPACES"
).split()

CANONICAL_PROBE = ROOT / "tests/external/linux/runtime_initramfs/p2_probe.c"
LOONG_PROBE = ROOT / "tests/external/linux/runtime_initramfs/loongarch64_p2_17_probe.c"


def source_call_block(source: str) -> str:
    """Retain the literal original main call list, including every KAT byte."""
    main = source.find("int main(void) {")
    if main < 0:
        raise ValueError("missing-main")
    begin = source.find("    overlay_copy_up();\n", main)
    last = "    nvme_controller_bound();"
    end = source.find(last, begin)
    if begin < 0 or end < 0:
        raise ValueError("missing-canonical-call-boundary")
    return source[begin:end + len(last)]


def source_call_agreement(canonical_source: str, loong_source: str) -> bool:
    canonical_block = source_call_block(canonical_source)
    if canonical_block.count("    kvm_api_device();\n") != 1:
        return False
    expected = canonical_block.replace("    kvm_api_device();\n", "")
    return source_call_block(loong_source) == expected


def exact_count(log: str, literal: str) -> int:
    return len(re.findall(r"^" + re.escape(literal) + r"[ \t]*$", log, re.M))


def check(log: str, qemu_rc: int, config: str, source: Path) -> list[str]:
    problems: list[str] = []
    if qemu_rc != 0:
        problems.append(f"qemu-rc={qemu_rc}")
    if (not (source / "arch/loongarch/kernel").is_dir() or
            (source / "arch/loongarch/kvm").exists()):
        problems.append("arch-loongarch-kvm-not-proven-absent")
    makefile = source / "Makefile"
    make_text = makefile.read_text() if makefile.is_file() else ""
    for variable, number in (("VERSION", "6"), ("PATCHLEVEL", "6"),
                             ("SUBLEVEL", "143")):
        if not re.search(rf"^{variable}\s*=\s*{number}\s*$", make_text, re.M):
            problems.append(f"not-linux-6.6.143-{variable}")
    settings = set(config.splitlines())
    for symbol in BUILTINS:
        if f"CONFIG_{symbol}=y" not in settings:
            problems.append(f"missing-builtin-{symbol}")
    if re.search(r"^CONFIG_KVM(?:_[A-Z0-9_]+)?=[ym]$", config, re.M):
        problems.append("unexpected-kvm-config")

    for pattern in canonical.FATAL_PATTERNS:
        if re.search(pattern, log, re.I):
            problems.append("kernel-fatal=" + pattern)
    for bad in canonical.STRICT_BAD_MARKER.finditer(log):
        problems.append("forbidden-marker=" + bad.group(0))

    checks = canonical.load_checks(
        ROOT / "tests/external/linux/runtime_v2_checks.tsv", "p2")
    p2 = [(identifier, pattern) for group, identifier, pattern, _ in checks
          if group == "p2"]
    if len(p2) != 18 or {identifier for identifier, _ in p2} != canonical.P2_CHECK_IDS:
        problems.append("manifest-not-canonical-18")
    expected_function_names = set()
    for group, identifier, pattern, _ in checks:
        if group == "p2" and identifier == "kvm":
            continue
        if group in ("p0", "p1"):
            if len(re.findall(pattern, log, re.M)) != 1:
                problems.append(f"{group}-{identifier}-not-once")
        elif pattern.startswith("FUNCTION_PASS "):
            name = pattern.removeprefix("FUNCTION_PASS ")
            expected_function_names.add(name)
            if exact_count(log, pattern) != 1:
                problems.append(f"p2-{identifier}-not-once")
        elif exact_count(log, pattern) != 1:
            problems.append(f"p2-{identifier}-not-once")

    # The canonical ext4 check has BOTH the real function result and an
    # additional rootfs-RW manifest marker. Neither may be omitted.
    expected_function_names.add("ext4-block-persist")
    if exact_count(log, "FUNCTION_PASS ext4-block-persist") != 1:
        problems.append("p2-ext4-operation-not-once")
    actual_functions = re.findall(r"^FUNCTION_PASS[ \t]+([^\r\n]+)", log, re.M)
    if len(actual_functions) != 16 or set(actual_functions) != expected_function_names:
        problems.append("unexpected-function-pass-list")
    for marker in (EXPECTED_BLOCK, EXPECTED_END,
                   "INIT_P2_PROBE=STRUCTURAL_BLOCKED", "RUNTIME_V2_P2_BEGIN"):
        if exact_count(log, marker) != 1:
            problems.append("missing-or-duplicate=" + marker)
    if len(re.findall(
        r"^P2_CONNTRACK_TRACE before=0 after=1 attempts=[1-9][0-9]*[ \t]*$",
        log, re.M
    )) != 1:
        problems.append("conntrack-isolated-zero-to-one-not-proven")
    if re.search(r"^RUNTIME_V2_P2_(?:FULL_)?END\b", log, re.M):
        problems.append("forbidden-p2-full-or-legacy-end")
    if re.search(r"^INIT_P2_PROBE=PASS\b|^FUNCTION_PASS kvm-device-created\b", log, re.M):
        problems.append("forbidden-kvm-or-wrapper-pass")
    if len(re.findall(
        r"^(?:\[[ \d.]+\][ \t]+)?reboot: Power down[ \t]*$", log, re.M
    )) != 1:
        problems.append("powerdown-not-exactly-once")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-guard-only", action="store_true")
    parser.add_argument("--log", type=Path)
    parser.add_argument("--qemu-rc", type=int)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    if args.source_guard_only:
        try:
            identical = source_call_agreement(
                CANONICAL_PROBE.read_text(), LOONG_PROBE.read_text())
        except (OSError, ValueError):
            identical = False
        if not identical:
            print("LOONGARCH64_P2_SOURCE_GUARD=FAIL reason=non-kvm-call-or-kat-drift",
                  file=sys.stderr)
            return 1
        print("LOONGARCH64_P2_SOURCE_GUARD=PASS removed=only-kvm-api-call")
        return 0
    if args.log is None or args.qemu_rc is None or args.config is None or args.source is None:
        parser.error("--log, --qemu-rc, --config, --source required for real-QEMU check")
    try:
        problems = check(args.log.read_text(errors="replace"), args.qemu_rc,
                         args.config.read_text(), args.source)
    except OSError as exc:
        problems = [f"missing-input={exc}"]
    if problems:
        for problem in problems:
            print("LOONGARCH64_GNU_QEMU_P2=FAIL reason=" + problem, file=sys.stderr)
        return 1
    print("LOONGARCH64_GNU_QEMU_P2=STRUCTURAL_BLOCKED "
          "p2=17/18 checks=39/40 kvm=linux-6.6.143-arch-absent "
          "qemu_rc=0 fail=0 skip=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
