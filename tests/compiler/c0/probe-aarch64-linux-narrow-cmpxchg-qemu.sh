#!/usr/bin/env bash
# Original Linux arm64 LL/SC 8/16/32/64-bit compare-exchange. Exactly one
# GNU-preprocessed fixture for MiniC and GNU C/GAS/QEMU, including narrow
# truncation, W/X register distinction, memory orders and mismatch handling.
set -Eeuo pipefail
minic=${1:?expected minic-cc}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source="$root/tests/compiler/c0/aarch64-linux-narrow-cmpxchg.c"

# Source-only native CI executes all safety and codegen contracts without
# requiring the foreign assembler/QEMU.  The runtime matrix executes both.
cc -E -P -x c "$source" -o "$work/narrow.i"
"$minic" --target=aarch64-linux -S "$work/narrow.i" -o "$work/narrow.minic.s"
for opcode in \
    'ldxrb w9' 'ldaxrb w9' 'stxrb w13' 'stlxrb w13' \
    'ldxrh w9' 'ldaxrh w9' 'stxrh w13' 'stlxrh w13' \
    'ldxr w9' 'ldaxr w9' 'stxr w13' 'stlxr w13' \
    'ldxr x9' 'ldaxr x9' 'stxr w13, x12' 'stlxr w13, x12' \
    'strb w9' 'strh w9' 'str w9' 'str x9' \
    'eor w13, w9, w11' 'eor x13, x9, x11' 'dmb ish'; do
    if ! grep -Fq "$opcode" "$work/narrow.minic.s"; then
        echo "AArch64 subword GNU source missing hardware opcode: $opcode" >&2
        exit 1
    fi
done
case "$(grep -Fc '  prfm pstl1strm, [x10]' "$work/narrow.minic.s")" in
    16) ;;
    *) echo 'AArch64 LL/SC must emit exactly 16 ISA-contract instances' >&2; exit 1 ;;
esac

# The original hardware contract must reject changes to the size, opcode,
# constraints, memory effect and exclusive retry control-flow.  Rejections
# must leave no partial .s file.  All eight functions are live in main().
for variant in wrong_load wrong_compare wrong_store wrong_constraint \
               wrong_memory wrong_clobber injected_svc \
               wrong_x64_load wrong_x64_constraint wrong_x64_store \
               wrong_status_width; do
    case "$variant" in
        wrong_load)
            sed 's/ldaxrb/ldaxrh/g' "$work/narrow.i" >"$work/$variant.i" ;;
        wrong_compare)
            sed 's/eor %w\[tmp\]/sub %w[tmp]/g' "$work/narrow.i" >"$work/$variant.i" ;;
        wrong_store)
            sed 's/stlxrh/stlxrb/g' "$work/narrow.i" >"$work/$variant.i" ;;
        wrong_constraint)
            sed 's/"Kr" (old)/"r" (old)/g' "$work/narrow.i" >"$work/$variant.i" ;;
        wrong_memory)
            sed 's/"+Q" (\*/"=Q" (*/g' "$work/narrow.i" >"$work/$variant.i" ;;
        wrong_clobber)
            sed 's/: "memory"/: "cc"/g' "$work/narrow.i" >"$work/$variant.i" ;;
        injected_svc)
            sed 's/1b\\n/1b; svc #0\\n/g' "$work/narrow.i" >"$work/$variant.i" ;;
        wrong_x64_load)
            sed 's/"ldaxr" " " "%"/"ldaxrh" " " "%"/g' "$work/narrow.i" >"$work/$variant.i" ;;
        wrong_x64_constraint)
            sed 's/"Lr" (old)/"Kr" (old)/g' "$work/narrow.i" >"$work/$variant.i" ;;
        wrong_x64_store)
            sed 's/"stlxr" " %w\[tmp\], " "%"/"stlxr" " %w[tmp], " "%w"/g' "$work/narrow.i" >"$work/$variant.i" ;;
        wrong_status_width)
            sed 's/"stlxrh" " %w\[tmp\]/"stlxrh" " %[tmp]/g' "$work/narrow.i" >"$work/$variant.i" ;;
    esac
    if cmp -s "$work/narrow.i" "$work/$variant.i"; then
        echo "negative fixture did not change the source: $variant" >&2
        exit 1
    fi
    if "$minic" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >"$work/$variant.stdout" \
        2>"$work/$variant.stderr"; then
        echo "AArch64 unsafe subword machine template accepted: $variant" >&2
        exit 1
    fi
    if test -s "$work/$variant.s"; then
        echo "AArch64 emitted partial source after rejecting: $variant" >&2
        exit 1
    fi
done

if [[ "${MINIC_SOURCE_ONLY:-0}" == 1 ]]; then
    echo 'AARCH64_LINUX_CMPXCHG=PASS source=16 orders=4 widths=4 negative=11 mode=source'
    exit 0
fi

cross_cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
"$cross_cc" -O0 -x c "$source" -o "$work/narrow.gnu.elf"
"$cross_cc" -x assembler "$work/narrow.minic.s" \
    -o "$work/narrow.minic.elf"
for flavor in gnu minic; do
    set +e
    qemu-aarch64 -L /usr/aarch64-linux-gnu "$work/narrow.$flavor.elf"
    rc=$?
    set -e
    if [[ $rc != 42 ]]; then
        echo "AArch64 $flavor returned $rc, expected 42" >&2
        exit 1
    fi
done
echo 'AARCH64_LINUX_CMPXCHG_GNU_QEMU=PASS original=1 widths=4 orders=4 negatives=11 exit=42'
