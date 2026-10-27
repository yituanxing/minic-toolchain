#include "riscv_profile.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* RISC-V ELF psABI e_flags. ISA selection and ABI are not independent. */
enum {
    EF_RVC = 1U, EF_FLOAT_MASK = 6U, EF_FLOAT_SINGLE = 2U,
    EF_FLOAT_DOUBLE = 4U, EF_FLOAT_QUAD = 6U, EF_RVE = 8U
};
typedef struct RiscvIsa {
    unsigned xlen;
    bool e, f, d, q, c, strict_i;
} RiscvIsa;
static bool reject(char *error, size_t cap, const char *reason,
                   const char *value) {
    if (cap != 0U) (void)snprintf(error, cap, "%s:%s", reason, value);
    return false;
}
/* This parser validates ELF class and ABI requirements for established ISA
 * spellings. It is NOT the final ISA extension/version/dependency validator;
 * non-I instruction encoding retains the legacy permissive policy. */
static bool read_isa(const char *s, RiscvIsa *isa,
                     char *error, size_t cap) {
    const char *p;
    memset(isa, 0, sizeof(*isa));
    if (strncmp(s, "rv32", 4U) == 0) isa->xlen = 32U;
    else if (strncmp(s, "rv64", 4U) == 0) isa->xlen = 64U;
    else return reject(error, cap, "unsupported-arch", s);
    p = s + 4U;
    if (*p != 'i' && *p != 'e' && *p != 'g')
        return reject(error, cap, "invalid-riscv-march", s);
    isa->e = *p == 'e';
    isa->f = isa->d = *p == 'g';
    ++p;
    for (; *p != '\0' && *p != '_'; ++p) {
        if (isdigit((unsigned char)*p) || *p == 'p') continue;
        if (strchr("mafdqclbjtvnh", *p) == NULL)
            return reject(error, cap, "invalid-riscv-march", s);
        if (*p == 'f') isa->f = true;
        if (*p == 'd') isa->d = true;
        if (*p == 'q') isa->q = true;
        if (*p == 'c') isa->c = true;
    }
    while (*p == '_') {
        const char *begin = ++p;
        if (*p != 'z' && *p != 's' && *p != 'x')
            return reject(error, cap, "invalid-riscv-march", s);
        for (; *p != '\0' && *p != '_'; ++p) {
            if (!islower((unsigned char)*p) &&
                !isdigit((unsigned char)*p))
                return reject(error, cap, "invalid-riscv-march", s);
        }
        if (p - begin < 2)
            return reject(error, cap, "invalid-riscv-march", s);
    }
    isa->strict_i = strcmp(s, isa->xlen == 32U ? "rv32i" : "rv64i") == 0;
    return true;
}
bool minias_riscv_resolve_profile(const char *march, const char *mabi,
                                  MiniAsRiscvProfile *out,
                                  char *error, size_t cap) {
    RiscvIsa isa;
    unsigned abi_xlen = 0U, abi_float = 0U;
    bool abi_e = false;
    memset(out, 0, sizeof(*out));
    memset(&isa, 0, sizeof(isa));
    if (cap != 0U) error[0] = '\0';
    if (march != NULL) {
        if (!read_isa(march, &isa, error, cap)) return false;
        out->elf32 = isa.xlen == 32U;
        out->strict_i_xlen = isa.strict_i ? isa.xlen : 0U;
        if (isa.c) out->elf_flags |= EF_RVC;
    }
    if (mabi != NULL) {
        const char *suffix;
        if (strncmp(mabi, "ilp32", 5U) == 0) {
            abi_xlen = 32U; suffix = mabi + 5U;
        } else if (strncmp(mabi, "lp64", 4U) == 0) {
            abi_xlen = 64U; suffix = mabi + 4U;
        } else return reject(error, cap, "unsupported-abi", mabi);
        if (*suffix == '\0') abi_float = 0U;
        else if (strcmp(suffix, "f") == 0) abi_float = EF_FLOAT_SINGLE;
        else if (strcmp(suffix, "d") == 0) abi_float = EF_FLOAT_DOUBLE;
        else if (strcmp(suffix, "q") == 0 && abi_xlen == 64U)
            abi_float = EF_FLOAT_QUAD;
        else if (strcmp(suffix, "e") == 0 && abi_xlen == 32U) abi_e = true;
        else return reject(error, cap, "unsupported-abi", mabi);
        if (march != NULL &&
            (isa.xlen != abi_xlen || isa.e != abi_e ||
             (abi_float >= EF_FLOAT_SINGLE && !isa.f) ||
             (abi_float >= EF_FLOAT_DOUBLE && !isa.d) ||
             (abi_float == EF_FLOAT_QUAD && !isa.q)))
            return reject(error, cap, "incompatible-riscv-isa-abi", mabi);
        out->elf32 = abi_xlen == 32U;
        out->elf_flags &= ~(uint32_t)(EF_FLOAT_MASK | EF_RVE);
        out->elf_flags |= abi_float | (abi_e ? EF_RVE : 0U);
    }
    return true;
}
