#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_hyp_spinlock_lse.c" \
    -o "$work/hyp.i"
"$minic" --target=aarch64-linux -S "$work/hyp.i" -o "$work/hyp.s"
test "$(grep -c '^  \.hword 33$' "$work/hyp.s")" -eq 2
test "$(grep -c '^  \.word 661b - \.$' "$work/hyp.s")" -eq 2
test "$(grep -c '^  \.word 663f - \.$' "$work/hyp.s")" -eq 2
test "$(grep -c '^  \.org \.' "$work/hyp.s")" -eq 4
for opcode in 'ldaxr w9' 'stxr w11' 'ldadda w11' 'ldaxrh w11' \
    'stlrh w9' 'staddlh w9'; do
    grep -Fq "$opcode" "$work/hyp.s"
done
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/hyp.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch hyp spinlock on $target" >&2
        exit 1
    fi
done
sed 's/ldadda/ldadd/' "$work/hyp.i" > "$work/wrong-opcode.i"
sed 's/"+Q"(\*lock)/"+m"(*lock)/' "$work/hyp.i" > "$work/wrong-constraint.i"
sed 's/".hword 33\\n"/".hword 34\\n"/' "$work/hyp.i" > "$work/wrong-feature.i"
sed 's/"=&r"(newval)/"=r"(newval)/' "$work/hyp.i" > "$work/wrong-earlyclobber.i"
for category in opcode constraint feature earlyclobber; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted modified hyp spinlock $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS Linux nvhe hyp ticket-spinlock GNU LSE/LLSC lock+unlock, 5ISA, four fail-closed'
