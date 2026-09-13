#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    "    MinicBlockId body_block;\n"
    "    MinicFunctionId alias_target;\n"
    "    bool is_defined;\n",
    "    MinicBlockId body_block;\n"
    "    MinicFunctionId alias_target;\n"
    "    MinicFunctionId specialization_source;\n"
    "    uint64_t specialization_integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool specialization_integer_known[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool is_integer_specialization;\n"
    "    bool is_defined;\n",
)

replace_once(
    "src/core/core_lower.c",
    "            (void)memset(&instruction, 0, sizeof(instruction));\n"
    "            instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;\n"
    "            instruction.span = parameter->name_span;\n"
    "            instruction.type = parameter_value_type;\n"
    "            instruction.result = MINIC_CORE_VALUE_INVALID;\n"
    "            instruction.value.parameter_index = parameter_index;\n"
    "            if (!minic_core_function_append_value_instruction(\n"
    "                    context->function, context->block_id, &instruction, &parameter_value)) {\n"
    "                return MINIC_CORE_LOWER_ERROR;\n"
    "            }\n",
    "            (void)memset(&instruction, 0, sizeof(instruction));\n"
    "            if (context->source_function->is_integer_specialization &&\n"
    "                context->source_function->specialization_integer_known[parameter_index] &&\n"
    "                minic_type_is_integer(parameter_value_type)) {\n"
    "                uint64_t specialized_bits =\n"
    "                    context->source_function->specialization_integer_bits[parameter_index];\n"
    "                instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;\n"
    "                (void)memcpy(&instruction.value.integer_value,\n"
    "                             &specialized_bits,\n"
    "                             sizeof(specialized_bits));\n"
    "            } else {\n"
    "                instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;\n"
    "                instruction.value.parameter_index = parameter_index;\n"
    "            }\n"
    "            instruction.span = parameter->name_span;\n"
    "            instruction.type = parameter_value_type;\n"
    "            instruction.result = MINIC_CORE_VALUE_INVALID;\n"
    "            if (!minic_core_function_append_value_instruction(\n"
    "                    context->function, context->block_id, &instruction, &parameter_value)) {\n"
    "                return MINIC_CORE_LOWER_ERROR;\n"
    "            }\n",
)

replace_once(
    "src/compiler/compiler.c",
    '#include "frontend/cast_normalization.h"\n#include "frontend/function_body.h"\n',
    '#include "frontend/cast_normalization.h"\n#include "frontend/const_eval.h"\n#include "frontend/function_body.h"\n',
)

helpers = r'''

#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 256U

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
            source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID ||
            source.parameter_count == 0U ||
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
            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT ||
                !minic_add_inline_integer_specialization(program, source_id, known, bits,
                                                         specialization_count,
                                                         &specialized_id)) {
                return false;
            }
            specialization_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = specialized_id;
    }
    if (specialization_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_INTEGER_SPECIALIZATION_V0 clones=%zu functions=%zu\n",
                      specialization_count, program->function_count);
    }
    return true;
}
'''

replace_once(
    "src/compiler/compiler.c",
    "static bool minic_validate_core_functions(const char *input_path,\n",
    helpers + "\nstatic bool minic_validate_core_functions(const char *input_path,\n",
)

replace_once(
    "src/compiler/compiler.c",
    "    if (success && !minic_c0_program_recompute_inline_emission_references(&program)) {\n",
    "    if (success && !minic_specialize_inline_integer_calls(&program, target_info)) {\n"
    "        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,\n"
    "                             \"cannot specialize inline integer call sites\");\n"
    "        success = false;\n"
    "    }\n"
    "    if (success && !minic_c0_program_recompute_inline_emission_references(&program)) {\n",
)

print("MINIC_INLINE_INTEGER_SPECIALIZATION_V0=APPLIED")
