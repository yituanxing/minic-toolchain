#!/usr/bin/env bash
# MC-014 / MC-015: one RV32+RV64 GNU C -> Core -> ABI frame regression.
# The local gate verifies generated ISA assembly and all four legal incoming
# SP alignment phases algebraically. It does NOT claim GNU ELF/QEMU execution.
set -Eeuo pipefail
: "$MINIC_CC"
: "$HOST_CC"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source="$root/tests/compiler/c0/riscv_overaligned_varargs.c"
"$HOST_CC" -std=gnu11 -fsyntax-only "$source"
"$HOST_CC" -E -P -x c "$source" -o "$work/source.i"

for xlen in 32 64; do
    if [[ "$xlen" == 32 ]]; then
        target="riscv32-linux"
        march="rv32i"
        mabi="ilp32"
    else
        target="riscv64-linux"
        march="rv64i"
        mabi="lp64"
    fi
    "$MINIC_CC" --target="$target" -march="$march" -mabi="$mabi" \
        -S "$work/source.i" -o "$work/$xlen.s"

    python3 - "$xlen" "$work/$xlen.s" <<'PY'
import pathlib
import re
import sys

xlen = int(sys.argv[1])
word = xlen // 8
asm = pathlib.Path(sys.argv[2]).read_text()
functions = {"va_frame_one": (1, 0), "va_frame_seven": (7, 0),
             "va_frame_eight": (8, 0), "va_frame_nine": (8, 1),
             "va_frame_dynamic": (2, 0)}

for name, (fixed_gprs, named_stack_slots) in functions.items():
    start = re.search(rf"(?m)^{name}:$", asm)
    end = re.search(rf"(?m)^\.size {name},", asm[start.end():]) if start else None
    if start is None or end is None:
        raise SystemExit(f"RISCV_VARARGS_ABI=FAIL xlen={xlen} missing={name}")
    body = asm[start.end():start.end() + end.start()]
    first_block = re.search(rf"(?m)^\.L{name}_(?:rv32|core)_bb0:$", body)
    if first_block is None:
        raise SystemExit(f"RISCV_VARARGS_ABI=FAIL xlen={xlen} missing_block={name}")
    prologue = body[:first_block.start()]
    instructions = body[first_block.end():]
    save_size = (8 - fixed_gprs) * word

    frame_match = (re.search(r"(?m)^  li t0, (\d+)\n  sub sp, sp, t0$", prologue)
                   if xlen == 32 else
                   re.search(r"(?m)^  addi sp, sp, -(\d+)$", prologue))
    if frame_match is None:
        raise SystemExit(f"RISCV_VARARGS_ABI=FAIL xlen={xlen} frame={name}")
    frame_size = int(frame_match.group(1))
    if "  li t1, -64\n  and sp, sp, t1\n" not in prologue:
        raise SystemExit(f"RISCV_VARARGS_ABI=FAIL xlen={xlen} align={name}")
    entry_match = re.search(
        rf"(?m)^  {'sw t2' if xlen == 32 else 'sd t0'}, (\d+)\(sp\)$",
        prologue)
    if entry_match is None:
        raise SystemExit(f"RISCV_VARARGS_ABI=FAIL xlen={xlen} entry_sp={name}")
    entry_slot = int(entry_match.group(1))
    expected_saves = [(f"a{i}", save_size - (i - fixed_gprs) * word)
                      for i in range(fixed_gprs, 8)]
    actual_saves = [(register, int(negative_offset)) for register, negative_offset
                    in re.findall(
                        rf"(?m)^  {'sw' if xlen == 32 else 'sd'} (a[0-7]), -(\d+)\(t0\)$",
                        prologue)]
    if actual_saves != expected_saves:
        raise SystemExit(
            f"RISCV_VARARGS_ABI=FAIL xlen={xlen} saves={name} "
            f"expected={expected_saves} got={actual_saves}")

    # All four legal 16-byte entry-SP alignment phases must fit within the
    # actual allocated frame and connect the register saves with caller args.
    for phase in (0, 16, 32, 48):
        e = 0x100000 + phase
        s = (e - frame_size) & -64
        cursor = e - save_size
        if cursor < s + entry_slot + word or cursor + save_size != e:
            raise SystemExit(
                f"RISCV_VARARGS_ABI=FAIL xlen={xlen} frame_collision={name} "
                f"phase={phase} E={e} S={s} V={save_size}")

    # Require the saved entry-SP slot, rather than an unrelated local load.
    # This is especially important for the RV32 zero-remaining-GPR case.
    if not re.search(
            rf"(?m)^  {'lw' if xlen == 32 else 'ld'} t0, {entry_slot}\((?:sp|s0)\)$",
            instructions):
        raise SystemExit(
            f"RISCV_VARARGS_ABI=FAIL xlen={xlen} cursor_entry={name} slot={entry_slot}")
    if save_size and f"  addi t0, t0, -{save_size}\n" not in instructions:
        raise SystemExit(f"RISCV_VARARGS_ABI=FAIL xlen={xlen} cursor_offset={name}")
    if named_stack_slots:
        start_cursor = (f"  {'lw' if xlen == 32 else 'ld'} t0, "
                        f"{entry_slot}(sp)\n")
        if start_cursor + f"  addi t0, t0, {named_stack_slots * word}\n" not in instructions:
            raise SystemExit(
                f"RISCV_VARARGS_ABI=FAIL xlen={xlen} named_stack_offset={name}")
    print(f"RISCV_VARARGS_ABI=PASS xlen={xlen} function={name} "
          f"saved_gprs={8-fixed_gprs} save_bytes={save_size} phases=4/4 "
          "evidence=SOURCE_ASM")
PY
done

printf '%s\n' \
  'RISCV_OVERALIGNED_VARARGS=PASS RV32=5/5 RV64=5/5 entry_sp_phases=4/4 evidence=SOURCE_ASM' \
  'RISCV_OVERALIGNED_VARARGS_QEMU=SKIP reason=GNU_cross_toolchain_and_QEMU_not_part_of_local_source_gate'
