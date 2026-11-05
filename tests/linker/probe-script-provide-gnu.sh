#!/usr/bin/env bash
# Cross-check GNU BFD script-only PROVIDE demand and symbol visibility.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-provide.XXXXXX")
trap 'rm -r -- "$work"' EXIT

cat >"$work/start.s" <<'ASM'
.text
.globl _start
_start:
  ret
ASM
as --64 -o "$work/start.o" "$work/start.s"

# This exact pattern is emitted by Linux LoongArch zboot.  The PROVIDE
# declaration is referenced only by LONG in the linker script, not ELF UND.
cat >"$work/zboot.lds" <<'LDS'
ENTRY(_start)
PROVIDE(zboot_code_size = ABSOLUTE(0));
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .rodata : { LONG(zboot_code_size) LONG(0x11223344) }
}
LDS

cat >"$work/forward.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .rodata : { LONG(forward_size) LONG(hidden_size) }
  PROVIDE_HIDDEN(hidden_size = ABSOLUTE(0x55667788));
  PROVIDE(forward_size = ABSOLUTE(0x11223344));
}
LDS

cat >"$work/chain.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  PROVIDE(chain_a = ABSOLUTE(chain_b + 5));
  PROVIDE(chain_b = ABSOLUTE(7));
  . = 0x10000;
  .text : { *(.text) }
  .rodata : { LONG(chain_a) }
}
LDS

cat >"$work/section-forward.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .rodata : {
    LONG(sec_forward)
    PROVIDE_HIDDEN(sec_forward = ABSOLUTE(0xaabbccdd));
  }
}
LDS

cat >"$work/no-reference.lds" <<'LDS'
ENTRY(_start)
PROVIDE(unused_only = ABSOLUTE(0x7788));
SECTIONS { . = 0x10000; .text : { *(.text) } }
LDS

cat >"$work/strong.lds" <<'LDS'
ENTRY(_start)
PROVIDE(existing = ABSOLUTE(0x9988));
SECTIONS { . = 0x10000; .text : { *(.text) } .rodata : { LONG(existing) } }
LDS
cat >"$work/strong-section.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .empty : { PROVIDE(existing = .); }
  .rodata : { LONG(existing) }
}
LDS
cat >"$work/existing.s" <<'ASM'
.globl existing
.set existing, 0x1234
ASM
as --64 -o "$work/existing.o" "$work/existing.s"

cat >"$work/extern.lds" <<'LDS'
ENTRY(_start)
EXTERN(archive_symbol)
SECTIONS { . = 0x10000; .text : { *(.text) } .rodata : { LONG(archive_symbol) } }
LDS
cat >"$work/member.s" <<'ASM'
.globl archive_symbol
.set archive_symbol, 0x4455
ASM
as --64 -o "$work/member.o" "$work/member.s"
ar crs "$work/libmember.a" "$work/member.o"

cat >"$work/cyclic.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  PROVIDE(cycle_a = cycle_b + 1);
  PROVIDE(cycle_b = cycle_a + 1);
  . = 0x10000;
  .text : { *(.text) }
  .rodata : { LONG(cycle_a) }
}
LDS

for case in zboot forward chain section-forward no-reference strong strong-section extern; do
    inputs=("$work/start.o")
    [[ $case == strong* ]] && inputs+=("$work/existing.o")
    [[ $case == extern ]] && inputs+=("$work/libmember.a")
    ld -m elf_x86_64 -static -n -T "$work/$case.lds" \
        -o "$work/$case.gnu" "${inputs[@]}"
    "$minild" -m elf_x86_64 -static -n --script="$work/$case.lds" \
        -o "$work/$case.mini" "${inputs[@]}"
done

python3 - "$work" <<'PY'
from pathlib import Path
import struct
import sys

base = Path(sys.argv[1])

def elf(case, producer):
    raw = (base / f'{case}.{producer}').read_bytes()
    hdr = struct.unpack_from('<16sHHIQQQIHHHHHH', raw)
    shoff, shentsize, shnum, shstrndx = hdr[6], hdr[11], hdr[12], hdr[13]
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + n * shentsize)
                for n in range(shnum)]
    shstr = sections[shstrndx]
    names = raw[shstr[4]:shstr[4]+shstr[5]]
    def name(section):
        n = section[0]
        return names[n:names.index(0, n)].decode()
    by_name = {name(section): section for section in sections}
    def data(section_name):
        section = by_name[section_name]
        return raw[section[4]:section[4]+section[5]]
    tab = by_name['.symtab']
    strings = sections[tab[6]]
    pool = raw[strings[4]:strings[4]+strings[5]]
    symbols = {}
    for off in range(tab[4], tab[4]+tab[5], 24):
        idx, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', raw, off)
        if idx:
            symbol = pool[idx:pool.index(0, idx)].decode()
            symbols[symbol] = (info, other, shndx, value, size)
    return data, symbols

expected = {
    'zboot': bytes.fromhex('0000000044332211'),
    'forward': bytes.fromhex('4433221188776655'),
    'chain': bytes.fromhex('0c000000'),
    'section-forward': bytes.fromhex('ddccbbaa'),
    'strong': bytes.fromhex('34120000'),
    'strong-section': bytes.fromhex('34120000'),
    'extern': bytes.fromhex('55440000'),
}
for case in ('zboot', 'forward', 'chain', 'section-forward', 'no-reference', 'strong', 'strong-section', 'extern'):
    g_data, g_symbols = elf(case, 'gnu')
    m_data, m_symbols = elf(case, 'mini')
    assert set(g_symbols) == set(m_symbols), (case, g_symbols.keys(), m_symbols.keys())
    for name in g_symbols:
        assert g_symbols[name] == m_symbols[name], (case, name, g_symbols[name], m_symbols[name])
    if case in expected:
        assert g_data('.rodata') == m_data('.rodata') == expected[case], case
    if case == 'no-reference':
        assert 'unused_only' not in g_symbols and 'unused_only' not in m_symbols
    if case in ('forward', 'section-forward'):
        name = 'hidden_size' if case == 'forward' else 'sec_forward'
        assert g_symbols[name][0] >> 4 == 0
        assert g_symbols[name][1] & 3 == 0
    print(f'MINILD_SCRIPT_PROVIDE_GNU=PASS case={case}', flush=True)
PY

# GNU BFD sometimes accepts a recursive PROVIDE by using an early-pass
# tentative value.  The freestanding linker deliberately fails such cycles
# closed; this negative guard does not claim GNU rejects every cycle.
if ld -m elf_x86_64 -static -n -T "$work/cyclic.lds" \
        -o "$work/cyclic.gnu" "$work/start.o" >"$work/cyclic.gnu.log" 2>&1; then
    echo 'MINILD_SCRIPT_PROVIDE_GNU=NOTE GNU_BFD_cyclic_tentative=accepted'
fi
if "$minild" -m elf_x86_64 -static -n --script="$work/cyclic.lds" \
        -o "$work/cyclic.mini" "$work/start.o" >"$work/cyclic.mini.log" 2>&1; then
    echo 'MINILD_SCRIPT_PROVIDE_GNU=FAIL Mini cyclic accepted unexpectedly' >&2
    exit 1
fi
grep -Eq 'unresolved-forward|undefined-symbol' "$work/cyclic.mini.log"
echo 'MINILD_SCRIPT_PROVIDE_GNU=PASS cyclic_rejected=1'
