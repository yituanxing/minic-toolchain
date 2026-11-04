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
    bool gc_sections;
} MiniLdStaticOptions;

typedef struct MiniLdSharedOptions {
    const char *soname;
    const char *entry_symbol;
    const char *dynamic_list_path;
    const char *version_script_path;
    const char *script_path;
    const char *needed_name;
    const char *interpreter_path;
    bool strip_debug;
    bool hash_style_both;
    bool eh_frame_hdr;
    bool pie;
} MiniLdSharedOptions;

/* A link target describes the OUTPUT ELF ABI, independently of the host
 * running minic-ld. Unsupported link modes must fail before writing output. */
typedef enum MiniLdLinkMode {
    MINILD_LINK_RELOCATABLE,
    MINILD_LINK_STATIC,
    MINILD_LINK_SHARED,
    MINILD_LINK_PIE
} MiniLdLinkMode;

bool minild_emulation_supports_mode(const char *emulation,
                                    MiniLdLinkMode mode);

int minild_link_relocatable_inputs_target(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *emulation,
                                          FILE *diagnostics);

int minild_link_static_elf64_inputs_target(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    const char *emulation,
    FILE *diagnostics);

int minild_link_shared_inputs_target(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdSharedOptions *options,
    const char *emulation,
    FILE *diagnostics);

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
