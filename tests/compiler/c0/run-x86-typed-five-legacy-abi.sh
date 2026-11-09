#!/bin/sh
# C39/R3 -> R4 source-to-machine retirement: same GNU .i, exact old/new .s,
# native GNU ELF, plus five fail-closed source-template mutations.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
baseline=${MINIC_X86_TYPED_BASELINE_CC:-}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/x86-typed-five-legacy
mkdir -p "$work"
test -x "$minic" || { echo "X86_TYPED_FIVE=BLOCKED missing_minic=$minic" >&2; exit 2; }
if test -n "$baseline"; then
    test -x "$baseline" || { echo "X86_TYPED_FIVE=BLOCKED missing_baseline=$baseline" >&2; exit 2; }
fi
source_file="$root/tests/compiler/c0/x86_typed_five_legacy_abi.c"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source_file"
"$host_cc" -std=gnu11 -E -P -x c "$source_file" -o "$work/positive.i"
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/mini.s"
if test -n "$baseline"; then
    "$baseline" --target=x86_64-linux -S "$work/positive.i" -o "$work/baseline.s"
    if ! cmp -s "$work/baseline.s" "$work/mini.s"; then
        echo 'X86_TYPED_FIVE_DIFF=FAIL changed .s' >&2
        diff -u "$work/baseline.s" "$work/mini.s" || true
        exit 1
    fi
    echo 'X86_TYPED_FIVE_DIFF=PASS same_gnu_i=1 byte_identical_s=1'
fi

"$host_cc" -std=gnu11 -O0 -no-pie -x c "$work/positive.i" -o "$work/gnu.elf"
"$host_cc" -no-pie -x assembler "$work/mini.s" -o "$work/mini.elf"
for producer in gnu mini; do
    rc=0
    timeout 20s "$work/$producer.elf" >"$work/$producer.out" \
        2>"$work/$producer.err" || rc=$?
    if test "$rc" -ne 42; then
        echo "X86_TYPED_FIVE=FAIL runtime=$producer exit=$rc (expected 42)" >&2
        exit 1
    fi
done
cmp "$work/gnu.out" "$work/mini.out"
cmp "$work/gnu.err" "$work/mini.err"

for family in ip_here zero_barrier fxsave lar_selector identity_load; do
    case "$family" in
        ip_here)
            sed 's/lea 0(%%rip), %0/lea 0(%%rip), %0; int3/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        zero_barrier)
            sed 's/"mfence"/"mfence; int3"/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        fxsave)
            sed 's/fxsaveq %\[fx\]/fxsaveq %[fx]; int3/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        lar_selector)
            sed 's/lar %\[old_ss\]/lsl %[old_ss]/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        identity_load)
            sed 's/mov %\[address\], %%r9/mov %[address], %%r8/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
    esac
    if cmp -s "$work/invalid.i" "$work/positive.i"; then
        echo "X86_TYPED_FIVE=FAIL mutation_missed=$family" >&2
        exit 1
    fi
    rm -f "$work/invalid.s"
    if "$minic" --target=x86_64-linux -S "$work/invalid.i" \
        -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
        echo "X86_TYPED_FIVE=FAIL admitted=$family" >&2
        exit 1
    fi
    test ! -e "$work/invalid.s"
    if test -n "$baseline"; then
        rm -f "$work/baseline-invalid.s"
        if "$baseline" --target=x86_64-linux -S "$work/invalid.i" \
            -o "$work/baseline-invalid.s" \
            >"$work/baseline-invalid.out" 2>"$work/baseline-invalid.err"; then
            echo "X86_TYPED_FIVE_DIFF=FAIL baseline_admitted=$family" >&2
            exit 1
        fi
        test ! -e "$work/baseline-invalid.s"
    fi
done
echo 'X86_TYPED_FIVE=PASS families=5 GNU_AS_LD=1 GNU_MiniC_native=2 exit=42 invalid=5 partial_s=0'
