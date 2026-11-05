#ifndef MINIC_TARGET_RISCV_MACHINE_FRAME_SELECT_H
#define MINIC_TARGET_RISCV_MACHINE_FRAME_SELECT_H

#include "target/common/machine_builder.h"

/* Canonical target-owned opcode admission shared by the physical frame and
 * Core selectors. The verifier contract supplies implicit register effects;
 * neither selector may invent an instruction effect or bypass legalization. */
bool minic_riscv_machine_emit_selected(MinicMachineBuilder *,
    uint32_t opcode, const MinicMachineOperand *, size_t operand_count);

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
