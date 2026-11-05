#!/usr/bin/env bash
# The x86 GOTPCRELX contract is a decoded machine operand property.
# Linkers may rewrite eligible memory reads to immediates; GNU GAS must be
# the independent oracle for REX/non-REX eligibility and byte/word exclusions.
set -Eeuo pipefail
: "${MINIAS:?Provide exact-head MiniAS binary}"
for command_name in as ld readelf objcopy cmp python3; do
  command -v "$command_name" >/dev/null
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/gotpcrel.s" <<'ASM'
.text
.globl foo
foo:
  # GNU relaxable source-memory ALU/TEST/MOV; LEA is not a GOT load.
  addq ext@GOTPCREL(%rip), %rax
  subq ext@GOTPCREL(%rip), %rax
  cmpq ext@GOTPCREL(%rip), %rax
  andq ext@GOTPCREL(%rip), %rax
  orq ext@GOTPCREL(%rip), %rax
  xorq ext@GOTPCREL(%rip), %rax
  adcq ext@GOTPCREL(%rip), %rax
  sbbq ext@GOTPCREL(%rip), %rax
  testq ext@GOTPCREL(%rip), %rax
  movq ext@GOTPCREL(%rip), %rax
  leaq ext@GOTPCREL(%rip), %rax
  # 8/16-bit operands are never relaxable; 32/64-bit are.
  movb ext@GOTPCREL(%rip), %al
  movw ext@GOTPCREL(%rip), %ax
  movl ext@GOTPCREL(%rip), %eax
  movq ext@GOTPCREL(%rip), %rax
  addb ext@GOTPCREL(%rip), %al
  addw ext@GOTPCREL(%rip), %ax
  addl ext@GOTPCREL(%rip), %eax
  addq ext@GOTPCREL(%rip), %rax
  testb ext@GOTPCREL(%rip), %al
  testw ext@GOTPCREL(%rip), %ax
  testl ext@GOTPCREL(%rip), %eax
  testq ext@GOTPCREL(%rip), %rax
  # Memory destinations and immediate forms are not relaxable. TEST is
  # read-only in either operand order and is an explicit GNU exception.
  movq %rax, ext@GOTPCREL(%rip)
  addq %rax, ext@GOTPCREL(%rip)
  cmpq %rax, ext@GOTPCREL(%rip)
  testq %rax, ext@GOTPCREL(%rip)
  addq $1, ext@GOTPCREL(%rip)
  incq ext@GOTPCREL(%rip)
ASM

as --64 -o "$work/gnu.o" "$work/gotpcrel.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/gotpcrel.s"
for producer in gnu mini; do
  objcopy -j .text -O binary "$work/$producer.o" "$work/$producer.bin"
  readelf -Wr "$work/$producer.o" >"$work/$producer.reloc"
done
cmp "$work/gnu.bin" "$work/mini.bin"

python3 - "$work/gnu.reloc" "$work/mini.reloc" <<'PY'
import pathlib
import re
import sys

plain = 'R_X86_64_GOTPCREL'
without_rex = 'R_X86_64_GOTPCRELX'
with_rex = 'R_X86_64_REX_GOTPCRELX'
expected = (
    [with_rex] * 10 + [plain] +
    [plain, plain, without_rex, with_rex] * 3 +
    [plain, plain, plain, with_rex, plain, plain]
)
assert len(expected) == 29

def relocations(filename):
    in_text = False
    result = []
    for line in pathlib.Path(filename).read_text().splitlines():
        header = re.match(r"Relocation section '([^']+)'", line)
        if header:
            in_text = header.group(1) == '.rela.text'
            continue
        if not in_text:
            continue
        fields = re.match(
            r'^\s*([0-9a-fA-F]+)\s+\S+\s+(R_X86_64_\w+)'
            r'\s+[0-9a-fA-F]+\s+(\S+)\s+([+-])\s+(\d+)\s*$',
            line)
        if fields:
            offset, kind, symbol, sign, addend = fields.groups()
            result.append((int(offset, 16), kind, symbol,
                           int(addend) * (-1 if sign == '-' else 1)))
    return result

gnu, mini = map(relocations, sys.argv[1:])
assert len(gnu) == len(expected), ('GNU fixture changed', gnu)
assert [entry[1] for entry in gnu] == expected, ('GNU eligibility changed', gnu)
if mini != gnu:
    raise SystemExit(f'x86 GOTPCRELX operand/REX/width relocation mismatch\n'
                     f'GNU={gnu}\nMiniAS={mini}')
print('MINIAS_X86_GOTPCRELX=PASS GNU_relocs=29 '
      'ALU_TEST_MOV_REX=10 width_profiles=12 mem_dest=6 '
      'ELF_relocations=exact bytes=exact')
PY

# The relocation type is also validated by the real GNU linker, not merely
# by GNU's assembler and readelf. GNU+MiniAS must remain link-compatible.
for producer in gnu mini; do
  ld -r -o "$work/$producer.partial.o" "$work/$producer.o"
  ld -e foo --defsym ext=0x400100 -o "$work/$producer.elf" "$work/$producer.o"
  objcopy -j .text -O binary "$work/$producer.elf" "$work/$producer.linked.bin"
done
cmp "$work/gnu.linked.bin" "$work/mini.linked.bin"
echo 'MINIAS_X86_GOTPCRELX=PASS GNU_final_link=exact'
