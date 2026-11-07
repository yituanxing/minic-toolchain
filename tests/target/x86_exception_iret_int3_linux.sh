#!/usr/bin/env bash
# Upstream Linux 6.6.143 sync_core IRET and alternative.c INT3 selftest:
# verify physical ASM, GNU ELF64 ET_REL, and objtool .discard.noendbr.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
compiler=${1:-"$repo_root/build/bin/minic-cc"}
fixture="$repo_root/tests/target/x86_exception_iret_int3_linux.c"
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

gcc -c -fno-PIE -x c "$fixture" -o "$tmpdir/gcc.o"
gcc -E -P -x c "$fixture" -o "$tmpdir/linux.i"
"$compiler" -S "$tmpdir/linux.i" --target=x86_64-linux -fno-pie \
    -o "$tmpdir/minic.s"
gcc -c -x assembler "$tmpdir/minic.s" -o "$tmpdir/minic.o"

readelf -h "$tmpdir/minic.o" | grep -Eq 'Type:[[:space:]]*REL \(Relocatable file\)'
readelf -h "$tmpdir/minic.o" | grep -Eq 'Machine:[[:space:]]*Advanced Micro Devices X86-64'
grep -Eq '^  movw %ss, %r11w$' "$tmpdir/minic.s"
grep -Eq '^  movw %cs, %r11w$' "$tmpdir/minic.s"
grep -Eq '^  iretq$' "$tmpdir/minic.s"
grep -Eq '^  movl %r11d, \(%r10\)$' "$tmpdir/minic.s"
grep -Eq '^int3_selftest_ip:$' "$tmpdir/minic.s"
grep -Eq '^986:$' "$tmpdir/minic.s"
grep -Eq '^  int3$' "$tmpdir/minic.s"

relocations=$(readelf -Wr "$tmpdir/minic.o")
printf '%s\n' "$relocations" | grep -Eq '\.rela\.discard\.noendbr'
noendbr_count=$(printf '%s\n' "$relocations" | awk '
    /^Relocation section / { noendbr = ($0 ~ /\.rela\.discard\.noendbr/) }
    noendbr && /R_X86_64_32[[:space:]]/ { ++count }
    END { print count + 0 }
')
if [[ "$noendbr_count" != 1 ]]; then
    printf 'expected one objtool noendbr relocation; got %s\n' "$noendbr_count" >&2
    exit 1
fi

printf 'MINIC_X86_IRET_INT3_LINUX=PASS GNU_AS=1 ELF_REL=1 noendbr=%s forms=2\n' "$noendbr_count"
