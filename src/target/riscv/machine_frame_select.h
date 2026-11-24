#ifndef MINIC_TARGET_RISCV_MACHINE_FRAME_SELECT_H
#define MINIC_TARGET_RISCV_MACHINE_FRAME_SELECT_H

#include "target/common/machine_builder.h"
#include "target/riscv/machine_opcode.h"
#include <string.h>

/* Canonical GPR, immediate and base/displacement operand construction.
 * Both physical selectors use identical register bank and relocation fields. */
static inline MinicMachineOperand minic_riscv_machine_gpr(
    unsigned xlen, unsigned number, unsigned access) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){
        number, (uint16_t)xlen, MINIC_MACHINE_REGISTER_GENERAL};
    op.value.physical.access = access;
    return op;
}
static inline MinicMachineOperand minic_riscv_machine_integer(int64_t number) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_INTEGER;
    op.value.integer = number;
    return op;
}
static inline MinicMachineOperand minic_riscv_machine_address(
    unsigned xlen, unsigned base, int64_t displacement) {
    MinicMachineOperand op;
    (void)memset(&op, 0, sizeof(op));
    op.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    op.value.address.target_mode = MINIC_RISCV_MACHINE_ADDRESS_BASE_DISPLACEMENT;
    op.value.address.has_base = true;
    op.value.address.base = (MinicMachineRegister){
        base, (uint16_t)xlen, MINIC_MACHINE_REGISTER_GENERAL};
    op.value.address.scale = 1U;
    op.value.address.displacement = displacement;
    return op;
}
uint32_t minic_riscv_machine_memory_opcode(
    unsigned xlen, bool load, unsigned bytes, bool sign, bool frame);

/* Canonical target-owned opcode admission shared by the physical frame and
 * Core selectors. The verifier contract supplies implicit register effects;
 * neither selector may invent an instruction effect or bypass legalization. */
bool minic_riscv_machine_emit_selected(MinicMachineBuilder *,
    uint32_t opcode, const MinicMachineOperand *, size_t operand_count);
/* Core volatile memory operations retain the target's ordinary physical
 * load/store opcode, but the effect bit is validated by that opcode's
 * contract and must not be silently dropped by a selector. */
bool minic_riscv_machine_emit_selected_effects(MinicMachineBuilder *,
    uint32_t opcode, unsigned int extra_effects,
    const MinicMachineOperand *, size_t operand_count);

/* Already-finalized physical psABI frame, independent of the source language.
 * RV32 ILP32 and RV64 LP64 use the SAME native selection algorithm, while
 * their planners remain responsible for all offsets and value locations.
 * A compiler selector must populate the descriptor from its verified plan;
 * this deliberately cannot reconstruct C types or invent stack slots. */
typedef struct MinicRiscvPhysicalCalleeSave {
    unsigned register_number; /* s0..s11, including structured-ASM bindings */
    size_t offset;
} MinicRiscvPhysicalCalleeSave;

typedef struct MinicRiscvPhysicalFrame {
    size_t frame_size;
    size_t stack_alignment;
    size_t outgoing_size;
    size_t return_address_offset;
    size_t hidden_result_pointer_offset;
    size_t saved_frame_base_offset;
    size_t entry_sp_offset;
    size_t dynamic_call_area_size;
    size_t varargs_offset;
    size_t varargs_size;
    size_t first_variadic_gpr;
    /* Frozen psABI named stack parameters preceding the first variadic
     * stack argument. This comes from the finalized RV32/RV64 ABI cursor. */
    size_t named_stack_argument_slots;
    bool saves_return_address;
    bool has_hidden_result_pointer;
    bool has_dynamic_stack;
    bool has_dynamic_stack_alignment;
    bool variadic;
    const MinicRiscvPhysicalCalleeSave *callee_saved;
    size_t callee_saved_count;
} MinicRiscvPhysicalFrame;

/* Machine block0 is strictly the ABI entry block; all Core blocks MUST be
 * distinct later blocks, so a Core entry backedge cannot reexecute prologue.
 * The caller creates block1 before emitting this fallthrough. */
bool minic_riscv_machine_select_prologue(MinicMachineBuilder *,
    const MinicRiscvPhysicalFrame *, size_t core_entry_block);

/* The caller owns CFG routing to this dedicated exit block. The epilogue
 * emits only native typed operations and a genuine RETURN terminator. */
bool minic_riscv_machine_select_epilogue(MinicMachineBuilder *,
    const MinicRiscvPhysicalFrame *, size_t exit_block);

/* Frame access uses an already chosen physical base; long displacements are
 * expanded to LI+ADD+LOAD/STORE. Every instruction is target-verified. */
bool minic_riscv_machine_select_frame_transfer(MinicMachineBuilder *,
    unsigned register_number, unsigned base_register, size_t offset,
    bool load, unsigned bytes);

bool minic_riscv_machine_select_frame_object(MinicMachineBuilder *,
    size_t frame_index, size_t byte_displacement, unsigned register_number,
    bool load, unsigned bytes);

#endif
