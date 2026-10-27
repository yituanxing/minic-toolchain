#!/usr/bin/env bash
# Real GNU GAS -> same .s -> MiniAS. An error collector is never a success.
set -Eeuo pipefail
# Show exact failing command in strict GNU object oracle.
trap 'echo "MINIAS_GNU_ORACLE_FAIL line=$LINENO command=$BASH_COMMAND" >&2' ERR
: "$MINIAS"
: "$LOONGAS"
: "$LOONGCOPY"
: "$LOONGREADELF"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/x86.s" <<'ASM_X'
.text
.globl entry_x
.type entry_x,@function
entry_x:
    nop
    endbr64
    pushq %rbp
    movq %rsp,%rbp
    movl $42,%eax
    xorl %ecx,%ecx
    addq $16, %rsp
    subq $8, %rsp
    addl $4096, %r8d
    subl $-129, %r9d
    movl (%rdi), %eax
    movq %rax, -8(%rbp)
    movq -8(%rbp), %r9
    movl %r9d, 8(%r12)
    movl 8(%r12), %r10d
    movl %eax, (%rdi)
    movq (%rsp), %rax
    movq %rax, 128(%rsp)
    movl 0(%rbp), %eax
    addq %r8,%r9
    subq %rax,%rcx
    cmpq %rbp,%rsp
    testq %r8,%r9
    testl %ecx,%eax
    movq %rax, 32(%r14)
    cmpq %rax, 8(%r12)
    cmpq 8(%r12), %rax
    cmpl %r9d, -8(%rbp)
    testq %r8, 16(%rsp)
    testl 8(%r12), %eax
    movq %r8, 16(%r10,%r11,8)
    movl -128(%rbp,%rdx,2), %r8d
    movq 1024(%r12,%r13,4), %r9
    cmpq %r9, (%r14,%rbx,1)
    testq 8(%rsp,%r11,4), %r9
    xorl (%r12,%r8,2), %r10d
    movq (%r9,%r12,8), %rax
    addq %r9, 16(%r12)
    addl 8(%r12), %eax
    subq (%rdi), %r9
    xorl %r9d, 8(%r12)
    andq -16(%rbp), %rax
    orq %r8, 16(%rsp)
    popq %rbp
    ret
.size entry_x, .-entry_x
.globl unresolved_x86
unresolved_x86:
    call ext_target
    callq ext_target
    jmp ext_target
    je ext_target
    jne ext_target
    ja ext_target
    jl ext_target
    movl %eax, 8(%rbx,%r8,2)
    movl 8(%rbx,%r9,4), %eax
    movq ext_global(%rip), %rax
    movl %r8d, ext_global+8(%rip)
    cmpq ext_global(%rip), %r9
    movq %gs:40, %rax
    movq %gs:pcpu_hot, %rax
    movq %rax, %gs:pcpu_hot
    movl %fs:12, %eax
ASM_X
if ! "$MINIAS" --target=x86_64 -march=x86-64 -mabi=sysv -o "$work/x86-mini.o" "$work/x86.s" >"$work/x86.stdout" 2>"$work/x86.stderr"; then
    echo 'MINIAS_X86_PARSE_OR_ENCODE_FAIL' >&2
    cat "$work/x86.stdout" "$work/x86.stderr" >&2
    exit 1
fi
as --64 -o "$work/x86-gnu.o" "$work/x86.s"
objcopy -j .text -O binary "$work/x86-gnu.o" "$work/x86-gnu.bin"
objcopy -j .text -O binary "$work/x86-mini.o" "$work/x86-mini.bin"
cmp "$work/x86-mini.bin" "$work/x86-gnu.bin"
readelf -h "$work/x86-mini.o" | grep -q 'Advanced Micro Devices X86-64'
readelf -Wr "$work/x86-mini.o" >"$work/x86-rel.txt"
grep -q R_X86_64_PLT32 "$work/x86-rel.txt"
grep -q R_X86_64_PC32 "$work/x86-rel.txt"
readelf -Wr "$work/x86-gnu.o" >"$work/x86-gnu-rel.txt"
grep -q R_X86_64_PLT32 "$work/x86-gnu-rel.txt"
grep -q R_X86_64_PC32 "$work/x86-gnu-rel.txt"
grep -q R_X86_64_32S "$work/x86-mini-rel.txt"
# GNU and MiniAS must agree on every relocation type, offset, addend.
# GNU GAS and MiniELF may serialize RELA entries in different orders.
# Compare exact relocation offset, type, symbol, and addend after sorting.
readelf -Wr "$work/x86-mini.o" | awk '$3 ~ /^R_X86_64_/ {print $1,$3,$5,$6,$7}' |
    LC_ALL=C sort >"$work/x86-mini-kinds.txt"
readelf -Wr "$work/x86-gnu.o" | awk '$3 ~ /^R_X86_64_/ {print $1,$3,$5,$6,$7}' |
    LC_ALL=C sort >"$work/x86-gnu-kinds.txt"
diff -u "$work/x86-gnu-kinds.txt" "$work/x86-mini-kinds.txt"

cat > "$work/la.s" <<'ASM_LA'
.text
.globl entry_la
.type entry_la, @function
entry_la:
    nop
    add.d $a0, $a0, $a1
    addi.d $sp, $sp, -16
    st.d $ra, $sp, 8
    ld.d $ra, $sp, 8
    stptr.d $ra, $sp, -32768
    ldptr.d $a0, $sp, 32764
    stptr.w $a0, $sp, -4
    ldptr.w $a1, $sp, 12
    st.b $a0, $sp, 7
    ld.bu $a1, $sp, 7
    st.h $a2, $sp, 14
    ld.hu $a3, $sp, 14
    ld.wu $a4, $sp, 8
    slti $a0, $a1, -2048
    sltui $a1, $a0, 2047
    andi $a0, $a1, 4095
    ori $a0, $a1, 1024
    xori $a0, $a1, 255
    slli.w $a0, $a1, 31
    slli.d $a1, $a0, 63
    srli.w $a0, $a1, 17
    srli.d $a1, $a0, 42
    srai.w $a0, $a1, 31
    srai.d $a0, $a1, 63
    rotri.w $a0, $a1, 13
    rotri.d $a0, $a1, 35
    lu12i.w $a0, -524288
    lu32i.d $a0, 524287
    pcalau12i $a1, 1
    pcaddu12i $a1, -2
    maskeqz $a0, $a1, $a2
    masknez $a0, $a1, $a2
    sll.d $a0, $a1, $a2
    sra.w $a1, $a0, $a2
    mul.d $a0, $a1, $a2
    sub.d $a0, $a0, $a1
    and $a0, $a0, $a1
    or $a0, $a0, $a1
    xor $a0, $a0, $a1
    move $a0, $a1
    jr $r12
    jirl $a0, $a1, 4
    jirl $zero, $ra, -131072
    jirl $ra, $a0, 131068
    break 1
    break 32767
    syscall 0
    dbar 18
    ibar 16
    bstrpick.d $a0, $a1, 31, 0
    bstrpick.w $a1, $a0, 7, 0
    bstrins.d $a2, $a3, 63, 1
    bstrins.w $a3, $a2, 30, 2
    ret
.size entry_la, .-entry_la
ASM_LA
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d -o "$work/la-mini.o" "$work/la.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-gnu.o" "$work/la.s"
"$LOONGCOPY" -j .text -O binary "$work/la-gnu.o" "$work/la-gnu.bin"
"$LOONGCOPY" -j .text -O binary "$work/la-mini.o" "$work/la-mini.bin"
cmp "$work/la-gnu.bin" "$work/la-mini.bin"
"$LOONGREADELF" -h "$work/la-mini.o" > "$work/la-header.txt"
grep -q 'LoongArch' "$work/la-header.txt"
grep -q '0x43' "$work/la-header.txt"

for bad in 'movq %rax, (%eax)' \
           'movl 4294967296(%rax), %eax' \
           'movq %rax, (%rax,%rsp,2)' \
           'movq %rax, (%rax,%eax,4)' \
           'movq %rax, (%rax,%rcx,3)' \
           'movq %rax, (%rax,%rcx,16)' \
           'movq %rax, (%rax,%rcx,2,7)' \
           'movq ext_global(%rax), %r9' \
           'movq ext_global(%rip,%rax,2), %r9' \
           'movq %rax, ext_global(%rbp)' \
           'movq %gs:4294967296, %rax' \
           'movq %fs:pcpu_hot(%rax), %rax'; do
    printf '.text\n%s\n' "$bad" >"$work/invalid-mem.s"
    if "$MINIAS" --target=x86_64 -o "$work/bad-mem.o" \
         "$work/invalid-mem.s" >/dev/null 2>"$work/invalid.err"; then
        echo "MiniAS accepted unsupported x86 memory form: $bad" >&2
        exit 1
    fi
done
# Original LoongArch GCC / Linux symbolic code model, GNU object as oracle.
cat >"$work/la-reloc.s" <<'LA_RELOC'
.text
.globl reloc_entry
.type reloc_entry,@function
reloc_entry:
    pcalau12i $a0, %pc_hi20(_external_data)
    addi.d $a0, $a0, %pc_lo12(_external_data)
    ld.d $a1, $a0, %pc_lo12(_external_data)
    b _external_branch
    bl _external_call
    beqz $a0, _external_branch
    bnez $a1, _external_branch
    beq $a0, $a1, _external_branch
    bne $a0, $a1, _external_branch
    blt $a0, $a1, _external_branch
    bge $a0, $a1, _external_branch
    bltu $a0, $a1, _external_branch
    bgeu $a0, $a1, _external_branch
.size reloc_entry, .-reloc_entry
LA_RELOC
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/rel-mini.o" "$work/la-reloc.s"
"$LOONGAS" -mabi=lp64d -o "$work/rel-gnu.o" "$work/la-reloc.s"
"$LOONGCOPY" -j .text -O binary "$work/rel-mini.o" "$work/rel-mini.bin"
"$LOONGCOPY" -j .text -O binary "$work/rel-gnu.o" "$work/rel-gnu.bin"
if ! cmp -s "$work/rel-mini.bin" "$work/rel-gnu.bin"; then
    echo 'MINIAS_LARCH_BYTES_MISMATCH: exact per-word GNU and MiniAS output'
    echo 'GNU bytes:'; od -An -tx4 -w4 "$work/rel-gnu.bin"
    echo 'MiniAS bytes:'; od -An -tx4 -w4 "$work/rel-mini.bin"
    echo 'GNU relocations:'; "$LOONGREADELF" -Wr "$work/rel-gnu.o"
    echo 'MiniAS relocations:'; "$LOONGREADELF" -Wr "$work/rel-mini.o"
    exit 1
fi
"$LOONGREADELF" -Wr "$work/rel-mini.o" >"$work/rel-mini.txt"
"$LOONGREADELF" -Wr "$work/rel-gnu.o" >"$work/rel-gnu.txt"
for kind in R_LARCH_PCALA_HI20 R_LARCH_PCALA_LO12 R_LARCH_B16 R_LARCH_B21 R_LARCH_B26; do
    grep -q "$kind" "$work/rel-mini.txt"
    grep -q "$kind" "$work/rel-gnu.txt"
done
echo 'MINIAS_LARCH_RELOC_ORACLE=PASS text_bytes=equal required_ELF_reloc_families=5'
# Same-section forward/backward LoongArch branches must use the
# original RV-proven symbol-layout resolver rather than producing a redundant
# external ELF RELA or guessing a GNU relaxation.
cat >"$work/la-local.s" <<'LA_LOCAL'
.text
.globl local_entry
.type local_entry,@function
local_entry:
.Lback:
    nop
    beqz $a0, .Lforward
    b .Lforward
    beq $a0, $a1, .Lback
.Lforward:
    bnez $a1, .Lback
    ret
.size local_entry, .-local_entry
LA_LOCAL
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/la-local-mini.o" "$work/la-local.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-local-gnu.o" "$work/la-local.s"
"$LOONGCOPY" -j .text -O binary "$work/la-local-mini.o" "$work/la-local-mini.bin"
"$LOONGCOPY" -j .text -O binary "$work/la-local-gnu.o" "$work/la-local-gnu.bin"
cmp "$work/la-local-mini.bin" "$work/la-local-gnu.bin"
# The GNU assembler may retain R_LARCH_MARK_LA/relax bookkeeping; the
# machine-level code and defined local labels must still be identical.
echo 'MINIAS_LARCH_LOCAL_BRANCH_ORACLE=PASS same_section_forward_backward_bytes=equal'

for bad in 'ldptr.d $a0, $sp, 3' \
           'stptr.w $a0, $sp, 32768' \
           'ldptr.d $a1, $sp, -32772' \
           'andi $a0, $a1, -1' \
           'ori $a0, $a1, 4096' \
           'slli.w $a0, $a1, 32' \
           'srli.d $a0, $a1, 64' \
           'lu12i.w $a0, 524288' \
           'pcalau12i $a0, -524289' \
           'jirl $a0, $a1, 3' \
           'jirl $a0, $a1, 131072' \
           'jirl $a0, $a1, -131076' \
           'break -1' \
           'dbar 32768' \
           'bstrpick.w $a0, $a1, 32, 1' \
           'bstrpick.d $a0, $a1, 2, 3'; do
    printf '.text\n%s\n' "$bad" >"$work/invalid-la-ptr.s"
    if "$MINIAS" --target=loongarch64 -o "$work/bad-ptr.o" \
         "$work/invalid-la-ptr.s" >/dev/null 2>"$work/invalid.err"; then
        echo "MiniAS accepted invalid LoongArch pointer displacement: $bad" >&2
        exit 1
    fi
done
for target in x86_64 loongarch64; do
    if [ "$target" = x86_64 ]; then invalid='movq %rax, %eax'
    else invalid='addi.d $a0, $a0, 4096'; fi
    printf '.text\n%s\n' "$invalid" > "$work/invalid.s"
    if "$MINIAS" --target="$target" -o "$work/invalid.o" "$work/invalid.s" \
           >/dev/null 2>"$work/invalid.err"; then
        echo "invalid instruction accepted target=$target" >&2
        exit 1
    fi
    rm -f "$work/invalid.o"
done
# Unsupported GOT/TLS relocation modifiers must NOT accidentally be treated
# as numeric PCALA relocations.
printf '.text\npcalau12i $a0, %%got_pc_hi20(unknown)\n' > "$work/unimplemented.s"
if "$MINIAS" --target=loongarch64 -o "$work/invalid.o" "$work/unimplemented.s" \
        >/dev/null 2>"$work/invalid.err"; then
    echo 'Unimplemented GOT/fixup silently accepted' >&2
    exit 1
fi
echo 'MINIAS_TWO_TARGET_ORACLE=PASS x86_GNU_bytes=equal loong_GNU_bytes=equal ELF_machine=correct lp64d_v1=0x43 CET_endbr64=1 ALU_imm8_imm32=1 x86_ModRM_SIB_disp=1 x86_test_cmp_alu_memory=1 x86_scaled_index_SIB=1 LA_jr=1 LA_scaled_ptr14=1 LA_imm12_shift_upper20=1 invalid=reject'
