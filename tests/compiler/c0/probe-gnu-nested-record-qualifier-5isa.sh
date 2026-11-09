#!/usr/bin/env bash
# A record-rvalue member loses only its outer value qualifier. The pointee's
# const survives, and volatile nested scalar members remain memory accesses.
set -Eeuo pipefail
cc_bin=$1
test -x "$cc_bin"
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
positive="$repo/tests/compiler/c0/gnu_nested_record_member_qualifier_abi.c"
negative="$repo/tests/compiler/c0/invalid_gnu_nested_record_member_pointee_const.c"
negative_volatile="$repo/tests/compiler/c0/invalid_gnu_nested_record_volatile_rvalue_address.c"
gcc -E -P -std=gnu11 "$positive" -o "$d/positive.i"
gcc -E -P -std=gnu11 "$negative" -o "$d/negative.i"
gcc -E -P -std=gnu11 "$negative_volatile" -o "$d/negative-volatile.i"
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
 "$cc_bin" --target="$target" -S "$d/positive.i" -o "$d/$target.s"
 test -s "$d/$target.s"
 if "$cc_bin" --target="$target" -S "$d/negative.i" -o "$d/invalid-$target.s" >/dev/null 2>&1; then
  echo "FAIL $target wrote through nested const pointee" >&2
  exit 1
 fi
 test ! -e "$d/invalid-$target.s"
 if "$cc_bin" --target="$target" -S "$d/negative-volatile.i" -o "$d/invalid-volatile-$target.s" >/dev/null 2>&1; then
  echo "FAIL $target took address of nested volatile record rvalue" >&2
  exit 1
 fi
 test ! -e "$d/invalid-volatile-$target.s"
done
if gcc -std=gnu11 -fsyntax-only "$negative" >/dev/null 2>&1; then
 echo 'FAIL GNU accepted nested const pointee write' >&2
 exit 1
fi
if gcc -std=gnu11 -fsyntax-only "$negative_volatile" >/dev/null 2>&1; then
 echo 'FAIL GNU accepted nested volatile record rvalue address' >&2
 exit 1
fi
gcc -O0 -fno-pie -no-pie -x c "$d/positive.i" -o "$d/gnu-native"
gcc -O0 -fno-pie -no-pie -S -x c "$d/positive.i" -o "$d/gnu.s"
# Runtime equality alone cannot prove that volatile member loads survived:
# both lowering paths must retain at least two explicit scalar memory reads
# in the dedicated `read_nested_volatile_twice` function.
for compiler in gnu x86_64-linux; do
 awk '
  /^read_nested_volatile_twice:/ { in_fn=1; next }
  in_fn && /^[.]size[[:space:]]+read_nested_volatile_twice/ { in_fn=0 }
  in_fn && /movl[[:space:]]+[^#]*\([^)]*\),[[:space:]]*%e[a-z][a-z]/ { reads++ }
  END { exit (reads >= 2 ? 0 : 1) }
 ' "$d/$compiler.s"
done
gcc -fno-pie -no-pie -x assembler "$d/x86_64-linux.s" -o "$d/minic-native"
"$d/gnu-native"
"$d/minic-native"
echo 'MINIC_GNU_NESTED_RECORD_QUALIFIERS=PASS ISA=5 GNU_NATIVE=1 nested_pointer_pointee_const=1 volatile_member_reads=2 negatives=2x5'
