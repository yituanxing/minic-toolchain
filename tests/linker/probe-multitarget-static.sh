#!/usr/bin/env bash
# The first cross-target final-link milestone: genuine ELF64 ET_EXEC from
# x86-64, AArch64 and LoongArch64 MiniAS objects.  Common output owns PT_LOAD,
# sections and symbols; each target must resolve its own absolute relocation.
# x86-64 additionally proves a real Linux syscall and GNU ld runtime parity.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to the exact-head minic-as binary}"
: "${MINILD:?MINILD must point to the exact-head minic-ld binary}"
for tool in python3 readelf ld timeout; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_MULTITARGET_STATIC=FAIL missing_tool=$tool" >&2
        exit 1
    }
done
ld --version | grep -q 'GNU ld' || {
    echo 'MINILD_MULTITARGET_STATIC=FAIL non_gnu_ld_on_path' >&2
    exit 1
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-multitarget-static.XXXXXX")
cleanup() {
    if [[ ${MINILD_MULTITARGET_STATIC_KEEP:-0} == 1 ]]; then
        echo "MINILD_MULTITARGET_STATIC_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

assemble_static_pair() {
    local name=$1 target=$2 march=$3 mabi=$4 emulation=$5

    case "$name" in
        x86_64)
            # This R_X86_64_PC32 cross-object reference is exercised at
            # runtime.  R_X86_64_64 in .data is separately checked as a
            # patched absolute VA, so two relocation classes are covered.
            printf '%s\n' \
                '.text' \
                '.globl _start' \
                '.type _start,@function' \
                '_start:' \
                '    movq $1,%rax' \
                '    movq $1,%rdi' \
                '    leaq fixture_target(%rip),%rsi' \
                '    movq $21,%rdx' \
                '    syscall' \
                '    movq $60,%rax' \
                '    xorq %rdi,%rdi' \
                '    syscall' \
                '.size _start,.-_start' >"$work/$name-entry.s"
            ;;
        aarch64)
            printf '%s\n' \
                '.text' \
                '.globl _start' \
                '.type _start,@function' \
                '_start:' \
                '    bl fixture_helper' \
                '.size _start,.-_start' >"$work/$name-entry.s"
            ;;
        loongarch64)
            printf '%s\n' \
                '.text' \
                '.globl _start' \
                '.type _start,@function' \
                '_start:' \
                '    bl fixture_helper' \
                '.size _start,.-_start' >"$work/$name-entry.s"
            ;;
        *) echo "unsupported fixture target: $name" >&2; exit 1 ;;
    esac

    printf '%s\n' \
        '.data' \
        '.p2align 3' \
        '.globl fixture_ptr' \
        '.type fixture_ptr,@object' \
        'fixture_ptr:' \
        '    .quad fixture_target' \
        '.size fixture_ptr,8' >>"$work/$name-entry.s"
    case "$name" in
        aarch64)
            printf '%s\n' \
                '.text' \
                '.globl fixture_helper' \
                '.type fixture_helper,@function' \
                'fixture_helper:' \
                '    mov x0,#0' \
                '    mov x8,#93' \
                '    svc #0' \
                '.size fixture_helper,.-fixture_helper' \
                >"$work/$name-data.s"
            ;;
        loongarch64)
            printf '%s\n' \
                '.text' \
                '.globl fixture_helper' \
                '.type fixture_helper,@function' \
                'fixture_helper:' \
                '    ori $a0,$zero,0' \
                '    ori $a7,$zero,93' \
                '    syscall 0' \
                '.size fixture_helper,.-fixture_helper' \
                >"$work/$name-data.s"
            ;;
        x86_64) : >"$work/$name-data.s" ;;
    esac
    printf '%s\n' \
        '.section .rodata,"a",@progbits' \
        '.globl fixture_target' \
        '.type fixture_target,@object' \
        'fixture_target:' \
        '    .ascii "MINILD_STATIC_X86_OK\n"' \
        '.size fixture_target,21' >>"$work/$name-data.s"

    for part in entry data; do
        "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
            -o "$work/$name-$part.o" "$work/$name-$part.s"
    done
    case "$name" in
        aarch64)
            readelf -Wr "$work/$name-entry.o" |
                grep -Eq 'R_AARCH64_CALL26[[:space:]]+.*fixture_helper' || {
                echo 'MINILD_MULTITARGET_STATIC=FAIL missing_aarch64_call26' >&2
                exit 1
            }
            ;;
        loongarch64)
            readelf -Wr "$work/$name-entry.o" |
                grep -Eq 'R_LARCH_B26[[:space:]]+.*fixture_helper' || {
                echo 'MINILD_MULTITARGET_STATIC=FAIL missing_loongarch_b26' >&2
                exit 1
            }
            ;;
    esac
    "$MINILD" -m "$emulation" -static -e _start \
        -o "$work/$name-minild.elf" \
        "$work/$name-entry.o" "$work/$name-data.o"
}

assemble_static_pair x86_64 x86_64 x86-64 sysv elf_x86_64
assemble_static_pair aarch64 aarch64 armv8-a lp64 aarch64linux
assemble_static_pair loongarch64 loongarch64 loongarch64 lp64d elf64loongarch

# Same producer inputs, independent GNU final-link reference.  Machine code
# bytes or virtual addresses may differ with layout, but the guest-visible
# behavior, symbols, relocation value and loadability must not.
ld -m elf_x86_64 -static -e _start -o "$work/x86_64-gnu.elf" \
    "$work/x86_64-entry.o" "$work/x86_64-data.o"

python3 - "$work" <<'PY'
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])


def ensure(condition, detail):
    if not condition:
        raise SystemExit(f'MINILD_MULTITARGET_STATIC=FAIL {detail}')


class Elf64:
    def __init__(self, path):
        self.path = path
        self.data = path.read_bytes()
        data = self.data
        ensure(len(data) >= 64 and data[:6] == b'\x7fELF\x02\x01',
               f'bad_elf64le={path.name}')
        self.kind, self.machine = struct.unpack_from('<HH', data, 16)
        self.entry = struct.unpack_from('<Q', data, 24)[0]
        phoff, shoff = struct.unpack_from('<QQ', data, 32)
        self.flags = struct.unpack_from('<I', data, 48)[0]
        phentsize, phnum = struct.unpack_from('<HH', data, 54)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 58)
        if self.kind == 1:  # ET_REL source objects have no program headers.
            ensure(phnum == 0 and phoff == 0,
                   f'unexpected_relocatable_program_headers={path.name}')
            self.programs = []
        else:
            ensure(phentsize == 56 and phnum > 0 and
                   phoff + phentsize * phnum <= len(data),
                   f'bad_program_headers={path.name}')
            self.programs = [struct.unpack_from('<IIQQQQQQ', data, phoff + i * 56)
                             for i in range(phnum)]
        ensure(shentsize == 64 and 0 < shstrndx < shnum and
               shoff + shnum * 64 <= len(data),
               f'bad_section_headers={path.name}')
        self.sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * 64)
                         for i in range(shnum)]
        names = self.section_data(shstrndx)
        self.names = [self.cstr(names, s[0]) for s in self.sections]
        self.symbols = {}
        for i, section in enumerate(self.sections):
            if section[1] != 2:  # SHT_SYMTAB
                continue
            ensure(section[6] < shnum, f'bad_symtab_link={path.name}')
            strings = self.section_data(section[6])
            symbols = self.section_data(i)
            ensure(section[9] == 24 and len(symbols) % 24 == 0,
                   f'bad_symtab={path.name}')
            for off in range(0, len(symbols), 24):
                name, info, other, sec, value, size = struct.unpack_from(
                    '<IBBHQQ', symbols, off)
                if name:
                    self.symbols[self.cstr(strings, name)] = (sec, value, size)

    @staticmethod
    def cstr(data, index):
        ensure(index < len(data), 'invalid_string_offset')
        end = data.find(b'\0', index)
        ensure(end >= 0, 'unterminated_string')
        return data[index:end].decode('utf-8', 'replace')

    def section_data(self, index):
        section = self.sections[index]
        offset, size = section[4:6]
        ensure(offset + size <= len(self.data),
               f'bad_section_data={self.path.name}')
        return self.data[offset:offset + size]

    def read_symbol_qword(self, name):
        section_index, va, _ = self.symbols[name]
        ensure(0 < section_index < len(self.sections),
               f'bad_symbol_section={self.path.name}:{name}')
        section = self.sections[section_index]
        section_va, section_offset, section_size = section[3:6]
        delta = va - section_va
        ensure(0 <= delta <= section_size - 8,
               f'bad_symbol_extent={self.path.name}:{name}')
        return struct.unpack_from('<Q', self.data, section_offset + delta)[0]

    def read_symbol_word(self, name):
        section_index, va, _ = self.symbols[name]
        ensure(0 < section_index < len(self.sections),
               f'bad_symbol_section={self.path.name}:{name}')
        section = self.sections[section_index]
        section_va, section_offset, section_size = section[3:6]
        delta = va - section_va
        ensure(0 <= delta <= section_size - 4,
               f'bad_symbol_word_extent={self.path.name}:{name}')
        return struct.unpack_from('<I', self.data, section_offset + delta)[0]


for name, machine, flags in (('x86_64', 62, 0),
                             ('aarch64', 183, 0),
                             ('loongarch64', 258, 0x43)):
    elf = Elf64(root / f'{name}-minild.elf')
    ensure((elf.kind, elf.machine, elf.flags) == (2, machine, flags),
           f'bad_exec_identity={name} kind={elf.kind} '
           f'machine={elf.machine} flags={elf.flags:#x}')
    ensure(elf.entry > 0 and any(p[0] == 1 and p[5] > 0 for p in elf.programs),
           f'missing_loadable_entry={name}')
    ensure(all(s[1] != 4 or s[5] == 0 for s in elf.sections),
           f'unresolved_relocations={name}')
    for symbol in ('_start', 'fixture_ptr', 'fixture_target'):
        ensure(symbol in elf.symbols and 0 < elf.symbols[symbol][0] <
               len(elf.sections), f'missing_symbol={name}:{symbol}')
    ensure(elf.entry == elf.symbols['_start'][1],
           f'entry_mismatch={name}')
    ensure(elf.read_symbol_qword('fixture_ptr') ==
           elf.symbols['fixture_target'][1],
           f'absolute_relocation_not_applied={name}')
    if name in ('aarch64', 'loongarch64'):
        ensure('fixture_helper' in elf.symbols,
               f'missing_cross_object_helper={name}')
        word = elf.read_symbol_word('_start')
        if name == 'aarch64':
            ensure((word & 0xfc000000) == 0x94000000,
                   f'bad_aarch64_bl_opcode={word:#x}')
            units = word & 0x03ffffff
        else:
            ensure((word & 0xfc000000) == 0x54000000,
                   f'bad_loongarch_bl_opcode={word:#x}')
            units = ((word >> 10) & 0xffff) | ((word & 0x3ff) << 16)
        if units & (1 << 25):
            units -= 1 << 26
        branch_destination = elf.symbols['_start'][1] + units * 4
        ensure(branch_destination == elf.symbols['fixture_helper'][1],
               f'cross_object_branch_not_applied={name} '
               f'actual={branch_destination:#x} '
               f'expected={elf.symbols["fixture_helper"][1]:#x}')
    print(f'MINILD_MULTITARGET_STATIC_STRUCTURE=PASS target={name} '
          f'machine={machine} ptr_relocation=resolved '
          f'branch_relocation={"resolved" if name != "x86_64" else "runtime"} '
          f'PT_LOAD=yes')

gnu = Elf64(root / 'x86_64-gnu.elf')
ensure(gnu.kind == 2 and gnu.machine == 62 and
       gnu.read_symbol_qword('fixture_ptr') ==
       gnu.symbols['fixture_target'][1],
       'gnu_reference_incorrect')


# Mutate a *real*, MiniAS-emitted R_*_64 relocation instead of inventing a
# fake ELF. A unknown machine relocation must fail closed before publication.
for name in ('x86_64', 'aarch64', 'loongarch64'):
    original = Elf64(root / f'{name}-entry.o')
    updated = bytearray(original.data)
    candidates = [s for s in original.sections if s[1] == 4 and s[5] >= 24]
    ensure(candidates, f'missing_source_relocation={name}')
    relocation = candidates[0]
    info_offset = relocation[4] + 8
    old_info = struct.unpack_from('<Q', updated, info_offset)[0]
    ensure(old_info >> 32 != 0, f'missing_source_reloc_symbol={name}')
    struct.pack_into('<Q', updated, info_offset,
                     (old_info & 0xffffffff00000000) | 0xf0000001)
    (root / f'{name}-unknown-reloc.o').write_bytes(updated)
print('MINILD_MULTITARGET_STATIC_NEGATIVE_FIXTURES=PASS unknown_types=3')
PY

for name in x86_64 aarch64 loongarch64; do
    case "$name" in
        x86_64) emulation=elf_x86_64 ;;
        aarch64) emulation=aarch64linux ;;
        loongarch64) emulation=elf64loongarch ;;
    esac
    output="$work/$name-unknown-should-not-exist.elf"
    if "$MINILD" -m "$emulation" -static -e _start -o "$output" \
        "$work/$name-unknown-reloc.o" "$work/$name-data.o" \
        >"$work/$name-unknown.stdout" 2>"$work/$name-unknown.stderr"; then
        echo "MINILD_MULTITARGET_STATIC=FAIL accepted_unknown_reloc=$name" >&2
        exit 1
    fi
    if [[ -e "$output" || ! -s "$work/$name-unknown.stderr" ]]; then
        echo "MINILD_MULTITARGET_STATIC=FAIL published_or_silent_unknown=$name" >&2
        exit 1
    fi
done

if [[ $(uname -m) != x86_64 ]]; then
    echo 'MINILD_MULTITARGET_STATIC=INCONCLUSIVE x86_native_runtime_requires_x86_64' >&2
    exit 8
fi
timeout 5s "$work/x86_64-minild.elf" >"$work/x86-minild.stdout"
timeout 5s "$work/x86_64-gnu.elf" >"$work/x86-gnu.stdout"
printf 'MINILD_STATIC_X86_OK\n' | cmp -s - "$work/x86-minild.stdout" || {
    echo 'MINILD_MULTITARGET_STATIC=FAIL x86_native_output_mismatch' >&2
    exit 1
}
cmp "$work/x86-minild.stdout" "$work/x86-gnu.stdout" || {
    echo 'MINILD_MULTITARGET_STATIC=FAIL x86_gnu_runtime_differential' >&2
    exit 1
}
echo 'MINILD_MULTITARGET_STATIC_RUNTIME=PASS target=x86_64 native_syscall=1 gnu_ld=equal'

for name in aarch64 loongarch64; do
    case "$name" in
        aarch64) qemu=${QEMU_AARCH64:-qemu-aarch64} ;;
        loongarch64) qemu=${QEMU_LOONGARCH64:-qemu-loongarch64} ;;
    esac
    if ! command -v "$qemu" >/dev/null 2>&1; then
        if [[ ${MINILD_REQUIRE_QEMU:-0} == 1 ]]; then
            echo "MINILD_MULTITARGET_STATIC=FAIL required_qemu_missing=$name:$qemu" >&2
            exit 1
        fi
        echo "MINILD_MULTITARGET_STATIC_RUNTIME=UNVERIFIED target=$name reason=missing_qemu"
        continue
    fi
    timeout 5s "$qemu" "$work/$name-minild.elf" \
        >"$work/$name-qemu.stdout" 2>"$work/$name-qemu.stderr" || {
        echo "MINILD_MULTITARGET_STATIC=FAIL qemu_runtime=$name" >&2
        exit 1
    }
    test ! -s "$work/$name-qemu.stdout"
    echo "MINILD_MULTITARGET_STATIC_RUNTIME=PASS target=$name qemu_exit=0"
done

echo 'MINILD_MULTITARGET_STATIC=PASS structures=3 absolute_relocs=3 branch_relocs=2 unknown_reloc_rejections=3 x86_native_gnu=PASS non_x86_runtime=separately_reported'
