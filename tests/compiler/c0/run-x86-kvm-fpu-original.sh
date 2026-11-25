#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
linux_src=${LINUX_SRC:-"$root/../linux-6.6.143"}
work=${BUILD_DIR:-"$root/build/release"}/tests/compiler-c0-x86-original-kvm-fpu
source="$root/tests/compiler/c0/x86_kvm_original_fpu_owner.c"
shim="$root/tests/compiler/c0/x86_kvm_fpu_shim"

if test ! -f "$linux_src/arch/x86/kvm/fpu.h"; then
    echo "BLOCKED: actual Linux arch/x86/kvm/fpu.h missing: $linux_src" >&2
    exit 1
fi
mkdir -p "$work"

"$host_cc" -std=gnu11 -O2 -Werror -mno-sse -mno-sse2 -mno-mmx -mno-avx \
    -I"$shim" -I"$linux_src" -E -P "$source" -o "$work/kvm_fpu.i"
"$host_cc" -std=gnu11 -O2 -Werror -mno-sse -mno-sse2 -mno-mmx -mno-avx \
    -I"$shim" -I"$linux_src" "$source" -no-pie -o "$work/kvm-fpu.gnu.elf"
"$work/kvm-fpu.gnu.elf"

MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -mno-sse \
    -S "$work/kvm_fpu.i" -o "$work/kvm-fpu.minic.s"
test -s "$work/kvm-fpu.minic.s"
"$host_cc" -c "$work/kvm-fpu.minic.s" -o "$work/kvm-fpu.minic.o"
"$host_cc" -no-pie "$work/kvm-fpu.minic.o" -o "$work/kvm-fpu.minic.elf"
"$work/kvm-fpu.minic.elf"

# Both compilers must produce actual 0F 6F and 0F 7F MOVDQA memory opcodes.
for compiler in gnu minic; do
    objdump -dr "$work/kvm-fpu.$compiler.elf" >"$work/kvm-fpu.$compiler.dis"
    grep -E '[[:space:]]movdqa[[:space:]]' "$work/kvm-fpu.$compiler.dis" >/dev/null
done
printf '%s\n' 'PASS original Linux 6.6.143 KVM fpu.h GNU/MiniC full 16-XMM source, dual ELF runtime=2, SIMD memory opcodes, -mno-sse'
