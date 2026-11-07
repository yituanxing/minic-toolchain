#!/bin/sh
# A source-level regression for true 32B local alignment, including a ninth
# stacked argument and dynamic alloca. The native ELF/QEMU campaign tests the
# generated machine instructions independently with GNU tools.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
source="$root/tests/compiler/c0/lp64_overaligned_stack_frame.c"
work=$(mktemp -d)
trap 'rm -r "$work"' EXIT HUP INT TERM

"$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/input.i"
for target in loongarch64-linux aarch64-linux; do
    "$minic" --target="$target" -S "$work/input.i" -o "$work/$target.s"
    test -s "$work/$target.s"
    grep -Fq 'overalign_with_stack_argument:' "$work/$target.s"
    grep -Fq 'overalign_dynamic_stack:' "$work/$target.s"
    printf 'LP64_OVERALIGNED_FRAME_SOURCE=PASS target=%s local=32B incoming_stack=original_SP dynamic_alloca=1\n' "$target"
done

# LP64D must preserve entry SP separately from its aligned $s0 fixed base:
# merely changing an object-offset guard would never produce these actions.
grep -Fq '  addi.d $s0, $sp, 31' "$work/loongarch64-linux.s"
grep -Fq '  li.d $t3, -32' "$work/loongarch64-linux.s"
grep -Fq '  move $sp, $t0' "$work/loongarch64-linux.s"
