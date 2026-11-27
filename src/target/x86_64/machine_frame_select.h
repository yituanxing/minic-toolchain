#ifndef MINIC_TARGET_X86_64_MACHINE_FRAME_SELECT_H
#define MINIC_TARGET_X86_64_MACHINE_FRAME_SELECT_H

#include "target/common/machine_builder.h"
#include "target/x86_64/machine_opcode.h"
#include <string.h>

/* Physical operand constructors shared by the frame and Core selectors.
 * Register access is assigned by the opcode contract, not by the caller. */
static inline MinicMachineOperand minic_x86_machine_reg(
    MinicMachineRegisterBank bank, unsigned number, unsigned width) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){number, (uint16_t)width, bank};
    return op;
}
static inline MinicMachineOperand minic_x86_machine_gp(unsigned number, unsigned width) {
    return minic_x86_machine_reg(MINIC_MACHINE_REGISTER_GENERAL, number, width);
}
static inline MinicMachineOperand minic_x86_machine_imm(int64_t value) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_INTEGER;
    op.value.integer = value;
    return op;
}
static inline MinicMachineOperand minic_x86_machine_block(size_t id) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_BLOCK;
    op.value.block_id = id;
    return op;
}
static inline MinicMachineOperand minic_x86_machine_symbol(const char *name, uint32_t relocation) {
    MinicMachineOperand op;
    (void)memset(&op, 0, sizeof(op));
    op.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    op.value.symbol.name = (char *)name;
    op.value.symbol.relocation = relocation;
    return op;
}
static inline MinicMachineOperand minic_x86_machine_address(unsigned base, int64_t offset) {
    MinicMachineOperand op;
    (void)memset(&op, 0, sizeof(op));
    op.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    op.value.address.has_base = true;
    op.value.address.base = (MinicMachineRegister){base, 64U, MINIC_MACHINE_REGISTER_GENERAL};
    op.value.address.scale = 1U;
    op.value.address.displacement = offset;
    return op;
}

bool minic_x86_machine_emit_selected(MinicMachineBuilder *, const MinicTargetInfo *,
    MinicX86MachineOpcode, MinicMachineOperand *, size_t, unsigned int extra_effects);

struct MinicX86Frame;

/* Only physical ABI geometry crosses the Core-frame -> Machine selector
 * boundary. Machine IR never owns a Core value, Core object or C type. */
typedef struct MinicX86MachineFrameView {
    size_t frame_size;
    MinicFunctionCallingConvention calling_convention;
    size_t ms_gp_save_offset;
    size_t ms_xmm_save_offset;
    bool has_sret;
    size_t sret_save_offset;
    bool saves_incoming_gp;
    size_t incoming_gp_save_offset;
    bool objects_from_aligned_rsp;
    size_t object_base_alignment;
    bool has_varargs;
    size_t varargs_regsave_offset;
} MinicX86MachineFrameView;

/* Frozen x86 frame plan -> non-Core physical view, before Machine selection. */
bool minic_x86_64_machine_frame_view(const struct MinicX86Frame *,
    MinicFunctionCallingConvention, MinicX86MachineFrameView *);

typedef struct MinicX86MachinePrologue {
    size_t entry_block;
    size_t continuation_block;
} MinicX86MachinePrologue;

/* Creates a separate Machine entry block for the ABI prologue, never the
 * Core entry block. This prevents a Core backedge from repeating the prologue.
 * Caller appends the next Core block(s), then terminates continuation_block
 * to the chosen Core entry block. Variadic FP spills have verified machine
 * branches and a real physical CFG of their own. */
bool minic_x86_64_machine_select_prologue(MinicMachineBuilder *,
    const MinicX86MachineFrameView *, MinicX86MachinePrologue *);

/* Selects a complete shared return block; caller owns its CFG label/edges. */
bool minic_x86_64_machine_select_epilogue(MinicMachineBuilder *,
    const MinicX86MachineFrameView *);

/* Target-selected RBP/RSP frame slots. The resulting operand owns only a
 * MachineFunction-local frame index; no numeric-offset GNU fragment exists. */
bool minic_x86_64_machine_frame_slot(MinicMachineBuilder *,
    MinicMachineFrameBase, MinicMachineFrameArea, size_t, size_t,
    int64_t, MinicMachineOperand *);

#endif
