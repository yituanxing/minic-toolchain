#ifndef MINIC_TARGET_RISCV64_CODEGEN_INTERNAL_H
#define MINIC_TARGET_RISCV64_CODEGEN_INTERNAL_H

#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/data_layout.h"
#include "target/riscv/frame_address_policy.h"

#include <stddef.h>
#include <stdio.h>

/* The RV64 frame owns only the physical base and existing scratch liveness.
 * All large-displacement address, load and store sequences are selected by
 * one RISC-V machine policy, including a nonaliasing scratch fallback. */
static inline MinicRiscvFrameAddressPolicy minic_riscv64_machine_frame_policy(
    bool dynamic_frame) {
    MinicRiscvFrameAddressPolicy policy = {
        dynamic_frame ? "s0" : "sp",
        dynamic_frame ? "t6" : "t2",
        dynamic_frame ? "t5" : "t6",
        "ld", "sd"
    };
    return policy;
}

void minic_riscv64_set_diagnostic(MinicDiagnostic *diagnostic,
                                  const char *path,
                                  const char *message);
bool minic_riscv64_emit_stack_allocate(FILE *file, size_t size);
bool minic_riscv64_emit_stack_release(FILE *file, size_t size);
bool minic_riscv64_emit_sp_store64(FILE *file, const char *register_name, size_t offset);
bool minic_riscv64_emit_sp_load64(FILE *file, const char *register_name, size_t offset);
bool minic_riscv64_emit_integer_conversion_for_program(FILE *file,
                                                       const MinicC0Program *program,
                                                       MinicType type,
                                                       const char *register_name);
bool minic_riscv64_emit_scalar_load_for_program(FILE *file,
                                                const MinicC0Program *program,
                                                MinicType type,
                                                const char *destination_register,
                                                const char *address_register);
bool minic_riscv64_emit_scalar_store_for_program(FILE *file,
                                                 const MinicC0Program *program,
                                                 MinicType type,
                                                 const char *source_register,
                                                 const char *address_register);
/* Typed Core pointer memory, distinct from aligned machine-owned frame
 * slots. All source pointer effects must use this proven-or-guarded form. */
bool minic_riscv64_emit_safe_scalar_load(FILE *, const MinicC0Program *,
    MinicType, const char *, const char *, const char *, size_t);
bool minic_riscv64_emit_safe_scalar_store(FILE *, const MinicC0Program *,
    MinicType, const char *, const char *, const char *, size_t);
bool minic_riscv64_emit_integer_aggregate_load_chunk_profile(
    FILE *file, const MinicC0Program *program, MinicType type,
    size_t chunk_index, const char *destination_register,
    const char *address_register, bool lp64_soft_float,
    size_t known_frame_offset);
bool minic_riscv64_emit_integer_aggregate_load_chunk(FILE *file,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     size_t chunk_index,
                                                     const char *destination_register,
                                                     const char *address_register);

#endif
