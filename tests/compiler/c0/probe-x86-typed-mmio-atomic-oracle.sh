#!/usr/bin/env bash
set -Eeuo pipefail
minic="${1:?pass built minic-cc}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
temp="$(mktemp -d)"
trap 'rm -rf "$temp"' EXIT

"${CC:-cc}" -E -P "$root/tests/compiler/c0/x86_typed_mmio_atomic_oracle.c" \
    -o "$temp/mini.i"
"${CC:-cc}" -E -P -DGNU_ORACLE \
    "$root/tests/compiler/c0/x86_typed_mmio_atomic_oracle.c" \
    -o "$temp/gnu.i"
"$minic" --target=x86_64-linux -S "$temp/mini.i" -o "$temp/mini.s"
"${CC:-cc}" -O0 -c "$temp/gnu.i" -o "$temp/gnu.o"
"${CC:-cc}" -O0 -no-pie \
    "$root/tests/compiler/c0/x86_typed_mmio_atomic_driver.c" \
    "$temp/gnu.o" -x assembler "$temp/mini.s" -o "$temp/oracle"

set +e
"$temp/oracle"
result=$?
set -e
if [[ "$result" -ne 42 ]]; then
    echo "FAIL typed MMIO read/write and xchg GNU native differential: $result"
    exit 1
fi
# Privileged and feature-dependent forms have no safe generic native runtime.
# Keep their original Linux GNU operand contracts, require machine-assembly
# success and inspect source-only failures through the separate negative gate.
for fixture in x86_64_linux_pkru x86_64_linux_rng_flag x86_64_linux_fdiv_bug; do
    "${CC:-cc}" -E -P "$root/tests/target/$fixture.c" \
        -o "$temp/$fixture.i"
    "$minic" --target=x86_64-linux -S "$temp/$fixture.i" \
        -o "$temp/$fixture.s"
    "${CC:-cc}" -c -x assembler "$temp/$fixture.s" \
        -o "$temp/$fixture.o"
done
echo 'X86_TYPED_MMIO_ATOMIC=PASS families=6 native_GCC_oracle=42 privileged_compile=3'
