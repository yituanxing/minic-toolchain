#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-x86-p2-change-bit-xor
mkdir -p "$work"

source_file="$root/tests/compiler/c0/x86_linux_p2_change_bit_xor.c"
driver_file="$root/tests/compiler/c0/x86_linux_p2_change_bit_xor_driver.c"

"$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -pthread -c "$driver_file" -o "$work/driver.o"
"$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -c "$source_file" -o "$work/gnu.o"
"$cc" -pthread "$work/gnu.o" "$work/driver.o" -o "$work/gnu-control"
"$work/gnu-control"
objdump -d "$work/gnu.o" > "$work/gnu.dump"
grep -Eq 'lock[[:space:]]+xor(b)?[[:space:]]' "$work/gnu.dump"
grep -Eq 'lock[[:space:]]+btc([[:space:]]|q[[:space:]])' "$work/gnu.dump"
objdump -h "$work/gnu.o" | grep -Fq '.smp_locks'
readelf -r "$work/gnu.o" | grep -Fq '.rela.smp_locks'

"$cc" -E -P "$source_file" -o "$work/minic.i"
"$minic" --target=x86_64-linux -S "$work/minic.i" -o "$work/minic.s"
"$cc" -c "$work/minic.s" -o "$work/minic.o"
"$cc" -pthread "$work/minic.o" "$work/driver.o" -o "$work/minic-gnu-driver"
"$work/minic-gnu-driver"
objdump -d "$work/minic.o" > "$work/minic.dump"
grep -Eq 'lock[[:space:]]+xor(b)?[[:space:]]' "$work/minic.dump"
grep -Eq 'lock[[:space:]]+btc([[:space:]]|q[[:space:]])' "$work/minic.dump"
objdump -h "$work/minic.o" | grep -Fq '.smp_locks'
readelf -r "$work/minic.o" | grep -Fq '.rela.smp_locks'

printf '%s\n' 'MINIC_X86_P2_CHANGE_BIT_XOR_GNU_NATIVE=PASS constant_lock_xorb=1 variable_lock_btcq=1 smp_locks=1 concurrent_parity=1'
