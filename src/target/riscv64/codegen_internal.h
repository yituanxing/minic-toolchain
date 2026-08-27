#ifndef MINIC_TARGET_RISCV64_CODEGEN_INTERNAL_H
#define MINIC_TARGET_RISCV64_CODEGEN_INTERNAL_H

#include "frontend/ast.h"
#include "minic/compiler.h"

#include <stddef.h>
#include <stdio.h>

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
bool minic_riscv64_emit_integer_aggregate_load_chunk(FILE *file,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     size_t chunk_index,
                                                     const char *destination_register,
                                                     const char *address_register);

#endif
