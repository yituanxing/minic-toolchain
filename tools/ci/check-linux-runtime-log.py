#!/usr/bin/env python3
"""Validate observable Linux/QEMU runtime contracts from a captured log."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


DEFAULT_FORBIDDEN = (
    "Kernel panic",
    "Oops",
    "BUG:",
    "Unable to handle kernel",
    "unhandled signal",
    "soft lockup",
    "hung task",
    "Unknown symbol",
    "invalid module format",
    "PLT error",
)

PROFILE_REQUIRED = {
    "initramfs-init": (
        "Run /init as init process",
        "USER_SHELL_OK",
        "DONE_COMMANDS",
    ),
    "rdinit-shell": (
        "Run /bin/sh as init process",
        "RDINIT_SH_OK",
        "DONE_RDINIT",
    ),
    "poweroff": (),
    "generic": (),
}

POWERDOWN_PATTERNS = (
    r"reboot:\s+Power down",
    r"Power down",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument(
        "--profile",
        choices=sorted(PROFILE_REQUIRED),
        default="generic",
    )
    parser.add_argument("--require", action="append", default=[])
    parser.add_argument("--forbid", action="append", default=[])
    parser.add_argument(
        "--no-default-forbidden",
        action="store_true",
        help="do not apply the common Linux bad-marker set",
    )
    parser.add_argument("--require-powerdown", action="store_true")
    parser.add_argument("--endpoint")
    parser.add_argument("--expect-module-pass", type=int)
    parser.add_argument("--expect-function-pass", type=int)
    parser.add_argument("--expect-function-fail", type=int, default=None)
    parser.add_argument("--qemu-rc", type=int)
    parser.add_argument(
        "--allow-timeout-after-endpoint",
        action="store_true",
        help="accept qemu rc=124 only if all required markers/endpoints passed",
    )
    return parser.parse_args()


def count_token(text: str, token: str) -> int:
    return text.count(token)


def find_counter(text: str, name: str) -> int | None:
    patterns = (
        rf"\b{name}\s*=\s*(\d+)",
        rf"\b{name}\s+(\d+)",
    )
    values: list[int] = []
    for pattern in patterns:
        values.extend(int(match) for match in re.findall(pattern, text))
    return values[-1] if values else None


def fail(message: str, failures: list[str]) -> None:
    failures.append(message)


def main() -> int:
    args = parse_args()
    if not args.log.is_file():
        print(f"RUNTIME_GATE FAIL missing_log={args.log}", file=sys.stderr)
        return 2

    text = args.log.read_text(errors="replace")
    failures: list[str] = []

    required = list(PROFILE_REQUIRED[args.profile])
    required.extend(args.require)
    for marker in required:
        if marker not in text:
            fail(f"missing required marker: {marker}", failures)

    forbidden = list(args.forbid)
    if not args.no_default_forbidden:
        forbidden = list(DEFAULT_FORBIDDEN) + forbidden
    forbidden_hits: list[tuple[str, int]] = []
    for marker in forbidden:
        count = count_token(text, marker)
        if count:
            forbidden_hits.append((marker, count))
            fail(f"forbidden marker: {marker} count={count}", failures)

    endpoint_ok = True
    if args.endpoint is not None:
        endpoint_ok = args.endpoint in text
        if not endpoint_ok:
            fail(f"missing endpoint: {args.endpoint}", failures)

    powerdown_required = args.require_powerdown or args.profile == "poweroff"
    powerdown_ok = any(re.search(pattern, text) for pattern in POWERDOWN_PATTERNS)
    if powerdown_required and not powerdown_ok:
        fail("normal powerdown marker not found", failures)

    counters = {
        "MODULE_PASS": find_counter(text, "MODULE_PASS"),
        "FUNCTION_PASS": find_counter(text, "FUNCTION_PASS"),
        "FUNCTION_FAIL": find_counter(text, "FUNCTION_FAIL"),
    }

    if args.expect_module_pass is not None:
        actual = counters["MODULE_PASS"]
        if actual != args.expect_module_pass:
            fail(
                f"MODULE_PASS expected={args.expect_module_pass} actual={actual}",
                failures,
            )

    if args.expect_function_pass is not None:
        actual = counters["FUNCTION_PASS"]
        if actual != args.expect_function_pass:
            fail(
                f"FUNCTION_PASS expected={args.expect_function_pass} actual={actual}",
                failures,
            )

    if args.expect_function_fail is not None:
        actual = counters["FUNCTION_FAIL"]
        if actual != args.expect_function_fail:
            fail(
                f"FUNCTION_FAIL expected={args.expect_function_fail} actual={actual}",
                failures,
            )

    qemu_rc_ok = True
    if args.qemu_rc is not None:
        qemu_rc_ok = args.qemu_rc == 0
        if (
            args.qemu_rc == 124
            and args.allow_timeout_after_endpoint
            and not failures
            and endpoint_ok
        ):
            qemu_rc_ok = True
        if not qemu_rc_ok:
            fail(f"qemu rc={args.qemu_rc}", failures)

    status = "PASS" if not failures else "FAIL"
    print(
        "RUNTIME_GATE"
        f" status={status}"
        f" profile={args.profile}"
        f" required={len(required)}"
        f" forbidden_hits={sum(count for _, count in forbidden_hits)}"
        f" powerdown={int(powerdown_ok)}"
        f" endpoint={int(endpoint_ok)}"
        f" module_pass={counters['MODULE_PASS']}"
        f" function_pass={counters['FUNCTION_PASS']}"
        f" function_fail={counters['FUNCTION_FAIL']}"
        f" qemu_rc={args.qemu_rc}"
    )

    if failures:
        for item in failures:
            print(f"RUNTIME_GATE_REASON {item}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
