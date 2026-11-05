#!/usr/bin/env bash
# The first cross-target final-link milestone: genuine ELF64 ET_EXEC from
# x86-64, AArch64 and LoongArch64 MiniAS objects.  Common output owns PT_LOAD,
# sections and symbols; each target must resolve its own absolute relocation.
# x86-64 additionally proves a real Linux syscall and GNU ld runtime parity.
# Negative fixtures keep genuine MiniAS ELF sections/symbols, mutating only
# the affected relocation or instruction to exercise target-vs-mode fences.
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
        '.size fixture_ptr,8' \
        '.globl fixture_addend_ptr' \
        'fixture_addend_ptr:' \
        '    .quad fixture_target-17' \
        '.weak fixture_missing_weak' \
        '.globl fixture_weak_ptr' \
        'fixture_weak_ptr:' \
        '    .quad fixture_missing_weak' \
        '.globl fixture_pcrel32' \
        'fixture_pcrel32:' >>"$work/$name-entry.s"
    if [[ $name == loongarch64 ]]; then
        # MiniAS currently expresses a LoongArch symbol difference as an
        # ADD32/SUB32 pair. This positive fixture below changes its one
        # emitted absolute relocation into the supported R_LARCH_32_PCREL.
        printf '%s\n' '    .long fixture_target + 7' >>"$work/$name-entry.s"
    else
        printf '%s\n' '    .long fixture_target - . + 7' >>"$work/$name-entry.s"
    fi
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
    if [[ $name == loongarch64 ]]; then
        # The ELF remains a genuine MiniAS object; only the source's one
        # 32-bit absolute relocation kind changes to 32-bit PC-relative.
        python3 - "$work/$name-entry.o" <<'PY'
from pathlib import Path
import struct
import sys

path = Path(sys.argv[1])
raw = bytearray(path.read_bytes())
section_offset = struct.unpack_from('<Q', raw, 40)[0]
section_count = struct.unpack_from('<H', raw, 60)[0]
patched = 0
for i in range(section_count):
    sh = struct.unpack_from('<IIQQQQIIQQ', raw, section_offset + i * 64)
    if sh[1] != 4:
        continue
    for off in range(sh[4], sh[4] + sh[5], 24):
        offset, info, addend = struct.unpack_from('<QQq', raw, off)
        if offset == 24 and (info & 0xffffffff) == 1 and addend == 7:
            struct.pack_into('<Q', raw, off + 8,
                             (info & ~0xffffffff) | 99)
            patched += 1
assert patched == 1, f'expected one R_LARCH_32 at +24, got {patched}'
path.write_bytes(raw)
PY
    fi
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

    # Exercise the same producer objects through the complete two-stage
    # linker boundary. No architecture may rely on a direct-only admission,
    # symbol-identity or relocation path to produce a runnable ELF.
    "$MINILD" -m "$emulation" -r \
        -o "$work/$name-partial.o" \
        "$work/$name-entry.o" "$work/$name-data.o"
    "$MINILD" -m "$emulation" -static -e _start \
        -o "$work/$name-roundtrip.elf" "$work/$name-partial.o"
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
    for stage in ('minild', 'roundtrip'):
        elf = Elf64(root / f'{name}-{stage}.elf')
        ensure((elf.kind, elf.machine, elf.flags) == (2, machine, flags),
               f'bad_exec_identity={name}:{stage} kind={elf.kind} '
               f'machine={elf.machine} flags={elf.flags:#x}')
        ensure(elf.entry > 0 and any(p[0] == 1 and p[5] > 0 for p in elf.programs),
               f'missing_loadable_entry={name}:{stage}')
        # Linux supplies AT_PHDR into the mapped program-header table. The
        # static CRT/TLS loader must not receive a pointer to an unmapped
        # file header (GNU BFD maps these bytes in the first PT_LOAD).
        ensure(any(p[0] == 1 and p[2] == 0 and
                   p[5] >= 64 + 56 * len(elf.programs)
                   for p in elf.programs),
               f'unmapped_static_program_headers={name}:{stage}')
        ensure(all(s[1] != 4 or s[5] == 0 for s in elf.sections),
               f'unresolved_relocations={name}:{stage}')
        for symbol in ('_start', 'fixture_ptr', 'fixture_target',
                       'fixture_addend_ptr', 'fixture_weak_ptr',
                       'fixture_pcrel32'):
            ensure(symbol in elf.symbols and 0 < elf.symbols[symbol][0] <
                   len(elf.sections), f'missing_symbol={name}:{stage}:{symbol}')
        ensure(elf.entry == elf.symbols['_start'][1],
               f'entry_mismatch={name}:{stage}')
        ensure(elf.read_symbol_qword('fixture_ptr') ==
               elf.symbols['fixture_target'][1],
               f'absolute_relocation_not_applied={name}:{stage}')
        ensure(elf.read_symbol_qword('fixture_addend_ptr') ==
               elf.symbols['fixture_target'][1] - 17,
               f'negative_addend_not_applied={name}:{stage}')
        ensure(elf.read_symbol_qword('fixture_weak_ptr') == 0 and
               'fixture_missing_weak' in elf.symbols and
               elf.symbols['fixture_missing_weak'][0] == 0,
               f'undefined_weak_not_zero={name}:{stage}')
        actual_pcrel = elf.read_symbol_word('fixture_pcrel32')
        expected_pcrel = (elf.symbols['fixture_target'][1] + 7 -
                          elf.symbols['fixture_pcrel32'][1]) & 0xffffffff
        ensure(actual_pcrel == expected_pcrel,
               f'pcrel_addend_not_applied={name}:{stage} '
               f'actual={actual_pcrel:#x} expected={expected_pcrel:#x}')
        if name in ('aarch64', 'loongarch64'):
            ensure('fixture_helper' in elf.symbols,
                   f'missing_cross_object_helper={name}:{stage}')
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
                   f'cross_object_branch_not_applied={name}:{stage} '
                   f'actual={branch_destination:#x} '
                   f'expected={elf.symbols["fixture_helper"][1]:#x}')
        print(f'MINILD_MULTITARGET_STATIC_STRUCTURE=PASS target={name} '
              f'stage={stage} machine={machine} ptr_relocation=resolved '
              f'negative_addend=resolved weak_undef=zero '
              f'pcrel_addend=resolved '
              f'branch_relocation={"resolved" if name != "x86_64" else "runtime"} '
              f'PT_LOAD=yes')

gnu = Elf64(root / 'x86_64-gnu.elf')
ensure(gnu.kind == 2 and gnu.machine == 62 and
       gnu.read_symbol_qword('fixture_ptr') ==
       gnu.symbols['fixture_target'][1] and
       gnu.read_symbol_qword('fixture_addend_ptr') ==
       gnu.symbols['fixture_target'][1] - 17 and
       gnu.read_symbol_qword('fixture_weak_ptr') == 0 and
       gnu.read_symbol_word('fixture_pcrel32') ==
       ((gnu.symbols['fixture_target'][1] + 7 -
         gnu.symbols['fixture_pcrel32'][1]) & 0xffffffff),
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

    # The pointer in .data is exactly eight bytes. Moving an otherwise valid
    # ABS64 relocation four bytes forward must not write across its boundary.
    # A narrow ABS32 replacement with an enormous positive addend must fail
    # representability, rather than silently truncate a 64-bit symbol value.
    # A GOT/TLS relocation may be admitted by the target but must not
    # silently evaluate as a scalar when its required TLS block is absent.
    # x86 GOTTPOFF has real IE-TLS support; this intentionally malformed
    # fixture references a non-TLS symbol and still must fail closed.
    absolute_type, narrow_type, got_type = {
        'x86_64': (1, 10, 22),
        'aarch64': (257, 258, 311),
        'loongarch64': (2, 1, 75),
    }[name]
    rel_data = [s for i, s in enumerate(original.sections)
                if original.names[i] == '.rela.data']
    ensure(len(rel_data) == 1 and rel_data[0][5] >= 24,
           f'missing_exact_abs64_relocation={name}')
    data_reloc = rel_data[0]
    data_sections = [s for i, s in enumerate(original.sections)
                     if original.names[i] == '.data']
    ensure(len(data_sections) == 1 and data_sections[0][5] >= 8,
           f'missing_data_section={name}')
    info_at = data_reloc[4] + 8
    addend_at = data_reloc[4] + 16
    source_info = struct.unpack_from('<Q', original.data, info_at)[0]
    ensure((source_info & 0xffffffff) == absolute_type and
           source_info >> 32 != 0,
           f'unexpected_abs64_relocation={name}')
    for variant in ('out-of-bounds', 'narrow-overflow', 'got-mode-leak'):
        damaged = bytearray(original.data)
        if variant == 'out-of-bounds':
            struct.pack_into('<Q', damaged, data_reloc[4],
                             data_sections[0][5] - 4)
        elif variant == 'narrow-overflow':
            struct.pack_into('<Q', damaged, info_at,
                             (source_info & 0xffffffff00000000) | narrow_type)
            struct.pack_into('<q', damaged, addend_at, (1 << 63) - 1)
        else:
            struct.pack_into('<Q', damaged, info_at,
                             (source_info & 0xffffffff00000000) | got_type)
        (root / f'{name}-{variant}.o').write_bytes(damaged)

    # A genuinely undefined weak operand resolves to zero (tested above),
    # whereas changing only its binding to GLOBAL must fail at symbol
    # resolution. The input section and relocation remain otherwise valid.
    damaged = bytearray(original.data)
    weak_patched = 0
    for sec in original.sections:
        if sec[1] != 2:  # SHT_SYMTAB
            continue
        strings = original.section_data(sec[6])
        for pos in range(sec[4], sec[4] + sec[5], 24):
            symname, info, other, shndx, value, size = struct.unpack_from(
                '<IBBHQQ', damaged, pos)
            if symname and original.cstr(strings, symname) == 'fixture_missing_weak':
                ensure(info >> 4 == 2 and shndx == 0,
                       f'not_undefined_weak_source={name}')
                damaged[pos + 4] = (1 << 4) | (info & 15)
                weak_patched += 1
    ensure(weak_patched == 1, f'missing_weak_source={name}')
    (root / f'{name}-strong-undefined.o').write_bytes(damaged)

    # S+A-P must retain signed-range checking after common operand
    # projection. The exact same supported 32-bit PC-relative relocation is
    # reused with an out-of-range addend; no ISA instruction is introduced.
    damaged = bytearray(original.data)
    pcrel_type = {'x86_64': 2, 'aarch64': 261, 'loongarch64': 99}[name]
    pcrel_patched = 0
    for pos in range(data_reloc[4], data_reloc[4] + data_reloc[5], 24):
        rel_offset, rel_info, rel_addend = struct.unpack_from(
            '<QQq', damaged, pos)
        if rel_offset == 24 and (rel_info & 0xffffffff) == pcrel_type:
            struct.pack_into('<q', damaged, pos + 16, (1 << 63) - 1)
            pcrel_patched += 1
    ensure(pcrel_patched == 1, f'missing_pcrel_source={name}')
    (root / f'{name}-pcrel32-overflow.o').write_bytes(damaged)

# An AArch64 CALL26 relocation may only patch a BL; a well-formed reference
# attached to a NOP is a malformed instruction/relocation pair.
original = Elf64(root / 'aarch64-entry.o')
damaged = bytearray(original.data)
text = next(s for i, s in enumerate(original.sections)
            if original.names[i] == '.text')
struct.pack_into('<I', damaged, text[4], 0xd503201f)  # A64 NOP
(root / 'aarch64-invalid-call-encoding.o').write_bytes(damaged)
print('MINILD_MULTITARGET_STATIC_NEGATIVE_FIXTURES=PASS '
      'unknown_types=3 out_of_bounds=3 narrow_overflow=3 '
      'got_mode_leak=3 strong_undefined=3 pcrel32_overflow=3 '
      'invalid_call_encoding=1')
PY

negative_rejections=0
reject_static() {
    local id=$1 name=$2 expected=$3
    shift 3
    local output="$work/$id-should-not-exist.elf"
    if "$MINILD" "$@" -o "$output" >"$work/$id.stdout" 2>"$work/$id.stderr"; then
        echo "MINILD_MULTITARGET_STATIC=FAIL accepted_invalid_case=$id" >&2
        exit 1
    fi
    if [[ -e "$output" ]] || ! grep -Fq "$expected" "$work/$id.stderr"; then
        echo "MINILD_MULTITARGET_STATIC=FAIL bad_rejection=$id target=$name" >&2
        sed -n '1,10p' "$work/$id.stderr" >&2
        exit 1
    fi
    negative_rejections=$((negative_rejections + 1))
}

for name in x86_64 aarch64 loongarch64; do
    case "$name" in
        x86_64) emulation=elf_x86_64 machine=62 ;;
        aarch64) emulation=aarch64linux machine=183 ;;
        loongarch64) emulation=elf64loongarch machine=258 ;;
    esac
    reject_static "$name-unknown" "$name" \
        "unsupported-static-relocation:$machine:4026531841" \
        -m "$emulation" -static -e _start \
        "$work/$name-unknown-reloc.o" "$work/$name-data.o"
    reject_static "$name-out-of-bounds" "$name" \
        "nonriscv-relocation-offset-out-of-range:$machine:" \
        -m "$emulation" -static -e _start \
        "$work/$name-out-of-bounds.o" "$work/$name-data.o"
    reject_static "$name-narrow-overflow" "$name" \
        "nonriscv-relocation-overflow-or-encoding:$machine:" \
        -m "$emulation" -static -e _start \
        "$work/$name-narrow-overflow.o" "$work/$name-data.o"
    got_mode_diagnostic="unsupported-static-relocation:$machine:"
    if [[ $name == x86_64 ]]; then
        got_mode_diagnostic='missing-x86-static-tls-block'
    elif [[ $name == aarch64 ]]; then
        # AArch64 GOT_PAGE is now supported for valid ADRP instructions;
        # this mutation targets .data bytes, which must still fail closed.
        got_mode_diagnostic='nonriscv-relocation-overflow-or-encoding:183:311'
    elif [[ $name == loongarch64 ]]; then
        # LoongArch GOT_HI20 likewise requires a matching machine opcode;
        # arbitrary .data bytes are not an instruction relocation site.
        got_mode_diagnostic='loongarch-instruction-reloc-in-nonexec:75'
    fi
    reject_static "$name-got-mode-leak" "$name" \
        "$got_mode_diagnostic" \
        -m "$emulation" -static -e _start \
        "$work/$name-got-mode-leak.o" "$work/$name-data.o"
    reject_static "$name-strong-undefined" "$name" \
        'undefined-reference:fixture_missing_weak' \
        -m "$emulation" -static -e _start \
        "$work/$name-strong-undefined.o" "$work/$name-data.o"
    reject_static "$name-pcrel32-overflow" "$name" \
        "nonriscv-relocation-overflow-or-encoding:$machine:" \
        -m "$emulation" -static -e _start \
        "$work/$name-pcrel32-overflow.o" "$work/$name-data.o"
done
reject_static aarch64-invalid-call-encoding aarch64 \
    'nonriscv-relocation-overflow-or-encoding:183:283' \
    -m aarch64linux -static -e _start \
    "$work/aarch64-invalid-call-encoding.o" "$work/aarch64-data.o"
test "$negative_rejections" -eq 19

if [[ $(uname -m) != x86_64 ]]; then
    echo 'MINILD_MULTITARGET_STATIC=INCONCLUSIVE x86_native_runtime_requires_x86_64' >&2
    exit 8
fi
timeout 5s "$work/x86_64-gnu.elf" >"$work/x86-gnu.stdout"
for stage in minild roundtrip; do
    timeout 5s "$work/x86_64-$stage.elf" >"$work/x86-$stage.stdout"
    printf 'MINILD_STATIC_X86_OK\n' | cmp -s - "$work/x86-$stage.stdout" || {
        echo "MINILD_MULTITARGET_STATIC=FAIL x86_native_output_mismatch:$stage" >&2
        exit 1
    }
    cmp "$work/x86-$stage.stdout" "$work/x86-gnu.stdout" || {
        echo "MINILD_MULTITARGET_STATIC=FAIL x86_gnu_runtime_differential:$stage" >&2
        exit 1
    }
done
echo 'MINILD_MULTITARGET_STATIC_RUNTIME=PASS target=x86_64 native_syscall=2 gnu_ld=equal'

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
    for stage in minild roundtrip; do
        timeout 5s "$qemu" "$work/$name-$stage.elf" \
            >"$work/$name-$stage-qemu.stdout" \
            2>"$work/$name-$stage-qemu.stderr" || {
            echo "MINILD_MULTITARGET_STATIC=FAIL qemu_runtime=$name:$stage" >&2
            exit 1
        }
        test ! -s "$work/$name-$stage-qemu.stdout"
    done
    echo "MINILD_MULTITARGET_STATIC_RUNTIME=PASS target=$name qemu_exit=0 stages=2"
done

echo 'MINILD_MULTITARGET_STATIC=PASS structures=6 absolute_relocs=6 branch_relocs=4 signed_addends=6 weak_undef=6 pcrel_addends=6 fail_closed_rejections=19 x86_native_gnu=PASS roundtrips=3 non_x86_runtime=separately_reported'
