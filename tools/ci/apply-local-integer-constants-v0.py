#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


# One conservative per-local state lives only during Core lowering.  It is not
# semantic AST state and is deliberately discarded at uncertain CFG joins.
replace_once(
    "src/core/core_lower_internal.h",
    '#include "core/core_lower.h"\n\ntypedef struct MinicCoreLowerContext {\n',
    '#include "core/core_lower.h"\n#include "frontend/const_eval.h"\n\n'
    'typedef struct MinicCoreLocalIntegerConstant {\n'
    '    MinicConstValue value;\n'
    '    bool known;\n'
    '    bool escaped;\n'
    '} MinicCoreLocalIntegerConstant;\n\n'
    'typedef struct MinicCoreLowerContext {\n',
)
replace_once(
    "src/core/core_lower_internal.h",
    '    MinicCoreObjectId *local_objects;\n    MinicCoreBlockId *statement_blocks;\n',
    '    MinicCoreObjectId *local_objects;\n'
    '    MinicCoreLocalIntegerConstant *local_integer_constants;\n'
    '    MinicCoreBlockId *statement_blocks;\n',
)

helpers = r'''

/* LINUX_LOCAL_INTEGER_CONSTANTS_V0
 *
 * This is intentionally a tiny straight-line fact table, not an optimizer.
 * A fact is admitted only for an unescaped non-volatile direct local integer.
 * Unknown CFG joins discard all facts.  The purpose is to preserve C/GNU
 * compile-time reachability idioms after their constant value has travelled
 * through a local object (notably Linux BUILD_BUG_ON/compiletime_assert). */
static bool core_local_constant_index(const MinicCoreLowerContext *context,
                                      MinicLocalId local_id,
                                      size_t *index) {
    if (context == NULL || context->source_function == NULL || index == NULL ||
        local_id < context->source_function->local_begin) {
        return false;
    }
    *index = local_id - context->source_function->local_begin;
    return *index < context->source_function->local_count &&
           context->local_integer_constants != NULL;
}

static void core_local_constants_clear_known(MinicCoreLowerContext *context) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL) {
        return;
    }
    for (index = 0U; index < context->source_function->local_count; ++index) {
        context->local_integer_constants[index].known = false;
    }
}

static void core_local_constant_invalidate(MinicCoreLowerContext *context,
                                           MinicLocalId local_id) {
    size_t index;
    if (core_local_constant_index(context, local_id, &index)) {
        context->local_integer_constants[index].known = false;
    }
}

static void core_local_constant_escape(MinicCoreLowerContext *context,
                                       MinicLocalId local_id) {
    size_t index;
    if (core_local_constant_index(context, local_id, &index)) {
        context->local_integer_constants[index].known = false;
        context->local_integer_constants[index].escaped = true;
    }
}

static bool core_local_constant_get(const MinicCoreLowerContext *context,
                                    MinicLocalId local_id,
                                    MinicConstValue *value) {
    const MinicLocal *local;
    size_t index;

    if (value == NULL || !core_local_constant_index(context, local_id, &index) ||
        !context->local_integer_constants[index].known ||
        context->local_integer_constants[index].escaped || context->body == NULL ||
        context->body->program == NULL) {
        return false;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    if (local == NULL || minic_type_is_volatile(local->type)) {
        return false;
    }
    *value = context->local_integer_constants[index].value;
    return true;
}

static void core_local_constant_set(MinicCoreLowerContext *context,
                                    MinicLocalId local_id,
                                    const MinicConstValue *value) {
    const MinicLocal *local;
    MinicConstValue converted;
    MinicType local_type;
    size_t index;

    if (value == NULL || context == NULL || context->body == NULL ||
        context->body->program == NULL || context->target == NULL ||
        !core_local_constant_index(context, local_id, &index) ||
        context->local_integer_constants[index].escaped) {
        return;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    if (local == NULL || local->is_array || minic_type_is_volatile(local->type) ||
        !minic_type_unqualified(local->type, &local_type) ||
        !minic_type_is_integer(local_type) ||
        !minic_const_value_convert_integer(context->body->program,
                                           context->target,
                                           value,
                                           local_type,
                                           &converted)) {
        context->local_integer_constants[index].known = false;
        return;
    }
    context->local_integer_constants[index].value = converted;
    context->local_integer_constants[index].known = true;
}

static bool core_value_integer_constant(const MinicCoreLowerContext *context,
                                        MinicCoreValueId value_id,
                                        MinicConstValue *value) {
    const MinicCoreValue *core_value;
    const MinicCoreInstruction *definition;
    uint64_t bits;

    if (context == NULL || context->function == NULL || value == NULL ||
        value_id >= context->function->value_count) {
        return false;
    }
    core_value = &context->function->values[value_id];
    if (!minic_type_is_integer(core_value->type) ||
        core_value->definition >= context->function->instruction_count) {
        return false;
    }
    definition = &context->function->instructions[core_value->definition];
    if (definition->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT) {
        return false;
    }
    bits = 0U;
    (void)memcpy(&bits, &definition->value.integer_value, sizeof(bits));
    value->type = core_value->type;
    value->bits = bits;
    return true;
}

static bool core_direct_local_read(const MinicCoreLowerContext *context,
                                   MinicExpressionId expression_id,
                                   MinicLocalId *local_id) {
    const MinicExpression *expression;
    const MinicExpression *operand;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        local_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL &&
        expression->value_category == MINIC_VALUE_RVALUE) {
        *local_id = expression->value.local_id;
        return true;
    }
    if (expression->kind != MINIC_EXPRESSION_LVALUE_READ) {
        return false;
    }
    operand = minic_c0_program_expression(
        context->body->program, expression->value.unary.operand);
    if (operand == NULL || operand->kind != MINIC_EXPRESSION_LOCAL ||
        operand->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    *local_id = operand->value.local_id;
    return true;
}

static bool core_const_signed_value(const MinicCoreLowerContext *context,
                                    const MinicConstValue *value,
                                    int64_t *result) {
    unsigned int width;
    uint64_t bits;
    uint64_t mask;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || value == NULL || result == NULL ||
        !minic_type_is_signed_integer(value->type) ||
        !minic_target_info_integer_width(
            context->target, context->body->program, value->type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    bits = value->bits;
    if (width < 64U) {
        mask = (UINT64_C(1) << width) - UINT64_C(1);
        bits &= mask;
        if ((bits & (UINT64_C(1) << (width - 1U))) != 0U) {
            bits |= ~mask;
        }
    }
    (void)memcpy(result, &bits, sizeof(*result));
    return true;
}

static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,
                                                MinicExpressionId expression_id,
                                                MinicConstValue *value) {
    const MinicExpression *expression;
    MinicConstValue operand_value;
    MinicLocalId local_id;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || value == NULL) {
        return false;
    }
    if (minic_const_eval_integer(
            context->body->program, context->target, expression_id, value)) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (core_direct_local_read(context, expression_id, &local_id) &&
        core_local_constant_get(context, local_id, &operand_value)) {
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        if (!core_const_eval_integer_with_locals(
                context, expression->value.unary.operand, &operand_value)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &operand_value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        bool is_zero;
        if (!core_const_eval_integer_with_locals(
                context, expression->value.unary.operand, &operand_value) ||
            !minic_const_value_is_zero(context->body->program,
                                       context->target,
                                       &operand_value,
                                       &is_zero)) {
            return false;
        }
        value->type = expression->type;
        value->bits = is_zero ? 1U : 0U;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicConstValue left;
        MinicConstValue right;
        MinicConstValue left_common;
        MinicConstValue right_common;
        MinicType common_type;
        bool predicate;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &left) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &right) ||
            !minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          left_expression->type,
                                                          right_expression->type,
                                                          &common_type) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &left,
                                               common_type,
                                               &left_common) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &right,
                                               common_type,
                                               &right_common)) {
            return false;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
            expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
            predicate = left_common.bits == right_common.bits;
            if (expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
                predicate = !predicate;
            }
        } else if (minic_type_is_signed_integer(common_type)) {
            int64_t left_signed;
            int64_t right_signed;
            if (!core_const_signed_value(context, &left_common, &left_signed) ||
                !core_const_signed_value(context, &right_common, &right_signed)) {
                return false;
            }
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_LESS: predicate = left_signed < right_signed; break;
            case MINIC_BINARY_LESS_EQUAL: predicate = left_signed <= right_signed; break;
            case MINIC_BINARY_GREATER: predicate = left_signed > right_signed; break;
            case MINIC_BINARY_GREATER_EQUAL: predicate = left_signed >= right_signed; break;
            default: return false;
            }
        } else {
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_LESS: predicate = left_common.bits < right_common.bits; break;
            case MINIC_BINARY_LESS_EQUAL: predicate = left_common.bits <= right_common.bits; break;
            case MINIC_BINARY_GREATER: predicate = left_common.bits > right_common.bits; break;
            case MINIC_BINARY_GREATER_EQUAL: predicate = left_common.bits >= right_common.bits; break;
            default: return false;
            }
        }
        value->type = expression->type;
        value->bits = predicate ? 1U : 0U;
        return true;
    }
    return false;
}
'''

replace_once(
    "src/core/core_lower.c",
    'static MinicCoreLowerStatus lower_scalar_assignment_value(MinicCoreLowerContext *context,\n'
    '                                                          MinicType target_type,\n'
    '                                                          MinicExpressionId expression_id,\n'
    '                                                          MinicCoreValueId *value_id);\n\n'
    'MinicCoreLowerStatus ensure_statement_block',
    'static MinicCoreLowerStatus lower_scalar_assignment_value(MinicCoreLowerContext *context,\n'
    '                                                          MinicType target_type,\n'
    '                                                          MinicExpressionId expression_id,\n'
    '                                                          MinicCoreValueId *value_id);\n'
    + helpers + '\nMinicCoreLowerStatus ensure_statement_block',
)

# Fold an integer expression immediately when the straight-line local fact table
# can prove it.  Pure AST constants remain owned by the existing evaluator.
replace_once(
    "src/core/core_lower.c",
    '    expression = minic_c0_program_expression(context->body->program, expression_id);\n'
    '    if (expression == NULL) {\n'
    '        return MINIC_CORE_LOWER_ERROR;\n'
    '    }\n'
    '    /* Runtime allocation is a pointer-producing rvalue.',
    '    expression = minic_c0_program_expression(context->body->program, expression_id);\n'
    '    if (expression == NULL) {\n'
    '        return MINIC_CORE_LOWER_ERROR;\n'
    '    }\n'
    '    if (minic_type_is_integer(expression->type) && context->target != NULL) {\n'
    '        MinicConstValue tracked_constant;\n'
    '        if (core_const_eval_integer_with_locals(context, expression_id, &tracked_constant) &&\n'
    '            minic_type_equal(tracked_constant.type, expression->type)) {\n'
    '            uint64_t tracked_bits = tracked_constant.bits;\n'
    '            (void)memset(&instruction, 0, sizeof(instruction));\n'
    '            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;\n'
    '            instruction.span = expression->span;\n'
    '            instruction.type = expression->type;\n'
    '            instruction.result = MINIC_CORE_VALUE_INVALID;\n'
    '            (void)memcpy(&instruction.value.integer_value, &tracked_bits, sizeof(tracked_bits));\n'
    '            return minic_core_function_append_value_instruction(\n'
    '                       context->function, context->block_id, &instruction, value_id)\n'
    '                       ? MINIC_CORE_LOWER_OK\n'
    '                       : MINIC_CORE_LOWER_ERROR;\n'
    '        }\n'
    '    }\n'
    '    /* Runtime allocation is a pointer-producing rvalue.',
)

# An explicit source-language address escape permanently disables substitution
# for that local.  Internal lowering calls to lower_address() do not count.
replace_once(
    "src/core/core_lower.c",
    '    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {\n'
    '        MinicCoreLowerStatus status;\n\n'
    '        status = lower_address(context, expression->value.unary.operand, value_id);',
    '    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {\n'
    '        const MinicExpression *addressed_expression;\n'
    '        MinicCoreLowerStatus status;\n\n'
    '        addressed_expression = minic_c0_program_expression(\n'
    '            context->body->program, expression->value.unary.operand);\n'
    '        if (addressed_expression != NULL &&\n'
    '            addressed_expression->kind == MINIC_EXPRESSION_LOCAL) {\n'
    '            core_local_constant_escape(context, addressed_expression->value.local_id);\n'
    '        }\n'
    '        status = lower_address(context, expression->value.unary.operand, value_id);',
)

# Any non-simple update to a direct local kills its fact before the generic
# lowering path reads/writes memory.
replace_once(
    "src/core/core_lower.c",
    '    if (expression->kind == MINIC_EXPRESSION_UNARY &&\n'
    '        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||\n'
    '         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||\n'
    '         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||\n'
    '         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT)) {\n'
    '        return lower_scalar_update(context, expression, value_id);\n'
    '    }',
    '    if (expression->kind == MINIC_EXPRESSION_UNARY &&\n'
    '        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||\n'
    '         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||\n'
    '         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||\n'
    '         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT)) {\n'
    '        const MinicExpression *updated_expression = minic_c0_program_expression(\n'
    '            context->body->program, expression->value.unary.operand);\n'
    '        if (updated_expression != NULL &&\n'
    '            updated_expression->kind == MINIC_EXPRESSION_LOCAL) {\n'
    '            core_local_constant_invalidate(context, updated_expression->value.local_id);\n'
    '        }\n'
    '        return lower_scalar_update(context, expression, value_id);\n'
    '    }',
)

# After a simple direct-local store, retain the fact only if the actual stored
# Core value is an integer constant after assignment conversion.
replace_once(
    "src/core/core_lower.c",
    '    if (!minic_core_function_append_effect_instruction(\n'
    '            context->function, context->block_id, &instruction)) {\n'
    '        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=store status=%d\\n",\n'
    '                      context->source_function != NULL ? context->source_function->name : "?",\n'
    '                      (int)MINIC_CORE_LOWER_ERROR);\n'
    '        return MINIC_CORE_LOWER_ERROR;\n'
    '    }\n'
    '    if (result_value != NULL) {\n'
    '        *result_value = stored_value;\n'
    '    }',
    '    if (!minic_core_function_append_effect_instruction(\n'
    '            context->function, context->block_id, &instruction)) {\n'
    '        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=store status=%d\\n",\n'
    '                      context->source_function != NULL ? context->source_function->name : "?",\n'
    '                      (int)MINIC_CORE_LOWER_ERROR);\n'
    '        return MINIC_CORE_LOWER_ERROR;\n'
    '    }\n'
    '    if (target->kind == MINIC_EXPRESSION_LOCAL) {\n'
    '        MinicConstValue stored_constant;\n'
    '        core_local_constant_invalidate(context, target->value.local_id);\n'
    '        if (core_value_integer_constant(context, stored_value, &stored_constant)) {\n'
    '            core_local_constant_set(context, target->value.local_id, &stored_constant);\n'
    '        }\n'
    '    }\n'
    '    if (result_value != NULL) {\n'
    '        *result_value = stored_value;\n'
    '    }',
)

# Use the local-aware evaluator for source-level if pruning.  If it cannot prove
# the condition, discard straight-line facts before building a multi-entry CFG.
replace_once(
    "src/core/core_lower.c",
    '        if (minic_const_eval_integer(context->body->program,\n'
    '                                     context->target,\n'
    '                                     statement->expression,\n'
    '                                     &condition_value) &&',
    '        if (core_const_eval_integer_with_locals(\n'
    '                context, statement->expression, &condition_value) &&',
)
replace_once(
    "src/core/core_lower.c",
    '    condition_block = context->block_id;\n'
    '    if (!minic_core_function_add_block(context->function, &then_block)) {',
    '    core_local_constants_clear_known(context);\n'
    '    condition_block = context->block_id;\n'
    '    if (!minic_core_function_add_block(context->function, &then_block)) {',
)
replace_once(
    "src/core/core_lower.c",
    '    if (else_source != NULL) {\n'
    '        context->block_id = else_block;\n'
    '        status = lower_block(context, else_source, &else_terminated);',
    '    if (else_source != NULL) {\n'
    '        core_local_constants_clear_known(context);\n'
    '        context->block_id = else_block;\n'
    '        status = lower_block(context, else_source, &else_terminated);',
)
replace_once(
    "src/core/core_lower.c",
    '    context->block_id = continuation_block;\n'
    '    *terminated = !needs_merge;\n'
    '    return MINIC_CORE_LOWER_OK;\n'
    '}\n\nstatic bool internal_while_label_pair',
    '    context->block_id = continuation_block;\n'
    '    core_local_constants_clear_known(context);\n'
    '    *terminated = !needs_merge;\n'
    '    return MINIC_CORE_LOWER_OK;\n'
    '}\n\nstatic bool internal_while_label_pair',
)

# Uncertain loop/switch CFG is a hard fact barrier.  Inline asm is also an
# unknown write boundary. Constant ifs bypass the general lower_if path above
# and therefore preserve facts through their selected arm.
replace_once(
    "src/core/core_lower.c",
    '            case MINIC_STATEMENT_INLINE_ASM:\n'
    '                status = minic_core_lower_inline_asm(context, statement);\n'
    '                break;',
    '            case MINIC_STATEMENT_INLINE_ASM:\n'
    '                status = minic_core_lower_inline_asm(context, statement);\n'
    '                core_local_constants_clear_known(context);\n'
    '                break;',
)
replace_once(
    "src/core/core_lower.c",
    '            case MINIC_STATEMENT_WHILE:\n'
    '                status = lower_while(\n'
    '                    context, statement, MINIC_STATEMENT_INVALID, &statement_terminated);\n'
    '                break;\n'
    '            case MINIC_STATEMENT_SWITCH:\n'
    '                status = lower_switch(context, statement, &statement_terminated);\n'
    '                break;',
    '            case MINIC_STATEMENT_WHILE:\n'
    '                core_local_constants_clear_known(context);\n'
    '                status = lower_while(\n'
    '                    context, statement, MINIC_STATEMENT_INVALID, &statement_terminated);\n'
    '                core_local_constants_clear_known(context);\n'
    '                break;\n'
    '            case MINIC_STATEMENT_SWITCH:\n'
    '                core_local_constants_clear_known(context);\n'
    '                status = lower_switch(context, statement, &statement_terminated);\n'
    '                core_local_constants_clear_known(context);\n'
    '                break;',
)

# Allocate/free one local-fact table next to the existing local-object table.
replace_once(
    "src/core/core_lower.c",
    '    MinicCoreObjectId *local_objects;\n'
    '    MinicCoreBlockId *statement_blocks;\n',
    '    MinicCoreObjectId *local_objects;\n'
    '    MinicCoreLocalIntegerConstant *local_integer_constants;\n'
    '    MinicCoreBlockId *statement_blocks;\n',
)
replace_once(
    "src/core/core_lower.c",
    '    if (source_function->local_count != 0U && local_objects == NULL) {\n'
    '        return MINIC_CORE_LOWER_ERROR;\n'
    '    }\n'
    '    for (local_index = 0U; local_index < source_function->local_count; ++local_index) local_objects[local_index] = MINIC_CORE_OBJECT_INVALID;\n'
    '    if (body->program->statement_count > SIZE_MAX / sizeof(*statement_blocks)) { free(local_objects); return MINIC_CORE_LOWER_ERROR; }',
    '    if (source_function->local_count != 0U && local_objects == NULL) {\n'
    '        return MINIC_CORE_LOWER_ERROR;\n'
    '    }\n'
    '    local_integer_constants =\n'
    '        source_function->local_count == 0U\n'
    '            ? NULL\n'
    '            : (MinicCoreLocalIntegerConstant *)calloc(\n'
    '                  source_function->local_count, sizeof(*local_integer_constants));\n'
    '    if (source_function->local_count != 0U && local_integer_constants == NULL) {\n'
    '        free(local_objects);\n'
    '        return MINIC_CORE_LOWER_ERROR;\n'
    '    }\n'
    '    for (local_index = 0U; local_index < source_function->local_count; ++local_index) local_objects[local_index] = MINIC_CORE_OBJECT_INVALID;\n'
    '    if (body->program->statement_count > SIZE_MAX / sizeof(*statement_blocks)) { free(local_integer_constants); free(local_objects); return MINIC_CORE_LOWER_ERROR; }',
)
replace_once(
    "src/core/core_lower.c",
    '    if (body->program->statement_count != 0U && statement_blocks == NULL) { free(local_objects); return MINIC_CORE_LOWER_ERROR; }',
    '    if (body->program->statement_count != 0U && statement_blocks == NULL) { free(local_integer_constants); free(local_objects); return MINIC_CORE_LOWER_ERROR; }',
)
replace_once(
    "src/core/core_lower.c",
    '        free(statement_blocks); free(local_objects); minic_core_function_destroy(&lowered); return MINIC_CORE_LOWER_ERROR;',
    '        free(statement_blocks); free(local_integer_constants); free(local_objects); minic_core_function_destroy(&lowered); return MINIC_CORE_LOWER_ERROR;',
)
replace_once(
    "src/core/core_lower.c",
    '    context.local_objects = local_objects;\n'
    '    context.statement_blocks = statement_blocks;',
    '    context.local_objects = local_objects;\n'
    '    context.local_integer_constants = local_integer_constants;\n'
    '    context.statement_blocks = statement_blocks;',
)
replace_once(
    "src/core/core_lower.c",
    '    free(statement_blocks); free(local_objects);\n'
    '    if (status != MINIC_CORE_LOWER_OK) {',
    '    free(statement_blocks); free(local_integer_constants); free(local_objects);\n'
    '    context.local_integer_constants = NULL;\n'
    '    if (status != MINIC_CORE_LOWER_OK) {',
)

print("MINIC_LOCAL_INTEGER_CONSTANTS_PATCH=APPLIED")
