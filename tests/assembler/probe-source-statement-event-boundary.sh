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
lowering=(root/'common_lowering.c').read_text()
if '#include "minias_internal.h"' in common:
    raise SystemExit('Source/Common contract exposes private session layout')
if '#include "minias_internal.h"' in private:
    raise SystemExit('Source private implementation imports Common session')
if '#include "minias_source_semantic_services.h"' not in private:
    raise SystemExit('Missing Source-only semantic service contract')
if 'minias_statement_process' in (common+source+core+lowering):
    raise SystemExit('Legacy exported statement parser survives')
if re.search(r'\b(?:lower_source_statement|consume_source_statement)\s*\(',source):
    raise SystemExit('Source calls Common statement parser directly')
if len(re.findall(r'\bstatic bool lower_source_statement\s*\(',lowering))!=1:
    raise SystemExit('Common must have one private statement lowerer')
if re.search(r'\bminias_source_try_(?:conditional|macro_invocation|control_directive)\s*\(',lowering) or \
   'minias_source_conditional_active(as)' in lowering:
    raise SystemExit('Common still routes conditional, macro or control decisions back into Source')
for private_service in ('minias_source_try_conditional',
                        'minias_source_conditional_active',
                        'minias_source_try_macro_invocation',
                        'minias_source_try_control_directive'):
    if private_service in common:
        raise SystemExit(f'Source lexical router leaked through Common contract: {private_service}')
    if not re.search(r'\bstatic bool\s+'+private_service+r'\s*\(',source):
        raise SystemExit(f'Source lexical router must be TU-private: {private_service}')
if 'return lower_source_statement(as,text,line)' in lowering:
    raise SystemExit('Common still recursively routes a label tail around Source dispatch')
if 'MiniAsSourceStatementKind kind;' not in events or \
   'MINIAS_SOURCE_STATEMENT_LABEL' not in events or \
   'MINIAS_SOURCE_STATEMENT_TEXT' not in events:
    raise SystemExit('Source/Common event lacks a single typed label/text identity')
statement_lowerer=lowering.split('static bool lower_source_statement',1)[1].split('static bool consume_source_statement',1)[0]
if re.search(r'for\s*\(\s*;\s*;\s*\)\s*\{\s*char \*token_end',statement_lowerer):
    raise SystemExit('Common still lexes leading labels a second time')
if 'case MINIAS_SOURCE_STATEMENT_LABEL:' not in lowering or \
   'return define_label(as,event->text,event->line);' not in lowering or \
   'case MINIAS_SOURCE_STATEMENT_TEXT:' not in lowering or \
   'return lower_source_statement(as,event->text,event->line);' not in lowering or \
   'invalid-source-statement-kind' not in lowering:
    raise SystemExit('Common sink must handle typed label/text events without a fallback')
if 'dispatch_source_statement(as,cursor,line)' not in source or \
   'MINIAS_SOURCE_STATEMENT_LABEL' not in source or \
   'MINIAS_SOURCE_STATEMENT_TEXT' not in source:
    raise SystemExit('Source did not take ownership of lexical statement dispatch')
if '.consume=consume_source_statement' not in lowering or \
   '.site=minias_source_session(as)->current_site' not in source:
    raise SystemExit('Missing bound event sink or source identity')
if 'minias_source_session(as)->statements=(MiniAsSourceStatementSink){0};' not in source:
    raise SystemExit('Source callback lifetime not cleared at parse exit')
graph=(root/'source_graph.c').read_text()
if re.search(r'\bas->(?:stmts|stmt_count)\b',graph):
    raise SystemExit('SourceGraph directly reads Common mutable statement vector')
if 'as,as->stmts,as->stmt_count' not in lowering or \
   '&as,as.stmts,as.stmt_count' not in core:
    raise SystemExit('Common omitted immutable Fragment spans across lowering/driver')
if 'as->layout_sensitive' in source or 'minias_get_symbol(' in source:
    raise SystemExit('Source bypasses Common semantic ownership')
if 'minias_common_source_symbol_defined(as,args)' not in source:
    raise SystemExit('Source conditional lacks Common definition predicate')
if 'minias_source_context_layout_sensitive(as->source)' not in lowering:
    raise SystemExit('Common did not import Source conditional replay signal')
if re.search(r'\bas\s*->',source+graph):
    raise SystemExit('Source dereferences private Common session')
if 'minias_source_context_create(' not in source:
    raise SystemExit('Source pass-state factory missing')
if 'as->source=minias_source_context_create();' not in core:
    raise SystemExit('Common no longer owns Source session lifetime binding')
if 'size_t site;' not in events or 'char *text;' not in events:
    raise SystemExit('Statement event loses writable text or stable identity')
print('MINIAS_SOURCE_EVENT_STRUCTURE=PASS one_lowerer=1 stable_site=1 label_text_typed=1 source_lexical_dispatch=1 private_router=4 no_common_label_lexer=1 no_reverse_source_callback=1 sink_unbound=1 fragments=borrowed semantic_state=owned')
PY
# Standalone contract clients can hold an opaque session, but cannot
# dereference it through this header. No Common private include is allowed.
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iassembler/src \
  -x c -fsyntax-only - <<'C'
#include "source_graph_common.h"
static bool receive(void *user,const MiniAsSourceStatementEvent *event) {
    return user!=NULL && event!=NULL && event->site!=MINIAS_SITE_NONE &&
           (event->kind==MINIAS_SOURCE_STATEMENT_LABEL ||
            event->kind==MINIAS_SOURCE_STATEMENT_TEXT);
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
if cc -std=c11 -Wall -Wextra -Werror -Iassembler/src -x c -fsyntax-only - 2>/dev/null <<'C'; then
#include "source_graph_common.h"
int invalid(void) {
    MiniAsSourceStatementEvent event={.kind=MINIAS_SOURCE_STATEMENT_LABEL,
                                      .label_name="shadow-owner"};
    return event.text!=NULL;
}
C
    echo 'Source event gained a duplicate label spelling representation' >&2
    exit 1
fi
echo 'MINIAS_SOURCE_EVENT_BOUNDARY=PASS common_session=opaque label_text=typed consumer=typed no_shadow_label=1'
