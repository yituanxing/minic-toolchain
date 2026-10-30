#ifndef MINIC_COMPILER_INLINE_SPECIALIZE_INTERNAL_H
#define MINIC_COMPILER_INLINE_SPECIALIZE_INTERNAL_H
#include "frontend/ast.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* One conservative opaque-ASM symbol scan shared by candidate selection and
 * residual-reference retirement. A hit prevents static source removal. */
bool minic_inline_source_has_opaque_asm_reference(
    const MinicC0Program *program, const MinicFunction *source);
bool minic_program_has_opaque_asm_symbol(const MinicC0Program *program,
                                          const char *name, size_t name_length);

/* Only two existing clone construction helpers cross specialization
 * families. They remain compiler-private, never backend or Core hooks. */
bool minic_add_inline_integer_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const bool *known,
    const uint64_t *bits,
    size_t ordinal,
    MinicFunctionId *specialized_id);
bool minic_add_inline_symbolic_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    MinicFunctionId *specialized_id);
#endif
