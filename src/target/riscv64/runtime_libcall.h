#ifndef MINIC_TARGET_RISCV64_RUNTIME_LIBCALL_H
#define MINIC_TARGET_RISCV64_RUNTIME_LIBCALL_H

#include "target/riscv64/isa.h"
#include "core/core_ir.h"
#include <stdbool.h>
#include <stdio.h>

/* Standard GCC libgcc / LLVM compiler-rt integer builtins.
 * The compiler owns ABI-aware CALL emission; the runtime provider owns
 * arithmetic algorithms. The provider is supplied by the final linker. */
typedef enum MinicRiscv64ArithmeticLibcall {
    MINIC_RISCV64_LIBCALL_MUL,
    MINIC_RISCV64_LIBCALL_SDIV,
    MINIC_RISCV64_LIBCALL_UDIV,
    MINIC_RISCV64_LIBCALL_SMOD,
    MINIC_RISCV64_LIBCALL_UMOD
} MinicRiscv64ArithmeticLibcall;

/* NULL selects existing native F/D or an unsupported non-arithmetic op.
 * A non-NULL name selects an external standard libgcc soft-float helper. */
const char *minic_riscv64_softfloat_binary_symbol(
    const MinicRiscvIsa *isa, MinicCoreInstructionKind kind);
bool minic_riscv64_needs_arithmetic_libcall(const MinicRiscvIsa *isa,
                                             const MinicCoreFunction *function);
bool minic_riscv64_uses_arithmetic_libcall(const MinicRiscvIsa *isa);
bool minic_riscv64_emit_arithmetic_libcall(
    FILE *file, const MinicRiscvIsa *isa, MinicRiscv64ArithmeticLibcall operation,
    const char *destination, const char *left, const char *right);
bool minic_riscv64_power_of_two(size_t value);
unsigned minic_riscv64_log2_power_of_two(size_t value);
#endif
