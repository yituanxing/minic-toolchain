#!/usr/bin/env bash
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
p=Path('assembler/src/cfi.c').read_text()
w=Path('assembler/src/cfi_writer.c').read_text()
h=Path('assembler/src/minias_internal.h').read_text()
d=Path('assembler/src/assembler.c').read_text()
m=Path('Makefile').read_text()
for test,ok in [
  ('target-free parser', 'minias_cfi_emit(' not in p),
  ('writer owns serializer', 'bool minias_cfi_emit(' in w),
  ('opaque payload', 'CFI_ESCAPE' in h and 'byte_count' in h),
  ('single escape anchor', 'cfi_append(as,CFI_ESCAPE,0U,0)' in p and
                           'CFI_ESCAPE_BYTE' not in p),
  ('payload freed', 'free(as->cfi_ops[i].bytes)' in d),
  ('build owns DWARF writer', 'assembler/src/cfi_writer.c' in m),
  ('typed escape bytecode', 'op->byte_count' in w and
                            'minias_section_append(as,sec,op->bytes' in w)]:
    if not ok:raise SystemExit('CFI ownership mismatch: '+test)
print('MINIAS_CFI_OBJECT_MODEL=PASS parser=separate bytecode=retained anchors=one writer=common')
PY
