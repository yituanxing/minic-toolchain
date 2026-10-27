#!/usr/bin/env bash
set -Eeuo pipefail
cc=$1
arch=$2
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
if [[ "$arch" == x86_64 ]]; then
cat >"$dir/input.i" <<'EOF'
int exported(void) __asm__("minic_" "\x65" "xport");
int exported(void) { return 42; }
int main(void) { return exported(); }
EOF
"$cc" --target=x86_64-linux -S "$dir/input.i" -o "$dir/minic.s"
grep -F "minic_export:" "$dir/minic.s" >/dev/null
cc -no-pie -x assembler "$dir/minic.s" -o "$dir/minic.elf"
cc -no-pie -x c "$dir/input.i" -o "$dir/gnu.elf"
for name in minic gnu; do
    set +e
    "$dir/$name.elf"
    result=$?
    set -e
    test "$result" -eq 42 || exit 1
done
echo GNU_ASM_NAME_X86_NATIVE=PASS
elif [[ "$arch" == riscv64 ]]; then
cat >"$dir/input.i" <<'EOF'
unsigned long f(unsigned long y) {
    register unsigned long x __asm__("a" "0") = y + 1UL;
    return x;
}
unsigned long g(unsigned long y) {
    register unsigned long x __asm__("\x61" "1") = y + 2UL;
    return x;
}
int main(void) { return f(19UL) == 20UL && g(20UL) == 22UL ? 42 : 1; }
EOF
"$cc" --target=riscv64-linux -S "$dir/input.i" -o "$dir/minic.s"
riscv64-linux-gnu-gcc -static -x assembler "$dir/minic.s" -o "$dir/minic.elf"
riscv64-linux-gnu-gcc -static -x c "$dir/input.i" -o "$dir/gnu.elf"
for name in minic gnu; do
    set +e
    qemu-riscv64 "$dir/$name.elf"
    result=$?
    set -e
    test "$result" -eq 42 || exit 1
done
echo GNU_ASM_NAME_RV64_QEMU=PASS
else exit 1
fi
