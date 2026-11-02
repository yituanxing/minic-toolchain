#!/usr/bin/env bash
# One persistent SymbolID table, with all semantic mutations owned by Context.
# Lowering parses GNU policy, MCLayout computes anchor plans, and Emission/CFI/
# DWARF request typed transitions; none writes a shared MiniAsSymbol directly.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import re

root=Path('assembler/src')
context=(root/'common_context.c').read_text()
header=(root/'minias_common_context.h').read_text()
internal=(root/'minias_internal.h').read_text()
records=(root/'minias_object_records.h').read_text()
layout=(root/'mc_layout.c').read_text()
lowering=(root/'common_lowering.c').read_text()

front=(
    'declare_symbol_binding', 'declare_symbol_type',
    'declare_symbol_size', 'capture_symbol_size_end',
    'define_source_fragment_anchor', 'assign_symbol_absolute',
    'bind_symbol_alias', 'declare_common', 'finish_local_common',
)
synthetic=(
    'define_output_anchor', 'reserve_named_dwarf_view',
    'publish_dwarf_view', 'bind_fixup_reference',
)
relayout=('commit_local_symbol_anchors', 'finalize_symbol_layout')
for suffix in front+synthetic+relayout:
    name='minias_common_context_'+suffix
    if not re.search(r'\bbool\s+'+name+r'\s*\(',context) or name not in header:
        raise SystemExit('missing unique Context Symbol transition: '+name)

if ('bool has_explicit_size;' not in records or
    'bool alias_size_tracks_target;' not in records or
    'typedef struct MiniAsSymbol {' in internal or
    '#include "minias_object_records.h"' not in internal or
    not re.search(r'alias->alias_size_tracks_target\s*&&\s*'
                  r'!alias->has_explicit_size',context)):
    raise SystemExit('alias size must track forward/dynamic sizes, not frozen snapshots')
if 'minias_refresh_symbol_aliases(' in internal or \
   'minias_common_context_publish_layout_sizes(' in header:
    raise SystemExit('obsolete cross-owner Symbol/layout mutation entry survived')
if ('const MiniAsSymbol *symbols;' not in internal or
    'MiniAsSymbol *symbol_records;' not in context or
    'minias_get_symbol(' in internal+context):
    raise SystemExit('Context did not hide its one mutable Symbol storage')
if 'minias_common_context_commit_local_symbol_anchors(' not in layout or \
   'minias_common_context_finalize_symbol_layout(' not in layout:
    raise SystemExit('MCLayout did not commit its complete Symbol plan via Context')

# Audit every production translation unit, not just today's seven migrated
# consumers. This also rejects future Target/ELF/driver writes through a
# borrowed Symbol pointer, even if they happen outside this refactor batch.
owners=tuple(path for path in root.glob('*.c') if path.name!='common_context.c')
fields=(
    'defined|section|subsection|value|size|bind|type|visibility|'
    'explicit_local_binding|has_explicit_size|alias_pending|'
    'mutable_absolute_set|has_alias_dependency|alias_target_index|'
    'alias_addend|alias_line|alias_section_base|has_fragment_anchor|'
    'fragment_anchor|fragment_anchor_addend|has_size_end_anchor|'
    'size_end_anchor|size_end_subsection|size_end_addend|'
    'has_size_start_symbol|size_start_symbol_index|size_end_value'
)
# These names designate Symbol borrows; unrelated Target/Fragment fields have
# their own owners. Compiler checks separately enforce const for *any* name.
direct=re.compile(r'\b(?:symbol|sym|alias|anchor|debug_base|view_symbol|view)\s*->\s*'
                  r'(?:'+fields+r')\s*(?:\+=|\|=|=(?!=))')
indexed=re.compile(r'\bas->symbols\s*\[[^\n]*?\]\s*\.\s*'
                   r'(?:'+fields+r')\s*(?:\+=|\|=|=(?!=))')
for path in owners:
    body=path.read_text()
    for pattern in (direct,indexed):
        found=pattern.search(body)
        if found:
            raise SystemExit(path.name+' bypasses Context Symbol state: '+found.group())

if any(name+'(' in lowering for name in ('minias_refresh_symbol_aliases',)):
    raise SystemExit('Lowering reached into the Context alias DAG')
print('MINIAS_CONTEXT_SYMBOL_OWNER=PASS frontend=9 synthetic=4 '
      f'layout=2 translation_units={len(owners)} direct_mutations=0 '
      'alias_size=GNU_binding_order')
PY
