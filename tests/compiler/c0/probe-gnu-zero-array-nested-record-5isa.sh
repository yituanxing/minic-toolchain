#!/usr/bin/env bash
# Linux Renesas CPG uses `common[N]` followed by explicitly empty `drp[0]`.
# The GNU [0] extension consumes no initializer slots, storage or ELF relocs.
set -Eeuo pipefail
cc_bin=$1
test -x "$cc_bin"
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
positive="$repo/tests/compiler/c0/gnu_zero_array_nested_record_global.c"
negative="$repo/tests/compiler/c0/invalid_gnu_zero_array_nonempty_record_global.c"
gcc -E -P -std=gnu11 "$positive" -o "$d/positive.i"
gcc -E -P -std=gnu11 "$negative" -o "$d/negative.i"
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
 "$cc_bin" --target="$target" -S "$d/positive.i" -o "$d/$target.s"
 test -s "$d/$target.s"
 if "$cc_bin" --target="$target" -S "$d/negative.i" -o "$d/negative-$target.s" >/dev/null 2>&1; then
  echo "FAIL $target accepted excess elements of zero-length record array" >&2
  exit 1
 fi
 test ! -e "$d/negative-$target.s"
done
if gcc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$negative" >/dev/null 2>&1; then
 echo 'FAIL GNU did not warn about excess [0] record initializer' >&2
 exit 1
fi
gcc -O0 -fno-pie -no-pie -x c "$d/positive.i" -o "$d/gnu-native"
gcc -O0 -fno-pie -fno-pic -S -x c "$d/positive.i" -o "$d/gnu.s"
gcc -c -x assembler "$d/gnu.s" -o "$d/gnu.o"
gcc -c -x assembler "$d/x86_64-linux.s" -o "$d/minic.o" 2>"$d/gas-warnings.log"
for compiler in gnu minic; do
 test "$(readelf -rW "$d/$compiler.o" | grep -c 'R_X86_64_64')" = 4
done
gcc -fno-pie -no-pie -x assembler "$d/x86_64-linux.s" -o "$d/minic-native"
"$d/gnu-native"
"$d/minic-native"
echo 'MINIC_GNU_ZERO_ARRAY_NESTED_RECORD=PASS ISA=5 GNU_NATIVE=1 common=2 drp=0 reloc=4 negative_excess=5'
