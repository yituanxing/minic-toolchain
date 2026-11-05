#include "target/loongarch64/machine_builtin_select.h"
#include "target/loongarch64/machine_opcode.h"
#include "target/common/core_call_view.h"
#include "target/target_info.h"

#include <stdint.h>
#include <string.h>

enum { LOONG_BUILTIN_A0 = 4U, LOONG_BUILTIN_A1 = 5U };

static MinicMachineOperand builtin_reg(unsigned int number,
    unsigned int access) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_REGISTER;
    operand.value.physical.reg.number = number;
    operand.value.physical.reg.width_bits = 64U;
    operand.value.physical.reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
    operand.value.physical.access = access;
    return operand;
}

static MinicMachineOperand builtin_imm(uint64_t value) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_INTEGER;
    operand.value.integer = (int64_t)value;
    return operand;
}

/* A real Core SSA definition is mandatory for privileged CSR selector
 * indices. The old source-policy descriptor admitted the constant value,
 * and native lowering rechecks its immutable Core integer definition. */
static bool builtin_core_constant(const MinicCoreFunction *fn,
    MinicCoreValueId value, uint64_t *integer) {
    MinicCoreInstructionId definition;
    const MinicCoreInstruction *instruction;
    if (fn == NULL || integer == NULL || fn->values == NULL ||
        fn->instructions == NULL || value >= fn->value_count) return false;
    definition = fn->values[value].definition;
    if (definition >= fn->instruction_count) return false;
    instruction = &fn->instructions[definition];
    if (instruction->result != value ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT)
        return false;
    *integer = (uint64_t)instruction->value.integer_value;
    return true;
}

bool minic_loongarch64_machine_select_builtin_call(
    MinicMachineBuilder *builder, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *instruction,
    const MinicLP64PreparedCall *call_plan) {
    const MinicCoreFunction *fn;
    const MinicCoreCallee *callee;
    MinicCoreCallView view = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineOpcodeContract contract = {0};
    MinicMachineOperand operands[3] = {{0}};
    const char *mnemonic = NULL;
    size_t argc = 0U, immediate_index = SIZE_MAX, k, operand_count = 0U;
    uint32_t opcode = 0U;
    uint64_t immediate = 0U;
    if (builder == NULL || ctx == NULL || instruction == NULL ||
        call_plan == NULL || ctx->fn == NULL || ctx->target == NULL ||
        builder->function == NULL ||
        builder->function->target != ctx->target ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        instruction->kind != MINIC_CORE_INSTRUCTION_CALL ||
        (fn = ctx->fn)->callees == NULL ||
        instruction->value.call.callee_id >= fn->callee_count ||
        instruction->value.call.extended_float_operation !=
            MINIC_CORE_EXT_FLOAT_NONE)
        return false;
    callee = &fn->callees[instruction->value.call.callee_id];
    if (callee->name == NULL ||
        !minic_core_call_view_build(fn, instruction, &view) ||
        view.callee != callee ||
        !minic_target_info_machine_builtin_form(ctx->target,
            callee->name, strlen(callee->name), &argc,
            &mnemonic, &immediate_index) ||
        mnemonic == NULL ||
        !minic_loongarch64_machine_builtin_opcode(mnemonic, &opcode) ||
        view.core_return_type_id != instruction->core_type_id ||
        view.is_variadic || view.parameter_count != argc ||
        view.argument_count != argc || call_plan->argument_count != argc ||
        call_plan->stack_bytes != 0U || argc == 0U || argc > 3U ||
        view.core_parameter_type_ids == NULL || view.arguments == NULL)
        return false;
    for (k = 0U; k < argc; ++k) {
        const MinicCoreCallArgument *argument = &view.arguments[k];
        const MinicLP64ArgumentLocation *location = &call_plan->arguments[k];
        if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            argument->value.value_id >= fn->value_count ||
            fn->values[argument->value.value_id].core_type_id !=
                view.core_parameter_type_ids[k] ||
            location->gp_first != k || location->gp_registers != 1U ||
            location->fp_registers != 0U ||
            location->stack_first != SIZE_MAX) return false;
    }
    if (immediate_index != SIZE_MAX) {
        if (immediate_index >= argc ||
            !builtin_core_constant(fn,
                view.arguments[immediate_index].value.value_id,
                &immediate) || immediate > 16383U)
            return false;
        /* Physical ISA signatures: CSR index is a typed, range-checked
         * immediate, not a target formatter's parsed assembly expression. */
        operands[0] = builtin_reg(LOONG_BUILTIN_A0,
            argc == 1U ? MINIC_MACHINE_REGISTER_WRITE :
                        MINIC_MACHINE_REGISTER_READ |
                        MINIC_MACHINE_REGISTER_WRITE);
        if (argc == 3U) {
            operands[1] = builtin_reg(LOONG_BUILTIN_A1,
                MINIC_MACHINE_REGISTER_READ);
            operands[2] = builtin_imm(immediate);
            operand_count = 3U;
        } else {
            operands[1] = builtin_imm(immediate);
            operand_count = 2U;
        }
    } else if (argc == 1U) {
        operands[0] = builtin_reg(LOONG_BUILTIN_A0,
            MINIC_MACHINE_REGISTER_WRITE);
        operands[1] = builtin_reg(LOONG_BUILTIN_A0,
            MINIC_MACHINE_REGISTER_READ);
        operand_count = 2U;
    } else if (argc == 2U &&
               (strncmp(mnemonic, "crc.", 4U) == 0 ||
                strncmp(mnemonic, "crcc.", 5U) == 0)) {
        operands[0] = builtin_reg(LOONG_BUILTIN_A0,
            MINIC_MACHINE_REGISTER_WRITE);
        operands[1] = builtin_reg(LOONG_BUILTIN_A0,
            MINIC_MACHINE_REGISTER_READ);
        operands[2] = builtin_reg(LOONG_BUILTIN_A1,
            MINIC_MACHINE_REGISTER_READ);
        operand_count = 3U;
    } else if (argc == 2U) {
        operands[0] = builtin_reg(LOONG_BUILTIN_A0,
            MINIC_MACHINE_REGISTER_READ);
        operands[1] = builtin_reg(LOONG_BUILTIN_A1,
            MINIC_MACHINE_REGISTER_READ);
        operand_count = 2U;
    } else return false;
    if (!minic_loongarch64_machine_gnu_asm_policy_initialize(ctx->target,
            &policy) || policy.verify.lookup_opcode == NULL ||
        !policy.verify.lookup_opcode(ctx->target, opcode, &contract,
            policy.context) || contract.operand_count != operand_count ||
        (contract.required_effects & MINIC_MACHINE_EFFECT_CALL) == 0U)
        return false;
    return minic_machine_builder_emit_selected(builder, opcode,
        contract.required_effects, operands, operand_count,
        contract.implicit_count != 0U ? contract.implicit : NULL,
        contract.implicit_count);
}
