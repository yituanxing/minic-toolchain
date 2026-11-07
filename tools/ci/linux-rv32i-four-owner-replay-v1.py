#!/usr/bin/env python3
"""Attest a bounded RV32 cached Linux replay: one direct + three vDSO owners.

All 1741 old MiniC objects come from an immutable seven-shard producer.  The
original GNU Kbuild may regenerate vdso-offsets.h during the first link; only
its three dependency-closed consumers may then be rebuilt using fresh GNU -E,
current MiniC -S and GNU AS.  Neither a fourth implicit consumer nor a cached
old consumer is allowed in the final mixed-epoch Image.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex


SCHEMA = "rv32i-four-owner-replay-v1"
PRIMARY = "block/blk-mq.o"
HEADER = "include/generated/vdso-offsets.h"
GENERATED = (
    "arch/riscv/kernel/alternative.o",
    "arch/riscv/kernel/signal.o",
    "arch/riscv/kernel/vdso.o",
)
CURRENT = frozenset((PRIMARY, *GENERATED))


class ReplayError(Exception):
    pass


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def required(path: Path) -> Path:
    if not path.is_file() or path.is_symlink() or not path.stat().st_size:
        raise ReplayError(f"missing, empty or symlinked evidence: {path}")
    return path


def records(path: Path, prefix: str = "") -> dict[str, str]:
    expected: dict[str, str] = {}
    for line in required(path).read_text().splitlines():
        columns = line.split("  ", 1)
        if len(columns) != 2 or not re.fullmatch(r"[0-9a-f]{64}", columns[0]):
            raise ReplayError(f"invalid SHA256 record in {path}: {line!r}")
        sha, name = columns
        if prefix and not name.startswith(prefix):
            raise ReplayError(f"wrong SHA256 record prefix: {name}")
        name = name[len(prefix):]
        if name in expected or Path(name).is_absolute() or ".." in Path(name).parts or not name.endswith(".o"):
            raise ReplayError(f"unsafe/duplicate C owner: {name}")
        expected[name] = sha
    return expected


def trace(path: Path) -> list[str]:
    names = required(path).read_text().splitlines()
    if len(names) != len(set(names)):
        raise ReplayError(f"duplicate successful compile trace: {path}")
    return names


def snapshots(merged: Path, out: Path) -> tuple[str, str]:
    before = json.loads(required(merged / "generated-before.json").read_text())
    after = json.loads(required(merged / "generated-after-first-link.json").read_text())
    if before.get("schema") != "minic-gnu-kbuild-regeneration-guard-v1" or after.get("schema") != before["schema"]:
        raise ReplayError("generated header evidence schema mismatch")
    old, new = before.get("headers"), after.get("headers")
    if not isinstance(old, dict) or not isinstance(new, dict):
        raise ReplayError("missing generated header SHA256 map")
    changed = {name for name in old.keys() | new.keys() if old.get(name) != new.get(name)}
    if changed != {HEADER}:
        raise ReplayError(f"unexpected generated-header change (must be vdso-offsets alone): {sorted(changed)}")
    if digest(required(out / HEADER)) != new[HEADER]:
        raise ReplayError("actual vDSO offset header disagrees with first-link snapshot")
    return old[HEADER], new[HEADER]


def gate(merged: Path, out: Path) -> tuple[str, str]:
    affected = required(merged / "header-affected-objects.txt").read_text().splitlines()
    if affected != list(GENERATED):
        raise ReplayError(f"unexpected generated-header consumer set: {affected!r}")
    if PRIMARY in affected:
        raise ReplayError("primary replay owner appeared in regenerated-header dependencies")
    before, after = snapshots(merged, out)
    print("RV32I_FOUR_OWNER_HEADER_GATE=PASS generated=" + HEADER +
          f" dependent_C=3 names={','.join(affected)} old_sha={before} new_sha={after}")
    return before, after


def original_command_source(cmd_path: Path, out: Path, source: Path) -> str:
    body = required(cmd_path).read_text(errors="replace")
    commands = [line.partition(":=")[2].strip() for line in body.splitlines()
                if re.match(r"^(?:savedcmd|cmd)_[^\n]+:=", line)]
    if len(commands) != 1:
        raise ReplayError(f"GNU Kbuild .cmd not unique: {cmd_path}")
    args = shlex.split(commands[0])
    if not args or Path(args[0]).name != "riscv64-linux-gnu-gcc" or "-c" not in args:
        raise ReplayError(f"frozen Kbuild command not GNU C: {cmd_path}")
    sources = [arg for index, arg in enumerate(args) if arg.endswith(".c") and
               (not index or args[index - 1] not in ("-include", "-imacros"))]
    if len(sources) != 1:
        raise ReplayError(f"frozen Kbuild command has ambiguous source: {cmd_path}")
    resolved = (out / sources[0]).resolve(strict=True)
    if resolved != source.resolve(strict=True):
        raise ReplayError(f"GNU Kbuild source diverged: {cmd_path}: {resolved} != {source}")
    return digest(required(source))


def attest(merged: Path, out: Path, source_root: Path,
           producer: Path, current: Path) -> None:
    header_before, header_after = gate(merged, out)
    old = records(merged / "minic-manifest-before-replay.sha256", "minic/")
    first = records(merged / "minic-manifest.sha256", "minic/")
    owner_trace = records(merged / "owner-replay-hashes.sha256")
    header_trace = records(merged / "header-replay-hashes.sha256")
    if len(old) != 1741 or set(old) != set(first):
        raise ReplayError(f"old seven-shard universe is not exactly 1741: {len(old)}")
    if set(owner_trace) != {PRIMARY} or set(header_trace) != set(GENERATED):
        raise ReplayError(f"incorrect current MiniC owner traces: primary={set(owner_trace)} header={set(header_trace)}")
    if trace(merged / "owner-replay-success.txt") != [PRIMARY] or \
       set(trace(merged / "header-replay-success.txt")) != set(GENERATED):
        raise ReplayError("GNU-E/MiniC-S/AS successful compile traces omit or duplicate a current owner")
    if {name for name in old if first[name] != old[name]} != {PRIMARY} or first[PRIMARY] != owner_trace[PRIMARY]:
        raise ReplayError("primary-only interim manifest has extra drift or disagrees with new block")
    cohort = {**first, **header_trace}
    if len(cohort) != 1741 or len(set(cohort) - CURRENT) != 1737:
        raise ReplayError("four-current / 1737-legacy cohort count mismatch")
    object_evidence = []
    for name, old_sha in old.items():
        actual = digest(required(out / name))
        if actual != cohort[name]:
            raise ReplayError(f"postlink C owner SHA drift: {name} expected={cohort[name]} actual={actual}")
        if name not in CURRENT and (cohort[name] != old_sha or
                                    digest(required(merged / "minic" / name)) != old_sha):
            raise ReplayError(f"mutated historical seven-shard owner: {name}")
    for name in (PRIMARY, *GENERATED):
        rel = name.removesuffix(".o")
        base = out / f"{rel}.minic-stage2"
        command = merged / "gnu-cmd" / Path(name).parent / f".{Path(name).name}.cmd"
        source = source_root / f"{rel}.c"
        source_sha = original_command_source(command, out, source)
        object_evidence.append((name, old[name], cohort[name], source_sha,
                                digest(command), digest(required(Path(f"{base}.i"))),
                                digest(required(Path(f"{base}.s")))))
    path = merged / "four-owner-final-manifest.sha256"
    path.write_text("".join(f"{cohort[name]}  {name}\n" for name in old))
    image = required(out / "arch/riscv/boot/Image")
    with (merged / "four-owner-attestation.txt").open("w") as stream:
        for key, value in (
            ("schema", SCHEMA), ("original_C_owners", "1741"),
            ("old_producer_C_owners", "1737"), ("current_head_C_owners", "4"),
            ("full_current_head_C", "false"),
            ("source_mode", "original_frozen_GNU_Kbuild_cmd->live_GNU_E->current_MiniC_S->GNU_AS"),
            ("generated_header", HEADER), ("generated_header_old_sha256", header_before),
            ("generated_header_after_first_link_sha256", header_after),
            ("original_1741_manifest_sha256", digest(merged / "minic-manifest-before-replay.sha256")),
            ("current_four_owner_manifest_sha256", digest(path)),
            ("old_producer_minic_sha256", digest(required(producer))),
            ("current_minic_sha256", digest(required(current))),
            ("image_sha256", digest(image)),
        ):
            stream.write(f"{key}={value}\n")
        for name, old_sha, new_sha, source_sha, cmd_sha, i_sha, s_sha in object_evidence:
            stream.write(f"owner={name} old_sha256={old_sha} current_sha256={new_sha} "
                         f"original_source_sha256={source_sha} frozen_cmd_sha256={cmd_sha} "
                         f"fresh_gnu_i_sha256={i_sha} current_minic_s_sha256={s_sha}\n")
    print(f"RV32I_FOUR_OWNER_COHORT=PASS old_producer_C=1737 current_head_C=4 "
          f"original_C=1741 no_GNU_C_fallback=true mixed_epochs=true "
          f"manifest_sha={digest(path)} image_sha={digest(image)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("gate", "attest"), required=True)
    parser.add_argument("--merged", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--linux-source", type=Path)
    parser.add_argument("--producer-minic", type=Path)
    parser.add_argument("--current-minic", type=Path)
    args = parser.parse_args()
    try:
        if args.mode == "gate":
            gate(args.merged, args.out)
        else:
            if not all((args.linux_source, args.producer_minic, args.current_minic)):
                raise ReplayError("attest requires frozen original Linux source and both MiniC compilers")
            attest(args.merged, args.out, args.linux_source,
                   args.producer_minic, args.current_minic)
    except (ReplayError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"RV32I_FOUR_OWNER_REPLAY=FAIL {exc}")
        return 8
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
