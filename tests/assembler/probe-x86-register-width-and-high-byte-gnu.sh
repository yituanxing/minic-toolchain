#!/usr/bin/env bash
# Prove that x86 register-bank width decisions remain Target-owned:
# 16/32/64-bit CMOV, legacy high-byte MOVZX/MOVSX, and mandatory REX
# for the low-byte register bank must agree with GNU GAS byte-for-byte.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as objcopy cmp; do
    command -v "$tool" >/dev/null || { echo "missing GNU tool: $tool" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/valid.s" <<'ASM'
.text
.globl x86_width_high_byte
.type x86_width_high_byte,@function
x86_width_high_byte:
    movzbw %ah,%bx
    movsbw %ch,%ax
    movzbw %dh,%cx
    movsbw %bh,%dx
    movzbl %ah,%ecx
    movsbl %bh,%edi
    movzbw %al,%r8w
    movsbw %sil,%r9w
    movzbl %spl,%r12d
    movsbl %r15b,%r8d
    movzwq %r8w,%r9
    rolb %ah
    rorb %bh
    sarb $1,%ch
    shrb $3,%dh
    shlb %cl,%ah
    salb %cl,%bh
    sarb $-1,%al
    shlb $-128,%r8b
    shlb $-1,3(%r12)
    rorb $-2,%fs:7(%rbx)
    btw $1,%ax
    btsw $7,%r8w
    btrw $15,%bx
    btcw $31,%cx
    btcw $30,%fs:16(%r12)
    btrl $23,%r13d
    btsq $42,4(%rbp)
    cmovew %ax,%bx
    cmovnew %r8w,%r9w
    cmovlw %cx,%dx
    cmovge %r10w,%r11w
    cmovzl %ecx,%edx
    cmovnl %eax,%ecx
    cmovgq %r12,%r13
    cmovz %r8,%r9
    ret
.size x86_width_high_byte,.-x86_width_high_byte
ASM
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/valid.s"
as --64 -o "$work/gnu.o" "$work/valid.s"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
if ! cmp "$work/mini.bin" "$work/gnu.bin"; then
    echo 'x86 width/high-byte GNU machine-code differential failed:' >&2
    od -Ax -tx1 "$work/gnu.bin" >&2
    od -Ax -tx1 "$work/mini.bin" >&2
    exit 1
fi
for reject in \
    'movzbq %ah,%rax' \
    'movzbw %ah,%r8w' \
    'movsbq %dh,%r9' \
    'cmovew %eax,%ebx' \
    'cmoveq %ax,%bx' \
    'cmovlw %r10,%r11' \
    'btw $1,%r8d' \
    'btq $1,%r8w' \
    'btw $-1,%ax' \
    'btsq $-128,4(%rbp)'; do
    if printf '.text\n%s\n' "$reject" |
        "$MINIAS" --target=x86_64 -o "$work/reject.o" - >/dev/null 2>&1; then
        echo "x86 incorrectly accepted invalid register width: $reject" >&2
        exit 1
    fi
done
echo 'MINIAS_X86_REGISTER_WIDTH_HIGH_BYTE=PASS GNU_bytes=exact instructions=37 invalid_widths_rejected=10 CMOV_word=5 high_byte_no_REX=12 signed_shift_imm8=4 bit_imm16_32_64=7'
