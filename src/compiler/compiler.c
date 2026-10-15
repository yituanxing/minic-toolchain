#include "minic/compiler.h"

#include "core/core_ir.h"
#include "core/core_lower.h"
#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/cast_normalization.h"
#include "frontend/const_eval.h"
#include "frontend/function_body.h"
#include "frontend/parser.h"
#include "target/riscv64/codegen.h"
#include "target/riscv64/layout.h"
#include "target/target_info.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicSourceBuffer {
    char *data;
    size_t size;
} MinicSourceBuffer;

typedef struct MinicCoreFunctionSet {
    MinicCoreFunction *functions;
    MinicCoreLowerStatus *statuses;
    size_t function_count;
} MinicCoreFunctionSet;

/* Bootstrap diagnostics are deliberately opt-in.  They let a target-built
   compiler expose the exact phase/function where self-host replay stops
   making progress without changing normal compiler output or semantics. */
static bool minic_bootstrap_trace_enabled(void) {
    return getenv("MINIC_BOOTSTRAP_TRACE") != NULL;
}

static void minic_bootstrap_trace_stage(const char *input_path,
                                        const char *stage,
                                        const char *state,
                                        size_t function_count) {
    if (!minic_bootstrap_trace_enabled()) {
        return;
    }
    (void)fprintf(stderr,
                  "MINIC_BOOTSTRAP_TRACE stage=%s state=%s functions=%zu input=%s\n",
                  stage != NULL ? stage : "?",
                  state != NULL ? state : "?",
                  function_count,
                  input_path != NULL ? input_path : "?");
    (void)fflush(stderr);
}

static void minic_set_diagnostic(MinicDiagnostic *diagnostic,
                                 const char *path,
                                 size_t line,
                                 size_t column,
                                 const char *format,
                                 ...) {
    va_list arguments;

    if (diagnostic == NULL) {
        return;
    }
    diagnostic->path = path;
    diagnostic->line = line;
    diagnostic->column = column;
    va_start(arguments, format);
    (void)vsnprintf(diagnostic->message, sizeof(diagnostic->message), format, arguments);
    va_end(arguments);
}

static void minic_core_function_set_initialize(MinicCoreFunctionSet *set) {
    if (set == NULL) {
        return;
    }
    set->functions = NULL;
    set->statuses = NULL;
    set->function_count = 0U;
}

static void minic_core_function_set_destroy(MinicCoreFunctionSet *set) {
    size_t function_index;

    if (set == NULL) {
        return;
    }
    if (set->functions != NULL) {
        for (function_index = 0U; function_index < set->function_count; ++function_index) {
            minic_core_function_destroy(&set->functions[function_index]);
        }
    }
    free(set->functions);
    free(set->statuses);
    minic_core_function_set_initialize(set);
}

static bool minic_prepare_core_function_set(const MinicC0Program *program,
                                            const MinicTargetInfo *target,
                                            MinicCoreFunctionSet *output) {
    MinicCoreFunctionSet set;
    size_t function_index;

    if (program == NULL || target == NULL || output == NULL ||
        program->function_count > SIZE_MAX / sizeof(*set.functions) ||
        program->function_count > SIZE_MAX / sizeof(*set.statuses)) {
        return false;
    }
    minic_core_function_set_initialize(&set);
    set.function_count = program->function_count;
    if (set.function_count != 0U) {
        set.functions =
            (MinicCoreFunction *)calloc(set.function_count, sizeof(*set.functions));
        set.statuses = (MinicCoreLowerStatus *)malloc(set.function_count *
                                                       sizeof(*set.statuses));
        if (set.functions == NULL || set.statuses == NULL) {
            free(set.functions);
            free(set.statuses);
            return false;
        }
    }
    for (function_index = 0U; function_index < set.function_count; ++function_index) {
        minic_core_function_initialize(&set.functions[function_index]);
        set.statuses[function_index] = MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (function_index = 0U; function_index < set.function_count; ++function_index) {
        const MinicFunction *function;
        MinicFunctionBodyView body;

        function = minic_c0_program_function(program, function_index);
        if (function == NULL) {
            set.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            continue;
        }
        if (!function->is_defined) {
            continue;
        }
        if (function->is_internal && !function->is_referenced) {
            continue;
        }
        if (!minic_c0_function_body_view(program, function_index, &body)) {
            set.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            continue;
        }
        if (minic_bootstrap_trace_enabled()) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-lower state=begin "
                          "index=%zu function=%s\n",
                          function_index,
                          function->name != NULL ? function->name : "?");
            (void)fflush(stderr);
        }
        set.statuses[function_index] =
            minic_core_lower_function(&body, target, &set.functions[function_index]);
        if (minic_bootstrap_trace_enabled()) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-lower state=end "
                          "index=%zu function=%s status=%d blocks=%zu instructions=%zu\n",
                          function_index,
                          function->name != NULL ? function->name : "?",
                          (int)set.statuses[function_index],
                          set.functions[function_index].block_count,
                          set.functions[function_index].instruction_count);
            (void)fflush(stderr);
        }
    }
    minic_core_function_set_destroy(output);
    *output = set;
    return true;
}



#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 2048U

static bool minic_inline_integer_specialization_matches(
    const MinicFunction *candidate,
    MinicFunctionId source_id,
    const bool *known,
    const uint64_t *bits,
    size_t parameter_count) {
    size_t i;
    if (candidate == NULL || !candidate->is_integer_specialization ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count) {
        return false;
    }
    for (i = 0U; i < parameter_count; ++i) {
        if (candidate->specialization_integer_known[i] != known[i] ||
            (known[i] && candidate->specialization_integer_bits[i] != bits[i])) {
            return false;
        }
    }
    return true;
}

static bool minic_add_inline_integer_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const bool *known,
    const uint64_t *bits,
    size_t ordinal,
    MinicFunctionId *specialized_id) {
    MinicFunction source;
    MinicFunction *specialized;
    char name[96];
    int name_length;
    size_t i;

    if (program == NULL || source_id >= program->function_count ||
        specialized_id == NULL) {
        return false;
    }
    source = program->functions[source_id];
    if (!source.is_defined || !source.is_internal || !source.is_inline ||
        source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID) {
        return false;
    }
    name_length = snprintf(name, sizeof(name), "__minic_inline_spec_%zu_%zu",
                           (size_t)source_id, ordinal);
    if (name_length <= 0 || (size_t)name_length >= sizeof(name) ||
        !minic_c0_program_add_function(program, name, (size_t)name_length,
                                       source.local_begin, source.local_count,
                                       source.body_block, specialized_id) ||
        !minic_c0_program_set_function_signature(program, *specialized_id,
                                                 source.return_type,
                                                 source.parameter_types,
                                                 source.parameter_count) ||
        !minic_c0_program_set_function_internal(program, *specialized_id, true) ||
        !minic_c0_program_set_function_inline(program, *specialized_id, true) ||
        (source.is_noreturn &&
         !minic_c0_program_set_function_noreturn(program, *specialized_id, true)) ||
        (source.section_name != NULL &&
         !minic_c0_program_set_function_section(program, *specialized_id,
                                                source.section_name,
                                                source.section_name_length))) {
        return false;
    }
    specialized = &program->functions[*specialized_id];
    specialized->visibility = source.visibility;
    specialized->specialization_source = source_id;
    specialized->is_integer_specialization = true;
    for (i = 0U; i < source.parameter_count; ++i) {
        specialized->specialization_integer_known[i] = known[i];
        specialized->specialization_integer_bits[i] = bits[i];
    }
    return true;
}



static bool minic_inline_specialization_argument_value(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicType target_type,
    MinicConstValue *value,
    bool *used_caller_fact) {
    const MinicExpression *expression;
    MinicConstValue operand_value;
    MinicConstValue constant_value;

    if (program == NULL || target == NULL || caller == NULL || value == NULL ||
        used_caller_fact == NULL || !caller->is_integer_specialization ||
        caller->specialization_source >= program->function_count ||
        !minic_type_is_integer(target_type)) {
        return false;
    }
    *used_caller_fact = false;
    if (minic_const_eval_integer(program, target, expression_id, &constant_value) &&
        minic_const_value_convert_integer(
            program, target, &constant_value, target_type, value)) {
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        bool operand_used_caller_fact = false;
        if (!minic_inline_specialization_argument_value(
                program,
                target,
                caller,
                expression->value.unary.operand,
                expression->type,
                &operand_value,
                &operand_used_caller_fact) ||
            !minic_const_value_convert_integer(
                program, target, &operand_value, target_type, value)) {
            return false;
        }
        *used_caller_fact = operand_used_caller_fact;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicFunction *source;
        MinicLocalId local_id;
        size_t parameter_index;

        source = &program->functions[caller->specialization_source];
        local_id = expression->value.local_id;
        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_integer_known[parameter_index] ||
            !minic_type_is_integer(source->parameter_types[parameter_index])) {
            return false;
        }
        constant_value.type = source->parameter_types[parameter_index];
        constant_value.bits = caller->specialization_integer_bits[parameter_index];
        if (!minic_const_value_convert_integer(
                program, target, &constant_value, target_type, value)) {
            return false;
        }
        *used_caller_fact = true;
        return true;
    }
    return false;
}

static bool minic_specialize_inline_integer_calls(MinicC0Program *program,
                                                  const MinicTargetInfo *target) {
    size_t expression_index;
    size_t original_function_count;
    size_t specialization_count = 0U;

    if (program == NULL || target == NULL) {
        return false;
    }
    original_function_count = program->function_count;
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        MinicExpression *expression = &program->expressions[expression_index];
        MinicFunctionId source_id;
        MinicFunctionId specialized_id = MINIC_FUNCTION_INVALID;
        MinicFunction source;
        bool known[MINIC_MAX_FUNCTION_PARAMETERS];
        uint64_t bits[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t i;
        size_t candidate_index;
        bool has_integer_constant = false;

        if (expression->kind != MINIC_EXPRESSION_CALL ||
            expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            continue;
        }
        source_id = expression->value.call.function_id;
        if (source_id >= original_function_count) {
            continue;
        }
        source = program->functions[source_id];
        if (!source.is_defined || !source.is_internal || !source.is_inline ||
            !source.is_referenced || source.is_variadic ||
            source.alias_target != MINIC_FUNCTION_INVALID || source.parameter_count == 0U ||
            source.parameter_count != expression->value.call.argument_count) {
            continue;
        }
        (void)memset(known, 0, sizeof(known));
        (void)memset(bits, 0, sizeof(bits));
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicConstValue argument_value;
            MinicConstValue converted_value;
            if (!minic_type_is_integer(source.parameter_types[i]) ||
                !minic_const_eval_integer(program, target,
                                          expression->value.call.arguments[i],
                                          &argument_value) ||
                !minic_const_value_convert_integer(program, target, &argument_value,
                                                   source.parameter_types[i],
                                                   &converted_value)) {
                continue;
            }
            known[i] = true;
            bits[i] = converted_value.bits;
            has_integer_constant = true;
        }
        if (!has_integer_constant) {
            continue;
        }
        for (candidate_index = original_function_count;
             candidate_index < program->function_count; ++candidate_index) {
            if (minic_inline_integer_specialization_matches(
                    &program->functions[candidate_index], source_id, known, bits,
                    source.parameter_count)) {
                specialized_id = candidate_index;
                break;
            }
        }
        if (specialized_id == MINIC_FUNCTION_INVALID) {
            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT) {
                (void)fprintf(stderr,
                              "MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT_HIT count=%zu limit=%u functions=%zu source=%zu\n",
                              specialization_count,
                              (unsigned int)MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT,
                              program->function_count,
                              (size_t)source_id);
                return false;
            }
            if (!minic_add_inline_integer_specialization(program, source_id, known, bits,
                                                         specialization_count,
                                                         &specialized_id)) {
                (void)fprintf(stderr,
                              "MINIC_INLINE_INTEGER_SPECIALIZATION_ADD_FAILED count=%zu functions=%zu source=%zu\n",
                              specialization_count,
                              program->function_count,
                              (size_t)source_id);
                return false;
            }
            specialization_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = specialized_id;
    }

    {
        size_t caller_index;
        size_t refinement_count = 0U;

        for (caller_index = original_function_count;
             caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            size_t nested_expression_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= original_function_count) {
                continue;
            }
            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;
                 ++nested_expression_index) {
                const MinicExpression *nested = &program->expressions[nested_expression_index];
                MinicFunctionId current_callee_id;
                MinicFunctionId nested_source_id;
                MinicFunctionId refined_id = MINIC_FUNCTION_INVALID;
                MinicFunction current_callee;
                MinicFunction nested_source;
                bool nested_known[MINIC_MAX_FUNCTION_PARAMETERS];
                uint64_t nested_bits[MINIC_MAX_FUNCTION_PARAMETERS];
                bool used_caller_fact = false;
                bool adds_fact = false;
                size_t parameter_index;
                size_t nested_candidate_index;

                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                current_callee_id = nested->value.call.function_id;
                if (current_callee_id >= program->function_count) {
                    continue;
                }
                current_callee = program->functions[current_callee_id];
                if (current_callee.is_integer_specialization) {
                    if (current_callee.specialization_source >= original_function_count) {
                        continue;
                    }
                    nested_source_id = current_callee.specialization_source;
                } else {
                    if (current_callee_id >= original_function_count) {
                        continue;
                    }
                    nested_source_id = current_callee_id;
                }
                nested_source = program->functions[nested_source_id];
                if (!nested_source.is_defined || !nested_source.is_internal ||
                    !nested_source.is_inline || nested_source.is_variadic ||
                    nested_source.alias_target != MINIC_FUNCTION_INVALID ||
                    nested_source.parameter_count == 0U ||
                    nested_source.parameter_count != nested->value.call.argument_count) {
                    continue;
                }
                (void)memset(nested_known, 0, sizeof(nested_known));
                (void)memset(nested_bits, 0, sizeof(nested_bits));
                for (parameter_index = 0U;
                     parameter_index < nested_source.parameter_count;
                     ++parameter_index) {
                    MinicConstValue argument_value;
                    bool argument_used_caller_fact = false;

                    if (!minic_type_is_integer(nested_source.parameter_types[parameter_index]) ||
                        !minic_inline_specialization_argument_value(
                            program,
                            target,
                            &caller,
                            nested->value.call.arguments[parameter_index],
                            nested_source.parameter_types[parameter_index],
                            &argument_value,
                            &argument_used_caller_fact)) {
                        continue;
                    }
                    nested_known[parameter_index] = true;
                    nested_bits[parameter_index] = argument_value.bits;
                    used_caller_fact = used_caller_fact || argument_used_caller_fact;
                    if (!current_callee.is_integer_specialization ||
                        !current_callee.specialization_integer_known[parameter_index] ||
                        current_callee.specialization_integer_bits[parameter_index] !=
                            argument_value.bits) {
                        adds_fact = true;
                    }
                }
                if (!used_caller_fact || !adds_fact) {
                    continue;
                }
                for (nested_candidate_index = original_function_count;
                     nested_candidate_index < program->function_count;
                     ++nested_candidate_index) {
                    if (minic_inline_integer_specialization_matches(
                            &program->functions[nested_candidate_index],
                            nested_source_id,
                            nested_known,
                            nested_bits,
                            nested_source.parameter_count)) {
                        refined_id = nested_candidate_index;
                        break;
                    }
                }
                if (refined_id == MINIC_FUNCTION_INVALID) {
                    if (specialization_count >= 1024U ||
                        !minic_add_inline_integer_specialization(
                            program,
                            nested_source_id,
                            nested_known,
                            nested_bits,
                            specialization_count,
                            &refined_id)) {
                        return false;
                    }
                    specialization_count += 1U;
                    refinement_count += 1U;
                }
            }
        }
        if (refinement_count != 0U) {
            (void)fprintf(stderr,
                          "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_V0 refined=%zu clones=%zu functions=%zu\n",
                          refinement_count,
                          specialization_count,
                          program->function_count);
        }
    }
    if (specialization_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_INTEGER_SPECIALIZATION_V0 clones=%zu functions=%zu\n",
                      specialization_count, program->function_count);
    }
    return true;
}



static bool minic_inline_symbolic_static_expression_depth(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (program == NULL || target == NULL || depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT ||
        expression->kind == MINIC_EXPRESSION_FUNCTION) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return minic_inline_symbolic_static_expression_depth(
            program, target, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        return operand != NULL && operand->kind == MINIC_EXPRESSION_LOCAL && false;
    }
    /* A nested constant array subscript is still a static lvalue rooted in the
     * same global object.  This matters for addresses such as
     * &global_matrix[CONST_A][CONST_B]: ADDRESS_OF sees the outer SUBSCRIPT,
     * then recursion must be able to walk the inner array-valued SUBSCRIPT. */
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT &&
        minic_type_is_array(expression->type)) {
        MinicConstValue index_value;
        return minic_inline_symbolic_static_expression_depth(
                   program, target, expression->value.subscript.base, depth + 1U) &&
               minic_const_eval_integer(
                   program, target, expression->value.subscript.index, &index_value);
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        if (operand == NULL) {
            return false;
        }
        if (operand->kind == MINIC_EXPRESSION_GLOBAL_OBJECT ||
            operand->kind == MINIC_EXPRESSION_FUNCTION) {
            return true;
        }
        if (operand->kind == MINIC_EXPRESSION_DEREFERENCE) {
            return minic_inline_symbolic_static_expression_depth(
                program, target, operand->value.unary.operand, depth + 1U);
        }
        if (operand->kind == MINIC_EXPRESSION_MEMBER) {
            return minic_inline_symbolic_static_expression_depth(
                program, target, operand->value.member.base, depth + 1U);
        }
        if (operand->kind == MINIC_EXPRESSION_SUBSCRIPT) {
            MinicConstValue index_value;
            return minic_inline_symbolic_static_expression_depth(
                       program, target, operand->value.subscript.base, depth + 1U) &&
                   minic_const_eval_integer(
                       program, target, operand->value.subscript.index, &index_value);
        }
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        MinicConstValue integer_value;
        if (minic_inline_symbolic_static_expression_depth(
                program, target, expression->value.binary.left, depth + 1U) &&
            minic_const_eval_integer(
                program, target, expression->value.binary.right, &integer_value)) {
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
            minic_const_eval_integer(
                program, target, expression->value.binary.left, &integer_value) &&
            minic_inline_symbolic_static_expression_depth(
                program, target, expression->value.binary.right, depth + 1U)) {
            return true;
        }
    }
    return false;
}

static bool minic_inline_symbolic_static_expression(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    MinicExpressionId expression_id) {
    return minic_inline_symbolic_static_expression_depth(
        program, target, expression_id, 0U);
}

static bool minic_inline_close_symbolic_expression_depth(
    MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicExpressionId *closed_id,
    bool *used_caller_integer_fact,
    unsigned int depth) {
    const MinicExpression *expression;
    const MinicFunction *caller_source;
    MinicExpression closed;
    MinicExpressionId left_id;
    MinicExpressionId right_id;
    bool left_used = false;
    bool right_used = false;

    if (program == NULL || target == NULL || caller == NULL || closed_id == NULL ||
        used_caller_integer_fact == NULL || depth > 32U ||
        !caller->is_integer_specialization ||
        caller->specialization_source >= program->function_count) {
        return false;
    }
    *used_caller_integer_fact = false;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    caller_source = &program->functions[caller->specialization_source];

    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;
        uint64_t bits;

        if (local_id < caller_source->local_begin) {
            return false;
        }
        parameter_index = local_id - caller_source->local_begin;
        if (parameter_index >= caller_source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_integer_known[parameter_index] ||
            !minic_type_is_integer(caller_source->parameter_types[parameter_index])) {
            return false;
        }
        (void)memset(&closed, 0, sizeof(closed));
        closed.kind = MINIC_EXPRESSION_INTEGER;
        closed.span = expression->span;
        closed.type = caller_source->parameter_types[parameter_index];
        closed.value_category = MINIC_VALUE_RVALUE;
        bits = caller->specialization_integer_bits[parameter_index];
        (void)memcpy(&closed.value.integer_value, &bits, sizeof(bits));
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        if (operand != NULL && operand->kind == MINIC_EXPRESSION_LOCAL) {
            if (!minic_inline_close_symbolic_expression_depth(
                    program,
                    target,
                    caller,
                    expression->value.unary.operand,
                    &left_id,
                    &left_used,
                    depth + 1U) ||
                !left_used) {
                return false;
            }
            *closed_id = left_id;
            *used_caller_integer_fact = true;
            return true;
        }
    }

    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ ||
        expression->kind == MINIC_EXPRESSION_ADDRESS_OF ||
        expression->kind == MINIC_EXPRESSION_DEREFERENCE) {
        if (!minic_inline_close_symbolic_expression_depth(
                program,
                target,
                caller,
                expression->value.unary.operand,
                &left_id,
                &left_used,
                depth + 1U)) {
            return false;
        }
        if (!left_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.unary.operand = left_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        bool base_static = minic_inline_symbolic_static_expression(
            program, target, expression->value.subscript.base);
        if (base_static) {
            left_id = expression->value.subscript.base;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.subscript.base,
                       &left_id,
                       &left_used,
                       depth + 1U)) {
            return false;
        }
        if (minic_const_eval_integer(
                program, target, expression->value.subscript.index, &(MinicConstValue){0})) {
            right_id = expression->value.subscript.index;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.subscript.index,
                       &right_id,
                       &right_used,
                       depth + 1U)) {
            return false;
        }
        if (!left_used && !right_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.subscript.base = left_id;
        closed.value.subscript.index = right_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = left_used || right_used;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        if (minic_inline_symbolic_static_expression(
                program, target, expression->value.member.base)) {
            *closed_id = expression_id;
            return true;
        }
        if (!minic_inline_close_symbolic_expression_depth(
                program,
                target,
                caller,
                expression->value.member.base,
                &left_id,
                &left_used,
                depth + 1U)) {
            return false;
        }
        if (!left_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.member.base = left_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        if (minic_inline_symbolic_static_expression(
                program, target, expression->value.binary.left)) {
            left_id = expression->value.binary.left;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.binary.left,
                       &left_id,
                       &left_used,
                       depth + 1U)) {
            return false;
        }
        if (minic_const_eval_integer(
                program, target, expression->value.binary.right, &(MinicConstValue){0})) {
            right_id = expression->value.binary.right;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.binary.right,
                       &right_id,
                       &right_used,
                       depth + 1U)) {
            return false;
        }
        if (!left_used && !right_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.binary.left = left_id;
        closed.value.binary.right = right_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = left_used || right_used;
        return true;
    }

    return false;
}

static bool minic_inline_symbolic_argument_expression(
    MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicExpressionId *symbolic_expression,
    bool *used_caller_fact,
    unsigned int depth) {
    const MinicExpression *expression;
    MinicExpressionId closed_id;
    bool used_integer_fact = false;

    if (program == NULL || target == NULL || symbolic_expression == NULL ||
        used_caller_fact == NULL || depth > 32U) {
        return false;
    }
    *used_caller_fact = false;
    if (minic_inline_symbolic_static_expression(program, target, expression_id)) {
        *symbolic_expression = expression_id;
        return true;
    }
    if (caller == NULL || !caller->is_integer_specialization ||
        caller->specialization_source >= program->function_count) {
        return false;
    }

    if (minic_inline_close_symbolic_expression_depth(
            program,
            target,
            caller,
            expression_id,
            &closed_id,
            &used_integer_fact,
            depth + 1U) &&
        used_integer_fact &&
        minic_inline_symbolic_static_expression(program, target, closed_id)) {
        *symbolic_expression = closed_id;
        *used_caller_fact = true;
        return true;
    }

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return minic_inline_symbolic_argument_expression(
            program,
            target,
            caller,
            expression->value.unary.operand,
            symbolic_expression,
            used_caller_fact,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicFunction *source = &program->functions[caller->specialization_source];
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;

        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_symbolic_known[parameter_index]) {
            return false;
        }
        *symbolic_expression =
            caller->specialization_symbolic_expression[parameter_index];
        *used_caller_fact = true;
        return *symbolic_expression != MINIC_EXPRESSION_INVALID;
    }
    return false;
}

static bool minic_inline_symbolic_variant_matches(
    const MinicFunction *candidate,
    MinicFunctionId source_id,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    size_t parameter_count) {
    size_t i;

    if (candidate == NULL || !candidate->is_integer_specialization ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count) {
        return false;
    }
    for (i = 0U; i < parameter_count; ++i) {
        if (candidate->specialization_integer_known[i] != integer_known[i] ||
            (integer_known[i] &&
             candidate->specialization_integer_bits[i] != integer_bits[i]) ||
            candidate->specialization_symbolic_known[i] != symbolic_known[i] ||
            (symbolic_known[i] &&
             candidate->specialization_symbolic_expression[i] != symbolic_expression[i])) {
            return false;
        }
    }
    return true;
}

static bool minic_add_inline_symbolic_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    MinicFunctionId *specialized_id) {
    MinicFunction *specialized;
    size_t i;
    size_t ordinal;

    if (program == NULL || source_id >= program->function_count ||
        specialized_id == NULL) {
        return false;
    }
    ordinal = program->function_count;
    if (!minic_add_inline_integer_specialization(
            program,
            source_id,
            integer_known,
            integer_bits,
            ordinal,
            specialized_id)) {
        return false;
    }
    specialized = &program->functions[*specialized_id];
    for (i = 0U; i < specialized->parameter_count; ++i) {
        specialized->specialization_symbolic_known[i] = symbolic_known[i];
        specialized->specialization_symbolic_expression[i] =
            symbolic_known[i] ? symbolic_expression[i] : MINIC_EXPRESSION_INVALID;
    }
    return true;
}

static bool minic_specialize_inline_symbolic_calls(
    MinicC0Program *program,
    const MinicTargetInfo *target) {
    size_t expression_index;
    size_t initial_clone_count = 0U;
    size_t transitive_clone_count = 0U;
    size_t original_function_count;

    if (program == NULL || target == NULL) {
        return false;
    }
    original_function_count = program->function_count;

    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        MinicExpression *call = &program->expressions[expression_index];
        MinicFunctionId current_id;
        MinicFunctionId source_id;
        MinicFunctionId variant_id = MINIC_FUNCTION_INVALID;
        MinicFunction current;
        MinicFunction source;
        bool integer_known[MINIC_MAX_FUNCTION_PARAMETERS];
        uint64_t integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];
        bool symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicExpressionId symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];
        bool adds_symbol = false;
        size_t i;
        size_t candidate_index;

        if (call->kind != MINIC_EXPRESSION_CALL ||
            call->value.call.function_id == MINIC_FUNCTION_INVALID) {
            continue;
        }
        current_id = call->value.call.function_id;
        if (current_id >= program->function_count) {
            continue;
        }
        current = program->functions[current_id];
        source_id = current.is_integer_specialization
                        ? current.specialization_source
                        : current_id;
        if (source_id >= program->function_count) {
            continue;
        }
        source = program->functions[source_id];
        if (!source.is_defined || !source.is_internal || !source.is_inline ||
            source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID ||
            source.parameter_count == 0U ||
            source.parameter_count != call->value.call.argument_count) {
            continue;
        }
        (void)memset(integer_known, 0, sizeof(integer_known));
        (void)memset(integer_bits, 0, sizeof(integer_bits));
        (void)memset(symbolic_known, 0, sizeof(symbolic_known));
        for (i = 0U; i < MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
            symbolic_expression[i] = MINIC_EXPRESSION_INVALID;
        }
        if (current.is_integer_specialization) {
            for (i = 0U; i < source.parameter_count; ++i) {
                integer_known[i] = current.specialization_integer_known[i];
                integer_bits[i] = current.specialization_integer_bits[i];
                symbolic_known[i] = current.specialization_symbolic_known[i];
                symbolic_expression[i] =
                    current.specialization_symbolic_expression[i];
            }
        }
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicExpressionId argument_id = call->value.call.arguments[i];
            if (!minic_type_is_pointer(source.parameter_types[i]) ||
                !minic_inline_symbolic_static_expression(
                    program, target, argument_id)) {
                continue;
            }
            if (!symbolic_known[i] || symbolic_expression[i] != argument_id) {
                symbolic_known[i] = true;
                symbolic_expression[i] = argument_id;
                adds_symbol = true;
            }
        }
        if (!adds_symbol) {
            continue;
        }
        for (candidate_index = 0U; candidate_index < program->function_count;
             ++candidate_index) {
            if (minic_inline_symbolic_variant_matches(
                    &program->functions[candidate_index],
                    source_id,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    source.parameter_count)) {
                variant_id = candidate_index;
                break;
            }
        }
        if (variant_id == MINIC_FUNCTION_INVALID) {
            if (program->function_count >= SIZE_MAX - 1U ||
                !minic_add_inline_symbolic_specialization(
                    program,
                    source_id,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    &variant_id)) {
                return false;
            }
            initial_clone_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = variant_id;
    }

    {
        size_t caller_index;
        for (caller_index = original_function_count;
             caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            size_t nested_expression_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;
                 ++nested_expression_index) {
                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];
                MinicFunctionId current_id;
                MinicFunctionId source_id;
                MinicFunctionId variant_id = MINIC_FUNCTION_INVALID;
                MinicFunction current;
                MinicFunction source;
                bool integer_known[MINIC_MAX_FUNCTION_PARAMETERS];
                uint64_t integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];
                bool symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];
                MinicExpressionId symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];
                bool used_caller_fact = false;
                bool adds_symbol = false;
                size_t i;
                size_t candidate_index;

                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                current_id = nested->value.call.function_id;
                if (current_id >= program->function_count) {
                    continue;
                }
                current = program->functions[current_id];
                source_id = current.is_integer_specialization
                                ? current.specialization_source
                                : current_id;
                if (source_id >= program->function_count) {
                    continue;
                }
                source = program->functions[source_id];
                if (!source.is_defined || !source.is_internal || !source.is_inline ||
                    source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID ||
                    source.parameter_count == 0U ||
                    source.parameter_count != nested->value.call.argument_count) {
                    continue;
                }
                (void)memset(integer_known, 0, sizeof(integer_known));
                (void)memset(integer_bits, 0, sizeof(integer_bits));
                (void)memset(symbolic_known, 0, sizeof(symbolic_known));
                for (i = 0U; i < MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
                    symbolic_expression[i] = MINIC_EXPRESSION_INVALID;
                }
                if (current.is_integer_specialization) {
                    for (i = 0U; i < source.parameter_count; ++i) {
                        integer_known[i] = current.specialization_integer_known[i];
                        integer_bits[i] = current.specialization_integer_bits[i];
                        symbolic_known[i] = current.specialization_symbolic_known[i];
                        symbolic_expression[i] =
                            current.specialization_symbolic_expression[i];
                    }
                }
                for (i = 0U; i < source.parameter_count; ++i) {
                    MinicExpressionId resolved_expression;
                    bool argument_used_caller_fact = false;
                    if (!minic_type_is_pointer(source.parameter_types[i]) ||
                        !minic_inline_symbolic_argument_expression(
                            program,
                            target,
                            &caller,
                            nested->value.call.arguments[i],
                            &resolved_expression,
                            &argument_used_caller_fact,
                            0U)) {
                        continue;
                    }
                    used_caller_fact =
                        used_caller_fact || argument_used_caller_fact;
                    if (!symbolic_known[i] ||
                        symbolic_expression[i] != resolved_expression) {
                        symbolic_known[i] = true;
                        symbolic_expression[i] = resolved_expression;
                        adds_symbol = true;
                    }
                }
                if (!used_caller_fact || !adds_symbol) {
                    continue;
                }
                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {
                    if (minic_inline_symbolic_variant_matches(
                            &program->functions[candidate_index],
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            source.parameter_count)) {
                        variant_id = candidate_index;
                        break;
                    }
                }
                if (variant_id == MINIC_FUNCTION_INVALID) {
                    if (!minic_add_inline_symbolic_specialization(
                            program,
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            &variant_id)) {
                        return false;
                    }
                    transitive_clone_count += 1U;
                }
            }
        }
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_SYMBOLIC_SPECIALIZATION_V0 direct=%zu transitive=%zu functions=%zu\n",
                      initial_clone_count,
                      transitive_clone_count,
                      program->function_count);
    }
    return true;
}

static bool minic_inline_integer_source_has_residual_reference(
    const MinicC0Program *program, MinicFunctionId source_id) {
    size_t expression_index;
    size_t function_index;
    size_t object_index;

    if (program == NULL || source_id >= program->function_count) {
        return true;
    }
    if (program->entry_function == source_id || program->functions[source_id].force_emit) {
        return true;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        if (program->functions[function_index].alias_target == source_id) {
            return true;
        }
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                (MinicFunctionId)relocation->target_id == source_id) {
                return true;
            }
        }
    }
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *expression = &program->expressions[expression_index];
        if ((expression->kind == MINIC_EXPRESSION_FUNCTION &&
             expression->value.function_id == source_id) ||
            (expression->kind == MINIC_EXPRESSION_CALL &&
             expression->value.call.function_id == source_id)) {
            return true;
        }
    }
    return false;
}

static bool minic_finalize_inline_integer_specialization_references(MinicC0Program *program) {
    size_t function_index;

    if (program == NULL) {
        return false;
    }
    /* Generic reachability ran before cloning, while FunctionBody ownership was still
     * one-to-one. Preserve that result for clones first, then retire a source only when
     * specialization rewrote every syntactic/function-address reference to it. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization) {
            MinicFunctionId source_id = function->specialization_source;
            if (source_id >= program->function_count ||
                program->functions[source_id].is_integer_specialization) {
                return false;
            }
            function->is_referenced = program->functions[source_id].is_referenced;
        }
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization) {
            MinicFunctionId source_id = function->specialization_source;
            MinicFunction *source = &program->functions[source_id];
            if (source->is_internal && source->is_inline && source->is_referenced &&
                !minic_inline_integer_source_has_residual_reference(program, source_id)) {
                source->is_referenced = false;
            }
        }
    }
    return true;
}


static MinicFunctionId minic_core_named_function_id(
    const MinicC0Program *program, const char *name, size_t name_length) {
    size_t function_index;

    if (program == NULL || name == NULL || name_length == 0U) {
        return MINIC_FUNCTION_INVALID;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        const char *symbol_name = minic_c0_function_symbol_name(function);
        size_t symbol_name_length;

        if (symbol_name == NULL) {
            continue;
        }
        symbol_name_length = function->assembler_name != NULL
                                 ? function->assembler_name_length
                                 : function->name_length;
        if (symbol_name_length == name_length &&
            memcmp(symbol_name, name, name_length) == 0) {
            return function_index;
        }
    }
    return MINIC_FUNCTION_INVALID;
}

static bool minic_enqueue_core_named_function(
    const MinicC0Program *program,
    const char *name,
    size_t name_length,
    bool *reachable,
    MinicFunctionId *queue,
    size_t *queue_count) {
    MinicFunctionId target;

    if (program == NULL || reachable == NULL || queue == NULL || queue_count == NULL) {
        return false;
    }
    target = minic_core_named_function_id(program, name, name_length);
    if (target == MINIC_FUNCTION_INVALID || target >= program->function_count ||
        !program->functions[target].is_defined || reachable[target]) {
        return true;
    }
    if (*queue_count >= program->function_count) {
        return false;
    }
    reachable[target] = true;
    queue[(*queue_count)++] = target;
    return true;
}

static bool minic_enqueue_core_block(MinicCoreBlockId block_id,
                                     size_t block_count,
                                     bool *reachable,
                                     MinicCoreBlockId *queue,
                                     size_t *queue_count) {
    if (reachable == NULL || queue == NULL || queue_count == NULL ||
        block_id >= block_count) {
        return false;
    }
    if (reachable[block_id]) {
        return true;
    }
    if (*queue_count >= block_count) {
        return false;
    }
    reachable[block_id] = true;
    queue[(*queue_count)++] = block_id;
    return true;
}

static bool minic_walk_reachable_core_edges(const MinicC0Program *program,
                                            const MinicCoreFunction *core,
                                            bool *function_reachable,
                                            MinicFunctionId *function_queue,
                                            size_t *function_queue_count) {
    bool *block_reachable = NULL;
    MinicCoreBlockId *block_queue = NULL;
    size_t block_queue_count = 0U;
    size_t block_queue_cursor = 0U;
    bool success = false;

    if (program == NULL || core == NULL || function_reachable == NULL ||
        function_queue == NULL || function_queue_count == NULL) {
        return false;
    }
    if (core->block_count == 0U) {
        return true;
    }
    if (core->blocks == NULL ||
        (core->instruction_count != 0U && core->instructions == NULL) ||
        core->entry_block >= core->block_count) {
        return false;
    }
    block_reachable = (bool *)calloc(core->block_count, sizeof(*block_reachable));
    block_queue = (MinicCoreBlockId *)malloc(core->block_count * sizeof(*block_queue));
    if (block_reachable == NULL || block_queue == NULL ||
        !minic_enqueue_core_block(core->entry_block,
                                 core->block_count,
                                 block_reachable,
                                 block_queue,
                                 &block_queue_count)) {
        goto done;
    }
    /* M180_CORE_LABEL_REENTRY_ROOTS: a source C label is a legal re-entry root
     * for goto/asm-goto. Backend emission preserves such blocks, so final Core
     * reachability must start from them as well or it can prune a specialization
     * clone that is still called after label re-entry. */
    {
        size_t root_block;
        for (root_block = 0U; root_block < core->block_count; ++root_block) {
            if (core->blocks[root_block].source_label_id != SIZE_MAX &&
                !minic_enqueue_core_block((MinicCoreBlockId)root_block,
                                         core->block_count,
                                         block_reachable,
                                         block_queue,
                                         &block_queue_count)) {
                goto done;
            }
        }
    }

    while (block_queue_cursor < block_queue_count) {
        MinicCoreBlockId block_id = block_queue[block_queue_cursor++];
        const MinicCoreBlock *block = &core->blocks[block_id];
        size_t block_instruction_index;

        /* A valid block may contain only a terminator and therefore own no
         * instruction-id array.  Require storage only when the block actually
         * contains instructions. */
        if (block->instruction_count != 0U && block->instructions == NULL) {
            goto done;
        }

        for (block_instruction_index = 0U;
             block_instruction_index < block->instruction_count;
             ++block_instruction_index) {
            MinicCoreInstructionId instruction_id =
                block->instructions[block_instruction_index];
            const MinicCoreInstruction *instruction;

            if (instruction_id >= core->instruction_count) {
                goto done;
            }
            instruction = &core->instructions[instruction_id];
            if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
                MinicCoreCalleeId callee_id = instruction->value.call.callee_id;
                const MinicCoreCallee *callee;
                if (callee_id >= core->callee_count) {
                    goto done;
                }
                callee = &core->callees[callee_id];
                if (!minic_enqueue_core_named_function(program,
                                                       callee->name,
                                                       callee->name_length,
                                                       function_reachable,
                                                       function_queue,
                                                       function_queue_count)) {
                    goto done;
                }
            } else if (instruction->kind == MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS) {
                MinicCoreFunctionSymbolId symbol_id = instruction->value.function_symbol_id;
                const MinicCoreFunctionSymbol *symbol;
                if (symbol_id >= core->function_symbol_count) {
                    goto done;
                }
                symbol = &core->function_symbols[symbol_id];
                if (!minic_enqueue_core_named_function(program,
                                                       symbol->name,
                                                       symbol->name_length,
                                                       function_reachable,
                                                       function_queue,
                                                       function_queue_count)) {
                    goto done;
                }
            }
        }

        if (!block->has_terminator) {
            continue;
        }
        switch (block->terminator.kind) {
            case MINIC_CORE_TERMINATOR_RETURN:
            case MINIC_CORE_TERMINATOR_UNREACHABLE:
                break;
            case MINIC_CORE_TERMINATOR_BRANCH:
                if (!minic_enqueue_core_block(block->terminator.branch_target,
                                             core->block_count,
                                             block_reachable,
                                             block_queue,
                                             &block_queue_count)) {
                    goto done;
                }
                break;
            case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
                if (!minic_enqueue_core_block(block->terminator.conditional.when_true,
                                             core->block_count,
                                             block_reachable,
                                             block_queue,
                                             &block_queue_count) ||
                    !minic_enqueue_core_block(block->terminator.conditional.when_false,
                                             core->block_count,
                                             block_reachable,
                                             block_queue,
                                             &block_queue_count)) {
                    goto done;
                }
                break;
            case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH: {
                size_t target_block;
                /* The Core value feeding a computed goto can select any
                 * address-taken label. Conservatively retaining all blocks is
                 * correct; ordinary direct/conditional CFG stays precise. */
                for (target_block = 0U; target_block < core->block_count; ++target_block) {
                    if (!minic_enqueue_core_block((MinicCoreBlockId)target_block,
                                                 core->block_count,
                                                 block_reachable,
                                                 block_queue,
                                                 &block_queue_count)) {
                        goto done;
                    }
                }
                break;
            }
            default:
                goto done;
        }
    }
    success = true;

done:
    free(block_queue);
    free(block_reachable);
    return success;
}


static bool minic_inline_source_has_noncore_root(
    const MinicC0Program *program, MinicFunctionId source_id) {
    size_t function_index;
    size_t object_index;

    if (program == NULL || source_id >= program->function_count) {
        return true;
    }
    if (program->entry_function == source_id || program->functions[source_id].force_emit) {
        return true;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        if (program->functions[function_index].alias_target == source_id) {
            return true;
        }
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                (MinicFunctionId)relocation->target_id == source_id) {
                return true;
            }
        }
    }
    return false;
}

static bool minic_inline_source_is_core_reachable_only(
    const MinicC0Program *program, MinicFunctionId source_id) {
    const MinicFunction *source;

    if (program == NULL || source_id >= program->function_count) {
        return false;
    }
    source = &program->functions[source_id];
    /* Internal function bodies are implementation details, not emission roots.
     * Parser-level is_referenced can stay set after every executable call was
     * rewritten, specialized, or folded away.  Let the lowered Core graph pull
     * the body back in when a reachable CALL or FUNCTION_ADDRESS still needs
     * it; preserve only entry/force-emit/alias/global-relocation roots that live
     * outside that graph.  This applies to ordinary static functions as well as
     * static inline helpers so dead CONFIG-disabled helper subgraphs disappear. */
    return source->is_defined && source->is_internal &&
           !minic_inline_source_has_noncore_root(program, source_id);
}

static bool minic_prune_inline_specializations_from_core(
    MinicC0Program *program, const MinicCoreFunctionSet *set) {
    bool *reachable = NULL;
    bool *processed = NULL;
    MinicFunctionId *queue = NULL;
    size_t queue_count = 0U;
    size_t queue_cursor = 0U;
    size_t function_index;
    size_t kept = 0U;
    size_t pruned = 0U;
    bool success = false;

    if (program == NULL || set == NULL || set->function_count != program->function_count ||
        (set->function_count != 0U &&
         (set->functions == NULL || set->statuses == NULL))) {
        return false;
    }
    reachable = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                               sizeof(*reachable));
    processed = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                               sizeof(*processed));
    queue = (MinicFunctionId *)malloc(
        (program->function_count == 0U ? 1U : program->function_count) * sizeof(*queue));
    if (reachable == NULL || processed == NULL || queue == NULL) {
        goto done;
    }

    /* Preserve the established root policy for ordinary functions. Synthetic
     * specialization clones are never roots: they survive only through an
     * actual instruction in a CFG-reachable lowered Core block. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization || !function->is_defined ||
            (function->is_internal && !function->is_referenced)) {
            continue;
        }
        /* Parser-level references are only a discovery hint for internal
         * functions.  The final lowered Core graph is the authoritative
         * executable reachability source; non-Core roots stay conservative. */
        if (minic_inline_source_is_core_reachable_only(
                program, (MinicFunctionId)function_index)) {
            continue;
        }
        reachable[function_index] = true;
        queue[queue_count++] = function_index;
    }

    while (queue_cursor < queue_count) {
        MinicFunctionId caller_id = queue[queue_cursor++];

        if (caller_id >= set->function_count || processed[caller_id]) {
            continue;
        }
        processed[caller_id] = true;
        if (set->statuses[caller_id] != MINIC_CORE_LOWER_OK) {
            continue;
        }
        if (!minic_walk_reachable_core_edges(program,
                                             &set->functions[caller_id],
                                             reachable,
                                             queue,
                                             &queue_count)) {
            goto done;
        }
    }

    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (!function->is_integer_specialization) {
            if (minic_inline_source_is_core_reachable_only(
                    program, (MinicFunctionId)function_index)) {
                function->is_referenced = reachable[function_index];
            }
            continue;
        }
        function->is_referenced = reachable[function_index];
        if (function->is_referenced) {
            kept += 1U;
        } else {
            pruned += 1U;
        }
    }
    if (kept != 0U || pruned != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_SPECIALIZATION_CORE_REACHABILITY_V0 kept=%zu pruned=%zu\n",
                      kept,
                      pruned);
    }
    success = true;

done:
    free(queue);
    free(processed);
    free(reachable);
    return success;
}

static bool minic_validate_core_functions(const char *input_path,
                                          const MinicC0Program *program,
                                          const MinicCoreFunctionSet *set,
                                          MinicDiagnostic *diagnostic) {
    size_t function_index;

    if (program == NULL || set == NULL) {
        return false;
    }
    if (set->function_count != program->function_count ||
        (set->function_count != 0U &&
         (set->functions == NULL || set->statuses == NULL))) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "Core IR functions do not match source program");
        return false;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function;
        MinicCoreLowerStatus status;

        function = minic_c0_program_function(program, function_index);
        if (function == NULL) {
            minic_set_diagnostic(
                diagnostic, input_path, 1U, 1U, "Core lowering cannot access function");
            return false;
        }
        if (!function->is_defined) {
            continue;
        }
        if (function->is_internal && !function->is_referenced) {
            continue;
        }
        status = set->statuses[function_index];
        if (status == MINIC_CORE_LOWER_OK &&
            !minic_core_function_verify(&set->functions[function_index])) {
            status = MINIC_CORE_LOWER_ERROR;
        }
        if (status == MINIC_CORE_LOWER_OK) {
            continue;
        }
        if (status == MINIC_CORE_LOWER_UNSUPPORTED) {
            minic_set_diagnostic(diagnostic,
                                 input_path,
                                 1U,
                                 1U,
                                 "Core IR does not yet support function '%s'",
                                 function->name);
            return false;
        }
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "Core IR lowering failed for function '%s'",
                             function->name);
        return false;
    }
    return true;
}

static bool
minic_read_file(const char *path, MinicSourceBuffer *buffer, MinicDiagnostic *diagnostic) {
    FILE *file;
    long end_position;
    size_t size;
    char *data;

    file = fopen(path, "rb");
    if (file == NULL) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "cannot open input: %s", strerror(errno));
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (end_position = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "cannot seek input: %s", strerror(errno));
        (void)fclose(file);
        return false;
    }
    if ((unsigned long)end_position > (unsigned long)(SIZE_MAX - 1U)) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "input is too large");
        (void)fclose(file);
        return false;
    }

    size = (size_t)end_position;
    data = (char *)malloc(size + 1U);
    if (data == NULL) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "out of memory while reading input");
        (void)fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "cannot read input");
        free(data);
        (void)fclose(file);
        return false;
    }
    data[size] = '\0';
    if (fclose(file) != 0) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "cannot close input: %s", strerror(errno));
        free(data);
        return false;
    }

    buffer->data = data;
    buffer->size = size;
    return true;
}

static void minic_set_ast_verify_diagnostic(const char *input_path,
                                            const char *form_name,
                                            const MinicC0AstVerifyFailure *failure,
                                            MinicDiagnostic *diagnostic) {
    const char *stage_name;
    const char *reason;

    stage_name = failure == NULL ? "unknown" : minic_c0_ast_verify_stage_name(failure->stage);
    reason = failure == NULL || failure->reason == NULL ? "contract violation" : failure->reason;
    if (failure != NULL && failure->index != MINIC_C0_AST_VERIFY_INDEX_NONE) {
        if (failure->subindex != MINIC_C0_AST_VERIFY_INDEX_NONE) {
            minic_set_diagnostic(diagnostic,
                                 input_path,
                                 1U,
                                 1U,
                                 "%s AST contract failed at %s[%zu:%zu]: %s",
                                 form_name,
                                 stage_name,
                                 failure->index,
                                 failure->subindex,
                                 reason);
        } else {
            minic_set_diagnostic(diagnostic,
                                 input_path,
                                 1U,
                                 1U,
                                 "%s AST contract failed at %s[%zu]: %s",
                                 form_name,
                                 stage_name,
                                 failure->index,
                                 reason);
        }
    } else {
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "%s AST contract failed at %s: %s",
                             form_name,
                             stage_name,
                             reason);
    }
}

int minic_compile_preprocessed_file(const char *input_path,
                                    const char *output_path,
                                    MinicDiagnostic *diagnostic) {
    MinicSourceBuffer buffer;
    MinicC0Program program;
    MinicCoreFunctionSet core_set;
    const MinicTargetInfo *target_info;
    MinicC0AstVerifyFailure verify_failure;
    bool success;

    if (input_path == NULL || output_path == NULL) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U, "input and output paths are required");
        return 1;
    }
    if (diagnostic != NULL) {
        diagnostic->path = input_path;
        diagnostic->line = 1U;
        diagnostic->column = 1U;
        diagnostic->message[0] = '\0';
    }
    buffer.data = NULL;
    buffer.size = 0U;
    if (!minic_read_file(input_path, &buffer, diagnostic)) {
        return 1;
    }

    minic_c0_program_initialize(&program);
    minic_core_function_set_initialize(&core_set);
    target_info = minic_default_target_info();
    minic_bootstrap_trace_stage(input_path, "parse", "begin", 0U);
    success = minic_parse_c0_program(input_path, buffer.data, buffer.size, &program, diagnostic);
    minic_bootstrap_trace_stage(input_path, "parse", success ? "end-ok" : "end-fail",
                                program.function_count);
    if (success && !minic_c0_program_verify_target_detailed(
                       &program, MINIC_C0_AST_PARSED, target_info, &verify_failure)) {
        minic_set_ast_verify_diagnostic(input_path, "parsed", &verify_failure, diagnostic);
        success = false;
    }
    if (success && !minic_c0_program_validate_function_body_ownership(&program)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "parsed FunctionBody ownership is invalid");
        success = false;
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "normalize", "begin", program.function_count);
    }
    if (success && !minic_c0_program_normalize_casts(&program)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U, "cannot normalize cast expressions");
        success = false;
    }
    if (minic_bootstrap_trace_enabled()) {
        minic_bootstrap_trace_stage(input_path,
                                    "normalize",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success && !minic_c0_program_verify_target_detailed(
                       &program, MINIC_C0_AST_NORMALIZED, target_info, &verify_failure)) {
        minic_set_ast_verify_diagnostic(input_path, "normalized", &verify_failure, diagnostic);
        success = false;
    }
    if (success && !minic_c0_program_validate_function_body_ownership(&program)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "normalized FunctionBody ownership is invalid");
        success = false;
    }
    if (success && !minic_c0_program_recompute_inline_emission_references(&program)) {
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "cannot compute inline emission reachability");
        success = false;
    }
    if (success && !minic_specialize_inline_integer_calls(&program, target_info)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                             "cannot specialize inline integer call sites");
        success = false;
    }
    if (success && !minic_specialize_inline_symbolic_calls(&program, target_info)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                             "cannot specialize inline symbolic address call sites");
        success = false;
    }
    if (success && !minic_finalize_inline_integer_specialization_references(&program)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                             "cannot finalize inline specialization reachability");
        success = false;
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "core-set", "begin", program.function_count);
    }
    if (success && !minic_prepare_core_function_set(&program, target_info, &core_set)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "cannot retain Core IR lowering results");
        success = false;
    }
    if (minic_bootstrap_trace_enabled()) {
        minic_bootstrap_trace_stage(input_path,
                                    "core-set",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success && !minic_prune_inline_specializations_from_core(&program, &core_set)) {
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "cannot prune inline specialization Core reachability");
        success = false;
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "core-validate", "begin", program.function_count);
        success =
            minic_validate_core_functions(input_path, &program, &core_set, diagnostic);
        minic_bootstrap_trace_stage(input_path,
                                    "core-validate",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "layout", "begin", program.function_count);
        success = minic_riscv64_layout_program(input_path, &program, diagnostic);
        minic_bootstrap_trace_stage(input_path,
                                    "layout",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "codegen", "begin", program.function_count);
        success =
            minic_riscv64_write_c0_program_with_core_functions(output_path,
                                                               &program,
                                                               core_set.functions,
                                                               core_set.function_count,
                                                               diagnostic);
        minic_bootstrap_trace_stage(input_path,
                                    "codegen",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }

    minic_core_function_set_destroy(&core_set);
    minic_c0_program_destroy(&program);
    free(buffer.data);
    return success ? 0 : 1;
}
