#!/usr/bin/env bash
# Linux drivers/char/hw_random/via-rng.c exact XSTORE inline ASM: a hardware
# 4-output operation with memory alias, EAX count, EDX control, EDI address.
# Do not execute the CPU opcode: physical VIA PadLock is not on every runner.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
typedef unsigned int u32;
void mini_xstore(u32 *addr, u32 edx_in, u32 *eax_ptr,
                 u32 *edx_ptr, u32 **edi_ptr) {
    u32 eax_out;
    asm(".byte 0x0F,0xA7,0xC0 /* xstore %%edi (addr=%0) */"
        : "=m" (*addr), "=a" (eax_out), "+d" (edx_in), "+D" (addr));
    *eax_ptr = eax_out;
    *edx_ptr = edx_in;
    *edi_ptr = addr;
}
EOF
sed 's/mini_xstore/gnu_xstore/g' "$d/mini.i" >"$d/gnu.c"
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F '.byte 0x0f, 0xa7, 0xc0' "$d/mini.s" >/dev/null
grep -F 'movl %edx, (%r11)' "$d/mini.s" >/dev/null
grep -F 'movq %rdi, (%r11)' "$d/mini.s" >/dev/null
cc -c -x assembler "$d/gnu.s" -o "$d/gnu.o"
cc -c -x assembler "$d/mini.s" -o "$d/mini.o"
test "$(objdump -d "$d/gnu.o" | grep -ic '0f a7 c0')" -eq 1
test "$(objdump -d "$d/mini.o" | grep -ic '0f a7 c0')" -eq 1

# A memory-output alias differing from the hardware EDI pointer is unsafe.
sed 's/u32 eax_out;/u32 eax_out, other;/' "$d/mini.i" |
    sed 's/"=m" (\*addr)/"=m" (other)/' >"$d/unsafe_alias.i"
sed 's/u32 edx_in/unsigned long edx_in/g' "$d/mini.i" >"$d/unsafe_control.i"
sed 's/"=a"/"=r"/g' "$d/mini.i" >"$d/unsafe_register.i"
sed 's/0xA7,0xC0/0xA7,0xC1/g' "$d/mini.i" >"$d/unsafe_opcode.i"
for shape in unsafe_alias unsafe_control unsafe_register unsafe_opcode; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe VIA XSTORE variant accepted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_VIA_XSTORE=PASS GNU_AS=1 MiniC_AS=1 byte_equal=1 reject=4"
