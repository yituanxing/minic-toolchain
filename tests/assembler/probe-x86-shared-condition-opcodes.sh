#!/usr/bin/env bash
# The same x86 condition-code mapping must drive CMOV and Jcc aliases;
# compare real machine-code bytes for every GNU GAS synonym and both widths.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as objcopy cmp; do
    command -v "$tool" >/dev/null || { echo "missing GNU tool: $tool" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/jcc.s" <<'ASM'
.text
.globl x86_condition_aliases
.type x86_condition_aliases,@function
x86_condition_aliases:
    jo .Lshort
    jno .Lshort
    jb .Lshort
    jc .Lshort
    jnae .Lshort
    jae .Lshort
    jnb .Lshort
    jnc .Lshort
    je .Lshort
    jz .Lshort
    jne .Lshort
    jnz .Lshort
    jbe .Lshort
    jna .Lshort
    ja .Lshort
    jnbe .Lshort
    js .Lshort
    jns .Lshort
    jp .Lshort
    jpe .Lshort
    jnp .Lshort
    jpo .Lshort
    jl .Lshort
    jnge .Lshort
    jge .Lshort
    jnl .Lshort
    jle .Lshort
    jng .Lshort
    jg .Lshort
    jnle .Lshort
.Lshort:
    cmovoq %rax,%r9
    cmovnoq %rax,%r9
    cmovpeq %rax,%r9
    cmovpoq %rax,%r9
    setpo %r9b
    setpe %r8b
    setnc %dil
    setnae %sil
    seto %ah
    setc %ch
    sete %dh
    setg %bh
    jz .Lfar
    .space 128,0x90
.Lfar:
    jmp .Ldone
    .space 2,0x90
.Ldone:
    ret
.size x86_condition_aliases,.-x86_condition_aliases
ASM
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/jcc.s"
as --64 -o "$work/gnu.o" "$work/jcc.s"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"
echo 'MINIAS_X86_SHARED_CONDITION_OPCODES=PASS GNU_bytes=exact Jcc_aliases=30 CMOV_aliases=4 SETcc_aliases=8 high_byte_no_REX=4 branch_short_near=both'
