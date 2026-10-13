#!/usr/bin/env python3
"""Opt-in Core lowering: omit provably pure discarded local-address comparisons.

Linux type-check macros have (void)(&__dummy == &__dummy2); generating
CoreObjects solely for this semantically dead comparison wastes kernel stack.
Only two direct unadorned auto-local addresses may be skipped.
This remains experimental, never the default compiler.
"""
from pathlib import Path

p=Path("src/core/core_lower.c")
s=p.read_text()
if "P1_PURE_DISCARDED_LOCAL_ADDRESS_EQUALITY_V1" in s:
    raise SystemExit("P1_DISCARD_EQ=ERROR already_patched")
marker="static MinicCoreLowerStatus lower_expression_statement(MinicCoreLowerContext *context,"
if s.count(marker)!=1:
    raise SystemExit("P1_DISCARD_EQ=ERROR missing_statement_owner")

helper=r'''/* P1_PURE_DISCARDED_LOCAL_ADDRESS_EQUALITY_V1:
   Comparing two plain local addresses has no side effects when the result
   is explicitly discarded. Semantic type validation already ran. Never
   extend this to subscript, dereference, side-effectful calls or reads. */
static bool core_p1_address_of_plain_local(
    const MinicCoreLowerContext *context, MinicExpressionId candidate_id) {
    const MinicExpression *addr;
    const MinicExpression *local;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || candidate_id == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    addr = minic_c0_program_expression(context->body->program, candidate_id);
    if (addr == NULL || addr->kind != MINIC_EXPRESSION_ADDRESS_OF ||
        addr->value.unary.operand == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    local = minic_c0_program_expression(
        context->body->program, addr->value.unary.operand);
    return local != NULL && local->kind == MINIC_EXPRESSION_LOCAL &&
           local->value_category == MINIC_VALUE_LVALUE;
}

static bool core_p1_discarded_pure_local_address_equality(
    const MinicCoreLowerContext *context, const MinicExpression *expr) {
    const MinicExpression *cmp;
    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || expr == NULL ||
        expr->kind != MINIC_EXPRESSION_DISCARD ||
        expr->value.unary.operand == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    /* A single-function opt-in isolates this from all other kernel owners. */
    {
        const char *only = getenv("MINIC_P1_DISCARD_EQ_FUNCTION");
        if (only != NULL && only[0] != 0) {
            const char *name =
                context->source_function != NULL
                    ? context->source_function->name : NULL;
            const bool exact = name != NULL && strcmp(name, only) == 0;
            const bool ipv6_pair = name != NULL &&
                strcmp(only, "ip6_pair") == 0 &&
                (strcmp(name, "ip6_finish_output2") == 0 ||
                 strcmp(name, "ip6_xmit") == 0);
            if (!exact && !ipv6_pair) {
                return false;
            }
        }
    }
    cmp = minic_c0_program_expression(
        context->body->program, expr->value.unary.operand);
    return cmp != NULL && cmp->kind == MINIC_EXPRESSION_BINARY &&
           (cmp->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
            cmp->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) &&
           core_p1_address_of_plain_local(context, cmp->value.binary.left) &&
           core_p1_address_of_plain_local(context, cmp->value.binary.right);
}

'''
s=s.replace(marker,helper+marker,1)
anchor='''    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        status = lower_expression(context, statement->expression, &discarded_value);
        return core_trace_expression_statement_status(context, expression, "discard", status);
    }'''
if s.count(anchor)!=1:
    raise SystemExit("P1_DISCARD_EQ=ERROR unsupported_discard_anchor")
replacement='''    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        if (core_p1_discarded_pure_local_address_equality(context, expression)) {
            /* Entire discarded comparison is effect-free; no objects needed. */
            return MINIC_CORE_LOWER_OK;
        }
        status = lower_expression(context, statement->expression, &discarded_value);
        return core_trace_expression_statement_status(context, expression, "discard", status);
    }'''
s=s.replace(anchor,replacement,1)
p.write_text(s)
print("P1_PURE_DISCARDED_LOCAL_ADDRESS_EQUALITY_V1=APPLIED scope=discarded_local_addr_eq_only")
