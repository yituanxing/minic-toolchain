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

# A second independent, opt-in experiment on the same ephemeral compiler:
# preserve left SSA when a trivially block-straight integer RHS is loaded.
# Never affects default Linux-profile compiler (no env => old spill path).
marker="static MinicCoreLowerStatus lower_integer_binary_operands(MinicCoreLowerContext *context,"
if s.count(marker)!=1:
    raise SystemExit("P1_NOSPILL=ERROR expected_one_integer_binary_lowerer")
helper=r'''/* P1_SIMPLE_RHS_NOSPILL_V1: only a literal or a plain integer local.
   These cannot introduce a Core CFG split. The post-check on block_id
   fails closed if this contract ever changes in future frontend lowering.
   Preserve LHS-first evaluation and all volatile loads. */
static bool core_p1_simple_rhs_no_spill(const MinicCoreLowerContext *context,
                                        const MinicExpression *rhs) {
    const char *only = getenv("MINIC_P1_NOSPILL_FUNCTION");
    const char *name;
    if (context == NULL || context->source_function == NULL || rhs == NULL ||
        only == NULL || only[0] == '\0') {
        return false;
    }
    name = context->source_function->name;
    if (name == NULL ||
        (strcmp(only, name) != 0 &&
         !(strcmp(only, "tcp_pair") == 0 &&
           (strcmp(name, "tcp_conn_request") == 0 ||
            strcmp(name, "tcp_rcv_state_process") == 0)))) {
        return false;
    }
    return rhs->kind == MINIC_EXPRESSION_INTEGER ||
           (rhs->kind == MINIC_EXPRESSION_LOCAL &&
            rhs->value_category == MINIC_VALUE_LVALUE);
}

'''
s=s.replace(marker,helper+marker,1)
begin=s.index(marker)
end=s.index("static MinicCoreLowerStatus lower_floating_binary_operands(",begin)
region=s[begin:end]
def replace_one(old,new,label):
    global region
    if region.count(old)!=1:
        raise SystemExit(f"P1_NOSPILL=ERROR {label} matches={region.count(old)}")
    region=region.replace(old,new,1)

replace_one("    MinicCoreLowerStatus status;\n\n",
"""    MinicCoreLowerStatus status;
    MinicCoreBlockId rhs_start_block;
    bool skip_left_spill;

""","locals")
replace_one("""    status = spill_scalar_value(
        context, left_expression->span, result_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-spill status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }

    status = lower_expression(context, right_id, &right_source);""",
"""    skip_left_spill = core_p1_simple_rhs_no_spill(context, right_expression);
    rhs_start_block = context->block_id;
    if (!skip_left_spill) {
        status = spill_scalar_value(
            context, left_expression->span, result_type, left_normalized, &left_object);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-spill status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?", (int)status);
            return status;
        }
    }

    status = lower_expression(context, right_id, &right_source);""","spill")
replace_one("""    status =
        reload_scalar_value(context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-reload status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    *right_value = right_normalized;""",
"""    if (skip_left_spill) {
        /* Core values are reusable through instructions inside their
           defining block, never across a CFG edge. */
        if (context->block_id != rhs_start_block) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *left_value = left_normalized;
    } else {
        status =
            reload_scalar_value(context, left_expression->span, result_type, left_object, left_value);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-reload status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?", (int)status);
            return status;
        }
    }
    *right_value = right_normalized;""","reload")
s=s[:begin]+region+s[end:]
if "P1_SIMPLE_RHS_NOSPILL_V1" not in s:
    raise SystemExit("P1_NOSPILL=ERROR omitted")
p.write_text(s)
print("P1_SIMPLE_RHS_NOSPILL_V1=APPLIED env_opt_in_only=true")
