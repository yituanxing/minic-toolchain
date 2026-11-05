#ifndef MINILD_H
#define MINILD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
    /* GNU -n/--nmagic: script file layout uses section alignment rather
     * than maximum-page alignment. This is not GNU -N/--omagic. */
    bool nmagic;
    /* GNU -X: omit temporary .L* locals from the final ET_EXEC symtab only. */
    bool discard_locals;
    /* Kernel ET_DYN is a statically resolved image, not an ELF DSO. */
    bool output_et_dyn;
    /* Preserve the original static -pie request even when GNU -n forces
     * the emitted ELF type to ET_EXEC.  Target synthetic relocations (for
     * example LoongArch KASLR R_LARCH_RELATIVE) depend on this semantic,
     * not the ELF header's e_type. */
    bool static_pie_requested;
    /* Architecture erratum repair must be applied after final placement. */
    bool fix_cortex_a53_843419;
    /* GNU -z max-page-size, zero requests the Target ABI default. */
    uint64_t max_page_size;
    /* GNU --orphan-handling=error rejects unmatched alloc input sections. */
    bool orphan_handling_error;
    /* GNU --build-id=sha1: materialize an ELF note and hash the image. */
    bool build_id_sha1;
    /* AArch64 -shared -Bsymbolic --no-apply-dynamic-relocs cannot silently
     * consume fixups which instead require emitted runtime .rela.dyn. */
    bool kernel_shared_no_dynamic_relocations;
    /* GNU -q: retain final static relocation records. */
    bool emit_relocs;
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
bool minild_emulation_supports_kernel_shared_image(const char *emulation);
/* A linker -EL/-EB request selects the output byte order, which must agree
 * with the target's declared ELF encoding. The CLI never guesses from the
 * host machine or silently changes a target to an unsupported endian ABI. */
bool minild_emulation_supports_endianness(const char *emulation,
                                          bool little_endian);

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
