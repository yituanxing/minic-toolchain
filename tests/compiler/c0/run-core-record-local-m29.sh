#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-record-local-m29}"
mkdir -p "$BUILD_DIR"

MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_record_local_m29.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_record_local_m29_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf" > "$BUILD_DIR/minic.out"

"$RISCV_CC" -O0 -static tests/compiler/c0/core_record_local_m29_runtime.c tests/compiler/c0/core_record_local_m29.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf" > "$BUILD_DIR/gcc.out"

# Returning const-qualified records is a by-value copy; the Core IR
# verifier must not reject its RETURN_OBJECT merely because the original
# address-backed local retained top-level const in its type identity.
cat >"$BUILD_DIR/qualified-record.c" <<'SRC'
struct qualified_record { unsigned long x, y, z; };
struct qualified_record qualified_return(unsigned int choose) {
    struct qualified_record plain = {3UL, 5UL, 7UL};
    if (choose) {
        const struct qualified_record frozen = {11UL, 13UL, 17UL};
        return frozen;
    }
    return plain;
}
unsigned long qualified_result(unsigned int choose) {
    struct qualified_record copy = qualified_return(choose);
    return copy.x + copy.y + copy.z;
}
SRC
cat >"$BUILD_DIR/qualified-record-main.c" <<'SRC'
#include <stdio.h>
extern unsigned long qualified_result(unsigned int);
int main(void) {
    printf("qualified-return 0=%lu 1=%lu\\n",
           qualified_result(0U), qualified_result(1U));
    return qualified_result(0U) == 15UL &&
           qualified_result(1U) == 41UL ? 0 : 1;
}
SRC
"$RISCV_CC" -E -P -std=gnu11 "$BUILD_DIR/qualified-record.c" \
    -o "$BUILD_DIR/qualified-record.i"
"$MINIC" -S "$BUILD_DIR/qualified-record.i" \
    -o "$BUILD_DIR/qualified-record-mini.s"
"$RISCV_CC" -static "$BUILD_DIR/qualified-record-main.c" \
    "$BUILD_DIR/qualified-record-mini.s" \
    -o "$BUILD_DIR/qualified-record-mini.elf"
"$RISCV_CC" -static "$BUILD_DIR/qualified-record-main.c" \
    "$BUILD_DIR/qualified-record.c" \
    -o "$BUILD_DIR/qualified-record-gnu.elf"
"$QEMU_RISCV64" "$BUILD_DIR/qualified-record-mini.elf" \
    >"$BUILD_DIR/qualified-record-mini.out"
"$QEMU_RISCV64" "$BUILD_DIR/qualified-record-gnu.elf" \
    >"$BUILD_DIR/qualified-record-gnu.out"
diff -u "$BUILD_DIR/qualified-record-gnu.out" \
    "$BUILD_DIR/qualified-record-mini.out"

diff -u "$BUILD_DIR/gcc.out" "$BUILD_DIR/minic.out"
printf '%s\n' 'PASS compiler/c0/core-record-local-m29'
