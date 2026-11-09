#!/usr/bin/env python3
"""Validate Linux QEMU logs against the tiered runtime-v2 marker contract."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

from legacy_145_runtime_bridge import classify_log as classify_legacy_145

PROFILE_ORDER = {"p0": 0, "p1": 1, "p2": 2, "p3": 3}
FATAL_PATTERNS = (
    r"Unknown symbol",
    r"invalid module format",
    r"\bOops:",
    r"\bBUG:",
    r"Kernel panic",
    r"soft lockup",
    r"workqueue lockup",
)
P2_CHECK_IDS = frozenset({
    "rootfs-rw", "overlay-copy-up", "iptables4", "iptables6",
    "conntrack", "bridge", "br-netfilter", "veth", "ipv6-tcp",
    "sha256", "sha512", "sha3", "blake2b", "btrfs", "kvm",
    "dm", "virtio-gpu", "nvme",
})
# A partial P2 probe has its own RUNTIME_V2_P2_END summary. Only the
# separate 18-check endpoint can certify the entire P2 contract.
P2_FULL_ENDPOINT = "RUNTIME_V2_P2_FULL_END pass=18 fail=0 skip=0"
STRICT_BAD_MARKER = re.compile(
    r"^(?:FUNCTION|MODULE)_(?:FAIL|SKIP|UNAVAILABLE)\b|"
    r"^(?:INIT_(?:P2|RUNTIME)_PROBE|RUNTIME_V2_[A-Z0-9_]+)=(?:FAIL|SKIP|UNAVAILABLE)\b|"
    r"^RUNTIME_V2_P2_(?:FULL_)?END\s+pass=\d+\s+fail=[1-9]\d*|"
    r"^RUNTIME_V2_P2_(?:FULL_)?END\s+pass=\d+\s+fail=\d+\s+skip=[1-9]\d*",
    re.M,
)

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=PROFILE_ORDER, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument(
        "--qemu-rc", type=int,
        help="actual QEMU process return code; mandatory for full P2/P3",
    )
    parser.add_argument(
        "--qemu-timeout", action="store_true",
        help="report an expired QEMU timeout (never a pass)",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("runtime_v2_checks.tsv"),
    )
    return parser.parse_args()

def load_checks(path: Path, profile: str) -> list[tuple[str, str, str, str]]:
    selected: list[tuple[str, str, str, str]] = []
    ceiling = PROFILE_ORDER[profile]
    for raw in path.read_text().splitlines():
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 4:
            raise SystemExit(f"invalid runtime-v2 manifest row: {raw!r}")
        row_profile, check_id, pattern, fault_class = fields
        if row_profile not in PROFILE_ORDER:
            raise SystemExit(f"invalid profile in manifest: {row_profile}")
        if PROFILE_ORDER[row_profile] <= ceiling:
            selected.append((row_profile, check_id, pattern, fault_class))
    return selected

def check_strict_runtime(text: str, profile: str, qemu_rc: int | None,
                         timed_out: bool, checks: list[tuple[str, str, str, str]]) -> list[str]:
    """Fail closed for P2/P3. The P0/P1 legacy marker check stays unchanged."""
    failures: list[str] = []
    if qemu_rc is None:
        failures.append("missing-qemu-rc")
    elif qemu_rc != 0:
        failures.append(f"qemu-rc={qemu_rc}")
    if timed_out:
        failures.append("qemu-timeout")
    if len(re.findall(
        r"^(?:\[[ \d.]+\][ \t]+)?reboot: Power down[ \t]*$", text, re.M
    )) != 1:
        failures.append("powerdown-not-exactly-once")

    for match in STRICT_BAD_MARKER.finditer(text):
        failures.append("bad-marker=" + match.group(0))

    p2_rows = [(check_id, pattern) for row, check_id, pattern, _ in checks
               if row == "p2"]
    if len(p2_rows) != 18 or {check_id for check_id, _ in p2_rows} != P2_CHECK_IDS:
        failures.append("p2-manifest-is-not-canonical-18")
    for check_id, pattern in p2_rows:
        if pattern.startswith("FUNCTION_PASS "):
            token = pattern.removeprefix("FUNCTION_PASS ")
            # Avoid prefix spoofing (e.g. id-extra) and repeated PASS markers.
            exact = r"^FUNCTION_PASS[ \t]+" + re.escape(token) + r"(?:[ \t].*)?$"
        else:
            exact = r"^" + re.escape(pattern) + r"[ \t]*$"
        # The two RUNTIME_V2_* manifest patterns are plain literal strings.
        count = len(re.findall(exact, text, re.M))
        if count != 1:
            failures.append(f"p2-{check_id}-occurrences={count}")

    if len(re.findall(r"^" + re.escape(P2_FULL_ENDPOINT) + r"\s*$", text, re.M)) != 1:
        failures.append("missing-or-duplicate-p2-full-endpoint")

    if profile == "p3":
        # The historic 145/203 oracle is RV64-only. A different ISA requires
        # its own audited fixture and exact expected ID manifest.
        result = classify_legacy_145(text, qemu_rc, timed_out)
        if result["verdict"] != "PASS":
            failures.append("p3-historical-id-or-runtime-mismatch=" + result["verdict"])
    return failures

def main() -> int:
    args = parse_args()
    if not args.log.is_file():
        raise SystemExit(f"missing QEMU log: {args.log}")
    if not args.manifest.is_file():
        raise SystemExit(f"missing runtime manifest: {args.manifest}")

    text = args.log.read_text(errors="replace")
    fatal_hits = [pattern for pattern in FATAL_PATTERNS if re.search(pattern, text, re.I)]
    if fatal_hits:
        print(
            "LINUX_RUNTIME_V2_FAIL reason=fatal-markers "
            + " patterns="
            + ",".join(fatal_hits),
            file=sys.stderr,
        )
        return 1

    checks = load_checks(args.manifest, args.profile)
    if args.profile in ("p2", "p3"):
        failures = check_strict_runtime(
            text, args.profile, args.qemu_rc, args.qemu_timeout, checks)
        if failures:
            for reason in failures:
                print(f"LINUX_RUNTIME_V2_STRICT_FAIL reason={reason}", file=sys.stderr)
            return 1

    missing: list[tuple[str, str, str]] = []
    for _, check_id, pattern, fault_class in checks:
        if re.search(pattern, text, re.M) is None:
            missing.append((check_id, fault_class, pattern))

    if args.profile == "p3":
        module_pass = len(re.findall(r"^MODULE_PASS\b", text, re.M))
        function_pass = len(re.findall(r"^FUNCTION_PASS\b", text, re.M))
        if module_pass != 145 or function_pass != 203:
            print(
                f"LINUX_RUNTIME_V2_FAIL profile=p3 module_pass={module_pass} "
                f"function_pass={function_pass} expected=145/203",
                file=sys.stderr,
            )
            return 1

    if missing:
        for check_id, fault_class, pattern in missing:
            print(
                f"LINUX_RUNTIME_V2_MISSING id={check_id} class={fault_class} "
                f"pattern={pattern}",
                file=sys.stderr,
            )
        print(
            f"LINUX_RUNTIME_V2_FAIL profile={args.profile} "
            f"missing={len(missing)} total={len(checks)}",
            file=sys.stderr,
        )
        return 1

    print(
        f"LINUX_RUNTIME_V2_PROFILE=PASS profile={args.profile} "
        f"checks={len(checks)} fatal=0"
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
