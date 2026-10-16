#ifndef MINILD_ELF32_VDSO_H
#define MINILD_ELF32_VDSO_H
#include "minild.h"

/* Narrow, fail-closed ELF32 RISC-V compat-vDSO real shared linker.
 * Caller must select -melf32lriscv; this never falls back to GNU LD. */
int minild_link_elf32_compat_vdso(const char *output,
                                  const MiniLdInput *inputs,
                                  size_t count,
                                  const MiniLdSharedOptions *options,
                                  FILE *diagnostics);
#endif
