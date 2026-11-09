#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/loongarch64-immediate-core-fold
mkdir -p "$work"
source="$root/tests/compiler/c0/loongarch64_immediate_core_fold.c"
cc -E -P -std=gnu11 "$source" -o "$work/source.i"
cc -O2 -Wall -Wextra -Werror -std=gnu11 "$source" -o "$work/gnu"
for target in loongarch64-linux x86_64-linux aarch64-linux riscv64-linux riscv32-linux; do
    "$MINIC_CC" --target="$target" -S "$work/source.i" -o "$work/$target.s"
done
assembly="$work/loongarch64-linux.s"
grep -Fq '  addi.d $t0, $t0, 7' "$assembly"
grep -Fq '  addi.d $t0, $t0, -2048' "$assembly"
grep -Fq '  andi $t0, $t0, 4095' "$assembly"
grep -Fq '  xori $t0, $t0, 7' "$assembly"
grep -Fq '  li.d $t0, 2048' "$assembly"
if grep -Fq '  addi.d $t0, $t0, 2048' "$assembly"; then
    echo 'LOONG_IMMEDIATE_CORE_FOLD=FAIL invalid +2048 encoding' >&2
    exit 1
fi
# Both a retained volatile load and its immediate consumer must be present;
# the intermediate SSA spill/reload and pure constant materialization vanish.
sed -n '/^minic_immediate_load_chain:/,/^\.size minic_immediate_load_chain/p' \
    "$assembly" > "$work/load_chain.s"
grep -Fq '  ld.d $t0, $t1, 0' "$work/load_chain.s"
grep -Fq '  addi.d $t0, $t0, 7' "$work/load_chain.s"
if grep -Fq '  li.d $t0, 7' "$work/load_chain.s" ||
   grep -Fq '  st.d $t0, $sp, 8' "$work/load_chain.s"; then
    echo 'LOONG_IMMEDIATE_CORE_FOLD=FAIL redundant constant or load spill' >&2
    exit 1
fi
cc -c -x assembler "$work/x86_64-linux.s" -o "$work/minic.o"
cc -no-pie -Wl,-z,noexecstack "$work/minic.o" -o "$work/minic"
"$work/gnu"
"$work/minic"
printf 'LOONG_IMMEDIATE_CORE_FOLD=PASS targets=5 immediates=4 out_of_range=1 volatile_load_chain=1 GNU_O2=1 GNU_AS_LD=1 runtime=2\n'
