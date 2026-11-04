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

python3 - "$work" <<'PY'
from pathlib import Path
import subprocess
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
