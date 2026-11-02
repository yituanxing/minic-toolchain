#!/usr/bin/env bash
# Check actual ownership after the P06 Common domain split.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import os
import re
import subprocess

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
internal=(root/'minias_internal.h').read_text()
private=(root/'minias_common_emission_private.h').read_text()
contract=(root/'minias_common_emission.h').read_text()

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
if ('as->emission->committing_fragment=true' not in emission or
    'as->emission->committing_fragment=false' not in emission):
    raise SystemExit('Emission lost one-commit materialization guard')
if ('minias_common_context_destroy_sections(as);' not in driver or
    'minias_common_context_destroy_symbols(as);' not in driver or
    'minias_common_emission_destroy(as);' not in driver):
    raise SystemExit('Driver bypasses new domain destruction')
if 'as->sections' in leb or 'as->symbols' in leb:
    raise SystemExit('LEB serializer retained object-session coupling')

# A Common-private include is not an ownership boundary if unrelated modules
# can still change the result vector, fragment guard or anchor ordinal.
if 'MiniAsEmissionState *emission;' not in internal or \
   'struct MiniAsEmissionState {' in internal or \
   'struct MiniAsEmissionState {' not in private:
    raise SystemExit('Emission state is no longer opaque in the session')
mutable=('fixups','fixup_count','fixup_capacity','active_encode_result',
         'emitting_fragment','committing_fragment','emitting_fragment_id',
         'synthetic_anchor_counter')
for field in mutable:
    if re.search(r'\bas\s*->\s*'+field+r'\b', '\n'.join(
            path.read_text() for path in root.glob('*.c'))):
        raise SystemExit('legacy session-level Emission write: '+field)
    if re.search(r'\b'+field+r'\s*;',internal):
        raise SystemExit('exposed Emission state field: '+field)
private_users=sorted(p.name for p in root.glob('*.[ch]') if
                     '#include "minias_common_emission_private.h"' in
                     p.read_text())
if private_users!=['common_emission.c','mc_encode_result.c']:
    raise SystemExit('Emission private definition leaked: '+str(private_users))
if 'const MiniAsFixup *data;' not in contract or \
   'MiniAsFixupSpan minias_emission_fixups(' not in contract:
    raise SystemExit('read-only committed Fixup view missing')
for filename in ('mc_layout.c','elf_writer.c'):
    source=(root/filename).read_text()
    if 'minias_emission_fixups(as)' not in source or \
       'const MiniAsFixup *fixup=' not in source and filename=='mc_layout.c':
        raise SystemExit('Fixup consumer bypasses immutable view: '+filename)
expr=(root/'mc_expr.c').read_text()
if expr.count('minias_emission_mcdata_cursor(as,stmt,')!=2 or \
   'as->emission->' in expr:
    raise SystemExit('MCData lost its per-field staged-byte cursor')
if 'minias_common_emission_init(as)' not in driver or \
   driver.index('minias_common_emission_init(as)') > \
   driver.index('minias_source_context_create()'):
    raise SystemExit('Emission initialized after source parsing')

# Prove the public session header cannot be used to dereference the private
# state; a negative compilation catches regressions that text matching misses.
probe='#include "minias_internal.h"\nint probe(MiniAs *as) { return (int)as->emission->fixup_count; }\n'
result=subprocess.run([os.environ.get('CC','cc'),'-std=c11','-fsyntax-only',
                       '-x','c','-Iassembler/src','-'],input=probe,text=True,
                       capture_output=True,check=False)
if result.returncode==0 or 'incomplete' not in result.stderr and \
   'undefined' not in result.stderr:
    raise SystemExit('opaque emission field unexpectedly accessible: '+result.stderr)
print('MINIAS_COMMON_P06_DOMAINS=PASS context=single emission=opaque lowering=single leb=pure fixups=const driver=session')
PY
