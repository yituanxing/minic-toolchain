#!/usr/bin/env bash
# Linux 6.6.143 arch/x86/pci/mmconfig_64.c uses a fixed AL output and
# one byte-sized memory operand; the missing memory clobber is intentional.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
unsigned char mini_mmio(const volatile unsigned char *p) {
    unsigned char value;
    asm volatile("movb %1, %%al" : "=a"(value) : "m"(*p));
    return value;
}
EOF
sed 's/mini_mmio/gnu_mmio/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern unsigned char mini_mmio(const volatile unsigned char *);
extern unsigned char gnu_mmio(const volatile unsigned char *);
int main(void) {
    static const unsigned char values[] = {0,1,2,0x7f,0x80,0xff};
    unsigned i;
    for (i = 0; i < sizeof(values); ++i)
        if (mini_mmio(&values[i]) != gnu_mmio(&values[i])) return 1;
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'movzbl (%r11), %eax' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux MMIO explicit AL GNU differential=$result" >&2
    exit 1
}
sed 's/"=a"/"=q"/g' "$d/mini.i" >"$d/unsafe_output.i"
sed 's/"m"(\*p)/"r"(*p)/g' "$d/mini.i" >"$d/unsafe_input.i"
sed 's/"movb %1, %%al"/"movw %1, %%al"/g' "$d/mini.i" >"$d/unsafe_width.i"
for shape in unsafe_output unsafe_input unsafe_width; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe x86 MMIO explicit AL admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_MMIO_EXPLICIT_AL=PASS GCC_native=6 reject=3"
