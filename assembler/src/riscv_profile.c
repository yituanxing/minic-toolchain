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

static bool parse_isa(const char *arch, MiniAsRiscvProfile *out,
                      char *error, size_t cap) {
    uint64_t features = 0U;
    const char *p;
    unsigned xlen;
    size_t i, used;
    char normalized[512];
    struct Ext { uint64_t bit; const char *name; };
    static const struct Ext order[] = {
        {RISCV_ISA_M, "_m2p0"}, {RISCV_ISA_A, "_a2p1"},
        {RISCV_ISA_F, "_f2p2"}, {RISCV_ISA_D, "_d2p2"},
        {RISCV_ISA_Q, "_q2p2"}, {RISCV_ISA_C, "_c2p0"},
        {RISCV_ISA_V, "_v1p0"}, {RISCV_ISA_ZICSR, "_zicsr2p0"},
        {RISCV_ISA_ZIFENCEI, "_zifencei2p0"},
        {RISCV_ISA_ZBB, "_zbb1p0"}
    };
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
        uint64_t ext = 0U;
        switch (*p) {
            case 'm': ext = RISCV_ISA_M; break;
            case 'a': ext = RISCV_ISA_A; break;
            case 'f': ext = RISCV_ISA_F; break;
            case 'd': ext = RISCV_ISA_D; break;
            case 'q': ext = RISCV_ISA_Q; break;
            case 'c': ext = RISCV_ISA_C; break;
            case 'v': ext = RISCV_ISA_V; break;
            default: return reject(error, cap, "unqualified-riscv-extension", arch);
        }
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
        if (n == 5U && strncmp(start, "zicsr", 5U) == 0)
            ext = RISCV_ISA_ZICSR;
        else if (n == 8U && strncmp(start, "zifencei", 8U) == 0)
            ext = RISCV_ISA_ZIFENCEI;
        else if (n == 3U && strncmp(start, "zbb", 3U) == 0)
            ext = RISCV_ISA_ZBB;
        else return reject(error, cap, "unqualified-riscv-extension", arch);
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
    out->strict_i_xlen = features == RISCV_ISA_I ? xlen : 0U;
    if ((features & RISCV_ISA_C) != 0U) out->elf_flags |= EF_RVC;
    used = (size_t)snprintf(normalized, sizeof(normalized), "rv%u%s",
                           xlen, (features & RISCV_ISA_E) ? "e2p0" : "i2p1");
    if (used >= sizeof(normalized))
        return reject(error, cap, "riscv-arch-too-long", arch);
    for (i = 0U; i < sizeof(order) / sizeof(order[0]); ++i)
        if ((features & order[i].bit) != 0U &&
            !append_isa(normalized, sizeof(normalized),
                        &used, order[i].name))
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
