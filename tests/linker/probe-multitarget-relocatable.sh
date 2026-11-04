#!/usr/bin/env bash
# Five Linux ELF targets must use one real MiniLD -r pipeline.  Every input
# comes from the repository's own MiniAS, so a missing cross-GNU toolchain
# cannot turn an unsupported target into a skipped test.  The x86-64 output
# is additionally compared semantically with GNU ld -r on identical inputs.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to the exact-head minic-as executable}"
: "${MINILD:?MINILD must point to the exact-head minic-ld executable}"
command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_MULTITARGET_REL=FAIL missing_tool=python3' >&2
    exit 1
}
command -v ld >/dev/null 2>&1 || {
    echo 'MINILD_MULTITARGET_REL=FAIL missing_tool=gnu-ld' >&2
    exit 1
}
ld --version | grep -q 'GNU ld' || {
    echo 'MINILD_MULTITARGET_REL=FAIL non_gnu_ld_on_path' >&2
    exit 1
}
command -v as >/dev/null 2>&1 || {
    echo 'MINILD_MULTITARGET_REL=FAIL missing_tool=gnu-as' >&2
    exit 1
}
as --version | grep -q 'GNU assembler' || {
    echo 'MINILD_MULTITARGET_REL=FAIL non_gnu_as_on_path' >&2
    exit 1
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-multitarget-rel.XXXXXX")
cleanup() {
    if [[ ${MINILD_MULTITARGET_KEEP:-0} == 1 ]]; then
        echo "MINILD_MULTITARGET_REL_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

assemble_pair() {
    local name=$1 target=$2 march=$3 mabi=$4 emulation=$5 pointer=$6
    local alignment=3
    if [[ $pointer == .long ]]; then
        alignment=2
    fi

    # The two cross-object references must survive as genuine ELF RELA
    # records.  Executable bytes are also emitted by each actual ISA parser.
    # The linker is tested on both source object identities, not handcrafted
    # ELF headers that bypass the assembler/ABI contract.
    printf '%s\n' \
        '.text' \
        '.globl entry_a' \
        '.type entry_a,@function' \
        'entry_a:' \
        '    nop' \
        '.size entry_a,.-entry_a' \
        '.data' \
        ".p2align $alignment" \
        '.globl ref_a' \
        'ref_a:' \
        "    $pointer entry_b" >"$work/$name-a.s"
    printf '%s\n' \
        '.text' \
        '.globl entry_b' \
        '.type entry_b,@function' \
        'entry_b:' \
        '    nop' \
        '.size entry_b,.-entry_b' \
        '.data' \
        ".p2align $alignment" \
        '.globl ref_b' \
        'ref_b:' \
        "    $pointer entry_a" >"$work/$name-b.s"

    for side in a b; do
        "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
            -o "$work/$name-$side.o" "$work/$name-$side.s"
    done
    "$MINILD" -m "$emulation" -r -o "$work/$name-linked.o" \
        "$work/$name-a.o" "$work/$name-b.o"
}

assemble_pair x86_64 x86_64 x86-64 sysv elf_x86_64 .quad
assemble_pair aarch64 aarch64 armv8-a lp64 aarch64linux .quad
assemble_pair loongarch64 loongarch64 loongarch64 lp64d elf64loongarch .quad
assemble_pair riscv64 riscv rv64i lp64 elf64lriscv .quad
assemble_pair riscv32 riscv rv32i ilp32 elf32lriscv .long

# A target emulation must reject a real object of another ELF machine or
# class, never stamp the requested e_machine onto incompatible input or
# publish partial output.  Include mixed-input graphs, not merely one alien
# object, to exercise input admission after earlier valid inputs were read.
rejections=0
expect_reject() {
    local name=$1
    shift
    local output="$work/rejected-$name.o"
    if "$MINILD" "$@" -o "$output" \
        >"$work/rejected-$name.stdout" \
        2>"$work/rejected-$name.stderr"; then
        echo "MINILD_MULTITARGET_REL=FAIL accepted_invalid_case=$name" >&2
        exit 1
    fi
    if [[ -s "$output" ]]; then
        echo "MINILD_MULTITARGET_REL=FAIL published_invalid_output=$name" >&2
        exit 1
    fi
    if [[ ! -s "$work/rejected-$name.stderr" ]]; then
        echo "MINILD_MULTITARGET_REL=FAIL silent_invalid_case=$name" >&2
        exit 1
    fi
    rejections=$((rejections + 1))
}

for target in aarch64linux elf64loongarch elf64lriscv elf32lriscv; do
    expect_reject "foreign-x86-$target" -m "$target" -r "$work/x86_64-a.o"
done
expect_reject mixed-x86-a64 -m elf_x86_64 -r \
    "$work/x86_64-a.o" "$work/aarch64-b.o"
expect_reject mixed-a64-la64 -m aarch64linux -r \
    "$work/aarch64-a.o" "$work/loongarch64-b.o"
expect_reject mixed-la64-rv64 -m elf64loongarch -r \
    "$work/loongarch64-a.o" "$work/riscv64-b.o"
expect_reject mixed-rv64-rv32 -m elf64lriscv -r \
    "$work/riscv64-a.o" "$work/riscv32-b.o"
expect_reject mixed-rv32-x86 -m elf32lriscv -r \
    "$work/riscv32-a.o" "$work/x86_64-b.o"

# RV32 and RV64 share EM_RISCV=243 but are NOT interchangeable ELF classes.
# Check both input orders, so neither first-object nor later-object admission
# can choose a bogus output ABI.
expect_reject rv32-in-rv64 -m elf64lriscv -r \
    "$work/riscv64-a.o" "$work/riscv32-b.o"
expect_reject rv64-in-rv32 -m elf32lriscv -r \
    "$work/riscv32-a.o" "$work/riscv64-b.o"
expect_reject unknown-emulation -m elf_unknown_linux -r \
    "$work/x86_64-a.o"

# The next milestone adds ET_EXEC for three ELF64 targets.  An accepted -m
# flag must not accidentally fall through into RV64 shared/PIE output.
# The special RV32 Linux compat-vDSO shared contract is not general -shared.
for target in x86_64 aarch64 loongarch64 riscv32; do
    case "$target" in
        x86_64) emulation=elf_x86_64 ;;
        aarch64) emulation=aarch64linux ;;
        loongarch64) emulation=elf64loongarch ;;
        riscv32) emulation=elf32lriscv ;;
    esac
    for mode in -shared -pie; do
        expect_reject "unsupported-${target}-${mode#-}" \
            -m "$emulation" "$mode" "$work/$target-a.o"
    done
done
test "$rejections" -eq 20

ld -m elf_x86_64 -r -o "$work/x86_64-gnu.o" \
    "$work/x86_64-a.o" "$work/x86_64-b.o"

# An independent GNU assembler must produce valid input for the very same
# MiniLD x86 target.  Mix GNU-AS and MiniAS objects in one -r invocation,
# then compare that entire linked graph against genuine GNU ld -r output.
printf '%s\n' \
    '.text' \
    '.globl gnu_entry' \
    '.type gnu_entry,@function' \
    'gnu_entry:' \
    '    nop' \
    '.size gnu_entry,.-gnu_entry' \
    '.data' \
    '.p2align 3' \
    '.globl gnu_ref' \
    'gnu_ref:' \
    '    .quad entry_b' >"$work/x86_64-gnu-source.s"
as --64 -o "$work/x86_64-gnu-source.o" "$work/x86_64-gnu-source.s"
"$MINILD" -m elf_x86_64 -r -o "$work/x86_64-mixed-producer.o" \
    "$work/x86_64-a.o" "$work/x86_64-b.o" "$work/x86_64-gnu-source.o"
ld -m elf_x86_64 -r -o "$work/x86_64-mixed-producer-gnu.o" \
    "$work/x86_64-a.o" "$work/x86_64-b.o" "$work/x86_64-gnu-source.o"

python3 - "$work" <<'PY'
from pathlib import Path
import struct
import sys


root = Path(sys.argv[1])


def ensure(condition, detail):
    if not condition:
        raise SystemExit(f'MINILD_MULTITARGET_REL=FAIL {detail}')


class Elf:
    def __init__(self, path):
        self.path = path
        data = path.read_bytes()
        self.data = data
        ensure(len(data) >= 52 and data[:4] == b'\x7fELF',
               f'not_elf={path.name}')
        self.bits = {1: 32, 2: 64}.get(data[4])
        ensure(self.bits is not None and data[5] == 1,
               f'not_supported_little_endian={path.name}')
        self.kind, self.machine = struct.unpack_from('<HH', data, 16)
        if self.bits == 64:
            shoff = struct.unpack_from('<Q', data, 40)[0]
            self.flags = struct.unpack_from('<I', data, 48)[0]
            entsize, count, strings = struct.unpack_from('<HHH', data, 58)
            shape, size = '<IIQQQQIIQQ', 64
        else:
            shoff = struct.unpack_from('<I', data, 32)[0]
            self.flags = struct.unpack_from('<I', data, 36)[0]
            entsize, count, strings = struct.unpack_from('<HHH', data, 46)
            shape, size = '<IIIIIIIIII', 40
        ensure(entsize == size and count > 0 and strings < count and
               shoff + count * size <= len(data),
               f'bad_sections={path.name}')
        self.sections = [struct.unpack_from(shape, data, shoff + i * size)
                         for i in range(count)]
        names = self.section_data(strings)
        self.names = [self.cstr(names, s[0]) for s in self.sections]
        self.symbol_tables = {}
        self.named_symbols = {}
        for index, section in enumerate(self.sections):
            if section[1] != 2:  # SHT_SYMTAB
                continue
            ensure(section[6] < count, f'bad_symtab_link={path.name}')
            strings_data = self.section_data(section[6])
            symdata = self.section_data(index)
            entry_size = 24 if self.bits == 64 else 16
            ensure(section[9] == entry_size and
                   len(symdata) % entry_size == 0,
                   f'bad_symtab_shape={path.name}')
            table = []
            for offset in range(0, len(symdata), entry_size):
                if self.bits == 64:
                    n, info, other, shndx, value, sym_size = struct.unpack_from(
                        '<IBBHQQ', symdata, offset)
                else:
                    n, value, sym_size, info, other, shndx = struct.unpack_from(
                        '<IIIBBH', symdata, offset)
                symbol = (self.cstr(strings_data, n), shndx, value,
                          info, other, sym_size)
                table.append(symbol)
                if symbol[0]:
                    self.named_symbols[symbol[0]] = symbol
            self.symbol_tables[index] = table

    @staticmethod
    def cstr(data, offset):
        ensure(offset < len(data), 'invalid_string_offset')
        end = data.find(b'\0', offset)
        ensure(end >= 0, 'unterminated_string')
        return data[offset:end].decode('utf-8', 'replace')

    def section_data(self, index):
        section = self.sections[index]
        offset, size = section[4], section[5]
        ensure(offset + size <= len(self.data),
               f'bad_section_extent={self.path.name}')
        return self.data[offset:offset + size]

    def relocations(self):
        result = []
        for index, section in enumerate(self.sections):
            if section[1] != 4:  # SHT_RELA; all five selected Linux ABIs
                continue
            ensure(section[6] in self.symbol_tables and
                   section[7] < len(self.sections),
                   f'bad_rela_links={self.path.name}')
            table = self.symbol_tables[section[6]]
            data = self.section_data(index)
            entry_size = 24 if self.bits == 64 else 12
            ensure(section[9] == entry_size and len(data) % entry_size == 0,
                   f'bad_rela_shape={self.path.name}')
            for offset in range(0, len(data), entry_size):
                if self.bits == 64:
                    site, info, addend = struct.unpack_from('<QQq', data, offset)
                    symbol_index, relocation_type = info >> 32, info & 0xffffffff
                else:
                    site, info, addend = struct.unpack_from('<IIi', data, offset)
                    symbol_index, relocation_type = info >> 8, info & 0xff
                ensure(symbol_index < len(table),
                       f'bad_reloc_symbol={self.path.name}')
                symbol = table[symbol_index]
                ensure(0 < symbol[1] < len(self.sections),
                       f'undefined_reloc_symbol={self.path.name}')
                result.append((self.names[section[7]], site,
                               relocation_type, self.names[symbol[1]],
                               symbol[2] + addend))
        return sorted(result)


targets = {
    'x86_64': (64, 62, 0, 1),
    'aarch64': (64, 183, 0, 257),
    'loongarch64': (64, 258, 0x43, 2),
    'riscv64': (64, 243, 0, 2),
    'riscv32': (32, 243, 0, 1),
}

for name, (bits, machine, flags, relocation_type) in targets.items():
    obj = Elf(root / f'{name}-linked.o')
    ensure((obj.bits, obj.machine, obj.kind, obj.flags) ==
           (bits, machine, 1, flags),
           f'bad_output_identity={name} class={obj.bits} '
           f'machine={obj.machine} type={obj.kind} flags={obj.flags:#x}')
    symbols = obj.named_symbols
    for symbol_name in ('entry_a', 'entry_b', 'ref_a', 'ref_b'):
        ensure(symbol_name in symbols and
               0 < symbols[symbol_name][1] < len(obj.sections),
               f'missing_defined_symbol={name}:{symbol_name}')
    relocations = obj.relocations()
    ensure(len(relocations) == 2 and
           all(r[2] == relocation_type for r in relocations),
           f'wrong_relocations={name}:{relocations!r}')
    actual_targets = {(r[3], r[4]) for r in relocations}
    expected_targets = {
        (obj.names[symbols[n][1]], symbols[n][2])
        for n in ('entry_a', 'entry_b')
    }
    ensure(actual_targets == expected_targets,
           f'wrong_relocation_targets={name}:{relocations!r}')
    print(f'MINILD_MULTITARGET_TARGET=PASS target={name} '
          f'elf={bits} machine={machine} relocations=2')

actual = Elf(root / 'x86_64-linked.o')
oracle = Elf(root / 'x86_64-gnu.o')
ensure((actual.bits, actual.machine, actual.kind) ==
       (oracle.bits, oracle.machine, oracle.kind) and
       actual.relocations() == oracle.relocations(),
       f'x86_gnu_relocatable_differential actual={actual.relocations()!r} '
       f'gnu={oracle.relocations()!r}')
for name in ('entry_a', 'entry_b', 'ref_a', 'ref_b'):
    a, b = actual.named_symbols[name], oracle.named_symbols[name]
    ensure((actual.names[a[1]], a[2]) == (oracle.names[b[1]], b[2]),
           f'x86_gnu_symbol_difference={name}')

actual = Elf(root / 'x86_64-mixed-producer.o')
oracle = Elf(root / 'x86_64-mixed-producer-gnu.o')
ensure(len(actual.relocations()) == 3 and
       actual.relocations() == oracle.relocations(),
       f'x86_cross_producer_relocation_difference '
       f'actual={actual.relocations()!r} gnu={oracle.relocations()!r}')
for name in ('entry_a', 'entry_b', 'ref_a', 'ref_b',
             'gnu_entry', 'gnu_ref'):
    ensure(name in actual.named_symbols and name in oracle.named_symbols,
           f'x86_cross_producer_missing_symbol={name}')
    a, b = actual.named_symbols[name], oracle.named_symbols[name]
    ensure((actual.names[a[1]], a[2]) == (oracle.names[b[1]], b[2]),
           f'x86_cross_producer_symbol_difference={name}')

print('MINILD_MULTITARGET_REL=PASS targets=5 relocations=10 '
      'negative=20 x86_gnu_ld_r_differential=PASS '
      'x86_minias_gnuas_producer=PASS')
PY
