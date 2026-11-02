#!/usr/bin/env bash
# SourceGraph cannot import the Common statement lowerer or its private
# mutable session through the public Source/Common contract. Verify both
# compilation isolation and the single-event runtime wiring.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import re

root=Path('assembler/src')
common=(root/'source_graph_common.h').read_text()
events=(root/'source_statement_event.h').read_text()
source=(root/'source_expansion.c').read_text()
private=(root/'source_graph_internal.h').read_text()
core=(root/'assembler.c').read_text()
if '#include "minias_internal.h"' in common:
    raise SystemExit('Source/Common contract exposes private session layout')
if '#include "minias_internal.h"' not in private:
    raise SystemExit('Source private implementation lacks explicit ownership')
if 'minias_statement_process' in (common+source+core):
    raise SystemExit('Legacy exported statement parser survives')
if re.search(r'\b(?:lower_source_statement|consume_source_statement)\s*\(',source):
    raise SystemExit('Source calls Common statement parser directly')
if len(re.findall(r'\bstatic bool lower_source_statement\s*\(',core))!=1:
    raise SystemExit('Common must have one private statement lowerer')
if 'return lower_source_statement(as,text,line);' not in core:
    raise SystemExit('Common local label recursion changed')
if '.consume=consume_source_statement' not in core or \
   '.site=as->source->current_site' not in source:
    raise SystemExit('Missing bound event sink or source identity')
if 'as->source->statements=(MiniAsSourceStatementSink){0};' not in source:
    raise SystemExit('Source callback lifetime not cleared at parse exit')
graph=(root/'source_graph.c').read_text()
if re.search(r'\bas->(?:stmts|stmt_count)\b',graph):
    raise SystemExit('SourceGraph directly reads Common mutable statement vector')
for call in ('as,as->stmts,as->stmt_count','&as,as.stmts,as.stmt_count'):
    if call not in core:
        raise SystemExit('Common omitted explicit immutable Fragment span: '+call)
if 'size_t site;' not in events or 'char *text;' not in events:
    raise SystemExit('Statement event loses writable text or stable identity')
print('MINIAS_SOURCE_EVENT_STRUCTURE=PASS one_lowerer=1 stable_site=1 no_legacy_parser=1 sink_unbound=1 fragments=borrowed')
PY
# Standalone contract clients can hold an opaque session, but cannot
# dereference it through this header. No Common private include is allowed.
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iassembler/src \
  -x c -fsyntax-only - <<'C'
#include "source_graph_common.h"
static bool receive(void *user,const MiniAsSourceStatementEvent *event) {
    return user!=NULL && event!=NULL && event->site!=MINIAS_SITE_NONE;
}
static const MiniAsSourceStatementSink consumer={0,receive};
int probe(void) { return consumer.consume==NULL; }
C
if cc -std=c11 -Iassembler/src -x c -fsyntax-only - 2>/dev/null <<'C'; then
#include "source_graph_common.h"
int invalid(MiniAs *session) { return (int)session->stmt_count; }
C
    echo 'Source/Common contract leaked Common session fields' >&2
    exit 1
fi
echo 'MINIAS_SOURCE_EVENT_BOUNDARY=PASS common_session=opaque consumer=typed'
