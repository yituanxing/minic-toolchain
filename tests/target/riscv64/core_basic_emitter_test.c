#include "core/core_ir.h"
#include "target/riscv64/core_codegen.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static MinicSourceSpan empty_span(void) {
    MinicSourceSpan span;

    (void)memset(&span, 0, sizeof(span));
    return span;
}

static bool append_parameter_ingress(MinicCoreFunction *function,
                                     MinicCoreBlockId block_id,
                                     size_t parameter_index,
                                     MinicType type,
                                     MinicCoreValueId *address_id) {
    MinicCoreInstruction instruction;
    MinicCoreObjectId object_id;
    MinicCoreValueId parameter_value;
    MinicType pointer_type;

    if (!minic_core_function_add_object(function, empty_span(), type, &object_id) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    instruction.span = empty_span();
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.parameter_index = parameter_index;
    if (!minic_core_function_append_value_instruction(
            function, block_id, &instruction, &parameter_value)) {
        return false;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = empty_span();
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    if (!minic_core_function_append_value_instruction(function, block_id, &instruction, address_id)) {
        return false;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = empty_span();
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = *address_id;
    instruction.value.store.stored_value = parameter_value;
    instruction.value.store.is_volatile = false;
    return minic_core_function_append_effect_instruction(function, block_id, &instruction);
}

static bool append_load(MinicCoreFunction *function,
                        MinicCoreBlockId block_id,
                        MinicCoreValueId address_id,
                        MinicType type,
                        MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = empty_span();
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address_id;
    instruction.value.load.is_volatile = false;
    return minic_core_function_append_value_instruction(function, block_id, &instruction, value_id);
}

static bool append_constant(MinicCoreFunction *function,
                            MinicCoreBlockId block_id,
                            long long value,
                            MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = empty_span();
    instruction.type = minic_type_int();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = value;
    return minic_core_function_append_value_instruction(function, block_id, &instruction, value_id);
}

static bool append_unary(MinicCoreFunction *function,
                         MinicCoreBlockId block_id,
                         MinicCoreInstructionKind kind,
                         MinicCoreValueId operand,
                         MinicType type,
                         MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = kind;
    instruction.span = empty_span();
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = operand;
    return minic_core_function_append_value_instruction(function, block_id, &instruction, value_id);
}

static bool set_return(MinicCoreFunction *function,
                       MinicCoreBlockId block_id,
                       MinicCoreValueId value_id) {
    MinicCoreTerminator terminator;

    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.span = empty_span();
    terminator.return_value = value_id;
    terminator.branch_target = MINIC_CORE_BLOCK_INVALID;
    return minic_core_function_set_terminator(function, block_id, &terminator);
}

static bool build_math(MinicCoreFunction *function) {
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
    minic_core_function_initialize(function);
    if (!minic_core_function_set_signature(
            function, "core_basic_math", 15U, minic_type_int(), parameters, 2U) ||
        !minic_core_function_add_block(function, &block_id) ||
        !append_parameter_ingress(function, block_id, 0U, parameters[0], &x_address) ||
        !append_parameter_ingress(function, block_id, 1U, parameters[1], &y_address) ||
        !append_load(function, block_id, x_address, parameters[0], &x_value) ||
        !append_load(function, block_id, y_address, parameters[1], &y_value)) {
        return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    instruction.span = empty_span();
    instruction.type = minic_type_int();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = x_value;
    instruction.value.binary.right = y_value;
    if (!minic_core_function_append_value_instruction(function, block_id, &instruction, &sum) ||
        !append_unary(function,
                      block_id,
                      MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,
                      sum,
                      minic_type_int(),
                      &negated) ||
        !append_unary(function,
                      block_id,
                      MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,
                      negated,
                      minic_type_int(),
                      &zero) ||
        !set_return(function, block_id, zero)) {
        return false;
    }
    return minic_core_function_verify(function);
}

static bool build_branch(MinicCoreFunction *function) {
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
    minic_core_function_initialize(function);
    if (!minic_core_function_set_signature(
            function, "core_branch", 11U, minic_type_int(), &parameter, 1U) ||
        !minic_core_function_add_block(function, &entry) ||
        !minic_core_function_add_block(function, &when_true) ||
        !minic_core_function_add_block(function, &when_false) ||
        !append_parameter_ingress(function, entry, 0U, parameter, &x_address) ||
        !append_load(function, entry, x_address, parameter, &condition)) {
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
        !append_constant(function, when_true, 7, &true_value) ||
        !set_return(function, when_true, true_value) ||
        !append_constant(function, when_false, 3, &false_source) ||
        !append_unary(function,
                      when_false,
                      MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,
                      false_source,
                      minic_type_int(),
                      &false_value) ||
        !set_return(function, when_false, false_value)) {
        return false;
    }
    return minic_core_function_verify(function);
}

static bool build_ninth_parameter(MinicCoreFunction *function) {
    MinicCoreBlockId entry;
    MinicCoreValueId addresses[9];
    MinicCoreValueId ninth;
    MinicType parameters[9];
    size_t index;

    for (index = 0U; index < 9U; ++index) {
        parameters[index] = minic_type_int();
    }
    minic_core_function_initialize(function);
    if (!minic_core_function_set_signature(
            function, "core_ninth", 10U, minic_type_int(), parameters, 9U) ||
        !minic_core_function_add_block(function, &entry)) {
        return false;
    }
    for (index = 0U; index < 9U; ++index) {
        if (!append_parameter_ingress(function, entry, index, parameters[index], &addresses[index])) {
            return false;
        }
    }
    if (!append_load(function, entry, addresses[8], parameters[8], &ninth) ||
        !set_return(function, entry, ninth)) {
        return false;
    }
    return minic_core_function_verify(function);
}

static bool emit_one(FILE *file,
                     MinicCoreFunction *function,
                     bool (*builder)(MinicCoreFunction *),
                     const char *symbol_name) {
    bool success;

    if (!builder(function)) {
        return false;
    }
    success = minic_riscv64_core_function_can_emit_basic_v0(function) &&
              minic_riscv64_emit_core_function_basic_v0(file, function, symbol_name);
    minic_core_function_destroy(function);
    return success;
}

int main(int argc, char **argv) {
    MinicCoreFunction function;
    FILE *file;
    bool success;

    if (argc != 2) {
        return 2;
    }
    file = fopen(argv[1], "w");
    if (file == NULL) {
        return 3;
    }
    success = emit_one(file, &function, build_math, "core_basic_math") &&
              emit_one(file, &function, build_branch, "core_branch") &&
              emit_one(file, &function, build_ninth_parameter, "core_ninth");
    if (fclose(file) != 0) {
        success = false;
    }
    return success ? 0 : 1;
}
