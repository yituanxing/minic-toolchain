#ifndef MINIC_TARGET_RISCV64_LAYOUT_H
#define MINIC_TARGET_RISCV64_LAYOUT_H

#include "frontend/ast.h"
#include "minic/compiler.h"

#include <stdbool.h>
#include <stddef.h>

bool minic_riscv64_type_layout(const MinicC0Program *program,
                               MinicType type,
                               size_t *size,
                               size_t *alignment);

typedef struct MinicRiscv64FunctionLayout {
    size_t *local_offsets;
    size_t local_count;
    size_t local_storage_size;
} MinicRiscv64FunctionLayout;

void minic_riscv64_function_layout_initialize(MinicRiscv64FunctionLayout *layout);
void minic_riscv64_function_layout_destroy(MinicRiscv64FunctionLayout *layout);
bool minic_riscv64_layout_function(const char *path,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   MinicRiscv64FunctionLayout *layout,
                                   MinicDiagnostic *diagnostic);
bool minic_riscv64_function_layout_local_offset(const MinicRiscv64FunctionLayout *layout,
                                                const MinicFunction *function,
                                                MinicLocalId local_id,
                                                size_t *offset);

bool minic_riscv64_layout_program(const char *path,
                                  MinicC0Program *program,
                                  MinicDiagnostic *diagnostic);

#endif
