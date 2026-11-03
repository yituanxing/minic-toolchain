#!/usr/bin/env bash
# A source site's relaxation permission is immutable: an option at EOF cannot
# erase a branch or DWARF relocation needed by earlier shrinkable code.
# An R_LARCH_ALIGN reservation is itself a possible change in branch distance.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
: "${MINIAS:=$repo/build/release/bin/minic-as}"
test -x "$MINIAS"
for program in readelf python3; do command -v "$program" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/relax-then-norelax.s" <<'ASM'
.file 1 "scoped-branch.c"
.section .text.scoped,"ax",@progbits
.option relax
.globl scoped_entry
scoped_entry:
.loc 1 1 0
    beq $a0,$a1,scoped_target
    beqz $a0,scoped_target
    b scoped_target
scoped_relax_start:
    la.local $a2,scoped_target
    .p2align 5
scoped_relax_end:
.loc 1 2 0
scoped_target:
    nop
.option norelax
.section .debug_rnglists,"",@progbits
    .uleb128 scoped_relax_end-scoped_relax_start
ASM

cat >"$work/norelax-then-relax.s" <<'ASM'
.section .text.scoped,"ax",@progbits
.option norelax
scoped_entry:
    b scoped_target
.option relax
    la.local $a2,scoped_target
scoped_target:
    nop
ASM

cat >"$work/align-only.s" <<'ASM'
.section .text.scoped,"ax",@progbits
.option relax
scoped_entry:
    b scoped_target
    .p2align 5
scoped_target:
    nop
.option norelax
ASM

cat >"$work/never-relax.s" <<'ASM'
.section .text.scoped,"ax",@progbits
.option norelax
scoped_entry:
    b scoped_target
    .p2align 5
scoped_target:
    nop
ASM

for name in relax-then-norelax norelax-then-relax align-only never-relax; do
    "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
        -o "$work/$name.o" "$work/$name.s"
    readelf -Wr "$work/$name.o" >"$work/$name.reloc"
done

python3 - "$work" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
def records(name, section):
    text = (root / f'{name}.reloc').read_text()
    match = re.search(r"Relocation section '\.rela" + re.escape(section) +
                      r"'[^\n]*\n(.*?)(?=\nRelocation section |\Z)",
                      text, re.S)
    assert match is not None, (name, section, text)
    return [(int(offset, 16), kind) for offset, kind in
            re.findall(r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+'
                       r'(R_LARCH_[A-Z0-9_]+)\b', match.group(1), re.M)]

early = records('relax-then-norelax', '.text.scoped')
for offset, kind in ((0, 'R_LARCH_B16'), (4, 'R_LARCH_B21'),
                     (8, 'R_LARCH_B26')):
    assert (offset, kind) in early, ('early branch lost after norelax', offset, kind, early)
assert any(kind == 'R_LARCH_RELAX' for _, kind in early), early
assert any(kind == 'R_LARCH_ALIGN' for _, kind in early), early
debug = records('relax-then-norelax', '.debug_line')
assert any(kind == 'R_LARCH_ADD16' for _, kind in debug), debug
assert any(kind == 'R_LARCH_SUB16' for _, kind in debug), debug
leb = records('relax-then-norelax', '.debug_rnglists')
assert any(kind == 'R_LARCH_ADD_ULEB128' for _, kind in leb), leb
assert any(kind == 'R_LARCH_SUB_ULEB128' for _, kind in leb), leb

late = records('norelax-then-relax', '.text.scoped')
assert (0, 'R_LARCH_B26') in late, ('branch crosses later shrinkable code', late)
align = records('align-only', '.text.scoped')
assert (0, 'R_LARCH_B26') in align, ('ALIGN can move a local branch target', align)
assert any(kind == 'R_LARCH_ALIGN' for _, kind in align), align
unrelaxed = (root / 'never-relax.reloc').read_text()
assert 'R_LARCH_ALIGN' not in unrelaxed and 'R_LARCH_B26' not in unrelaxed, unrelaxed
print('MINIAS_LOONG_SCOPED_RELAX=PASS lexical_branch=3 cross_scope=1 '
      'alignment_branch=1 DWARF_ADD_SUB16=1 ULEB_ADD_SUB=1 norelax_fixed=1')
PY

# Ubuntu's GNU LoongArch assembler 2.42 predates lexical .option relax. Use
# its global -mrelax mode to independently check the final linked instruction
# bytes and local target position, including a branch crossing R_LARCH_ALIGN.
loong_as=${LOONGAS:-loongarch64-linux-gnu-as}
loong_ld=${LOONGLD:-loongarch64-linux-gnu-ld}
loong_copy=${LOONGCOPY:-loongarch64-linux-gnu-objcopy}
if ! command -v "$loong_as" >/dev/null ||
   ! command -v "$loong_ld" >/dev/null ||
   ! command -v "$loong_copy" >/dev/null; then
    if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
        echo 'MINIAS_LOONG_SCOPED_RELAX=BLOCKED GNU cross tools missing' >&2
        exit 1
    fi
    echo 'MINIAS_LOONG_SCOPED_RELAX=SKIP final GNU link cross tools unavailable'
    exit 0
fi

cat >"$work/default-relax.s" <<'ASM'
.file 1 "scoped-branch.c"
.text
.globl _start
_start:
.loc 1 1 0
    b target
    la.local $a0,target
    .p2align 5
.loc 1 2 0
target:
    nop
ASM
"$loong_as" -mabi=lp64d -mrelax -o "$work/gnu.o" "$work/default-relax.s"
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/mini.o" "$work/default-relax.s"
for mode in relax no-relax; do
    for producer in gnu mini; do
        "$loong_ld" "--$mode" -e _start \
            -o "$work/$producer-$mode.elf" "$work/$producer.o"
        "$loong_copy" -O binary -j .text "$work/$producer-$mode.elf" \
            "$work/$producer-$mode.text"
        readelf -Ws "$work/$producer-$mode.elf" |
            awk '$8=="target" {print $2}' >"$work/$producer-$mode.target"
        test -s "$work/$producer-$mode.target"
        readelf --debug-dump=decodedline "$work/$producer-$mode.elf" \
            >"$work/$producer-$mode.lines"
    done
    cmp "$work/gnu-$mode.text" "$work/mini-$mode.text"
    cmp "$work/gnu-$mode.target" "$work/mini-$mode.target"
    python3 - "$work/gnu-$mode.lines" "$work/mini-$mode.lines" \
        "$work/gnu-$mode.target" <<'PY'
from pathlib import Path
import re
import sys

def rows(path):
    result=[]
    for line in Path(path).read_text().splitlines():
        match=re.match(r'^\s*\S*scoped-branch\.c\s+(\d+)\s+(0x[0-9a-fA-F]+)\b', line)
        if match:
            result.append((int(match.group(1)),int(match.group(2),16)))
    return result

gnu, mini = rows(sys.argv[1]), rows(sys.argv[2])
target = int(Path(sys.argv[3]).read_text().strip(), 16)
assert gnu and mini and gnu == mini, ('final DWARF line PCs', gnu, mini)
assert any(line == 2 and pc == target for line, pc in mini), \
    ('line 2 did not follow relaxed target', mini, hex(target))
PY
done
echo 'MINIAS_LOONG_SCOPED_RELAX=PASS GNU_final_text_target_and_DWARF=exact linker_modes=2'
