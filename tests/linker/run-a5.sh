#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
CC=${RISCV_CC:-riscv64-linux-gnu-gcc}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}
OBJDUMP=${RISCV_OBJDUMP:-riscv64-linux-gnu-objdump}
QEMU=${QEMU_RISCV64:-qemu-riscv64}
SYSROOT=${RISCV_GLIBC_ROOT:-/usr/riscv64-linux-gnu}

work="$BUILD_DIR/tests/linker/a5"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/caller.s" <<'EOF'
.text
.globl shared_call_external
.type shared_call_external, @function
shared_call_external:
  call external_add
  ret
.size shared_call_external, .-shared_call_external
EOF

cat >"$work/consumer.c" <<'EOF'
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

int external_add(int a, int b) {
    return a + b;
}

typedef int (*shared_fn)(int, int);

int main(int argc, char **argv) {
    void *handle;
    shared_fn fn;
    const char *error;
    int value;

    if (argc != 2) {
        return 90;
    }
    handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "A5_DLOPEN_ERROR=%s\n", dlerror());
        return 91;
    }
    dlerror();
    *(void **)(&fn) = dlsym(handle, "shared_call_external");
    error = dlerror();
    if (error != NULL || fn == NULL) {
        fprintf(stderr, "A5_DLSYM_ERROR=%s\n", error != NULL ? error : "null");
        return 92;
    }
    value = fn(19, 23);
    printf("MINILD_A5_RUNTIME=%d\n", value);
    dlclose(handle);
    return value == 42 ? 0 : 93;
}
EOF

"$AS" -march=rv64gc -mabi=lp64d -o "$work/caller.o" "$work/caller.s"
"$READELF" -Wr "$work/caller.o" >"$work/caller.relocs"
grep -Eq 'R_RISCV_CALL(_PLT)?[[:space:]].*external_add' "$work/caller.relocs"

"$LD" -melf64lriscv -shared -soname libminild-a5.so   -o "$work/reference.so" "$work/caller.o"

"$MINILD" -melf64lriscv -shared -soname libminild-a5.so   -o "$work/product.so" "$work/caller.o"

"$READELF" -h "$work/product.so" >"$work/product.header"
"$READELF" -lW "$work/product.so" >"$work/product.programs"
"$READELF" -SW "$work/product.so" >"$work/product.sections"
"$READELF" -dW "$work/product.so" >"$work/product.dynamic"
"$READELF" -Ws "$work/product.so" >"$work/product.symbols"
"$READELF" -Wr "$work/product.so" >"$work/product.relocs"

"$OBJDUMP" -d -j .plt "$work/reference.so" >"$work/reference.plt.dis"
"$OBJDUMP" -d -j .plt "$work/product.so" >"$work/product.plt.dis"
"$READELF" -x .got.plt "$work/reference.so" >"$work/reference.gotplt.hex"
"$READELF" -x .got.plt "$work/product.so" >"$work/product.gotplt.hex"

echo "=== A5 GNU PLT ==="
cat "$work/reference.plt.dis"
echo "=== A5 MiniLD PLT ==="
cat "$work/product.plt.dis"
echo "=== A5 GNU GOT.PLT ==="
cat "$work/reference.gotplt.hex"
echo "=== A5 MiniLD GOT.PLT ==="
cat "$work/product.gotplt.hex"

grep -q 'DYN (Shared object file)' "$work/product.header"
grep -Eq '] \.plt[[:space:]]+PROGBITS' "$work/product.sections"
grep -Eq '] \.got\.plt[[:space:]]+PROGBITS' "$work/product.sections"
grep -Eq '] \.rela\.plt[[:space:]]+RELA' "$work/product.sections"
grep -q '(PLTGOT)' "$work/product.dynamic"
grep -q '(PLTRELSZ)' "$work/product.dynamic"
grep -q '(PLTREL)' "$work/product.dynamic"
grep -q '(JMPREL)' "$work/product.dynamic"
grep -Eq 'R_RISCV_JUMP_SLOT.*external_add' "$work/product.relocs"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.*UND.* external_add$' "$work/product.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* shared_call_external$' "$work/product.symbols"

"$CC" -O2 -Wall -Wextra -Werror -Wl,-E   "$work/consumer.c" -ldl -o "$work/consumer"

timeout 5s "$QEMU" -L "$SYSROOT" "$work/consumer" "$work/reference.so"   >"$work/reference.stdout" 2>"$work/reference.stderr"

set +e
timeout 5s "$QEMU" -L "$SYSROOT" "$work/consumer" "$work/product.so"   >"$work/product.stdout" 2>"$work/product.stderr"
product_rc=$?
set -e

echo "A5_PRODUCT_QEMU_RC=$product_rc"
cat "$work/product.stdout"
cat "$work/product.stderr"

grep -q '^MINILD_A5_RUNTIME=42
cmp "$work/reference.stdout" "$work/product.stdout"

echo "MINILD_A5=PASS plt=PASS gotplt=PASS jump_slot=PASS call_plt=PASS dlopen=PASS dlsym=PASS qemu=PASS"
 "$work/reference.stdout"
test "$product_rc" -eq 0
grep -q '^MINILD_A5_RUNTIME=42
cmp "$work/reference.stdout" "$work/product.stdout"

echo "MINILD_A5=PASS plt=PASS gotplt=PASS jump_slot=PASS call_plt=PASS dlopen=PASS dlsym=PASS qemu=PASS"
 "$work/product.stdout"
cmp "$work/reference.stdout" "$work/product.stdout"

echo "MINILD_A5=PASS plt=PASS gotplt=PASS jump_slot=PASS call_plt=PASS dlopen=PASS dlsym=PASS qemu=PASS"
