#!/usr/bin/env bash
# P41: C integer width/signedness must survive specialized GNU `i` evaluation.
# This checks emitted assembler text only. Other ISA raw GNU-asm admission is
# reported explicitly; cross-target Linux runtime is not claimed here.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$repo_root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
source="$repo_root/tests/compiler/c0/p41_asm_specialized_integer_width.c"
pointer_source="$repo_root/tests/compiler/c0/p41_asm_specialized_pointer_index_width.c"
test -x "$minic_cc"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/source.i"
"$host_cc" -O2 -std=gnu11 -S "$source" -o "$work/gnu.s"
"$host_cc" -E -P -std=gnu11 -x c "$pointer_source" -o "$work/pointer.i"
"$host_cc" -O2 -std=gnu11 -S "$pointer_source" -o "$work/pointer.gnu.s"

immediates() {
    # GNU and MiniC may spell the same .long bit pattern as signed or
    # unsigned decimal (for example -2 and 4294967294). Compare emitted
    # 32-bit assembler values, not superficial signed spelling.
    awk '$1 == ".long" {
        value=$2; sub(/^\$/, "", value);
        bits=(value+0) % 4294967296;
        if (bits < 0) bits += 4294967296;
        printf "%.0f\n", bits;
    }' "$1" \
        | LC_ALL=C sort
}

immediates "$work/gnu.s" >"$work/gnu.values"
if [[ $(wc -l <"$work/gnu.values") -ne 12 ]]; then
    echo 'P41_GNU_TYPED_ASM_REFERENCE_CHANGED' >&2
    exit 1
fi
for target in riscv32-linux riscv64-linux; do
    if ! "$minic_cc" --target="$target" -S "$work/source.i" \
        -o "$work/$target.s" >"$work/$target.out" 2>"$work/$target.err"; then
        echo "P41_TYPED_ASM_COMPILE_FAILED target=$target" >&2
        tail -n 18 "$work/$target.err" >&2
        exit 1
    fi
    immediates "$work/$target.s" >"$work/$target.values"
    if ! cmp -s "$work/gnu.values" "$work/$target.values"; then
        echo "P41_TYPED_ASM_MISMATCH target=$target" >&2
        diff -u "$work/gnu.values" "$work/$target.values" >&2 || true
        exit 1
    fi
    if ! "$minic_cc" --target="$target" -S "$work/pointer.i" \
        -o "$work/pointer.$target.s" >"$work/pointer.$target.out" \
        2>"$work/pointer.$target.err"; then
        echo "P41_TYPED_POINTER_COMPILE_FAILED target=$target" >&2
        tail -n 18 "$work/pointer.$target.err" >&2
        exit 1
    fi
    awk '$1 == ".quad" && $2 ~ /^p41_address_slots/ { print $2 }' \
        "$work/pointer.gnu.s" >"$work/pointer.gnu.quad"
    awk '$1 == ".quad" && $2 ~ /^p41_address_slots/ { print $2 }' \
        "$work/pointer.$target.s" >"$work/pointer.$target.quad"
    if [[ $(wc -l <"$work/pointer.gnu.quad") -ne 1 ]] ||
       ! cmp -s "$work/pointer.gnu.quad" "$work/pointer.$target.quad" ||
       ! grep -qx 'p41_address_slots+12' "$work/pointer.$target.quad"; then
        echo "P41_TYPED_POINTER_ADDEND_MISMATCH target=$target" >&2
        diff -u "$work/pointer.gnu.quad" "$work/pointer.$target.quad" >&2 || true
        exit 1
    fi
done

additional_isa_admitted=0
additional_isa_unsupported=0
for target in x86_64-linux aarch64-linux loongarch64-linux; do
    if "$minic_cc" --target="$target" -S "$work/source.i" \
        -o "$work/$target.s" >"$work/$target.out" 2>"$work/$target.err"; then
        immediates "$work/$target.s" >"$work/$target.values"
        if ! cmp -s "$work/gnu.values" "$work/$target.values"; then
            echo "P41_TYPED_ASM_MISMATCH target=$target" >&2
            diff -u "$work/gnu.values" "$work/$target.values" >&2 || true
            exit 1
        fi
        ((additional_isa_admitted += 1))
    elif grep -Eiq 'unsupported kind|target.*(unsupported|unclaimed)|Core instruction unsupported|Core instruction/ABI not yet supported by M1 emitter' \
        "$work/$target.err"; then
        printf 'P41_TYPED_ASM_ADMISSION=SKIP target=%s reason=unsupported-structured-immediate\n' "$target"
        ((additional_isa_unsupported += 1))
    else
        echo "P41_UNEXPECTED_TARGET_FAILURE target=$target" >&2
        tail -n 18 "$work/$target.err" >&2
        exit 1
    fi
done

printf 'P41_TYPED_ASM=PASS integer_cases=12 pointer_cases=1 riscv_targets=2 additional_isa_admitted=%d additional_isa_unsupported=%d\n' \
    "$additional_isa_admitted" "$additional_isa_unsupported"
