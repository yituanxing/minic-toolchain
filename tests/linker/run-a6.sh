#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

CC=${RISCV_CC:-riscv64-linux-gnu-gcc}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}
QEMU=${QEMU_RISCV64:-qemu-riscv64}
SYSROOT=${RISCV_GLIBC_ROOT:-/usr/riscv64-linux-gnu}

work="$BUILD_DIR/tests/linker/a6"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/provider.c" <<'EOF'
__thread int tls_value __attribute__((tls_model("global-dynamic"))) = 40;

int shared_tls_probe(void) {
    tls_value += 2;
    return tls_value;
}
EOF

cat >"$work/consumer.c" <<'EOF'
#include <dlfcn.h>
#include <stdio.h>

typedef int (*probe_fn)(void);

int main(int argc, char **argv) {
    void *handle;
    probe_fn fn;
    const char *error;
    int value;

    if (argc != 2) return 90;
    handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "A6_DLOPEN_ERROR=%s\n", dlerror());
        return 91;
    }
    dlerror();
    *(void **)(&fn) = dlsym(handle, "shared_tls_probe");
    error = dlerror();
    if (error != NULL || fn == NULL) {
        fprintf(stderr, "A6_DLSYM_ERROR=%s\n", error != NULL ? error : "null");
        return 92;
    }
    value = fn();
    printf("MINILD_A6_RUNTIME=%d\n", value);
    dlclose(handle);
    return value == 42 ? 0 : 93;
}
EOF

"$CC" -O2 -fPIC -ftls-model=global-dynamic -c   "$work/provider.c" -o "$work/provider.o"
"$READELF" -Wr "$work/provider.o" >"$work/provider.relocs"
grep -q 'R_RISCV_TLS_GD_HI20' "$work/provider.relocs"
grep -Eq 'R_RISCV_CALL(_PLT)?.*__tls_get_addr' "$work/provider.relocs"

"$LD" -melf64lriscv -shared -soname libminild-a6.so   -o "$work/reference.so" "$work/provider.o"

"$MINILD" -melf64lriscv -shared -soname libminild-a6.so   -o "$work/product.so" "$work/provider.o"

for kind in reference product; do
  "$READELF" -h "$work/$kind.so" >"$work/$kind.header"
  "$READELF" -lW "$work/$kind.so" >"$work/$kind.programs"
  "$READELF" -SW "$work/$kind.so" >"$work/$kind.sections"
  "$READELF" -dW "$work/$kind.so" >"$work/$kind.dynamic"
  "$READELF" -Ws "$work/$kind.so" >"$work/$kind.symbols"
  "$READELF" -Wr "$work/$kind.so" >"$work/$kind.relocs"
done

grep -q ' TLS ' "$work/product.programs"
grep -Eq '] \.tdata[[:space:]]+PROGBITS' "$work/product.sections"
grep -Eq 'R_RISCV_TLS_DTPMOD64.*tls_value' "$work/product.relocs"
grep -Eq 'R_RISCV_TLS_DTPREL64.*tls_value' "$work/product.relocs"
grep -Eq 'R_RISCV_JUMP_SLOT.*__tls_get_addr' "$work/product.relocs"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* shared_tls_probe$' "$work/product.symbols"
grep -Eq 'TLS[[:space:]]+GLOBAL[[:space:]]+DEFAULT.* tls_value$' "$work/product.symbols"

"$CC" -O2 -Wall -Wextra -Werror   "$work/consumer.c" -ldl -o "$work/consumer"

timeout 5s "$QEMU" -L "$SYSROOT" "$work/consumer" "$work/reference.so"   >"$work/reference.stdout" 2>"$work/reference.stderr"

set +e
timeout 5s "$QEMU" -L "$SYSROOT" "$work/consumer" "$work/product.so"   >"$work/product.stdout" 2>"$work/product.stderr"
product_rc=$?
set -e

echo "A6_PRODUCT_QEMU_RC=$product_rc"
cat "$work/product.stdout"
cat "$work/product.stderr"

grep -q '^MINILD_A6_RUNTIME=42$' "$work/reference.stdout"
test "$product_rc" -eq 0
grep -q '^MINILD_A6_RUNTIME=42$' "$work/product.stdout"
cmp "$work/reference.stdout" "$work/product.stdout"

echo "MINILD_A6=PASS pt_tls=PASS tls_gd=PASS dtpmod64=PASS dtprel64=PASS tls_get_addr_plt=PASS dlopen=PASS qemu=PASS"
