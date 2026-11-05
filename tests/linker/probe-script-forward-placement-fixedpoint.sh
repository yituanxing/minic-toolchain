#!/usr/bin/env bash
# A GNU-compatible linker-script forward reference used by address placement
# needs a bounded full-layout fixed point, not a zero-valued undefined symbol.
set -Eeuo pipefail
: "${MINIAS:?}"
: "${MINILD:?}"

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-forward-placement.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

cat >"$work/entry.s" <<'ASM'
.section .text,"ax",%progbits
.globl _start
_start:
  ret
ASM
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/entry.o" "$work/entry.s"

# This monotone equation has the unique fixed point k=3 pages:
#    k = floor(k/2) + 2.
# The first approximation from the current dot is k=2, requiring another
# complete script placement and symbol rollback before convergence.
cat >"$work/convergent.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  forward_copy = future_end;
  page_base = .;
  . += ((((future_end - page_base) / 4096) / 2) + 2) * 4096;
  future_end = .;
  .data : { *(.data) }
}
LDS
"$MINILD" -maarch64linux -static -T "$work/convergent.lds" \
    -o "$work/convergent.elf" "$work/entry.o"
python3 - "$work/convergent.elf" <<'PY_CHECK'
import struct,sys
b=open(sys.argv[1],'rb').read()
off=struct.unpack_from('<Q',b,40)[0]
shnum=struct.unpack_from('<H',b,60)[0]
ss=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*64) for i in range(shnum)]
values={}
for s in ss:
    if s[1]!=2: continue
    st=ss[s[6]]
    names=b[st[4]:st[4]+st[5]]
    for pos in range(s[4],s[4]+s[5],s[9]):
        name_off,_,_,_,value,_=struct.unpack_from('<IBBHQQ',b,pos)
        name=names[name_off:names.index(0,name_off)].decode()
        values[name]=value
assert values['page_base']==0x11000, values
assert values['future_end']==0x14000, values
assert values['forward_copy']==values['future_end'], values
print('MINILD_SCRIPT_FORWARD=PASS bounded_fixedpoint=3pages late_alias=1')
PY_CHECK

# A recursively growing placement is unsatisfiable and must fail closed
# after bounded trials without writing an apparently valid ELF.
cat >"$work/divergent.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  page_base = .;
  . += (((future_end - page_base) / 4096) + 1) * 4096;
  future_end = .;
}
LDS
if "$MINILD" -maarch64linux -static -T "$work/divergent.lds" \
    -o "$work/divergent.elf" "$work/entry.o" \
    >"$work/divergent.stdout" 2>"$work/divergent.stderr"; then
    echo 'MINILD_SCRIPT_FORWARD=FAIL divergent accepted' >&2
    exit 1
fi
grep -q 'script-forward-layout-did-not-converge' "$work/divergent.stderr"
test ! -e "$work/divergent.elf"

# Ordinary deferred symbol dependency loops have no valid placement seed:
# they must not acquire an arbitrary zero or bogus final absolute value.
cat >"$work/cycle.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  circular_a = circular_b + 1;
  circular_b = circular_a + 1;
}
LDS
if "$MINILD" -maarch64linux -static -T "$work/cycle.lds" \
    -o "$work/cycle.elf" "$work/entry.o" \
    >"$work/cycle.stdout" 2>"$work/cycle.stderr"; then
    echo 'MINILD_SCRIPT_FORWARD=FAIL cycle accepted' >&2
    exit 1
fi
grep -q 'linker-script-unresolved-forward-symbol' "$work/cycle.stderr"
test ! -e "$work/cycle.elf"
echo 'MINILD_SCRIPT_FORWARD=PASS divergent_rejected=1 cyclic_symbols_rejected=1'
