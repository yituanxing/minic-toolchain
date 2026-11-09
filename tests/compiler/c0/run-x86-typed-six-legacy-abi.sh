#!/bin/sh
# R4 -> R5 six legacy ASM retirement. Same host GNU .i and exact old/new .s,
# including the 8-operand CPUID that intentionally remains legacy-owned.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
baseline=${MINIC_X86_TYPED_BASELINE_CC:-}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/x86-typed-six-legacy
mkdir -p "$work"
test -x "$minic" || { echo "X86_TYPED_SIX=BLOCKED missing_minic=$minic" >&2; exit 2; }
if test -n "$baseline"; then
    test -x "$baseline" || { echo "X86_TYPED_SIX=BLOCKED missing_baseline=$baseline" >&2; exit 2; }
fi
src="$root/tests/compiler/c0/x86_typed_six_legacy_abi.c"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$src"
"$host_cc" -E -P -x c "$src" -o "$work/positive.i"
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/mini.s"
if test -n "$baseline"; then
    "$baseline" --target=x86_64-linux -S "$work/positive.i" -o "$work/baseline.s"
    if ! cmp -s "$work/baseline.s" "$work/mini.s"; then
        echo 'X86_TYPED_SIX_DIFF=FAIL changed .s' >&2
        diff -u "$work/baseline.s" "$work/mini.s" || true
        exit 1
    fi
    echo 'X86_TYPED_SIX_DIFF=PASS same_gnu_i=1 byte_identical_s=1 legacy_cpuid8=preserved'
fi

# GNU/MiniC both run safe CPUID, BITS, MUL, CLFLUSH and MOVNTI. Privileged
# INVLPG/HLT and ISA-gated TPAUSE are assembled but never executed.
"$host_cc" -std=gnu11 -O0 -no-pie -x c "$work/positive.i" -o "$work/gnu.elf"
"$host_cc" -no-pie -x assembler "$work/mini.s" -o "$work/mini.elf"
for producer in gnu mini; do
    rc=0
    timeout 20s "$work/$producer.elf" >"$work/$producer.out" \
        2>"$work/$producer.err" || rc=$?
    if test "$rc" -ne 42; then
        echo "X86_TYPED_SIX=FAIL runtime=$producer exit=$rc" >&2
        exit 1
    fi
done
cmp "$work/gnu.out" "$work/mini.out"
cmp "$work/gnu.err" "$work/mini.err"
test "$(grep -c '^  cpuid$' "$work/mini.s")" -eq 2

for family in verw_clear tlb_cache delay_mul delay_wait lowlevel_scalar boot_flags; do
    case "$family" in
        verw_clear)
            sed 's/verw %\[ds\]/verw %[ds]; int3/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        tlb_cache)
            sed 's/clflush %0/clflush %0; int3/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        delay_mul)
            sed 's/mull %%edx/idivl %%edx/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        delay_wait)
            sed 's/tpause %%ecx/tpause %%eax/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        lowlevel_scalar)
            sed 's/movntiq %1, %0/movnti %1, %0/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        boot_flags)
            sed 's/btsl %1,%0/btcl %1,%0/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
    esac
    if cmp -s "$work/invalid.i" "$work/positive.i"; then
        echo "X86_TYPED_SIX=FAIL mutation_missed=$family" >&2
        exit 1
    fi
    rm -f "$work/invalid.s"
    if "$minic" --target=x86_64-linux -S "$work/invalid.i" \
        -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
        echo "X86_TYPED_SIX=FAIL admitted=$family" >&2
        exit 1
    fi
    test ! -e "$work/invalid.s"
    if test -n "$baseline"; then
        rm -f "$work/baseline-invalid.s"
        if "$baseline" --target=x86_64-linux -S "$work/invalid.i" \
            -o "$work/baseline-invalid.s" \
            >"$work/baseline-invalid.out" 2>"$work/baseline-invalid.err"; then
            echo "X86_TYPED_SIX_DIFF=FAIL baseline_admitted=$family" >&2
            exit 1
        fi
        test ! -e "$work/baseline-invalid.s"
    fi
done
echo 'X86_TYPED_SIX=PASS families=6 GNU_AS_LD=1 GNU_MiniC_native=2 exit=42 invalid=6 legacy_cpuid8=preserved partial_s=0'
