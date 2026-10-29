#!/usr/bin/env bash
# Linux 6.6.143 lib/zstd/decompress/zstd_decompress_block.c:
# GNU basic asm (no colon fields) is implicitly volatile and aligns an
# inner decoder loop to 64 bytes; no arbitrary opaque assembler is allowed.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
int mini_align(int x) {
    x += 1;
    asm(".p2align 6");
    x += 2;
    return x;
}
EOF
sed 's/mini_align/gnu_align/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern int mini_align(int), gnu_align(int);
int main(void) {
    int i;
    for (i = -10; i <= 10; ++i)
        if (mini_align(i) != gnu_align(i)) return 1;
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F '.p2align 6' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux ZSTD align GNU differential=$result" >&2
    exit 1
}

sed 's/\.p2align 6/\.p2align 5/g' "$d/mini.i" >"$d/unsafe_alignment.i"
sed 's/\.p2align 6/\.p2align 6\\n\\tret/g' "$d/mini.i" >"$d/unsafe_payload.i"
sed 's/asm(".p2align 6")/asm(".p2align 6" : : : "memory")/g' \
    "$d/mini.i" >"$d/unsafe_extended_clobber.i"
for shape in unsafe_alignment unsafe_payload unsafe_extended_clobber; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe x86 ZSTD alignment accepted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_ZSTD_P2ALIGN=PASS GNU_native=21 exact_alignment=64 reject=3"
