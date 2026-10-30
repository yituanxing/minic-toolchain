#!/bin/sh
# GNU AS / ELF validation for original x86 Linux uaccess, idle and SysV ABI.
# Privileged kernel instructions are compiled and disassembled, NEVER run.
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

for fixture in \
    x86_64_linux_put_user_call \
    x86_64_linux_get_user_call \
    x86_64_linux_idle_clflush_alt \
    x86_64_linux_cpu_buffer_clear \
    x86_64_linux_debug_register \
    x86_64_linux_thread_state \
    x86_64_linux_membuf_abi \
    x86_64_linux_kernel_nofault_goto \
    x86_64_linux_smap_rep_alternative \
    x86_64_linux_usercopy_lfence \
    x86_64_linux_load_unaligned_zeropad \
    x86_64_linux_irqentry_state \
    x86_64_linux_selector_lar \
    x86_64_linux_rng_flag \
    x86_64_linux_fpu_userframe \
    x86_64_linux_fxrstor_safe \
    x86_64_linux_xstate_fault \
    x86_64_linux_xstate_alternatives \
    x86_64_linux_jump_label \
    x86_64_linux_pkru \
    x86_64_linux_fdiv_bug \
    x86_64_linux_ds_regset
do
    "$host_cc" -E -P "$source_root/tests/target/$fixture.c" \
        -o "$workdir/$fixture.i"
    "$minic_cc" -S "$workdir/$fixture.i" --target=x86_64-linux \
        -fshort-wchar -funsigned-char -fno-PIE \
        -o "$workdir/$fixture.s"
    "$host_cc" -c -x assembler "$workdir/$fixture.s" \
        -o "$workdir/$fixture.o"
done

objdump -d "$workdir/x86_64_linux_pkru.o" > "$workdir/pkru.dump"
if ! grep -Eq 'rdpkru' "$workdir/pkru.dump" ||
   ! grep -Eq 'wrpkru' "$workdir/pkru.dump" ||
   ! grep -Fq 'movl %edx, (%r11)' "$workdir/x86_64_linux_pkru.s" ||
   ! grep -Fq 'movl %eax, (%r11)' "$workdir/x86_64_linux_pkru.s"; then
    printf 'FAIL: x86 Linux PKRU fixed-GPR inputs or dual 32-bit outputs lost\n' >&2
    exit 1
fi

objdump -d "$workdir/x86_64_linux_fdiv_bug.o" > "$workdir/fdiv.dump"
for opcode in fninit fldl fdivl fmull fsubp fistpl; do
    if ! grep -Fq "$opcode" "$workdir/fdiv.dump"; then
        printf 'FAIL: x86 Linux FPU FDIV x87 opcode %s was dropped\n' "$opcode" >&2
        exit 1
    fi
done
if ! grep -Fq 'fwait' "$workdir/x86_64_linux_fdiv_bug.s"; then
    printf 'FAIL: x86 Linux FPU FDIV FWAIT synchronization was dropped\n' >&2
    exit 1
fi
if ! grep -Fq 'movl %ds, %eax' "$workdir/x86_64_linux_ds_regset.s" ||
   ! grep -Fq 'movl %eax, (%r11)' "$workdir/x86_64_linux_ds_regset.s" ||
   ! grep -Fq 'movw %es, %ax' "$workdir/x86_64_linux_ds_regset.s" ||
   ! grep -Fq 'movw %ax, (%r11)' "$workdir/x86_64_linux_ds_regset.s"; then
    printf 'FAIL: Linux regset 32-bit DS/16-bit ES selector store was mis-sized\n' >&2
    exit 1
fi

readelf -r "$workdir/x86_64_linux_put_user_call.o" > "$workdir/put.reloc"
readelf -r "$workdir/x86_64_linux_get_user_call.o" > "$workdir/get.reloc"
for size in 1 2 4 8
do
    if ! grep -Fq "__put_user_$size" "$workdir/put.reloc"; then
        printf 'FAIL: x86 put_user_%s helper relocation absent\n' "$size" >&2
        exit 1
    fi
done
for size in 2 4
do
    if ! grep -Fq "__put_user_nocheck_$size" "$workdir/put.reloc"; then
        printf 'FAIL: x86 put_user_nocheck_%s helper relocation absent\n' "$size" >&2
        exit 1
    fi
done
for symbol in __get_user_1 __get_user_2 \
              __get_user_nocheck_4 __get_user_nocheck_8
do
    if ! grep -Fq "$symbol" "$workdir/get.reloc"; then
        printf 'FAIL: x86 get_user helper relocation %s absent\n' "$symbol" >&2
        exit 1
    fi
done
if ! grep -Fq 'movq %rbx, (%rsp)' "$workdir/x86_64_linux_put_user_call.s" ||
   ! grep -Fq 'movq (%rsp), %rbx' "$workdir/x86_64_linux_put_user_call.s" ||
   ! grep -Fq 'movl %ecx, (%r11)' "$workdir/x86_64_linux_put_user_call.s"; then
    printf 'FAIL: x86 put_user must preserve RBX and write ECX error\n' >&2
    exit 1
fi
for store in 'movb %dl' 'movw %dx' 'movl %edx' 'movq %rdx'
do
    if ! grep -Fq "$store" "$workdir/x86_64_linux_get_user_call.s"; then
        printf 'FAIL: x86 get_user narrow/wide RDX result %s absent\n' "$store" >&2
        exit 1
    fi
done

objdump -d "$workdir/x86_64_linux_cpu_buffer_clear.o" > "$workdir/cpu.dump"
for opcode in verw wbinvd swapgs
do
    if ! grep -Fq "$opcode" "$workdir/cpu.dump"; then
        printf 'FAIL: x86 buffer clearing opcode %s absent\n' "$opcode" >&2
        exit 1
    fi
done
objdump -d "$workdir/x86_64_linux_debug_register.o" > "$workdir/debug.dump"
for reg in 0 1 2 3 6 7
do
    if ! grep -Fq "%db$reg,%rax" "$workdir/debug.dump" ||
       ! grep -Fq "%rax,%db$reg" "$workdir/debug.dump"; then
        printf 'FAIL: x86 DR%s bidirectional real register MOV absent\n' "$reg" >&2
        exit 1
    fi
done
if ! grep -Fq '%ss,%ax' "$workdir/debug.dump" ||
   ! grep -Fq '%cs,%ax' "$workdir/debug.dump" ||
   ! grep -Fq 'movq %rax, %gs:(%r10)' "$workdir/x86_64_linux_debug_register.s"; then
    printf 'FAIL: Linux x86 selector or GS pointer state transfer absent\n' >&2
    exit 1
fi
# DR7's dummy 0x1000 memory operand is only a compiler dependency.
# An immediate setup is legal; an actual load/store at 0x1000 is not.
if grep -Eq 'mov[bwlq]? (0x1000|\$0x1000)\(' "$workdir/debug.dump"; then
    printf 'FAIL: DR7 synthetic compiler memory was dereferenced\n' >&2
    exit 1
fi
objdump -d "$workdir/x86_64_linux_thread_state.o" > "$workdir/thread.dump"
for instruction in rdfsbase rdgsbase wrfsbase wrgsbase
do
    if ! grep -Fq "$instruction" "$workdir/thread.dump"; then
        printf 'FAIL: FSGSBASE original Linux instruction %s absent\n' "$instruction" >&2
        exit 1
    fi
done
readelf -x __ex_table "$workdir/x86_64_linux_thread_state.o" > "$workdir/thread.extable"
readelf -r "$workdir/x86_64_linux_thread_state.o" > "$workdir/thread.reloc"
if ! grep -Fq '11070000' "$workdir/thread.extable" ||
   ! grep -Fq '11000000' "$workdir/thread.extable" ||
   ! grep -Fq '.rela__ex_table' "$workdir/thread.reloc" ||
   [ "$(grep -c 'R_X86_64_PC32' "$workdir/thread.reloc")" -ne 8 ]; then
    printf 'FAIL: LKGS / ES / DS / SS type-17 exception fixup lost\n' >&2
    exit 1
fi
readelf -SW "$workdir/x86_64_linux_idle_clflush_alt.o" > "$workdir/alt.sections"
readelf -r "$workdir/x86_64_linux_idle_clflush_alt.o" > "$workdir/alt.reloc"
readelf -x .altinstructions "$workdir/x86_64_linux_idle_clflush_alt.o" > "$workdir/alt.hex"
for section in .altinstructions .altinstr_replacement .rela.altinstructions
do
    if ! grep -Fq "$section" "$workdir/alt.sections"; then
        printf 'FAIL: x86 CLFLUSH alternative section %s absent\n' "$section" >&2
        exit 1
    fi
done
if ! grep -Fq 'c7020000 0303' "$workdir/alt.hex" ||
   ! grep -Fq 'R_X86_64_PC32' "$workdir/alt.reloc"; then
    printf 'FAIL: x86 CLFLUSH feature 711, 3/3 bytes or PREL32 lost\n' >&2
    exit 1
fi

# Linux x86 uaccess nofault GNU ASM GOTO carries two PREL32 relocations
# per typed MOV: the fault instruction and its actual C error block.
# The ninth MOV stores a genuine pointer as Linux signal_64.i does.
readelf -r "$workdir/x86_64_linux_kernel_nofault_goto.o" > "$workdir/nofault.reloc"
readelf -x __ex_table "$workdir/x86_64_linux_kernel_nofault_goto.o" > "$workdir/nofault.hex"
if ! grep -Fq '.rela__ex_table' "$workdir/nofault.reloc" ||
   [ "$(grep -c 'R_X86_64_PC32' "$workdir/nofault.reloc")" -ne 18 ] ||
   [ "$(grep -o '03000000' "$workdir/nofault.hex" | wc -l)" -ne 9 ]; then
    printf 'FAIL: kernel nofault 8 MOV types plus pointer, C fault label, or PREL32 fixup lost\n' >&2
    exit 1
fi
"$host_cc" -no-pie \
    "$source_root/tests/target/x86_64_linux_kernel_nofault_driver.c" \
    "$workdir/x86_64_linux_kernel_nofault_goto.o" -o "$workdir/nofault-run"
"$workdir/nofault-run"

# Four SMAP forms carry feature 308, REP MOVSB FSRM feature 66116,
# and REP STOSB's ERMS fallback feature 65931;
# none may lose its original alternative replacement or PREL32 metadata.
readelf -r "$workdir/x86_64_linux_smap_rep_alternative.o" > "$workdir/smap.reloc"
objcopy --dump-section .altinstructions="$workdir/smap.altbin" \
    "$workdir/x86_64_linux_smap_rep_alternative.o"
smap_features=$(od -An -tx1 -v "$workdir/smap.altbin" | tr -d '[:space:]')
readelf -x __ex_table "$workdir/x86_64_linux_smap_rep_alternative.o" > "$workdir/smap.extable"
if [ "$(printf '%s' "$smap_features" | grep -o '34010000' | wc -l)" -ne 4 ] ||
   [ "$(printf '%s' "$smap_features" | grep -o '44020100' | wc -l)" -ne 1 ] ||
   [ "$(printf '%s' "$smap_features" | grep -o '8b010100' | wc -l)" -ne 1 ] ||
   ! grep -Fq 'rep_movs_alternative' "$workdir/smap.reloc" ||
   ! grep -Fq 'rep_stos_alternative' "$workdir/smap.reloc" ||
   ! grep -Fq '.rela__ex_table' "$workdir/smap.reloc" ||
   ! grep -Fq '03000000' "$workdir/smap.extable"; then
    printf 'FAIL: SMAP/FSRM alternatives, replacement CALL, or exception metadata lost\n' >&2
    exit 1
fi
objcopy --dump-section .altinstructions="$workdir/lfence.altbin" \
    "$workdir/x86_64_linux_usercopy_lfence.o"
lfence_features=$(od -An -tx1 -v "$workdir/lfence.altbin" | tr -d '[:space:]')
readelf -r "$workdir/x86_64_linux_usercopy_lfence.o" > "$workdir/lfence.reloc"
if [ "$(printf '%s' "$lfence_features" | grep -o '82020000' | wc -l)" -ne 1 ] ||
   ! grep -Fq '.rela.altinstructions' "$workdir/lfence.reloc"; then
    printf 'FAIL: Linux usercopy LFENCE feature 642 or PREL32 patch lost\n' >&2
    exit 1
fi
readelf -r "$workdir/x86_64_linux_load_unaligned_zeropad.o" > "$workdir/unaligned.reloc"
readelf -x __ex_table "$workdir/x86_64_linux_load_unaligned_zeropad.o" > "$workdir/unaligned.hex"
if ! grep -Fq '.rela__ex_table' "$workdir/unaligned.reloc" ||
   [ "$(grep -c 'R_X86_64_PC32' "$workdir/unaligned.reloc")" -ne 2 ] ||
   ! grep -Fq '14000000' "$workdir/unaligned.hex" ||
   ! grep -Fq 'movq (%r10), %rax' "$workdir/x86_64_linux_load_unaligned_zeropad.s"; then
    printf 'FAIL: type-20 Linux zero-padded unaligned load or recovery metadata lost\n' >&2
    exit 1
fi
"$host_cc" -no-pie \
    "$source_root/tests/target/x86_64_linux_load_unaligned_driver.c" \
    "$workdir/x86_64_linux_load_unaligned_zeropad.o" \
    -o "$workdir/unaligned-run"
"$workdir/unaligned-run"

# Linux's anonymous-union irqentry_state_t is one INTEGER eightbyte.
# Exercise both GNU -> MiniC and MiniC -> GNU returns/calls, not just ELF.
"$host_cc" -no-pie \
    "$source_root/tests/target/x86_64_linux_irqentry_driver.c" \
    "$workdir/x86_64_linux_irqentry_state.o" \
    -o "$workdir/irqentry-run"
"$workdir/irqentry-run"

"$host_cc" -no-pie \
    "$source_root/tests/target/x86_64_linux_selector_lar_driver.c" \
    "$workdir/x86_64_linux_selector_lar.o" -o "$workdir/lar-run"
"$workdir/lar-run"

objdump -d "$workdir/x86_64_linux_rng_flag.o" > "$workdir/rng.dump"
for opcode in rdrand rdseed setb
do
    if ! grep -Fq "$opcode" "$workdir/rng.dump"; then
        printf 'FAIL: x86 CPU random %s or carry-flag output absent\n' "$opcode" >&2
        exit 1
    fi
done
if [ "$(grep -c '  setc %cl' "$workdir/x86_64_linux_rng_flag.s")" -ne 2 ]; then
    printf 'FAIL: hardware RNG carry flag was not materialized before output stores\n' >&2
    exit 1
fi

readelf -r "$workdir/x86_64_linux_fpu_userframe.o" > "$workdir/fpu.reloc"
readelf -x __ex_table "$workdir/x86_64_linux_fpu_userframe.o" > "$workdir/fpu.extable"
objcopy --dump-section .altinstructions="$workdir/fpu.altbin" \
    "$workdir/x86_64_linux_fpu_userframe.o"
fpu_features=$(od -An -tx1 -v "$workdir/fpu.altbin" | tr -d '[:space:]')
if [ "$(printf '%s' "$fpu_features" | grep -o '34010000' | wc -l)" -ne 8 ] ||
   [ "$(grep -o '0f000000' "$workdir/fpu.extable" | wc -l)" -ne 4 ] ||
   [ "$(grep -c 'R_X86_64_PC32' "$workdir/fpu.reloc")" -ne 24 ]; then
    printf 'FAIL: FPU user save/restore SMAP patches or type-15 exception fixups lost\n' >&2
    exit 1
fi
readelf -r "$workdir/x86_64_linux_fxrstor_safe.o" > "$workdir/fxrstor.reloc"
readelf -x __ex_table "$workdir/x86_64_linux_fxrstor_safe.o" > "$workdir/fxrstor.extable"
if [ "$(grep -c 'R_X86_64_PC32' "$workdir/fxrstor.reloc")" -ne 2 ] ||
   ! grep -Fq '1100f2ff' "$workdir/fxrstor.extable"; then
    printf 'FAIL: FXRSTORQ type-17 EAX error-code relocation/data lost\n' >&2
    exit 1
fi
readelf -r "$workdir/x86_64_linux_xstate_fault.o" > "$workdir/xstate.reloc"
readelf -x __ex_table "$workdir/x86_64_linux_xstate_fault.o" > "$workdir/xstate.extable"
objdump -d "$workdir/x86_64_linux_xstate_fault.o" > "$workdir/xstate.dump"
if [ "$(grep -c 'R_X86_64_PC32' "$workdir/xstate.reloc")" -ne 8 ] ||
   [ "$(grep -o '0f000000' "$workdir/xstate.extable" | wc -l)" -ne 4 ] ||
   ! grep -Fq 'xsave64' "$workdir/xstate.dump" ||
   ! grep -Fq 'xrstor64' "$workdir/xstate.dump" ||
   ! grep -Fq 'xrstors64' "$workdir/xstate.dump" ||
   ! grep -Fq 'xsaves64' "$workdir/xstate.dump"; then
    printf 'FAIL: x86 XSAVE/XRSTOR/XSAVES/XRSTORS type-15 fixed-register exception fixups lost\n' >&2
    exit 1
fi
readelf -r "$workdir/x86_64_linux_xstate_alternatives.o" > "$workdir/xstate-alt.reloc"
readelf -x __ex_table "$workdir/x86_64_linux_xstate_alternatives.o" > "$workdir/xstate-alt.extable"
objcopy --dump-section .altinstructions="$workdir/xstate-alt.bin" \
    "$workdir/x86_64_linux_xstate_alternatives.o"
objdump -d -j .altinstr_replacement \
    "$workdir/x86_64_linux_xstate_alternatives.o" > "$workdir/xstate-alt.dump"
xstate_features=$(od -An -tx1 -v "$workdir/xstate-alt.bin" | tr -d '[:space:]')
if [ "$(grep -c 'R_X86_64_PC32' "$workdir/xstate-alt.reloc")" -ne 12 ] ||
   [ "$(printf '%s' "$xstate_features" | grep -o '40010000' | wc -l)" -ne 1 ] ||
   [ "$(printf '%s' "$xstate_features" | grep -o '41010000' | wc -l)" -ne 1 ] ||
   [ "$(printf '%s' "$xstate_features" | grep -o '43010000' | wc -l)" -ne 2 ] ||
   ! grep -Fq '1100f2ff' "$workdir/xstate-alt.extable" ||
   ! grep -Fq '06000000' "$workdir/xstate-alt.extable"; then
    printf 'FAIL: XSAVE ALTERNATIVE_3/XSAVES features or type-17/type-6 recovery lost\n' >&2
    exit 1
fi
for opcode in xsaveopt64 xsavec64 xsaves64 xrstors64
do
    if ! grep -Fq "$opcode" "$workdir/xstate-alt.dump"; then
        printf 'FAIL: x86 XSAVE CPU replacement opcode %s absent\n' "$opcode" >&2
        exit 1
    fi
done
readelf -r "$workdir/x86_64_linux_jump_label.o" > "$workdir/jump.reloc"
objdump -r -j __jump_table "$workdir/x86_64_linux_jump_label.o" \
    > "$workdir/jump-table.reloc"
# Count only the __jump_table patch entries: an independently valid RIP-
# relative address of the local key may add a PC32 relocation to .text.
if [ "$(grep -c 'R_X86_64_PC32' "$workdir/jump-table.reloc")" -ne 6 ] ||
   [ "$(grep -c 'R_X86_64_PC64' "$workdir/jump-table.reloc")" -ne 3 ] ||
   ! grep -Fq '.rela__jump_table' "$workdir/jump.reloc"; then
    printf 'FAIL: Linux jump_label patch-site/target/key+flags relocation lost\n' >&2
    exit 1
fi

# SysV two-GPR aggregate: GNU caller -> MiniC callee, plus MiniC -> MiniC
# call. Safe to execute: the fixture has no privileged instructions.
"$host_cc" -no-pie "$source_root/tests/target/x86_64_linux_membuf_driver.c" \
    "$workdir/x86_64_linux_membuf_abi.o" -o "$workdir/membuf-run"
"$workdir/membuf-run"

sed 's/__put_user_/__put_user_invalid_/g' \
    "$workdir/x86_64_linux_put_user_call.i" > "$workdir/mutated_put.i"
if "$minic_cc" -S "$workdir/mutated_put.i" --target=x86_64-linux \
    -o "$workdir/mutated_put.s" >/dev/null 2>&1; then
    printf 'FAIL: mutated privileged put_user helper accepted\n' >&2
    exit 1
fi
sed 's/clflush (/clflushopt (/g' \
    "$workdir/x86_64_linux_idle_clflush_alt.i" > "$workdir/mutated_alt.i"
if "$minic_cc" -S "$workdir/mutated_alt.i" --target=x86_64-linux \
    -o "$workdir/mutated_alt.s" >/dev/null 2>&1; then
    printf 'FAIL: mutated privileged CLFLUSH alternative accepted\n' >&2
    exit 1
fi
sed 's/%%db0/%%db4/g' "$workdir/x86_64_linux_debug_register.i" \
    > "$workdir/mutated_dr.i"
if "$minic_cc" -S "$workdir/mutated_dr.i" --target=x86_64-linux \
    -o "$workdir/mutated_dr.s" >/dev/null 2>&1; then
    printf 'FAIL: invalid DR4 privileged debug register admitted\n' >&2
    exit 1
fi
sed 's/(17 | ((0) << 16))/(18 | ((0) << 16))/g' \
    "$workdir/x86_64_linux_thread_state.i" > "$workdir/mutated_extable.i"
if "$minic_cc" -S "$workdir/mutated_extable.i" --target=x86_64-linux \
    -o "$workdir/mutated_extable.s" >/dev/null 2>&1; then
    printf 'FAIL: corrupted type-17 register exception fixup admitted\n' >&2
    exit 1
fi

sed 's/\.long 3/\.long 4/g' \
    "$workdir/x86_64_linux_kernel_nofault_goto.i" > "$workdir/mutated_nofault.i"
if "$minic_cc" -S "$workdir/mutated_nofault.i" --target=x86_64-linux \
    -o "$workdir/mutated_nofault.s" >/dev/null 2>&1; then
    printf 'FAIL: corrupted kernel nofault exception type accepted\n' >&2
    exit 1
fi
sed 's/rep movsb/rep stosb/g' \
    "$workdir/x86_64_linux_smap_rep_alternative.i" > "$workdir/mutated_rep.i"
if "$minic_cc" -S "$workdir/mutated_rep.i" --target=x86_64-linux \
    -o "$workdir/mutated_rep.s" >/dev/null 2>&1; then
    printf 'FAIL: invalid REP MOVSB alternative admitted\n' >&2
    exit 1
fi
sed 's/0x01,0xca/0x01,0xcc/g' \
    "$workdir/x86_64_linux_smap_rep_alternative.i" > "$workdir/mutated_smap.i"
if "$minic_cc" -S "$workdir/mutated_smap.i" --target=x86_64-linux \
    -o "$workdir/mutated_smap.s" >/dev/null 2>&1; then
    printf 'FAIL: invalid SMAP ALTERNATIVE opcode admitted\n' >&2
    exit 1
fi
sed 's/lfence/sfence/g' \
    "$workdir/x86_64_linux_usercopy_lfence.i" > "$workdir/mutated_lfence.i"
if "$minic_cc" -S "$workdir/mutated_lfence.i" --target=x86_64-linux \
    -o "$workdir/mutated_lfence.s" >/dev/null 2>&1; then
    printf 'FAIL: invalid usercopy LFENCE ALTERNATIVE admitted\n' >&2
    exit 1
fi
sed 's/"20"/"21"/g' \
    "$workdir/x86_64_linux_load_unaligned_zeropad.i" > "$workdir/mutated_unaligned.i"
if "$minic_cc" -S "$workdir/mutated_unaligned.i" --target=x86_64-linux \
    -o "$workdir/mutated_unaligned.s" >/dev/null 2>&1; then
    printf 'FAIL: invalid type-21 unaligned load recovery admitted\n' >&2
    exit 1
fi
sed 's/lar %\[old_ss\]/lsl %[old_ss]/g' \
    "$workdir/x86_64_linux_selector_lar.i" > "$workdir/mutated_lar.i"
if "$minic_cc" -S "$workdir/mutated_lar.i" --target=x86_64-linux \
    -o "$workdir/mutated_lar.s" >/dev/null 2>&1; then
    printf 'FAIL: corrupted Linux LAR selector-rights opcode admitted\n' >&2
    exit 1
fi
sed 's/rdrand %\[out\]/rdrandom %[out]/g' \
    "$workdir/x86_64_linux_rng_flag.i" > "$workdir/mutated_rng.i"
if "$minic_cc" -S "$workdir/mutated_rng.i" --target=x86_64-linux \
    -o "$workdir/mutated_rng.s" >/dev/null 2>&1; then
    printf 'FAIL: invalid RDRAND opcode with CF output admitted\n' >&2
    exit 1
fi
sed 's/"15"/"16"/g' \
    "$workdir/x86_64_linux_fpu_userframe.i" > "$workdir/mutated_fpu.i"
if "$minic_cc" -S "$workdir/mutated_fpu.i" --target=x86_64-linux \
    -o "$workdir/mutated_fpu.s" >/dev/null 2>&1; then
    printf 'FAIL: invalid FPU exception type-16 admitted\n' >&2
    exit 1
fi
sed 's/-14/-15/g' \
    "$workdir/x86_64_linux_fxrstor_safe.i" > "$workdir/mutated_fxrstor.i"
if "$minic_cc" -S "$workdir/mutated_fxrstor.i" --target=x86_64-linux \
    -o "$workdir/mutated_fxrstor.s" >/dev/null 2>&1; then
    printf 'FAIL: corrupted FXRSTORQ type-17 error code admitted\n' >&2
    exit 1
fi
sed 's/\.long " "15" " /\.long " "16" " /g' \
    "$workdir/x86_64_linux_xstate_fault.i" > "$workdir/mutated_xstate.i"
if "$minic_cc" -S "$workdir/mutated_xstate.i" --target=x86_64-linux \
    -o "$workdir/mutated_xstate.s" >/dev/null 2>&1; then
    printf 'FAIL: XSAVE type-16 instead of type-15 accepted\n' >&2
    exit 1
fi
sed 's/(10\*32+ 3)/(10*32+ 2)/g' \
    "$workdir/x86_64_linux_xstate_alternatives.i" > "$workdir/mutated_xsave_alt.i"
if "$minic_cc" -S "$workdir/mutated_xsave_alt.i" --target=x86_64-linux \
    -o "$workdir/mutated_xsave_alt.s" >/dev/null 2>&1; then
    printf 'FAIL: corrupted XSAVE CPU feature admitted\n' >&2
    exit 1
fi
sed 's/__jump_table/__jump_bad_table/g' \
    "$workdir/x86_64_linux_jump_label.i" > "$workdir/mutated_jump.i"
if "$minic_cc" -S "$workdir/mutated_jump.i" --target=x86_64-linux \
    -o "$workdir/mutated_jump.s" >/dev/null 2>&1; then
    printf 'FAIL: malformed Linux jump_label patch section accepted\n' >&2
    exit 1
fi

printf 'PASS: Linux x86 uaccess/FPU type-15 SMAP, nofault pointer asm-goto, LAR/ZF, RNG CF, REP STOS/MOVS/LFENCE, type-20 zero-pad, MDS/WBINVD/SWAPGS, CLFLUSH, DR0/1/2/3/6/7, FSGSBASE, LKGS/segment __ex_table, nested-union/2-GPR SysV GNU runtime\n'
