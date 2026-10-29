#!/usr/bin/env bash
# GNU-oracle x86 branch relaxation and relocation identities.  The parser
# retains an MCInst opcode; layout/encoding must not rediscover the ISA from
# mnemonic text, even for GNU aliases, external references and subsections.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MINIAS_X86_BRANCH_GROWTH_FAIL command=$BASH_COMMAND" >&2' ERR

for tool in as objcopy readelf cmp python3; do
    command -v "$tool" >/dev/null || { echo "missing GNU x86 tool: $tool" >&2; exit 1; }
done

cat >"$work/branches.s" <<'ASM'
.text
.globl x86_branch_test
.type x86_branch_test,@function
x86_branch_test:
    je .Lforward_short_jcc
    .space 127,0x90
.Lforward_short_jcc:
    je .Lforward_near_jcc
    .space 128,0x90
.Lforward_near_jcc:
    jmp .Lforward_short_jmp
    .space 127,0x90
.Lforward_short_jmp:
    jmp .Lforward_near_jmp
    .space 128,0x90
.Lforward_near_jmp:
.Lback_short_jcc:
    .space 126,0x90
    jne .Lback_short_jcc
.Lback_near_jcc:
    .space 127,0x90
    jne .Lback_near_jcc
.Lback_short_jmp:
    .space 126,0x90
    jmp .Lback_short_jmp
.Lback_near_jmp:
    .space 127,0x90
    jmp .Lback_near_jmp
    jae .Lalias_zero
    jnb .Lalias_one
    jc .Lalias_two
    jnc .Lalias_three
.Lalias_zero:
.Lalias_one:
.Lalias_two:
.Lalias_three:
    je external_conditional
    jmp external_jump
    call external_call
    jne x86_other_section
    je .Lsubsection_target
    nop
.size x86_branch_test,.-x86_branch_test
.subsection 1
.Lsubsection_target:
    nop
.section .text.x86_other,"ax",@progbits
.globl x86_other_section
x86_other_section:
    ret
ASM

"$MINIAS" --target=x86_64 -o "$work/minias.o" "$work/branches.s"
as --64 -o "$work/gnu.o" "$work/branches.s"
for section in .text .text.x86_other; do
    objcopy -j "$section" -O binary "$work/minias.o" "$work/minias.$section.bin"
    objcopy -j "$section" -O binary "$work/gnu.o" "$work/gnu.$section.bin"
    cmp "$work/minias.$section.bin" "$work/gnu.$section.bin"
done
readelf -Wr "$work/minias.o" >"$work/minias.reloc"
readelf -Wr "$work/gnu.o" >"$work/gnu.reloc"
readelf -Ws "$work/minias.o" >"$work/minias.symbol"
readelf -Ws "$work/gnu.o" >"$work/gnu.symbol"
python3 - "$work/minias.reloc" "$work/gnu.reloc" "$work/minias.symbol" "$work/gnu.symbol" <<'PY'
from pathlib import Path
import re, sys

def relocs(file):
    result=[]
    for line in Path(file).read_text().splitlines():
        fields=line.split()
        if len(fields)>=5 and fields[2].startswith('R_X86_64_'):
            # Section-relative relocation offset, kind, symbol and addend.
            result.append((int(fields[0],16), fields[2], ' '.join(fields[4:])))
    return sorted(result)

actual=relocs(sys.argv[1]); gnu=relocs(sys.argv[2])
if actual != gnu:
    raise SystemExit(f'x86 branch relocations differ MiniAS={actual} GNU={gnu}')

def value_size(file, name):
    for line in Path(file).read_text().splitlines():
        p=line.split()
        if len(p)>=8 and p[-1]==name:
            return (int(p[1],16), int(p[2]))
    raise SystemExit(f'missing symbol {name}: {file}')

symbol='x86_branch_test'
mini=value_size(sys.argv[3],symbol); gas=value_size(sys.argv[4],symbol)
if mini != gas:
    raise SystemExit(f'x86 branch symbol mismatch MiniAS={mini} GNU={gas}')

assert any(r[1]=='R_X86_64_PLT32' for r in actual), actual
print(f'MINIAS_X86_TYPED_BRANCH_GROWTH=PASS GNU_bytes=2 GNU_RELA={len(actual)} GNU_symbol={symbol}:{mini[1]} aliases=4 boundaries=8 extern=3 subsection=1')
PY

# Layout's precise signed PC-relative arithmetic must reject huge resolved
# local addends instead of wrapping into a short branch by accident.
cat >"$work/overflow.s" <<'ASM'
.text
overflow:
    je later+9223372036854775807
    nop
later:
    ret
ASM
if "$MINIAS" --target=x86_64 -o "$work/overflow.o" "$work/overflow.s" \
   >"$work/overflow.stdout" 2>"$work/overflow.stderr"; then
    echo 'x86 branch signed-overflow accepted' >&2
    exit 1
fi
if ! grep -Eq 'x86-branch-(address-or-addend-overflow|displacement-range)' "$work/overflow.stderr"; then
    echo 'x86 overflow did not fail at the branch encoding boundary' >&2
    cat "$work/overflow.stderr" >&2
    exit 1
fi
echo 'MINIAS_X86_TYPED_BRANCH_SIGNED_OVERFLOW=PASS'
