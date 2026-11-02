#!/usr/bin/env bash
# The FragmentKind is the sole payload discriminator across all MC phases.
# A non-instruction may hold a pointer-sized DATA value: checking the pointer
# before checking its kind is a real invalid read (LoongArch pre-layout case).
set -Eeuo pipefail

python3 - <<'PY'
from pathlib import Path
import os
import re
import subprocess

root=Path('assembler/src')
api=(root/'minias_target_api.h').read_text()
private=(root/'minias_fragment_internal.h').read_text()
lowering=(root/'common_lowering.c').read_text()
layout=(root/'mc_layout.c').read_text()
expr=(root/'mc_expr.c').read_text()
emission=(root/'common_emission.c').read_text()
source=(root/'source_graph.c').read_text()
la=(root/'loongarch64_target.c').read_text()

required={
    'instruction':'MiniAsInstructionFragment',
    'alignment':'MiniAsAlignmentFragment',
    'origin':'MiniAsOriginFragment',
    'fixed_data':'MiniAsFixedDataFragment',
    'variable':'MiniAsVariableDataFragment',
    'bytes':'MiniAsBytesFragment',
}
if 'typedef union MiniAsFragmentPayload {' in api or \
   'typedef struct MiniAsStmt {' in api:
    raise SystemExit('Mutable Fragment records escaped into Target header')
body=private.split('typedef union MiniAsFragmentPayload {',1)[1].split(
    '} MiniAsFragmentPayload;',1)[0]
for member, type_name in required.items():
    if not re.search(r'\b'+type_name+r'\s+'+member+r'\s*;',body):
        raise SystemExit('Fragment union member missing: '+member)
stmt=private.split('typedef struct MiniAsStmt {',1)[1].split('} MiniAsStmt;',1)[0]
if not re.search(r'\bMiniAsFragmentPayload\s+payload\s*;',stmt):
    raise SystemExit('MiniAsStmt has no discriminated payload')
if stmt.count('MiniAsFragmentPayload')!=1:
    raise SystemExit('multiple Fragment payloads/compatibility paths')
old=('mcinst','mcdata','static_data','static_data_size',
     'align','align_has_fill','align_fill','align_has_max_skip',
     'align_max_skip','org_mode','org_expr','org_target',
     'org_symbol_index','org_addend','variable_exprs',
     'variable_expr_count','variable_data_resolved','variable_repeat',
     'variable_value','variable_element_width','deferred_data_size',
     'leb_mode')
for field in old:
    if re.search(r'\b'+field+r'\s*(?:\[\s*\d+\s*\])?\s*;',stmt):
        raise SystemExit('old flat payload retained: '+field)

if 'switch(stmt->fragment_kind)' not in lowering or any(
    'minias_'+owner+'_destroy(stmt->payload.'+member not in lowering
    for owner,member in (('mcinst','instruction.inst'),
                         ('mcdata','fixed_data.data'),
                         ('deferred_expr','origin.expr'))):
    raise SystemExit('destructor must unwind only the active union variant')
for fragment in ('MINIAS_FRAGMENT_INSTRUCTION','MINIAS_FRAGMENT_ORIGIN',
                 'MINIAS_FRAGMENT_FIXED_DATA','MINIAS_FRAGMENT_FILL',
                 'MINIAS_FRAGMENT_SPACE','MINIAS_FRAGMENT_BYTES',
                 'MINIAS_FRAGMENT_ALIGNMENT'):
    if 'case '+fragment+':' not in lowering:
        raise SystemExit('destructor missing kind '+fragment)
if ('fragment_has_deferred_data(stmt)' not in layout or
    'stmt->payload.bytes.leb_expr' not in layout or
    'stmt->payload.variable.exprs' not in expr or
    'stmt->fragment_kind!=MINIAS_FRAGMENT_FIXED_DATA' not in expr or
    'stmt->payload.alignment.has_fill' not in emission or
    'stmt->payload.origin.has_fill' not in emission):
    raise SystemExit('Common layout/expression/emission variant routing regressed')

start=la.index('static bool la_prepare_layout(')
end=la.index('static bool la_section_is_linker_relaxable(',start)
prelayout=la[start:end]
kind=prelayout.find('stmt.kind==MINIAS_FRAGMENT_INSTRUCTION')
inst=prelayout.find('stmt.instruction_opcode')
if (kind<0 or inst<0 or kind>inst or
    'minias_pre_layout_fragment(view,i,&stmt)' not in prelayout or
    'stmt.has_instruction' not in prelayout):
    raise SystemExit('LoongArch pre-layout bypassed guarded instruction projection')
start=source.index('static bool source_graph_forced_shape_matches(')
end=source.index('bool minias_source_graph_rebind_forced_branches(',start)
forced=source[start:end]
if forced.find('stmt->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION') > \
   forced.find('inst=stmt->payload.instruction.inst'):
    raise SystemExit('SourceGraph reads instruction arm before kind check')

# Compile against the private Fragment contract to reject stale flat fields.
# The Target-only header is separately checked to lack this type entirely.
code='#include "minias_fragment_internal.h"\nvoid probe(MiniAsStmt *stmt) {\n'
for field in old:
    code+='(void)stmt->'+field+';\n'
code+='}\n'
result=subprocess.run([os.environ.get('CC','cc'),'-std=c11',
                       '-fsyntax-only','-x','c','-Iassembler/src','-'],
                      input=code,text=True,capture_output=True,check=False)
if result.returncode==0:
    raise SystemExit('old flat MiniAsStmt field access still compiles')
for field in old:
    if not re.search(r'\b'+field+r'\b',result.stderr):
        raise SystemExit('negative compiler check did not reject '+field)
print(f'MINIAS_FRAGMENT_PAYLOAD_UNION=PASS variants=6 kinds=7 '
      f'old_fields={len(old)} destructor=discriminated '
      'target_prelayout=guarded source_replay=guarded')
PY
