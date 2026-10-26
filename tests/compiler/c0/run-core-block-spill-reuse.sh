#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/core-block-spill-reuse
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.c" <<'SRC'
int core_block_spill_reuse(int x)
{
    int y = x;
    if (x & 1) y += 1;
    if (x & 2) y += 2;
    if (x & 4) y += 4;
    if (x & 8) y += 8;
    if (x & 16) y += 16;
    if (x & 32) y += 32;
    if (x & 64) y += 64;
    if (x & 128) y += 128;
    if (x & 256) y += 256;
    if (x & 512) y += 512;
    if (x & 1024) y += 1024;
    if (x & 2048) y += 2048;
    return y;
}
SRC

MINIC_BOOTSTRAP_TRACE=1 "$minic" -S "$work/input.c" -o "$work/output.s"   >"$work/stdout" 2>"$work/trace"

python3 - "$work/trace" <<'PY'
import re
import sys

text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
matches = re.findall(
    r"stage=core-codegen-frame state=end function=core_block_spill_reuse "
    r"frame_size=(\d+) value_slots=(\d+) values=(\d+)",
    text,
)
if len(matches) != 1:
    raise SystemExit("missing or duplicate frame trace")
frame, slots, values = map(int, matches[0])
if not (0 < slots < values):
    raise SystemExit(f"spill reuse inactive: frame={frame} slots={slots} values={values}")
print(
    "PASS compiler/c0/core-block-spill-reuse "
    f"frame={frame} value_slots={slots} values={values}"
)
PY

test -s "$work/output.s"

# RV32I consumes the SAME Common SSA last-use peak/assignment algorithms
# with ILP32-sized units. Compile without external assemblers or QEMU:
# the resulting .s checks both new function-wide planning entry points.
"$minic" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/input.c" -o "$work/output-rv32.s" \
  >"$work/rv32-stdout" 2>"$work/rv32-trace"
test -s "$work/output-rv32.s"
grep -Fq 'core_block_spill_reuse' "$work/output-rv32.s"
printf 'PASS compiler/c0/core-block-spill-reuse-rv32i ILP32 common SSA layout\n'
