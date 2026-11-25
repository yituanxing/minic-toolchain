#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-x86-p2-kvm-asm-gnu
mkdir -p "$work"

source_file="$root/tests/compiler/c0/x86_linux_p2_kvm_asm_followups.c"
driver_file="$root/tests/compiler/c0/x86_linux_p2_kvm_asm_followups_driver.c"
"$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -c "$source_file" -o "$work/oracle.o"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -c "$driver_file" -o "$work/driver.o"
"$cc" "$work/oracle.o" "$work/driver.o" -o "$work/gnu-control"
"$work/gnu-control"

objdump -dr "$work/oracle.o" > "$work/oracle.dump"
grep -Eq '[[:space:]]+div[[:space:]]+%' "$work/oracle.dump"
grep -Eq '[[:space:]]+xchg[[:space:]]' "$work/oracle.dump"
grep -Eq '[[:space:]]+mov[[:space:]]' "$work/oracle.dump"
objdump -h "$work/oracle.o" | grep -Fq '__ex_table'
readelf -r "$work/oracle.o" | grep -Fq '.rela__ex_table'

printf '%s\n' 'MINIC_X86_P2_KVM_ASM_GNU_ORACLE=PASS divl_edx_eax=1 xchgb_atomic=1 asm_goto_fixups=1 extable_reloc=1 no_minic_pass_claim=1'
