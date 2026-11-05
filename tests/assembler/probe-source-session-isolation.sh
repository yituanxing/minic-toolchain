#!/usr/bin/env bash
# A Source implementation must not be able to dereference a Common session.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import re
root=Path('assembler/src')
for name in ('source_graph.c','source_expansion.c','source_graph_internal.h',
             'source_graph_common.h','minias_source_semantic_services.h'):
    s=(root/name).read_text()
    if '#include "minias_internal.h"' in s:
        raise SystemExit('Common session header leaked: '+name)
for name in ('source_graph.c','source_expansion.c'):
    s=(root/name).read_text()
    if re.search(r'\bas\s*->',s) or 'MiniAsTargetOps' in s:
        raise SystemExit('Source still imports mutable Common/Target: '+name)
    if re.search(r'\bMiniAs\s*\*',s) or 'minias_source_session(' in s or \
       'minias_source_const_session(' in s:
        raise SystemExit('Source still carries a full Common session token: '+name)
semantic=(root/'minias_source_semantic_services.h').read_text()
driver=(root/'assembler.c').read_text()
if ('typedef struct MiniAsSourceSemanticServices {' not in semantic or
    'void *owner;' not in semantic or
    'MiniAsSourceSemanticServices services;' not in (root/'source_graph_internal.h').read_text() or
    'minias_source_context_create(&source_services)' not in driver or
    'source->services=*services;' not in (root/'source_expansion.c').read_text()):
    raise SystemExit('Source semantic services not bound as one invocation capability')
for callback in ('evaluate_absolute','evaluate_target_if','symbol_defined',
                 'rewrite_numeric_refs','error_va'):
    if not re.search(r'\(\*'+callback+r'\)\s*\(',semantic) or \
       '.'+callback+'=' not in driver:
        raise SystemExit('Missing Source/Common semantic capability: '+callback)
for removed in ('minias_source_target_comment_prefix(',
                'minias_source_target_evaluate_if(',
                'minias_common_source_symbol_defined(',
                'minias_source_session(',
                'minias_source_const_session('):
    if removed in driver+semantic:
        raise SystemExit('Old broad Common-to-Source entry survives: '+removed)
internal=(root/'minias_internal.h').read_text()
contract=(root/'minias_source_replay_contract.h').read_text()
if 'typedef struct MiniAsFragmentGrowthSet' in internal or \
   'typedef struct MiniAsFragmentGrowthSet' not in contract:
    raise SystemExit('Duplicate or missing Source/MCLayout replay contract')
print('MINIAS_SOURCE_SESSION_OPAQUE=PASS files=2 field_access=0 replay_contract=shared')
PY
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iassembler/src -x c -fsyntax-only - <<'C'
#include "source_graph_internal.h"
static bool consume(void *user,const MiniAsSourceStatementEvent *event) {
    return user!=NULL && event!=NULL;
}
int probe(void) {
    MiniAsSourceContext ctx={0};
    MiniAsSourceStatementSink sink={0,consume};
    MiniAsFragmentGrowthSet growth={0};
    ctx.statements=sink;
    return ctx.source_ended || growth.count!=0U;
}
C
if cc -std=c11 -Iassembler/src -x c -fsyntax-only - 2>/dev/null <<'C'; then
#include "source_graph_internal.h"
int invalid(MiniAs *session) { return (int)session->symbol_count; }
C
    echo 'Source-only headers leaked Common session definition' >&2
    exit 1
fi
echo 'MINIAS_SOURCE_SESSION_COMPILE=PASS Common_session=opaque'
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iassembler/src -x c -fsyntax-only - <<'C'
#include "source_graph_internal.h"
bool source_client(MiniAsSourceContext *source,const char *expression) {
    int64_t value=0;
    return source->services.evaluate_absolute(source->services.owner,
                                              expression,&value);
}
C
if cc -std=c11 -Werror -Iassembler/src -x c -fsyntax-only - 2>/dev/null <<'C'; then
#include "source_graph_internal.h"
int invalid(MiniAsSourceContext *source) {
    return source->services.owner->symbol_count;
}
C
    echo 'Source can dereference opaque Common semantic owner' >&2
    exit 1
fi
if cc -std=c11 -Werror -Iassembler/src -x c -fsyntax-only - 2>/dev/null <<'C'; then
#include "source_graph_common.h"
int invalid(MiniAs *as) { return (int)(as!=0); }
C
    echo 'Source public contract still names the entire MiniAs session' >&2
    exit 1
fi
echo 'MINIAS_SOURCE_SEMANTIC_CAPABILITIES=PASS callbacks=5 positive=1 negative=2 broad_session=absent'
