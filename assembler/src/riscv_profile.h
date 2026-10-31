#ifndef MINIAS_RISCV_PROFILE_H
#define MINIAS_RISCV_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum MiniAsRiscvFeature {
    RISCV_ISA_I = UINT64_C(1) << 0,
    RISCV_ISA_E = UINT64_C(1) << 1,
    RISCV_ISA_M = UINT64_C(1) << 2,
    RISCV_ISA_A = UINT64_C(1) << 3,
    RISCV_ISA_F = UINT64_C(1) << 4,
    RISCV_ISA_D = UINT64_C(1) << 5,
    RISCV_ISA_Q = UINT64_C(1) << 6,
    RISCV_ISA_C = UINT64_C(1) << 7,
    RISCV_ISA_V = UINT64_C(1) << 8,
    RISCV_ISA_ZICSR = UINT64_C(1) << 9,
    RISCV_ISA_ZIFENCEI = UINT64_C(1) << 10,
    RISCV_ISA_ZBB = UINT64_C(1) << 11
} MiniAsRiscvFeature;

typedef struct MiniAsRiscvProfile {
    bool elf32;
    uint32_t elf_flags;
    unsigned strict_i_xlen;
    bool explicit_isa;
    uint64_t features;
    /* Canonical target ISA spelling, not a serialized ELF vendor section.
     * Object output owns the one and only attribute serializer. */
    char arch_attribute_isa[512];
} MiniAsRiscvProfile;

bool minias_riscv_resolve_profile(const char *march, const char *mabi,
                                  MiniAsRiscvProfile *out,
                                  char *error, size_t capacity);
#endif
