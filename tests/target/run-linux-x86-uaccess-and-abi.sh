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
    x86_64_linux_load_unaligned_zeropad
do
    "$host_cc" -E -P "$source_root/tests/target/$fixture.c" \
        -o "$workdir/$fixture.i"
    "$minic_cc" -S "$workdir/$fixture.i" --target=x86_64-linux \
        -fshort-wchar -funsigned-char -fno-PIE \
        -o "$workdir/$fixture.s"
    "$host_cc" -c -x assembler "$workdir/$fixture.s" \
        -o "$workdir/$fixture.o"
done

readelf -r "$workdir/x86_64_linux_put_user_call.o" > "$workdir/put.reloc"
readelf -r "$workdir/x86_64_linux_get_user_call.o" > "$workdir/get.reloc"
for size in 1 2 4 8
do
    if ! grep -Fq "__put_user_$size" "$workdir/put.reloc"; then
        printf 'FAIL: x86 put_user_%s helper relocation absent\n' "$size" >&2
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
readelf -r "$workdir/x86_64_linux_kernel_nofault_goto.o" > "$workdir/nofault.reloc"
readelf -x __ex_table "$workdir/x86_64_linux_kernel_nofault_goto.o" > "$workdir/nofault.hex"
if ! grep -Fq '.rela__ex_table' "$workdir/nofault.reloc" ||
   [ "$(grep -c 'R_X86_64_PC32' "$workdir/nofault.reloc")" -ne 16 ] ||
   [ "$(grep -o '03000000' "$workdir/nofault.hex" | wc -l)" -ne 8 ]; then
    printf 'FAIL: kernel nofault 8 MOV types, C fault label, or PREL32 fixup lost\n' >&2
    exit 1
fi
"$host_cc" -no-pie \
    "$source_root/tests/target/x86_64_linux_kernel_nofault_driver.c" \
    "$workdir/x86_64_linux_kernel_nofault_goto.o" -o "$workdir/nofault-run"
"$workdir/nofault-run"

# Four SMAP forms carry feature 308, REP MOVSB FSRM feature 66116;
# none may lose its original alternative replacement or PREL32 metadata.
readelf -r "$workdir/x86_64_linux_smap_rep_alternative.o" > "$workdir/smap.reloc"
objcopy --dump-section .altinstructions="$workdir/smap.altbin" \
    "$workdir/x86_64_linux_smap_rep_alternative.o"
smap_features=$(od -An -tx1 -v "$workdir/smap.altbin" | tr -d '[:space:]')
readelf -x __ex_table "$workdir/x86_64_linux_smap_rep_alternative.o" > "$workdir/smap.extable"
if [ "$(printf '%s' "$smap_features" | grep -o '34010000' | wc -l)" -ne 4 ] ||
   [ "$(printf '%s' "$smap_features" | grep -o '44020100' | wc -l)" -ne 1 ] ||
   ! grep -Fq 'rep_movs_alternative' "$workdir/smap.reloc" ||
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

printf 'PASS: Linux x86 uaccess, kernel nofault C asm-goto, SMAP/FSRM/LFENCE alternatives, type-20 unaligned zero-pad, MDS/WBINVD/SWAPGS, CLFLUSH, DR0/1/2/3/6/7, FSGSBASE, LKGS and segment __ex_table, GS pointer, signed/unsigned 2-GPR ABI runtime\n'
