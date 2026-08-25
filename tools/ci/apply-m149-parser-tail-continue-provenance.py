#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M149_PARSER_TAIL_CONTINUE_PROVENANCE_OWNER'
if marker in core:
    print('M149 parser-tail continue provenance already staged')
    raise SystemExit(0)
for required in ('M137_DETACHED_LOOP_CONTINUE_OWNER',
                 'M146_OPPORTUNISTIC_DETACHED_CONTINUE_OWNER',
                 'M141_SCOPED_LOOP_CONTINUE_TAIL_OWNER',
                 'M147_NORMALIZED_FOR_CONTINUE_BINDING_OWNER',
                 'M148_NORMALIZED_FOR_UPDATE_CONTINUE_CFG_OWNER'):
    if required not in core:
        raise SystemExit(f'M149 requires staged {required}')

# M137's detached resolver inferred continue-label identity by scanning every
# GOTO in a loop subtree. M145-M148 proved that inference can alias unrelated
# same-span synthetic labels. Remove only that heuristic function. M142/M144
# insert later helper owners immediately before lower_block, so the deletion
# boundary must stop at the first later helper marker rather than lower_block.
start = core.find('/* M137_DETACHED_LOOP_CONTINUE_OWNER:')
if start < 0:
    raise SystemExit('M149 could not locate M137 resolver start')
end_candidates = [
    core.find('/* M142_NONEDGE_CLEANUP_METADATA_OWNER:', start),
    core.find('/* M144_UNREFERENCED_LOOP_LABEL_METADATA_OWNER:', start),
    core.find('static MinicCoreLowerStatus\nlower_block(', start),
]
end_candidates = [value for value in end_candidates if value >= 0]
if not end_candidates:
    raise SystemExit('M149 could not locate M137 resolver end')
end = min(end_candidates)
if end <= start:
    raise SystemExit('M149 invalid M137 resolver deletion range')
core = core[:start] + core[end:]

old_while = '''            case MINIC_STATEMENT_WHILE: {
                MinicStatementId detached_continue_label;

                /* M146_OPPORTUNISTIC_DETACHED_CONTINUE_OWNER: detached parser
                   metadata recovery is an optional refinement of ordinary WHILE
                   lowering, never a new validity gate.  M137 can fail discovery
                   when the loop subtree has ambiguous same-span internal labels
                   or when its graph cannot be classified conservatively.  In
                   either case preserve the pre-M137 behavior by lowering with no
                   detached target.  Only a positively resolved unique target is
                   allowed to change the loop CFG. */
                detached_continue_label = MINIC_STATEMENT_INVALID;
                if (!core_resolve_detached_while_continue_label(
                        context, statement, &detached_continue_label)) {
                    detached_continue_label = MINIC_STATEMENT_INVALID;
                }
                status = lower_while(
                    context, statement, detached_continue_label, &statement_terminated);
                break;
            }
'''
new_while = '''            case MINIC_STATEMENT_WHILE:
                status = lower_while(
                    context, statement, MINIC_STATEMENT_INVALID, &statement_terminated);
                break;
'''
if core.count(old_while) != 1:
    raise SystemExit(f'M149 expected one M146 standalone while seam, found {core.count(old_while)}')
core = core.replace(old_while, new_while, 1)

old_decl = '''    MinicStatementId normalized_do_while_continue;
    MinicCoreBlockId body_block;
'''
new_decl = '''    MinicStatementId normalized_do_while_continue;
    MinicStatementId normalized_for_continue;
    MinicCoreBlockId body_block;
'''
if core.count(old_decl) != 1:
    raise SystemExit(f'M149 expected one normalized-loop declaration seam, found {core.count(old_decl)}')
core = core.replace(old_decl, new_decl, 1)

old_init = '''    iteration_source = body_source;
    for_update = NULL;
    normalized_for = false;
'''
new_init = '''    iteration_source = body_source;
    for_update = NULL;
    normalized_for = false;
    normalized_for_continue = MINIC_STATEMENT_INVALID;
'''
if core.count(old_init) != 1:
    raise SystemExit(f'M149 expected one normalized-for initialization seam, found {core.count(old_init)}')
core = core.replace(old_init, new_init, 1)

anchor = '    /* M141_SCOPED_LOOP_CONTINUE_TAIL_OWNER:'
pos = core.find(anchor)
if pos < 0:
    raise SystemExit('M149 could not locate M141 normalized-for tail seam')
provenance = '''    /* M149_PARSER_TAIL_CONTINUE_PROVENANCE_OWNER: normalized_for_update_tail
       and normalized_for_continue_tail have already proven the exact parser
       shape and same-span synthetic LABEL at the source for-loop tail.  Use
       that statement id directly; never recover identity by scanning GOTOs.
       If the adjacent-label path also supplied an id, require exact agreement.
       This keeps ordinary while ownership on its pre-M137 adjacent-label path
       while giving detached normalized-for views one structural provenance. */
    if (normalized_for) {
        size_t continue_tail_distance = for_update != NULL ? 2U : 1U;

        if (body_source->statement_count < continue_tail_distance) {
            return MINIC_CORE_LOWER_ERROR;
        }
        normalized_for_continue =
            body_source->statements[body_source->statement_count - continue_tail_distance];
        if (normalized_for_continue == MINIC_STATEMENT_INVALID ||
            normalized_for_continue >= context->statement_block_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (continue_label_statement != MINIC_STATEMENT_INVALID &&
            continue_label_statement != normalized_for_continue) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        continue_label_statement = normalized_for_continue;
    }
'''
core = core[:pos] + provenance + core[pos:]

old_binding = '    if (continue_label_statement != MINIC_STATEMENT_INVALID && normalized_for) {\n'
new_binding = '    if (continue_label_statement != MINIC_STATEMENT_INVALID) {\n'
if core.count(old_binding) != 1:
    raise SystemExit(f'M149 expected one M147/M148 binding condition, found {core.count(old_binding)}')
core = core.replace(old_binding, new_binding, 1)

core_path.write_text(core)

regression = Path('tests/compiler/c0/m149_parser_tail_continue_provenance.c')
regression.write_text(r'''static int plain_while_continue(int n) {
    int total = 0;
    while (n > 0) {
        n -= 1;
        if (n & 1)
            continue;
        total += n;
    }
    return total;
}

static int for_update_continue(int limit) {
    int i;
    int total = 0;
    for (i = 0; i < limit; i += 1) {
        if (i & 1)
            continue;
        total += i;
    }
    return total;
}

static int for_unbounded_continue(int limit) {
    int i = 0;
    for (;;) {
        i += 1;
        if (i < limit)
            continue;
        return i;
    }
}

int main(void) {
    return plain_while_continue(5) == 6 &&
           for_update_continue(6) == 6 &&
           for_unbounded_continue(4) == 4 ? 0 : 1;
}
''')

print('M149 parser-tail continue provenance staged')
