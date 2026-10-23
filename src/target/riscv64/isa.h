#ifndef MINIC_TARGET_RISCV64_ISA_H
#define MINIC_TARGET_RISCV64_ISA_H

#include <stdbool.h>
#include <stddef.h>

/* ISA, XLEN, data model and calling ABI are distinct dimensions.
 * The legacy RV64 default remains unchanged until its modes are independently
 * qualified. No global state: this config belongs to each compiler invocation. */
typedef enum MinicRiscvAbi {
    MINIC_RISCV_ABI_INVALID = 0,
    MINIC_RISCV_ABI_LP64,
    MINIC_RISCV_ABI_LP64D
} MinicRiscvAbi;

typedef struct MinicRiscvIsa {
    unsigned xlen;
    MinicRiscvAbi abi;
    bool strict_base_i;
} MinicRiscvIsa;

/* Only RV64I+LP64 is newly qualified. No fake acceptance for RV32/rv64im. */
bool minic_riscv_isa_resolve(const char *march, const char *mabi,
                             MinicRiscvIsa *isa, char *reason, size_t capacity);

/* Validate the actual selected RV64I machine assembly, including implicit
 * prologue, address lowering, helper and future emitter instructions. */
bool minic_riscv64_check_base_i_assembly(const char *path, char *reason,
                                          size_t capacity);

#endif
