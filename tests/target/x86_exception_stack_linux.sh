#!/usr/bin/env bash
# The Linux 6.6 exception/IRQ/softirq stack macro family: exact source GNU
# asm, qualified Core/MachineIR, GNU assembler ELF64, and objtool reachability.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
compiler=${1:-"$repo_root/build/bin/minic-cc"}
fixture="$repo_root/tests/target/x86_exception_stack_linux.c"
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

gcc -c -fno-PIE -x c "$fixture" -o "$tmpdir/gnu.o"
gcc -E -P -x c "$fixture" -o "$tmpdir/linux.i"
"$compiler" -S "$tmpdir/linux.i" --target=x86_64-linux -fno-pie \
    -o "$tmpdir/minic.s"
gcc -c -x assembler "$tmpdir/minic.s" -o "$tmpdir/minic.o"

readelf -h "$tmpdir/minic.o" | grep -Eq 'Type:[[:space:]]*REL \(Relocatable file\)'
readelf -h "$tmpdir/minic.o" | grep -Eq 'Machine:[[:space:]]*Advanced Micro Devices X86-64'
readelf -Wr "$tmpdir/minic.o" | grep -Eq '\.rela\.discard\.reachable'

relocations=$(readelf -Wr "$tmpdir/minic.o")
reachable_count=$(printf '%s\n' "$relocations" | awk '
    /^Relocation section / { reachable = ($0 ~ /\.rela\.discard\.reachable/) }
    reachable && /R_X86_64_32[[:space:]]/ { ++count }
    END { print count + 0 }
')
if [[ "$reachable_count" != 4 ]]; then
    printf 'expected four objtool reachability entries; got %s\n' "$reachable_count" >&2
    exit 1
fi
for symbol in exception_target irq_target1 irq_target2 softirq_target \
    irq_enter_rcu irq_exit_rcu; do
    if ! printf '%s\n' "$relocations" | grep -Eq "R_X86_64_PLT32[[:space:]]+[^[:space:]]*[[:space:]]+$symbol"; then
        printf 'missing direct PLT32 call relocation for %s\n' "$symbol" >&2
        exit 1
    fi
done

printf 'MINIC_X86_EXCEPTION_STACK_LINUX=PASS GNU_AS=1 ELF_REL=1 reachability=%s variants=4\n' "$reachable_count"
