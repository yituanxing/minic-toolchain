#!/usr/bin/env bash
# x86 Kbuild's 32-bit vDSO/real-mode objects require an i386 ELF32 REL ABI,
# not ELF64 with a .code32 directive.  Compare real GNU i386 bytes/reloctions.
set -Eeuo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
work=${WORK_DIR:-"$(mktemp -d)"}
mkdir -p "$work"
cat >"$work/i386.s" <<'ASM'
.text
.code32
.globl test32
test32:
    pushl %ebp
    movl %esp,%ebp
    popl %ebp
.data
    .long target_symbol+4
ASM
"$minias" --target=x86_64 -march=i386 -mabi=i386 \
    -o "$work/mini.o" "$work/i386.s"
as --32 -o "$work/gnu.o" "$work/i386.s"
readelf -h "$work/mini.o" | grep -q 'Class:.*ELF32'
readelf -h "$work/mini.o" | grep -q 'Machine:.*Intel 80386'
readelf -S "$work/mini.o" | grep -q '\.rel\.data'
readelf -r "$work/mini.o" | grep -q 'R_386_32'
objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"
objcopy -j .data -O binary "$work/mini.o" "$work/mini.data"
objcopy -j .data -O binary "$work/gnu.o" "$work/gnu.data"
cmp "$work/mini.data" "$work/gnu.data"

# Source-local mode and the object ABI remain independent. Do not synthesize
# 16-bit machine code by passing an unsupported directive to the ELF writer.
printf '.text\n.code16gcc\npushl %%ebp\n' >"$work/code16gcc.s"
if "$minias" --target=x86_64 -march=i386 -mabi=i386 \
    -o "$work/invalid.o" "$work/code16gcc.s" 2>"$work/code16.err"; then
    echo 'i386 code16gcc unvalidated instruction incorrectly accepted' >&2
    exit 1
fi
grep -q 'unsupported-x86-code16-operand' "$work/code16.err"
printf '.text\n.code32\nmovq %%rax, %%rbx\n' >"$work/code32bad.s"
if "$minias" --target=x86_64 -march=i386 -mabi=i386 \
    -o "$work/invalid.o" "$work/code32bad.s" 2>"$work/code32.err"; then
    echo 'i386 code32 allowed REX-only 64-bit operand' >&2
    exit 1
fi
grep -q 'unsupported-x86-code32-operand' "$work/code32.err"
echo 'MINIAS_X86_I386_OBJECT=PASS ELF32_EM386_REL R_386_32 GNU_bytes=equal code16gcc_fail_closed=1'
