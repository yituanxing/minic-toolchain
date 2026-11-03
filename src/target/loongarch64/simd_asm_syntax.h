#ifndef MINIC_TARGET_LOONGARCH64_SIMD_ASM_SYNTAX_H
#define MINIC_TARGET_LOONGARCH64_SIMD_ASM_SYNTAX_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Shared, exact LSX/LASX instruction grammar.  The GNU source qualifier and
 * Core machine emitter consume one parser rather than maintaining separate
 * opcode/register/placeholder allowlists.  This is not a C type interface. */
typedef enum LoongSimdOp {
    LOONG_SIMD_LOAD,
    LOONG_SIMD_STORE,
    LOONG_SIMD_XOR,
    LOONG_SIMD_AND_IMMEDIATE,
    LOONG_SIMD_OR_IMMEDIATE,
    LOONG_SIMD_SLTI,
    LOONG_SIMD_SLLI,
    LOONG_SIMD_SRLI,
    LOONG_SIMD_SHUFFLE,
    LOONG_SIMD_REPLICATE
} LoongSimdOp;

typedef struct LoongSimdInstruction {
    LoongSimdOp op;
    bool lasx;
    unsigned int registers[4];
    size_t operand_index;
    int memory_offset;
    char machine_line[192];
} LoongSimdInstruction;

typedef struct LoongSimdProgram {
    LoongSimdInstruction instructions[64];
    size_t count;
    size_t operand_count;
    bool memory_read[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool memory_written[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool uses_lsx;
    bool uses_lasx;
} LoongSimdProgram;

bool minic_loongarch64_simd_parse(const char *, size_t, LoongSimdProgram *);

#endif
