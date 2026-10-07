#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

anchor = "static bool minic_inline_local_boolean_domain(\n"
if text.count(anchor) != 1:
    raise SystemExit(f"expected one local boolean domain helper, found {text.count(anchor)}")

cache_helpers = r'''
typedef struct MinicInlineLocalBooleanDomainCache {
    const MinicC0Program *program;
    bool *values;
    size_t local_count;
} MinicInlineLocalBooleanDomainCache;

static MinicInlineLocalBooleanDomainCache minic_inline_local_boolean_domain_cache;

static bool minic_inline_build_local_boolean_domain_cache(
    const MinicC0Program *program) {
    bool *values = NULL;
    bool *parameter = NULL;
    bool *saw_assignment = NULL;
    bool *invalid = NULL;
    size_t function_index;
    size_t statement_index;
    size_t expression_index;
    size_t asm_index;
    size_t local_index;

    if (program == NULL ||
        program->local_count > SIZE_MAX / sizeof(bool)) {
        return false;
    }
    values = (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                            sizeof(*values));
    parameter = (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                               sizeof(*parameter));
    saw_assignment =
        (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                       sizeof(*saw_assignment));
    invalid = (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                             sizeof(*invalid));
    if (values == NULL || parameter == NULL || saw_assignment == NULL ||
        invalid == NULL) {
        free(values);
        free(parameter);
        free(saw_assignment);
        free(invalid);
        return false;
    }

    for (function_index = 0U; function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        size_t parameter_index;
        if (!function->is_defined || function->local_begin > program->local_count ||
            function->parameter_count > program->local_count - function->local_begin) {
            continue;
        }
        for (parameter_index = 0U; parameter_index < function->parameter_count;
             ++parameter_index) {
            parameter[function->local_begin + parameter_index] = true;
        }
    }

    for (statement_index = 0U; statement_index < program->statement_count;
         ++statement_index) {
        const MinicStatement *statement = &program->statements[statement_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;
        if (!minic_inline_direct_local_target(
                program, statement->target_expression, &target_local) ||
            target_local >= program->local_count) {
            continue;
        }
        if (statement->kind != MINIC_STATEMENT_ASSIGN ||
            statement->expression == MINIC_EXPRESSION_INVALID ||
            !minic_inline_boolean_domain_value(
                program, statement->expression, 0U)) {
            invalid[target_local] = true;
        } else {
            saw_assignment[target_local] = true;
        }
    }

    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *expression = &program->expressions[expression_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;

        if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local < program->local_count) {
            invalid[target_local] = true;
            continue;
        }
        if (expression->kind == MINIC_EXPRESSION_UNARY &&
            (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local < program->local_count) {
            invalid[target_local] = true;
            continue;
        }
        if ((expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
             expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.binary.left, &target_local) &&
            target_local < program->local_count) {
            if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT ||
                !minic_inline_boolean_domain_value(
                    program, expression->value.binary.right, 0U)) {
                invalid[target_local] = true;
            } else {
                saw_assignment[target_local] = true;
            }
        }
    }

    for (asm_index = 0U; asm_index < program->inline_asm_count; ++asm_index) {
        const MinicInlineAsm *inline_asm = &program->inline_asms[asm_index];
        size_t output_index;
        for (output_index = 0U; output_index < inline_asm->output_count;
             ++output_index) {
            MinicLocalId output_local = MINIC_LOCAL_INVALID;
            if (minic_inline_direct_local_target(
                    program,
                    inline_asm->outputs[output_index].expression,
                    &output_local) &&
                output_local < program->local_count) {
                invalid[output_local] = true;
            }
        }
    }

    for (local_index = 0U; local_index < program->local_count; ++local_index) {
        values[local_index] =
            minic_type_is_integer(program->locals[local_index].type) &&
            !parameter[local_index] &&
            saw_assignment[local_index] &&
            !invalid[local_index];
    }

    free(parameter);
    free(saw_assignment);
    free(invalid);
    free(minic_inline_local_boolean_domain_cache.values);
    minic_inline_local_boolean_domain_cache.program = program;
    minic_inline_local_boolean_domain_cache.values = values;
    minic_inline_local_boolean_domain_cache.local_count = program->local_count;
    return true;
}

'''
text = text.replace(anchor, cache_helpers + anchor, 1)

start = text.find(anchor)
if start < 0:
    raise SystemExit("local boolean helper disappeared")
end_marker = "\n}\n\nstatic bool minic_inline_boolean_range_argument("
end = text.find(end_marker, start)
if end < 0:
    raise SystemExit("cannot find end of local boolean helper")

replacement = r'''static bool minic_inline_local_boolean_domain(
    const MinicC0Program *program,
    MinicLocalId local_id) {
    if (program == NULL || local_id >= program->local_count) {
        return false;
    }
    if (minic_inline_local_boolean_domain_cache.program != program ||
        minic_inline_local_boolean_domain_cache.local_count != program->local_count ||
        minic_inline_local_boolean_domain_cache.values == NULL) {
        if (!minic_inline_build_local_boolean_domain_cache(program)) {
            return false;
        }
    }
    return minic_inline_local_boolean_domain_cache.values[local_id];
}
'''
text = text[:start] + replacement + text[end + 2:]
p.write_text(text)
print("MINIC_INLINE_LOCAL_BOOLEAN_DOMAIN_INDEX_V0=APPLIED")
