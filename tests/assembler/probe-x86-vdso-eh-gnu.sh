#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to exact-head minic-as}"
for tool in as ld readelf objcopy cmp awk diff; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The real Linux i386 VDSO is linked as a shared ELF32 object.  The FDE's
# PC-relative reference must bind locally; otherwise GNU ld rejects a dynamic
# relocation in read-only .eh_frame even though a standalone .o is valid.
cat >"$work/sigreturn.s" <<'ASM'
.text
.code32
.globl __kernel_sigreturn
.type __kernel_sigreturn,@function
__kernel_sigreturn:
.LSTART_sigreturn:
popl %eax
ret
.LEND_sigreturn:
.size __kernel_sigreturn, .-.LSTART_sigreturn
.section .eh_frame,"a",@progbits
.LSTARTFRAMEDLSI1:
.long .LENDCIEDLSI1-.LSTARTCIEDLSI1
.LSTARTCIEDLSI1:
.long 0
.byte 1
.string "zRS"
.uleb128 1
.sleb128 -4
.byte 8
.uleb128 1
.byte 0x1b
.byte 0
.align 4
.LENDCIEDLSI1:
.long .LENDFDEDLSI1-.LSTARTFDEDLSI1
.LSTARTFDEDLSI1:
.long .LSTARTFDEDLSI1-.LSTARTFRAMEDLSI1
.long .LSTART_sigreturn-1-.
.long .LEND_sigreturn-.LSTART_sigreturn+1
.uleb128 0
.align 4
.LENDFDEDLSI1:
.section .data.rel.ro,"aw",@progbits
.long .LSTART_sigreturn+3-.
.long .LSTART_sigreturn-.+7
.long .LSTART_sigreturn-.-3
.long .LSTART_sigreturn-(.+3)
.long ((.LSTART_sigreturn-8)-.)
.long .LSTART_sigreturn-.LEND_sigreturn
.long .LSTART_sigreturn-(.LEND_sigreturn+1)
.section .note.GNU-stack,"",@progbits
ASM

as --32 -o "$work/gnu.o" "$work/sigreturn.s"
"$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
    -o "$work/mini.o" "$work/sigreturn.s"

ld -m elf_i386 -shared -o "$work/gnu.so" "$work/gnu.o"
ld -m elf_i386 -shared -o "$work/mini.so" "$work/mini.o"

readelf -rW "$work/gnu.o" >"$work/gnu.reloc"
readelf -rW "$work/mini.o" >"$work/mini.reloc"
awk '/R_386_/ {print $1, $3, $5}' "$work/gnu.reloc" >"$work/gnu.tuples"
awk '/R_386_/ {print $1, $3, $5}' "$work/mini.reloc" >"$work/mini.tuples"
if ! diff -u "$work/gnu.tuples" "$work/mini.tuples"; then
    echo 'GNU/MiniAS ELF32 PC-relative relocation mismatch' >&2
    echo 'GNU relocations:' >&2
    cat "$work/gnu.reloc" >&2
    echo 'MiniAS relocations:' >&2
    cat "$work/mini.reloc" >&2
    exit 1
fi
objcopy --dump-section .eh_frame="$work/gnu.eh.bin" "$work/gnu.o"
objcopy --dump-section .eh_frame="$work/mini.eh.bin" "$work/mini.o"
cmp "$work/gnu.eh.bin" "$work/mini.eh.bin"
objcopy --dump-section .data.rel.ro="$work/gnu.data.bin" "$work/gnu.o"
objcopy --dump-section .data.rel.ro="$work/mini.data.bin" "$work/mini.o"
cmp "$work/gnu.data.bin" "$work/mini.data.bin"
if readelf -rW "$work/mini.so" | grep -Eq 'R_386_(32|PC32)'; then
    echo 'MiniAS VDSO retained a dynamic relocation in shared .eh_frame' >&2
    exit 1
fi
echo 'MINIAS_X86_VDSO_EH=PASS GNU_shared_ELF32=1 MiniAS_shared_ELF32=1 dynamic_CFI_reloc=0'
