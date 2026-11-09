#!/bin/sh
# MC-066: a floating pointee is not an AAPCS64 HFA value member.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
source_file="$root/tests/compiler/audit/mc066_aarch64_fp_pointer_aggregate.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { echo "MC066_A64_FP_POINTER_RECORD=FAIL $*" >&2; exit 1; }

targets='x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux'
variants='default MC066_INTEGER_PTR MC066_POINTER_ARRAY MC066_TYPEDEF_PTR MC066_POINTER_TO_ARRAY MC066_POINTER_TO_RECORD MC066_PACKED_POINTER'
compile_count=0
for bytes in 16 17 32 64; do
    for variant in $variants; do
        # A 16B array of two pointers has exact size 16 only on LP64;
        # on RV32 it is an 8B aggregate with no trailing padding.
        if [ "$variant" = MC066_PACKED_POINTER ] && [ "$bytes" = 16 ]; then
            continue
        fi
        set -- "-DMC066_BYTES=$bytes"
        if [ "$variant" != default ]; then set -- "$@" "-D$variant"; fi
        "$host" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$@" \
            "$source_file" || fail "GNU C size=$bytes variant=$variant"
        "$host" -std=gnu11 -E -P "$@" "$source_file" \
            -o "$work/source.i" || fail "GNU preprocess size=$bytes variant=$variant"
        for target in $targets; do
            if [ "$variant" = MC066_POINTER_ARRAY ] && [ "$bytes" = 16 ] && \
               [ "$target" = riscv32-linux ]; then
                continue
            fi
            "$mini" --target="$target" -S "$work/source.i" \
                -o "$work/$target.s" || \
                fail "MiniC -S target=$target size=$bytes variant=$variant"
            compile_count=$((compile_count + 1))
        done
    done
done

# True FP HFAs and not-yet-supported small packed unaligned pointer records
# must fail at the A64 ABI admission stage, not via an unrelated parse error.
negative_count=0
for negative in MC066_PACKED_POINTER MC066_FLOAT_VALUES MC066_PACKED_FLOAT_VALUES; do
    case $negative in
        MC066_PACKED_POINTER) bytes=16 ;;
        *) bytes=32 ;;
    esac
    "$host" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        "-DMC066_BYTES=$bytes" "-D$negative" "$source_file" || \
        fail "GNU negative=$negative"
    "$host" -std=gnu11 -E -P "-DMC066_BYTES=$bytes" "-D$negative" \
        "$source_file" -o "$work/negative.i" || \
        fail "GNU preprocess negative=$negative"
    if "$mini" --target=aarch64-linux -S "$work/negative.i" \
        -o "$work/negative.s" >"$work/negative.log" 2>&1; then
        fail "A64 ABI unexpectedly admitted negative=$negative"
    fi
    grep -q 'AArch64: Core instruction/ABI not yet supported' \
        "$work/negative.log" || fail "unrelated failure negative=$negative"
    negative_count=$((negative_count + 1))
done

# Source-level GNU ELF native reference: separate caller and callee objects.
for bytes in 16 17 32 64; do
    "$host" -std=gnu11 -O0 -Wall -Wextra -Werror \
        "-DMC066_BYTES=$bytes" -DMC066_RUNTIME_CALLER -c "$source_file" \
        -o "$work/driver.gnu.o" || fail "GNU caller size=$bytes"
    "$host" -std=gnu11 -O0 -Wall -Wextra -Werror \
        "-DMC066_BYTES=$bytes" -c "$source_file" \
        -o "$work/callee.gnu.o" || fail "GNU callee size=$bytes"
    "$host" -no-pie "$work/driver.gnu.o" "$work/callee.gnu.o" \
        -o "$work/gnu-native" || fail "GNU link size=$bytes"
    result=0
    "$work/gnu-native" || result=$?
    [ "$result" -eq 42 ] || fail "GNU native size=$bytes exit=$result"
done

# Only report A64 ELF or QEMU PASS when real GNU cross tools execute them.
a64_elf=SKIP
a64_qemu=SKIP
gnu_cc=${AARCH64_GNU_CC:-aarch64-linux-gnu-gcc}
gnu_as=${AARCH64_GNU_AS:-aarch64-linux-gnu-as}
gnu_readelf=${AARCH64_GNU_READELF:-aarch64-linux-gnu-readelf}
cross_qemu=${AARCH64_QEMU:-qemu-aarch64}
if command -v "$gnu_as" >/dev/null 2>&1 && \
   command -v "$gnu_readelf" >/dev/null 2>&1; then
    "$host" -std=gnu11 -E -P -DMC066_BYTES=32 "$source_file" \
        -o "$work/abi-callee.i" || fail 'A64 ELF preprocess'
    "$mini" --target=aarch64-linux -S "$work/abi-callee.i" \
        -o "$work/abi-callee.s" || fail 'A64 ELF MiniC -S'
    "$gnu_as" "$work/abi-callee.s" -o "$work/abi-callee.o" || \
        fail 'A64 GNU assembler'
    "$gnu_readelf" -h "$work/abi-callee.o" | grep -q 'Machine:.*AArch64' || \
        fail 'A64 ELF wrong e_machine'
    a64_elf=PASS
fi
if [ "$a64_elf" = PASS ] && command -v "$gnu_cc" >/dev/null 2>&1 && \
   command -v "$cross_qemu" >/dev/null 2>&1; then
    for bytes in 16 17 32 64; do
        "$host" -std=gnu11 -E -P -DMC066_RUNTIME_CALLER \
            "-DMC066_BYTES=$bytes" "$source_file" \
            -o "$work/abi-caller.i" || fail 'A64 caller preprocess'
        "$host" -std=gnu11 -E -P "-DMC066_BYTES=$bytes" "$source_file" \
            -o "$work/abi-callee.i" || fail 'A64 callee preprocess'
        for role in caller callee; do
            "$mini" --target=aarch64-linux -S "$work/abi-$role.i" \
                -o "$work/$role.minic.s" || fail "A64 MiniC $role"
            "$gnu_as" "$work/$role.minic.s" \
                -o "$work/$role.minic.o" || fail "A64 GNU AS $role"
            if [ "$role" = caller ]; then flag=-DMC066_RUNTIME_CALLER; else flag=; fi
            "$gnu_cc" -std=gnu11 -O0 -fno-pie "-DMC066_BYTES=$bytes" \
                $flag -c "$source_file" -o "$work/$role.gnu.o" || \
                fail "A64 GNU C $role"
        done
        for caller in gnu minic; do
            for callee in gnu minic; do
                "$gnu_cc" -no-pie "$work/caller.$caller.o" \
                    "$work/callee.$callee.o" -o "$work/abi.elf" || \
                    fail "A64 link size=$bytes caller=$caller callee=$callee"
                result=0
                "$cross_qemu" -L "${AARCH64_QEMU_SYSROOT:-/usr/aarch64-linux-gnu}" \
                    "$work/abi.elf" || result=$?
                [ "$result" -eq 42 ] || \
                    fail "A64 QEMU size=$bytes caller=$caller callee=$callee exit=$result"
            done
        done
    done
    a64_qemu=PASS
fi
echo "MC066_A64_FP_POINTER_RECORD=PASS GNU_native=4 five_target_S=$compile_count A64_negative=$negative_count A64_GNU_ELF=$a64_elf A64_GNU_QEMU=$a64_qemu"
