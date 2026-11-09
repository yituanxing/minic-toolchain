#!/bin/sh
# GNU Linux 6.6 syscall wrapper exception must stay an exact LoongArch ABI
# family, not a blanket int/pointer ↔ 64-bit alias compatibility relaxation.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
: "${MINIC_CC:?set MINIC_CC to minic-cc executable}"
host_cc=${HOST_CC:-cc}
source="$root/tests/compiler/c0/loongarch64_syscall_alias_policy.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

case_number=0
while [ "$case_number" -le 12 ]; do
    "$host_cc" -E -P -std=gnu11 -DCASE="$case_number" -x c "$source" \
        -o "$work/case-$case_number.i"
    # GCC accepts these GNU aliases as source constructs even when their
    # prototypes differ; MiniC must distinguish a real ABI exception.
    "$host_cc" -Wno-attribute-alias -std=gnu11 -x c -c \
        "$work/case-$case_number.i" -o "$work/case-$case_number.gnu.o"
    if [ "$case_number" -eq 0 ] || [ "$case_number" -eq 1 ] ||
       [ "$case_number" -eq 12 ]; then
        "$MINIC_CC" --target=loongarch64-linux -S \
            "$work/case-$case_number.i" -o "$work/case-$case_number.s"
        test -s "$work/case-$case_number.s"
    else
        if "$MINIC_CC" --target=loongarch64-linux -S \
            "$work/case-$case_number.i" -o "$work/case-$case_number.s" \
            >"$work/case-$case_number.out" \
            2>"$work/case-$case_number.err"; then
            echo "FAIL LoongArch unsafe syscall alias CASE=$case_number admitted" >&2
            exit 1
        fi
        grep -Fq 'GNU function alias requires' "$work/case-$case_number.err"
        test ! -e "$work/case-$case_number.s"
    fi
    case_number=$((case_number + 1))
done

# The ordinary C-compatible alias stays legal for every target, while the
# Loong-specific syscall sanitizer exception must not leak to the four other
# ABIs simply because an incompatible integer occupies one register there.
for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$MINIC_CC" --target="$target-linux" -S "$work/case-1.i" \
        -o "$work/ordinary-$target.s"
    test -s "$work/ordinary-$target.s"
    if [ "$target" != loongarch64 ]; then
        if "$MINIC_CC" --target="$target-linux" -S "$work/case-0.i" \
            -o "$work/syscall-$target.s" >"$work/syscall-$target.out" \
            2>"$work/syscall-$target.err"; then
            echo "FAIL incompatible syscall alias leaked into $target" >&2
            exit 1
        fi
        grep -Fq 'GNU function alias requires' "$work/syscall-$target.err"
        test ! -e "$work/syscall-$target.s"
    fi
done

echo 'MINIC_LOONGARCH64_SYSCALL_ALIAS_SOURCE_POLICY=PASS gcc_oracle=13 allowed=3 rejected=10 five_isa_compatible=5 other_isa_reject=4 symbol_family=restricted'
