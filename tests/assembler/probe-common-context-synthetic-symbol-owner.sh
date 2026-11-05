#!/usr/bin/env bash
# CFI, DWARF, MCExpr and CodeEmitter still own their source events/fixups,
# but only Context can define/refresh the one retained SymbolID table.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import re

root=Path('assembler/src')
context=(root/'common_context.c').read_text()
api=(root/'minias_common_context.h').read_text()
clients={name:(root/name).read_text() for name in (
    'cfi.c','cfi_writer.c','debug_line.c','mc_expr.c','common_emission.c')}

required=(
    'minias_common_context_define_source_fragment_anchor',
    'minias_common_context_define_output_anchor',
    'minias_common_context_reserve_named_dwarf_view',
    'minias_common_context_publish_dwarf_view',
    'minias_common_context_bind_fixup_reference',
)
for name in required:
    if not re.search(r'\bbool\s+'+name+r'\s*\(',context) or name not in api:
        raise SystemExit('missing sole Context Symbol capability: '+name)

expected={
    'cfi.c':('minias_common_context_define_source_fragment_anchor',),
    'cfi_writer.c':('minias_common_context_define_output_anchor',),
    'debug_line.c':('minias_common_context_reserve_named_dwarf_view',
                    'minias_common_context_publish_dwarf_view'),
    'mc_expr.c':('minias_common_context_define_output_anchor',),
    'common_emission.c':('minias_common_context_define_output_anchor',
                         'minias_common_context_bind_fixup_reference'),
}
for name,functions in expected.items():
    body=clients[name]
    for function in functions:
        if function+'(' not in body:
            raise SystemExit(name+' bypasses Context capability: '+function)
    # Only the domain owner may write Symbol contents. This catches both
    # borrowed pointer aliases and direct as->symbols[ID] mutations; reads
    # remain allowed for GNU CFI/reloc arithmetic and DWARF location sorting.
    symbol_fields=('defined','section','subsection','value','size','bind',
                   'type','visibility','alias_pending','has_fragment_anchor',
                   'fragment_anchor','size_end_value')
    lhs=(r'(?:\b(?:sym|symbol|anchor|view|debug_base)->|'
         r'\bas->symbols\[[^\]\n]+\]\.)'
         r'(?:'+'|'.join(symbol_fields)+r')\s*')
    if re.search(lhs+r'(?:\+=|-=|\|=|(?<![=!<>])=(?!=))',body):
        raise SystemExit(name+' directly writes the shared Symbol record')

if clients['debug_line.c'].count('minias_common_context_publish_dwarf_view(')!=2:
    raise SystemExit('DWARF provisional/final view updates lost a Context path')
if ('MINIAS_SOURCE_CFI' not in clients['cfi.c'] or
    'as->stmt_count' not in clients['cfi.c']):
    raise SystemExit('CFI source fragment anchor lost its source boundary')
if ('result->anchors[prior].name_id==request->name_id' not in
    clients['common_emission.c']):
    raise SystemExit('duplicate encoded anchor names not rejected precommit')
if ('minias_section_patch_le32(as,sec,begin,0U)' not in
    clients['cfi_writer.c'] or
    'memset(data->data+begin' in clients['cfi_writer.c']):
    raise SystemExit('CFI writer bypasses Emission byte patch ownership')

print('MINIAS_CONTEXT_SYNTHETIC_SYMBOL_OWNER=PASS source_anchor=CFI+Dwarf '
      'output_anchor=CFI+MCExpr+Emission dwarf_view=recomputable '
      'implicit_extern=Context duplicate_anchor=preflight bytes=Emission')
PY
