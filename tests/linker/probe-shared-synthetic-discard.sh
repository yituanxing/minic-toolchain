#!/usr/bin/env bash
# Synthetic SHF_ALLOC /DISCARD/ must not silently invalidate ET_DYN metadata.
# An empty .rela.dyn is different: with zero dynamic relocations, it has no
# DT_RELA dependency and may safely disappear from the final output plan.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 sed grep; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "MINILD_SHARED_SYNTHETIC_DISCARD=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-shared-synthetic-discard.XXXXXX")
cleanup() {
    if [[ ${MINILD_SHARED_SYNTHETIC_DISCARD_KEEP:-0} == 1 ]]; then
        echo "MINILD_SHARED_SYNTHETIC_DISCARD_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

cat >"$work/empty.s" <<'ASM'
.section .text.live,"ax",@progbits
.globl live_fn
.type live_fn,@function
live_fn:
    addi a0,zero,7
    ret
.size live_fn,.-live_fn
.section .note.minild,"a",@note
.balign 4
    .long 4,4,1
    .ascii "GNU\0"
    .long 0x11223344
ASM

cat >"$work/with-reloc.s" <<'ASM'
.section .data.refs,"aw",@progbits
.balign 8
.globl live_ref
live_ref:
    .dword external_symbol
ASM

# This call is to a hidden definition within the DSO: no PLT slot is needed.
# The target intentionally sits over 4 KiB away, requiring a nonzero PC
# displacement and exercising both AUIPC HI20 and JALR LO12 patching.
cat >"$work/local-call.s" <<'ASM'
.section .text.caller,"ax",@progbits
.balign 4
.globl probe_call_site
.hidden probe_call_site
.type probe_call_site,@function
probe_call_site:
    call local_target
    ret
    .zero 5200
.section .text.target,"ax",@progbits
.balign 4
.globl local_target
.hidden local_target
.type local_target,@function
local_target:
    addi a0,zero,19
    ret
ASM

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/empty.o" "$work/empty.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/with-reloc.o" "$work/with-reloc.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/local-call.o" "$work/local-call.s"

cat >"$work/script-template.lds" <<'LDS'
OUTPUT_ARCH(riscv)
SECTIONS {
  . = SIZEOF_HEADERS;
  .note : { *(.note.minild) } :text :note
  .hash : { *(.hash) } :text
  .gnu.hash : { *(.gnu.hash) } :text
  .dynsym : { *(.dynsym) } :text
  .dynstr : { *(.dynstr) } :text
  .gnu.version : { *(.gnu.version) } :text
  .gnu.version_d : { *(.gnu.version_d) } :text
  .rela.dyn : { *(.rela.dyn) } :text
  .text : { *(.text.live) *(.text.caller) *(.text.target) } :text
  .data : { *(.data.refs) } :text
  .dynamic : { *(.dynamic) } :text :dynamic
  /DISCARD/ : { *(@DISCARD@) }
}
PHDRS {
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh_frame_hdr PT_GNU_EH_FRAME;
}
VERSION {
  LINUX_4.15 {
    global: live_fn;
    local: *;
  };
}
LDS

# Preserve exact first-match ownership: omit the output-section placement
# corresponding to the synthetic section which /DISCARD/ must now select.
make_script() {
    local name=$1
    local section=$2
    sed -e "s|@DISCARD@|$section|g" \
        -e "/^  $section : /d" \
        "$work/script-template.lds" >"$work/$name.lds"
}

link_case() {
    local name=$1
    local input=$2
    local objects=("$work/empty.o")
    if [[ $input != "$work/empty.o" ]]; then
        objects+=("$input")
    fi
    "$MINILD" -melf64lriscv -shared --hash-style=both \
        -soname libminild-synthetic.so -T "$work/$name.lds" \
        -o "$work/$name.so" "${objects[@]}" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"
}

# Check the ELF independently from MiniLD's diagnostics; an empty synthetic
# relocation section is safe only when DT_RELA is absent as well.
cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

case, filename = sys.argv[1:]
data = Path(filename).read_bytes()
if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', data, 16) != (3, 243):
    raise SystemExit(f'{case}: not RV64 ET_DYN')
shoff = struct.unpack_from('<Q', data, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 58)
if shentsize != 64 or shstrndx >= shnum or shoff + 64 * shnum > len(data):
    raise SystemExit(f'{case}: malformed section table')
headers = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + 64 * i)
           for i in range(shnum)]
names_section = headers[shstrndx]
names = data[names_section[4]:names_section[4] + names_section[5]]

def name_at(offset):
    end = names.find(b'\0', offset)
    if end < 0:
        raise SystemExit(f'{case}: unterminated section name')
    return names[offset:end].decode('ascii')

by_name = {name_at(header[0]): header for header in headers}
for required in ('.note', '.dynsym', '.dynstr', '.hash', '.dynamic', '.text'):
    if required not in by_name or by_name[required][5] == 0:
        raise SystemExit(f'{case}: missing live metadata {required}')
dynamic = by_name['.dynamic']
start, size = dynamic[4:6]
if start + size > len(data) or size % 16:
    raise SystemExit(f'{case}: invalid dynamic data')
tags = [struct.unpack_from('<qQ', data, start + i)[0]
        for i in range(0, size, 16)]
if 0 not in tags:
    raise SystemExit(f'{case}: missing DT_NULL')

if case == 'baseline-reloc':
    rela = by_name.get('.rela.dyn')
    if rela is None or rela[1] != 4 or rela[5] != 24 or 7 not in tags:
        raise SystemExit(f'{case}: fixture lacks required nonempty .rela.dyn')
elif case == 'discard-empty-rela':
    if '.rela.dyn' in by_name or 7 in tags or 8 in tags or 9 in tags:
        raise SystemExit(f'{case}: empty .rela.dyn or DT_RELA tags survived discard')
else:
    raise SystemExit(f'unknown case {case}')
print(f'MINILD_SHARED_SYNTHETIC_DISCARD_CHECK=PASS case={case}')
PY

cat >"$work/check-local-call.py" <<'PY'
import struct
import sys
from pathlib import Path

input_path, output_path = map(Path, sys.argv[1:])

def elf_sections(path, elf_type):
    raw = path.read_bytes()
    if raw[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', raw, 16) != (elf_type, 243):
        raise SystemExit(f'{path}: not RV64 ELF type={elf_type}')
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    entsize, count, str_index = struct.unpack_from('<HHH', raw, 58)
    if entsize != 64 or str_index >= count or shoff + count * 64 > len(raw):
        raise SystemExit(f'{path}: invalid section headers')
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
                for i in range(count)]
    string_sh = sections[str_index]
    pool = raw[string_sh[4]:string_sh[4] + string_sh[5]]

    def cstr(blob, off):
        if off >= len(blob):
            raise SystemExit(f'{path}: bad string offset')
        end = blob.find(b'\0', off)
        if end < 0:
            raise SystemExit(f'{path}: unterminated string')
        return blob[off:end].decode('ascii')

    names = {cstr(pool, sh[0]): (index, sh)
             for index, sh in enumerate(sections)}

    def data(sh):
        start, size = sh[4:6]
        if start + size > len(raw):
            raise SystemExit(f'{path}: out-of-bounds section')
        return raw[start:start + size]

    return raw, sections, names, cstr, data

_, input_sections, input_names, _, input_data = elf_sections(input_path, 1)
caller_i, _ = input_names['.text.caller']
calls = []
for section in input_sections:
    if section[1] != 4 or section[7] != caller_i:
        continue
    relocations = input_data(section)
    if len(relocations) % 24:
        raise SystemExit('bad ET_REL relocation records')
    for offset in range(0, len(relocations), 24):
        site, info, _addend = struct.unpack_from('<QQq', relocations, offset)
        if site == 0 and (info & 0xffffffff) in (18, 19):
            calls.append(info & 0xffffffff)
if len(calls) != 1:
    raise SystemExit(f'missing one actual R_RISCV_CALL/CALL_PLT at caller offset 0: {calls}')

raw, sections, names, cstr, payload = elf_sections(output_path, 3)
text_i, text = names['.text']
_, symtab = names['.symtab']
_, strtab = names['.strtab']
string_pool = payload(strtab)
records = payload(symtab)
if len(records) % 24:
    raise SystemExit('bad final ELF symbol table')
addresses = {}
for offset in range(0, len(records), 24):
    name, _info, _other, shndx, value, _size = struct.unpack_from('<IBBHQQ', records, offset)
    if name and shndx == text_i:
        addresses[cstr(string_pool, name)] = value
for symbol in ('probe_call_site', 'local_target'):
    if symbol not in addresses:
        raise SystemExit(f'final local call symbol missing or wrong output owner: {symbol}')
caller, target = addresses['probe_call_site'], addresses['local_target']
expected_delta = target - caller
if expected_delta < 4096:
    raise SystemExit(f'fixture ineffective: local call delta {expected_delta} is too small')
code = payload(text)
site = caller - text[3]
if site < 0 or site + 8 > len(code):
    raise SystemExit('callsite address outside .text')
auipc, jalr = struct.unpack_from('<II', code, site)
if ((auipc & 0x7f) != 0x17 or ((auipc >> 7) & 31) != 1 or
        (jalr & 0x7f) != 0x67 or ((jalr >> 15) & 31) != 1 or
        ((jalr >> 7) & 31) != 1):
    raise SystemExit(f'call opcode/registers changed: auipc={auipc:#x} jalr={jalr:#x}')
hi20 = struct.unpack('<i', struct.pack('<I', auipc & 0xfffff000))[0]
lo12 = (jalr >> 20) & 0xfff
if lo12 & 0x800:
    lo12 -= 0x1000
patched_delta = hi20 + lo12
if patched_delta != expected_delta:
    raise SystemExit(f'stale local R_RISCV_CALL: encoded={patched_delta} '
                     f'expected={expected_delta} auipc={auipc:#x} jalr={jalr:#x}')
print(f'MINILD_SHARED_SYNTHETIC_DISCARD_CHECK=PASS case=local-call-no-plt '
      f'reloc_type={calls[0]} delta={patched_delta} auipc={auipc:#x} jalr={jalr:#x}')
PY

make_script baseline .never_matches_a_section
link_case baseline "$work/with-reloc.o"
python3 "$work/check.py" baseline-reloc "$work/baseline.so"

rejected=0
for spec in 'dynsym:.dynsym:empty.o' 'dynamic:.dynamic:empty.o' \
            'nonempty-rela:.rela.dyn:with-reloc.o'; do
    IFS=: read -r name section input <<<"$spec"
    make_script "$name" "$section"
    if link_case "$name" "$work/$input"; then
        echo "MINILD_SHARED_SYNTHETIC_DISCARD=FAIL unexpected_success=$name" >&2
        exit 1
    fi
    if ! grep -Fq "unsupported-shared-script-discard-alloc:$section" \
        "$work/$name.stderr"; then
        echo "MINILD_SHARED_SYNTHETIC_DISCARD=FAIL missing_rejection=$name" >&2
        sed -n '1,14p' "$work/$name.stderr" >&2
        exit 1
    fi
    echo "MINILD_SHARED_SYNTHETIC_DISCARD_CHECK=PASS case=$name rejection=alloc-synthetic"
    rejected=$((rejected + 1))
done

make_script discard-empty-rela .rela.dyn
if ! link_case discard-empty-rela "$work/empty.o"; then
    echo 'MINILD_SHARED_SYNTHETIC_DISCARD=FAIL empty-rela-rejected' >&2
    sed -n '1,14p' "$work/discard-empty-rela.stderr" >&2
    exit 1
fi
python3 "$work/check.py" discard-empty-rela "$work/discard-empty-rela.so"

make_script local-call .never_matches_a_section
if ! link_case local-call "$work/local-call.o"; then
    echo 'MINILD_SHARED_SYNTHETIC_DISCARD=FAIL local-call-link' >&2
    sed -n '1,14p' "$work/local-call.stderr" >&2
    exit 1
fi
python3 "$work/check-local-call.py" "$work/local-call.o" "$work/local-call.so"
echo "MINILD_SHARED_SYNTHETIC_DISCARD=PASS rejected=$rejected empty_rela=allowed baseline_rela=required local_call=patched"
