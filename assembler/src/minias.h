#ifndef MINIAS_H
#define MINIAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Public invocation contract. Callers select a target by its registered
 * name and pass ISA/ABI spelling through unchanged. Backend profiles,
 * ELF class, feature state and relocations remain private to MiniAS. */
typedef struct MiniAsInvocation {
    const char *target;
    const char *march;
    const char *mabi;
} MiniAsInvocation;

typedef enum MiniAsLinkRelaxMode {
    MINIAS_LINK_RELAX_TARGET_DEFAULT = 0,
    MINIAS_LINK_RELAX_ENABLE = 1,
    MINIAS_LINK_RELAX_DISABLE = 2
} MiniAsLinkRelaxMode;

/* The target list is obtained from the one internal Target registry.
 * Adding a backend never requires a matching CLI switch/help edit. */
size_t minias_supported_target_count(void);
const char *minias_supported_target_name(size_t index);
int minias_assemble_invocation(const char *input_path,
                               const char *output_path,
                               const MiniAsInvocation *invocation,
                               FILE *diagnostic);
/* Opt-in ELF output policy without changing the historical three-field
 * MiniAsInvocation ABI or breaking existing aggregate initializers. */
int minias_assemble_invocation_with_policy(const char *input_path,
                                          const char *output_path,
                                          const MiniAsInvocation *invocation,
                                          bool suppress_target_attributes,
                                          FILE *diagnostic);
/* The stable three-field Invocation remains source/ABI compatible. New
 * TargetSpec transport options use an independent extended invocation API. */
int minias_assemble_invocation_with_options(const char *input_path,
                                           const char *output_path,
                                           const MiniAsInvocation *invocation,
                                           MiniAsLinkRelaxMode link_relax,
                                           bool suppress_target_attributes,
                                           FILE *diagnostic);

int minias_assemble_file(const char *input_path, const char *output_path, FILE *diagnostic);
int minias_assemble_file_class(const char *input_path,
                               const char *output_path,
                               bool elf32,
                               FILE *diagnostic);
/* Legacy explicit audit API: validate expansion, encoding, and executable
 * data against the true base-I machine ISA. Ordinary -march=rv{32,64}i does
 * not request this additional raw-byte audit. */
int minias_assemble_file_target_isa(const char *input_path,
                                    const char *output_path,
                                    bool elf32,
                                    uint32_t elf_flags,
                                    unsigned strict_i_xlen,
                                    FILE *diagnostic);
int minias_assemble_file_target(const char *input_path,
                                const char *output_path,
                                bool elf32,
                                uint32_t elf_flags,
                                FILE *diagnostic);

#endif
