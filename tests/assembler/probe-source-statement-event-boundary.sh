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
if 'dispatch_source_statement(source,cursor,line)' not in source or \
   'MINIAS_SOURCE_STATEMENT_LABEL' not in source or \
   'MINIAS_SOURCE_STATEMENT_TEXT' not in source:
    raise SystemExit('Source did not take ownership of lexical statement dispatch')
if '.consume=consume_source_statement' not in lowering or \
   '.site=source->current_site' not in source:
    raise SystemExit('Missing bound event sink or source identity')
if 'source->statements=(MiniAsSourceStatementSink){0};' not in source:
    raise SystemExit('Source callback lifetime not cleared at parse exit')
graph=(root/'source_graph.c').read_text()
replay=(root/'minias_source_replay_contract.h').read_text()
semantic=(root/'minias_source_semantic_services.h').read_text()
if re.search(r'\bas->(?:stmts|stmt_count)\b',graph):
    raise SystemExit('SourceGraph directly reads Common mutable statement vector')
if 'minias_fragment_internal.h' in (common+graph+private+semantic+replay) or \
   re.search(r'\bMiniAs(?:Stmt|MCInst|MCOperand)\s*\*',common+graph+replay):
    raise SystemExit('Source replay contract retains a raw Common Fragment/MCInst borrow')
if re.search(r'\bminias_query_target_relax_plan\b',graph+semantic):
    raise SystemExit('Source replay invokes Common Target relax planning directly')
if 'typedef struct MiniAsSourceReplayReader' not in replay or \
   'typedef struct MiniAsForcedOperandShape' not in replay or \
   'typedef struct MiniAsForcedOperandShape' in private or \
   'MiniAsSourceReplayReader' not in common:
    raise SystemExit('Source replay lacks a single value-projection contract')
for callback in ('fragment','operand','growth_plan'):
    if not re.search(r'\(\*'+callback+r'\)\s*\(',replay):
        raise SystemExit('Missing replay reader capability: '+callback)
for api in ('validate_fragment_sites','lookup_forced_branch',
            'rebind_forced_branches','capture_forced_branches'):
    signature=common.split('minias_source_graph_'+api+'(',1)[1].split(');',1)[0]
    if 'MiniAsSourceReplayReader *reader' not in signature or 'MiniAsStmt' in signature:
        raise SystemExit('Source replay API still borrows a Fragment: '+api)
if 'minias_common_source_replay_reader(' not in lowering or \
   'minias_common_source_replay_reader(' not in core or \
   '.fragment=common_replay_fragment' not in lowering or \
   '.operand=common_replay_operand' not in lowering or \
   '.growth_plan=common_replay_growth_plan' not in lowering:
    raise SystemExit('Common did not bind its live Fragment reader for replay')
if 'as->layout_sensitive' in source or 'minias_get_symbol(' in source:
    raise SystemExit('Source bypasses Common semantic ownership')
if 'source->services.symbol_defined(source->services.owner,args)' not in source:
    raise SystemExit('Source conditional lacks Common definition predicate')
if 'minias_source_context_layout_sensitive(as->source)' not in lowering:
    raise SystemExit('Common did not import Source conditional replay signal')
if re.search(r'\bas\s*->',source+graph):
    raise SystemExit('Source dereferences private Common session')
if 'minias_source_context_create(' not in source:
    raise SystemExit('Source pass-state factory missing')
if 'as->source=minias_source_context_create(&source_services);' not in core:
    raise SystemExit('Common no longer owns Source session lifetime binding')
if 'size_t site;' not in events or 'char *text;' not in events:
    raise SystemExit('Statement event loses writable text or stable identity')
print('MINIAS_SOURCE_EVENT_STRUCTURE=PASS one_lowerer=1 stable_site=1 label_text_typed=1 source_lexical_dispatch=1 private_router=4 no_common_label_lexer=1 no_reverse_source_callback=1 sink_unbound=1 replay=value_projection semantic_state=owned')
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
# The fake reader exercises SourceGraph replay safety without creating a
# second persistent Common Fragment graph or using a real Target parser.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  -Wstrict-prototypes -Wmissing-prototypes -Werror \
  -ffunction-sections -fdata-sections -Iassembler/src \
  tests/assembler/source-replay-projection-unit.c assembler/src/source_graph.c \
  -Wl,--gc-sections -o "$work/replay-projection-unit"
"$work/replay-projection-unit"
# The fake SourceGraph reader above checks replay algorithms. Independently
# link the actual Common projector to enforce the read-capability semantics
# on the real mutable Fragment/MCInst representation.
cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  -Wstrict-prototypes -Wmissing-prototypes -Werror \
  -ffunction-sections -fdata-sections -Iassembler/src \
  tests/assembler/source-replay-live-common-contract.c assembler/src/common_lowering.c \
  -Wl,--gc-sections -o "$work/replay-live-common-contract"
"$work/replay-live-common-contract"
