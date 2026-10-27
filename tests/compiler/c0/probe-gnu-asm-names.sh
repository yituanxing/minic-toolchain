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
# The four-architecture CI installs a GNU cross assembler and QEMU but
# does not install a riscv64 glibc sysroot (crt1.o/libc.a). Use the
# already-proven freestanding syscall exit ABI rather than pulling in a
# second Linux userspace. The exact same start.o calls each compiler's
# independent main() and exits through the genuine RV64 Linux syscall.
cat >"$dir/start.s" <<'EOF'
.text
.globl _start
_start:
  call main
  li a7, 93
  ecall
EOF
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -ffreestanding -fno-builtin \
  -c -x c "$dir/input.i" -o "$dir/gnu.o"
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -static -nostdlib -no-pie \
  -Wl,-e,_start -x assembler "$dir/start.s" "$dir/minic.s" -o "$dir/minic.elf"
riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -static -nostdlib -no-pie \
  -Wl,-e,_start -x assembler "$dir/start.s" -x none "$dir/gnu.o" -o "$dir/gnu.elf"
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
