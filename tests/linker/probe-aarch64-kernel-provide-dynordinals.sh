#!/usr/bin/env bash
# GNU BFD: original HIDDEN UND inputs resolved by late PROVIDE become
# forced-local dynamic-symbol candidates, before unresolved WEAK globals.
# The ordinal survives /DISCARD/ of the physical .dynsym.  AArch64 kernel
# shared links must use that ordinal in R_AARCH64_GLOB_DAT's raw r_info.
set -Eeuo pipefail

: "${MINIAS:?set MINIAS to minic-as}"
: "${MINILD:?set MINILD to minic-ld}"

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-aarch64-provide-dynordinal.XXXXXX")
trap 'rm -r -- "$work"' EXIT

cat >"$work/input.S" <<'ASM'
.section .text,"ax",@progbits
.global _start
_start:
    adrp x0, :got:late_hidden_a
    ldr x0, [x0, :got_lo12:late_hidden_a]
    adrp x0, :got:late_hidden_b
    ldr x0, [x0, :got_lo12:late_hidden_b]
    adrp x0, :got:weak_external
    ldr x0, [x0, :got_lo12:weak_external]
    ret
.hidden late_hidden_a
.hidden late_hidden_b
.weak weak_external
ASM

cat >"$work/kernel.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
    .text 0x400000 : { *(.text) }
    .got 0x401000 : { *(.got) }
    .got.plt 0x402000 : { *(.got.plt) }
    .rela.dyn 0x403000 : { *(.rela.dyn) }
    /DISCARD/ : { *(.interp .dynamic .dynsym .dynstr .hash .gnu.hash) }
}
PROVIDE(late_hidden_a = ABSOLUTE(0x11223344));
PROVIDE(late_hidden_b = ABSOLUTE(0x55667788));
LDS

"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/mini.o" "$work/input.S"
"$MINILD" -m aarch64linux -shared -Bsymbolic -z notext \
    --no-apply-dynamic-relocs -T "$work/kernel.lds" \
    -o "$work/mini.elf" "$work/mini.o"

if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] && \
   ! command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    echo 'MINILD_AARCH64_PROVIDE_DYNORDINAL=FAIL missing_GNU_ld' >&2
    exit 1
fi

compare=("$work/mini.elf")
if command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    aarch64-linux-gnu-ld -m aarch64linux -shared -Bsymbolic -z notext \
        --no-apply-dynamic-relocs -T "$work/kernel.lds" \
        -o "$work/gnu.elf" "$work/mini.o"
    compare+=("$work/gnu.elf")
fi

python3 - "${compare[@]}" <<'PY'
from pathlib import Path
import struct
import sys

def extract(path):
    image=Path(path).read_bytes()
    assert image[:6]==b'\x7fELF\x02\x01'
    shoff=struct.unpack_from('<Q',image,40)[0]
    entsize,count,shstr=struct.unpack_from('<HHH',image,58)
    headers=[struct.unpack_from('<IIQQQQIIQQ',image,shoff+i*entsize)
             for i in range(count)]
    tab=headers[shstr]
    names=image[tab[4]:tab[4]+tab[5]]
    sections={names[s[0]:names.index(0,s[0])].decode():s for s in headers}
    assert '.dynsym' not in sections
    assert '.rela.dyn' in sections
    rela=sections['.rela.dyn']
    records=list(struct.iter_unpack('<QQq',
                image[rela[4]:rela[4]+rela[5]]))
    glob=[rec for rec in records if (rec[1]&0xffffffff)==1025]
    assert len(glob)==1,(path,records)
    assert (glob[0][1]>>32)==3,(path,glob)
    return records

mini=extract(sys.argv[1])
print('MINILD_AARCH64_PROVIDE_DYNORDINAL=PASS '
      'hidden_undefined_provide=2 weak_global_dynindex=3')
if len(sys.argv)>2:
    gnu=extract(sys.argv[2])
    assert mini==gnu,('GNU dynamic reloc parity',gnu,mini)
    print('MINILD_AARCH64_PROVIDE_DYNORDINAL_GNU=PASS '
          'exact_rela_records_with_discarded_dynsym')
else:
    print('MINILD_AARCH64_PROVIDE_DYNORDINAL_GNU=SKIP missing_cross_binutils')
PY
