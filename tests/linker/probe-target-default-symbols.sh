#!/usr/bin/env bash
# Separate target-owned psABI PROVIDE symbols from Common init/fini array
# boundaries. The four ELF64 static links use genuine MiniAS objects.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to the exact-head minic-as binary}"
: "${MINILD:?MINILD must point to the exact-head minic-ld binary}"
for tool in python3 readelf ld; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_DEFAULT_SYMBOLS=FAIL missing_tool=$tool" >&2
        exit 1
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-default-symbols.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

write_fixture() {
    local name=$1
    case "$name" in
        riscv64)
            printf '%s\n' \
                '.text' '.globl _start' '_start:' \
                '    addi a0,zero,0' '    addi a7,zero,93' '    ecall' \
                >"$work/$name.s"
            ;;
        x86_64)
            printf '%s\n' \
                '.text' '.globl _start' '_start:' \
                '    movq $60,%rax' '    xorq %rdi,%rdi' '    syscall' \
                >"$work/$name.s"
            ;;
        aarch64)
            printf '%s\n' \
                '.text' '.globl _start' '_start:' \
                '    mov x0,#0' '    mov x8,#93' '    svc #0' \
                >"$work/$name.s"
            ;;
        loongarch64)
            printf '%s\n' \
                '.text' '.globl _start' '_start:' \
                '    ori $a0,$zero,0' '    ori $a7,$zero,93' '    syscall 0' \
                >"$work/$name.s"
            ;;
        *) exit 2 ;;
    esac
    printf '%s\n' \
        '.data' '.globl fixture_data' 'fixture_data: .quad 0' \
        '.section .init_array,"aw",@init_array' \
        '    .p2align 3' '    .quad _start' \
        >>"$work/$name.s"
    if [[ $name == riscv64 ]]; then
        # GNU static libc references this linker-defined ELF-header address.
        # Retain the real R_RISCV_64 relocation through MiniAS and MiniLD.
        printf '%s\n' \
            '.data' '.globl rv64_ehdr_reference' \
            'rv64_ehdr_reference: .quad __ehdr_start' \
            >>"$work/$name.s"
    fi
}

for name in riscv64 x86_64 aarch64 loongarch64; do
    case "$name" in
        riscv64) target=riscv march=rv64i mabi=lp64 emulation=elf64lriscv ;;
        x86_64) target=x86_64 march=x86-64 mabi=sysv emulation=elf_x86_64 ;;
        aarch64) target=aarch64 march=armv8-a mabi=lp64 emulation=aarch64linux ;;
        loongarch64) target=loongarch64 march=loongarch64 mabi=lp64d emulation=elf64loongarch ;;
    esac
    write_fixture "$name"
    "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
        -o "$work/$name.o" "$work/$name.s"
    "$MINILD" -m "$emulation" -static -e _start \
        -o "$work/$name.elf" "$work/$name.o"
done

# GNU ld is an independent x86-64 reference for the special symbols. It may
# export ordinary _edata/_end markers, but not RISC-V's __global_pointer$.
ld -m elf_x86_64 -static -e _start -o "$work/x86_64-gnu.elf" \
    "$work/x86_64.o"

# The actual RV64 glibc linker-defined symbol is required for a non-scripted
# ET_EXEC.  Compare GNU BFD when installed; local structural validation still
# exercises the exact MiniLD ELF/PLOAD contract without a cross linker.
rv64_gnu_ld=${GNU_RISCV_LD:-riscv64-linux-gnu-ld.bfd}
if command -v "$rv64_gnu_ld" >/dev/null 2>&1; then
    "$rv64_gnu_ld" -m elf64lriscv -static -e _start \
        -o "$work/riscv64-gnu.elf" "$work/riscv64.o"
elif [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]]; then
    echo "MINILD_DEFAULT_SYMBOLS=FAIL missing_rv64_gnu_oracle=$rv64_gnu_ld" >&2
    exit 1
fi

python3 - "$work" <<'PY'
from pathlib import Path
import subprocess
import struct
import sys

root = Path(sys.argv[1])
riscv_only = ('__global_pointer$', '__DATA_BEGIN__',
              '__SDATA_BEGIN__', '__BSS_END__')


def symbols(path):
    output = subprocess.check_output(('readelf', '-W', '-s', str(path)), text=True)
    found = {}
    for line in output.splitlines():
        cols = line.split()
        if len(cols) < 8 or not cols[0].endswith(':'):
            continue
        try:
            value = int(cols[1], 16)
        except ValueError:
            continue
        found[cols[7]] = value
    return found


gnu = symbols(root / 'x86_64-gnu.elf')
if any(key in gnu for key in riscv_only):
    raise SystemExit('MINILD_DEFAULT_SYMBOLS=FAIL gnu_x86_contains_riscv_symbols')


def verify_rv64_ehdr(path):
    blob = path.read_bytes()
    if blob[:6] != b'\x7fELF\x02\x01':
        raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL invalid_rv64_elf={path}')
    ehdr = symbols(path)
    if '__ehdr_start' not in ehdr or 'rv64_ehdr_reference' not in ehdr:
        raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL missing_rv64_ehdr_symbol={path}')
    phoff = struct.unpack_from('<Q', blob, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', blob, 54)
    if phentsize != 56 or phoff < 64 or phnum == 0:
        raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL invalid_rv64_phdr={path}')
    loads = []
    for i in range(phnum):
        base = phoff + i * phentsize
        typ, flags, offset, vaddr, paddr, size, memsize, alignment = \
            struct.unpack_from('<IIQQQQQQ', blob, base)
        if typ == 1:
            loads.append((offset, vaddr, size, flags))
    header_loads = [x for x in loads if x[0] == 0 and
                    x[2] >= phoff + phnum * phentsize]
    if not header_loads or ehdr['__ehdr_start'] != header_loads[0][1]:
        raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL unbacked_rv64_ehdr={path}')
    ref_address = ehdr['rv64_ehdr_reference']
    mapped = [offset + ref_address - vaddr for offset, vaddr, size, _ in loads
              if vaddr <= ref_address and ref_address + 8 <= vaddr + size]
    if len(mapped) != 1 or mapped[0] + 8 > len(blob):
        raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL unmapped_ehdr_reference={path}')
    written = struct.unpack_from('<Q', blob, mapped[0])[0]
    if written != ehdr['__ehdr_start']:
        raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL wrong_ehdr_relocation={path}')
    return ehdr['__ehdr_start']


mini_ehdr = verify_rv64_ehdr(root / 'riscv64.elf')
gnu_rv64 = root / 'riscv64-gnu.elf'
if gnu_rv64.exists():
    # Each final linker owns its image base; the ABI contract is that the
    # symbol and relocated pointer match the *mapped* header of that output.
    # Requiring GNU/MiniLD to choose identical VMAs would reject legal ELF.
    verify_rv64_ehdr(gnu_rv64)
print(f'MINILD_RV64_EHDR_START=PASS mapped_header={mini_ehdr:#x} '
      f'gnu_bfd={"PASS" if gnu_rv64.exists() else "SKIP"}')

for name in ('riscv64', 'x86_64', 'aarch64', 'loongarch64'):
    found = symbols(root / (name + '.elf'))
    if any(key not in found for key in ('__init_array_start',
                                        '__init_array_end')):
        raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL missing_common_arrays={name}')
    if found['__init_array_end'] - found['__init_array_start'] != 8:
        raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL wrong_array_range={name}')
    actual = [key for key in riscv_only if key in found]
    if name == 'riscv64':
        if len(actual) != len(riscv_only):
            raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL missing_riscv_symbols={actual}')
    elif actual:
        raise SystemExit(f'MINILD_DEFAULT_SYMBOLS=FAIL leaked_riscv_symbols={name}:{actual}')
    print(f'MINILD_DEFAULT_SYMBOLS_TARGET=PASS target={name} '
          f'riscv_psabi={"provided" if name == "riscv64" else "absent"} '
          'common_init_array=provided')

print('MINILD_DEFAULT_SYMBOLS=PASS targets=4 common_arrays=4 '
      'riscv_specific_symbols=4 x86_gnu_differential=PASS')
PY
