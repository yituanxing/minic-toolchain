#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_rcu_record_acquire_release.c" \
    -o "$work/rcu.i"
"$minic" --target=aarch64-linux -S "$work/rcu.i" -o "$work/rcu.s"
test "$(grep -c '^  ldarb w9, \[x10\]$' "$work/rcu.s")" -eq 1
test "$(grep -c '^  stlrb w9, \[x10\]$' "$work/rcu.s")" -eq 1
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/rcu.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch RCU LDARB/STLRB on $target" >&2
        exit 1
    fi
done
sed 's/"Q"(\*flags)/"m"(*flags)/' "$work/rcu.i" > "$work/wrong-constraint.i"
sed 's/unsigned char s;/unsigned long s;/' "$work/rcu.i" > "$work/wrong-width.i"
sed 's/ldarb %w0/ldarh %w0/' "$work/rcu.i" > "$work/wrong-opcode.i"
sed 's/"memory"/"cc"/' "$work/rcu.i" > "$work/wrong-clobber.i"
for category in constraint width opcode clobber; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted invalid AArch RCU $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS AArch GNU RCU Q union carrier exact byte LDARB/STLRB, 5ISA +4 negative'
