#include "target/driver_stage_recipe.h"

#include <stdio.h>
#include <string.h>

/* These are RV64 Linux-musl staged CPP facts. A different backend's -S
 * support must never inherit them, even when its C model is also LP64. */
static bool append_define(char **arguments, size_t *count, size_t capacity,
                          const char *define) {
    if (arguments == NULL || count == NULL || *count >= capacity) return false;
    arguments[(*count)++] = (char *)define;
    return true;
}

static bool rv64_append_cpp_predefines(char **arguments, size_t *count,
                                       size_t capacity, bool hosted,
                                       bool plain_char_unsigned,
                                       const char *march, const char *mabi) {
#define ADD(define) do { \
    if (!append_define(arguments, count, capacity, define)) return false; \
} while (0)
    const MinicRiscvProfile *profile = minic_riscv_profile_find(march);
    ADD("-D__STDC__=1");
    ADD("-D__STDC_VERSION__=201112L");
    /* Conservative GCC compatibility: GNU __attribute__ works but newer
     * GCC builtin and optimization surfaces have not been qualified. */
    ADD("-D__GNUC__=2");
    ADD("-D__GNUC_MINOR__=7");
    ADD("-D__GNUC_PATCHLEVEL__=0");
    ADD(hosted ? "-D__STDC_HOSTED__=1" : "-D__STDC_HOSTED__=0");
    ADD("-D__linux__=1");
    ADD("-D__linux=1");
    ADD("-Dlinux=1");
    ADD("-D__unix__=1");
    ADD("-D__unix=1");
    ADD("-D__riscv=1");
    ADD("-D__riscv_xlen=64");
    if (plain_char_unsigned) ADD("-D__CHAR_UNSIGNED__=1");
    ADD("-D__LP64__=1");
    ADD("-D_LP64=1");
    if (mabi != NULL && strcmp(mabi, "lp64") == 0)
        ADD("-D__riscv_float_abi_soft=1");
    else
        ADD("-D__riscv_float_abi_double=1");
    /* __riscv_mul describes the selected target ISA, not MiniC's chosen
     * machine instruction subset. Legacy implicit RV64GC has M. */
    if (march == NULL ||
        (profile != NULL && (profile->features & MINIC_RISCV_FEATURE_M))) {
        ADD("-D__riscv_mul=1");
        ADD("-D__riscv_div=1");
    }
    ADD("-D__ORDER_LITTLE_ENDIAN__=1234");
    ADD("-D__ORDER_BIG_ENDIAN__=4321");
    ADD("-D__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__");
    ADD("-D__CHAR_BIT__=8");
    ADD("-D__SIZEOF_SHORT__=2");
    ADD("-D__SIZEOF_INT__=4");
    ADD("-D__SIZEOF_LONG__=8");
    ADD("-D__SIZEOF_LONG_LONG__=8");
    ADD("-D__SIZEOF_POINTER__=8");
    ADD("-D__SIZEOF_SIZE_T__=8");
#undef ADD
    return true;
}

static bool rv64_validate_staged_isa_abi(
    const char *march, const char **mabi, char *reason,
    size_t reason_capacity) {
    const MinicRiscvProfile *profile;
    if (mabi == NULL || reason == NULL || reason_capacity == 0U) return false;
    reason[0] = '\0';
    if (march == NULL) {
        if (*mabi == NULL || strcmp(*mabi, "lp64d") == 0) return true;
        (void)snprintf(reason, reason_capacity,
                       "explicit-nondefault-abi-requires--march");
        return false;
    }
    profile = minic_riscv_profile_find(march);
    if (profile == NULL || profile->xlen != 64U) {
        (void)snprintf(reason, reason_capacity,
                       "unsupported-riscv-isa:%s", march);
        return false;
    }
    if (*mabi == NULL) *mabi = profile->mabi;
    if (strcmp(*mabi, "lp64") != 0 &&
        !(strcmp(*mabi, "lp64d") == 0 &&
          (profile->features &
           (MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D)) ==
              (MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D))) {
        (void)snprintf(reason, reason_capacity,
                       "incompatible-riscv-isa-abi:%s/%s", march, *mabi);
        return false;
    }
    if (!profile->staged_assembler_qualified) {
        (void)snprintf(reason, reason_capacity,
                       "extended-riscv-isa-requires--S-external-assembler:%s",
                       march);
        return false;
    }
    return true;
}

const MinicTargetDriverStageRecipe minic_riscv64_driver_stage_recipe = {
    .architecture = MINIC_TARGET_ARCH_RISCV64,
    .assembler_default_march = "rv64gc",
    .assembler_default_mabi = "lp64d",
    .ld_emulation_option = "-melf64lriscv",
    .dynamic_linker_option = "--dynamic-linker=/lib/ld-musl-riscv64.so.1",
    .explicit_runtime_archive_march = "rv64i",
    .omit_default_lgcc_march = "rv64i",
    .validate_isa_abi = rv64_validate_staged_isa_abi,
    .append_cpp_predefines = rv64_append_cpp_predefines,
};
