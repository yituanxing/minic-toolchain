#!/usr/bin/env python3
"""Prove that a source-level _Bool is zero at every defined read.

Run only in the correctness Runtime profile after existing local-fact patches.
The proof is intentionally stricter than generic constant propagation:
  * zero initialization must occur in the leading straight-line source block;
  * every syntactic write anywhere in its owning C0 program must be zero;
  * neither address-taking, compound updates, asm outputs nor volatile access
    are allowed.
Only after this closed-world proof may a local zero fact survive CFG joins,
including calls with nonescaping arguments, gotos, and forward label merges.
This is not a symbol-name or Linux-specific special case.
"""
from pathlib import Path

header = Path("src/core/core_lower_internal.h")
text = header.read_text()
anchor = "typedef struct MinicCoreLocalIntegerConstant {"
if text.count(anchor) != 1:
    raise SystemExit("STABLE_ZERO=ERROR fact_struct_missing")
a = text.index(anchor)
b = text.index("} MinicCoreLocalIntegerConstant;", a)
struct = text[a:b]
needle = "    bool escaped;\n"
if struct.count(needle) != 1:
    raise SystemExit("STABLE_ZERO=ERROR escaped_field_ambiguous")
struct = struct.replace(needle, needle +
    "    bool program_zero_checked;\n"
    "    bool program_zero_stable;\n", 1)
text = text[:a] + struct + text[b:]
header.write_text(text)

path = Path("src/core/core_lower.c")
text = path.read_text()
if "RUNTIME_STABLE_ZERO_BOOLS_V0" in text:
    raise SystemExit("STABLE_ZERO=ERROR duplicate_patch")

helper = r'''
/* RUNTIME_STABLE_ZERO_BOOLS_V0
   This proof precedes, and never follows, CFG-based propagation. A function
   initializer alone is insufficient: EVERY write must have the same value.
   A label/goto can then join arbitrary predecessors without changing zero. */
static bool core_program_expr_is_literal_zero(
    const MinicCoreLowerContext *context, MinicExpressionId id) {
    MinicConstValue value;
    int64_t number;
    return context != NULL && context->body != NULL &&
           context->body->program != NULL && context->target != NULL &&
           minic_const_eval_integer(context->body->program,
                                    context->target, id, &value) &&
           minic_const_value_as_int64(context->body->program,
                                      context->target, &value, &number) &&
           number == 0;
}

static bool core_program_expr_is_local(
    const MinicC0Program *program, MinicExpressionId expression_id,
    MinicLocalId local_id) {
    const MinicExpression *expression =
        minic_c0_program_expression(program, expression_id);
    return expression != NULL &&
           expression->kind == MINIC_EXPRESSION_LOCAL &&
           expression->value.local_id == local_id;
}

static bool core_program_bool_has_entry_zero_initializer(
    const MinicCoreLowerContext *context, MinicLocalId local_id) {
    const MinicC0Program *program = context->body->program;
    const MinicBlock *entry = minic_c0_program_block(
        program, context->source_function->body_block);
    size_t i;
    if (entry == NULL) {
        return false;
    }
    /* A leading sequence of direct assignments is the only entry region
       accepted. C source declarations initialized later or conditionally
       are NOT credited with dominating every defined use. */
    for (i = 0U; i < entry->statement_count && i < 64U; ++i) {
        const MinicStatement *stmt =
            minic_c0_program_statement(program, entry->statements[i]);
        const MinicExpression *target;
        if (stmt == NULL || stmt->kind != MINIC_STATEMENT_ASSIGN) {
            break;
        }
        target = minic_c0_program_expression(program, stmt->target_expression);
        if (target == NULL || target->kind != MINIC_EXPRESSION_LOCAL) {
            break;
        }
        if (target->value.local_id == local_id) {
            return core_program_expr_is_literal_zero(context, stmt->expression);
        }
    }
    return false;
}

static bool core_program_bool_is_provably_zero(
    const MinicCoreLowerContext *context, MinicLocalId local_id) {
    const MinicC0Program *program;
    const MinicLocal *local;
    size_t i;

    if (context == NULL || context->source_function == NULL ||
        context->body == NULL || context->body->program == NULL ||
        context->target == NULL) {
        return false;
    }
    program = context->body->program;
    local = minic_c0_program_local(program, local_id);
    if (local == NULL || local->is_array || local->is_register_storage ||
        !minic_type_is_bool_integer(local->type) ||
        minic_type_is_volatile(local->type) ||
        !core_program_bool_has_entry_zero_initializer(context, local_id)) {
        return false;
    }
    for (i = 0U; i < program->statement_count; ++i) {
        const MinicStatement *stmt = &program->statements[i];
        if (!core_program_expr_is_local(
                program, stmt->target_expression, local_id)) {
            continue;
        }
        if (stmt->kind != MINIC_STATEMENT_ASSIGN ||
            !core_program_expr_is_literal_zero(context, stmt->expression)) {
            return false;
        }
    }
    for (i = 0U; i < program->expression_count; ++i) {
        const MinicExpression *expr = &program->expressions[i];
        const MinicExpression *lhs;
        switch (expr->kind) {
            case MINIC_EXPRESSION_ADDRESS_OF:
                if (core_program_expr_is_local(
                        program, expr->value.unary.operand, local_id)) {
                    return false;
                }
                break;
            case MINIC_EXPRESSION_UNARY:
                if ((expr->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
                     expr->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
                     expr->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT ||
                     expr->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT) &&
                    core_program_expr_is_local(
                        program, expr->value.unary.operand, local_id)) {
                    return false;
                }
                break;
            case MINIC_EXPRESSION_ASSIGNMENT:
            case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:
                lhs = minic_c0_program_expression(
                    program, expr->value.binary.left);
                if (lhs == NULL || lhs->kind != MINIC_EXPRESSION_LOCAL ||
                    lhs->value.local_id != local_id) {
                    break;
                }
                if (expr->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT ||
                    !core_program_expr_is_literal_zero(
                        context, expr->value.binary.right)) {
                    return false;
                }
                break;
            default:
                break;
        }
    }
    for (i = 0U; i < program->inline_asm_count; ++i) {
        const MinicInlineAsm *asm_stmt = &program->inline_asms[i];
        size_t j;
        for (j = 0U; j < asm_stmt->output_count; ++j) {
            if (core_program_expr_is_local(
                    program, asm_stmt->outputs[j].expression, local_id)) {
                return false;
            }
        }
    }
    return true;
}
'''
anchor = "static void core_local_constant_set(MinicCoreLowerContext *context,"
if text.count(anchor) != 1:
    raise SystemExit(f"STABLE_ZERO=ERROR set_anchor_count={text.count(anchor)}")
text = text.replace(anchor, helper + "\n" + anchor, 1)

# Preserve only proven immutable zero; the existing normal local fact
# invalidation and escaping remain in full force for every other local.
start = text.index("static void core_local_constants_clear_known(")
stop = text.index("\nstatic ", start + 10)
part = text[start:stop]
needle = "        context->local_integer_constants[index].known = false;\n"
if part.count(needle) != 1:
    raise SystemExit("STABLE_ZERO=ERROR clear_join_ambiguous")
part = part.replace(needle,
    "        if (!context->local_integer_constants[index].program_zero_stable ||\n"
    "            context->local_integer_constants[index].escaped) {\n"
    "            context->local_integer_constants[index].known = false;\n"
    "        }\n", 1)
text = text[:start] + part + text[stop:]

start = text.index("static void core_local_constant_set(")
stop = text.index("\nstatic ", start + 10)
part = text[start:stop]
needle = "    context->local_integer_constants[index].known = true;\n"
if part.count(needle) != 1:
    raise SystemExit(f"STABLE_ZERO=ERROR set_known_count={part.count(needle)}")
part = part.replace(needle,
    needle +
    "    if (!context->local_integer_constants[index].program_zero_checked) {\n"
    "        context->local_integer_constants[index].program_zero_checked = true;\n"
    "        if (converted.bits == 0U &&\n"
    "            core_program_bool_is_provably_zero(context, local_id)) {\n"
    "            context->local_integer_constants[index].program_zero_stable = true;\n"
    "        }\n"
    "    }\n", 1)
text = text[:start] + part + text[stop:]
path.write_text(text)
print("MINIC_RUNTIME_STABLE_ZERO_BOOLS_V0=APPLIED proof=entry_and_all_writes escaped=deny")
