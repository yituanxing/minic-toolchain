#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIC_CC:?Set MINIC_CC to the built compiler}"
host_cc=${HOST_CC:-cc}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

for count in 255 256 257 512; do
    "$host_cc" -std=gnu11 -DMINIC_ASM_CASE="$count" -E -P \
        "$root/tests/compiler/c0/direct_lp64_asm_capacity.c" -o "$work/asm-$count.i"
    "$host_cc" -std=gnu11 -fsyntax-only -x c "$work/asm-$count.i"
done

"$host_cc" -std=gnu11 -E -P \
    "$root/tests/compiler/c0/direct_lp64_asm_unqualified.c" \
    -o "$work/asm-invalid.i"

for arch in aarch64 loongarch64; do
    for count in 255 256 257 512; do
        asm="$work/$arch-$count.s"
        "$MINIC_CC" --target="$arch-linux" -S "$work/asm-$count.i" -o "$asm"
        actual=$(grep -Ec '^[[:space:]]*nop([[:space:]]|$)' "$asm" || true)
        if [[ "$actual" -ne "$count" ]]; then
            echo "DIRECT_LP64_ASM_CAPACITY=FAIL arch=$arch count=$count emitted=$actual" >&2
            exit 1
        fi
        as_tool="$arch-linux-gnu-as"
        if command -v "$as_tool" >/dev/null 2>&1; then
            "$as_tool" "$asm" -o "$work/$arch-$count.o"
            echo "DIRECT_LP64_ASM_ELF=PASS arch=$arch count=$count"
        elif [[ "$count" == 512 ]]; then
            echo "DIRECT_LP64_ASM_ELF=NOT_TESTED arch=$arch missing_tool=$as_tool"
        fi
    done
    if "$MINIC_CC" --target="$arch-linux" -S "$work/asm-invalid.i" \
        -o "$work/$arch-invalid.s" >"$work/$arch-invalid.out" 2>&1; then
        echo "DIRECT_LP64_ASM_CAPACITY=FAIL arch=$arch unqualified_asm_accepted" >&2
        exit 1
    fi
    if [[ -s "$work/$arch-invalid.s" ]]; then
        echo "DIRECT_LP64_ASM_CAPACITY=FAIL arch=$arch partial_output" >&2
        exit 1
    fi
    echo "DIRECT_LP64_ASM_CAPACITY=PASS arch=$arch counts=255,256,257,512 unqualified_rejected=1"
done
