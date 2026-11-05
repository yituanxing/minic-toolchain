#!/bin/sh
set -eu
mini=${1:?usage: probe-typed-extended-float-runtime.sh path/to/minic-cc}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file="$root/tests/compiler/c0/probe-typed-extended-float-runtime.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

cc -std=gnu11 -E -P "$source_file" -o "$work/source.i"
cc -std=gnu11 -O0 -no-pie "$source_file" -o "$work/gnu"
for target in x86_64-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$mini" --target="$target" -S "$work/source.i" -o "$work/$target.s"
done
grep -Eq '[[:space:]]call[[:space:]]+__addtf3(@PLT)?' "$work/x86_64-linux.s"
grep -Eq '[[:space:]]faddp' "$work/x86_64-linux.s"
grep -Eq '[[:space:]]setnp[[:space:]]' "$work/x86_64-linux.s"
grep -Eq '[[:space:]]call[[:space:]]+__addtf3' "$work/riscv64-linux.s"
grep -Eq '[[:space:]]seqz[[:space:]]+(a0|x10),[[:space:]]*(a0|x10)' "$work/riscv64-linux.s"
grep -Eq '[[:space:]]slt[[:space:]]+(a0|x10),[[:space:]]*(a0|x10),[[:space:]]*(zero|x0)' "$work/riscv64-linux.s"
grep -Eq '[[:space:]]slti[[:space:]]+(a0|x10),[[:space:]]*(a0|x10),[[:space:]]*1' "$work/riscv64-linux.s"
grep -Eq '[[:space:]]bl[[:space:]]+__addtf3' "$work/aarch64-linux.s"
grep -Eq '[[:space:]]cset[[:space:]]+w0,[[:space:]]*eq' "$work/aarch64-linux.s"
grep -Eq '[[:space:]]cset[[:space:]]+w0,[[:space:]]*lt' "$work/aarch64-linux.s"
grep -Eq '[[:space:]]cset[[:space:]]+w0,[[:space:]]*le' "$work/aarch64-linux.s"
grep -Eq '[[:space:]]bl[[:space:]]+__addtf3' "$work/loongarch64-linux.s"
grep -Eq '[[:space:]]sltu[[:space:]]+\$a0,[[:space:]]*\$zero,[[:space:]]*\$a0' "$work/loongarch64-linux.s"
grep -Eq '[[:space:]]slt[[:space:]]+\$a0,[[:space:]]*\$a0,[[:space:]]*\$zero' "$work/loongarch64-linux.s"
# A binary128 value has two physical 64-bit payload carriers. In particular,
# the upper limb of the exact +2.0 constant cannot be dropped or interpreted
# as an ordinary binary64 floating register value.
grep -Eq '[[:space:]]li[.]d[[:space:]]+\$t0,[[:space:]]*4611826755915743232' "$work/loongarch64-linux.s"
cc -no-pie "$work/x86_64-linux.s" -o "$work/mini"
set +e
"$work/gnu"
gnu_status=$?
"$work/mini"
mini_status=$?
set -e
test "$gnu_status" -eq 42 && test "$mini_status" -eq 42
printf '%s\n' 'TYPED_EXTENDED_FLOAT=PASS external_same_name=ordinary ordered_boolean_predicates=3 x86_native=42 four_target_S=4'
