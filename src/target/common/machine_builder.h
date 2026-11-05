#ifndef MINIC_TARGET_COMMON_MACHINE_BUILDER_H
#define MINIC_TARGET_COMMON_MACHINE_BUILDER_H

#include "target/common/machine_ir.h"

/* Mutable selection cursor. It owns no Core, C, or TargetInfo objects;
 * MachineFunction takes full ownership of each appended physical instruction.
 * Target-specific legalization/ABI planning happen BEFORE this builder. */
typedef struct MinicMachineBuilder {
    MinicMachineFunction *function;
    size_t current_block;
    bool has_position;
    bool has_core_origin;
    size_t origin_block;
    size_t origin_instruction;
} MinicMachineBuilder;

bool minic_machine_builder_initialize(MinicMachineBuilder *,
    MinicMachineFunction *);
bool minic_machine_builder_create_block(MinicMachineBuilder *,
    const char *exported_label, bool indirect_entry, bool auxiliary,
    size_t *block_id);
bool minic_machine_builder_position_at(MinicMachineBuilder *, size_t block_id);
void minic_machine_builder_set_origin(MinicMachineBuilder *,
    size_t core_block_id, size_t core_instruction_id);
void minic_machine_builder_clear_origin(MinicMachineBuilder *);
bool minic_machine_builder_add_successor(MinicMachineBuilder *,
    size_t target_block);
bool minic_machine_builder_set_fallthrough(MinicMachineBuilder *,
    size_t next_block);

/* Input operands/effects may be temporary arrays, and must already contain
 * target-owned register numbers, relocation encodings, frame indices, etc.
 * Opcode encodability and precise register effects are independently checked
 * at whole-function verification, before GNU serialization. */
bool minic_machine_builder_emit_selected(MinicMachineBuilder *,
    uint32_t target_opcode, unsigned int effects,
    const MinicMachineOperand *operands, size_t operand_count,
    const MinicMachineRegisterEffect *implicit, size_t implicit_count);

/* This is ONLY for source-proven inline ASM; never use it to smuggle selected
 * instruction strings past target opcode, feature, and effect verification. */
bool minic_machine_builder_emit_source_asm(MinicMachineBuilder *,
    const char *source_bytes, size_t byte_length, unsigned int effects,
    const MinicMachineOperand *bindings, size_t binding_count,
    const MinicMachineRegisterEffect *implicit, size_t implicit_count,
    bool terminates_block, bool permits_fallthrough);

#endif
