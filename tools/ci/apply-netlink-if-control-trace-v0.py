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

# Instrument the exact lower_if() seam rather than guessing from a successful
# end-to-end compile. For each dynamic GENL IF, record which exits exist and
# whether Core ever emits the guard after constructing the then/else bodies.
# Before the expensive Linux replay this script asserts each anchor exactly once.
checks = [
  ("pre_then",
   '''    context->block_id = then_block;
    status = lower_block(context, then_source, &then_terminated);
''',
   '''    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=pre_then expr=%zu current=%u then=%u condition=%u\n",
            (size_t)statement->expression, (unsigned int)context->block_id,
            (unsigned int)then_block, (unsigned int)condition_block);
'''),
  ("post_then",
   '''    then_continuation_block = context->block_id;
''',
   '''    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=post_then expr=%zu status=%d terminated=%d continuation=%u\n",
            (size_t)statement->expression, (int)status,
            then_terminated ? 1 : 0, (unsigned int)context->block_id);
'''),
  ("before_guard",
   '''    continuation_block = context->block_id;
    context->block_id = condition_block;
    status = lower_condition_branch(
        context, statement->expression, statement->span, then_block, false_target);
''',
   '''    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=pre_guard expr=%zu condition=%u then=%u else=%u merge=%u need_merge=%d\n",
            (size_t)statement->expression, (unsigned int)condition_block,
            (unsigned int)then_block, (unsigned int)false_target,
            (unsigned int)merge_block, needs_merge ? 1 : 0);
'''),
  ("after_guard",
   '''    context->block_id = continuation_block;
    *terminated = !needs_merge;
    return MINIC_CORE_LOWER_OK;
''',
   '''    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=post_guard expr=%zu status=%d block=%u\n",
            (size_t)statement->expression, (int)status,
            (unsigned int)context->block_id);
''')
]
for label, anchor, insertion in checks:
    n = s.count(anchor)
    if n != 1:
        raise SystemExit(f"genl seam anchor {label} count={n}")
    s = s.replace(anchor, insertion+anchor, 1)
p.write_text(s)
print("MINIC_NETLINK_IF_SEAM_TRACE=APPLIED guards=4")
