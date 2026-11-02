#!/usr/bin/env bash
# SectionKey metadata mutations live in Common Context. MCLayout measures a
# transient complete layout snapshot; Lowering never directly writes an ELF
# Section's persistent attributes or the published layout_size field.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import re

root=Path('assembler/src')
context=(root/'common_context.c').read_text()
lowering=(root/'common_lowering.c').read_text()
layout=(root/'mc_layout.c').read_text()
api=(root/'minias_common_context.h').read_text()

for name in ('minias_common_context_raise_section_alignment',
             'minias_common_context_apply_ident_comment',
             'minias_common_context_finalize_symbol_layout'):
    if not re.search(r'\bbool\s+'+name+r'\s*\(',context) or name not in api:
        raise SystemExit('missing Context Section transition: '+name)

if lowering.count('minias_common_context_raise_section_alignment(')!=3:
    raise SystemExit('Lowering lost an implicit/explicit/literal alignment transition')
if lowering.count('minias_common_context_apply_ident_comment(')!=1:
    raise SystemExit('GNU .ident no longer uses Context comment-upgrade semantics')
if layout.count('minias_common_context_finalize_symbol_layout(')!=1 or \
   'minias_common_context_publish_layout_sizes(' in layout:
    raise SystemExit('MCLayout bypasses the complete Section layout publication')
if not re.search(r'as->context_indexes->section_records\[i\]\.layout_size\s*=\s*section_sizes\[i\]',context):
    raise SystemExit('Context must publish all Section sizes after Symbol normalization')
if 'sec->entry_size=entry_size;' not in context:
    raise SystemExit('new SectionKey lacks first-declaration sh_entsize')
if 'minias_ensure_section_key(as,&key,group_flags,' not in lowering or \
   'type,flags,align,entry_size)' not in lowering:
    raise SystemExit('source .section failed to pass first-declaration metadata')

# A reopen may raise alignment but cannot replace first-declared type, flags
# or entry_size. The `.ident` exception is explicit and name-checked.
reopen=context.split('if(found>=0) {',1)[1].split('if(as->section_count',1)[0]
for field in ('type','flags','entry_size'):
    if re.search(r'\bsec->'+field+r'\s*(?:\|=|\+=|=(?!=))',reopen):
        raise SystemExit('reopen rewrites first-declaration '+field)
if 'strcmp(as->sections[(size_t)section].name,".comment")' not in context:
    raise SystemExit('GNU .ident upgrade lost its section-name guard')

# Catch direct writes through the shared as->sections vector, or a borrowed
# Section pointer in the two clients whose writes this migration removes.
write=re.compile(
    r'(?:as->sections\[[^;\n]*\]\.|\b(?:sec|section)->)'
    r'(?:align|flags|entry_size|layout_size)\s*(?:\|=|\+=|=(?!=))')
for owner,source in (('common_lowering.c',lowering),('mc_layout.c',layout)):
    match=write.search(source)
    if match:
        raise SystemExit(owner+' directly mutates Section metadata: '+match.group())

if 'section_layout_sizes=calloc(' not in layout or \
   'free(section_layout_sizes);' not in layout or \
   'as->sections[i].layout_size =' in layout:
    raise SystemExit('MCLayout lost its transient, single-commit layout snapshot')

print('MINIAS_CONTEXT_SECTION_METADATA_OWNER=PASS first_section=Context '
      'reopen=GNU align=Context ident=Context layout_snapshot=Context')
PY
