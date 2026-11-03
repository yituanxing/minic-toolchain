#!/usr/bin/env bash
# ET_DYN dynamic relocations in read-only LOADs require DT_TEXTREL and
# DF_TEXTREL.  PIE definitions bind locally and must not manufacture a PLT.
# Inspect final ELF bytes; GNU RISC-V binutils and QEMU are not required.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_SHARED_TEXTREL_PIE=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-shared-textrel-pie.XXXXXX")
cleanup() {
    if [[ ${MINILD_SHARED_TEXTREL_PIE_KEEP:-0} == 1 ]]; then
        echo "MINILD_SHARED_TEXTREL_PIE_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

cat >"$work/textrel.s" <<'ASM'
.section .text.reloc,"ax",@progbits
.balign 8
.globl textrel_pointer
textrel_pointer:
    .dword external_symbol
ASM

cat >"$work/writable.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
_start:
    addi a0,zero,3
    ret
.section .data.refs,"aw",@progbits
.balign 8
.globl writable_pointer
writable_pointer:
    .dword external_symbol
ASM

cat >"$work/pie-local.s" <<'ASM'
.section .text.start,"ax",@progbits
.balign 4
.globl _start
.type _start,@function
_start:
    call own_function
    ret
    .zero 5200
.section .text.target,"ax",@progbits
.balign 4
.globl own_function
.type own_function,@function
own_function:
    addi a0,zero,29
    ret
ASM

cat >"$work/pie-undefined.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call external_fn
    ret
ASM

cat >"$work/pie-weak.s" <<'ASM'
.weak optional_fn
.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call optional_fn
    ret
ASM

cat >"$work/provider.s" <<'ASM'
.section .text.provider,"ax",@progbits
.globl external_fn
.type external_fn,@function
external_fn:
    addi a0,zero,17
    ret
ASM

# R_RISCV_CALL_PLT uses S+A-P, even when S names a PLT entry.  Keep both
# external addends and a non-preemptible hidden local call in one fixture.
# The PLT's R_RISCV_JUMP_SLOT relocation itself must still use addend zero.
cat >"$work/plt-addend.s" <<'ASM'
.section .text.calls,"ax",@progbits
.globl probe_calls
.type probe_calls,@function
probe_calls:
    call external_fn@plt
    call external_fn@plt+4
    call hidden_target+4
    ret
.size probe_calls,.-probe_calls

.section .text.hidden,"ax",@progbits
.globl hidden_target
.hidden hidden_target
.type hidden_target,@function
hidden_target:
    nop
    ret
.size hidden_target,.-hidden_target
ASM

for name in textrel writable pie-local pie-undefined pie-weak provider plt-addend; do
    "$MINIAS" -march=rv64i -mabi=lp64 \
        -o "$work/$name.o" "$work/$name.s"
done

"$MINILD" -melf64lriscv -shared \
    -o "$work/textrel.so" "$work/textrel.o" \
    >"$work/textrel.stdout" 2>"$work/textrel.stderr"
"$MINILD" -melf64lriscv -shared \
    -o "$work/writable.so" "$work/writable.o" \
    >"$work/writable.stdout" 2>"$work/writable.stderr"
"$MINILD" -melf64lriscv -pie -e _start \
    -o "$work/pie-local.elf" "$work/pie-local.o" \
    >"$work/pie-local.stdout" 2>"$work/pie-local.stderr"
"$MINILD" -melf64lriscv -r \
    -o "$work/plt-addend-after-r.o" "$work/plt-addend.o"
for stage in direct after-r; do
    input="$work/plt-addend.o"
    if [[ $stage == after-r ]]; then
        input="$work/plt-addend-after-r.o"
    fi
    "$MINILD" -melf64lriscv -shared \
        -o "$work/plt-addend-$stage.so" "$input" \
        >"$work/plt-addend-$stage.stdout" \
        2>"$work/plt-addend-$stage.stderr"
done

# GNU ld's normal PIE rule: a strong undefined from a regular object must
# resolve from another input (e.g. a DSO).  Merely producing a PLT does not
# make that unresolved strong reference valid.  Weak undefined is permitted.
if "$MINILD" -melf64lriscv -pie -e _start \
        -o "$work/pie-undefined.elf" "$work/pie-undefined.o" \
        >"$work/pie-undefined.stdout" 2>"$work/pie-undefined.stderr"; then
    echo 'MINILD_SHARED_TEXTREL_PIE=FAIL strong undefined PIE unexpectedly linked' >&2
    exit 1
fi
if ! grep -Fq 'unresolved-pie-symbol:external_fn' "$work/pie-undefined.stderr"; then
    echo 'MINILD_SHARED_TEXTREL_PIE=FAIL strong undefined PIE diagnostic missing' >&2
    sed -n '1,12p' "$work/pie-undefined.stderr" >&2
    exit 1
fi
if [[ -e "$work/pie-undefined.elf" ]]; then
    echo 'MINILD_SHARED_TEXTREL_PIE=FAIL rejected PIE left output on disk' >&2
    exit 1
fi
echo 'MINILD_SHARED_TEXTREL_PIE_CHECK=PASS case=pie-strong-undefined rejected=true'

"$MINILD" -melf64lriscv -pie -e _start \
    -o "$work/pie-weak.elf" "$work/pie-weak.o" \
    >"$work/pie-weak.stdout" 2>"$work/pie-weak.stderr"

"$MINILD" -melf64lriscv -shared -soname libprovider.so \
    -o "$work/libprovider.so" "$work/provider.o" \
    >"$work/provider.stdout" 2>"$work/provider.stderr"
"$MINILD" -melf64lriscv -pie -e _start \
    -o "$work/pie-provider.elf" "$work/pie-undefined.o" "$work/libprovider.so" \
    >"$work/pie-provider.stdout" 2>"$work/pie-provider.stderr"

# This is a host GNU oracle for the strong-undefined rule, not for RISC-V
# instruction encoding.  No cross-toolchain dependency is introduced.
if [[ $(uname -m) == x86_64 ]] && \
        command -v as >/dev/null 2>&1 && command -v ld >/dev/null 2>&1; then
    cat >"$work/host-undefined.s" <<'ASM'
.globl _start
_start:
    call external_fn
    ret
ASM
    as --64 -o "$work/host-undefined.o" "$work/host-undefined.s"
    if ld -m elf_x86_64 -pie -e _start -o "$work/host-undefined.elf" \
            "$work/host-undefined.o" \
            >"$work/host-undefined.stdout" 2>"$work/host-undefined.stderr"; then
        echo 'MINILD_SHARED_TEXTREL_PIE=FAIL GNU host accepted strong undefined PIE' >&2
        exit 1
    fi
    if ! grep -Eqi 'undefined reference|undefined symbol' "$work/host-undefined.stderr"; then
        echo 'MINILD_SHARED_TEXTREL_PIE=FAIL GNU host failure was not an undefined symbol' >&2
        sed -n '1,12p' "$work/host-undefined.stderr" >&2
        exit 1
    fi
    echo 'MINILD_SHARED_TEXTREL_PIE_CHECK=PASS case=gnu-host-pie-undefined rejected=true'
fi

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

base = Path(sys.argv[1])


def elf(filename, machine=243):
    raw = (base / filename).read_bytes()
    if raw[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', raw, 16) != (3, machine):
        raise SystemExit(f'{filename}: invalid ELF64 ET_DYN')
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    entsize, count, name_index = struct.unpack_from('<HHH', raw, 58)
    if entsize != 64 or name_index >= count or shoff + count * 64 > len(raw):
        raise SystemExit(f'{filename}: malformed section headers')
    headers = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
               for i in range(count)]
    pool_header = headers[name_index]
    names = raw[pool_header[4]:pool_header[4] + pool_header[5]]

    def section_name(offset):
        end = names.find(b'\0', offset)
        if end < 0:
            raise SystemExit(f'{filename}: unterminated section name')
        return names[offset:end].decode('ascii')

    sections = {section_name(h[0]): h for h in headers}
    return raw, sections


def data_for(raw, section):
    offset, size = section[4:6]
    if offset + size > len(raw):
        raise SystemExit('section payload outside image')
    return raw[offset:offset + size]


def dynamic(raw, sections):
    section = sections.get('.dynamic')
    if section is None:
        raise SystemExit('missing .dynamic')
    data = data_for(raw, section)
    if len(data) % 16:
        raise SystemExit('malformed .dynamic')
    tags = {}
    for offset in range(0, len(data), 16):
        tag, value = struct.unpack_from('<qQ', data, offset)
        tags.setdefault(tag, []).append(value)
    if 0 not in tags:
        raise SystemExit('missing DT_NULL')
    return tags


def dyn_relocs(raw, sections):
    section = sections.get('.rela.dyn')
    if section is None:
        return []
    data = data_for(raw, section)
    if len(data) % 24:
        raise SystemExit('malformed .rela.dyn')
    return [struct.unpack_from('<QQq', data, i)
            for i in range(0, len(data), 24)]


def segment_flags_at(raw, address):
    phoff = struct.unpack_from('<Q', raw, 32)[0]
    entsize, count = struct.unpack_from('<HH', raw, 54)
    if entsize != 56 or phoff + count * entsize > len(raw):
        raise SystemExit('malformed program headers')
    found = []
    for i in range(count):
        kind, flags, offset, vaddr, paddr, filesz, memsz, align = \
            struct.unpack_from('<IIQQQQQQ', raw, phoff + i * entsize)
        if kind == 1 and vaddr <= address < vaddr + memsz:
            found.append(flags)
    if len(found) != 1:
        raise SystemExit(f'expected one PT_LOAD for {address:#x}: {found}')
    return found[0]


def has_address(section, address):
    return section[3] <= address < section[3] + section[5]


def check_textrel():
    raw, sections = elf('textrel.so')
    tags = dynamic(raw, sections)
    section = sections['.text.reloc']
    relocations = dyn_relocs(raw, sections)
    if not (section[2] & 0x2) or section[2] & 0x1:
        raise SystemExit('textrel input is not allocated read-only')
    if len(relocations) != 1 or relocations[0][1] & 0xffffffff != 2 or \
            not has_address(section, relocations[0][0]):
        raise SystemExit('textrel fixture needs one R_RISCV_64 in read-only text')
    if segment_flags_at(raw, relocations[0][0]) & 2:
        raise SystemExit('text relocation is not inside a read-only PT_LOAD')
    if 22 not in tags or 30 not in tags or not (tags[30][0] & 0x4):
        raise SystemExit(f'missing DT_TEXTREL or DF_TEXTREL: {tags}')
    print('MINILD_SHARED_TEXTREL_PIE_CHECK=PASS case=textrel '
          'DT_TEXTREL=true DF_TEXTREL=true reloc=read-only')


def check_writable():
    raw, sections = elf('writable.so')
    tags = dynamic(raw, sections)
    section = sections['.data.refs']
    relocations = dyn_relocs(raw, sections)
    if not (section[2] & 0x3) == 0x3 or len(relocations) != 1 or \
            relocations[0][1] & 0xffffffff != 2 or \
            not has_address(section, relocations[0][0]):
        raise SystemExit('writable fixture needs one dynamic data relocation')
    if not (segment_flags_at(raw, relocations[0][0]) & 2):
        raise SystemExit('data relocation is not inside a writable PT_LOAD')
    if 22 in tags or any(flags & 0x4 for flags in tags.get(30, [])):
        raise SystemExit('writable relocation must not request TEXTREL')
    print('MINILD_SHARED_TEXTREL_PIE_CHECK=PASS case=writable '
          'DT_TEXTREL=false DF_TEXTREL=false')


def symbols(raw, sections):
    sym = sections['.symtab']
    string = data_for(raw, sections['.strtab'])
    data = data_for(raw, sym)
    if len(data) % 24:
        raise SystemExit('malformed .symtab')
    result = {}
    for i in range(0, len(data), 24):
        noff, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', data, i)
        if noff:
            end = string.find(b'\0', noff)
            if end < 0:
                raise SystemExit('unterminated symbol')
            result[string[noff:end].decode('ascii')] = (value, info >> 4, other & 3)
    return result


def signed(value, bits):
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def check_pie():
    raw, sections = elf('pie-local.elf')
    tags = dynamic(raw, sections)
    found = symbols(raw, sections)
    if '_start' not in found or 'own_function' not in found:
        raise SystemExit('missing PIE call symbols')
    caller, target = found['_start'][0], found['own_function'][0]
    if found['own_function'][1:] != (1, 0):
        raise SystemExit('PIE self-call target must be global DEFAULT')
    if target - caller <= 4096:
        raise SystemExit('PIE fixture did not span 4 KiB')
    code = None
    for section in sections.values():
        if section[2] & 4 and has_address(section, caller):
            offset = section[4] + caller - section[3]
            if offset + 8 > len(raw):
                raise SystemExit('PIE call outside image')
            code = struct.unpack_from('<II', raw, offset)
            break
    if code is None:
        raise SystemExit('PIE call instruction missing')
    auipc, jalr = code
    if auipc & 0x7f != 0x17 or jalr & 0x7f != 0x67:
        raise SystemExit(f'expected AUIPC/JALR, got {auipc:#x} {jalr:#x}')
    displacement = (signed((auipc >> 12) & 0xfffff, 20) << 12) + \
                   signed((jalr >> 20) & 0xfff, 12)
    if caller + displacement != target:
        raise SystemExit(f'PIE self-call unexpectedly uses PLT: '
                         f'encoded={displacement} actual={target - caller}')
    if '.rela.plt' in sections and sections['.rela.plt'][5] != 0:
        raise SystemExit('PIE self-call generated a PLT relocation')
    if 23 in tags or 3 in tags:
        raise SystemExit('PIE self-call generated DT_JMPREL or DT_PLTGOT')
    print('MINILD_SHARED_TEXTREL_PIE_CHECK=PASS case=pie-selfcall '
          f'local_delta={displacement} plt=absent')


def dyn_symbols(raw, sections):
    if '.dynsym' not in sections or '.dynstr' not in sections:
        raise SystemExit('missing dynamic symbols')
    data, strings = data_for(raw, sections['.dynsym']), data_for(raw, sections['.dynstr'])
    if len(data) % 24:
        raise SystemExit('malformed .dynsym')
    result = {}
    for offset in range(0, len(data), 24):
        noff, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', data, offset)
        if not noff:
            continue
        end = strings.find(b'\0', noff)
        if end < 0:
            raise SystemExit('unterminated dynamic symbol')
        result[strings[noff:end].decode('ascii')] = (offset // 24, info >> 4, shndx)
    return result


def check_pie_undefined_allowed():
    raw, sections = elf('pie-weak.elf')
    weak = dyn_symbols(raw, sections).get('optional_fn')
    if weak is None or weak[1:] != (2, 0):
        raise SystemExit(f'weak undefined symbol was not preserved: {weak}')
    print('MINILD_SHARED_TEXTREL_PIE_CHECK=PASS case=pie-weak-undefined allowed=true')

    raw, sections = elf('pie-provider.elf')
    tags = dynamic(raw, sections)
    provider = dyn_symbols(raw, sections).get('external_fn')
    if provider is None or provider[2] != 0:
        raise SystemExit(f'DSO-provided symbol should remain dynamic undefined: {provider}')
    strings = data_for(raw, sections['.dynstr'])
    if not any(strings[pos:].startswith(b'libprovider.so\0') for pos in tags.get(1, [])):
        raise SystemExit('PIE with DSO provider must record DT_NEEDED libprovider.so')
    rela_plt = data_for(raw, sections['.rela.plt']) if '.rela.plt' in sections else b''
    if len(rela_plt) != 24:
        raise SystemExit('PIE undefined DSO call must emit one .rela.plt entry')
    target_address, info, addend = struct.unpack_from('<QQq', rela_plt)
    if info >> 32 != provider[0] or info & 0xffffffff != 5:
        raise SystemExit(f'PIE DSO call needs R_RISCV_JUMP_SLOT for external_fn: {info:#x}')
    if 23 not in tags or 3 not in tags:
        raise SystemExit('PIE DSO call must export DT_JMPREL and DT_PLTGOT')
    print('MINILD_SHARED_TEXTREL_PIE_CHECK=PASS case=pie-dso-provider '
          'needed=true symbol=external_fn JUMP_SLOT=true')


def verify_input_call_addends(filename):
    raw = (base / filename).read_bytes()
    if raw[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', raw, 16) != (1, 243):
        raise SystemExit(f'{filename}: malformed RV64 ET_REL fixture')
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shentsize, count, strndx = struct.unpack_from('<HHH', raw, 58)
    if shentsize != 64 or strndx >= count or shoff + count * 64 > len(raw):
        raise SystemExit(f'{filename}: malformed ET_REL section table')
    headers = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
               for i in range(count)]
    strings = data_for(raw, headers[strndx])
    names = []
    for header in headers:
        end = strings.find(b'\0', header[0])
        if end < 0:
            raise SystemExit(f'{filename}: unterminated section name')
        names.append(strings[header[0]:end].decode())
    if '.text.calls' not in names:
        raise SystemExit(f'{filename}: missing input call section')
    call_index = names.index('.text.calls')
    relocations = []
    for section in headers:
        if section[1] != 4 or section[7] != call_index:
            continue
        contents = data_for(raw, section)
        for offset in range(0, len(contents), 24):
            site, info, addend = struct.unpack_from('<QQq', contents, offset)
            if info & 0xffffffff in (18, 19):
                relocations.append((site, addend))
    if relocations != [(0, 0), (8, 4), (16, 4)]:
        raise SystemExit(f'{filename}: CALL relocation addends lost: {relocations}')


def check_plt_addend(stage):
    filename = f'plt-addend-{stage}.so'
    raw, sections = elf(filename)
    calls, hidden, plt = (sections[name] for name in
                          ('.text.calls', '.text.hidden', '.plt'))
    table = symbols(raw, sections)
    caller, target = table['probe_calls'][0], table['hidden_target'][0]
    if not has_address(calls, caller) or not has_address(hidden, target):
        raise SystemExit(f'{stage}: incorrect call/local target sections')
    if len(data_for(raw, plt)) != 16:
        raise SystemExit(f'{stage}: expected exactly one PLT stub')
    if dyn_symbols(raw, sections).get('external_fn', (None, None, -1))[2] != 0:
        raise SystemExit(f'{stage}: missing undefined dynamic PLT symbol')
    rela = data_for(raw, sections['.rela.plt'])
    if len(rela) != 24:
        raise SystemExit(f'{stage}: expected one JUMP_SLOT relocation')
    _, info, addend = struct.unpack_from('<QQq', rela)
    if info & 0xffffffff != 5 or addend != 0:
        raise SystemExit(f'{stage}: JUMP_SLOT must remain addend zero')

    for site, expected_destination in ((0, plt[3]), (8, plt[3] + 4),
                                       (16, target + 4)):
        offset = calls[4] + caller - calls[3] + site
        if offset + 8 > len(raw):
            raise SystemExit(f'{stage}: CALL site beyond file')
        auipc, jalr = struct.unpack_from('<II', raw, offset)
        if auipc & 0x7f != 0x17 or jalr & 0x7f != 0x67:
            raise SystemExit(f'{stage}: lost AUIPC/JALR at {site}')
        displacement = (signed((auipc >> 12) & 0xfffff, 20) << 12) + \
                       signed((jalr >> 20) & 0xfff, 12)
        actual_destination = caller + site + displacement
        if actual_destination != expected_destination:
            raise SystemExit(f'{stage}: CALL addend lost at {site}: '
                             f'got {actual_destination:#x}, '
                             f'want {expected_destination:#x}')
    print(f'MINILD_SHARED_TEXTREL_PIE_CHECK=PASS case=plt-addend-{stage} '
          'CALL_PLT=+0/+4 local_hidden=+4 JUMP_SLOT=+0')


check_textrel()
check_writable()
check_pie()
check_pie_undefined_allowed()
verify_input_call_addends('plt-addend.o')
verify_input_call_addends('plt-addend-after-r.o')
check_plt_addend('direct')
check_plt_addend('after-r')
PY

python3 "$work/check.py" "$work"

echo 'MINILD_SHARED_TEXTREL_PIE=PASS native_cases=8'
