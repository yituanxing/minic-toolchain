#define _POSIX_C_SOURCE 200809L
#include "target/aarch64/machine_source_asm.h"
#include "target/aarch64/machine_opcode.h"
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
    {
        static const char linux_at_barrier[] =
            ".if 1 == 1\n"
            "661:\n\tnop\n662:\n"
            ".pushsection .altinstructions,\"a\"\n"
            " .word 661b - .\n .word 663f - .\n .hword 71\n"
            " .byte 662b-661b\n .byte 664f-663f\n"
            ".popsection\n.subsection 1\n"
            "663:\n\tdmb sy\n664:\n"
            ".org . - (664b-663b) + (662b-661b)\n"
            ".org . - (662b-661b) + (664b-663b)\n"
            ".previous\n.endif\n";
        char changed[sizeof(linux_at_barrier)];
        FILE *selected;
        memset(&ins, 0, sizeof(ins));
        ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
        ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
        ins.source_asm_machine_form_id =
            AARCH64_MACHINE_ASM_BARRIER_ALTERNATIVE;
        ins.source_asm_template = (char *)linux_at_barrier;
        ins.source_asm_length = sizeof(linux_at_barrier) - 1U;
        if (!minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        selected = open_test_output();
        if (selected == NULL ||
            !minic_aarch64_machine_source_asm_emit(
                selected, &fn, &ins, NULL, NULL) ||
            fflush(selected) != 0 || fseek(selected, 0L, SEEK_END) != 0 ||
            ftell(selected) != (long)sizeof(linux_at_barrier)) {
            if (selected != NULL) fclose(selected);
            return 1;
        }
        fclose(selected);
        ins.effects &= ~MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        ins.effects |= MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
        memcpy(changed, linux_at_barrier, sizeof(changed));
        {
            char *cap = strstr(changed, ".hword 71");
            if (cap == NULL) return 1;
            cap[7] = '2';
        }
        ins.source_asm_template = changed;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        ins.source_asm_template = (char *)linux_at_barrier;
        ins.source_asm_machine_form_id = 0U;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        memset(&ins, 0, sizeof(ins));
        ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    }
    {
        /* Inlined Linux static keys may be elements of an external array:
         * symbol+32 is a real key, not a misplaced bit-0 branch flag. */
        static const char key_goto[] =
            "1:\tnop\n"
            ".pushsection __jump_table,\"aw\"\n"
            ".align 3\n"
            ".long 1b - ., %l[l_yes] - .\n"
            ".quad %c0 - .\n"
            ".popsection\n";
        MinicMachineOperand bindings[2] = {{0}};
        const char *const names[] = {".Lkey_yes"};
        const MinicMachineGNUAsmLabels labels = {names, 1U};
        char selected_bytes[512] = {0};
        FILE *selected;
        memset(&ins, 0, sizeof(ins));
        ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
        ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_BRANCH;
        ins.source_asm_template = (char *)key_goto;
        ins.source_asm_length = sizeof(key_goto) - 1U;
        ins.source_asm_terminates_block = true;
        ins.source_asm_permits_fallthrough = true;
        ins.operand_count = 2U;
        ins.operands = bindings;
        bindings[0].kind = MINIC_MACHINE_OPERAND_SYMBOL;
        bindings[0].asm_constraint = "i";
        bindings[0].value.symbol.name = "perf_swevent_enabled";
        bindings[0].value.symbol.relocation =
            MINIC_A64_RELOC_SOURCE_IMMEDIATE;
        bindings[0].value.symbol.addend = 32;
        bindings[1].kind = MINIC_MACHINE_OPERAND_BLOCK;
        bindings[1].asm_name = "l_yes";
        bindings[1].value.block_id = 0U;
        if (!minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        selected = open_test_output();
        if (selected == NULL ||
            !minic_aarch64_machine_source_asm_emit(
                selected, &fn, &ins, &labels, NULL) ||
            fflush(selected) != 0 || fseek(selected, 0L, SEEK_SET) != 0) {
            if (selected != NULL) fclose(selected);
            return 1;
        }
        len = fread(selected_bytes, 1U, sizeof(selected_bytes) - 1U,
            selected);
        fclose(selected);
        selected_bytes[len] = '\0';
        if (strstr(selected_bytes, ".quad perf_swevent_enabled+32 - .") == NULL ||
            strstr(selected_bytes, ".Lkey_yes") == NULL ||
            strstr(selected_bytes, "%c0") != NULL) return 1;
        bindings[0].value.symbol.addend = 33;
        if (!minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        bindings[0].value.symbol.addend = 2;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        bindings[0].value.symbol.addend = -8;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        memset(&ins, 0, sizeof(ins));
        ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    }
    {
        static const char kvm_va_mask[] =
            ".if 1 == 1\n"
            "661:\n"
            "\tand %0, %0, #1\n"
            "\tror %0, %0, #1\n"
            "\tadd %0, %0, #0\n"
            "\tadd %0, %0, #0, lsl 12\n"
            "\tror %0, %0, #63\n"
            "662:\n"
            ".pushsection .altinstructions,\"a\"\n"
            " .word 661b - .\n"
            " .word kvm_update_va_mask - .\n"
            " .hword (1 << 15) | (1)\n"
            " .byte 662b - 661b\n"
            " .byte 664f - 663f\n"
            ".popsection\n"
            "663:\n664:\n"
            ".endif\n";
        static const char alien_callback[] =
            ".if 1 == 1\n661:\n\tand %0, %0, #1\n\t"
            "ror %0, %0, #1\n\tadd %0, %0, #0\n\t"
            "add %0, %0, #0, lsl 12\n\tror %0, %0, #63\n"
            "662:\n.pushsection .altinstructions,\"a\"\n"
            ".word 661b - .\n.word forbidden_callback - .\n"
            ".hword (1 << 15) | (1)\n.byte 662b - 661b\n"
            ".byte 664f - 663f\n.popsection\n663:\n664:\n.endif\n";
        MinicMachineOperand binding = {0};
        char selected_bytes[1024] = {0};
        FILE *selected;
        memset(&ins, 0, sizeof(ins));
        binding.kind = MINIC_MACHINE_OPERAND_REGISTER;
        binding.value.physical.reg = (MinicMachineRegister){9U, 64U,
            MINIC_MACHINE_REGISTER_GENERAL};
        binding.value.physical.access = MINIC_MACHINE_REGISTER_READ |
            MINIC_MACHINE_REGISTER_WRITE;
        binding.asm_constraint = "+r";
        ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
        ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE;
        ins.source_asm_machine_form_id = AARCH64_MACHINE_ASM_KVM_VA_MASK;
        ins.source_asm_template = (char *)kvm_va_mask;
        ins.source_asm_length = sizeof(kvm_va_mask) - 1U;
        ins.operand_count = 1U;
        ins.operands = &binding;
        if (!minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        selected = open_test_output();
        if (selected == NULL ||
            !minic_aarch64_machine_source_asm_emit(
                selected, &fn, &ins, NULL, NULL) ||
            fflush(selected) != 0 || fseek(selected, 0L, SEEK_SET) != 0) {
            if (selected != NULL) fclose(selected);
            return 1;
        }
        len = fread(selected_bytes, 1U, sizeof(selected_bytes) - 1U,
            selected);
        fclose(selected);
        selected_bytes[len] = '\0';
        if (strstr(selected_bytes, "and x9, x9, #1") == NULL ||
            strstr(selected_bytes, "kvm_update_va_mask") == NULL ||
            strstr(selected_bytes, ".altinstructions") == NULL ||
            strstr(selected_bytes, "%0") != NULL) return 1;
        binding.value.physical.reg.number = 10U;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        binding.value.physical.reg.number = 9U;
        binding.value.physical.access = MINIC_MACHINE_REGISTER_READ;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        binding.value.physical.access = MINIC_MACHINE_REGISTER_READ |
            MINIC_MACHINE_REGISTER_WRITE;
        ins.source_asm_template = (char *)alien_callback;
        ins.source_asm_length = sizeof(alien_callback) - 1U;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        ins.source_asm_template = (char *)kvm_va_mask;
        ins.source_asm_length = sizeof(kvm_va_mask) - 1U;
        memset(&ins, 0, sizeof(ins));
        ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    }
    {
        static const char daif_source[] =
            "msr\tdaifset, #0xf\t\t// local_daif_mask\n";
        static const char tampered[] =
            "msr\tdaifset, #0xf\t\t// local_daif_mask\n\tnop";
        FILE *selected;
        char rendered[sizeof(daif_source) + 8U] = {0};
        ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
        ins.source_asm_template = (char *)daif_source;
        ins.source_asm_length = sizeof(daif_source) - 1U;
        if (!minic_aarch64_machine_source_asm_validate(
                &target, &ins, NULL)) return 1;
        selected = open_test_output();
        if (selected == NULL ||
            !minic_aarch64_machine_source_asm_emit(
                selected, &fn, &ins, NULL, NULL) ||
            fflush(selected) != 0 || fseek(selected, 0L, SEEK_SET) != 0) {
            if (selected != NULL) fclose(selected);
            return 1;
        }
        len = fread(rendered, 1U, sizeof(rendered), selected);
        fclose(selected);
        if (len != sizeof(daif_source) ||
            memcmp(rendered, daif_source, sizeof(daif_source) - 1U) != 0 ||
            rendered[len - 1U] != '\n') return 1;
        ins.source_asm_template = (char *)tampered;
        ins.source_asm_length = sizeof(tampered) - 1U;
        if (minic_aarch64_machine_source_asm_validate(
                &target, &ins, NULL)) return 1;
        ins.source_asm_template = (char *)daif_source;
        ins.source_asm_length = sizeof(daif_source) - 1U;
    }
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
        static char bug_table[] =
            ".pushsection __bug_table,\"aw\";\n"
            ".align 2;\n"
            "14470: .long 14471f - .;\n"
            ".pushsection .rodata.str,\"aMS\",@progbits,1;\n"
            "14472: .string \"aarch64-fixture.c\";\n"
            ".popsection;\n"
            ".long 14472b - .;\n"
            ".short 41;\n"
            ".short 0;\n"
            ".align 2;\n"
            ".popsection;\n"
            "14471: brk 0x800\n";
        char edited[sizeof(bug_table) + 8U];
        char *at;
        char rendered[sizeof(bug_table) + 8U];
        size_t count;
        FILE *selected;
        memset(&ins, 0, sizeof(ins));
        ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
        ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE;
        ins.source_asm_template = bug_table;
        ins.source_asm_length = sizeof(bug_table) - 1U;
        if (!minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        selected = open_test_output();
        if (selected == NULL ||
            !minic_aarch64_machine_source_asm_emit(selected, &fn,
                &ins, NULL, NULL) || fflush(selected) != 0 ||
            fseek(selected, 0L, SEEK_SET) != 0) {
            if (selected != NULL) fclose(selected);
            return 1;
        }
        count = fread(rendered, 1U, sizeof(rendered), selected);
        fclose(selected);
        if (count != sizeof(bug_table) ||
            memcmp(rendered, bug_table, sizeof(bug_table) - 1U) != 0 ||
            rendered[count - 1U] != '\n') return 1;
        /* Do not permit modified sections, numeric labels or injected ASM. */
        memcpy(edited, bug_table, sizeof(bug_table));
        ins.source_asm_template = edited;
        at = strstr(edited, "__bug_table");
        if (at == NULL) return 1;
        at[2] = 'x';
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        memcpy(edited, bug_table, sizeof(bug_table));
        edited[1] = 'x';
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        memcpy(edited, bug_table, sizeof(bug_table));
        at = strstr(edited, "aarch64-fixture.c");
        if (at == NULL) return 1;
        at[7] = '?';
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        memcpy(edited, bug_table, sizeof(bug_table));
        at = strstr(edited, "14471f");
        if (at == NULL) return 1;
        at[4] = '3';
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        memcpy(edited, bug_table, sizeof(bug_table));
        at = strstr(edited, ".popsection;");
        if (at == NULL) return 1;
        at[1] = 'x';
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        memcpy(edited, bug_table, sizeof(bug_table));
        memcpy(edited + sizeof(bug_table) - 1U, "nop", 4U);
        ins.source_asm_length += 3U;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        ins.source_asm_template = bug_table;
        ins.source_asm_length = sizeof(bug_table) - 1U;
        ins.effects |= MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
        ins.effects &= ~MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
        ins.source_asm_terminates_block = true;
        if (minic_aarch64_machine_source_asm_validate(&target, &ins, NULL))
            return 1;
    }
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
    puts("A64_MACHINE_SOURCE_ASM=PASS exact_opaque=1 bug_table=1 bug_table_negative=8 arbitrary_rejected=1 tlbi=18 source_seal_rejected=18 binding_rejected=13 injection_rejected=18 one_gp=8 physical_negative=32 injected_rejected=1");
    return 0;
}
