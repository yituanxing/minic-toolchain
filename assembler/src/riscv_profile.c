#include "riscv_profile.h"
#include <ctype.h>
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
    MiniAsRiscvVersion version;
} RiscvExtensionSpec;
static const RiscvExtensionSpec riscv_extensions[] = {
    {RISCV_ISA_M, 'm', NULL, {2U,0U}},
    {RISCV_ISA_A, 'a', NULL, {2U,1U}},
    {RISCV_ISA_F, 'f', NULL, {2U,2U}},
    {RISCV_ISA_D, 'd', NULL, {2U,2U}},
    {RISCV_ISA_Q, 'q', NULL, {2U,2U}},
    {RISCV_ISA_C, 'c', NULL, {2U,0U}},
    {RISCV_ISA_V, 'v', NULL, {1U,0U}},
    {RISCV_ISA_ZICSR, '\0', "zicsr", {2U,0U}},
    {RISCV_ISA_ZIFENCEI, '\0', "zifencei", {2U,0U}},
    {RISCV_ISA_ZIHINTPAUSE, '\0', "zihintpause", {2U,0U}},
    {RISCV_ISA_ZBB, '\0', "zbb", {1U,0U}}
};
_Static_assert(sizeof(riscv_extensions)/sizeof(riscv_extensions[0])==
               MINIAS_RISCV_EXTENSION_COUNT,"RISC-V version inventory drift");

static bool riscv_version_matches(const char *text,size_t length,
                                  MiniAsRiscvVersion version) {
    char expected[32];
    int n=snprintf(expected,sizeof(expected),"%up%u",
                   (unsigned)version.major,(unsigned)version.minor);
    return n>=0 && (size_t)n==length &&
           memcmp(text,expected,length)==0;
}

static bool riscv_consume_version(const char **text,
                                  MiniAsRiscvVersion version) {
    const char *begin=*text,*p=begin;
    if(!isdigit((unsigned char)*p))return true;
    while(isdigit((unsigned char)*p))++p;
    if(*p++!='p' || !isdigit((unsigned char)*p))return false;
    while(isdigit((unsigned char)*p))++p;
    if(!riscv_version_matches(begin,(size_t)(p-begin),version))return false;
    *text=p;
    return true;
}

static const RiscvExtensionSpec *riscv_lookup_letter(char letter) {
    size_t i;
    for (i = 0U; i < sizeof(riscv_extensions) /
                        sizeof(riscv_extensions[0]); ++i) {
        const RiscvExtensionSpec *ext = &riscv_extensions[i];
        if (ext->letter != '\0' && ext->letter == letter)return ext;
    }
    return NULL;
}

static const RiscvExtensionSpec *riscv_lookup_name(const char *spelling,
                                                   size_t length,
                                                   bool *invalid_version) {
    size_t i;
    for(i=0U;i<MINIAS_RISCV_EXTENSION_COUNT;++i) {
        const RiscvExtensionSpec *ext=&riscv_extensions[i];
        size_t name_length;
        if(ext->long_name==NULL)continue;
        name_length=strlen(ext->long_name);
        if(length<name_length ||
           strncmp(spelling,ext->long_name,name_length)!=0)continue;
        if(length==name_length ||
           riscv_version_matches(spelling+name_length,length-name_length,
                                 ext->version))return ext;
        *invalid_version=true;
    }
    return NULL;
}

static bool riscv_append_extension(char *out,size_t cap,size_t *length,
                                   const RiscvExtensionSpec *ext,
                                   MiniAsRiscvVersion version) {
    int n;
    if(*length>=cap)return false;
    if(ext->letter!='\0')
        n=snprintf(out+*length,cap-*length,"_%c%up%u",ext->letter,
                   (unsigned)version.major,(unsigned)version.minor);
    else n=snprintf(out+*length,cap-*length,"_%s%up%u",ext->long_name,
                    (unsigned)version.major,(unsigned)version.minor);
    if(n<0 || (size_t)n>=cap-*length)return false;
    *length+=(size_t)n;
    return true;
}

static bool riscv_append_feature(char *out,size_t cap,size_t *length,
                                 const MiniAsRiscvISA *isa,uint64_t bit,
                                 const MiniAsRiscvVersion *override) {
    size_t i;
    for(i=0U;i<MINIAS_RISCV_EXTENSION_COUNT;++i)
        if(riscv_extensions[i].bit==bit)
            return riscv_append_extension(out,cap,length,&riscv_extensions[i],
                   override!=NULL?*override:isa->extension_versions[i]);
    return false;
}

static bool riscv_format_base(char *out,size_t capacity,size_t *length,
                              const MiniAsRiscvISA *isa) {
    int n;
    if(out==NULL || capacity==0U ||
       (isa->xlen!=32U && isa->xlen!=64U))return false;
    n=snprintf(out,capacity,"rv%u%c%up%u",isa->xlen,
               isa->base_e?'e':'i',(unsigned)isa->base_version.major,
               (unsigned)isa->base_version.minor);
    if(n<0 || (size_t)n>=capacity)return false;
    *length=(size_t)n;
    return true;
}

static void riscv_default_versions(MiniAsRiscvISA *isa) {
    size_t i;
    isa->base_version=(MiniAsRiscvVersion){2U,1U};
    for(i=0U;i<MINIAS_RISCV_EXTENSION_COUNT;++i)
        isa->extension_versions[i]=riscv_extensions[i].version;
}

uint64_t minias_riscv_lookup_site_extension(const char *name,size_t length) {
    const RiscvExtensionSpec *ext=NULL;
    bool invalid_version=false;
    if(name==NULL || length==0U)return 0U;
    if(name[0]=='v' && length>0U) {
        ext=riscv_lookup_letter('v');
        if(length>1U &&
           !riscv_version_matches(name+1U,length-1U,ext->version))ext=NULL;
    } else ext=riscv_lookup_name(name,length,&invalid_version);
    /* Only these two lexical additions have instruction and GNU oracle
     * coverage. Other inventory entries are recognized for -march, but
     * must not silently become untested .option arch permissions. */
    return ext!=NULL && (ext->bit==RISCV_ISA_ZBB ||
                         ext->bit==RISCV_ISA_V)?ext->bit:0U;
}

uint64_t minias_riscv_site_features(uint64_t file_features,
                                   uint64_t site_additions,bool no_rvc) {
    uint64_t features=file_features|site_additions;
    if((features&RISCV_ISA_V)!=0U)
        features|=RISCV_ISA_F|RISCV_ISA_D|RISCV_ISA_ZICSR;
    if(no_rvc)features&=~(uint64_t)RISCV_ISA_C;
    return features;
}

/* The supported profile inventory has exactly one version per file-level
 * extension. GNU 2.42's local +v instead inserts F2p0/D2p0 and the full
 * vector-implied Zve/Zvl list. Build this *projection* from the file profile
 * and the stable per-Fragment delta, never mutate Tag_RISCV_arch. */
bool minias_riscv_mapping_isa(const MiniAsRiscvISA *file_isa,
                              uint64_t site_additions,bool no_rvc,
                              char *out,size_t capacity) {
    static const char *const vector_dependencies[]={
        "_zve32f1p0","_zve32x1p0","_zve64d1p0",
        "_zve64f1p0","_zve64x1p0",
        "_zvl128b1p0","_zvl32b1p0","_zvl64b1p0"
    };
    const uint64_t file_features=file_isa!=NULL?file_isa->features:0U;
    const uint64_t effective=minias_riscv_site_features(
        file_features,site_additions,no_rvc);
    const MiniAsRiscvVersion implied_vector_f_d={2U,0U};
    size_t len, i;
    bool has_v=(effective&RISCV_ISA_V)!=0U;
    if(file_isa==NULL || out==NULL || capacity==0U)return false;
    if(!riscv_format_base(out,capacity,&len,file_isa))return false;
    /* Single-letter canonical order matches the Target's extension table. */
    for(i=0U;i<sizeof(riscv_extensions)/sizeof(riscv_extensions[0]);++i) {
        const RiscvExtensionSpec *ext=&riscv_extensions[i];
        MiniAsRiscvVersion version=file_isa->extension_versions[i];
        if(ext->letter=='\0' || (effective&ext->bit)==0U)continue;
        if((ext->bit==RISCV_ISA_F || ext->bit==RISCV_ISA_D) &&
           (file_features&ext->bit)==0U && has_v)
            version=implied_vector_f_d;
        if(!riscv_append_extension(out,capacity,&len,ext,version))return false;
    }
    /* GNU's canonical named-extension group puts Zihintpause before the
     * implied Zmmul, and Zbb after it. The explicit versioned ISA oracle
     * also covers Zihintpause and Zbb together rather than only alone. */
    if((file_features&RISCV_ISA_ZICSR)!=0U &&
       !riscv_append_feature(out,capacity,&len,file_isa,RISCV_ISA_ZICSR,NULL))return false;
    if((file_features&RISCV_ISA_ZIFENCEI)!=0U &&
       !riscv_append_feature(out,capacity,&len,file_isa,RISCV_ISA_ZIFENCEI,NULL))return false;
    if((file_features&RISCV_ISA_ZIHINTPAUSE)!=0U &&
       !riscv_append_feature(out,capacity,&len,file_isa,RISCV_ISA_ZIHINTPAUSE,NULL))return false;
    if((effective&RISCV_ISA_M)!=0U &&
       !append_isa(out,capacity,&len,"_zmmul1p0"))return false;
    /* The +v Zicsr *parse permission* does not add the file-level Zicsr
     * token. Keep that source/ELF distinction even when composing mappings. */
    if((effective&RISCV_ISA_ZBB)!=0U &&
       !riscv_append_feature(out,capacity,&len,file_isa,RISCV_ISA_ZBB,NULL))return false;
    if(has_v)for(i=0U;i<sizeof(vector_dependencies)/
                             sizeof(vector_dependencies[0]);++i)
        if(!append_isa(out,capacity,&len,vector_dependencies[i]))
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
    riscv_default_versions(&out->isa);
    out->isa.xlen=xlen;
    if (*p == 'i') features = RISCV_ISA_I;
    else if (*p == 'e' && xlen == 32U) {
        features = RISCV_ISA_E;
        out->isa.base_e=true;
        out->isa.base_version=(MiniAsRiscvVersion){2U,0U};
    }
    else if (*p == 'g') {
        features = RISCV_ISA_I | RISCV_ISA_M | RISCV_ISA_A |
                   RISCV_ISA_F | RISCV_ISA_D |
                   RISCV_ISA_ZICSR | RISCV_ISA_ZIFENCEI;
    } else return reject(error, cap, "unsupported-riscv-base", arch);
    ++p;
    if(!riscv_consume_version(&p,out->isa.base_version))
        return reject(error,cap,"unsupported-riscv-base-version",arch);
    while (*p != '\0' && *p != '_') {
        const RiscvExtensionSpec *ext=riscv_lookup_letter(*p);
        if(ext==NULL)
            return reject(error,cap,"unqualified-riscv-extension",arch);
        if ((features & ext->bit) != 0U)
            return reject(error, cap, "duplicate-riscv-extension", arch);
        ++p;
        if(!riscv_consume_version(&p,ext->version))
            return reject(error,cap,"unsupported-riscv-extension-version",arch);
        features |= ext->bit;
    }
    while (*p == '_') {
        const char *start = ++p;
        size_t n;
        const RiscvExtensionSpec *ext;
        bool invalid_version=false;
        while (*p != '\0' && *p != '_') ++p;
        n = (size_t)(p - start);
        ext = riscv_lookup_name(start,n,&invalid_version);
        if (ext == NULL)
            return reject(error,cap,invalid_version?
                          "unsupported-riscv-extension-version":
                          "unqualified-riscv-extension",arch);
        if ((features & ext->bit) != 0U)
            return reject(error, cap, "duplicate-riscv-extension", arch);
        features |= ext->bit;
    }
    if ((features & RISCV_ISA_D) && !(features & RISCV_ISA_F))
        return reject(error, cap, "riscv-d-requires-f", arch);
    if ((features & RISCV_ISA_Q) && !(features & RISCV_ISA_D))
        return reject(error, cap, "riscv-q-requires-d", arch);
    if ((features & RISCV_ISA_E) &&
        (features & (RISCV_ISA_F | RISCV_ISA_D | RISCV_ISA_Q | RISCV_ISA_V)))
        return reject(error, cap, "unsupported-riscv-e-extension-combination", arch);

    out->elf32 = xlen == 32U;
    out->isa.features = features;
    out->explicit_isa = true;
    /* -march selects legal typed instructions; it does not opt in to a
     * raw executable-byte audit. Only the explicit legacy audit entry point
     * may set strict_i_xlen, independently of the selected ISA. */
    if ((features & RISCV_ISA_C) != 0U) out->elf_flags |= EF_RVC;
    if(!riscv_format_base(normalized,sizeof(normalized),&used,&out->isa))
        return reject(error, cap, "riscv-arch-too-long", arch);
    for (i = 0U; i < sizeof(riscv_extensions) /
                      sizeof(riscv_extensions[0]); ++i)
        if ((features & riscv_extensions[i].bit) != 0U &&
            !riscv_append_extension(normalized,sizeof(normalized),&used,
                                    &riscv_extensions[i],
                                    out->isa.extension_versions[i]))
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
    riscv_default_versions(&out->isa);
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
             (((out->isa.features & RISCV_ISA_E) != 0U) != eabi) ||
             (float_abi >= EF_FLOAT_SINGLE && !(out->isa.features & RISCV_ISA_F)) ||
             (float_abi >= EF_FLOAT_DOUBLE && !(out->isa.features & RISCV_ISA_D)) ||
             (float_abi == EF_FLOAT_QUAD && !(out->isa.features & RISCV_ISA_Q))))
            return reject(error, cap, "incompatible-riscv-isa-abi", mabi);
        out->elf32 = xlen == 32U;
        out->isa.xlen=xlen;
        out->elf_flags &= ~(uint32_t)(EF_FLOAT_MASK | EF_RVE);
        out->elf_flags |= float_abi | (eabi ? EF_RVE : 0U);
    } else if (march != NULL && (out->isa.features & RISCV_ISA_E)) {
        out->elf_flags |= EF_RVE;
    }
    return true;
}
