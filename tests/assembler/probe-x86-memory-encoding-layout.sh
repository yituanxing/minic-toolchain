#!/usr/bin/env bash
# A single Target memory layout is shared by MC size planning and emission.
# Exercise displacement widths, no-base SIB, high GPR/INDEX REX, FS/GS,
# PC-relative and ABS32S symbols, short/long immediate suffixes and VEX3.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as objcopy cmp; do
    command -v "$tool" >/dev/null || { echo "missing GNU tool: $tool" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/memory.s" <<'ASM'
.text
.globl x86_memory_layout
.type x86_memory_layout,@function
x86_memory_layout:
    movq (%rsp),%rax
    movq (%r12),%r11
    movq (%rbp),%r8
    movl 127(%rdi),%eax
    movl 128(%rdi),%eax
    movw -128(%r15),%r10w
    movw -129(%r15),%r10w
    movq 8(%r12,%r13,4),%r9
    movq 32768(,%r12,8),%r11
    movq x86_external(%rip),%r10
    movl %fs:my_tls(%rip),%r8d
    movl absolute_data,%eax
    movl %r9d,%gs:16(%r12,%r13,2)
    movb %al,%fs:3(%rbp)
    movw %fs:24(,%r8,4),%r10w
    cmpb $1,absolute_data
    cmpq $256,8(%rbp)
    cmpq $5,16(%rbp)
    imulq $100,8(%rsp),%r13
    imulq $1000,x86_external(%rip),%r13
    setpo 8(%r12)
    setnc 16(%r13)
    shlq $1,24(%r14)
    sarb $3,32(%r15)
    shlxq %r12,8(%r13),%r14
    shrxq %r12,16(%r13),%r14
    sarxq %r12,24(%r13),%r14
    ret
.size x86_memory_layout,.-x86_memory_layout
ASM
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/memory.s"
as --64 -o "$work/gnu.o" "$work/memory.s"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"
if python3 -c 'import elftools' >/dev/null 2>&1; then
    if ! python3 tests/assembler/elf_semantic_compare.py --strict \
            "$work/gnu.o" "$work/mini.o" >"$work/semantic.json"; then
        echo 'GNU x86 memory encoding matched bytes, but strict ELF semantics diverged:' >&2
        python3 - "$work/semantic.json" <<'PY' >&2
import json
import sys

with open(sys.argv[1], encoding='utf-8') as report:
    verdict = json.load(report)
print('comparison dimensions:', json.dumps(verdict.get('dimensions', {}), indent=2))
print('strict differences:', json.dumps(verdict.get('differences', {}), indent=2))
PY
        exit 1
    fi
    semantic='PASS'
else
    semantic='NOT_TESTED:no_pyelftools'
fi
echo "MINIAS_X86_MEMORY_LAYOUT=PASS GNU_bytes=exact GNU_object_semantic=$semantic memory_forms=27 displacement=SIB_REX_RIP_ABS32S VEX3=3"
