#!/usr/bin/env bash
# GNU BFD parity for Linux's final-static --no-dynamic-linker and -X flags.
# -X affects only .symtab; it must not disturb ELF relocation resolution.
set -Eeuo pipefail

: "${MINILD:?MINILD required}"
for tool in as ld readelf nm; do command -v "$tool" >/dev/null; done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-linux-static-flags.XXXXXXXX")
trap 'rm -r -- "$work"' EXIT

cat >"$work/fixture.s" <<'ASM'
.section .text,"ax",@progbits
.globl _start
.type _start,@function
_start:
    mov $60,%eax
    lea .Ltemporary(%rip),%rdi
    sub $_start,%rdi
    mov $42,%edi
    syscall
.Ltemporary:
    nop
.type _start,@function
.size _start,.-_start
.local local_kept
local_kept:
    .byte 0x90
ASM
as -L -o "$work/fixture.o" "$work/fixture.s"
nm -a "$work/fixture.o" | grep -Fq .Ltemporary

ld -melf_x86_64 --no-dynamic-linker -X -e _start \
    -o "$work/gnu.elf" "$work/fixture.o"
"$MINILD" -melf_x86_64 --no-dynamic-linker -X -e _start \
    -o "$work/mini.elf" "$work/fixture.o"
"$MINILD" -melf_x86_64 -e _start -o "$work/unstripped.elf" \
    "$work/fixture.o"

for owner in gnu mini; do
    readelf -W -s "$work/$owner.elf" >"$work/$owner.symbols"
    if grep -Fq .Ltemporary "$work/$owner.symbols"; then
        echo "MINILD_LINUX_STATIC_FLAGS=FAIL temporary_symbol_$owner" >&2
        exit 1
    fi
    grep -Eq '[[:space:]]local_kept$' "$work/$owner.symbols"
    ! readelf -W -l "$work/$owner.elf" | grep -Fq INTERP
    chmod +x "$work/$owner.elf"
    status=0
    "$work/$owner.elf" || status=$?
    test "$status" -eq 42
done
readelf -W -s "$work/unstripped.elf" | grep -Fq .Ltemporary

if "$MINILD" -melf_x86_64 -pie --no-dynamic-linker -o "$work/bad.elf" \
    "$work/fixture.o" >"$work/bad.out" 2>"$work/bad.err"; then
    echo 'MINILD_LINUX_STATIC_FLAGS=FAIL accepted_dynamic_without_interpreter' >&2
    exit 1
fi
grep -Fq -- '--no-dynamic-linker-requires-static-link' "$work/bad.err"
test ! -e "$work/bad.elf"
ld -melf_x86_64 --emit-relocs -e _start -o "$work/gnu-relocs.elf" \
    "$work/fixture.o"
"$MINILD" -melf_x86_64 --emit-relocs -e _start \
    -o "$work/mini-relocs.elf" "$work/fixture.o"
for owner in gnu mini; do
    readelf -W -r "$work/$owner-relocs.elf" >"$work/$owner-relocs.txt"
    grep -Fq '.rela.text' "$work/$owner-relocs.txt"
    grep -Fq R_X86_64_32S "$work/$owner-relocs.txt"
    status=0
    "$work/$owner-relocs.elf" || status=$?
    [[ $status == 42 ]]
done
echo 'MINILD_LINUX_STATIC_FLAGS=PASS GNU_BFD_X_symtab=parity static_no_interp=true runtime=42 emit_relocs=positive'
