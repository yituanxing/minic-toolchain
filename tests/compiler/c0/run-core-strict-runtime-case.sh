#!/bin/sh
# Shared strict Core RV64 differential: retain each fixture and its verdict.
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: $0 CASE" >&2
    exit 2
fi
# Strict native x86 record cases retain both cross-target source checks.
case "$1" in
    record-array-range-once|record-rvalue-array-member)
        name=$1
        root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
        minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
        host_cc=${HOST_CC:-${CC:-cc}}
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-$name
        mkdir -p "$work"
        case "$name" in
            record-array-range-once) source=record_array_range_once; native=x86; loong=loong ;;
            record-rvalue-array-member) source=record_rvalue_array_member; native=input; loong=loongarch64 ;;
        esac
        "$host_cc" -std=gnu11 -E -P -x c "$root/tests/compiler/c0/$source.c" -o "$work/input.i"
        MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -S "$work/input.i" -o "$work/$native.s"
        "$minic_cc" --target=aarch64-linux -S "$work/input.i" -o "$work/aarch64.s"
        "$minic_cc" --target=loongarch64-linux -S "$work/input.i" -o "$work/$loong.s"
        "$host_cc" -no-pie "$work/$native.s" -o "$work/$native"
        "$work/$native"
        case "$name" in
            record-array-range-once)
                printf '%s\n' 'PASS compiler/c0/record_array_range_once aggregate-range=1 once-only-call=1 zero-gaps=1 compound-literal=1 x86-native=1 other-targets=2 strict-core=1'
                ;;
            record-rvalue-array-member)
                "$host_cc" -std=gnu11 -E -P -x c \
                    "$root/tests/compiler/c0/invalid_address_record_rvalue_array.c" \
                    -o "$work/invalid.i"
                if "$minic_cc" --target=x86_64-linux -S "$work/invalid.i" -o "$work/invalid.s" \
                    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
                    echo 'FAIL address of record rvalue array unexpectedly compiled' >&2
                    exit 1
                fi
                grep -F 'address-of requires an lvalue object or function designator' \
                    "$work/invalid.stderr" >/dev/null
                printf '%s\n' 'PASS compiler/c0/record_rvalue_array_member targets=x86_64,aarch64,loongarch64 decay=1 subscript=1 nested=1 conditional=1 sizeof=1 once-only-effects=1 nonlvalue=1 strict-core=1'
                ;;
        esac
        exit 0 ;;
esac

case "$1" in
    core-const-parameter-m23b|core-integer-conditional-m23|core-postfix-update-m24|core-discard-expression-m25|core-integer-binary-preservation-m25b|core-integer-less-m26|core-integer-foundation-m26b|core-switch-m27|core-aggregate-boundary-m30) ;;
    *) echo "unknown strict Core runtime case: $1" >&2; exit 2 ;;
esac
name=$1
source=$(printf '%s' "$name" | tr '-' '_')
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/$name}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S "tests/compiler/c0/$source.c" -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static "tests/compiler/c0/${source}_runtime.c" "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
if [ "$name" = core-aggregate-boundary-m30 ]; then
    "$QEMU_RISCV64" "$BUILD_DIR/minic.elf" > "$BUILD_DIR/minic.out"
else
    "$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
fi
"$RISCV_CC" -O0 -static "tests/compiler/c0/${source}_runtime.c" "tests/compiler/c0/$source.c" -o "$BUILD_DIR/gcc.elf"
if [ "$name" = core-aggregate-boundary-m30 ]; then
    "$QEMU_RISCV64" "$BUILD_DIR/gcc.elf" > "$BUILD_DIR/gcc.out"
    diff -u "$BUILD_DIR/gcc.out" "$BUILD_DIR/minic.out"
else
    "$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
fi
printf '%s\n' "PASS compiler/c0/$name"
