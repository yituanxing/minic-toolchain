#ifndef MINIC_TARGET_RISCV64_CODEGEN_INTERNAL_H
#define MINIC_TARGET_RISCV64_CODEGEN_INTERNAL_H

#include "frontend/ast.h"
#include "minic/compiler.h"

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
bool minic_riscv64_frame_size(const MinicFunction *function, size_t *frame_size);

bool minic_riscv64_emit_lvalue_address(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicFunction *function,
                                       MinicExpressionId expression_id);
bool minic_riscv64_emit_expression(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   MinicExpressionId expression_id);
bool minic_riscv64_emit_block(FILE *file,
                              const MinicC0Program *program,
                              const MinicFunction *function,
                              MinicBlockId block_id,
                              size_t *label_counter);

#endif