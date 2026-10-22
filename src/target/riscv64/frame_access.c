#include "target/riscv64/frame_access.h"
#include "target/riscv64/codegen_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* No frame-size recalculation or Core mutation in native addressing. */
static bool emit_sp_address(FILE *file, const char *destination_register, size_t offset) {
    if (file == NULL || destination_register == NULL) {
        return false;
    }
    if (offset <= 2047U) {
        return fprintf(file, "  addi %s, sp, %zu\n", destination_register, offset) >= 0;
    }
    return fprintf(file,
                   "  li t3, %zu\n"
                   "  add %s, sp, t3\n",
                   offset,
                   destination_register) >= 0;
}

static const char *core_frame_scratch_register(const char *value_register) {
    return value_register != NULL && strcmp(value_register, "t6") == 0 ? "t5" : "t6";
}

bool minic_riscv64_frame_emit_address(FILE *file,
                               const MinicRiscv64CoreFrame *frame,
                               const char *destination_register,
                               size_t offset) {
    const char *scratch;

    if (file == NULL || frame == NULL || destination_register == NULL) {
        return false;
    }
    if (!frame->has_dynamic_stack_alloc) {
        return emit_sp_address(file, destination_register, offset);
    }
    if (offset <= 2047U) {
        return fprintf(file, "  addi %s, s0, %zu\n", destination_register, offset) >= 0;
    }
    scratch = core_frame_scratch_register(destination_register);
    return fprintf(file,
                   "  li %s, %zu\n"
                   "  add %s, s0, %s\n",
                   scratch,
                   offset,
                   destination_register,
                   scratch) >= 0;
}

bool minic_riscv64_frame_emit_load64(FILE *file,
                              const MinicRiscv64CoreFrame *frame,
                              const char *destination_register,
                              size_t offset) {
    const char *scratch;

    if (file == NULL || frame == NULL || destination_register == NULL) {
        return false;
    }
    if (!frame->has_dynamic_stack_alloc) {
        return minic_riscv64_emit_sp_load64(file, destination_register, offset);
    }
    if (offset <= 2047U) {
        return fprintf(file, "  ld %s, %zu(s0)\n", destination_register, offset) >= 0;
    }
    scratch = core_frame_scratch_register(destination_register);
    return fprintf(file,
                   "  li %s, %zu\n"
                   "  add %s, s0, %s\n"
                   "  ld %s, 0(%s)\n",
                   scratch,
                   offset,
                   scratch,
                   scratch,
                   destination_register,
                   scratch) >= 0;
}

bool minic_riscv64_frame_emit_store64(FILE *file,
                               const MinicRiscv64CoreFrame *frame,
                               const char *source_register,
                               size_t offset) {
    const char *scratch;

    if (file == NULL || frame == NULL || source_register == NULL) {
        return false;
    }
    if (!frame->has_dynamic_stack_alloc) {
        return minic_riscv64_emit_sp_store64(file, source_register, offset);
    }
    if (offset <= 2047U) {
        return fprintf(file, "  sd %s, %zu(s0)\n", source_register, offset) >= 0;
    }
    scratch = core_frame_scratch_register(source_register);
    return fprintf(file,
                   "  li %s, %zu\n"
                   "  add %s, s0, %s\n"
                   "  sd %s, 0(%s)\n",
                   scratch,
                   offset,
                   scratch,
                   scratch,
                   source_register,
                   scratch) >= 0;
}

bool minic_riscv64_frame_load_value(FILE *file,
                            const MinicRiscv64CoreFrame *frame,
                            MinicCoreValueId value_id,
                            const char *register_name) {
    size_t offset;

    return minic_riscv64_core_frame_value_offset(frame, value_id, &offset) &&
           minic_riscv64_frame_emit_load64(file, frame, register_name, offset);
}

bool minic_riscv64_frame_store_value(FILE *file,
                             const MinicRiscv64CoreFrame *frame,
                             MinicCoreValueId value_id,
                             const char *register_name) {
    size_t offset;

    return minic_riscv64_core_frame_value_offset(frame, value_id, &offset) &&
           minic_riscv64_frame_emit_store64(file, frame, register_name, offset);
}

/* M161_CORE_RV64_INT128_PAIR: Core remains target-neutral; RV64 lowers wide
   integer values to a low/high XLEN pair only inside the target backend. */
bool minic_riscv64_frame_load_int128(FILE *file,
                                   const MinicRiscv64CoreFrame *frame,
                                   MinicCoreValueId value_id,
                                   const char *low_register,
                                   const char *high_register) {
    size_t offset;

    return low_register != NULL && high_register != NULL &&
           minic_riscv64_core_frame_value_offset(frame, value_id, &offset) && offset <= SIZE_MAX - 8U &&
           minic_riscv64_frame_emit_load64(file, frame, low_register, offset) &&
           minic_riscv64_frame_emit_load64(file, frame, high_register, offset + 8U);
}

bool minic_riscv64_frame_store_int128(FILE *file,
                                    const MinicRiscv64CoreFrame *frame,
                                    MinicCoreValueId value_id,
                                    const char *low_register,
                                    const char *high_register) {
    size_t offset;

    return low_register != NULL && high_register != NULL &&
           minic_riscv64_core_frame_value_offset(frame, value_id, &offset) && offset <= SIZE_MAX - 8U &&
           minic_riscv64_frame_emit_store64(file, frame, low_register, offset) &&
           minic_riscv64_frame_emit_store64(file, frame, high_register, offset + 8U);
}

