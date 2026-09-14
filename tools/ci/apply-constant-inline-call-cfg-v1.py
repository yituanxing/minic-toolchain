#!/usr/bin/env python3
from pathlib import Path

# V0's semantic transformation is correct, but its locator assumed a specific
# spelling for lower_condition_branch(). Earlier stack patches can reformat the
# function signature, while the validation block inside the function remains
# unique. Reuse V0 and make that locator signature-independent. Also allow a
# non-volatile dereference/lvalue read to count as a side-effect-free call
# argument for CFG-only constant folding.
path = Path("tools/ci/apply-constant-inline-call-cfg-v0.py")
source = path.read_text()
old = '''condition_fn = text.find("static MinicCoreLowerStatus lower_condition_branch(\\n")
if condition_fn < 0:
    raise SystemExit("lower_condition_branch definition missing")
'''
new = '''condition_fn = 0
'''
count = source.count(old)
if count != 1:
    raise SystemExit(f"expected one V0 condition locator, found {count}")
source = source.replace(old, new, 1)

pure_old = '''    case MINIC_EXPRESSION_ADDRESS_OF:\n    case MINIC_EXPRESSION_LVALUE_READ:\n        return core_cfg_pure_call_argument(\n            context, expression->value.unary.operand, depth + 1U);\n'''
pure_new = '''    case MINIC_EXPRESSION_ADDRESS_OF:\n    case MINIC_EXPRESSION_DEREFERENCE:\n    case MINIC_EXPRESSION_LVALUE_READ:\n        return core_cfg_pure_call_argument(\n            context, expression->value.unary.operand, depth + 1U);\n'''
count = source.count(pure_old)
if count != 1:
    raise SystemExit(f"expected one V0 pure unary/lvalue block, found {count}")
source = source.replace(pure_old, pure_new, 1)
exec(compile(source, str(path), "exec"))

multi = Path("tools/ci/apply-constant-inline-multireturn-v0.py")
exec(compile(multi.read_text(), str(multi), "exec"))

# Algebraic CFG facts where the result is known without knowing the other
# operand. Only consume the unknown side when it is side-effect free.
p = Path("src/core/core_lower.c")
text = p.read_text()
fn_anchor = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
start = text.find(fn_anchor)
if start < 0:
    raise SystemExit("local integer evaluator missing for annihilator closure")
anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||\n'''
pos = text.find(anchor, start)
if pos < 0:
    raise SystemExit("comparison anchor missing for annihilator closure")
annihilator = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY)) {
        MinicConstValue known;
        bool is_zero;

        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &known) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &known,
                                      &is_zero) &&
            is_zero &&
            core_cfg_pure_call_argument(
                context, expression->value.binary.right, 0U)) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &known) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &known,
                                      &is_zero) &&
            is_zero &&
            core_cfg_pure_call_argument(
                context, expression->value.binary.left, 0U)) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
    }
'''
text = text[:pos] + annihilator + text[pos:]

# Keep this fast path in lower_condition_branch itself as well. x & 0 is a
# context-free C identity and should not depend on local data-flow state.
condition_fn = text.find("lower_condition_branch(")
if condition_fn < 0:
    raise SystemExit("lower_condition_branch missing for direct zero-and fold")
condition_constant_anchor = '''    if (minic_type_is_integer(expression->type)) {
        MinicConstValue condition_constant;
        bool condition_is_zero;
'''
condition_pos = text.find(condition_constant_anchor, condition_fn)
if condition_pos < 0:
    raise SystemExit("condition constant fold anchor missing for direct zero-and fold")
direct_zero_and = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
        MinicConstValue operand_constant;
        bool operand_is_zero;
        bool fold_false = false;

        if (minic_const_eval_integer(context->body->program,
                                     context->target,
                                     expression->value.binary.left,
                                     &operand_constant) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &operand_constant,
                                      &operand_is_zero) &&
            operand_is_zero &&
            core_cfg_pure_call_argument(
                context, expression->value.binary.right, 0U)) {
            fold_false = true;
        } else if (minic_const_eval_integer(context->body->program,
                                            context->target,
                                            expression->value.binary.right,
                                            &operand_constant) &&
                   minic_const_value_is_zero(context->body->program,
                                             context->target,
                                             &operand_constant,
                                             &operand_is_zero) &&
                   operand_is_zero &&
                   core_cfg_pure_call_argument(
                       context, expression->value.binary.left, 0U)) {
            fold_false = true;
        }
        if (fold_false) {
            return set_branch(context, context->block_id, span, when_false);
        }
    }
'''
text = text[:condition_pos] + direct_zero_and + text[condition_pos:]

# M182_TERMINATING_GUARD_FACTS: for a pure no-else guard whose taken arm is a
# goto/break, that arm cannot merge into the following statement. Preserve the
# incoming straight-line local facts on the only surviving false path instead
# of clearing them at the generic CFG join.
if_start = text.find(
    "static MinicCoreLowerStatus\nlower_if(MinicCoreLowerContext *context, "
    "const MinicStatement *statement, bool *terminated) {")
if if_start < 0:
    raise SystemExit("lower_if definition missing for terminating-guard facts")
if_end = text.find("\nstatic bool internal_while_label_pair", if_start)
if if_end < 0:
    raise SystemExit("lower_if end missing for terminating-guard facts")
if_body = text[if_start:if_end]

decl_old = '''    bool needs_merge;
    bool then_terminated;
'''
decl_new = '''    bool needs_merge;
    bool then_terminated;
    bool preserve_guard_facts;
'''
if if_body.count(decl_old) != 1:
    raise SystemExit(f"terminating-guard declaration anchor count={if_body.count(decl_old)}")
if_body = if_body.replace(decl_old, decl_new, 1)

setup_anchor = '''    /* BusyBox and ordinary GNU C intentionally leave impossible references in
'''
setup = '''    /* M182_TERMINATING_GUARD_FACTS: a pure terminating taken arm does not
       contribute state to the following statement. */
    preserve_guard_facts = false;
    if (else_source == NULL && then_source->statement_count == 1U &&
        core_cfg_pure_call_argument(context, statement->expression, 0U)) {
        const MinicStatement *guard_statement = minic_c0_program_statement(
            context->body->program, then_source->statements[0]);
        preserve_guard_facts =
            guard_statement != NULL &&
            (guard_statement->kind == MINIC_STATEMENT_GOTO ||
             guard_statement->kind == MINIC_STATEMENT_BREAK);
    }

    /* BusyBox and ordinary GNU C intentionally leave impossible references in
'''
if if_body.count(setup_anchor) != 1:
    raise SystemExit(f"terminating-guard setup anchor count={if_body.count(setup_anchor)}")
if_body = if_body.replace(setup_anchor, setup, 1)

pre_clear = '''    core_local_constants_clear_known(context);
    condition_block = context->block_id;
'''
pre_keep = '''    if (!preserve_guard_facts) {
        core_local_constants_clear_known(context);
    }
    condition_block = context->block_id;
'''
if if_body.count(pre_clear) != 1:
    raise SystemExit(f"terminating-guard pre-clear anchor count={if_body.count(pre_clear)}")
if_body = if_body.replace(pre_clear, pre_keep, 1)

post_clear = '''    context->block_id = continuation_block;
    core_local_constants_clear_known(context);
    *terminated = !needs_merge;
'''
post_keep = '''    context->block_id = continuation_block;
    if (!preserve_guard_facts) {
        core_local_constants_clear_known(context);
    }
    *terminated = !needs_merge;
'''
if if_body.count(post_clear) != 1:
    raise SystemExit(f"terminating-guard post-clear anchor count={if_body.count(post_clear)}")
if_body = if_body.replace(post_clear, post_keep, 1)

text = text[:if_start] + if_body + text[if_end:]
p.write_text(text)
print("MINIC_CONSTANT_CFG_ANNIHILATOR_V1=APPLIED")
print("M182_TERMINATING_GUARD_FACTS=APPLIED")

# Temporary diagnostic on the focused diagnose branch only.
probe = Path("tools/ci/apply-constant-inline-call-cfg-probe-v0.py")
exec(compile(probe.read_text(), str(probe), "exec"))
