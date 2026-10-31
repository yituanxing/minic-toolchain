#!/usr/bin/env bash
# One target-neutral balanced lexical scanner, four Target ISA grammars.
set -Eeuo pipefail
: "${MINIAS:?MiniAS required}"
: "${ARCH:?x86_64, aarch64, riscv or loongarch64 required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MC_LEXER_FAIL arch=$ARCH command=$BASH_COMMAND" >&2; test ! -f "$work/trace" || cat "$work/trace" >&2' ERR

cc -Iassembler/src -std=c11 -Wall -Wextra -Wpedantic -Werror \
    assembler/src/mc_lexer.c tests/assembler/mc-operand-lexer-contract.c \
    -o "$work/mc-lexer-contract"
"$work/mc-lexer-contract"

case "$ARCH" in
x86_64)
    mini=(--target=x86_64); gas=(as --64); copy=(objcopy)
    cat >"$work/operands.s" <<'SRC'
.text
.globl mixed_operands
mixed_operands:
    movq 8(%rax,%rbx,4),%r9
    leaq 16(%r12,%r13,8),%r10
    addq %rcx,-24(%rbp,%rsi,4)
    ret
SRC
    cat >"$work/negative.s" <<'SRC'
.text
    movq 8(%rax,%rbx],%r9
SRC
    ;;
aarch64)
    mini=(--target=aarch64)
    gas=(aarch64-linux-gnu-as); copy=(aarch64-linux-gnu-objcopy)
    cat >"$work/operands.s" <<'SRC'
.text
.globl mixed_operands
mixed_operands:
    ldr x0,[x1,#16]
    str x2,[sp,#24]
    add x3,x0,#7
    ret
SRC
    cat >"$work/negative.s" <<'SRC'
.text
    ldr x0,[x1,#16)
SRC
    ;;
riscv)
    mini=(-march=rv64i -mabi=lp64)
    gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
    copy=(riscv64-linux-gnu-objcopy)
    cat >"$work/operands.s" <<'SRC'
.text
.globl mixed_operands
mixed_operands:
    addi a0,a1,7
    lw a2,8(a0)
    sw a2,-4(sp)
    jalr zero,0(ra)
SRC
    cat >"$work/negative.s" <<'SRC'
.text
    lw a2,8(a0]
SRC
    ;;
loongarch64)
    mini=(--target=loongarch64)
    gas=(loongarch64-linux-gnu-as)
    copy=(loongarch64-linux-gnu-objcopy)
    cat >"$work/operands.s" <<'SRC'
.text
.globl mixed_operands
mixed_operands:
    addi.d $a0,$a1,7
    ld.d $a2,$a0,8
    st.d $a2,$sp,8
    jirl $zero,$ra,0
SRC
    cat >"$work/negative.s" <<'SRC'
.text
    ld.d $a2,($a0],8
SRC
    ;;
*) echo "unsupported ARCH=$ARCH" >&2;exit 2;;
esac

"$MINIAS" "${mini[@]}" -o "$work/mini.o" "$work/operands.s" 2>"$work/trace"
"${gas[@]}" -o "$work/gnu.o" "$work/operands.s"
"${copy[@]}" -O binary -j .text "$work/mini.o" "$work/mini.bin"
"${copy[@]}" -O binary -j .text "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"
if "$MINIAS" "${mini[@]}" -o "$work/bad.o" \
    "$work/negative.s" 2>"$work/negative.err";then
    echo "MiniAS accepted mismatched delimiters for $ARCH" >&2;exit 1
fi
test ! -e "$work/bad.o"
echo "MINIAS_MC_OPERAND_LEXER=PASS arch=$ARCH grouped_operands=GNU_exact invalid_rejected=1"
