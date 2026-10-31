#!/usr/bin/env bash
# GNU GAS oracle for the i386 real-mode path used by Linux 6.6.143.
#
# Instruction families are drawn from:
#   arch/x86/boot/header.S          (.code16 setup, 16/32-bit calls)
#   arch/x86/boot/pmjump.S          (CR0 and .code16 -> .code32)
#   arch/x86/boot/bioscall.S        (pushal/popal, pushfl/popfl, REP)
#   arch/x86/boot/copy.S            (REP MOVS/STOS, width-mixed ALU)
#   arch/x86/realmode/rm/trampoline_32.S (LGDT/LIDT/LMSW/indirect jump)
#   arch/x86/realmode/rm/wakeup_asm.S    (16-bit/32-bit mode transitions)
#   arch/x86/realmode/rm/reboot.S        (.code32 and .code16 coexistence)
#   arch/x86/realmode/rm/trampoline_64.S (LOCK BTS/Jcc/PAUSE)
# The snippets below deliberately avoid requiring Linux CPP macros or linker
# scripts; this checks actual GNU encodings/relocations, not boot success.
#
# Usage: MINIAS=/path/to/minic-as bash tests/assembler/probe-x86-linux-code16-gnu.sh
# Optional: MINIAS_X86_ARGS='--target=x86_64 -march=i386 -mabi=i386'
# All cases run even after a failure, so one invocation exposes every known
# instruction family that still differs from GNU GAS.
set -Eeuo pipefail
export LC_ALL=C

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
args=(--target=x86_64 -march=i386 -mabi=i386)
if [[ -n ${MINIAS_X86_ARGS:-} ]]; then
    read -r -a args <<<"$MINIAS_X86_ARGS"
fi
work=$(mktemp -d)
trap 'if [[ ${MINIAS_X86_BOOT_KEEP_WORK:-0} == 1 ]]; then printf "MINIAS_X86_BOOT_ORACLE_WORK=%s\n" "$work"; else rm -r -- "$work"; fi' EXIT

for program in as readelf objcopy objdump cmp ld awk sort; do
    if ! command -v "$program" >/dev/null 2>&1; then
        printf 'MINIAS_X86_BOOT_ORACLE=ERROR missing=%s\n' "$program" >&2
        exit 2
    fi
done
if [[ ! -x "$minias" ]]; then
    printf 'MINIAS_X86_BOOT_ORACLE=ERROR missing_minias=%s\n' "$minias" >&2
    exit 2
fi

cat >"$work/code16_setup.s" <<'ASM'
    .text
    .code16
    .globl boot16
boot16:
    cli
    cld
    pushw %ds
    popw %ds
    pushw $6
    popw %bx
    movw %ds, %ax
    movw %ax, %es
    movw %ax, %ss
    movw $0xfffc, %dx
    movl $0x12345678, %eax
    pushl %eax
    popl %ecx
    calll boot16_sub
    jmp boot16_done
boot16_sub:
    retl
boot16_done:
    hlt
ASM

# These are the exact b/w/l binary forms whose proven legacy-mode legality
# lives alongside their encoder descriptors.  Cover every allowed width,
# including narrower subsets of ADC/SBB/SUB/XOR, in one GNU byte oracle.
cat >"$work/code16_binary_policy.s" <<'ASM'
    .text
    .code16
binary_policy:
    movb %al, %bl
    movw %ax, %bx
    movl %eax, %ebx
    addb %al, %bl
    addw %ax, %bx
    addl %eax, %ebx
    orb %al, %bl
    orw %ax, %bx
    orl %eax, %ebx
    adcl %eax, %ebx
    sbbl %eax, %ebx
    andb %al, %bl
    andw %ax, %bx
    andl %eax, %ebx
    subw %ax, %bx
    subl %eax, %ebx
    xorw %ax, %bx
    xorl %eax, %ebx
    cmpb %al, %bl
    cmpw %ax, %bx
    cmpl %eax, %ebx
    testb %al, %bl
    testw %ax, %bx
    testl %eax, %ebx
ASM

cat >"$work/code16gcc_stack.s" <<'ASM'
    .text
    .code16gcc
    .globl boot_gcc
boot_gcc:
    push %ebp
    movl %esp, %ebp
    push %ebx
    movl 8(%ebp), %eax
    movl 4(%esp), %ecx
    call boot_gcc_child
    pop %ebx
    leave
    ret
boot_gcc_child:
    ret
ASM

# A deliberately minimal baseline keeps the GNU .code16gcc operand-size and
# address-size prefix rules independently observable while a larger GCC
# prologue (above) exercises unsuffixed push/leave/ret behavior.
cat >"$work/code16gcc_explicit.s" <<'ASM'
    .text
    .code16gcc
    .globl boot_gcc_explicit
boot_gcc_explicit:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %eax
    popl %ebp
    retl
ASM

cat >"$work/code16_width_baseline.s" <<'ASM'
    .text
    .code16
    .globl boot_width16
boot_width16:
    pushl %eax
    movl %ebx, %eax
    popl %edx
    retl
ASM

cat >"$work/code16_header.s" <<'ASM'
    .text
    .code16
    .globl boot_header
boot_header:
    movw %ds, %ax
    movw %ax, %es
    cld
    movw %ss, %dx
    cmpw %ax, %dx
    testb $0x80, boot_loadflags
    jz 1f
    movw boot_heap_end, %dx
1:
    addw $0x400, %dx
    xorw %dx, %dx
    andw $-4, %dx
    jnz 2f
    movw $0xfffc, %dx
2:
    movw %ax, %ss
    movzwl %dx, %esp
    sti
    pushw %ds
    pushw $3f
    lretw
3:
    cmpl $0x5a5aaa55, boot_signature
    xorw %ax, %ax
    retl
boot_loadflags:
    .byte 0
boot_heap_end:
    .word 0
boot_signature:
    .long 0
ASM

cat >"$work/code16_bioscall.s" <<'ASM'
    .text
    .code16
    .globl boot_bioscall
boot_bioscall:
    pushfl
    pushw %fs
    pushw %gs
    pushal
    subw $44, %sp
    movw %dx, %si
    movw %sp, %di
    movw $11, %cx
    rep; movsl
    popal
    popw %gs
    popw %fs
    popfl
    cld
    movzwl %sp, %esp
    movw 68(%esp), %di
    andw %di, %di
    jz 1f
    movw %sp, %si
    rep; movsl
1:
    addw $44, %sp
    retl
ASM

cat >"$work/code16_copy.s" <<'ASM'
    .text
    .code16
    .globl boot_copy
boot_copy:
    pushw %si
    pushw %di
    movw %ax, %di
    movw %dx, %si
    pushw %cx
    shrw $2, %cx
    rep; movsl
    popw %cx
    andw $3, %cx
    rep; movsb
    popw %di
    popw %si
    retl
boot_fill:
    movzbl %dl, %eax
    imull $0x01010101, %eax
    pushw %cx
    shrw $2, %cx
    rep; stosl
    popw %cx
    andw $3, %cx
    rep; stosb
    retl
ASM

cat >"$work/code16_trampoline.s" <<'ASM'
    .text
    .code16
    .globl trampoline16
trampoline16:
    wbinvd
    movw %cs, %ax
    movw %ax, %ds
    cli
    lidtl real_idt
    lgdtl real_gdt
    movl %cr0, %eax
    movl %eax, %cr0
    lmsw %dx
    ljmpl $0x10, $protected32
real_idt:
    .word 0
    .long 0
real_gdt:
    .word 0x17
    .long 0x8000
    .code32
protected32:
    jmp *%eax
ASM

cat >"$work/code16_wakeup_reboot.s" <<'ASM'
    .text
    .code16
    .globl wakeup_reboot16
wakeup_reboot16:
    cli
    cld
    movw $16, %cx
    lgdtl %cs:wakeup_gdt
    movl %cr0, %eax
    orb $1, %al
    movl %eax, %cr0
    ljmpw $8, $1f
1:
    movw %cx, %ds
    movw %cx, %es
    movw %cx, %ss
    movl $0x7000, %esp
    lidtl wakeup_idt
    pushl $0
    popfl
    movl %ecx, %cr3
    testl $0x60000000, %edx
    jz 2f
    wbinvd
2:
    movw $0x1000, %ax
    movw %ax, %ss
    movw $0xf000, %sp
    int $0x15
    ljmpw $0xf000, $0xfff0
wakeup_idt:
    .word 0
    .long 0
wakeup_gdt:
    .word 0x17
    .long 0x8000
ASM

cat >"$work/code16_trampoline_atomic.s" <<'ASM'
    .text
    .code16
    .globl trampoline_lock16
trampoline_lock16:
    lock btsl $0, tr_lock
    jnc 1f
    pause
    jmp trampoline_lock16
1:
    testl %eax, %eax
    jnz 2f
    movl $0x7000, %esp
    call verify_cpu
2:
    hlt
tr_lock:
    .long 0
verify_cpu:
    ret
ASM

cat >"$work/code16_address_modes.s" <<'ASM'
    .text
    .code16
    .globl address_modes16
address_modes16:
    movw (%bx,%si), %ax
    movw 4(%bp,%di), %cx
    movb %fs:8(%bx), %al
    movl 4(%esp), %edx
    movl 8(%ebp), %esi
    movl 0x1234, %eax
    .code16gcc
    movw (%bx,%di), %dx
    movl (%esi,%edi,4), %eax
    ret
ASM

cat >"$work/code16_branch_relax.s" <<'ASM'
    .text
    .code16
    .globl branch_relax16
branch_relax16:
    testw %ax, %ax
    jz far16
    .fill 180, 1, 0x90
far16:
    call near16
    jmp done16
    .fill 140, 1, 0x90
near16:
    ret
done16:
    .code16gcc
    testl %eax, %eax
    jnz far_gcc
    .fill 190, 1, 0x90
far_gcc:
    ret
ASM

cat >"$work/code32_vdso.s" <<'ASM'
    .text
    .code32
    .globl vdso32
vdso32:
    pushl %ecx
    pushl %ebp
    movl %esp, %ebp
    movl 4(%esp), %eax
    movl %eax, (%esp)
    popl %eax
    popl %ebp
    ret
ASM

# Additional Kbuild blockers encountered in 16-bit real-mode and 32-bit
# VDSO/entry assembly. These have existing typed encoders; verify that the
# legacy-mode profile admits them with GNU's operand-size prefix semantics.
cat >"$work/code16_linux_flags_bit.s" <<'ASM'
    .text
    .code16
    .globl linux_flags_bit16
linux_flags_bit16:
    pushf
    popf
    btl %eax, (%bx)
    btrl %eax, (%bx)
    shldl $3, %eax, %ebx
    lcallw $0xc000, $3
    ret
ASM

cat >"$work/code32_linux_syscall_time.s" <<'ASM'
    .text
    .code32
    .globl linux_entry32
linux_entry32:
    sysenter
    rdtsc
    shrl $1, %eax
    ret
ASM

cat >"$work/code16_linux_realmode_gcc.s" <<'ASM'
    .text
    .code16gcc
    .globl linux_realmode_gcc
linux_realmode_gcc:
    cpuid
    sall $1, %eax
    incl %eax
    negl %eax
    outb %al, %dx
    setne %al
    jb 1f
    ja 1f
    jnb 1f
    jbe 1f
1:
    ret
ASM

# Linux realmode's GCC-generated instruction stream mixes unsuffixed
# register-width inference, explicit -l, REP string width, and index-only
# addressing in .code16gcc.  Their 66/67 ordering is part of the ISA, not
# a mnemonic string exception in the final encoder.
cat >"$work/code16_typed_width_index.s" <<'ASM'
    .text
    .code16gcc
    .globl linux_typed_width
linux_typed_width:
    leal 4(%esi), %eax
    subl $12, %esp
    incl 4(%esp)
    btr $15, %eax
    btrl $2, %edx
    rep; stosl
    rep; movsl
    movl 12(,%eax,4), %edx
    movl 8(,%ecx,4), %eax
    .code16
    movw %ax, %dx
    retl
ASM

cat >"$work/code32_linux_fences.s" <<'ASM'
    .text
    .code32
    .globl linux_fences32
linux_fences32:
    lfence
    rdtscp
    syscall
    cmovne %eax, %ebx
    ret
ASM

cat >"$work/code32_reboot.s" <<'ASM'
    .text
    .code32
    .globl reboot32
reboot32:
    movl $0x10, %eax
    movl reboot_gdt, %eax
    movl %eax, reboot_gdt
    movl %eax, %ds
    lgdtl reboot_gdt
    movl %cr0, %eax
    andl $0x7fffffff, %eax
    movl %eax, %cr0
    xorl %eax, %eax
    xorl %edx, %edx
    movl $0xc0000080, %ecx
    rdmsr
    wrmsr
    lidtl reboot_idt
    movl %ecx, %ss
    ljmpw $0x8, $reboot16
    ljmpl $0x8, $reboot16
reboot_idt:
    .word 0
    .long 0
reboot_gdt:
    .word 0x17
    .long 0x8000
    .code16
reboot16:
    xorl %ecx, %ecx
    movl %cr0, %edx
    andl $0x11, %edx
    orl $0x60000000, %edx
    movl %edx, %cr0
ASM

# Exactly the eleven instruction/operand families reported by the first
# real Linux 6.6.143 bzImage GCC -> MiniAS Kbuild pass. Keep one GNU oracle
# for all blockers, including the deferred symbol differences in compressed
# head_64.S and efi_mixed.S. None is a fake ELF-only acceptance criterion.
cat >"$work/code16_linux_gcc_boot_blockers.s" <<'ASM'
    .text
    .code16gcc
    .globl gcc_boot_blockers
gcc_boot_blockers:
    fninit
    cltd
    notl %eax
    orw $1, %ax
    shll $2, %eax
    addb $1, %al
    repe; movsb
    incb %al
    lcallw *boot_params+520
    ret
    .data
boot_params:
    .space 528
ASM

cat >"$work/code32_linux_gcc_symbol_difference.s" <<'ASM'
    .text
    .code32
    .globl startup_32
startup_32:
    leal ((gdt) - startup_32)(%ebp), %eax
    .zero 176
gdt:
    nop
1:
    leal (efi32_boot_args - 1b)(%ecx), %ebx
    .zero 137
efi32_boot_args:
    nop
ASM

# Second real Linux 6.6.143 GNU Kbuild frontier after the preceding eleven
# failures were fixed: all eight *new* opcode families must be enabled in
# their actual 16/32-bit CPU modes, not accidentally only in long mode.
# Both register and memory forms are checked where the machine ISA permits.
cat >"$work/code16_linux_second_frontier.s" <<'ASM'
    .text
    .code16gcc
    .globl second_frontier16
second_frontier16:
    fnstsw %ax
    fnstsw -2(%ebp)
    idivl %ecx
    idivl 8(%ebp)
    movswl %ax, %eax
    movswl 4(%ebp), %edx
    repe; cmpsb
    rep; stosw
    ret
ASM

cat >"$work/code32_linux_second_frontier.s" <<'ASM'
    .text
    .code32
    .globl second_frontier32
second_frontier32:
    ltr %ax
    ltr 2(%ebp)
    lgdt (%esp)
    lgdt 6(%ebp)
    lldt %cx
    lldt 4(%ebx)
    ret
ASM

# Linux compressed/head_64.S and efi_mixed.S use .code32 within ELF64.
# The CPU decoding mode must be a lexical property, not inferred from the
# relocatable object's ELF class.
cat >"$work/elf64_code32_linux_second_frontier.s" <<'ASM'
    .text
    .code32
    .globl mixed_efi32
mixed_efi32:
    ltr %ax
    lgdt (%ebx)
    lldt %cx
    .code64
    ret
ASM

# The next genuine Kbuild frontier (cpuflags, pmjump, compressed EFI/head64)
# exercises already-owned FPU/system/indirect/return encoders and, crucially,
# deferred section-anchor differences in both memory displacement and imm32.
cat >"$work/code16_linux_third_frontier.s" <<'ASM'
    .text
    .code16gcc
    fnstcw (%esp)
    fnstcw -4(%ebp)
    .balign 16, 0x90
    ret
ASM

cat >"$work/code32_linux_third_frontier.s" <<'ASM'
    .text
    .code32
boot32:
    jmpl *%eax
    jmpl *4(%ebx)
    btcl $12, %eax
    lretl
1:
    sgdtl (gdt - 1b)(%ebx)
    sidtl (idt - 1b)(%ebx)
    .balign 16, 0x90
    ret
gdt:
    .space 8
idt:
    .space 8
ASM

cat >"$work/elf64_linux_third_frontier_difference.s" <<'ASM'
    .text
    .code32
boot32:
    call 1f
1:
    pop %ebx
    subl $boot32 - 1b, %ebp
    movl $external_end - 1b, 608(%esi)
    subl $external_start - 1b, 608(%esi)
    sgdtl (gdt - 1b)(%ebx)
    sidtl (idt - 1b)(%ebx)
    .space 160
2:
    subl $boot32 - 2b, %ebp
    .code64
startup_32:
    leaq ((_bss-8) - startup_32)(%rbx), %rdi
    leaq ((gdt) - startup_32)(%rbx), %rsi
    .balign 16, 0x90
    ret
gdt:
    .space 8
idt:
    .space 8
_bss:
    .space 8
ASM

# GNU chooses branch relocation flavor from the lexical CPU mode, symbol
# binding/section, and explicit @PLT request.  ELF64 .code32 external CALL
# deliberately differs from ELF64 .code64 external CALL, while local
# cross-section branches remain PC32 in either mode. Compare full tuples.
cat >"$work/elf64_branch_modes_plt.s" <<'ASM'
    .text
    .globl branch_probe
    .type branch_probe,@function
branch_probe:
    jmp undefined_jump
    je undefined_cond
    call undefined_call
    jmp local_other_section
    jne local_other_section
    call local_other_section
    jmp explicit_plt@PLT
    call explicit_plt@PLT
    jne explicit_cond_plt@PLT
    jmp undefined_function
    jmp global_other_section
    call global_other_section
    ret
    .code32
branch_probe32:
    jmp undefined_jump32
    je undefined_cond32
    call undefined_call32
    jmp local_other_section
    jmp explicit_plt32@PLT
    jne explicit_cond_plt32@PLT
    .code64
    .type undefined_function,@function
    .section .data,"aw",@progbits
local_other_section:
    .long 0
    .globl global_other_section
global_other_section:
    .long 0
ASM

cat >"$work/elf64_conditional_plt.s" <<'ASM'
    .text
    .code64
    jne ext_cond_plt@PLT
    ja ext_above_plt@PLT
    .code32
    jne ext_cond32_plt@PLT
    ja ext_above32_plt@PLT
    .code64
    ret
ASM

cat >"$work/invalid_code16_ltr_64reg.s" <<'ASM'
    .text
    .code16
    ltr %r8w
ASM

cat >"$work/invalid_code32_lgdt_register.s" <<'ASM'
    .text
    .code32
    lgdt %eax
ASM

cat >"$work/mixed_modes.s" <<'ASM'
    .text
    .code16
    .globl mixed_mode16
mixed_mode16:
    pushw %ax
    movl %edx, %esi
    calll mode32_start
    popw %ax
    .code32
mode32_start:
    pushl %eax
    movl %ebx, %ecx
    popl %eax
    .code16gcc
    push %ebp
    movl %esp, %ebp
    leave
    ret
ASM

cat >"$work/external_rel.s" <<'ASM'
    .text
    .code16
    .globl realmode_reloc
realmode_reloc:
    calll external_func
    movl $external_data, %eax
    .word external_data
    .long external_data+4
    .code32
    call external_func
    .long external_data
ASM

# The real-mode Linux boot code uses 16-bit immediate fields in machine
# instructions, not just literal `.word` data. In ELF32 these require REL
# R_386_16 fixups (with implicit addends in the instruction bytes).
# Unsuffixed .code16 CALL/JMP/Jcc to an unresolved external use PC16, not the
# 32-bit R_386_PC32 emitted by `.code16 calll`.
cat >"$work/external_rel16.s" <<'ASM'
    .text
    .code16
    .globl realmode_reloc
realmode_reloc:
    movw $external_data, (%bx)
    movw $external_data, %ax
    addw $external_data, %ax
    cmpw $external_data, %ax
    call external_func
    jmp external_func
    jz external_func
    ret
ASM

# These are source-level errors in GNU AS for --32. MiniAS must also reject
# them instead of silently encoding a 64-bit-only register or instruction.
cat >"$work/invalid_code16_movq.s" <<'ASM'
    .text
    .code16
    movq %rax, %rbx
ASM
cat >"$work/invalid_code32_pushq.s" <<'ASM'
    .text
    .code32
    pushq %rax
ASM
cat >"$work/invalid_code16gcc_r8.s" <<'ASM'
    .text
    .code16gcc
    movl %r8d, %eax
ASM

failures=0
passed=0
log_fail() {
    printf 'MINIAS_X86_BOOT_ORACLE_CASE=%s FAIL stage=%s\n' "$1" "$2" >&2
    if [[ -f "$work/$1.mini.err" ]]; then
        sed -n '1,8p' "$work/$1.mini.err" >&2
    fi
    failures=$((failures+1))
}
log_pass() {
    printf 'MINIAS_X86_BOOT_ORACLE_CASE=%s PASS\n' "$1"
    passed=$((passed+1))
}

# Normalize the *semantic* reloc identity (offset/type/external symbol); ELF
# symbol-table ordering, relocation INFO word, and section offsets may differ.
normalize_relocations() {
    readelf -Wr "$1" | awk '
        /^[[:space:]]*[0-9a-fA-F]+[[:space:]]+[0-9a-fA-F]+[[:space:]]+R_(386_|X86_64_)/ {
            print $1 ":" $3 ":" $5 ":" $6 ":" $7
        }
    ' | sort
}

run_positive() {
    local name=$1
    local linked=${2:-no}
    if ! as --32 -o "$work/$name.gnu.o" "$work/$name.s" \
            2>"$work/$name.gnu.err"; then
        log_fail "$name" 'gnu_oracle_fixture_invalid'
        sed -n '1,12p' "$work/$name.gnu.err" >&2
        return
    fi
    if [[ $name == external_rel16 ]]; then
        # Require the GNU reference to exercise both real 16-bit relocations;
        # otherwise a changed GAS interpretation would silently weaken this.
        if ! readelf -Wr "$work/$name.gnu.o" | grep -q 'R_386_16' || \
           ! readelf -Wr "$work/$name.gnu.o" | grep -q 'R_386_PC16'; then
            log_fail "$name" 'gnu_reference_missing_386_16_or_pc16'
            return
        fi
        if ! ld -m elf_i386 -e realmode_reloc -Ttext 0x2000 \
            --defsym=external_func=0x2200 --defsym=external_data=0x1234 \
            -o "$work/$name.gnu.reference.elf" "$work/$name.gnu.o" \
            2>"$work/$name.gnu.reference.err"; then
            log_fail "$name" 'gnu_reference_16bit_link_failed'
            sed -n '1,8p' "$work/$name.gnu.reference.err" >&2
            return
        fi
        if ld -m elf_i386 -e realmode_reloc -Ttext 0x2000 \
            --defsym=external_func=0x2200 --defsym=external_data=0x10000 \
            -o "$work/$name.gnu.reference.overflow.elf" "$work/$name.gnu.o" \
            2>"$work/$name.gnu.reference.overflow.err" || \
           ! grep -q 'R_386_16' "$work/$name.gnu.reference.overflow.err"; then
            log_fail "$name" 'gnu_reference_16bit_overflow_not_rejected'
            sed -n '1,8p' "$work/$name.gnu.reference.overflow.err" >&2
            return
        fi
    fi
    if ! "$minias" "${args[@]}" -o "$work/$name.mini.o" "$work/$name.s" \
            2>"$work/$name.mini.err"; then
        log_fail "$name" 'minias_assembly'
        return
    fi
    if ! readelf -h "$work/$name.mini.o" | grep -q 'Class:.*ELF32' || \
       ! readelf -h "$work/$name.mini.o" | grep -q 'Machine:.*Intel 80386'; then
        log_fail "$name" 'elf32_em386'
        return
    fi
    if ! objcopy -j .text -O binary "$work/$name.gnu.o" "$work/$name.gnu.bin" || \
       ! objcopy -j .text -O binary "$work/$name.mini.o" "$work/$name.mini.bin"; then
        log_fail "$name" 'text_extraction'
        return
    fi
    if ! cmp -s "$work/$name.gnu.bin" "$work/$name.mini.bin"; then
        printf '  GNU vs MiniAS section .text bytes:\n' >&2
        printf '  First differing byte positions (decimal position, GNU octal, MiniAS octal):\n' >&2
        cmp -l "$work/$name.gnu.bin" "$work/$name.mini.bin" | \
            sed -n '1,12p' >&2 || true
        objdump -d -M intel "$work/$name.gnu.o" | tail -n 16 >&2
        objdump -d -M intel "$work/$name.mini.o" | tail -n 16 >&2
        log_fail "$name" 'text_bytes'
        return
    fi
    normalize_relocations "$work/$name.gnu.o" >"$work/$name.gnu.rel"
    normalize_relocations "$work/$name.mini.o" >"$work/$name.mini.rel"
    if ! cmp -s "$work/$name.gnu.rel" "$work/$name.mini.rel"; then
        printf '  GNU relocations:\n' >&2
        sed -n '1,20p' "$work/$name.gnu.rel" >&2
        printf '  MiniAS relocations:\n' >&2
        sed -n '1,20p' "$work/$name.mini.rel" >&2
        log_fail "$name" 'relocation_semantics'
        return
    fi
    if [[ $linked == yes ]]; then
        # The same GNU linker completes both objects with identical externs.
        # This verifies real R_386 fixup consumption; object similarity alone
        # cannot prove the ABI is accepted by a production linker.
        local obj
        for obj in gnu mini; do
            if ! ld -m elf_i386 -e realmode_reloc -Ttext 0x2000 \
                --defsym=external_func=0x2200 --defsym=external_data=0x1234 \
                -o "$work/$name.$obj.elf" "$work/$name.$obj.o" \
                2>"$work/$name.$obj.ld.err"; then
                log_fail "$name" "gnu_ld_$obj"
                sed -n '1,10p' "$work/$name.$obj.ld.err" >&2
                return
            fi
            objcopy -j .text -O binary "$work/$name.$obj.elf" \
                "$work/$name.$obj.linked.bin"
        done
        if ! cmp -s "$work/$name.gnu.linked.bin" "$work/$name.mini.linked.bin"; then
            log_fail "$name" 'gnu_ld_linked_text_bytes'
            return
        fi
        if [[ $name == external_rel16 ]]; then
            # An R_386_16 symbol at 0x10000 cannot be represented. GNU ld
            # must reject *both* objects; an incorrect R_386_32 widening in
            # MiniAS would incorrectly link successfully and be caught here.
            for obj in gnu mini; do
                if ld -m elf_i386 -e realmode_reloc -Ttext 0x2000 \
                    --defsym=external_func=0x2200 \
                    --defsym=external_data=0x10000 \
                    -o "$work/$name.$obj.overflow.elf" "$work/$name.$obj.o" \
                    2>"$work/$name.$obj.overflow.err"; then
                    log_fail "$name" "gnu_ld_overflow_accepted_$obj"
                    return
                fi
                if ! grep -q 'R_386_16' "$work/$name.$obj.overflow.err"; then
                    log_fail "$name" "gnu_ld_overflow_wrong_relocation_$obj"
                    sed -n '1,8p' "$work/$name.$obj.overflow.err" >&2
                    return
                fi
            done
        fi
    fi
    log_pass "$name"
}

run_negative() {
    local name=$1
    if as --32 -o "$work/$name.gnu.o" "$work/$name.s" \
            2>"$work/$name.gnu.err"; then
        log_fail "$name" 'gnu_oracle_fixture_unexpectedly_legal'
        return
    fi
    if "$minias" "${args[@]}" -o "$work/$name.mini.o" "$work/$name.s" \
            2>"$work/$name.mini.err"; then
        log_fail "$name" 'minias_accepted_illegal_instruction'
        return
    fi
    log_pass "$name"
}

run_mixed_elf64() {
    local name=$1
    if ! as --64 -o "$work/$name.gnu.o" "$work/$name.s" \
            2>"$work/$name.gnu.err"; then
        log_fail "$name" gnu_elf64_oracle_fixture_invalid
        sed -n '1,8p' "$work/$name.gnu.err" >&2
        return
    fi
    if ! "$minias" --target=x86_64 -o "$work/$name.mini.o" \
            "$work/$name.s" 2>"$work/$name.mini.err"; then
        log_fail "$name" minias_elf64_assembly
        return
    fi
    for obj in gnu mini; do
        objcopy -j .text -O binary "$work/$name.$obj.o" "$work/$name.$obj.bin"
    done
    if ! cmp -s "$work/$name.gnu.bin" "$work/$name.mini.bin"; then
        log_fail "$name" elf64_code32_text_bytes
        cmp -l "$work/$name.gnu.bin" "$work/$name.mini.bin" | \
            sed -n '1,12p' >&2 || true
        return
    fi
    normalize_relocations "$work/$name.gnu.o" >"$work/$name.gnu.rel"
    normalize_relocations "$work/$name.mini.o" >"$work/$name.mini.rel"
    if ! cmp -s "$work/$name.gnu.rel" "$work/$name.mini.rel"; then
        log_fail "$name" elf64_code32_relocations
        return
    fi
    log_pass "$name"
}

run_positive code16_setup
run_positive code16_binary_policy
run_positive code16_width_baseline
run_positive code16gcc_stack
run_positive code16gcc_explicit
run_positive code16_header
run_positive code16_bioscall
run_positive code16_copy
run_positive code16_trampoline
run_positive code16_wakeup_reboot
run_positive code16_trampoline_atomic
run_positive code16_address_modes
run_positive code16_branch_relax
run_positive code32_vdso
run_positive code16_linux_flags_bit
run_positive code32_linux_syscall_time
run_positive code16_linux_realmode_gcc
run_positive code16_typed_width_index
run_positive code32_linux_fences
run_positive code32_reboot
run_positive code16_linux_gcc_boot_blockers
run_positive code32_linux_gcc_symbol_difference
run_positive code16_linux_second_frontier
run_positive code32_linux_second_frontier
run_positive code16_linux_third_frontier
run_positive code32_linux_third_frontier
run_mixed_elf64 elf64_code32_linux_second_frontier
run_mixed_elf64 elf64_linux_third_frontier_difference
run_mixed_elf64 elf64_branch_modes_plt
run_mixed_elf64 elf64_conditional_plt
run_positive mixed_modes
run_positive external_rel yes
run_positive external_rel16 yes
run_negative invalid_code16_movq
run_negative invalid_code32_pushq
run_negative invalid_code16gcc_r8
run_negative invalid_code16_ltr_64reg
run_negative invalid_code32_lgdt_register

printf 'MINIAS_X86_BOOT_ORACLE=%s pass=%d fail=%d\n' \
    "$([[ $failures == 0 ]] && echo PASS || echo FAIL)" "$passed" "$failures"
[[ $failures == 0 ]]
