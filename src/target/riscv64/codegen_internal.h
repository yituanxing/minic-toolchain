#ifndef MINIC_TARGET_RISCV64_CODEGEN_INTERNAL_H
#define MINIC_TARGET_RISCV64_CODEGEN_INTERNAL_H

#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/riscv64/layout.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

void minic_riscv64_set_diagnostic(MinicDiagnostic *diagnostic,
                                  const char *path,
                                  const char *message);
bool minic_riscv64_emit_stack_allocate(FILE *file, size_t size);
bool minic_riscv64_emit_stack_release(FILE *file, size_t size);
bool minic_riscv64_emit_sp_store64(FILE *file, const char *register_name, size_t offset);
bool minic_riscv64_emit_sp_load64(FILE *file, const char *register_name, size_t offset);
bool minic_riscv64_emit_s0_load64(FILE *file, const char *register_name, size_t offset);
bool minic_riscv64_emit_integer_conversion(FILE *file, MinicType type, const char *register_name);
bool minic_riscv64_emit_scalar_load(FILE *file,
                                    MinicType type,
                                    const char *destination_register,
                                    const char *address_register);
bool minic_riscv64_emit_scalar_store(FILE *file,
                                     MinicType type,
                                     const char *source_register,
                                     const char *address_register);

bool minic_riscv64_emit_object_address(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicFunction *function,
                                       MinicLocalId local_id);
bool minic_riscv64_emit_object_load(FILE *file,
                                    const MinicC0Program *program,
                                    const MinicFunction *function,
                                    MinicLocalId local_id);
bool minic_riscv64_emit_object_store(FILE *file,
                                     const MinicC0Program *program,
                                     const MinicFunction *function,
                                     MinicLocalId local_id);
bool minic_riscv64_emit_object_store_register(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
                                              MinicLocalId local_id,
                                              const char *register_name);
bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *storage_size,
                                         size_t *register_chunks);
bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
                                                      const MinicC0Program *program,
                                                      const MinicFunction *function,
                                                      MinicLocalId local_id,
                                                      size_t chunk_index,
                                                      const char *register_name);
typedef struct MinicRiscv64FrameLayout {
    size_t frame_size;
    size_t saved_ra_offset;
    size_t saved_s0_offset;
    size_t varargs_offset;
    size_t varargs_size;
    size_t integer_parameter_count;
} MinicRiscv64FrameLayout;

bool minic_riscv64_frame_layout(const MinicC0Program *program,
                                const MinicFunction *function,
                                MinicRiscv64FrameLayout *layout);

bool minic_riscv64_emit_lvalue_address(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicFunction *function,
                                       MinicExpressionId expression_id);
bool minic_riscv64_emit_address_backed_record_value(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicFunction *function,
                                                    MinicExpressionId expression_id);
bool minic_riscv64_emit_record_copy_value(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          MinicExpressionId target_id,
                                          MinicExpressionId source_id,
                                          bool preserve_target_address);
bool minic_riscv64_emit_expression(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   MinicExpressionId expression_id);
bool minic_riscv64_emit_inline_asm(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   const MinicStatement *statement);
bool minic_riscv64_emit_block(FILE *file,
                              const MinicC0Program *program,
                              const MinicFunction *function,
                              MinicBlockId block_id,
                              size_t *label_counter);

#endif
