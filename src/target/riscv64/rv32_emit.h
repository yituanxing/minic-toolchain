#ifndef MINIC_RISCV_RV32_EMIT_H
#define MINIC_RISCV_RV32_EMIT_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* First RV32 ILP32 consumer of the shared Core -> RISC-V emitter boundary.
 * Validated scalar operations only; never reuse RV64's 8-byte frame slots. */
bool minic_riscv32_write_core_program(const char *path,
                                      const MinicC0Program *program,
                                      const MinicCoreFunction *functions,
                                      size_t function_count,
                                      bool function_sections,
                                      const MinicTargetInfo *target,
                                      MinicDiagnostic *diagnostic);
#endif
