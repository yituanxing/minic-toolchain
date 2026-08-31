#!/bin/sh
set -eu

: "${MINIAR:?MINIAR must point to minic-ar}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

HOST_CC=${HOST_CC:-cc}
HOST_AR=${HOST_AR:-ar}
HOST_NM=${HOST_NM:-nm}

work="$BUILD_DIR/tests/archiver/a1"
rm -rf "$work"
mkdir -p "$work/inner/leaf"

cat >"$work/a.c" <<'SRC'
int nested_alpha(void) { return 7; }
SRC
cat >"$work/b.c" <<'SRC'
int nested_beta(void) { return 9; }
SRC
cat >"$work/main.c" <<'SRC'
int nested_alpha(void);
int nested_beta(void);
int main(void) { return nested_alpha() + nested_beta() == 16 ? 0 : 1; }
SRC

"$HOST_CC" -c "$work/a.c" -o "$work/inner/leaf/a.o"
"$HOST_CC" -c "$work/b.c" -o "$work/inner/leaf/b.o"

(
    cd "$work/inner"
    "$HOST_AR" rcSD empty-child.a
    "$HOST_AR" rcSTPD reference-empty-parent.a empty-child.a leaf/a.o
    "$MINIAR" rcSTPD product-empty-parent.a empty-child.a leaf/a.o
    cmp reference-empty-parent.a product-empty-parent.a
    test "$("$HOST_AR" t product-empty-parent.a)" = "leaf/a.o"
)

(
    cd "$work/inner"
    "$HOST_AR" rcsTPD reference-inner.a leaf/a.o leaf/b.o
    "$MINIAR" rcsTPD product-inner.a leaf/a.o leaf/b.o
    cmp reference-inner.a product-inner.a
)

(
    cd "$work"
    "$HOST_AR" rcsTPD reference-outer.a inner/reference-inner.a
    "$MINIAR" rcsTPD product-outer.a inner/product-inner.a
    cmp reference-outer.a product-outer.a

    "$HOST_AR" t reference-outer.a >reference.list
    "$MINIAR" t product-outer.a >product.list
    cmp reference.list product.list
    printf 'inner/leaf/a.o\ninner/leaf/b.o\n' >expected.list
    cmp expected.list product.list

    "$HOST_NM" -s product-outer.a >product.nm
    grep -q 'nested_alpha in .*inner/leaf/a.o$' product.nm
    grep -q 'nested_beta in .*inner/leaf/b.o$' product.nm
)

"$HOST_CC" "$work/main.c" "$work/product-outer.a" -o "$work/run"
"$work/run"

(
    cd "$work"
    cp product-outer.a move.reference.a
    cp product-outer.a move.product.a
    "$HOST_AR" mPiT inner/leaf/a.o move.reference.a
    "$MINIAR" mPiT inner/leaf/a.o move.product.a
    cmp move.reference.a move.product.a
)

(
    cd "$work"
    "$HOST_AR" rcsDP ordinary.a inner/leaf/a.o inner/leaf/b.o
    "$HOST_AR" t ordinary.a >ordinary.reference.list
    "$MINIAR" t ordinary.a >ordinary.product.list
    cmp ordinary.reference.list ordinary.product.list
)

echo "MINIAR_A1=PASS thin-flatten=byte-exact empty-archive-flatten=byte-exact list=gnu-exact noop-mPiT=byte-exact nested-link=PASS"
