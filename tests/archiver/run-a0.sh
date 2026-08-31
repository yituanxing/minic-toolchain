#!/bin/sh
set -eu

: "${MINIAR:?MINIAR must point to minic-ar}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

HOST_CC=${HOST_CC:-cc}
HOST_AR=${HOST_AR:-ar}
HOST_NM=${HOST_NM:-nm}
RISCV_CC=${RISCV_CC:-riscv64-linux-gnu-gcc}
RISCV_AR=${RISCV_AR:-riscv64-linux-gnu-ar}
RISCV_NM=${RISCV_NM:-riscv64-linux-gnu-nm}

work="$BUILD_DIR/tests/archiver/a0"
rm -rf "$work"
mkdir -p "$work/normal" "$work/long/subdirectory_name" "$work/thin/sub"

cat >"$work/a.c" <<'SRC'
int alpha(void) { return 20; }
SRC
cat >"$work/b.c" <<'SRC'
int beta(void) { return 22; }
SRC
cat >"$work/main.c" <<'SRC'
int alpha(void);
int beta(void);
int main(void) { return alpha() + beta() == 42 ? 0 : 1; }
SRC

"$HOST_CC" -c "$work/a.c" -o "$work/normal/a.o"
"$HOST_CC" -c "$work/b.c" -o "$work/normal/b.o"
(
    cd "$work/normal"
    "$HOST_AR" rcsD reference.a a.o b.o
    "$MINIAR" rcsD product.a a.o b.o
    cmp reference.a product.a
    "$HOST_AR" t product.a >product.members
    printf 'a.o\nb.o\n' >expected.members
    cmp expected.members product.members
    "$HOST_NM" -s product.a >product.nm
    grep -q '^alpha in a.o$' product.nm
    grep -q '^beta in b.o$' product.nm
)
"$HOST_CC" "$work/main.c" "$work/normal/product.a" -o "$work/normal/run"
"$work/normal/run"

cp "$work/normal/a.o" "$work/long/subdirectory_name/very_long_member_name_object.o"
(
    cd "$work/long"
    "$HOST_AR" rcsDP reference.a subdirectory_name/very_long_member_name_object.o
    "$MINIAR" rcsDP product.a subdirectory_name/very_long_member_name_object.o
    cmp reference.a product.a
    "$HOST_AR" t product.a | grep -q '^subdirectory_name/very_long_member_name_object.o$'
)

"$HOST_CC" -c "$work/a.c" -o "$work/thin/sub/a.o"
"$HOST_CC" -c "$work/b.c" -o "$work/thin/sub/b.o"
(
    cd "$work/thin"
    "$HOST_AR" rcSTPD reference-built-in.a sub/a.o sub/b.o
    "$MINIAR" rcSTPD product-built-in.a sub/a.o sub/b.o
    cmp reference-built-in.a product-built-in.a
    test "$(head -c 8 product-built-in.a)" = '!<thin>'

    "$HOST_AR" rcsTPD reference-lib.a sub/a.o sub/b.o
    "$MINIAR" rcsTPD product-lib.a sub/a.o sub/b.o
    cmp reference-lib.a product-lib.a
    "$HOST_NM" -s product-lib.a >product-lib.nm
    grep -q 'alpha in .*sub/a.o$' product-lib.nm
    grep -q 'beta in .*sub/b.o$' product-lib.nm
)
"$HOST_CC" "$work/main.c" "$work/thin/product-lib.a" -o "$work/thin/run"
"$work/thin/run"

(
    cd "$work/thin"
    "$HOST_AR" rcSTPD reference-empty.a
    "$MINIAR" rcSTPD product-empty.a
    cmp reference-empty.a product-empty.a
)

if command -v "$RISCV_CC" >/dev/null 2>&1 &&
   command -v "$RISCV_AR" >/dev/null 2>&1 &&
   command -v "$RISCV_NM" >/dev/null 2>&1; then
    mkdir -p "$work/riscv/sub"
    "$RISCV_CC" -c "$work/a.c" -o "$work/riscv/sub/a.o"
    "$RISCV_CC" -c "$work/b.c" -o "$work/riscv/sub/b.o"
    (
        cd "$work/riscv"
        "$RISCV_AR" rcsTPD reference.a sub/a.o sub/b.o
        "$MINIAR" rcsTPD product.a sub/a.o sub/b.o
        cmp reference.a product.a
        "$RISCV_NM" -s product.a >product.nm
        grep -q 'alpha in .*sub/a.o$' product.nm
        grep -q 'beta in .*sub/b.o$' product.nm
    )
else
    echo "MINIAR_A0_RISCV=SKIP missing cross binutils/compiler"
fi

echo "MINIAR_A0=PASS normal=byte-exact long=byte-exact thin-no-index=byte-exact thin-index=byte-exact"
