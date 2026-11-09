#ifndef MINIC_TARGET_COMMON_MACHINE_LP64_FRAME_H
#define MINIC_TARGET_COMMON_MACHINE_LP64_FRAME_H

#include <stdbool.h>
#include <stddef.h>

/* AAPCS64 / LoongArch LP64 physical function-frame facts, determined exactly
 * once after Core and ABI planning. No C type, Core ID, source object, or
 * assembler text crosses this boundary. A target remains responsible for
 * choosing hardware frame registers and legal address materialization.
 *
 * The historical Core-indexed LP64 slot lookup is deliberately separate:
 * instruction selection consumes it to create physical frame references,
 * while a future MachineFunction will own those references without Core.
 */
typedef struct MinicMachineLP64FrameContract {
    size_t frame_size;
    size_t frame_alignment;
    size_t saved_entry_sp_offset;
    size_t save_offset;
    size_t indirect_return_offset;
    size_t variadic_save_base;
    size_t variadic_count;
    size_t variadic_gp_count;
    size_t variadic_fp_count;
    bool has_calls;
    bool has_indirect_return;
    bool has_dynamic_stack;
} MinicMachineLP64FrameContract;

#endif
