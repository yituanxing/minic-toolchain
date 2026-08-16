#ifndef MINIC_TARGET_RISCV64_CORE_CODEGEN_H
#define MINIC_TARGET_RISCV64_CORE_CODEGEN_H

#include "core/core_ir.h"

#include <stdbool.h>
#include <stdio.h>

/*
 * Bounded O0 Core -> RV64 emitter used before production takeover.
 * 第一阶段受限 O0 Core -> RV64 emitter；当前用于 differential 验证，不接管 production。
 *
 * v0 accepts the scalar/memory/CFG subset and deliberately rejects CALL and
 * FIELD_ADDRESS. Core values and objects receive backend-owned frame slots;
 * no physical register or stack offset is written back into Core IR.
 */
bool minic_riscv64_core_function_can_emit_basic_v0(const MinicCoreFunction *function);

bool minic_riscv64_emit_core_function_basic_v0(FILE *file,
                                               const MinicCoreFunction *function,
                                               const char *symbol_name);

#endif
