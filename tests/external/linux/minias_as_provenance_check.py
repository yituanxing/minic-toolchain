#!/usr/bin/env python3
"""Audit the assembler of every Kbuild CC/AS object, not a success-count floor.

Linux saves the actual target compiler/assembler command in `.*.o.cmd` files.
For each such production owner, require a matching, successful MiniAS wrapper
trace entry.  This complements, rather than replaces, the fail-closed AS
wrapper: neither a successful Image nor `passes > 1000` alone proves that
all production object builds used MiniAS.

Only `CC -c` and direct `AS` object producers are in scope.  Aggregates from
GNU LD/objcopy/AR, the host toolchain and deliberate GCC feature probes must
not be mistaken for MiniAS-owned objects.
"""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path
import re
import shlex
import sys


OWNER_COMMAND = re.compile(r"^(?:savedcmd|cmd)_(.+?\.o)\s*:=\s*(.*)$")
HOST_OWNER_PREFIXES = (
    "scripts/", "tools/", "arch/x86/tools/", "arch/x86/boot/tools/",
)


def command_owners(kbuild_out: Path, compiler: str, assembler: str) -> dict[Path, Path]:
    """Return {absolute object: .cmd file} for target CC/AS invocations."""
    expected: dict[Path, Path] = {}
    compiler_name = Path(compiler).name
    assembler_name = Path(assembler).name
    for cmdfile in kbuild_out.rglob(".*.cmd"):
        lines = cmdfile.read_text(errors="replace").splitlines()
        index = 0
        while index < len(lines):
            match = OWNER_COMMAND.match(lines[index])
            index += 1
            if match is None:
                continue
            owner, command = match.groups()
            while command.endswith("\\") and index < len(lines):
                command = command[:-1] + lines[index].strip()
                index += 1
            try:
                tokens = shlex.split(command)
            except ValueError as exc:
                raise ValueError(f"unparseable Kbuild command {cmdfile}: {exc}") from exc
            executables = {Path(token).name for token in tokens}
            produces_cc = compiler_name in executables and "-c" in tokens
            # The x86 target names GNU `as` directly in some saved commands
            # and `x86_64-linux-gnu-as` in others; both must be accounted for.
            produces_as = (assembler_name in executables or
                           (assembler_name == "as" and
                            any(name.endswith("-as") for name in executables)))
            if not (produces_cc or produces_as):
                continue
            # x86's native target CC and HOSTCC can both be named `gcc`.
            # Kbuild's host-only utility directories are not kernel objects
            # and intentionally use the system compiler/assembler. All other
            # target CC commands remain in scope, even if their required -B
            # wrapper selection was accidentally dropped.
            if compiler_name == "gcc" and owner.startswith(HOST_OWNER_PREFIXES):
                continue
            # Kbuild's object-owner metadata is a stronger source of truth
            # than looking for arbitrary .o files, many of which GNU LD/AR
            # produce without any assembler invocation.
            obj = (kbuild_out / owner).resolve()
            try:
                obj.relative_to(kbuild_out)
            except ValueError as exc:
                raise ValueError(f"Kbuild owner escapes build root: {owner}") from exc
            old = expected.get(obj)
            if old is not None and old != cmdfile:
                raise ValueError(f"duplicate object owner: {obj}: {old}, {cmdfile}")
            expected[obj] = cmdfile
    return expected


def trace_owners(kbuild_out: Path, trace: Path) -> tuple[Counter[Path], Counter[str]]:
    passed: Counter[Path] = Counter()
    attempts: Counter[Path] = Counter()
    terminals: Counter[Path] = Counter()
    counters: Counter[str] = Counter()
    for index, line in enumerate(trace.read_text(errors="replace").splitlines(), 1):
        action, _, payload = line.partition(" ")
        if action not in {"assemble", "pass", "fail", "delegate-query", "query"}:
            continue
        if action in {"delegate-query", "query"}:
            counters["queries"] += 1
            continue
        counters[action] += 1
        try:
            fields = dict(field.split("=", 1) for field in shlex.split(payload)
                          if "=" in field)
        except ValueError as exc:
            raise ValueError(f"unparseable AS trace line {index}: {exc}") from exc
        raw_output = fields.get("output")
        if not raw_output:
            raise ValueError(f"missing AS trace owner at line {index}: {line!r}")
        # New wrappers include the actual invocation cwd. Older RV64 logs
        # omit it; Kbuild's O= build runs under kbuild_out by default.
        cwd = Path(fields.get("cwd", str(kbuild_out)))
        if not cwd.is_absolute():
            cwd = kbuild_out / cwd
        output = (cwd / raw_output).resolve()
        if action == "assemble":
            attempts[output] += 1
        else:
            terminals[output] += 1
        if action == "pass":
            # Temporary GCC feature probes may reuse one output pathname.
            # Production owner uniqueness is checked after loading .cmd.
            passed[output] += 1
    if counters["assemble"] != counters["pass"] + counters["fail"]:
        raise ValueError("AS wrapper trace incomplete: "
                         f"assemble={counters['assemble']} "
                         f"pass={counters['pass']} fail={counters['fail']}")
    if attempts != terminals:
        mismatches = [str(path) for path in attempts.keys() | terminals.keys()
                      if attempts[path] != terminals[path]]
        raise ValueError(f"AS wrapper incomplete owner lifecycles: {mismatches[:8]}")
    return passed, counters


def verify(kbuild_out: Path, trace: Path, compiler: str, assembler: str,
           min_objects: int, arch: str) -> tuple[int, int, Counter[str]]:
    out = kbuild_out.resolve()
    if not out.is_dir() or not trace.is_file():
        raise ValueError(f"missing build or AS trace: out={out} trace={trace}")
    expected = command_owners(out, compiler, assembler)
    passed, counters = trace_owners(out, trace)
    if len(expected) < min_objects:
        raise ValueError(f"insufficient target Kbuild CC/AS owners: "
                         f"{len(expected)} < {min_objects}")
    missing = sorted(expected.keys() - passed.keys())
    if missing:
        preview = ", ".join(str(path.relative_to(out)) for path in missing[:12])
        raise ValueError(f"{len(missing)} Kbuild objects bypassed MiniAS: {preview}")
    duplicated = sorted(obj for obj in expected if passed[obj] != 1)
    if duplicated:
        preview = ", ".join(str(path.relative_to(out)) for path in duplicated[:12])
        raise ValueError(f"{len(duplicated)} production objects assembled multiple times: {preview}")
    empty = sorted(path for path in expected if not path.is_file() or path.stat().st_size == 0)
    if empty:
        preview = ", ".join(str(path.relative_to(out)) for path in empty[:12])
        raise ValueError(f"{len(empty)} missing/empty production object(s): {preview}")
    return len(expected), counters["pass"], counters


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", required=True)
    parser.add_argument("--kbuild-out", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--assembler", required=True)
    parser.add_argument("--min-objects", type=int, default=1000)
    args = parser.parse_args()
    try:
        expected, passed, counters = verify(args.kbuild_out, args.trace,
                                            args.compiler, args.assembler,
                                            args.min_objects, args.arch)
    except (ValueError, OSError) as exc:
        print(f"MINIAS_LINUX_AS_PROVENANCE=FAIL arch={args.arch} reason={exc}",
              file=sys.stderr)
        return 1
    print(f"MINIAS_LINUX_AS_PROVENANCE=PASS arch={args.arch} "
          f"kbuild_cc_as_objects={expected} matched={expected} "
          f"mini_as_passes={passed} expected_probes={counters['fail']} "
          f"queries={counters['queries']} bypass=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
