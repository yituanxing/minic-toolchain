#!/usr/bin/env bash
# Linux lib/iomap.c exact REP INS/OUTS admission, physical side effects and
# GNU AS.  I/O instructions are privileged and must never run in user space.
set -Eeuo pipefail
minic=${1:-build/debug/bin/minic-cc}
test -x "$minic"
root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
source_file="$root/tests/target/x86_64_linux_rep_string_io.c"

cc -E -P -x c "$source_file" -o "$tmp/real.i"
"$minic" --target=x86_64-linux -fshort-wchar -funsigned-char -fno-PIE \
    -S "$tmp/real.i" -o "$tmp/minic.s"
as --64 "$tmp/minic.s" -o "$tmp/minic.o"
objdump -d "$tmp/minic.o" > "$tmp/minic.dump"
cc -O2 -fshort-wchar -funsigned-char -fno-PIE -c "$source_file" \
    -o "$tmp/gnu.o"
objdump -d "$tmp/gnu.o" > "$tmp/gnu.dump"
for instruction in 'rep outsb' 'rep outsw' 'rep outsl' \
                   'rep insb' 'rep insw' 'rep insl'; do
    grep -Fq "$instruction" "$tmp/minic.dump"
    grep -Fq "$instruction" "$tmp/gnu.dump"
done
# Both GNU read/write outputs must be materialized in hardware registers,
# then written back to the original C lvalues, never dropped as dead asm.
test "$(grep -Fc 'movq %rsi, (%r10)' "$tmp/minic.s")" -eq 3
test "$(grep -Fc 'movq %rdi, (%r10)' "$tmp/minic.s")" -eq 3
test "$(grep -Fc 'movq %rcx, (%r10)' "$tmp/minic.s")" -eq 6
for scenario in TEMPLATE CONSTRAINT PORT MEMORY; do
    cc -E -P -x c -DMINIC_NEGATIVE_"$scenario" "$source_file" \
        -o "$tmp/bad-$scenario.i"
    if "$minic" --target=x86_64-linux -S "$tmp/bad-$scenario.i" \
            -o "$tmp/bad-$scenario.s" >/dev/null 2>&1; then
        echo "FAIL unsafe Linux REP port I/O: $scenario" >&2
        exit 1
    fi
done
echo 'MINIC_X86_LINUX_REP_STRING_IO=PASS variants=6 GNU_AS=1 GNU_oracle=6 outputs=SI/DI+CX strict_negatives=4 privileged_execution=none'
