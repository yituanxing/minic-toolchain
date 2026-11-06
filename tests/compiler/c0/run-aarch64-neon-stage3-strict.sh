#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu_cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
qemu=${QEMU_AARCH64:-qemu-aarch64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/aarch64-neon-stage3-strict
mkdir -p "$work"

# Existing supported 128-bit NEON addition receives a real two-ELF GNU/QEMU
# differential, before probing the much larger full arm_neon.h surface.
add_src="$root/tests/compiler/c0/aarch64_neon_vector128_add_core.c"
"$gnu_cc" -E -P -x c "$add_src" -o "$work/native-add.i"
"$gnu_cc" -static -O2 "$add_src" -o "$work/native-add.gnu.elf"
"$qemu" -cpu max "$work/native-add.gnu.elf"
"$minic_cc" --target=aarch64-linux -S "$work/native-add.i" -o "$work/native-add.minic.s"
"$gnu_cc" -static "$work/native-add.minic.s" -o "$work/native-add.minic.elf"
"$qemu" -cpu max "$work/native-add.minic.elf"
echo 'AARCH64_VECTOR128_ADD_QEMU=PASS GNU/MiniC real 16B+2D addition'

# This is a future green gate, intentionally independent of the Stage2
# fail-closed contract. Missing tools or unsupported MiniC semantics are FAIL,
# never SKIP/PASS. Both compilers see the SAME GNU-preprocessed TU.
for case_name in load_xor_store sha3_eor3; do
    source_file="$root/tests/compiler/c0/aarch64_neon_$case_name.c"
    "$gnu_cc" -E -P -x c "$source_file" -o "$work/$case_name.i"
    "$gnu_cc" -static -O2 "$source_file" -o "$work/$case_name.gnu.elf"
    "$qemu" -cpu max "$work/$case_name.gnu.elf"
    if ! "$minic_cc" --target=aarch64-linux -S "$work/$case_name.i" \
            -o "$work/$case_name.minic.s" \
            2>"$work/$case_name.minic.stderr"; then
        printf '%s\n' "AARCH64_NEON_STAGE3=FAIL stage=minic_compile case=$case_name" >&2
        cat "$work/$case_name.minic.stderr" >&2
        # Show the first actual failing GNU-preprocessed source location,
        # not a fixed 80-110 window that hid every later ACLE gap.
        failing_line=$(sed -n 's/.*\.i:\([0-9][0-9]*\):[0-9][0-9]*: error:.*/\1/p' \
            "$work/$case_name.minic.stderr" | head -n 1)
        if [ -n "$failing_line" ]; then
            first_line=$((failing_line > 8 ? failing_line - 8 : 1))
            last_line=$((failing_line + 8))
            sed -n "${first_line},${last_line}{=;p;}" \
                "$work/$case_name.i" >&2
        fi
        exit 1
    fi
    "$gnu_cc" -static "$work/$case_name.minic.s" -o "$work/$case_name.minic.elf"
    "$qemu" -cpu max "$work/$case_name.minic.elf"
done

# Permanent negative: lane-incompatible vector identities are not converted
# into each other or silently treated as 128-bit integers by either compiler.
bad_source="$root/tests/compiler/c0/aarch64_neon_invalid_mixed_xor.c"
"$gnu_cc" -E -P -x c "$bad_source" -o "$work/mixed-lane.i"
if "$gnu_cc" -c -x c "$bad_source" -o "$work/mixed-lane.gnu.o" \
    >"$work/mixed-lane.gnu.stdout" 2>"$work/mixed-lane.gnu.stderr"; then
    printf '%s\n' 'FAIL GNU admitted mixed AArch64 vector lane types' >&2
    exit 1
fi
if "$minic_cc" --target=aarch64-linux -S "$work/mixed-lane.i" \
    -o "$work/mixed-lane.minic.s" \
    >"$work/mixed-lane.minic.stdout" 2>"$work/mixed-lane.minic.stderr"; then
    printf '%s\n' 'FAIL MiniC admitted mixed AArch64 vector lane types' >&2
    exit 1
fi

# No AArch64 builtin may leak into an unrelated target's source vocabulary.
if "$minic_cc" --target=x86_64-linux -S "$work/load_xor_store.i" \
    -o "$work/foreign-isa.s" \
    >"$work/foreign-isa.stdout" 2>"$work/foreign-isa.stderr"; then
    printf '%s\n' 'FAIL MiniC leaked ARM ACLE to x86 target' >&2
    exit 1
fi

printf '%s\n' 'PASS strict AArch64 ACLE GNU=MiniC QEMU load/xor/store and SHA3 eor3'
