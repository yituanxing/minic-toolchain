#ifndef MINIC_TARGET_X86_64_ASM_CONTRACT_INTERNAL_H
#define MINIC_TARGET_X86_64_ASM_CONTRACT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

/* Shared, ISA-specific spelling recognizer. The machine implementation owns
 * this parser; source admission may query it but must not copy its algorithm.
 * No C type, AST or source constraint is permitted in this interface. */
bool minic_x86_64_compact_asm_equals(const char *text, size_t length,
                                      const char *expected);

#endif
