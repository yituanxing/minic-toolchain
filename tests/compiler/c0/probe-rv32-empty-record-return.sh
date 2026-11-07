#!/bin/sh
# MC-057 RV32 only: GNU zero-byte Record has Core identity, no ABI payload.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
source_file="$root/tests/compiler/audit/mc057_rv32_empty_record_return_paths.c"
old_source="$root/tests/compiler/audit/mc057_gnu_empty_record_abi.c"
union_source="$root/tests/compiler/audit/mc057_rv32_empty_union_typedef.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { echo "MC057_RV32_EMPTY_RETURN=FAIL $*" >&2; exit 1; }

# The two historically rejected RV32 definition forms, plus two already
# admitted call forms, all remain GNU-valid and now pass target -S.
for kind in RETURN PARAM CALL_RETURN CALL_PARAM; do
    "$host" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        "-DTEST_$kind" "$old_source" || fail "GNU old=$kind"
    "$host" -std=gnu11 -E -P "-DTEST_$kind" "$old_source" \
        -o "$work/old.i" || fail "GNU preprocess old=$kind"
    "$mini" --target=riscv32-linux -S "$work/old.i" \
        -o "$work/old.s" || fail "RV32 old=$kind"
done

# A zero-sized union returned through a typedef is the same no-payload ABI
# class; Core still carries a valid record object on every return edge.
"$host" -std=gnu11 -O0 -Wall -Wextra -Werror -c "$union_source" \
    -o "$work/union.gnu.o" || fail 'GNU empty union/typedef'
"$host" -std=gnu11 -E -P "$union_source" \
    -o "$work/union.i" || fail 'GNU union preprocess'
for target in riscv32-linux aarch64-linux loongarch64-linux; do
    "$mini" --target="$target" -S "$work/union.i" \
        -o "$work/union-$target.s" || fail "union target=$target"
done

independent_pending=0
for variant in empty nonempty; do
    set --
    if [ "$variant" = nonempty ]; then set -- -DMC057_NONEMPTY_CONTROL; fi
    "$host" -std=gnu11 -O0 -Wall -Wextra -Werror -no-pie "$@" \
        "$source_file" -o "$work/reference" || fail "GNU native build=$variant"
    result=0
    "$work/reference" || result=$?
    [ "$result" -eq 42 ] || fail "GNU native=$variant exit=$result"
    "$host" -std=gnu11 -E -P "$@" "$source_file" \
        -o "$work/$variant.i" || fail "GNU preprocess=$variant"
    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        if [ "$variant" = empty ] && \
           { [ "$target" = x86_64-linux ] || [ "$target" = riscv64-linux ]; }; then
            # Other targets own their own MC-057 fixes: either a successful
            # compile or the existing specific rejection is acceptable here.
            # Never make the RV32 regression fail when those targets improve.
            if "$mini" --target="$target" -S "$work/$variant.i" \
                -o "$work/$variant-$target.s" \
                >"$work/$variant-$target.log" 2>&1; then
                :
            else
                grep -Eq 'Core terminator unsupported|cannot emit RISC-V function' \
                    "$work/$variant-$target.log" || \
                    fail "unrelated target failure=$target"
                independent_pending=$((independent_pending + 1))
            fi
            continue
        fi
        "$mini" --target="$target" -S "$work/$variant.i" \
            -o "$work/$variant-$target.s" || \
            fail "MiniC variant=$variant target=$target"
    done
done

# RV32 direct empty return must not manufacture a0/a1 payload. Real scalar
# parameters must still occupy a0, a1, a2 despite interleaved empty Records.
awk '
    /^mc057_direct:$/ { in_direct = 1; next }
    in_direct && /^\.size mc057_direct,/ { in_direct = 0; next }
    in_direct && /(^|[^[:alnum:]_])a[01]([^[:alnum:]_]|$)/ { bad_return++ }
    /^mc057_sum:$/ { in_sum = 1; next }
    in_sum && /^\.size mc057_sum,/ { in_sum = 0; next }
    in_sum && /mv t0, a0/ { a0++ }
    in_sum && /mv t0, a1/ { a1++ }
    in_sum && /mv t0, a2/ { a2++ }
    END { exit !(bad_return == 0 && a0 == 1 && a1 == 1 && a2 == 1) }
' "$work/empty-riscv32-linux.s" || fail 'RV32 register/return payload ABI'

# Nonempty sibling still runs using MiniC x86 assembly and GNU ELF/native.
"$host" -no-pie -x assembler "$work/nonempty-x86_64-linux.s" \
    -o "$work/nonempty-minic" || fail 'x86 GNU AS/ELF'
result=0
"$work/nonempty-minic" || result=$?
[ "$result" -eq 42 ] || fail "x86 MiniC native exit=$result"

rv32_elf=SKIP
rv32_qemu=SKIP
gnu_as=${RV32_GNU_AS:-riscv64-linux-gnu-as}
gnu_readelf=${RV32_GNU_READELF:-riscv64-linux-gnu-readelf}
gnu_cc=${RV32_GNU_CC:-riscv64-linux-gnu-gcc}
cross_qemu=${RV32_QEMU:-qemu-riscv32}
if command -v "$gnu_as" >/dev/null 2>&1 && \
   command -v "$gnu_readelf" >/dev/null 2>&1; then
    "$gnu_as" -march=rv32i -mabi=ilp32 "$work/empty-riscv32-linux.s" \
        -o "$work/empty-rv32.o" || fail 'GNU RV32 GAS'
    "$gnu_readelf" -h "$work/empty-rv32.o" | grep -q 'Class:.*ELF32' || \
        fail 'GNU RV32 ELF class'
    rv32_elf=PASS
fi
if [ "$rv32_elf" = PASS ] && command -v "$gnu_cc" >/dev/null 2>&1 && \
   command -v "$cross_qemu" >/dev/null 2>&1; then
    # GNU cross must first prove that this environment can link ILP32 ELF.
    # A cross compiler without ILP32 libc is not an executable QEMU oracle.
    if "$gnu_cc" -std=gnu11 -O0 -march=rv32i -mabi=ilp32 -no-pie \
            "$source_file" -o "$work/gnu-rv32" >"$work/link.log" 2>&1; then
        "$gnu_cc" -march=rv32i -mabi=ilp32 -no-pie \
            "$work/empty-rv32.o" -o "$work/mini-rv32" || \
            fail 'RV32 GNU link MiniC'
        for mode in gnu mini; do
            result=0
            "$cross_qemu" -L "${RV32_QEMU_SYSROOT:-/usr/riscv64-linux-gnu}" \
                "$work/$mode-rv32" || result=$?
            [ "$result" -eq 42 ] || fail "RV32 QEMU mode=$mode exit=$result"
        done
        rv32_qemu=PASS
    else
        echo 'MC057_RV32_QEMU=SKIP GNU cross lacks linkable ILP32 runtime' >&2
    fi
fi
echo "MC057_RV32_EMPTY_RETURN=PASS historical_RV32=4 GNU_native=2 RV32_empty_S=1 union_typedef_S=3 nonempty_five_ISA_S=5 A64_Loong_empty_S=2 x86_RV64_pending=$independent_pending x86_nonempty_native=42 RV32_GNU_ELF=$rv32_elf RV32_QEMU=$rv32_qemu"
