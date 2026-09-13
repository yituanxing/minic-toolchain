#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


# Carry temporary specialization metadata on synthetic MinicFunction clones.
# This is a CI/product-prototype seam: ordinary parsed functions leave all
# `known` entries false and specialization_source invalid.
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
    "    bool is_defined;\n",
)
replace_once(
    "src/frontend/ast.c",
    "    (void)memset(&function, 0, sizeof(function));\n"
    "    function.name = minic_copy_name(name, name_length);\n",
    "    (void)memset(&function, 0, sizeof(function));\n"
    "    function.specialization_source = MINIC_FUNCTION_INVALID;\n"
    "    function.name = minic_copy_name(name, name_length);\n",
)

# At Core ingress a synthetic specialization still has the original ABI
# signature, but a proven integer parameter is materialized as a constant and
# stored into the normal parameter-local object.  Everything after ingress can
# therefore reuse the existing local-fact/CFG machinery unchanged.
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
    "            if (context->source_function->specialization_source != MINIC_FUNCTION_INVALID &&\n"
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

# Compiler-side bounded direct-call multi-versioning.  Reuse the original body
# and locals only after AST/body verification has completed.  Calls are then
# redirected to a synthetic internal function carrying the proven integer
# argument tuple.  The later inline-emission reachability recomputation sees the
# rewritten function_ids and suppresses an unneeded generic static-inline body.
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
    size_t parameter_index;

    if (candidate == NULL || known == NULL || bits == NULL ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count) {
        return false;
    }
    for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
        if (candidate->specialization_integer_known[parameter_index] != known[parameter_index] ||
            (known[parameter_index] &&
             candidate->specialization_integer_bits[parameter_index] != bits[parameter_index])) {
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
    size_t parameter_index;

    if (program == NULL || known == NULL || bits == NULL || specialized_id == NULL ||
        source_id >= program->function_count) {
        return false;
    }
    source = program->functions[source_id];
    if (!source.is_defined || !source.is_internal || !source.is_inline || source.is_variadic ||
        source.alias_target != MINIC_FUNCTION_INVALID ||
        source.parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
        return false;
    }
    name_length = snprintf(name,
                           sizeof(name),
                           "__minic_inline_spec_%zu_%zu",
                           (size_t)source_id,
                           ordinal);
    if (name_length <= 0 || (size_t)name_length >= sizeof(name) ||
        !minic_c0_program_add_function(program,
                                       name,
                                       (size_t)name_length,
                                       source.local_begin,
                                       source.local_count,
                                       source.body_block,
                                       specialized_id) ||
        !minic_c0_program_set_function_signature(program,
                                                 *specialized_id,
                                                 source.return_type,
                                                 source.parameter_types,
                                                 source.parameter_count) ||
        !minic_c0_program_set_function_internal(program, *specialized_id, true) ||
        !minic_c0_program_set_function_inline(program, *specialized_id, true) ||
        (source.is_noreturn &&
         !minic_c0_program_set_function_noreturn(program, *specialized_id, true)) ||
        (source.section_name != NULL &&
         !minic_c0_program_set_function_section(program,
                                                *specialized_id,
                                                source.section_name,
                                                source.section_name_length))) {
        return false;
    }
    specialized = &program->functions[*specialized_id];
    specialized->visibility = source.visibility;
    specialized->specialization_source = source_id;
    for (parameter_index = 0U; parameter_index < source.parameter_count; ++parameter_index) {
        specialized->specialization_integer_known[parameter_index] = known[parameter_index];
        specialized->specialization_integer_bits[parameter_index] = bits[parameter_index];
    }
    return true;
}

static bool minic_specialize_inline_integer_calls(MinicC0Program *program,
                                                  const MinicTargetInfo *target) {
    size_t expression_index;
    size_t original_function_count;
    size_t specialization_count;

    if (program == NULL || target == NULL) {
        return false;
    }
    original_function_count = program->function_count;
    specialization_count = 0U;
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        MinicExpression *expression;
        MinicFunctionId source_id;
        MinicFunctionId specialized_id;
        const MinicFunction *source;
        bool known[MINIC_MAX_FUNCTION_PARAMETERS];
        uint64_t bits[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t parameter_index;
        size_t candidate_index;
        bool has_integer_constant;

        expression = &program->expressions[expression_index];
        if (expression->kind != MINIC_EXPRESSION_CALL ||
            expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            continue;
        }
        source_id = expression->value.call.function_id;
        if (source_id >= original_function_count) {
            continue;
        }
        source = &program->functions[source_id];
        if (!source->is_defined || !source->is_internal || !source->is_inline ||
            source->is_variadic || source->alias_target != MINIC_FUNCTION_INVALID ||
            source->parameter_count == 0U ||
            source->parameter_count != expression->value.call.argument_count ||
            source->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
            continue;
        }
        (void)memset(known, 0, sizeof(known));
        (void)memset(bits, 0, sizeof(bits));
        has_integer_constant = false;
        for (parameter_index = 0U; parameter_index < source->parameter_count;
             ++parameter_index) {
            MinicConstValue argument_value;
            MinicConstValue converted_value;

            if (!minic_type_is_integer(source->parameter_types[parameter_index]) ||
                !minic_const_eval_integer(program,
                                          target,
                                          expression->value.call.arguments[parameter_index],
                                          &argument_value) ||
                !minic_const_value_convert_integer(program,
                                                   target,
                                                   &argument_value,
                                                   source->parameter_types[parameter_index],
                                                   &converted_value)) {
                continue;
            }
            known[parameter_index] = true;
            bits[parameter_index] = converted_value.bits;
            has_integer_constant = true;
        }
        if (!has_integer_constant) {
            continue;
        }

        specialized_id = MINIC_FUNCTION_INVALID;
        for (candidate_index = original_function_count;
             candidate_index < program->function_count;
             ++candidate_index) {
            if (minic_inline_integer_specialization_matches(&program->functions[candidate_index],
                                                            source_id,
                                                            known,
                                                            bits,
                                                            source->parameter_count)) {
                specialized_id = candidate_index;
                break;
            }
        }
        if (specialized_id == MINIC_FUNCTION_INVALID) {
            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT ||
                !minic_add_inline_integer_specialization(program,
                                                         source_id,
                                                         known,
                                                         bits,
                                                         specialization_count,
                                                         &specialized_id)) {
                return false;
            }
            specialization_count += 1U;
        }
        expression = &program->expressions[expression_index];
        expression->value.call.function_id = specialized_id;
    }
    if (specialization_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_INTEGER_SPECIALIZATION_V0 clones=%zu functions=%zu\n",
                      specialization_count,
                      program->function_count);
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
    "        minic_set_diagnostic(diagnostic,\n"
    "                             input_path,\n"
    "                             1U,\n"
    "                             1U,\n"
    "                             \"cannot specialize inline integer call sites\");\n"
    "        success = false;\n"
    "    }\n"
    "    if (success && !minic_c0_program_recompute_inline_emission_references(&program)) {\n",
)

print("MINIC_INLINE_INTEGER_SPECIALIZATION_V0=APPLIED")
