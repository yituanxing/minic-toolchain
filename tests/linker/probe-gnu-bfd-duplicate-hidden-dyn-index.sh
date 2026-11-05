#!/usr/bin/env bash
# Host BFD oracle: an HIDDEN UND script PROVIDE present in two distinct
# ET_REL objects does not consume a forced-local dynsym ordinal.  The same
# rule must hold after the script discards the physical .dynsym section.
set -Eeuo pipefail

work=$(mktemp -d "${TMPDIR:-/tmp}/gnu-bfd-duplicate-hidden.XXXXXX")
trap 'rm -r -- "$work"' EXIT

cat >"$work/first.s" <<'ASM'
.text
.globl _start
_start:
    mov weak_got@GOTPCREL(%rip), %rax
    ret
.weak weak_got
.weak weak_abs
.hidden script_hidden_single
.hidden script_hidden_duplicate
.data
    .quad script_hidden_single
    .quad script_hidden_duplicate
    .quad weak_abs
ASM
cat >"$work/second.s" <<'ASM'
.hidden script_hidden_duplicate
.data
    .quad script_hidden_duplicate
ASM
as --64 -o "$work/first.o" "$work/first.s"
as --64 -o "$work/second.o" "$work/second.s"

cat >"$work/keep.lds" <<'LDS'
PROVIDE(script_hidden_single = ABSOLUTE(0x11223344));
PROVIDE(script_hidden_duplicate = ABSOLUTE(0x55667788));
SECTIONS {
  . = 0x400000;
  .text : { *(.text .text.*) }
  .data : { *(.data .data.*) }
  .got : { *(.got) }
  .got.plt : { *(.got.plt) }
  .rela.dyn : { *(.rela.dyn) }
}
LDS
cat >"$work/discard.lds" <<'LDS'
PROVIDE(script_hidden_single = ABSOLUTE(0x11223344));
PROVIDE(script_hidden_duplicate = ABSOLUTE(0x55667788));
SECTIONS {
  . = 0x400000;
  .text : { *(.text .text.*) }
  .data : { *(.data .data.*) }
  .got : { *(.got) }
  .got.plt : { *(.got.plt) }
  .rela.dyn : { *(.rela.dyn) }
  /DISCARD/ : { *(.interp .dynamic .dynsym .dynstr .hash .gnu.hash) }
}
LDS

for input in single repeated; do
    objects=("$work/first.o")
    if [[ $input == repeated ]]; then objects+=("$work/second.o"); fi
    for script in keep discard; do
        ld -m elf_x86_64 -shared -Bsymbolic -z notext \
            -T "$work/$script.lds" -o "$work/$input-$script.so" \
            "${objects[@]}"
    done
done

python3 - "$work" <<'PY'
import pathlib
import re
import subprocess
import sys

work = pathlib.Path(sys.argv[1])

def readelf(*args):
    return subprocess.check_output(('readelf', '-W', *map(str, args)), text=True)

def dynsym(path):
    rows = {}
    for line in readelf('--dyn-syms', path).splitlines():
        match = re.match(r'^\s*(\d+):\s+\S+\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(\S+)$', line)
        if match:
            rows[match.group(2)] = int(match.group(1))
    return rows

def dynrel(path):
    pairs = {}
    for line in readelf('-r', path).splitlines():
        match = re.match(r'^\s*\S+\s+([0-9a-f]{16})\s+(R_X86_64_(?:GLOB_DAT|64))\b', line)
        if match:
            pairs[match.group(2)] = int(match.group(1), 16) >> 32
    return pairs

for input, expected in (('single', (3, 4)), ('repeated', (2, 3))):
    retained = work / f'{input}-keep.so'
    discarded = work / f'{input}-discard.so'
    names = dynsym(retained)
    assert names['script_hidden_single'] == 1, (input, names)
    assert ('script_hidden_duplicate' in names) == (input == 'single'), (input, names)
    assert (names['weak_got'], names['weak_abs']) == expected, (input, names)
    assert '.dynsym' not in readelf('-S', discarded), input
    want = {'R_X86_64_GLOB_DAT': expected[0], 'R_X86_64_64': expected[1]}
    for path in (retained, discarded):
        assert dynrel(path) == want, (input, path, dynrel(path), want)

print('GNU_BFD_DUPLICATE_HIDDEN_DYNINDEX=PASS '
      'single_weak=3,4 repeated_weak=2,3 retain_and_discard_exact')
PY
