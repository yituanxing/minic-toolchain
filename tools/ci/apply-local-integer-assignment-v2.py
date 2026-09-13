#!/usr/bin/env python3
from pathlib import Path
import subprocess

# Start from the regression-safe V1: local facts are consulted only for
# compile-time control-flow reachability, never as a global expression-lowering
# shortcut.
subprocess.run(["python3", "tools/ci/apply-local-integer-conditions-v1.py"], check=True)

p = Path("src/core/core_lower.c")
text = p.read_text()

# Assignment facts must come from the source AST semantics, not from whether
# the already-lowered Core value happened to be a literal instruction.  This
# admits -22, casts/conversions and direct local reads without perturbing normal
# expression code generation.
old = '''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        core_local_constant_invalidate(context, target->value.local_id);
        if (core_value_integer_constant(context, stored_value, &stored_constant)) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        }
    }
'''
new = '''    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        bool fact_known;
        core_local_constant_invalidate(context, target->value.local_id);
        fact_known = core_const_eval_integer_with_locals(context, source_id, &stored_constant);
        if (fact_known) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        }
        if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "LOCAL_FACT_ASSIGN function=%s target=%zu source=%zu source_kind=%d known=%d bits=%" PRIu64 "\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (size_t)target->value.local_id,
                          (size_t)source_id,
                          source != NULL ? (int)source->kind : -1,
                          fact_known ? 1 : 0,
                          fact_known ? stored_constant.bits : UINT64_C(0));
        }
    }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one assignment fact block, found {count}")
text = text.replace(old, new, 1)

# V1 removed the broad lower_expression() fold.  After assignment facts switch
# to source-AST evaluation this old Core-literal helper has no users; remove it
# rather than weakening -Werror or keeping dead product code.
helper_start = text.find("static bool core_value_integer_constant(")
helper_end = text.find("static bool core_direct_local_read(", helper_start)
if helper_start < 0 or helper_end < 0 or helper_end <= helper_start:
    raise SystemExit("expected core_value_integer_constant helper before direct-local helper")
text = text[:helper_start] + text[helper_end:]

# GNU statement expressions commonly return a local whose value became known
# while lowering the selected compile-time branch.  Insert this only inside the
# local-aware evaluator, immediately after its logical-not case.  The longer
# anchor is deliberate: another binary-expression test exists elsewhere in the
# lowering file and must not be touched.
anchor = '''        value->type = expression->type;
        value->bits = is_zero ? 1U : 0U;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
'''
insert = '''        value->type = expression->type;
        value->bits = is_zero ? 1U : 0U;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
        expression->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {
        bool statement_known;
        statement_known = core_const_eval_integer_with_locals(
            context, expression->value.statement_expression.result, &operand_value);
        if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "LOCAL_FACT_STMT function=%s expression=%zu result=%zu known=%d bits=%" PRIu64 "\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (size_t)expression_id,
                          (size_t)expression->value.statement_expression.result,
                          statement_known ? 1 : 0,
                          statement_known ? operand_value.bits : UINT64_C(0));
        }
        if (!statement_known) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
'''
count = text.count(anchor)
if count != 1:
    raise SystemExit(f"expected one evaluator-local anchor, found {count}")
text = text.replace(anchor, insert, 1)

# Lightweight diagnostics for the focused probe only.  The environment gate
# keeps the real Linux lane quiet; this traces exactly where facts are killed.
clear_anchor = '''static void core_local_constants_clear_known(MinicCoreLowerContext *context) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
'''
clear_insert = '''static void core_local_constants_clear_known(MinicCoreLowerContext *context) {
    size_t index;

    if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL && context != NULL &&
        context->source_function != NULL) {
        (void)fprintf(stderr,
                      "LOCAL_FACT_CLEAR function=%s\\n",
                      context->source_function->name != NULL ? context->source_function->name : "?");
    }
    if (context == NULL || context->source_function == NULL ||
'''
count = text.count(clear_anchor)
if count != 1:
    raise SystemExit(f"expected one fact-clear helper anchor, found {count}")
text = text.replace(clear_anchor, clear_insert, 1)

p.write_text(text)
print("MINIC_LOCAL_INTEGER_ASSIGNMENT_V2=APPLIED")
