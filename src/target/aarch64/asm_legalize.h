#ifndef MINIC_TARGET_AARCH64_ASM_LEGALIZE_H
#define MINIC_TARGET_AARCH64_ASM_LEGALIZE_H
#include <stdbool.h>
#include <stdio.h>
/* Target GNU text legalizer; fixed frame x19 and temporary scratch x12
 * remain AArch64-only machine details, not Core IR or generic LP64. */
#define MINIC_LARGE_FRAME_MAX 1048576U
bool minic_aarch64_legalize_large_sp_memory(const char *path);
bool minic_aarch64_copy_dynamic_frame_asm(FILE *out, FILE *input);
#endif
