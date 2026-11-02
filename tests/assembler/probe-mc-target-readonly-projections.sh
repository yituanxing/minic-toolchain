#!/usr/bin/env bash
# Targets must not regain Common Section/Symbol or mutable view arrays by
# including the public target header. The phase views stay live and unboxed.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import os
import re
import subprocess

root=Path('assembler/src')
public=(root/'minias_target_api.h').read_text()
private=(root/'minias_internal.h').read_text()
driver=(root/'assembler.c').read_text()
context=(root/'common_context.c').read_text()
emission=(root/'common_emission.c').read_text()
layout=(root/'mc_layout.c').read_text()
writer=(root/'elf_writer.c').read_text()

for record in ('MiniAsSection','MiniAsSymbol'):
    if 'typedef struct '+record+' {' in public or \
       'typedef struct '+record+' {' not in private:
        raise SystemExit('Common mutable record escaped into Target ABI: '+record)
for phase in ('MiniAsPreLayoutView','MiniAsPostEmitView'):
    if ('typedef struct '+phase+' '+phase+';' not in public or
        'struct '+phase+' {' in public or
        'struct '+phase+' { const MiniAs *as; };' not in driver):
        raise SystemExit('Phase view is no longer stack-scoped and opaque: '+phase)
for record in ('MiniAsFragmentView','MiniAsSectionView','MiniAsSymbolView'):
    if 'typedef struct '+record+' {' not in public:
        raise SystemExit('Missing phase projection: '+record)
for service in ('minias_pre_layout_fragment','minias_post_emit_fragment',
                'minias_post_emit_section','minias_encode_ctx_symbol_lookup',
                'minias_relax_symbol_ref_lookup'):
    if service+'(' not in public:
        raise SystemExit('Missing phase read service: '+service)
if ('bool minias_project_symbol(' not in context or
    'minias_project_symbol(minias_symbol_ref_lookup(' not in emission or
    'minias_project_symbol(minias_symbol_ref_lookup(' not in layout or
    'minias_project_symbol(sym,&target_symbol)' not in writer):
    raise SystemExit('Symbol snapshot conversion duplicated or bypassed')
if ('static bool minias_project_fragment(' not in driver or
    'minias_project_fragment(view->as,index,out)' not in driver or
    '*out=(MiniAsSectionView){' not in driver or
    'section=&view->as->sections[index]' not in driver):
    raise SystemExit('Read projection no longer borrows live Common state')

targets=('aarch64_target.c','loongarch64_target.c','riscv_target.c',
         'riscv_encode.c','x86_64_target.c')
for name in targets:
    source=(root/name).read_text()
    for banned in (r'\bMiniAsSection\s*\*',r'\bMiniAsSymbol\s*\*',
                   r'view\s*->\s*stmts',r'view\s*->\s*sections',
                   r'view\s*->\s*stmt_count',
                   r'view\s*->\s*section_count'):
        if re.search(banned,source):
            raise SystemExit(f'{name}: mutable Common array/record borrow {banned}')
if 'minias_pre_layout_fragment(view,i,&stmt)' not in \
   (root/'loongarch64_target.c').read_text():
    raise SystemExit('LoongArch pre-layout bypasses kind-safe projection')

def must_reject(label,code,expected):
    cc=os.environ.get('CC','cc')
    result=subprocess.run([cc,'-std=c11','-fsyntax-only','-x','c',
                           '-Iassembler/src','-'],input=code,text=True,
                          capture_output=True,check=False)
    if result.returncode==0 or expected not in result.stderr:
        raise SystemExit(f'{label}: unexpectedly compiles or wrong failure:\n'
                         +result.stderr)

# These names are absent as complete types from the target-only ABI.
must_reject('Section hidden',
    '#include "minias_target_api.h"\n'
    'int invalid[(int)sizeof(struct MiniAsSection)];\n',
    'incomplete')
must_reject('Symbol hidden',
    '#include "minias_target_api.h"\n'
    'int invalid[(int)sizeof(struct MiniAsSymbol)];\n',
    'incomplete')
for phase in ('MiniAsPreLayoutView','MiniAsPostEmitView'):
    must_reject(phase+' opaque',
        '#include "minias_target_api.h"\n'
        'size_t bad(const '+phase+' *v) { return v->as->stmt_count; }\n',
        'incomplete')
for record,field in (('MiniAsFragmentView','op'),
                     ('MiniAsSectionView','name'),
                     ('MiniAsSectionView','data'),
                     ('MiniAsSymbolView','name')):
    must_reject(record+'.'+field+' const',
        '#include "minias_target_api.h"\n'
        'void bad('+record+' *v) { v->'+field+'[0]=1; }\n',
        'read-only')
print('MINIAS_TARGET_READONLY_PROJECTIONS=PASS opaque_phases=2 '
      'hidden_records=2 const_nested_fields=4 target_units=5 '
      'symbol_owner=single fragment_owner=live')
PY
