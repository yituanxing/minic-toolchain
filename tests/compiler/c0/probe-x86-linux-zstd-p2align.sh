#!/usr/bin/env bash
# Linux 6.6.143 lib/zstd/decompress/zstd_decompress_block.c:
# GNU basic asm (no colon fields) is implicitly volatile and aligns an
# inner decoder loop to 64 bytes; no arbitrary opaque assembler is allowed.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
# Linux's ZSTD decoder uses this precise sequence of four alignments and
# explicitly inserted `nop` instructions; basic GNU asm is volatile.
cat >"$d/mini.i" <<'EOF'
int mini_align(int x) {
    x += 1;
    asm(".p2align 6");
    x += 2;
    return x;
}

int mini_align_profile(int x) {
    x *= 2;
    asm(".p2align 6");
    asm("nop");
    asm(".p2align 5");
    asm("nop");
    asm(".p2align 4");
    asm("nop");
    asm(".p2align 3");
    return x + 3;
}
EOF
sed 's/mini_align/gnu_align/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern int mini_align(int), gnu_align(int);
extern int mini_align_profile(int), gnu_align_profile(int);
int main(void) {
    int i;
    for (i = -10; i <= 10; ++i)
        if (mini_align(i) != gnu_align(i) ||
            mini_align_profile(i) != gnu_align_profile(i)) return 1;
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F '.p2align 6' "$d/mini.s" >/dev/null
for shape in '.p2align 3' '.p2align 4' '.p2align 5' 'nop'; do
    grep -F "$shape" "$d/mini.s" >/dev/null
done
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux ZSTD align GNU differential=$result" >&2
    exit 1
}

sed 's/\.p2align 6/\.p2align 7/g' "$d/mini.i" >"$d/unsafe_alignment.i"
sed 's/\.p2align 6/\.p2align 6\\n\\tret/g' "$d/mini.i" >"$d/unsafe_payload.i"
sed 's/asm(".p2align 6")/asm(".p2align 6" : : : "memory")/g' \
    "$d/mini.i" >"$d/unsafe_extended_clobber.i"
sed 's/asm("nop")/asm("nop; ret")/g' "$d/mini.i" >"$d/unsafe_nop_payload.i"
sed 's/asm("nop")/asm("nop" : : : "memory")/g' "$d/mini.i" >"$d/unsafe_nop_clobber.i"
for shape in unsafe_alignment unsafe_payload unsafe_extended_clobber \
    unsafe_nop_payload unsafe_nop_clobber; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe x86 ZSTD alignment accepted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_ZSTD_P2ALIGN=PASS GNU_native=21 align=3,4,5,6 nop=3 reject=5"
