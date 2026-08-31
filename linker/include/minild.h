#ifndef MINILD_H
#define MINILD_H

#include <stddef.h>
#include <stdio.h>

int minild_link_relocatable_elf64_riscv(const char *output_path,
                                        const char *const *input_paths,
                                        size_t input_count,
                                        FILE *diagnostics);

#endif
