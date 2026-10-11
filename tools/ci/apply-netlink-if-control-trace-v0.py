#!/usr/bin/env python3
"""Temporary C0 control-flow decision trace for genl_op_from_split only.

No semantic change. Emits exactly which IFs were fact-pruned and whether a
Core conditional terminator was created, before deciding any further repair.
"""
from pathlib import Path

p=Path("src/core/core_lower.c")
s=p.read_text()
marker="        if (condition_known) {\n            const MinicBlock *discarded_source;"
prefix=r'''        if (context->source_function != NULL &&
            context->source_function->name != NULL &&
            strcmp(context->source_function->name, "genl_op_from_split") == 0) {
            (void)fprintf(stderr,
                          "GENL_IF_DECISION expression=%zu kind=%d known=%d zero=%d block=%u\n",
                          (size_t)statement->expression,
                          (int)condition_expression->kind,
                          condition_known ? 1 : 0,
                          condition_is_zero ? 1 : 0,
                          (unsigned int)context->block_id);
        }
'''
if s.count(marker)!=1:
    raise SystemExit(f"genl decision anchor count={s.count(marker)}")
s=s.replace(marker,prefix+marker,1)
anchor='''    status = lower_expression(context, expression_id, &condition);
    if (status != MINIC_CORE_LOWER_OK) {
'''
prefix2=r'''    if (context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0) {
        (void)fprintf(stderr,
                      "GENL_COND_EMIT expression=%zu kind=%d block=%u true=%u false=%u\n",
                      (size_t)expression_id,
                      (int)expression->kind,
                      (unsigned int)context->block_id,
                      (unsigned int)when_true,
                      (unsigned int)when_false);
    }
'''
if s.count(anchor)!=1:
    raise SystemExit(f"genl emit anchor count={s.count(anchor)}")
s=s.replace(anchor,prefix2+anchor,1)
# Trace every recursive branch lowering entry, just after argument validation.
# Do not combine the null/type guard with the following unary predicate: they
# are distinct statements in product Core and must not be matched as one.
probe='''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {'''
hook=r'''    if (context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0) {
        (void)fprintf(stderr,
                      "GENL_COND_ENTER expression=%zu kind=%d block=%u true=%u false=%u op=%d\\n",
                      (size_t)expression_id,
                      (int)expression->kind,
                      (unsigned int)context->block_id,
                      (unsigned int)when_true,
                      (unsigned int)when_false,
                      expression->kind == MINIC_EXPRESSION_BINARY
                          ? (int)expression->value.binary.operator_kind
                          : expression->kind == MINIC_EXPRESSION_UNARY
                          ? (int)expression->value.unary.operator_kind : -1);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {'''
# Limit the match to the implementation of lower_condition_branch(), not
# unrelated unary-expression paths elsewhere in this large Core module.
function_head='''static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false) {'''
if s.count(function_head)!=1:
    raise SystemExit(f"genl branch function anchor count={s.count(function_head)}")
start=s.index(function_head)
end=s.index("    status = lower_expression(context, expression_id, &condition);",start)
fragment=s[start:end]
if fragment.count(probe)!=1:
    raise SystemExit(f"genl branch recurse scoped anchor count={fragment.count(probe)}")
s=s[:start]+fragment.replace(probe,hook,1)+s[end:]
p.write_text(s)
print("MINIC_NETLINK_IF_CONTROL_TRACE=APPLIED")
