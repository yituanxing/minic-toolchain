#ifndef MINIAS_RISCV_PROFILE_H
#define MINIAS_RISCV_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct MiniAsRiscvProfile {
    bool elf32;
    uint32_t elf_flags;
    unsigned strict_i_xlen;
} MiniAsRiscvProfile;
bool minias_riscv_resolve_profile(const char *march, const char *mabi,
                                  MiniAsRiscvProfile *out,
                                  char *error, size_t capacity);
#endif
