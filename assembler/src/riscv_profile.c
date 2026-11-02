#include "riscv_profile.h"
#include <stdio.h>
#include <string.h>

enum {
    EF_RVC = 1U, EF_FLOAT_MASK = 6U, EF_FLOAT_SINGLE = 2U,
    EF_FLOAT_DOUBLE = 4U, EF_FLOAT_QUAD = 6U, EF_RVE = 8U
};

static bool reject(char *error, size_t cap, const char *reason,
                   const char *value) {
    if (cap != 0U) (void)snprintf(error, cap, "%s:%s", reason, value);
    return false;
}

static bool append_isa(char *buf, size_t cap, size_t *len,
                       const char *feature) {
    size_t n = strlen(feature);
    if (*len > cap || n + 1U > cap - *len) return false;
    memcpy(buf + *len, feature, n + 1U);
    *len += n;
    return true;
}

/* A single Target-private extension inventory drives source recognition and
 * canonical ELF Tag_RISCV_arch spelling. Extension letter, long spelling,
 * feature bit, and standardized version suffix must not drift apart. */
typedef struct RiscvExtensionSpec {
    uint64_t bit;
    char letter;
    const char *long_name;
    const char *attribute_suffix;
} RiscvExtensionSpec;
static const RiscvExtensionSpec riscv_extensions[] = {
    {RISCV_ISA_M, 'm', NULL, "_m2p0"},
    {RISCV_ISA_A, 'a', NULL, "_a2p1"},
    {RISCV_ISA_F, 'f', NULL, "_f2p2"},
    {RISCV_ISA_D, 'd', NULL, "_d2p2"},
    {RISCV_ISA_Q, 'q', NULL, "_q2p2"},
    {RISCV_ISA_C, 'c', NULL, "_c2p0"},
    {RISCV_ISA_V, 'v', NULL, "_v1p0"},
    {RISCV_ISA_ZICSR, '\0', "zicsr", "_zicsr2p0"},
    {RISCV_ISA_ZIFENCEI, '\0', "zifencei", "_zifencei2p0"},
    {RISCV_ISA_ZBB, '\0', "zbb", "_zbb1p0"},
    {RISCV_ISA_ZIHINTPAUSE, '\0', "zihintpause", "_zihintpause2p0"}
};

static uint64_t riscv_lookup_extension(char letter, const char *spelling,
                                       size_t length) {
    size_t i;
    for (i = 0U; i < sizeof(riscv_extensions) /
                        sizeof(riscv_extensions[0]); ++i) {
        const RiscvExtensionSpec *ext = &riscv_extensions[i];
        if (letter != '\0' && ext->letter == letter) return ext->bit;
        if (spelling != NULL && ext->long_name != NULL &&
            strlen(ext->long_name) == length &&
            strncmp(spelling, ext->long_name, length) == 0)
            return ext->bit;
    }
    return 0U;
}

uint64_t minias_riscv_lookup_site_extension(const char *name,size_t length) {
    uint64_t feature;
    if(name==NULL || length==0U)return 0U;
    feature=length==1U?riscv_lookup_extension(name[0],NULL,0U):
                        riscv_lookup_extension('\0',name,length);
    /* Only these two lexical additions have instruction and GNU oracle
     * coverage. Other inventory entries are recognized for -march, but
     * must not silently become untested .option arch permissions. */
    return (feature==RISCV_ISA_ZBB || feature==RISCV_ISA_V)?feature:0U;
}

uint64_t minias_riscv_site_features(uint64_t file_features,
                                   uint64_t site_additions,bool no_rvc) {
    uint64_t features=file_features|site_additions;
    if((features&RISCV_ISA_V)!=0U)
        features|=RISCV_ISA_F|RISCV_ISA_D|RISCV_ISA_ZICSR;
    if(no_rvc)features&=~(uint64_t)RISCV_ISA_C;
    return features;
}

static bool append_mapping_suffix(char *out,size_t cap,size_t *length,
                                  const char *suffix) {
    return append_isa(out,cap,length,suffix);
}

/* The supported profile inventory has exactly one version per file-level
 * extension. GNU 2.42's local +v instead inserts F2p0/D2p0 and the full
 * vector-implied Zve/Zvl list. Build this *projection* from the file profile
 * and the stable per-Fragment delta, never mutate Tag_RISCV_arch. */
bool minias_riscv_mapping_isa(const char *file_isa,uint64_t file_features,
                              uint64_t site_additions,bool no_rvc,
                              char *out,size_t capacity) {
    static const char *const vector_dependencies[]={
        "_zve32f1p0","_zve32x1p0","_zve64d1p0",
        "_zve64f1p0","_zve64x1p0",
        "_zvl128b1p0","_zvl32b1p0","_zvl64b1p0"
    };
    const uint64_t effective=minias_riscv_site_features(
        file_features,site_additions,no_rvc);
    const char *first_extension;
    size_t len, i;
    bool has_v=(effective&RISCV_ISA_V)!=0U;
    if(file_isa==NULL || out==NULL || capacity==0U)return false;
    first_extension=strchr(file_isa,'_');
    len=first_extension!=NULL?(size_t)(first_extension-file_isa):
        strlen(file_isa);
    if(len==0U || len>=capacity)return false;
    memcpy(out,file_isa,len);
    out[len]='\0';
    /* Single-letter canonical order matches the Target's extension table. */
    for(i=0U;i<sizeof(riscv_extensions)/sizeof(riscv_extensions[0]);++i) {
        const RiscvExtensionSpec *ext=&riscv_extensions[i];
        const char *suffix=ext->attribute_suffix;
        if(ext->letter=='\0' || (effective&ext->bit)==0U)continue;
        if((ext->bit==RISCV_ISA_F || ext->bit==RISCV_ISA_D) &&
           (file_features&ext->bit)==0U && has_v)
            suffix=ext->bit==RISCV_ISA_F?"_f2p0":"_d2p0";
        if(!append_mapping_suffix(out,capacity,&len,suffix))return false;
    }
    /* GNU's implied Zmmul follows the initial Zicsr/Zifencei group but
     * precedes explicit Zbb. Both rv64gc and rv64im_zbb need this order;
     * blindly appending Zmmul (or putting it first) breaks one of them. */
    if((file_features&RISCV_ISA_ZICSR)!=0U &&
       !append_mapping_suffix(out,capacity,&len,"_zicsr2p0"))return false;
    if((file_features&RISCV_ISA_ZIFENCEI)!=0U &&
       !append_mapping_suffix(out,capacity,&len,"_zifencei2p0"))return false;
    if((effective&RISCV_ISA_M)!=0U &&
       !append_mapping_suffix(out,capacity,&len,"_zmmul1p0"))return false;
    /* The +v Zicsr *parse permission* does not add the file-level Zicsr
     * token. Keep that source/ELF distinction even when composing mappings. */
    if((effective&RISCV_ISA_ZBB)!=0U &&
       !append_mapping_suffix(out,capacity,&len,"_zbb1p0"))return false;
    if((file_features&RISCV_ISA_ZIHINTPAUSE)!=0U &&
       !append_mapping_suffix(out,capacity,&len,"_zihintpause2p0"))return false;
    if(has_v)for(i=0U;i<sizeof(vector_dependencies)/
                             sizeof(vector_dependencies[0]);++i)
        if(!append_mapping_suffix(out,capacity,&len,vector_dependencies[i]))
            return false;
    return true;
}

static bool parse_isa(const char *arch, MiniAsRiscvProfile *out,
                      char *error, size_t cap) {
    uint64_t features = 0U;
    const char *p;
    unsigned xlen;
    size_t i, used;
    char normalized[512];
    if (strncmp(arch, "rv32", 4U) == 0) xlen = 32U;
    else if (strncmp(arch, "rv64", 4U) == 0) xlen = 64U;
    else return reject(error, cap, "unsupported-arch", arch);
    p = arch + 4U;
    if (*p == 'i') features = RISCV_ISA_I;
    else if (*p == 'e' && xlen == 32U) features = RISCV_ISA_E;
    else if (*p == 'g') {
        features = RISCV_ISA_I | RISCV_ISA_M | RISCV_ISA_A |
                   RISCV_ISA_F | RISCV_ISA_D |
                   RISCV_ISA_ZICSR | RISCV_ISA_ZIFENCEI;
    } else return reject(error, cap, "unsupported-riscv-base", arch);
    ++p;
    while (*p != '\0' && *p != '_') {
        uint64_t ext = riscv_lookup_extension(*p, NULL, 0U);
        if (ext == 0U)
            return reject(error, cap, "unqualified-riscv-extension", arch);
        if ((features & ext) != 0U)
            return reject(error, cap, "duplicate-riscv-extension", arch);
        features |= ext;
        ++p;
    }
    while (*p == '_') {
        const char *start = ++p;
        size_t n;
        uint64_t ext;
        while (*p != '\0' && *p != '_') ++p;
        n = (size_t)(p - start);
        ext = riscv_lookup_extension('\0', start, n);
        if (ext == 0U)
            return reject(error, cap, "unqualified-riscv-extension", arch);
        if ((features & ext) != 0U)
            return reject(error, cap, "duplicate-riscv-extension", arch);
        features |= ext;
    }
    if ((features & RISCV_ISA_D) && !(features & RISCV_ISA_F))
        return reject(error, cap, "riscv-d-requires-f", arch);
    if ((features & RISCV_ISA_Q) && !(features & RISCV_ISA_D))
        return reject(error, cap, "riscv-q-requires-d", arch);
    if ((features & RISCV_ISA_E) &&
        (features & (RISCV_ISA_F | RISCV_ISA_D | RISCV_ISA_Q | RISCV_ISA_V)))
        return reject(error, cap, "unsupported-riscv-e-extension-combination", arch);

    out->elf32 = xlen == 32U;
    out->features = features;
    out->explicit_isa = true;
    /* -march selects legal typed instructions; it does not opt in to a
     * raw executable-byte audit. Only the explicit legacy audit entry point
     * may set strict_i_xlen, independently of the selected ISA. */
    if ((features & RISCV_ISA_C) != 0U) out->elf_flags |= EF_RVC;
    used = (size_t)snprintf(normalized, sizeof(normalized), "rv%u%s",
                           xlen, (features & RISCV_ISA_E) ? "e2p0" : "i2p1");
    if (used >= sizeof(normalized))
        return reject(error, cap, "riscv-arch-too-long", arch);
    for (i = 0U; i < sizeof(riscv_extensions) /
                      sizeof(riscv_extensions[0]); ++i)
        if ((features & riscv_extensions[i].bit) != 0U &&
            !append_isa(normalized, sizeof(normalized),
                        &used, riscv_extensions[i].attribute_suffix))
            return reject(error, cap, "riscv-arch-too-long", arch);
    if (strlen(normalized) + 1U > sizeof(out->arch_attribute_isa))
        return reject(error, cap, "riscv-arch-too-long", arch);
    memcpy(out->arch_attribute_isa, normalized, strlen(normalized) + 1U);
    return true;
}

bool minias_riscv_resolve_profile(const char *march, const char *mabi,
                                  MiniAsRiscvProfile *out,
                                  char *error, size_t cap) {
    unsigned xlen = 0U, float_abi = 0U;
    bool eabi = false;
    memset(out, 0, sizeof(*out));
    if (cap != 0U) error[0] = '\0';
    if (march != NULL && !parse_isa(march, out, error, cap)) return false;
    if (mabi != NULL) {
        const char *suffix;
        if (strncmp(mabi, "ilp32", 5U) == 0) {
            xlen = 32U; suffix = mabi + 5U;
        } else if (strncmp(mabi, "lp64", 4U) == 0) {
            xlen = 64U; suffix = mabi + 4U;
        } else return reject(error, cap, "unsupported-abi", mabi);
        if (*suffix == '\0') float_abi = 0U;
        else if (strcmp(suffix, "f") == 0) float_abi = EF_FLOAT_SINGLE;
        else if (strcmp(suffix, "d") == 0) float_abi = EF_FLOAT_DOUBLE;
        else if (strcmp(suffix, "q") == 0) float_abi = EF_FLOAT_QUAD;
        else if (strcmp(suffix, "e") == 0 && xlen == 32U) eabi = true;
        else return reject(error, cap, "unsupported-abi", mabi);
        if (march != NULL &&
            (out->elf32 != (xlen == 32U) ||
             (((out->features & RISCV_ISA_E) != 0U) != eabi) ||
             (float_abi >= EF_FLOAT_SINGLE && !(out->features & RISCV_ISA_F)) ||
             (float_abi >= EF_FLOAT_DOUBLE && !(out->features & RISCV_ISA_D)) ||
             (float_abi == EF_FLOAT_QUAD && !(out->features & RISCV_ISA_Q))))
            return reject(error, cap, "incompatible-riscv-isa-abi", mabi);
        out->elf32 = xlen == 32U;
        out->elf_flags &= ~(uint32_t)(EF_FLOAT_MASK | EF_RVE);
        out->elf_flags |= float_abi | (eabi ? EF_RVE : 0U);
    } else if (march != NULL && (out->features & RISCV_ISA_E)) {
        out->elf_flags |= EF_RVE;
    }
    return true;
}
