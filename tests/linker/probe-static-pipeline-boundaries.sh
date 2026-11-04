#!/usr/bin/env bash
# Structural regression for the unified ET_EXEC driver. All four modes must
# traverse the same final-link stages; only source normalization/GC differs.
# Uses MiniAS/MiniLD only: no GNU RISC-V cross tools or QEMU required.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
if ! command -v python3 >/dev/null 2>&1; then
    echo 'MINILD_STATIC_PIPELINE=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-static-pipeline.XXXXXX")
finish() {
    if [[ ${MINILD_STATIC_PIPELINE_KEEP:-0} == 1 ]]; then
        echo "MINILD_STATIC_PIPELINE_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.balign 8
.globl _start
_start:
    .byte 0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8
    .dword helper_fn
    .dword live_data
ASM
cat >"$work/other.s" <<'ASM'
.section .text.helper,"ax",@progbits
.balign 8
.globl helper_fn
helper_fn:
    .byte 0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8
.section .data.live,"aw",@progbits
.balign 8
.globl live_data
live_data:
    .dword 0x1122334455667788
.section .text.dead,"ax",@progbits
.balign 8
.globl dead_fn
dead_fn:
    .byte 0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8
ASM
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/entry.o" "$work/entry.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/other.o" "$work/other.s"

cat >"$work/layout.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) *(.text.helper) *(.text.dead) }
  .data : { *(.data.live) }
}
LDS

cat >"$work/check.py" <<'PY'
from pathlib import Path
import struct
import sys

case, path = sys.argv[1:]
image = Path(path).read_bytes()
if image[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', image, 16) != (2, 243):
    raise SystemExit(f'{case}: expected ELF64 little-endian RISC-V ET_EXEC')
entry = struct.unpack_from('<Q', image, 24)[0]
shoff = struct.unpack_from('<Q', image, 40)[0]
entsize, count, strindex = struct.unpack_from('<HHH', image, 58)
if entsize != 64 or strindex >= count or shoff + count * entsize > len(image):
    raise SystemExit(f'{case}: malformed section headers')
sections = [struct.unpack_from('<IIQQQQIIQQ', image, shoff + 64 * i)
            for i in range(count)]
namesec = sections[strindex]
names = image[namesec[4]:namesec[4] + namesec[5]]
def cstring(pool, offset):
    end = pool.find(b'\0', offset)
    if offset >= len(pool) or end == -1:
        raise SystemExit(f'{case}: invalid string offset')
    return pool[offset:end].decode('ascii')
section_names = [cstring(names, sec[0]) for sec in sections]
if any(name.startswith('.rela.') or name.startswith('.rel.')
       for name in section_names):
    raise SystemExit(f'{case}: final output leaked input relocations')
symindices = [i for i, name in enumerate(section_names) if name == '.symtab']
if len(symindices) != 1:
    raise SystemExit(f'{case}: expected one .symtab')
symtab = sections[symindices[0]]
strtab = sections[symtab[6]]
strings = image[strtab[4]:strtab[4] + strtab[5]]
symbols = {}
for pos in range(symtab[4], symtab[4] + symtab[5], 24):
    n, info, other, owner, value, size = struct.unpack_from('<IBBHQQ', image, pos)
    if n:
        symbols[cstring(strings, n)] = (owner, value)
def symbol_bytes(name, length):
    if name not in symbols:
        raise SystemExit(f'{case}: missing symbol {name}')
    owner, value = symbols[name]
    if not 0 < owner < len(sections):
        raise SystemExit(f'{case}: symbol {name} has no output section')
    sec = sections[owner]
    off = value - sec[3]
    if off < 0 or off + length > sec[5] or sec[4] + off + length > len(image):
        raise SystemExit(f'{case}: symbol {name} outside section')
    return image[sec[4] + off:sec[4] + off + length]
if entry != symbols['_start'][1]:
    raise SystemExit(f'{case}: wrong entry address')
refs = symbol_bytes('_start', 24)
if refs[:8] != bytes(range(0xe1, 0xe9)):
    raise SystemExit(f'{case}: corrupted entry bytes')
if struct.unpack_from('<QQ', refs, 8) != (symbols['helper_fn'][1], symbols['live_data'][1]):
    raise SystemExit(f'{case}: R_RISCV_64 fixups do not match final symbols')
if symbol_bytes('helper_fn', 8) != bytes(range(0xa1, 0xa9)) or \
        symbol_bytes('live_data', 8) != struct.pack('<Q', 0x1122334455667788):
    raise SystemExit(f'{case}: live source bytes incorrect')
gc = case in ('gc', 'script_gc')
if gc:
    if 'dead_fn' in symbols and 0 < symbols['dead_fn'][0] < len(sections):
        raise SystemExit(f'{case}: GC retained unreachable definition')
else:
    if symbol_bytes('dead_fn', 8) != bytes(range(0xd1, 0xd9)):
        raise SystemExit(f'{case}: ordinary link dropped unreferenced input')
print(f'MINILD_STATIC_PIPELINE_CHECK=PASS case={case} et_exec=1 fixups=2 gc={int(gc)}')
PY

for case in plain script gc script_gc; do
    options=()
    if [[ $case == script || $case == script_gc ]]; then
        options+=(-T "$work/layout.lds")
    fi
    if [[ $case == gc || $case == script_gc ]]; then
        options+=(--gc-sections)
    fi
    if ! MINILD_PROFILE_PHASES=1 "$MINILD" -melf64lriscv -static \
        "${options[@]}" -o "$work/$case.elf" \
        "$work/entry.o" "$work/other.o" \
        >"$work/$case.stdout" 2>"$work/$case.stderr"; then
        echo "MINILD_STATIC_PIPELINE=FAIL case=$case stage=link" >&2
        sed -n '1,14p' "$work/$case.stderr" >&2
        exit 1
    fi
    python3 "$work/check.py" "$case" "$work/$case.elf"
    # The phase profile is also an execution-order contract.  Merely finding
    # each name somewhere would miss duplicate, skipped or reordered phases.
    python3 - "$case" "$work/$case.stderr" <<'PY'
from pathlib import Path
import sys

case, logfile = sys.argv[1:]
expected = ('inputs', 'layout', 'relocations', 'output', 'cleanup')
observed = tuple(line.split(' ', 1)[0].split('=', 1)[1]
                 for line in Path(logfile).read_text().splitlines()
                 if line.startswith('MINILD_PHASE='))
if observed != expected:
    raise SystemExit(f'MINILD_STATIC_PIPELINE=FAIL case={case} '
                     f'phase_order={observed!r} expected={expected!r}')
PY
    # The admission graph is projected exactly once for GC/script selection.
    # Plain final linking uses a coalesced projection and need not advertise
    # a source-fragment expansion; the legacy materialization stage is gone.
    python3 - "$case" "$work/$case.stderr" <<'PY'
from pathlib import Path
import sys

case, logfile = sys.argv[1:]
lines = Path(logfile).read_text().splitlines()
events = [line for line in lines if line.startswith(
    'minic-ld: projected-input-sections:')]
legacy = [line for line in lines if 'materialized-input-sections:' in line]
if legacy:
    raise SystemExit(f'{case}: legacy materialization event still emitted')
if case == 'plain':
    if events:
        raise SystemExit(f'{case}: unexpected source-fragment projection: {events}')
else:
    mode = 'script' if case in ('script', 'script_gc') else 'gc'
    if len(events) != 1 or not events[0].endswith(f':mode={mode}'):
        raise SystemExit(f'{case}: expected one {mode} projector event: {events}')
PY
done
echo 'MINILD_STATIC_PIPELINE=PASS cases=4 profile_phases=5 projector=single_pass'
