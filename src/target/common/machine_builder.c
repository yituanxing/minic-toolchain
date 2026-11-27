#include "target/common/machine_builder.h"

#include <string.h>

bool minic_machine_builder_initialize(MinicMachineBuilder *builder,
    MinicMachineFunction *function) {
    if (builder == NULL || function == NULL || function->name == NULL ||
        function->target == NULL) return false;
    memset(builder, 0, sizeof(*builder));
    builder->function = function;
    return true;
}

bool minic_machine_builder_position_at(MinicMachineBuilder *builder,
    size_t block_id) {
    if (builder == NULL || builder->function == NULL ||
        block_id >= builder->function->block_count) return false;
    builder->current_block = block_id;
    builder->has_position = true;
    return true;
}





bool minic_machine_builder_create_block(MinicMachineBuilder *builder,
    const char *exported_label, bool indirect_entry, bool auxiliary,
    size_t *block_id) {
    if (builder == NULL || builder->function == NULL || block_id == NULL ||
        !minic_machine_function_add_block(builder->function, exported_label,
                                           indirect_entry, block_id))
        return false;
    builder->function->blocks[*block_id].machine_auxiliary = auxiliary;
    return minic_machine_builder_position_at(builder, *block_id);
}

bool minic_machine_builder_mark_core_block(MinicMachineBuilder *builder,
    size_t core_block_id) {
    MinicMachineBlock *block;
    if (builder == NULL || builder->function == NULL ||
        !builder->has_position ||
        builder->current_block >= builder->function->block_count)
        return false;
    block = &builder->function->blocks[builder->current_block];
    if (block->machine_auxiliary || block->has_core_block_origin)
        return false;
    block->has_core_block_origin = true;
    block->core_block_id = core_block_id;
    return true;
}

void minic_machine_builder_set_origin(MinicMachineBuilder *builder,
    size_t core_block_id, size_t core_instruction_id) {
    if (builder == NULL) return;
    builder->has_core_origin = true;
    builder->origin_block = core_block_id;
    builder->origin_instruction = core_instruction_id;
}

void minic_machine_builder_clear_origin(MinicMachineBuilder *builder) {
    if (builder == NULL) return;
    builder->has_core_origin = false;
    builder->origin_block = 0U;
    builder->origin_instruction = 0U;
}

bool minic_machine_builder_add_successor(MinicMachineBuilder *builder,
    size_t target_block) {
    return builder != NULL && builder->function != NULL &&
        builder->has_position &&
        minic_machine_block_add_successor(builder->function,
            builder->current_block, target_block);
}

bool minic_machine_builder_set_fallthrough(MinicMachineBuilder *builder,
    size_t next_block) {
    return builder != NULL && builder->function != NULL &&
        builder->has_position &&
        minic_machine_block_set_fallthrough(builder->function,
            builder->current_block, next_block);
}

static bool machine_builder_append(MinicMachineBuilder *builder,
    const MinicMachineInstruction *instruction) {
    return builder != NULL && builder->function != NULL &&
        builder->has_position &&
        minic_machine_block_append_instruction(builder->function,
            builder->current_block, instruction);
}









bool minic_machine_builder_emit_selected(MinicMachineBuilder *builder,
    uint32_t target_opcode, unsigned int effects,
    const MinicMachineOperand *operands, size_t operand_count,
    const MinicMachineRegisterEffect *implicit, size_t implicit_count) {
    MinicMachineInstruction instruction = {0};
    if (builder == NULL || target_opcode == 0U ||
        (operand_count != 0U && operands == NULL) ||
        (implicit_count != 0U && implicit == NULL) ||
        operand_count > MINIC_MACHINE_MAX_OPERANDS ||
        implicit_count > MINIC_MACHINE_MAX_REGISTER_EFFECTS)
        return false;
    instruction.kind = MINIC_MACHINE_INSTRUCTION_SELECTED;
    instruction.opcode = target_opcode;
    instruction.effects = effects;
    instruction.operands = (MinicMachineOperand *)operands;
    instruction.operand_count = operand_count;
    instruction.implicit = (MinicMachineRegisterEffect *)implicit;
    instruction.implicit_count = implicit_count;
    instruction.has_core_origin = builder->has_core_origin;
    instruction.core_block_id = builder->origin_block;
    instruction.core_instruction_id = builder->origin_instruction;
    return machine_builder_append(builder, &instruction);
}

bool minic_machine_builder_seal_selected_source_asm(
    MinicMachineBuilder *builder, uint32_t target_machine_form_id,
    uint32_t target_machine_form_variant, uint64_t source_asm_unique_id) {
    MinicMachineBlock *block;
    MinicMachineInstruction *selected;
    if (builder == NULL || builder->function == NULL ||
        !builder->has_position || !builder->has_core_origin ||
        target_machine_form_id == 0U ||
        builder->current_block >= builder->function->block_count)
        return false;
    block = &builder->function->blocks[builder->current_block];
    if (block->instruction_count == 0U) return false;
    selected = &block->instructions[block->instruction_count - 1U];
    if (selected->kind != MINIC_MACHINE_INSTRUCTION_SELECTED ||
        !selected->has_core_origin ||
        selected->core_block_id != builder->origin_block ||
        selected->core_instruction_id != builder->origin_instruction ||
        selected->has_selected_source_asm_seal) return false;
    selected->has_selected_source_asm_seal = true;
    selected->source_asm_machine_form_id = target_machine_form_id;
    selected->source_asm_machine_form_variant = target_machine_form_variant;
    selected->has_source_asm_unique_id = source_asm_unique_id != 0U;
    selected->source_asm_unique_id = source_asm_unique_id;
    return true;
}

bool minic_machine_builder_emit_compiler_barrier(
    MinicMachineBuilder *builder) {
    MinicMachineInstruction instruction = {0};
    if (builder == NULL || !builder->has_core_origin) return false;
    instruction.kind = MINIC_MACHINE_INSTRUCTION_COMPILER_BARRIER;
    instruction.effects = MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    instruction.has_core_origin = true;
    instruction.core_block_id = builder->origin_block;
    instruction.core_instruction_id = builder->origin_instruction;
    return machine_builder_append(builder, &instruction);
}



bool minic_machine_builder_emit_source_asm_typed(MinicMachineBuilder *builder,
    const MinicMachineSourceAsmInput *input) {
    MinicMachineInstruction instruction = {0};
    if (builder == NULL || !builder->has_core_origin || input == NULL ||
        input->source_bytes == NULL ||
        (input->binding_count != 0U && input->bindings == NULL) ||
        (input->implicit_count != 0U && input->implicit == NULL) ||
        input->binding_count > MINIC_MACHINE_MAX_OPERANDS ||
        input->implicit_count > MINIC_MACHINE_MAX_REGISTER_EFFECTS ||
        (input->target_machine_form_id == 0U &&
         input->target_machine_form_variant != 0U) ||
        (!input->has_unique_id && input->unique_id != 0U))
        return false;
    instruction.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    instruction.effects = input->effects | MINIC_MACHINE_EFFECT_SOURCE_ASM;
    instruction.operands = (MinicMachineOperand *)input->bindings;
    instruction.operand_count = input->binding_count;
    instruction.implicit = (MinicMachineRegisterEffect *)input->implicit;
    instruction.implicit_count = input->implicit_count;
    instruction.has_core_origin = true;
    instruction.core_block_id = builder->origin_block;
    instruction.core_instruction_id = builder->origin_instruction;
    instruction.source_asm_template = (char *)input->source_bytes;
    instruction.source_asm_length = input->source_byte_length;
    instruction.source_asm_terminates_block = input->terminates_block;
    instruction.source_asm_permits_fallthrough = input->permits_fallthrough;
    instruction.source_asm_machine_form_id = input->target_machine_form_id;
    instruction.source_asm_machine_form_variant =
        input->target_machine_form_variant;
    instruction.has_source_asm_unique_id = input->has_unique_id;
    instruction.source_asm_unique_id = input->unique_id;
    return machine_builder_append(builder, &instruction);
}
