#define _POSIX_C_SOURCE 200809L
#include "target/aarch64/machine_source_asm.h"
#include "target/aarch64/asm_contract_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *open_test_output(void) {
    const char *directory = getenv("TMPDIR");
    char path[512];
    int fd;
    FILE *file;
    if (directory == NULL || *directory == '\0') directory = "/tmp";
    if (snprintf(path, sizeof(path), "%s/minic-a64-source-XXXXXX",
            directory) >= (int)sizeof(path)) return NULL;
    fd = mkstemp(path);
    if (fd < 0) return NULL;
    unlink(path);
    file = fdopen(fd, "w+b");
    if (file == NULL) close(fd);
    return file;
}

int main(void) {
    MinicTargetInfo target = {0};
    MinicMachineFunction fn = {0};
    MinicMachineInstruction ins = {0};
    FILE *out;
    char bytes[8] = {0};
    size_t len;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    fn.target = &target;
    ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
                  MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    ins.source_asm_template = "nop";
    ins.source_asm_length = 3U;
    if (!minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
        return 1;
    out = open_test_output();
    if (out == NULL) return 1;
    if (!minic_aarch64_machine_source_asm_emit(out, &fn, &ins, NULL, NULL) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) {
        fclose(out);
        return 1;
    }
    len = fread(bytes, 1U, sizeof(bytes), out);
    fclose(out);
    if (len != 4U || memcmp(bytes, "nop\n", 4U) != 0) return 1;
    ins.source_asm_template = "br x0";
    ins.source_asm_length = 5U;
    if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
        return 1;
    ins.source_asm_template = "nop;%0";
    ins.source_asm_length = 6U;
    if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
        return 1;
    ins.source_asm_template = "nop";
    ins.source_asm_length = 3U;
    ins.source_asm_machine_form_id = 1U;
    if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
        return 1;
    {
        MinicMachineOperand binding = {0};
        char source[128], assembled[256];
        static const size_t count = sizeof(aarch64_linux_tlbi_names) /
            sizeof(aarch64_linux_tlbi_names[0]);
        size_t i;
        binding.kind = MINIC_MACHINE_OPERAND_REGISTER;
        binding.asm_constraint = "rZ";
        binding.value.physical.reg = (MinicMachineRegister){9U, 64U,
            MINIC_MACHINE_REGISTER_GENERAL};
        binding.value.physical.access = MINIC_MACHINE_REGISTER_READ;
        memset(&ins, 0, sizeof(ins));
        ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
        ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
        ins.source_asm_machine_form_id = AARCH64_MACHINE_ASM_TLBI;
        ins.has_source_asm_unique_id = true;
        ins.source_asm_unique_id = 7U;
        for (i = 0U; i < count; ++i) {
            const bool addressed = i < AARCH64_LINUX_TLBI_ADDR_FORM_COUNT;
            FILE *asm_out;
            size_t source_length;
            size_t read;
            const int written = snprintf(source, sizeof(source),
                ".arch armv8.5-a\n\ttlbi %s%s\n",
                aarch64_linux_tlbi_names[i], addressed ? ", %x0" : "");
            if (written <= 0 || (size_t)written >= sizeof(source))
                return 1;
            source_length = (size_t)written;
            ins.source_asm_template = source;
            ins.source_asm_length = source_length;
            ins.source_asm_machine_form_variant = (uint32_t)i + 1U;
            ins.operand_count = addressed ? 1U : 0U;
            ins.operands = addressed ? &binding : NULL;
            if (!minic_aarch64_machine_source_asm_validate(
                    &target, &ins, NULL)) {
                fprintf(stderr, "A64 source TLBI rejected form=%zu\n", i);
                return 1;
            }
            asm_out = open_test_output();
            if (asm_out == NULL ||
                !minic_aarch64_machine_source_asm_emit(asm_out, &fn,
                    &ins, NULL, NULL) || fflush(asm_out) != 0 ||
                fseek(asm_out, 0L, SEEK_SET) != 0) {
                if (asm_out != NULL) fclose(asm_out);
                return 1;
            }
            read = fread(assembled, 1U, sizeof(assembled) - 1U, asm_out);
            assembled[read] = '\0';
            fclose(asm_out);
            if (strstr(assembled, ".arch armv8.5-a\n") == NULL ||
                strstr(assembled, aarch64_linux_tlbi_names[i]) == NULL ||
                (addressed ? strstr(assembled, ", x9") == NULL :
                    strstr(assembled, ", x9") != NULL))
                return 1;
            ++ins.source_asm_machine_form_variant;
            if (minic_aarch64_machine_source_asm_validate(
                    &target, &ins, NULL)) return 1;
            --ins.source_asm_machine_form_variant;
            if (addressed) {
                binding.value.physical.reg.width_bits = 32U;
                if (minic_aarch64_machine_source_asm_validate(
                        &target, &ins, NULL)) return 1;
                binding.value.physical.reg.width_bits = 64U;
            }
            source[source_length - 1U] = ';';
            if (minic_aarch64_machine_source_asm_validate(
                    &target, &ins, NULL)) return 1;
        }
    }
    {
        static const struct {
            const char *source;
            const char *rendered;
            unsigned int form;
            bool memory_clobber;
            bool efi;
        } one_gp[] = {
            {"at s1e1r, %0", "at s1e1r, x9\n",
                AARCH64_MACHINE_ASM_AT_S1E1R, false, false},
            {"dc ivac, %0", "dc ivac, x9\n",
                AARCH64_MACHINE_ASM_CACHE_OP, true, false},
            {"dc cvac, %0", "dc cvac, x9\n",
                AARCH64_MACHINE_ASM_CACHE_OP, true, false},
            {"dc cvau, %0", "dc cvau, x9\n",
                AARCH64_MACHINE_ASM_CACHE_OP, false, true},
            {"dc civac, %0", "dc civac, x9\n",
                AARCH64_MACHINE_ASM_CACHE_OP, false, true},
            {"ic ivau, %0", "ic ivau, x9\n",
                AARCH64_MACHINE_ASM_CACHE_OP, true, false},
            {"prfm pldl1keep, %a0", "prfm pldl1keep, [x9]\n",
                AARCH64_MACHINE_ASM_PREFETCH, false, false},
            {"prfm pstl1keep, %a0", "prfm pstl1keep, [x9]\n",
                AARCH64_MACHINE_ASM_PREFETCH, false, false}
        };
        MinicMachineOperand binding = {0};
        char rendered[256];
        size_t i;
        binding.kind = MINIC_MACHINE_OPERAND_REGISTER;
        binding.value.physical.reg = (MinicMachineRegister){9U, 64U,
            MINIC_MACHINE_REGISTER_GENERAL};
        binding.value.physical.access = MINIC_MACHINE_REGISTER_READ;
        memset(&ins, 0, sizeof(ins));
        ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
        ins.operand_count = 1U;
        ins.operands = &binding;
        ins.has_source_asm_unique_id = true;
        ins.source_asm_unique_id = 17U;
        for (i = 0U; i < sizeof(one_gp) / sizeof(one_gp[0]); ++i) {
            FILE *selected;
            size_t actual;
            ins.source_asm_template = (char *)one_gp[i].source;
            ins.source_asm_length = strlen(one_gp[i].source);
            ins.source_asm_machine_form_id = one_gp[i].form;
            ins.source_asm_machine_form_variant = 0U;
            ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
                MINIC_MACHINE_EFFECT_VOLATILE;
            if (one_gp[i].form == AARCH64_MACHINE_ASM_CACHE_OP) {
                ins.effects |= MINIC_MACHINE_EFFECT_MEMORY_READ |
                    MINIC_MACHINE_EFFECT_MEMORY_WRITE;
                if (one_gp[i].memory_clobber)
                    ins.effects |= MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
                if (one_gp[i].efi)
                    ins.effects &= ~MINIC_MACHINE_EFFECT_VOLATILE;
            }
            binding.asm_constraint = one_gp[i].form ==
                AARCH64_MACHINE_ASM_PREFETCH ? "p" : "r";
            if (!minic_aarch64_machine_source_asm_validate(&target,
                    &ins, NULL)) {
                fprintf(stderr, "A64 one-GP source rejected index=%zu\n", i);
                return 1;
            }
            selected = open_test_output();
            if (selected == NULL ||
                !minic_aarch64_machine_source_asm_emit(selected, &fn,
                    &ins, NULL, NULL) || fflush(selected) != 0 ||
                fseek(selected, 0L, SEEK_SET) != 0) {
                if (selected != NULL) fclose(selected);
                return 1;
            }
            actual = fread(rendered, 1U, sizeof(rendered) - 1U, selected);
            fclose(selected);
            rendered[actual] = '\0';
            if (strcmp(rendered, one_gp[i].rendered) != 0) return 1;
            binding.value.physical.reg.width_bits = 32U;
            if (minic_aarch64_machine_source_asm_validate(&target,
                    &ins, NULL)) return 1;
            binding.value.physical.reg.width_bits = 64U;
            binding.asm_constraint = "m";
            if (minic_aarch64_machine_source_asm_validate(&target,
                    &ins, NULL)) return 1;
            binding.asm_constraint = one_gp[i].form ==
                AARCH64_MACHINE_ASM_PREFETCH ? "p" : "r";
            ++ins.source_asm_machine_form_variant;
            if (minic_aarch64_machine_source_asm_validate(&target,
                    &ins, NULL)) return 1;
            --ins.source_asm_machine_form_variant;
            ins.effects ^= MINIC_MACHINE_EFFECT_MEMORY_READ;
            if (minic_aarch64_machine_source_asm_validate(&target,
                    &ins, NULL)) return 1;
            ins.effects ^= MINIC_MACHINE_EFFECT_MEMORY_READ;
        }
        ins.source_asm_template = "dc civac, %0; nop";
        ins.source_asm_length = strlen(ins.source_asm_template);
        ins.source_asm_machine_form_id = AARCH64_MACHINE_ASM_CACHE_OP;
        ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_MEMORY_WRITE;
        if (minic_aarch64_machine_source_asm_validate(&target,
                &ins, NULL)) return 1;
    }
    puts("A64_MACHINE_SOURCE_ASM=PASS exact_opaque=1 arbitrary_rejected=1 tlbi=18 source_seal_rejected=18 binding_rejected=13 injection_rejected=18 one_gp=8 physical_negative=32 injected_rejected=1");
    return 0;
}
