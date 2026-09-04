#ifndef MINILD_H
#define MINILD_H

#include <stdbool.h>
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

typedef struct MiniLdStaticOptions {
    const char *entry_symbol;
    const char *script_path;
} MiniLdStaticOptions;

typedef struct MiniLdSharedOptions {
    const char *soname;
    const char *entry_symbol;
    const char *dynamic_list_path;
    const char *needed_name;
    const char *interpreter_path;
    bool pie;
} MiniLdSharedOptions;

int minild_link_relocatable_elf64_riscv(const char *output_path,
                                        const char *const *input_paths,
                                        size_t input_count,
                                        FILE *diagnostics);

int minild_link_relocatable_elf64_riscv_inputs(const char *output_path,
                                               const MiniLdInput *inputs,
                                               size_t input_count,
                                               FILE *diagnostics);

int minild_link_static_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    FILE *diagnostics);

int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics);

int minild_link_shared_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdSharedOptions *options,
    FILE *diagnostics);

int minild_link_shared_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *soname,
                                          const char *entry_symbol,
                                          FILE *diagnostics);

#endif
