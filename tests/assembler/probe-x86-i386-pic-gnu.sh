#!/usr/bin/env bash
# GNU GCC -m32 PIC -> GNU GAS / MiniAS -> GNU ld real ELF32 shared link.
# A diagnostic for the Linux 6.6.143 i386 VDSO producer, NOT a QEMU boot PASS.
set -Eeuo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
for tool in gcc as ld readelf objcopy cmp python3; do
  command -v "$tool" >/dev/null || { echo "MISSING_TOOL=$tool" >&2; exit 2; }
done
test -x "$minias"

gcc -m32 -fPIC -O2 -S -x c -o "$work/external.s" - <<'C'
extern int global_value;
extern int ext(int);
int foo_external(void) { return ext(global_value); }
C
gcc -m32 -fPIC -O2 -S -x c -o "$work/local.s" - <<'C'
static int local_value = 7;
int *foo_local(void) { return &local_value; }
C
cat >"$work/got_variants.s" <<'ASM'
.text
.code32
.globl got_variants
got_variants:
movl global_value@GOT(%ebx), %eax
addl global_value@GOT(%ebx), %eax
ret
.section .note.GNU-stack,"",@progbits
ASM
cat >"$work/gotoff_numeric.s" <<'ASM'
.text
.code32
.globl gotoff_numeric
gotoff_numeric:
cmpl $2147483647, 4+vvar__vdso_data@GOTOFF(%edi)
ret
.data
.globl vvar__vdso_data
.hidden vvar__vdso_data
vvar__vdso_data:
.long 0
.section .note.GNU-stack,"",@progbits
ASM

for case_name in external local got_variants gotoff_numeric; do
  as --32 -o "$work/gnu-$case_name.o" "$work/$case_name.s"
  readelf -rW "$work/gnu-$case_name.o" >"$work/gnu-$case_name.reloc"
done
python3 - "$work" <<'PY'
from pathlib import Path
import re
import sys
work = Path(sys.argv[1])
expected = {
 'external': {'R_386_PC32','R_386_GOTPC','R_386_GOT32X','R_386_PLT32'},
 'local': {'R_386_PC32','R_386_GOTPC','R_386_GOTOFF'},
 'got_variants': {'R_386_GOT32X'},
 'gotoff_numeric': {'R_386_GOTOFF'},
}
for name, required in expected.items():
    content=(work/f'gnu-{name}.reloc').read_text()
    assert "Relocation section '.rel.text'" in content, name
    actual=set(re.findall(r'\bR_386_[A-Z0-9_]+\b',content))
    assert required <= actual, (name,sorted(required-actual))
    print(f'MINIAS_I386_PIC_GNU=PASS case={name} types={",".join(sorted(required))}')
assert (work/'gnu-got_variants.reloc').read_text().count('R_386_GOT32X')==2
PY

# Link unresolved external/global symbols via *real* GNU i386 GOT/PLT, not
# merely a self-consistent relocatable .o or an ELF header check.
ld -m elf_i386 -shared -o "$work/gnu-pic.so" \
  "$work/gnu-external.o" "$work/gnu-local.o" "$work/gnu-got_variants.o" "$work/gnu-gotoff_numeric.o"
readelf -h "$work/gnu-pic.so" | grep -Eq 'Type:.*DYN'
echo 'MINIAS_I386_PIC_GNU_LINK=PASS GNU_ld_ELF32_DYN=1'

fail=0
for case_name in external local got_variants gotoff_numeric; do
  if ! "$minias" --target=x86_64 -march=i386 -mabi=i386 \
        -o "$work/mini-$case_name.o" "$work/$case_name.s" \
        >"$work/mini-$case_name.log" 2>&1; then
    echo "MINIAS_I386_PIC_CASE=$case_name FAIL stage=MiniAS"
    sed -n '1,8p' "$work/mini-$case_name.log"
    fail=$((fail+1)); continue
  fi
  readelf -rW "$work/mini-$case_name.o" >"$work/mini-$case_name.reloc"
  if ! python3 - "$work/gnu-$case_name.reloc" "$work/mini-$case_name.reloc" <<'PY'
from pathlib import Path
import sys
def text_relocs(path):
    text=False
    rows=[]
    for line in Path(path).read_text().splitlines():
        if line.startswith('Relocation section '):
            text="'.rel.text'" in line
        elif text and 'R_386_' in line:
            fields=line.split()
            if len(fields)>=5: rows.append((int(fields[0],16),fields[2],fields[-1]))
    return rows
a=text_relocs(sys.argv[1]); b=text_relocs(sys.argv[2])
if a!=b:
    print(f'GNU .rel.text DIFFERENTIAL: GNU={a} MINI={b}',file=sys.stderr)
    sys.exit(1)
PY
  then
    echo "MINIAS_I386_PIC_CASE=$case_name FAIL stage=GNU-relocations"
    fail=$((fail+1)); continue
  fi
  objcopy -O binary --only-section=.text "$work/gnu-$case_name.o" "$work/gnu-$case_name.text"
  objcopy -O binary --only-section=.text "$work/mini-$case_name.o" "$work/mini-$case_name.text"
  if ! cmp -s "$work/gnu-$case_name.text" "$work/mini-$case_name.text"; then
    echo "MINIAS_I386_PIC_CASE=$case_name FAIL stage=GNU-machine-bytes"
    fail=$((fail+1)); continue
  fi
  echo "MINIAS_I386_PIC_CASE=$case_name PASS"
done

if [[ $fail -eq 0 ]]; then
  if ld -m elf_i386 -shared -o "$work/mini-pic.so" \
       "$work/mini-external.o" "$work/mini-local.o" "$work/mini-got_variants.o"; then
    readelf -h "$work/mini-pic.so" | grep -Eq 'Type:.*DYN'
    echo 'MINIAS_I386_PIC_LINK=PASS GNU_ld_ELF32_DYN=1'
  else
    echo 'MINIAS_I386_PIC_LINK=FAIL stage=GNU-ld' >&2
    fail=$((fail+1))
  fi
fi

# GNU and MiniAS must both reject a made-up relocation suffix and RIP-only
# addressing in an i386 object.  Fail closed, especially in .code32.
cat >"$work/invalid.s" <<'ASM'
.text
.code32
movl global_value@GOTXYZ(%ebx), %eax
ASM
if as --32 -o "$work/gnu-invalid.o" "$work/invalid.s" >/dev/null 2>&1; then
  echo 'MINIAS_I386_PIC=ERROR GNU accepted invalid relocation suffix' >&2; exit 1
fi
if "$minias" --target=x86_64 -march=i386 -mabi=i386 \
     -o "$work/mini-invalid.o" "$work/invalid.s" >/dev/null 2>&1; then
  echo 'MINIAS_I386_PIC=FAIL MiniAS accepted invalid suffix' >&2; fail=$((fail+1))
fi
cat >"$work/invalid.s" <<'ASM'
.text
.code32
movl global_value@GOT(%rip), %eax
ASM
if as --32 -o "$work/gnu-invalid.o" "$work/invalid.s" >/dev/null 2>&1; then
  echo 'MINIAS_I386_PIC=ERROR GNU accepted RIP in code32' >&2; exit 1
fi
if "$minias" --target=x86_64 -march=i386 -mabi=i386 \
     -o "$work/mini-invalid.o" "$work/invalid.s" >/dev/null 2>&1; then
  echo 'MINIAS_I386_PIC=FAIL MiniAS accepted RIP in code32' >&2; fail=$((fail+1))
fi
if [[ $fail -ne 0 ]]; then
  echo "MINIAS_I386_PIC=FAIL failures=$fail" >&2
  exit 1
fi
echo 'MINIAS_I386_PIC=PASS GNU_GOT_GOTOFF_GOTPC_PLT32_link=1'
