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
