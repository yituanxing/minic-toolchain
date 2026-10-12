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

/* Core lower_block owns the canonical external goto/asm-goto reentry
   oracle.  It is defined later in this translation unit. */
static bool core_unreachable_statement_has_external_reentry(
    const MinicCoreLowerContext *context,
    const MinicStatement *root_statement,
    MinicStatementId extra_statement_id);

/* The frontend still retains syntactic stores following a guaranteed
   return.  Core lower_block skips exactly these unreachable tails.  Discount
   such stores from a whole-function invariant proof, but only if there is
   no label/case/default reentry between the terminator and that store. */
static bool core_program_statement_must_return(
    const MinicCoreLowerContext *context,
    const MinicStatement *statement) {
    const MinicC0Program *program;
    const MinicBlock *then_block;
    const MinicStatement *first;
    MinicConstValue constant;
    bool zero;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || statement == NULL) {
        return false;
    }
    if (statement->kind == MINIC_STATEMENT_RETURN) {
        return true;
    }
    if (statement->kind != MINIC_STATEMENT_IF ||
        statement->then_block == MINIC_BLOCK_INVALID) {
        return false;
    }
    program = context->body->program;
    if (!minic_const_eval_integer(program, context->target,
                                  statement->expression, &constant) ||
        !minic_const_value_is_zero(program, context->target,
                                   &constant, &zero) || zero) {
        return false;
    }
    then_block = minic_c0_program_block(program, statement->then_block);
    if (then_block == NULL || then_block->statement_count == 0U) {
        return false;
    }
    first = minic_c0_program_statement(program, then_block->statements[0]);
    return first != NULL && first->kind == MINIC_STATEMENT_RETURN;
}

static bool core_program_stmt_is_dead_after_return(
    const MinicCoreLowerContext *context, MinicStatementId statement_id) {
    const MinicC0Program *program;
    size_t block_index;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL) {
        return false;
    }
    for (block_index = 0U; block_index < program->block_count; ++block_index) {
        const MinicBlock *block = &program->blocks[block_index];
        size_t i;
        bool must_return = false;
        for (i = 0U; i < block->statement_count; ++i) {
            const MinicStatement *statement =
                minic_c0_program_statement(program, block->statements[i]);
            if (statement == NULL) {
                return false;
            }
            if (block->statements[i] == statement_id) {
                return must_return &&
                    !core_unreachable_statement_has_external_reentry(
                        context, statement, MINIC_STATEMENT_INVALID);
            }
            if (statement->kind == MINIC_STATEMENT_LABEL ||
                statement->kind == MINIC_STATEMENT_CASE ||
                statement->kind == MINIC_STATEMENT_DEFAULT) {
                must_return = false;
            } else if (core_program_statement_must_return(context, statement)) {
                must_return = true;
            }
        }
    }
    return false;
}

/* Parser may encode an ordinary "x = 1;" as a statement-expression
   containing MINIC_EXPRESSION_ASSIGNMENT rather than MINIC_STATEMENT_ASSIGN.
   The same proven unreachable-tail rule must apply to that exact statement.
   Do not generalize to assignments nested in potentially live expressions. */
static bool core_program_expr_is_dead_after_return(
    const MinicCoreLowerContext *context, MinicExpressionId expression_id) {
    const MinicC0Program *program;
    size_t i;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL) {
        return false;
    }
    for (i = 0U; i < program->statement_count; ++i) {
        const MinicStatement *stmt = &program->statements[i];
        if (stmt->kind != MINIC_STATEMENT_EXPRESSION ||
            stmt->expression != expression_id) {
            continue;
        }
        return core_program_stmt_is_dead_after_return(
            context, (MinicStatementId)i);
    }
    return false;
}

static bool core_program_bool_is_provably_zero(
    const MinicCoreLowerContext *context, MinicLocalId local_id) {
    const MinicC0Program *program;
    const MinicLocal *local;
    size_t i;
    bool trace;

    if (context == NULL || context->source_function == NULL ||
        context->body == NULL || context->body->program == NULL ||
        context->target == NULL) {
        return false;
    }
    program = context->body->program;
    trace = getenv("MINIC_DMA_ZERO_TRACE") != NULL &&
            context->source_function->name != NULL &&
            strcmp(context->source_function->name, "dma_direct_alloc") == 0;
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
            (!core_program_expr_is_literal_zero(context, stmt->expression) &&
             !core_program_stmt_is_dead_after_return(
                 context, (MinicStatementId)i))) {
            if (trace) (void)fprintf(stderr,
                "DMA_ZERO_REJECT local=%zu reason=stmt index=%zu line=%zu kind=%d zero=%d dead=%d\\n",
                (size_t)local_id, i, stmt->span.begin.line, (int)stmt->kind,
                core_program_expr_is_literal_zero(context, stmt->expression) ? 1 : 0,
                core_program_stmt_is_dead_after_return(context, (MinicStatementId)i) ? 1 : 0);
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
                    if (trace) (void)fprintf(stderr, "DMA_ZERO_REJECT local=%zu reason=address expr=%zu line=%zu\\n", (size_t)local_id, i, expr->span.begin.line);
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
                    if (trace) (void)fprintf(stderr, "DMA_ZERO_REJECT local=%zu reason=update expr=%zu line=%zu\\n", (size_t)local_id, i, expr->span.begin.line);
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
                    (!core_program_expr_is_literal_zero(
                         context, expr->value.binary.right) &&
                     !core_program_expr_is_dead_after_return(
                         context, (MinicExpressionId)i))) {
                    if (trace) (void)fprintf(stderr, "DMA_ZERO_REJECT local=%zu reason=expression expr=%zu line=%zu kind=%d\\n", (size_t)local_id, i, expr->span.begin.line, (int)expr->kind);
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
                if (trace) (void)fprintf(stderr, "DMA_ZERO_REJECT local=%zu reason=asm idx=%zu\\n", (size_t)local_id, i);
                return false;
            }
        }
    }
    return true;
}
'''
anchor = "static void core_local_constant_set(MinicCoreLowerContext *context,"
if text.count(anchor) < 1:
    raise SystemExit("STABLE_ZERO=ERROR no_set_anchor")
# Earlier Runtime patches may inject a forward declaration of the setter.
# Target the LAST definition, never that declaration.
position = text.rfind(anchor)
text = text[:position] + helper + "\n" + text[position:]

# Preserve only proven immutable zero; the existing normal local fact
# invalidation and escaping remain in full force for every other local.
start = text.index("static void core_local_constants_clear_known(")
stop = text.index("\nstatic ", start + 10)
part = text[start:stop]
needle = (
    "        } else {\n"
    "            fact->known = false;\n"
    "        }\n"
)
if part.count(needle) != 1:
    raise SystemExit(f"STABLE_ZERO=ERROR clear_join_count={part.count(needle)}")
part = part.replace(needle,
    "        } else if (fact->program_zero_stable && !fact->escaped) {\n"
    "            fact->known = true;\n"
    "        } else {\n"
    "            fact->known = false;\n"
    "        }\n", 1)
text = text[:start] + part + text[stop:]

start = text.rfind("static void core_local_constant_set(")
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
    "        if (getenv(\"MINIC_DMA_ZERO_TRACE\") != NULL &&\n"
    "            context->source_function != NULL &&\n"
    "            context->source_function->name != NULL &&\n"
    "            strcmp(context->source_function->name, \"dma_direct_alloc\") == 0) {\n"
    "            (void)fprintf(stderr,\n"
    "                          \"DMA_ZERO_PROOF local=%zu line=%zu bits=%llu entry=%d stable=%d\\n\",\n"
    "                          (size_t)local_id,\n"
    "                          (size_t)local->name_span.begin.line,\n"
    "                          (unsigned long long)converted.bits,\n"
    "                          core_program_bool_has_entry_zero_initializer(context, local_id) ? 1 : 0,\n"
    "                          context->local_integer_constants[index].program_zero_stable ? 1 : 0);\n"
    "        }\n"
    "    }\n", 1)
text = text[:start] + part + text[stop:]

# Strictly diagnostic: trace whether a local bool is set before an unrelated
# CFG boundary clears it in shmem_alloc_and_acct_folio. Never change IR here.
clear_head = "static void core_local_constants_clear_known("
clear_start = text.index(clear_head)
clear_end = text.index("\nstatic ", clear_start + 10)
piece = text[clear_start:clear_end]
probe = "        if (fact->specialization_known && !fact->specialization_modified &&"
if piece.count(probe) != 1:
    raise SystemExit(f"SHMEM_FACT_TRACE=ERROR clear_anchor={piece.count(probe)}")
piece = piece.replace(probe, r'''        if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
            context->source_function->name != NULL &&
            strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0 &&
            fact->known) {
            (void)fprintf(stderr, "SHMEM_FACT_EVENT event=clear local=%zu bits=%llu escaped=%d\n",
                (size_t)(context->source_function->local_begin + index),
                (unsigned long long)fact->value.bits, fact->escaped ? 1 : 0);
        }
''' + probe, 1)
text = text[:clear_start] + piece + text[clear_end:]
set_head = "static void core_local_constant_set("
set_start = text.rfind(set_head)
set_end = text.index("\nstatic ", set_start + 10)
piece = text[set_start:set_end]
probe = "    context->local_integer_constants[index].known = true;\n"
if piece.count(probe) != 1:
    raise SystemExit(f"SHMEM_FACT_TRACE=ERROR set_anchor={piece.count(probe)}")
piece = piece.replace(probe, probe + r'''    if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0) {
        (void)fprintf(stderr, "SHMEM_FACT_EVENT event=set local=%zu bits=%llu escaped=%d\n",
            (size_t)local_id,
            (unsigned long long)converted.bits,
            context->local_integer_constants[index].escaped ? 1 : 0);
    }
''', 1)
text = text[:set_start] + piece + text[set_end:]
path.write_text(text)
print("MINIC_RUNTIME_STABLE_ZERO_BOOLS_V0=APPLIED proof=entry_and_all_writes escaped=deny")
