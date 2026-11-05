#!/usr/bin/env bash
# RVC pseudo width is a retained Target MCInst decision, frozen at each
# .option push/pop lexical site. Neither Common layout nor the final encoder
# may infer it from a later mutable Target option.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/pseudo.s" <<'ASM'
.text
.globl rvc_pseudo_sites
rvc_pseudo_sites:
    nop
    ret
    .option norvc
    nop
    ret
    .option push
    .option rvc
    nop
    ret
    .option pop
    nop
    ret
ASM
cat >"$work/no_c.s" <<'ASM'
.text
.globl noncompressed_pseudo
noncompressed_pseudo:
    nop
    ret
ASM
python3 - "$work" "$MINIAS" <<'PY'
import pathlib
import re
import shutil
import subprocess
import sys

work = pathlib.Path(sys.argv[1])
minias = sys.argv[2]
expected = bytes.fromhex('01008280' '13000000' '67800000'
                         '01008280' '13000000' '67800000')

def code_bytes(path):
    dump = subprocess.check_output(['readelf', '-x', '.text', str(path)], text=True)
    result = bytearray()
    for row in dump.splitlines():
        match = re.match(r'^\s*0x[0-9a-fA-F]+\s+(.+)$', row)
        if match:
            for group in match.group(1).split():
                if not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4}', group):
                    break
                result.extend(bytes.fromhex(group))
    return bytes(result)

for march, abi in [('rv64gc', 'lp64d'), ('rv32ec', 'ilp32e')]:
    obj = work / (march + '.o')
    subprocess.run([minias, '-march='+march, '-mabi='+abi,
                    '-o', str(obj), str(work/'pseudo.s')], check=True)
    result = code_bytes(obj)
    assert result == expected, (march, result.hex(), expected.hex())
    header = subprocess.check_output(['readelf', '-h', str(obj)], text=True)
    assert re.search(r'Flags:.*RVC', header), (march, 'EF_RISCV_RVC missing')
    gnu = 'riscv64-linux-gnu-as'
    gnu_available = shutil.which(gnu) is not None
    if gnu_available:
        ref = work/(march+'-gnu.o')
        subprocess.run([gnu,'-march='+march,'-mabi='+abi,
                        '-o',str(ref),str(work/'pseudo.s')],check=True)
        assert code_bytes(ref) == result, (march, code_bytes(ref).hex(), result.hex())
    print('MINIAS_RV_PSEUDO_SITE=PASS',march,'bytes='+str(len(result)),
          'GNU=' + ('exact' if gnu_available else 'NOT_TESTED:cross-toolchain-unavailable'))

obj = work/'rv64i.o'
subprocess.run([minias,'-march=rv64i','-mabi=lp64',
                '-o',str(obj),str(work/'no_c.s')],check=True)
assert code_bytes(obj) == bytes.fromhex('1300000067800000')
print('MINIAS_RV_PSEUDO_SITE_NO_C=PASS bytes=8')
PY
