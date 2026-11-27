#ifndef MINIAS_H
#define MINIAS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Explicit RV64I opt-in: validate expansion, encoding, and executable
 * data against the true base-I machine ISA, not only the ELF flags. */
int minias_assemble_file_target_isa(const char *input_path,
                                    const char *output_path,
                                    bool elf32,
                                    uint32_t elf_flags,
                                    unsigned strict_i_xlen,
                                    FILE *diagnostic);

#endif
