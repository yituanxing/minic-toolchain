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


/* One partial-slot marshaller for incoming/outgoing records and Core memory. */
bool
minic_riscv64_frame_load_chunk(FILE *file,
                      const MinicRiscv64CoreFrame *frame,
                      const char *destination_register,
                      size_t offset,
                      size_t size) {
    const char *opcode;
    size_t byte_index;

    if (file == NULL || frame == NULL || destination_register == NULL ||
        size == 0U || size > 8U) {
        return false;
    }
    if (size == 8U) {
        return minic_riscv64_frame_emit_load64(file, frame, destination_register, offset);
    }
    opcode = size == 4U ? "lwu" : size == 2U ? "lhu" : size == 1U ? "lbu" : NULL;
    if (opcode != NULL) {
        if (!minic_riscv64_frame_emit_address(file, frame, "t3", offset)) {
            return false;
        }
        return fprintf(file, "  %s %s, 0(t3)\n", opcode, destination_register) >= 0;
    }
    if (fprintf(file, "  li %s, 0\n", destination_register) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < size; ++byte_index) {
        size_t byte_offset;

        if (offset > SIZE_MAX - byte_index) {
            return false;
        }
        byte_offset = offset + byte_index;
        if (!minic_riscv64_frame_emit_address(file, frame, "t3", byte_offset) ||
            fprintf(file, "  lbu t1, 0(t3)\n") < 0) {
            return false;
        }
        if (byte_index != 0U && fprintf(file, "  slli t1, t1, %zu\n", byte_index * 8U) < 0) {
            return false;
        }
        if (fprintf(file, "  or %s, %s, t1\n", destination_register, destination_register) < 0) {
            return false;
        }
    }
    return true;
}

bool
minic_riscv64_frame_store_chunk(FILE *file,
                       const MinicRiscv64CoreFrame *frame,
                       const char *source_register,
                       size_t offset,
                       size_t size) {
    const char *opcode;
    size_t byte_index;

    if (file == NULL || frame == NULL || source_register == NULL ||
        size == 0U || size > 8U) {
        return false;
    }
    if (size == 8U) {
        return minic_riscv64_frame_emit_store64(file, frame, source_register, offset);
    }
    opcode = size == 4U ? "sw" : size == 2U ? "sh" : size == 1U ? "sb" : NULL;
    if (opcode != NULL) {
        if (!minic_riscv64_frame_emit_address(file, frame, "t3", offset)) {
            return false;
        }
        return fprintf(file, "  %s %s, 0(t3)\n", opcode, source_register) >= 0;
    }
    if (fprintf(file, "  mv t1, %s\n", source_register) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < size; ++byte_index) {
        size_t byte_offset;

        if (offset > SIZE_MAX - byte_index) {
            return false;
        }
        byte_offset = offset + byte_index;
        if (!minic_riscv64_frame_emit_address(file, frame, "t3", byte_offset) ||
            fprintf(file, "  sb t1, 0(t3)\n") < 0) {
            return false;
        }
        if (byte_index + 1U < size && fprintf(file, "  srli t1, t1, 8\n") < 0) {
            return false;
        }
    }
    return true;
}

