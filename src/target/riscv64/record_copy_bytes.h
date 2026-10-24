#ifndef MINIC_RISCV_RECORD_COPY_BYTES_H
#define MINIC_RISCV_RECORD_COPY_BYTES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
/* Shared RISC-V byte-exact aggregate copy for XLEN=32 and XLEN=64.
 * Caller chooses semantic extent via its own TargetDataLayout and supplies
 * two pointer registers. Uses scratch t2/t3; does not use M/FP instructions. */
bool minic_riscv_emit_record_copy_bytes(FILE *out,
                                        const char *destination_reg,
                                        const char *source_reg,
                                        size_t size);
#endif
