#ifndef MINILD_H
#define MINILD_H

#include <stddef.h>
#include <stdio.h>

typedef enum MiniLdInputKind {
    MINILD_INPUT_OBJECT = 0,
    MINILD_INPUT_WHOLE_ARCHIVE = 1,
    MINILD_INPUT_GROUP_ARCHIVE = 2,
    MINILD_INPUT_ARCHIVE = 3
} MiniLdInputKind;

typedef struct MiniLdInput {
    const char *path;
    MiniLdInputKind kind;
} MiniLdInput;

int minild_link_relocatable_elf64_riscv(const char *output_path,
                                        const char *const *input_paths,
                                        size_t input_count,
                                        FILE *diagnostics);

int minild_link_relocatable_elf64_riscv_inputs(const char *output_path,
                                               const MiniLdInput *inputs,
                                               size_t input_count,
                                               FILE *diagnostics);

int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics);

#endif
