#ifndef MINIC_TARGET_RISCV64_CORE_CODEGEN_H
#define MINIC_TARGET_RISCV64_CORE_CODEGEN_H

#include "core/core_ir.h"
#include "target/common/core_function_facts.h"
#include "target/riscv/function_symbol.h"
#include "target/riscv64/isa.h"
#include "minic/compiler.h"

#include <stdbool.h>
#include <stdio.h>

/*
 * O0 Core -> RV64 function-body emitter for the production Core-only path.
 * Core owns semantic function state; RV64 owns ABI placement, frame layout,
 * register allocation, and final instruction selection.
 *
 * Core values and objects receive backend-owned frame slots; no physical
 * register or stack offset is written back into Core IR. Function
 * linkage/visibility/section/assembler-name policy is target-owned metadata
 * carried separately from Core IR through MinicRiscv64FunctionSymbol.
 */
bool minic_riscv64_core_function_can_emit(const MinicCoreFunction *function);
bool minic_riscv64_core_function_can_emit_for_program(const MinicC0Program *program,
                                                       const MinicCoreFunction *function);

bool minic_riscv64_emit_core_function_with_symbol(
    FILE *file, const MinicCoreFunction *function, const MinicRiscv64FunctionSymbol *symbol);
bool minic_riscv64_emit_core_function_for_program_with_symbol_prepared(
    FILE *, const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64FunctionSymbol *, const MinicCoreFunctionFacts *,
    bool, const MinicRiscvIsa *, MinicDiagnostic *);
bool minic_riscv64_emit_core_function_for_program_with_symbol(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64FunctionSymbol *symbol,
    bool position_independent,
    const MinicRiscvIsa *isa,
    MinicDiagnostic *diagnostic);

static inline bool minic_riscv64_emit_core_function(FILE *file,
                                                     const MinicCoreFunction *function,
                                                     const char *symbol_name) {
    MinicRiscv64FunctionSymbol symbol;

    symbol.symbol_name = symbol_name;
    symbol.section_name = NULL;
    symbol.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    symbol.is_internal = false;
    symbol.is_weak = false;
    symbol.function_sections = false;
    return minic_riscv64_emit_core_function_with_symbol(file, function, &symbol);
}

#endif
