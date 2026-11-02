#!/usr/bin/env bash
# A Target compilation unit may construct an MCInst at parse time but must
# never see or mutate Common's retained Fragment/variant payload records.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import os
import re
import subprocess

root=Path('assembler/src')
public=(root/'minias_target_api.h').read_text()
private=(root/'minias_fragment_internal.h').read_text()
internal=(root/'minias_internal.h').read_text()
lowering=(root/'common_lowering.c').read_text()
emission=(root/'common_emission.c').read_text()
source=(root/'source_graph_common.h').read_text()

for record in ('MiniAsStmt', 'MiniAsFragmentPayload',
               'MiniAsInstructionFragment', 'MiniAsAlignmentFragment',
               'MiniAsOriginFragment', 'MiniAsFixedDataFragment',
               'MiniAsVariableDataFragment', 'MiniAsBytesFragment'):
    if re.search(r'\b(?:struct|union)\s+'+record+r'\s*\{',public) or \
       not re.search(r'\b(?:struct|union)\s+'+record+r'\s*\{',private):
        raise SystemExit('Fragment record not private and canonical: '+record)

# Persistent MCExpr, position constraints, and Common worklists are never
# Target-facing APIs. Keep each definition in its one owning layer; an
# unneeded public forward typedef is also a capability leak.
owned_types={
    'MiniAsOrgMode':('enum',private),
    'MiniAsDeferredExpr':('forward',private),
    'MiniAsMCExprKind':('enum',private),
    'MiniAsMCExpr':('struct',private),
    'MiniAsMCData':('struct',private),
    'MiniAsInternedSymbolName':('struct',internal),
    'MiniAsPendingLocalCommon':('struct',lowering),
    'MiniAsLiteral':('struct',lowering),
}
for name,(kind,owner) in owned_types.items():
    if re.search(r'\b'+name+r'\b',public):
        raise SystemExit('Common-private type leaked to Target API: '+name)
    if kind=='forward':
        pattern=r'\btypedef\s+struct\s+'+name+r'\s+'+name+r'\s*;'
    else:
        pattern=r'\btypedef\s+'+kind+r'\s+'+name+r'\s*\{'
    if not re.search(pattern,owner):
        raise SystemExit('Common-private type missing from unique owner: '+name)
    if sum(len(re.findall(pattern,text)) for text in
           (public,private,internal,lowering))!=1:
        raise SystemExit('Common-private type has duplicate declarations: '+name)

for name in ('aarch64_target.c','loongarch64_target.c','riscv_target.c',
             'riscv_encode.c','x86_64_target.c'):
    body=(root/name).read_text()
    for banned in (r'\bMiniAsStmt\b',r'\bMiniAsFragmentPayload\b',
                   r'\bminias_fragment_internal\.h\b',
                   r'\bminias_internal\.h\b',
                   r'\bsource_graph_common\.h\b',
                   r'\bpayload\.instruction\.inst\b'):
        if re.search(banned,body):
            raise SystemExit(name+' bypasses private Fragment boundary: '+banned)
    for private_type in owned_types:
        if re.search(r'\b'+private_type+r'\b',body):
            raise SystemExit(name+' reads Common-private type: '+private_type)

if ('#include "minias_fragment_internal.h"' not in internal or
    '#include "minias_fragment_internal.h"' in source or
    '#include "minias_source_replay_contract.h"' not in source):
    raise SystemExit('Common Fragment owner or Source replay projection boundary regressed')
if ('MiniAsParseInstruction instruction={' not in lowering or
    'st->payload.instruction.inst=instruction.inst;' not in lowering or
    'if (!parsed)return false;' not in lowering or
    'MiniAsSourceSiteView source_site={' not in lowering or
    'capture_site_state(as->target_state,&source_site,&st->site)' not in lowering or
    'MiniAsCodeAlignmentView site={' not in emission or
    'append_code_alignment(&alignment_sink,&site,stmt->size)' not in emission):
    raise SystemExit('Common is not mediating the only Target Fragment access')

cc=os.environ.get('CC','cc')
def compile_source(label,code,success):
    result=subprocess.run([cc,'-std=c11','-Werror','-fsyntax-only',
                           '-Iassembler/src','-x','c','-'],input=code,
                          text=True,capture_output=True,check=False)
    if (result.returncode==0)!=success:
        raise SystemExit(label+': unexpected compile result:\n'+result.stderr)

compile_source('Common Fragment available privately',
    '#include "minias_fragment_internal.h"\n'
    'size_t good(void) {return sizeof(MiniAsStmt)+sizeof(MiniAsFragmentPayload);}\n',
    True)
compile_source('Common MCExpr and origin payloads available privately',
    '#include "minias_fragment_internal.h"\n'
    'MiniAsDeferredExpr *deferred;\n'
    'size_t good(void) {return sizeof(MiniAsOrgMode)+sizeof(MiniAsMCExprKind)'
    '+sizeof(MiniAsMCExpr)+sizeof(MiniAsMCData);}\n',True)
compile_source('Common symbol internals available privately',
    '#include "minias_internal.h"\n'
    'size_t good(void) {return sizeof(MiniAsInternedSymbolName);}\n',True)
for record in ('MiniAsStmt','MiniAsFragmentPayload'):
    compile_source(record+' hidden from Target',
        '#include "minias_target_api.h"\n'
        'size_t bad(void) {return sizeof('+record+');}\n',False)
for name in owned_types:
    compile_source(name+' hidden from Target',
        '#include "minias_target_api.h"\n'
        +name+' *bad(void) {return 0;}\n',False)
for record,field in (('MiniAsParseInstruction','op'),
                     ('MiniAsParseInstruction','args'),
                     ('MiniAsSourceSiteView','op')):
    compile_source(record+'.'+field+' lexical borrow immutable',
        '#include "minias_target_api.h"\n'
        'void bad('+record+' *p) {p->'+field+'[0]=0;}\n',False)
for record in ('MiniAsParseInstruction','MiniAsSourceSiteView',
               'MiniAsCodeAlignmentView'):
    compile_source(record+' no legacy payload',
        '#include "minias_target_api.h"\n'
        'void bad('+record+' *p) {p->payload.instruction.inst=0;}\n',False)
compile_source('legacy parser callback rejected',
    '#include "minias_target_api.h"\n'
    'static bool old(MiniAsParseContext *c,void *stmt){(void)c;(void)stmt;return true;}\n'
    'MiniAsTargetOps ops={.parse_mcinst=old};\n',False)
compile_source('legacy alignment callback rejected',
    '#include "minias_target_api.h"\n'
    'static bool old(MiniAsCodeAlignSink *s,void *stmt,size_t n){'
    '(void)s;(void)stmt;(void)n;return true;}\n'
    'MiniAsTargetOps ops={.append_code_alignment=old};\n',False)
compile_source('new phase contracts typecheck',
    '#include "minias_target_api.h"\n'
    'static bool parse(MiniAsParseContext *c,MiniAsParseInstruction *i){'
    '(void)c;(void)i;return true;}\n'
    'static bool align(MiniAsCodeAlignSink *s,'
    'const MiniAsCodeAlignmentView *i,size_t n){'
    '(void)s;(void)i;(void)n;return true;}\n'
    'MiniAsTargetOps ops={.parse_mcinst=parse,.append_code_alignment=align};\n',
    True)
print('MINIAS_TARGET_FRAGMENT_PRIVATE=PASS targets=5 '
      'hidden_records=8 hidden_common_types=8 '
      'negative_compiles=18 positive_compiles=4 '
      'Common=sole_fragment_owner')
PY
