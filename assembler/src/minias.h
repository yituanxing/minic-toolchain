#ifndef MINIAS_H
#define MINIAS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int minias_assemble_file(const char *input_path, const char *output_path, FILE *diagnostic);
int minias_assemble_file_class(const char *input_path,
                               const char *output_path,
                               bool elf32,
                               FILE *diagnostic);
int minias_assemble_file_target(const char *input_path,
                                const char *output_path,
                                bool elf32,
                                uint32_t elf_flags,
                                FILE *diagnostic);

#endif
