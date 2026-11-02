#!/usr/bin/env bash
# Check actual ownership after the P06 Common domain split.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import re

root=Path('assembler/src')
owners={name:(root/name).read_text() for name in
        ('assembler.c','common_context.c','common_emission.c',
         'common_lowering.c','mc_leb.c')}
driver=owners['assembler.c']
context=owners['common_context.c']
emission=owners['common_emission.c']
lowering=owners['common_lowering.c']
leb=owners['mc_leb.c']
mk=Path('Makefile').read_text()

for name in ('common_context.c','common_emission.c','common_lowering.c',
             'mc_leb.c'):
    if 'assembler/src/'+name not in mk:
        raise SystemExit('Common owner not independently linked: '+name)

# Definitions, not call sites: no copied implementation or compatibility
# shim may re-create an old parser, symbol table or materializer.
functions={
  'common_context.c':('minias_find_section_key','minias_ensure_section_key',
                      'minias_get_symbol','minias_symbol_name_intern',
                      'minias_refresh_symbol_aliases',
                      'minias_common_context_destroy_sections',
                      'minias_common_context_destroy_symbols'),
  'common_emission.c':('minias_record_fixup','minias_commit_encode_result',
                       'minias_stage_fragment_bytes',
                       'minias_section_append','minias_section_append_pattern',
                       'minias_emit_data_fragment','minias_emit_sections',
                       'minias_common_emission_destroy'),
  'common_lowering.c':('lower_source_statement',
                       'minias_common_parse_source_graph'),
  'mc_leb.c':('minias_encode_uleb_integer','minias_encode_leb_integer'),
  'assembler.c':('minias_assemble_config',),
}
for owner, names in functions.items():
    for name in names:
        signature=r'(?m)^(?:static\s+)?(?:bool|int|void|size_t|uint64_t|MiniAsSymbol)\s*\*?\s*'+name+r'\s*\('
        hits=[path for path,code in owners.items() if re.search(signature,code)]
        if hits!=[owner]:
            raise SystemExit(f'{name} owner mismatch: {hits}, expected {owner}')

if ('minias_common_emit_data_stmt(' in lowering+driver or
    'emit_common_pattern(' in lowering or
    'minias_emit_data_fragment(' in lowering):
    raise SystemExit('Lowering still owns final DATA materialization')
if ('minias_common_parse_source_graph(' in emission or
    'lower_source_statement(' in emission or
    'minias_source_frame_process(' in emission):
    raise SystemExit('Emission reopened the source parser')
if ('minias_commit_encode_result(' in context or
    'minias_stage_fragment_bytes(' in context or
    'as->active_encode_result' in context):
    raise SystemExit('Context owns part of Fragment transaction')
if ('as->committing_fragment=true' not in emission or
    'as->committing_fragment=false' not in emission):
    raise SystemExit('Emission lost one-commit materialization guard')
if ('minias_common_context_destroy_sections(as);' not in driver or
    'minias_common_context_destroy_symbols(as);' not in driver or
    'minias_common_emission_destroy(as);' not in driver):
    raise SystemExit('Driver bypasses new domain destruction')
if 'as->sections' in leb or 'as->symbols' in leb:
    raise SystemExit('LEB serializer retained object-session coupling')
print('MINIAS_COMMON_P06_DOMAINS=PASS context=single emission=single lowering=single leb=pure driver=session')
PY
