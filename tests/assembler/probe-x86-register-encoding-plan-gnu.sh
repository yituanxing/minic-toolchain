#!/usr/bin/env bash
# RegisterEncodingPlan replaces scattered REX/66/F3 and ModR/M policy in
# fourteen x86 form families. GNU machine bytes and relocation metadata are
# the behavioral oracle, including high-byte/REX illegal combinations.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for cmd in as objcopy readelf python3; do command -v "$cmd" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MINIAS_X86_REGISTER_PLAN=FAIL command=$BASH_COMMAND" >&2' ERR

cat >"$work/forms.s" <<'ASM'
.text
.globl entry
entry:
    movq %rax,%rbx
    addq %r8,%r9
    movb %ah,%bl
    cmoveq %rax,%rbx
    movzbl %ah,%ecx
    movzbw %ah,%bx
    movzbl %spl,%r9d
    negq %r8
    sete %al
    bswapq %r8
    shldq $3,%rax,%rbx
    movq %cr3,%rax
    imulq $7,%r8,%r9
    btq $5,%r8
    testq $0xff,%rax
    movb $12,%ah
    movslq %eax,%r9
    popq %r8
    tzcntq %r8,%r9
    popcntl %r8d,%r9d
    jmp *%rax
    call ext_function
    movq ext_global(%rip),%rax
ASM

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/forms.s"
as --64 -o "$work/gnu.o" "$work/forms.s"
for tool in mini gnu; do
  objcopy -j .text -O binary "$work/$tool.o" "$work/$tool.bin"
done
cmp "$work/mini.bin" "$work/gnu.bin"

# Exhaust the 10 shared BinaryFormDescriptor stems over all GPR widths.
# Four source/destination directions expose opcode reversal and immediate
# group selection; the GNU byte oracle catches the historical ADDb 0x00
# confusion and REX/66 displacement regressions.
python3 - "$work/binary-forms.s" <<'PY'
from pathlib import Path
import sys
stems=('mov','add','or','adc','sbb','and','sub','xor','cmp','test')
registers={'b':('%al','%bl'),'w':('%ax','%bx'),
           'l':('%eax','%ebx'),'q':('%rax','%rbx')}
lines=['.text','.globl binary_form_entry','binary_form_entry:']
for stem in stems:
    for width, (src,dst) in registers.items():
        op=stem+width
        lines.extend((f'  {op} {src},{dst}',
                      f'  {op} {src},8(%rdi)',
                      f'  {op} 8(%rdi),{dst}',
                      f'  {op} $3,{dst}'))
for stem,width,src,dst in (('mov','q','%rax','%rbx'),
                           ('add','q','%r8','%r9'),
                           ('cmp','l','%eax','%ebx'),
                           ('test','w','%ax','%bx')):
    lines.append(f'  {stem} {src},{dst}')
Path(sys.argv[1]).write_text('\n'.join(lines)+'\n',encoding='utf-8')
PY
"$MINIAS" --target=x86_64 -o "$work/binary.mini.o" "$work/binary-forms.s"
as --64 -o "$work/binary.gnu.o" "$work/binary-forms.s"
for tool in mini gnu; do
  objcopy -j .text -O binary "$work/binary.$tool.o" "$work/binary.$tool.bin"
done
cmp "$work/binary.mini.bin" "$work/binary.gnu.bin"

# Compare the relocation coordinates, types, bound symbols and addends.
python3 - "$work/mini.o" "$work/gnu.o" <<'PY'
import re
import subprocess
import sys
def relocs(path):
    dump=subprocess.check_output(['readelf','-Wr',path],text=True)
    result=[]
    for line in dump.splitlines():
        fields=line.split()
        if len(fields)<5 or not re.fullmatch(r'[0-9A-Fa-f]{8,16}',fields[0]):
            continue
        if not fields[2].startswith('R_X86_64_'):
            continue
        result.append((fields[0],fields[2],tuple(fields[4:])))
    return result
mini,gnu=relocs(sys.argv[1]),relocs(sys.argv[2])
if mini!=gnu:
    raise SystemExit(f'GNU x86 form relocation contract changed: mini={mini}, gnu={gnu}')
if len(mini)<2:
    raise SystemExit(f'expected external CALL and RIP fixups: {mini}')
PY

for invalid in \
  'movzbl %ah,%r9d' \
  'movzbq %ch,%rax' \
  'movb %spl,%ah' \
  'shldq $256,%rax,%rbx'; do
  printf '.text\n%s\n' "$invalid" >"$work/invalid.s"
  if "$MINIAS" --target=x86_64 -o "$work/invalid.o" \
      "$work/invalid.s" 2>"$work/invalid.err"; then
    echo "accepted invalid REX/high-byte or immediate: $invalid" >&2
    exit 1
  fi
  test ! -e "$work/invalid.o"
done
echo 'MINIAS_X86_REGISTER_PLAN=PASS GNU_bytes=exact relocations=exact form_families=14 binary_descriptor=10x4x4 unsuffixed=4 rejects=4'
