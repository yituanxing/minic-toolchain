#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-inline-asm-identity-m28}"
mkdir -p "$BUILD_DIR"

MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_inline_asm_identity_m28.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_inline_asm_identity_m28_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_inline_asm_identity_m28_runtime.c tests/compiler/c0/core_inline_asm_identity_m28.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"

check_unsupported() {
    name="$1"
    source="$2"
    file="$BUILD_DIR/$name.i"
    err="$BUILD_DIR/$name.err"
    printf '%s\n' "$source" > "$file"
    if MINIC_CORE_IR=strict "$MINIC" -S "$file" -o "$BUILD_DIR/$name.s" 2>"$err"; then
        echo "M28 negative unexpectedly supported: $name" >&2
        exit 1
    fi
    grep -F "Core IR shadow does not yet support function 'probe_m28_${name}'" "$err" >/dev/null
}

check_unsupported plus_r 'unsigned long probe_m28_plus_r(unsigned long value) { __asm__("" : "+r"(value)); return value; }'
check_unsupported nonempty 'unsigned long probe_m28_nonempty(unsigned long value) { __asm__("nop" : "+rm"(value)); return value; }'
check_unsupported volatile_rm 'unsigned long probe_m28_volatile_rm(unsigned long value) { __asm__ volatile("" : "+rm"(value)); return value; }'
check_unsupported dereference 'void probe_m28_dereference(unsigned long *p) { __asm__("" : "+rm"(*p)); }'

printf '%s\n' 'PASS compiler/c0/core-inline-asm-identity-m28'
