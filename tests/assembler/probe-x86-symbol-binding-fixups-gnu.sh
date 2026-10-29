#!/usr/bin/env bash
# Verify that x86 same-section symbols obey ELF binding/preemption rules,
# and that all memory-addressing paths agree on signed versus unsigned
# immediate relocations. GNU GAS supplies the independent object oracle.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as readelf objcopy cmp python3; do
    command -v "$tool" >/dev/null || { echo "missing GNU tool: $tool" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/binding.s" <<'ASM'
.text
.globl strong
.type strong,@function
strong:
    ret
    call strong
    jmp strong
    je strong
    movq strong(%rip),%rax

.globl hidden
.hidden hidden
.type hidden,@function
hidden:
    ret
    call hidden
    movq hidden(%rip),%r8

.weak weak
.type weak,@function
weak:
    ret
    call weak
    jmp weak
    jne weak
    movq weak(%rip),%r9

local:
    ret
    call local
    jmp local
    je local
    movq local(%rip),%r10

    movl $immediate,(%rax)
    movl $immediate,displacement(%rip)
    movl $immediate,displacement(%rax)
    movq $immediate,(%rax)
ASM
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/binding.s"
as --64 -o "$work/gnu.o" "$work/binding.s"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"
python3 - "$work/gnu.o" "$work/mini.o" <<'PY'
import re
import subprocess
import sys

def relocations(path):
    records = []
    section = None
    for line in subprocess.check_output(['readelf', '-Wr', path], text=True).splitlines():
        header = re.match(r"Relocation section '([^']+)'", line)
        if header:
            section = header.group(1)
            continue
        columns = line.split()
        if (section == '.rela.text' and len(columns) >= 7 and
                re.fullmatch(r'[0-9a-fA-F]{8,16}', columns[0]) and
                columns[2].startswith('R_X86_64_')):
            records.append((int(columns[0], 16), columns[2],
                            columns[4], tuple(columns[5:])))
    return sorted(records)

gnu, mini = (relocations(path) for path in sys.argv[1:])
if len(gnu) < 12:
    raise SystemExit(f'GNU binding/imm32 oracle unexpectedly sparse: {gnu}')
if mini != gnu:
    raise SystemExit(f'x86 relocation mismatch\nGNU: {gnu}\nMiniAS: {mini}')
print(f'GNU relocations exact: {len(gnu)}')
PY

# Both assemblers must independently reject a high-byte register requiring
# an incompatible REX prefix and an invalid SIB index, with no partial object.
cat >"$work/invalid-rex.s" <<'ASM'
.text
    movb %ah,(%r8)
ASM
cat >"$work/invalid-sib.s" <<'ASM'
.text
    movq %rax,(%rdi,%rsp,2)
ASM
for negative in rex sib; do
    if as --64 -o "$work/gnu-invalid.o" "$work/invalid-$negative.s" >"$work/gnu.err" 2>&1; then
        echo "GNU unexpectedly accepted invalid $negative operand" >&2; exit 1
    fi
    if "$MINIAS" --target=x86_64 -o "$work/mini-invalid.o" "$work/invalid-$negative.s" >"$work/mini.err" 2>&1; then
        echo "MiniAS unexpectedly accepted invalid $negative operand" >&2; exit 1
    fi
    test ! -e "$work/mini-invalid.o"
done
echo 'MINIAS_X86_SYMBOL_BINDING_FIXUPS=PASS GNU_bytes=exact GNU_relocs=exact negative_REX_SIB=2'
