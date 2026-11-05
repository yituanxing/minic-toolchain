#!/usr/bin/env bash
# Linux x86 compressed/vmlinux is freestanding -pie --no-dynamic-linker -T:
# its ET_DYN image has no dynamic loader or runtime relocations. This must
# use the final static linker pipeline without admitting user-space PIE.
set -Eeuo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
mini=${MINILD:-$repo/build/release/bin/minic-ld}
[[ -x $mini ]] || { echo 'MINILD_X86_KERNEL_PIE=FAIL missing_MiniLD' >&2; exit 1; }
for command in gcc ld readelf timeout; do
    command -v "$command" >/dev/null || {
        echo "MINILD_X86_KERNEL_PIE=FAIL missing_tool=$command" >&2
        exit 1
    }
done
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT

printf '%s\n' \
    '.section .head.text,"ax",@progbits' \
    '.globl _start' \
    '_start:' \
    '    xorl %edi,%edi' \
    '    movl $60,%eax' \
    '    syscall' >"$work/start.s"
gcc -m64 -x assembler -c "$work/start.s" -o "$work/start.o"
printf '%s\n' \
    'OUTPUT_FORMAT("elf64-x86-64")' \
    'OUTPUT_ARCH(i386:x86-64)' \
    'ENTRY(_start)' \
    'SECTIONS {' \
    '  . = 0;' \
    '  .head.text : { *(.head.text) }' \
    '  .text : { *(.text*) }' \
    '  .rodata : { *(.rodata*) }' \
    '  .data : { *(.data*) *(.dynamic) *(.dynsym) *(.dynstr) }' \
    '  .bss : { *(.bss*) *(COMMON) }' \
    '  .rela.dyn : { *(.rela.*) }' \
    '}' >"$work/compressed.lds"

ld -m elf_x86_64 -pie --no-dynamic-linker -T "$work/compressed.lds" \
    -o "$work/gnu.elf" "$work/start.o" >"$work/gnu.log" 2>&1
"$mini" -m elf_x86_64 -pie --no-dynamic-linker \
    --no-ld-generated-unwind-info -z noexecstack \
    -T "$work/compressed.lds" \
    -o "$work/mini.elf" "$work/start.o" >"$work/mini.log" 2>&1 || {
        tail -n 30 "$work/mini.log" >&2
        echo 'MINILD_X86_KERNEL_PIE=FAIL kernel_mode_link' >&2
        exit 1
    }
for owner in gnu mini; do
    readelf -h "$work/$owner.elf" >"$work/$owner.header"
    readelf -l "$work/$owner.elf" >"$work/$owner.phdr"
    grep -Eq 'Type:[[:space:]]*DYN' "$work/$owner.header" || {
        echo "MINILD_X86_KERNEL_PIE=FAIL wrong_elf_type=$owner" >&2; exit 1;
    }
    if grep -Eq 'INTERP|Requesting program interpreter' "$work/$owner.phdr"; then
        echo "MINILD_X86_KERNEL_PIE=FAIL unexpected_interpreter=$owner" >&2
        exit 1
    fi
    timeout 5 "$work/$owner.elf" || {
        echo "MINILD_X86_KERNEL_PIE=FAIL runtime=$owner" >&2; exit 1;
    }
done
# MiniLD's freestanding output must never synthesize user-space metadata.
if readelf -l "$work/mini.elf" | grep -Eq 'DYNAMIC|INTERP'; then
    echo 'MINILD_X86_KERNEL_PIE=FAIL dynamic_program_header' >&2
    exit 1
fi
if readelf -S "$work/mini.elf" | grep -Eq '\.dynamic|\.dynsym|\.rela\.dyn'; then
    echo 'MINILD_X86_KERNEL_PIE=FAIL dynamic_runtime_section' >&2
    exit 1
fi

reject() {
    local name=$1
    shift
    if "$mini" "$@" >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        echo "MINILD_X86_KERNEL_PIE=FAIL falsely_admitted=$name" >&2
        exit 1
    fi
    [[ ! -e $work/$name.elf ]] || {
        echo "MINILD_X86_KERNEL_PIE=FAIL partial_output=$name" >&2
        exit 1
    }
}
reject x86_user_pie -m elf_x86_64 -pie -T "$work/compressed.lds" \
    -o "$work/x86_user_pie.elf" "$work/start.o"
reject x86_no_script -m elf_x86_64 -pie --no-dynamic-linker \
    -o "$work/x86_no_script.elf" "$work/start.o"
reject x86_dyn_interp -m elf_x86_64 -pie --no-dynamic-linker \
    --dynamic-linker /lib/ld-linux-x86-64.so.2 -T "$work/compressed.lds" \
    -o "$work/x86_dyn_interp.elf" "$work/start.o"
reject x86_dyn_soname -m elf_x86_64 -pie --no-dynamic-linker \
    -soname inappropriate.so -T "$work/compressed.lds" \
    -o "$work/x86_dyn_soname.elf" "$work/start.o"
reject x86_dynamic_hash -m elf_x86_64 -pie --no-dynamic-linker \
    --hash-style=both -T "$work/compressed.lds" \
    -o "$work/x86_dynamic_hash.elf" "$work/start.o"
reject x86_shared -m elf_x86_64 -shared \
    -o "$work/x86_shared.elf" "$work/start.o"
reject foreign_kernel_pie -m aarch64linux -pie --no-dynamic-linker \
    -T "$work/compressed.lds" -o "$work/foreign_kernel_pie.elf" "$work/start.o"

echo 'MINILD_X86_KERNEL_PIE=PASS ET_DYN=1 PT_INTERP=0 PT_DYNAMIC=0 GNU_runtime=0 MiniLD_runtime=0 user_PIE_rejected=1 x86_shared_rejected=1 foreign_target_rejected=1'
