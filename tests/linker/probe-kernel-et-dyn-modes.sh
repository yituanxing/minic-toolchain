#!/usr/bin/env bash
# Isolated, exact-head Linux Kbuild link-mode evidence. This is deliberately
# NOT a vmlinux PASS claim: full linker scripts and emitted dynamic relocations
# require separate GNU-vs-MiniLD replay before promotion.
set -Eeuo pipefail

: "${MINIAS:?set MINIAS to exact-head minic-as}"
: "${MINILD:?set MINILD to exact-head minic-ld}"
command -v python3 >/dev/null

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-kernel-et-dyn.XXXXXX")
finish() {
    if [[ ${MINILD_KERNEL_ET_DYN_KEEP:-0} == 1 ]]; then
        echo "MINILD_KERNEL_ET_DYN_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

die() { echo "MINILD_KERNEL_ET_DYN=FAIL arch=$arch stage=$1" >&2; exit 1; }

check_elf() {
    python3 - "$1" "$2" "$3" <<'PY'
import struct
import sys
from pathlib import Path

path, machine, mode = sys.argv[1:]
image = Path(path).read_bytes()
if image[:4] != b'\x7fELF' or image[5] != 1:
    raise SystemExit(f'MINILD_KERNEL_ET_DYN=FAIL invalid_elf:{path}')
elf_class = image[4]
elf_type, elf_machine = struct.unpack_from('<HH', image, 16)
expected_type = 3 if mode == 'dyn' else 2
if elf_type != expected_type or elf_machine != int(machine):
    raise SystemExit(f'MINILD_KERNEL_ET_DYN=FAIL wrong_identity:{path}')
if elf_class == 2:
    phoff = struct.unpack_from('<Q', image, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', image, 54)
elif elf_class == 1:
    phoff = struct.unpack_from('<I', image, 28)[0]
    phentsize, phnum = struct.unpack_from('<HH', image, 42)
else:
    raise SystemExit(f'MINILD_KERNEL_ET_DYN=FAIL wrong_class:{path}')
if phnum == 0 or phoff + phentsize * phnum > len(image):
    raise SystemExit(f'MINILD_KERNEL_ET_DYN=FAIL malformed_phdr:{path}')
types = [struct.unpack_from('<I', image, phoff + phentsize * i)[0]
         for i in range(phnum)]
if 1 not in types or (mode == 'dyn' and (2 in types or 3 in types)):
    raise SystemExit(f'MINILD_KERNEL_ET_DYN=FAIL wrong_program_headers:{path}:{types}')
PY
}

for spec in \
    'rv32:riscv:rv32i:ilp32:elf32lriscv:riscv:243:0x10000' \
    'rv64:riscv:rv64i:lp64:elf64lriscv:riscv:243:0x10000' \
    'x86_64:x86_64:x86-64:sysv:elf_x86_64:i386:x86-64:62:0x400000' \
    'aarch64:aarch64:armv8-a:lp64:aarch64elf:aarch64:183:0x400000' \
    'loongarch64:loongarch64:loongarch64:lp64d:elf64loongarch:loongarch64:258:0x120010000'; do
    # The x86 GNU OUTPUT_ARCH spelling includes ':', so parse fields from
    # both ends instead of splitting on the embedded architecture separator.
    IFS=: read -r arch target march mabi emulation rest <<<"$spec"
    base=${spec##*:}
    without_base=${spec%:*}
    machine=${without_base##*:}
    arch_name=${without_base%:*}
    arch_name=${arch_name#"$arch:$target:$march:$mabi:$emulation:"}
    mkdir -p "$work/$arch"
    printf '%s\n' \
        '.section .text.entry,"ax",@progbits' \
        '.globl _start' \
        '_start:' \
        '    .byte 0,0,0,0' >"$work/$arch/entry.s"
    pointer_directive=.quad
    [[ $arch != rv32 ]] || pointer_directive=.long
    printf '%s\n' \
        '.section .data.ptr,"aw",@progbits' \
        '.globl ptr_to_start' \
        'ptr_to_start:' \
        "    $pointer_directive _start" >"$work/$arch/pointer.s"
    printf '%s\n' \
        "OUTPUT_ARCH($arch_name)" \
        'ENTRY(_start)' \
        'SECTIONS {' \
        "  . = $base;" \
        '  .text : { *(.text.entry) }' \
        '  . = ALIGN(0x1000);' \
        '  .rela.dyn : { __rela_start = .; *(.rela .rela*); __rela_end = .; }' \
        '  .data : { *(.data.ptr) }' \
        '}' >"$work/$arch/vmlinux.lds"

    "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
        -o "$work/$arch/entry.o" "$work/$arch/entry.s" || die assemble_entry
    "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
        -o "$work/$arch/pointer.o" "$work/$arch/pointer.s" || die assemble_pointer

    "$MINILD" -m "$emulation" -static -T "$work/$arch/vmlinux.lds" \
        -o "$work/$arch/static.elf" \
        "$work/$arch/entry.o" "$work/$arch/pointer.o" \
        >"$work/$arch/static.log" 2>&1 || die script_static
    check_elf "$work/$arch/static.elf" "$machine" exec

    case "$arch" in
        rv64|loongarch64)
            "$MINILD" -m "$emulation" -static -pie -n -X \
                --no-dynamic-linker -z notext -T "$work/$arch/vmlinux.lds" \
                -o "$work/$arch/kernel.elf" "$work/$arch/entry.o" \
                >"$work/$arch/kernel.log" 2>&1 || die kernel_static_pie
            # GNU ld -n overrides the PIE ELF identity to ET_EXEC while
            # retaining the admitted static-kernel linker mode.
            check_elf "$work/$arch/kernel.elf" "$machine" exec
            "$MINILD" -m "$emulation" -static -pie -X \
                --no-dynamic-linker -z notext -T "$work/$arch/vmlinux.lds" \
                -o "$work/$arch/kernel-pie.elf" "$work/$arch/entry.o" \
                >"$work/$arch/kernel-pie.log" 2>&1 || die kernel_without_nmagic
            check_elf "$work/$arch/kernel-pie.elf" "$machine" dyn
            if [[ $arch == loongarch64 ]]; then
                # The actual Linux 6.6.143 script spells the GNU output
                # architecture "loongarch", not our ABI-width alias.
                sed 's/OUTPUT_ARCH(loongarch64)/OUTPUT_ARCH(loongarch)/' \
                    "$work/$arch/vmlinux.lds" >"$work/$arch/kernel-gnu-arch.lds"
                "$MINILD" -m "$emulation" -static -pie -n -X \
                    --no-dynamic-linker -z notext \
                    -T "$work/$arch/kernel-gnu-arch.lds" \
                    -o "$work/$arch/kernel-gnu-arch.elf" \
                    "$work/$arch/entry.o" \
                    >"$work/$arch/kernel-gnu-arch.log" 2>&1 ||
                    die kernel_gnu_output_arch
                check_elf "$work/$arch/kernel-gnu-arch.elf" "$machine" exec
            fi
            ;;
        aarch64)
            "$MINILD" -m "$emulation" -shared -Bsymbolic \
                --no-apply-dynamic-relocs --pic-veneer -z notext \
                -T "$work/$arch/vmlinux.lds" \
                -o "$work/$arch/kernel.elf" "$work/$arch/entry.o" \
                >"$work/$arch/kernel.log" 2>&1 || die kernel_shared
            check_elf "$work/$arch/kernel.elf" "$machine" dyn
            "$MINILD" -m "$emulation" -shared -Bsymbolic \
                    --no-apply-dynamic-relocs -T "$work/$arch/vmlinux.lds" \
                    -o "$work/$arch/kernel-relative.elf" \
                    "$work/$arch/entry.o" "$work/$arch/pointer.o" \
                    >"$work/$arch/kernel-relative.log" 2>&1 ||
                die kernel_relative_conversion
            check_elf "$work/$arch/kernel-relative.elf" "$machine" dyn
            python3 - "$work/$arch/kernel-relative.elf" "$base" <<'PY_RELATIVE'
import struct,sys
b=open(sys.argv[1],'rb').read()
base=int(sys.argv[2],0)
shoff=struct.unpack_from('<Q',b,40)[0]
shentsize,shnum,shstridx=struct.unpack_from('<HHH',b,58)
headers=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+i*shentsize)
         for i in range(shnum)]
names=headers[shstridx]
strtab=b[names[4]:names[4]+names[5]]
def section_name(header):
    start=header[0]
    return strtab[start:strtab.find(b'\0',start)].decode()
sections={section_name(h):h for h in headers}
assert '.rela.dyn' in sections and '.data' in sections
rela=sections['.rela.dyn']
assert rela[1]==4 and rela[5]==24, 'expected exactly one RELA'
roff,info,addend=struct.unpack_from('<QQq',b,rela[4])
assert info & 0xffffffff == 1027 and info>>32==0, 'R_AARCH64_RELATIVE'
data=sections['.data']
assert roff==data[3], 'r_offset must address pointer slot'
assert addend==base, 'r_addend must be the image target address'
assert b[data[4]:data[4]+8]==b'\0'*8, '--no-apply must keep original data bytes'
print('MINILD_KERNEL_ET_DYN=PASS kernel_AARCH64_RELATIVE=1 deferred_input_slot=1')
PY_RELATIVE
            ;;
    esac
    case "$arch" in
        rv64|loongarch64)
            echo "MINILD_KERNEL_ET_DYN=PASS arch=$arch script_ET_EXEC=PASS nmagic_ET_EXEC=PASS static_PIE_ET_DYN=PASS"
            ;;
        aarch64)
            echo "MINILD_KERNEL_ET_DYN=PASS arch=$arch script_ET_EXEC=PASS shared_kernel_ET_DYN=PASS"
            ;;
        *)
            echo "MINILD_KERNEL_ET_DYN=PASS arch=$arch script_ET_EXEC=PASS kernel_mode=NOT_APPLICABLE"
            ;;
    esac
done

# A Linux-kernel profile cannot silently become a general-purpose PIE/DSO.
arch=rv64
if "$MINILD" -m elf64lriscv -pie -T "$work/rv64/vmlinux.lds" \
        -o "$work/rv64/ordinary-pie.elf" "$work/rv64/entry.o" \
        >"$work/rv64/ordinary-pie.log" 2>&1; then
    die ordinary_pie_script_accepted
fi
if "$MINILD" -m elf64lriscv -shared -Bsymbolic \
        --no-apply-dynamic-relocs -T "$work/rv64/vmlinux.lds" \
        -o "$work/rv64/ordinary-shared.elf" "$work/rv64/entry.o" \
        >"$work/rv64/ordinary-shared.log" 2>&1; then
    die non_aarch_kernel_shared_accepted
fi
echo 'MINILD_KERNEL_ET_DYN=COMPLETE script_all_targets=5/5 kernel_modes=3/3 aarch64_runtime_rebase=RELA'
