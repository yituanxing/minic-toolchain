#ifndef MINIC_TARGET_RISCV_ISA_RESOLVE_H
#define MINIC_TARGET_RISCV_ISA_RESOLVE_H

#include "target/riscv/isa_features.h"
#include <stddef.h>
#include <stdbool.h>

/* Shared RISC-V target ISA/ABI negotiation for both XLENs. This never
 * permits extra compiler-generated opcodes simply because GNU AS accepts
 * source-owned inline assembly from an extended profile. */
bool minic_riscv_isa_resolve(const char *march, const char *mabi,
                            MinicRiscvIsa *isa, char *reason, size_t capacity);

#endif
