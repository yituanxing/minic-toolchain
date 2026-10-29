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

/* The target list is obtained from the one internal Target registry.
 * Adding a backend never requires a matching CLI switch/help edit. */
size_t minias_supported_target_count(void);
const char *minias_supported_target_name(size_t index);
int minias_assemble_invocation(const char *input_path,
                               const char *output_path,
                               const MiniAsInvocation *invocation,
                               FILE *diagnostic);

int minias_assemble_file(const char *input_path, const char *output_path, FILE *diagnostic);
int minias_assemble_file_class(const char *input_path,
                               const char *output_path,
                               bool elf32,
                               FILE *diagnostic);
/* Explicit RV64I opt-in: validate expansion, encoding, and executable
 * data against the true base-I machine ISA, not only the ELF flags. */
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
