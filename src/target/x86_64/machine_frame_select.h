#ifndef MINIC_TARGET_X86_64_MACHINE_FRAME_SELECT_H
#define MINIC_TARGET_X86_64_MACHINE_FRAME_SELECT_H

#include "target/common/machine_builder.h"

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
    bool has_variadic_fp_split;
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
