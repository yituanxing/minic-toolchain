#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/core-rhs-no-cfg-snapshot
mkdir -p "$work"
source="$root/tests/compiler/c0/core_rhs_no_cfg_snapshot.c"
cc -E -P -std=gnu11 "$source" -o "$work/source.i"
cc -O2 -Wall -Wextra -Werror -std=gnu11 "$source" -o "$work/gnu"
for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$MINIC_CC" --target="$target" -S "$work/source.i" -o "$work/$target.s"
done
cc -c -x assembler "$work/x86_64-linux.s" -o "$work/minic.o"
cc -no-pie -Wl,-z,noexecstack "$work/minic.o" -o "$work/minic"
"$work/gnu"
"$work/minic"
printf 'CORE_RHS_NO_CFG_SNAPSHOT=PASS targets=5 GNU_O2=1 MiniC_GNU_AS_LD=1 native_runtime=2 volatile_read=1 nested_rhs=1 conditional_rhs=1 shortcircuit_rhs=1 statement_rhs=1 call_rhs=1\n'
