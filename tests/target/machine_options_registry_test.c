#include "target/target_machine_options.h"
#include "target/riscv/machine_target_state.h"
#include "target/x86_64/machine_target_state.h"
#include "target/loongarch64/machine_target_state.h"
#include "target/x86_64/codegen_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(expr) do { \
    if (!(expr)) { \
        (void)fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_target_registry(void) {
    const MinicTargetCatalogEntry *entries;
    size_t count, index;
    char reason[256];
    entries = minic_target_catalog_all(&count);
    REQUIRE(count == 5U);
    for (index = 0U; index < count; ++index) {
        MinicTargetInfo target = {0};
        target.architecture = entries[index].architecture;
        REQUIRE(minic_target_machine_options_resolve(
            &target, NULL, NULL, reason, sizeof(reason)));
        REQUIRE(reason[0] == '\0');
        REQUIRE(!minic_target_codegen_option_apply(
            &target, "-definitely-unsupported", reason, sizeof(reason)));
        REQUIRE(reason[0] != '\0');
    }
}

static void test_riscv_isas(void) {
    MinicTargetInfo rv32 = {0};
    MinicTargetInfo rv64 = {0};
    MinicRiscvIsa isa32 = {.xlen = 32U, .abi = MINIC_RISCV_ABI_ILP32};
    MinicRiscvIsa isa64 = {.xlen = 64U, .abi = MINIC_RISCV_ABI_LP64D};
    char reason[256];
    rv32.architecture = MINIC_TARGET_ARCH_RISCV32;
    REQUIRE(minic_riscv_target_isa_store(&rv32, &isa32));
    rv64.architecture = MINIC_TARGET_ARCH_RISCV64;
    REQUIRE(minic_riscv_target_isa_store(&rv64, &isa64));
    REQUIRE(minic_target_machine_options_resolve(
        &rv32, "rv32i", "ilp32", reason, sizeof(reason)));
    REQUIRE(minic_riscv_target_isa(&rv32).strict_isa &&
            minic_riscv_target_isa(&rv32).codegen_features == 0U);
    REQUIRE(!minic_target_machine_options_resolve(
        &rv32, "rv64i", "lp64", reason, sizeof(reason)));
    REQUIRE(minic_riscv_target_isa(&rv32).xlen == 32U);
    REQUIRE(minic_target_machine_options_resolve(
        &rv64, "rv64i", "lp64", reason, sizeof(reason)));
    REQUIRE(minic_riscv_target_isa(&rv64).abi == MINIC_RISCV_ABI_LP64);
    REQUIRE(minic_riscv_target_isa(&rv64).strict_isa &&
            minic_riscv_target_isa(&rv64).codegen_features == 0U);
    REQUIRE(!minic_target_machine_options_resolve(
        &rv64, "rv64imafd", "unqualified", reason, sizeof(reason)));
    REQUIRE(minic_riscv_target_isa(&rv64).abi == MINIC_RISCV_ABI_LP64);
    REQUIRE(minic_target_machine_options_resolve(
        &rv64, "rv64imafd", "lp64d", reason, sizeof(reason)));
    REQUIRE(minic_riscv_target_isa(&rv64).abi == MINIC_RISCV_ABI_LP64D);
    REQUIRE(minic_riscv_target_isa(&rv64).codegen_features != 0U);
    {
        MinicTargetInfo clone = rv64;
        REQUIRE(minic_target_machine_options_resolve(
            &clone, "rv64i", "lp64", reason, sizeof(reason)));
        REQUIRE(minic_riscv_target_isa(&rv64).abi == MINIC_RISCV_ABI_LP64D);
        REQUIRE(minic_riscv_target_isa(&clone).abi == MINIC_RISCV_ABI_LP64);
    }
}

static void test_loongarch_abi(void) {
    MinicTargetInfo target = {0};
    char reason[256];
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    REQUIRE(minic_target_machine_options_resolve(
        &target, NULL, "lp64s", reason, sizeof(reason)));
    REQUIRE(minic_loongarch64_target_soft_float_abi(&target));
    REQUIRE(minic_target_machine_options_resolve(
        &target, NULL, "lp64d", reason, sizeof(reason)));
    REQUIRE(!minic_loongarch64_target_soft_float_abi(&target));
    {
        MinicTargetInfo clone = target;
        REQUIRE(minic_loongarch64_target_soft_float_abi_set(&clone, true));
        REQUIRE(minic_loongarch64_target_soft_float_abi(&clone));
        REQUIRE(!minic_loongarch64_target_soft_float_abi(&target));
    }
    REQUIRE(!minic_target_machine_options_resolve(
        &target, "loongarch64", "lp64s", reason, sizeof(reason)));
    REQUIRE(!minic_loongarch64_target_soft_float_abi(&target));
    REQUIRE(strstr(reason, "LoongArch") != NULL);
}

static void test_x86_options(void) {
    MinicTargetInfo target = {0};
    char reason[256];
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    REQUIRE(!minic_target_machine_options_resolve(
        &target, "rv64i", "lp64", reason, sizeof(reason)));
    REQUIRE(strstr(reason, "RISC-V") != NULL);
    REQUIRE(minic_target_codegen_option_apply(
        &target, "-fcf-protection=branch", reason, sizeof(reason)));
    REQUIRE((minic_x86_64_target_codegen_options(&target) & MINIC_X86_CODEGEN_IBT_ENDBR) != 0U);
    REQUIRE(minic_target_codegen_option_apply(
        &target, "-fcf-protection=none", reason, sizeof(reason)));
    REQUIRE((minic_x86_64_target_codegen_options(&target) & MINIC_X86_CODEGEN_IBT_ENDBR) == 0U);
    REQUIRE(minic_target_codegen_option_apply(
        &target, "-mindirect-branch=thunk-inline", reason, sizeof(reason)));
    REQUIRE((minic_x86_64_target_codegen_options(&target) & MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK) != 0U);
    REQUIRE(minic_target_codegen_option_apply(
        &target, "-mindirect-branch=thunk-extern", reason, sizeof(reason)));
    REQUIRE((minic_x86_64_target_codegen_options(&target) & MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK) == 0U);
    REQUIRE((minic_x86_64_target_codegen_options(&target) & MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK) != 0U);
    REQUIRE(minic_target_codegen_option_apply(
        &target, "-mindirect-branch=keep", reason, sizeof(reason)));
    REQUIRE((minic_x86_64_target_codegen_options(&target) & MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK) == 0U);
    REQUIRE(minic_target_codegen_option_apply(
        &target, "-mno-sse", reason, sizeof(reason)));
    REQUIRE((minic_x86_64_target_codegen_options(&target) & MINIC_X86_CODEGEN_NO_SSE) != 0U);
    {
        MinicTargetInfo clone = target;
        REQUIRE(minic_x86_64_target_codegen_options_set(&clone, 0U));
        REQUIRE(minic_x86_64_target_codegen_options(&clone) == 0U);
        REQUIRE((minic_x86_64_target_codegen_options(&target) &
            MINIC_X86_CODEGEN_NO_SSE) != 0U);
    }
    REQUIRE(minic_target_codegen_option_apply(
        &target, "-msse", reason, sizeof(reason)));
    REQUIRE((minic_x86_64_target_codegen_options(&target) & MINIC_X86_CODEGEN_NO_SSE) == 0U);
    REQUIRE(!minic_target_codegen_option_apply(
        &target, "-fmade-up-switch", reason, sizeof(reason)));
    REQUIRE(strstr(reason, "unsupported x86") != NULL);
}

static void test_unknown_target(void) {
    MinicTargetInfo target = {0};
    char reason[256];
    target.architecture = (MinicTargetArchitecture)9999;
    REQUIRE(!minic_target_machine_options_resolve(
        &target, "rv64i", "lp64", reason, sizeof(reason)));
    REQUIRE(strstr(reason, "unregistered") != NULL);
    REQUIRE(!minic_target_codegen_option_apply(
        &target, "-mno-sse", reason, sizeof(reason)));
    REQUIRE(strstr(reason, "unregistered") != NULL);
    {
        MinicRiscvIsa isa = {.xlen = 64U};
        REQUIRE(!minic_riscv_target_isa_store(&target, &isa));
        REQUIRE(!minic_riscv_target_isa_load(&target, &isa));
        REQUIRE(!minic_x86_64_target_codegen_options_set(&target, 1U));
        REQUIRE(minic_x86_64_target_codegen_options(&target) == 0U);
        REQUIRE(!minic_loongarch64_target_soft_float_abi_set(&target, true));
        REQUIRE(!minic_loongarch64_target_soft_float_abi(&target));
    }
}

int main(void) {
    test_target_registry();
    test_riscv_isas();
    test_loongarch_abi();
    test_x86_options();
    test_unknown_target();
    (void)puts("TARGET_MACHINE_OPTIONS_REGISTRY=PASS targets=5 isa_abi=PASS codegen=PASS fail_closed=PASS");
    return 0;
}
