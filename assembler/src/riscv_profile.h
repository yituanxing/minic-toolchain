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
    RISCV_ISA_ZBB = UINT64_C(1) << 11,
    RISCV_ISA_ZIHINTPAUSE = UINT64_C(1) << 12
} MiniAsRiscvFeature;

/* One private extension order is shared by feature resolution, versions,
 * file attributes and per-site GNU mapping symbols.  Only versions with
 * verified encoding/ABI semantics are accepted; others fail closed. */
enum { MINIAS_RISCV_EXTENSION_COUNT = 11 };
typedef struct MiniAsRiscvVersion {
    uint16_t major;
    uint16_t minor;
} MiniAsRiscvVersion;

typedef struct MiniAsRiscvISA {
    uint64_t features;
    unsigned xlen;
    bool base_e;
    MiniAsRiscvVersion base_version;
    MiniAsRiscvVersion extension_versions[MINIAS_RISCV_EXTENSION_COUNT];
} MiniAsRiscvISA;

typedef struct MiniAsRiscvProfile {
    bool elf32;
    uint32_t elf_flags;
    unsigned strict_i_xlen;
    bool explicit_isa;
    MiniAsRiscvISA isa;
    /* Canonical target ISA spelling, not a serialized ELF vendor section.
     * Object output owns the one and only attribute serializer. */
    char arch_attribute_isa[512];
} MiniAsRiscvProfile;

bool minias_riscv_resolve_profile(const char *march, const char *mabi,
                                  MiniAsRiscvProfile *out,
                                  char *error, size_t capacity);
/* One Target-private extension inventory serves -march and supported lexical
 * .option arch,+extension spellings. Unrecognized site additions fail closed. */
uint64_t minias_riscv_lookup_site_extension(const char *name,size_t length);
uint64_t minias_riscv_site_features(uint64_t file_features,
                                   uint64_t site_additions,bool no_rvc);
/* GNU GAS 2.42 per-instruction mapping ISA; never changes file attributes.
 * The V dependency closure in mapping names carries its own default version
 * numbers, distinct from the file-wide profile's F/D defaults. */
bool minias_riscv_mapping_isa(const MiniAsRiscvISA *file_isa,
                              uint64_t site_additions,bool no_rvc,
                              char *out,size_t capacity);
#endif
