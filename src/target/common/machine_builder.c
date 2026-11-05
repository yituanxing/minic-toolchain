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

bool minic_machine_builder_add_alias_label(MinicMachineBuilder *builder,
    const char *alias_label) {
    return builder != NULL && builder->has_position &&
        minic_machine_block_add_alias_label(builder->function,
                                            builder->current_block,
                                            alias_label);
}

bool minic_machine_builder_emit_local_label(MinicMachineBuilder *builder,
    const char *name) {
    MinicMachineInstruction label = {0};
    if (builder == NULL || !builder->has_position || name == NULL ||
        name[0] == '\0') return false;
    label.kind = MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL;
    label.local_label_name = (char *)name;
    return minic_machine_block_append_instruction(builder->function,
                                                  builder->current_block,
                                                  &label);
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

static bool machine_builder_append_directive(MinicMachineBuilder *builder,
    const MinicMachineDirective *directive,
    const MinicMachineOperand *operands, size_t operand_count) {
    MinicMachineInstruction instruction = {0};
    if (builder == NULL || directive == NULL ||
        (operand_count != 0U && operands == NULL) ||
        operand_count > 2U) return false;
    instruction.kind = MINIC_MACHINE_INSTRUCTION_DIRECTIVE;
    instruction.directive = (MinicMachineDirective *)directive;
    instruction.operands = (MinicMachineOperand *)operands;
    instruction.operand_count = operand_count;
    return machine_builder_append(builder, &instruction);
}

bool minic_machine_builder_emit_section_push(MinicMachineBuilder *builder,
    const char *section_name, unsigned int section_flags,
    MinicMachineDirectiveSectionType section_type,
    const char *link_order_symbol) {
    MinicMachineDirective directive = {0};
    directive.kind = MINIC_MACHINE_DIRECTIVE_PUSH_SECTION;
    directive.section_name = (char *)section_name;
    directive.section_flags = section_flags;
    directive.section_type = section_type;
    directive.link_order_symbol = (char *)link_order_symbol;
    return machine_builder_append_directive(builder, &directive, NULL, 0U);
}

bool minic_machine_builder_emit_section_pop(MinicMachineBuilder *builder) {
    MinicMachineDirective directive = {0};
    directive.kind = MINIC_MACHINE_DIRECTIVE_POP_SECTION;
    return machine_builder_append_directive(builder, &directive, NULL, 0U);
}

bool minic_machine_builder_emit_data(MinicMachineBuilder *builder,
    unsigned int width_bytes, const MinicMachineOperand *operands,
    size_t operand_count) {
    MinicMachineDirective directive = {0};
    directive.kind = MINIC_MACHINE_DIRECTIVE_DATA;
    directive.data_width_bytes = width_bytes;
    return machine_builder_append_directive(builder, &directive,
                                            operands, operand_count);
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

bool minic_machine_builder_emit_source_asm(MinicMachineBuilder *builder,
    const char *source_bytes, size_t byte_length, unsigned int effects,
    const MinicMachineOperand *bindings, size_t binding_count,
    const MinicMachineRegisterEffect *implicit, size_t implicit_count,
    bool terminates_block, bool permits_fallthrough) {
    MinicMachineSourceAsmInput input = {0};
    input.source_bytes = source_bytes;
    input.source_byte_length = byte_length;
    input.effects = effects;
    input.bindings = bindings;
    input.binding_count = binding_count;
    input.implicit = implicit;
    input.implicit_count = implicit_count;
    input.terminates_block = terminates_block;
    input.permits_fallthrough = permits_fallthrough;
    return minic_machine_builder_emit_source_asm_typed(builder, &input);
}

bool minic_machine_builder_emit_source_asm_typed(MinicMachineBuilder *builder,
    const MinicMachineSourceAsmInput *input) {
    MinicMachineInstruction instruction = {0};
    if (builder == NULL || !builder->has_core_origin || input == NULL ||
        input->source_bytes == NULL || input->source_byte_length == 0U ||
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
