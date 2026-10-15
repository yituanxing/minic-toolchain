#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M194_SMALL_LOOP_PROBE"
if marker in text:
    print("MINIC_SMALL_LOOP_PROBE_V0=ALREADY")
else:
    fn = "static bool core_cfg_small_constant_loop_call("
    start = text.find(fn)
    if start < 0:
        raise SystemExit("small-loop probe: evaluator missing")
    anchor = '''    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count < 3U || body->statement_count > 8U) {
'''
    pos = text.find(anchor, start)
    if pos < 0:
        raise SystemExit("small-loop probe: body anchor missing")
    insert = '''    body = minic_c0_program_block(program, callee->body_block);
    /* M194_SMALL_LOOP_PROBE */
    if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL && callee->name != NULL &&
        strcmp(callee->name, "skb_ext_total_length") == 0 && body != NULL) {
        size_t probe_i;
        (void)fprintf(stderr, "SMALL_LOOP_PROBE name=%s body=%zu locals=%zu\\n",
                      callee->name, body->statement_count, callee->local_count);
        for (probe_i = 0U; probe_i < body->statement_count; ++probe_i) {
            const MinicStatement *ps = minic_c0_program_statement(program, body->statements[probe_i]);
            const MinicExpression *pe = ps != NULL && ps->expression != MINIC_EXPRESSION_INVALID
                                            ? minic_c0_program_expression(program, ps->expression)
                                            : NULL;
            (void)fprintf(stderr,
                          "SMALL_LOOP_TOP i=%zu kind=%d exprkind=%d then=%zu target=%zu clean=%zu/%zu\\n",
                          probe_i, ps == NULL ? -1 : (int)ps->kind,
                          pe == NULL ? -1 : (int)pe->kind,
                          ps == NULL ? (size_t)MINIC_BLOCK_INVALID : (size_t)ps->then_block,
                          ps == NULL ? (size_t)MINIC_EXPRESSION_INVALID : (size_t)ps->target_expression,
                          ps == NULL ? 0U : (size_t)ps->cleanup_context,
                          ps == NULL ? 0U : (size_t)ps->cleanup_stop_context);
            if (ps != NULL && ps->kind == MINIC_STATEMENT_WHILE &&
                ps->then_block != MINIC_BLOCK_INVALID) {
                const MinicBlock *pb = minic_c0_program_block(program, ps->then_block);
                size_t probe_j;
                (void)fprintf(stderr, "SMALL_LOOP_BODY count=%zu\\n", pb == NULL ? 0U : pb->statement_count);
                if (pb != NULL) {
                    for (probe_j = 0U; probe_j < pb->statement_count; ++probe_j) {
                        const MinicStatement *bs = minic_c0_program_statement(program, pb->statements[probe_j]);
                        const MinicExpression *be = bs != NULL && bs->expression != MINIC_EXPRESSION_INVALID
                                                        ? minic_c0_program_expression(program, bs->expression)
                                                        : NULL;
                        (void)fprintf(stderr,
                                      "SMALL_LOOP_STMT j=%zu kind=%d exprkind=%d op=%d target=%zu clean=%zu/%zu\\n",
                                      probe_j, bs == NULL ? -1 : (int)bs->kind,
                                      be == NULL ? -1 : (int)be->kind,
                                      be != NULL && (be->kind == MINIC_EXPRESSION_BINARY ||
                                                     be->kind == MINIC_EXPRESSION_ASSIGNMENT ||
                                                     be->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT)
                                          ? (int)be->value.binary.operator_kind
                                          : be != NULL && be->kind == MINIC_EXPRESSION_UNARY
                                                ? (int)be->value.unary.operator_kind
                                                : -1,
                                      bs == NULL ? (size_t)MINIC_EXPRESSION_INVALID : (size_t)bs->target_expression,
                                      bs == NULL ? 0U : (size_t)bs->cleanup_context,
                                      bs == NULL ? 0U : (size_t)bs->cleanup_stop_context);
                        if (be != NULL && be->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
                            const MinicExpression *r = minic_c0_program_expression(program, be->value.binary.right);
                            const MinicExpression *u = r;
                            if (u != NULL && u->kind == MINIC_EXPRESSION_LVALUE_READ)
                                u = minic_c0_program_expression(program, u->value.unary.operand);
                            (void)fprintf(stderr,
                                          "SMALL_LOOP_RHS kind=%d unwrapped=%d left=%zu right=%zu\\n",
                                          r == NULL ? -1 : (int)r->kind,
                                          u == NULL ? -1 : (int)u->kind,
                                          (size_t)be->value.binary.left,
                                          (size_t)be->value.binary.right);
                            if (u != NULL && u->kind == MINIC_EXPRESSION_SUBSCRIPT) {
                                const MinicExpression *base = minic_c0_program_expression(program, u->value.subscript.base);
                                const MinicExpression *idx = minic_c0_program_expression(program, u->value.subscript.index);
                                (void)fprintf(stderr,
                                              "SMALL_LOOP_SUB basekind=%d idxkind=%d base=%zu idx=%zu\\n",
                                              base == NULL ? -1 : (int)base->kind,
                                              idx == NULL ? -1 : (int)idx->kind,
                                              (size_t)u->value.subscript.base,
                                              (size_t)u->value.subscript.index);
                            }
                        }
                    }
                }
            }
        }
    }
    if (body == NULL || body->statement_count < 3U || body->statement_count > 8U) {
'''
    text = text[:pos] + insert + text[pos + len(anchor):]
    p.write_text(text)
    print("MINIC_SMALL_LOOP_PROBE_V0=APPLIED")
