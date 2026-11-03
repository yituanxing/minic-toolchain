#include "compiler/c_lower/core_ir_source_internal.h"
#include "compiler/c_lower/core_type_import.h"
#include "target/riscv64/core_codegen.h"
#include "target/source_policy/source_info.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static MinicSourceSpan empty_span(void) {
    MinicSourceSpan span;

    (void)memset(&span, 0, sizeof(span));
    return span;
}

static bool append_parameter_ingress(MinicCoreSourceBuilder *builder,
                                     MinicCoreBlockId block_id,
                                     size_t parameter_index,
                                     MinicType type,
                                     MinicCoreValueId *address_id) {
    MinicCoreInstruction instruction;
    MinicCoreObjectId object_id;
    MinicCoreValueId parameter_value;
    MinicType pointer_type;

    if (!minic_core_function_add_object(builder, empty_span(), type, &object_id) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    instruction.span = empty_span();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.parameter_index = parameter_index;
    if (!minic_core_function_append_source_value_instruction(
            builder, block_id, &instruction, type, &parameter_value)) {
        return false;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = empty_span();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    if (!minic_core_function_append_source_value_instruction(
            builder, block_id, &instruction, pointer_type, address_id)) {
        return false;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = empty_span();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = *address_id;
    instruction.value.store.stored_value = parameter_value;
    instruction.value.store.is_volatile = false;
    return minic_core_function_append_source_effect_instruction(
        builder, block_id, &instruction, minic_type_void());
}

static bool append_load(MinicCoreSourceBuilder *builder,
                        MinicCoreBlockId block_id,
                        MinicCoreValueId address_id,
                        MinicType type,
                        MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = empty_span();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address_id;
    instruction.value.load.is_volatile = false;
    return minic_core_function_append_source_value_instruction(
        builder, block_id, &instruction, type, value_id);
}

static bool append_constant(MinicCoreSourceBuilder *builder,
                            MinicCoreBlockId block_id,
                            long long value,
                            MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = empty_span();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = value;
    return minic_core_function_append_source_value_instruction(
        builder, block_id, &instruction, minic_type_int(), value_id);
}

static bool append_unary(MinicCoreSourceBuilder *builder,
                         MinicCoreBlockId block_id,
                         MinicCoreInstructionKind kind,
                         MinicCoreValueId operand,
                         MinicType type,
                         MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = kind;
    instruction.span = empty_span();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = operand;
    return minic_core_function_append_source_value_instruction(
        builder, block_id, &instruction, type, value_id);
}

static bool
set_return(MinicCoreFunction *function, MinicCoreBlockId block_id, MinicCoreValueId value_id) {
    MinicCoreTerminator terminator;

    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.span = empty_span();
    terminator.return_value = value_id;
    terminator.branch_target = MINIC_CORE_BLOCK_INVALID;
    return minic_core_function_set_terminator(function, block_id, &terminator);
}

static bool build_math(MinicCoreSourceBuilder *builder) {
    MinicCoreFunction *function = &builder->core;
    MinicCoreInstruction instruction;
    MinicCoreBlockId block_id;
    MinicCoreValueId x_address;
    MinicCoreValueId y_address;
    MinicCoreValueId x_value;
    MinicCoreValueId y_value;
    MinicCoreValueId sum;
    MinicCoreValueId negated;
    MinicCoreValueId zero;
    MinicType parameters[2];

    parameters[0] = minic_type_int();
    parameters[1] = minic_type_int();
    minic_core_source_builder_initialize(builder);
    if (!minic_core_function_set_signature(
            builder, "core_basic_math", 15U, minic_type_int(), parameters, 2U) ||
        !minic_core_function_add_block(function, &block_id) ||
        !append_parameter_ingress(builder, block_id, 0U, parameters[0], &x_address) ||
        !append_parameter_ingress(builder, block_id, 1U, parameters[1], &y_address) ||
        !append_load(builder, block_id, x_address, parameters[0], &x_value) ||
        !append_load(builder, block_id, y_address, parameters[1], &y_value)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    instruction.span = empty_span();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = x_value;
    instruction.value.binary.right = y_value;
    if (!minic_core_function_append_source_value_instruction(
            builder, block_id, &instruction, minic_type_int(), &sum) ||
        !append_unary(builder,
                      block_id,
                      MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,
                      sum,
                      minic_type_int(),
                      &negated) ||
        !append_unary(builder,
                      block_id,
                      MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,
                      negated,
                      minic_type_int(),
                      &zero) ||
        !set_return(function, block_id, zero)) {
        return false;
    }
    return minic_core_function_verify_source_ingress(builder);
}

static bool build_branch(MinicCoreSourceBuilder *builder) {
    MinicCoreFunction *function = &builder->core;
    MinicCoreBlockId entry;
    MinicCoreBlockId when_true;
    MinicCoreBlockId when_false;
    MinicCoreTerminator terminator;
    MinicCoreValueId x_address;
    MinicCoreValueId condition;
    MinicCoreValueId true_value;
    MinicCoreValueId false_source;
    MinicCoreValueId false_value;
    MinicType parameter;

    parameter = minic_type_int();
    minic_core_source_builder_initialize(builder);
    if (!minic_core_function_set_signature(
            builder, "core_branch", 11U, minic_type_int(), &parameter, 1U) ||
        !minic_core_function_add_block(function, &entry) ||
        !minic_core_function_add_block(function, &when_true) ||
        !minic_core_function_add_block(function, &when_false) ||
        !append_parameter_ingress(builder, entry, 0U, parameter, &x_address) ||
        !append_load(builder, entry, x_address, parameter, &condition)) {
        return false;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = empty_span();
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = when_true;
    terminator.conditional.when_false = when_false;
    if (!minic_core_function_set_terminator(function, entry, &terminator) ||
        !append_constant(builder, when_true, 7, &true_value) ||
        !set_return(function, when_true, true_value) ||
        !append_constant(builder, when_false, 3, &false_source) ||
        !append_unary(builder,
                      when_false,
                      MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,
                      false_source,
                      minic_type_int(),
                      &false_value) ||
        !set_return(function, when_false, false_value)) {
        return false;
    }
    return minic_core_function_verify_source_ingress(builder);
}

static bool build_ninth_parameter(MinicCoreSourceBuilder *builder) {
    MinicCoreFunction *function = &builder->core;
    MinicCoreBlockId entry;
    MinicCoreValueId addresses[9];
    MinicCoreValueId ninth;
    MinicType parameters[9];
    size_t index;

    for (index = 0U; index < 9U; ++index) {
        parameters[index] = minic_type_int();
    }
    minic_core_source_builder_initialize(builder);
    if (!minic_core_function_set_signature(
            builder, "core_ninth", 10U, minic_type_int(), parameters, 9U) ||
        !minic_core_function_add_block(function, &entry)) {
        return false;
    }
    for (index = 0U; index < 9U; ++index) {
        if (!append_parameter_ingress(
                builder, entry, index, parameters[index], &addresses[index])) {
            return false;
        }
    }
    if (!append_load(builder, entry, addresses[8], parameters[8], &ninth) ||
        !set_return(function, entry, ninth)) {
        return false;
    }
    return minic_core_function_verify_source_ingress(builder);
}

static bool import_source_type(void *user_data, MinicType source,
                               MinicCoreTypeId *result) {
    return minic_core_type_import((MinicCoreTypeImporter *)user_data, source, result);
}

static bool emit_one(FILE *file,
                     MinicCoreSourceBuilder *source_builder,
                     bool (*build)(MinicCoreSourceBuilder *),
                     const char *symbol_name) {
    const MinicTargetInfo *target =
        minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
    MinicC0Program empty_program = {0};
    MinicTargetInvocation invocation;
    MinicCoreTypeContext type_context;
    MinicCoreTypeImporter importer;
    MinicCoreFunction sealed;
    bool success = false;

    minic_core_type_context_initialize(&type_context);
    minic_core_function_initialize(&sealed);
    if (!minic_target_invocation_begin(&invocation, target) ||
        !minic_core_type_importer_initialize(&importer, &empty_program,
                                             &invocation, &type_context)) {
        minic_core_type_context_destroy(&type_context);
        minic_core_function_destroy(&sealed);
        return false;
    }
    if (build(source_builder) &&
        /* A C source draft is not an executable machine Core function. */
        !minic_riscv64_core_function_can_emit(&source_builder->core) &&
        minic_core_function_snapshot_types(source_builder, &type_context,
                                           import_source_type, &importer) &&
        minic_core_function_verify(&source_builder->core) &&
        minic_core_source_builder_take_core(source_builder, &sealed)) {
        success = minic_riscv64_core_function_can_emit(&sealed) &&
                  minic_riscv64_emit_core_function(file, &sealed, symbol_name);
    }
    minic_core_source_builder_destroy(source_builder);
    minic_core_function_destroy(&sealed);
    minic_core_type_importer_destroy(&importer);
    minic_core_type_context_destroy(&type_context);
    return success;
}

int main(int argc, char **argv) {
    MinicCoreSourceBuilder builder;
    FILE *file;
    bool success;

    if (argc != 2) {
        return 2;
    }
    file = fopen(argv[1], "w");
    if (file == NULL) {
        return 3;
    }
    success = emit_one(file, &builder, build_math, "core_basic_math") &&
              emit_one(file, &builder, build_branch, "core_branch") &&
              emit_one(file, &builder, build_ninth_parameter, "core_ninth");
    if (fclose(file) != 0) {
        success = false;
    }
    return success ? 0 : 1;
}
