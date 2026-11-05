#include "target/loongarch64/machine_source_asm.h"
#include "target/loongarch64/asm_contract.h"
#include <stdio.h>
#include <string.h>

#define REQUIRE(value) do { if (!(value)) { \
    fprintf(stderr, "loong source ASM test:%d failed: %s\n", \
        __LINE__, #value); return 1; } } while (0)

int main(void) {
    MinicTargetInfo target = {0};
    MinicMachineFunction function = {0};
    MinicMachineInstruction asm_instruction = {0};
    MinicMachineOperand binding = {0};
    const char *label[] = {".Lloong_source_0"};
    MinicMachineGNUAsmLabels labels = {label, 1U};
    FILE *output;
    char text[512];
    size_t count;
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    function.target = &target;
    asm_instruction.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    asm_instruction.has_core_origin = true;
    asm_instruction.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE;
    asm_instruction.source_asm_template = "nop";
    asm_instruction.source_asm_length = 3U;
    REQUIRE(minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    asm_instruction.source_asm_template = "tlbrd; .globl injected";
    asm_instruction.source_asm_length = strlen(asm_instruction.source_asm_template);
    REQUIRE(!minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    asm_instruction.source_asm_template = "dbar 0";
    asm_instruction.source_asm_length = 6U;
    REQUIRE(minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    asm_instruction.source_asm_template = "dbar 0\n\t";
    asm_instruction.source_asm_length = strlen(asm_instruction.source_asm_template);
    asm_instruction.effects |= MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    REQUIRE(minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    /* Actual Linux 6.6.143 BUG-table payload. The metadata and trap must
     * be accepted together, without allowing an arbitrary ASM program. */
    asm_instruction.effects &= ~MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    asm_instruction.source_asm_template =
        ".pushsection __bug_table, \"aw\"; .align 2; 10000: .long 10001f - .; "
        ".pushsection .rodata.str, \"aMS\", @progbits, 1; 10002: .string "
        "\"arch/loongarch/kernel/smp.c\"; .popsection; .long 10002b - .; "
        ".short 413; .short 0; .popsection; 10001: break 1";
    asm_instruction.source_asm_length = strlen(asm_instruction.source_asm_template);
    REQUIRE(minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    asm_instruction.source_asm_template =
        ".pushsection __bug_table, \"aw\"; .align 2; 10000: .long 10001f - .; "
        ".pushsection .rodata.str, \"aMS\", @progbits, 1; 10002: .string "
        "\"arch/loongarch/kernel/smp.c\"; .popsection; .long 10002b - .; "
        ".short 413; .short 0; .popsection; 10001: break 2";
    asm_instruction.source_asm_length = strlen(asm_instruction.source_asm_template);
    REQUIRE(!minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    asm_instruction.source_asm_template = "dbar 0; evil";
    asm_instruction.source_asm_length = strlen(asm_instruction.source_asm_template);
    REQUIRE(!minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    asm_instruction.source_asm_template = "dbar %0";
    asm_instruction.source_asm_length = 7U;
    asm_instruction.source_asm_machine_form_id =
        MINIC_LOONG_MACHINE_ASM_HINT_DBAR;
    asm_instruction.source_asm_machine_form_variant = 1U;
    asm_instruction.operands = &binding;
    asm_instruction.operand_count = 1U;
    binding.kind = MINIC_MACHINE_OPERAND_INTEGER;
    binding.value.integer = 0x700;
    binding.asm_constraint = "I";
    REQUIRE(minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    output = tmpfile();
    REQUIRE(output != NULL);
    REQUIRE(minic_loongarch64_machine_emit_source_asm(
        output, &function, &asm_instruction, &labels, NULL));
    REQUIRE(fseek(output, 0, SEEK_SET) == 0);
    count = fread(text, 1U, sizeof(text) - 1U, output);
    REQUIRE(fclose(output) == 0);
    text[count] = '\0';
    REQUIRE(strstr(text, "dbar 1792") != NULL);
    binding.value.integer = 2048;
    REQUIRE(!minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    binding.value.integer = 1792;
    asm_instruction.source_asm_template = "dbar %0; evil";
    asm_instruction.source_asm_length = strlen(asm_instruction.source_asm_template);
    REQUIRE(!minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    asm_instruction.source_asm_template = "dbar %0";
    asm_instruction.source_asm_length = 7U;
    asm_instruction.source_asm_machine_form_variant = 2U;
    REQUIRE(!minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    asm_instruction.source_asm_machine_form_id = 20U;
    asm_instruction.source_asm_machine_form_variant = 1U;
    REQUIRE(!minic_loongarch64_machine_validate_source_asm(
        &target, &asm_instruction, NULL));
    /* +ZB on a sealed per-CPU AMO names only a register base. A generic
     * offset-bearing memory print would emit a fourth bogus ASM operand. */
    {
        MinicMachineOperand amo[3] = {{0}};
        asm_instruction.source_asm_template =
            "amadd.d %[ret], %[val], %[ptr]";
        asm_instruction.source_asm_length =
            strlen(asm_instruction.source_asm_template);
        asm_instruction.source_asm_machine_form_id =
            MINIC_LOONG_MACHINE_ASM_PERCPU_AMO;
        asm_instruction.source_asm_machine_form_variant = 2U;
        asm_instruction.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
            MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_MEMORY_WRITE;
        asm_instruction.operands = amo;
        asm_instruction.operand_count = 3U;
        amo[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
        amo[0].asm_name = "ret";
        amo[0].asm_constraint = "=&r";
        amo[0].value.physical.reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
        amo[0].value.physical.reg.number = 14U;
        amo[0].value.physical.reg.width_bits = 64U;
        amo[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        amo[1].kind = MINIC_MACHINE_OPERAND_ADDRESS;
        amo[1].asm_name = "ptr";
        amo[1].asm_constraint = "+ZB";
        amo[1].value.address.target_mode = 1U;
        amo[1].value.address.has_base = true;
        amo[1].value.address.base.bank = MINIC_MACHINE_REGISTER_GENERAL;
        amo[1].value.address.base.number = 13U;
        amo[1].value.address.base.width_bits = 64U;
        amo[1].value.address.scale = 1U;
        amo[2].kind = MINIC_MACHINE_OPERAND_REGISTER;
        amo[2].asm_name = "val";
        amo[2].asm_constraint = "r";
        amo[2].value.physical.reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
        amo[2].value.physical.reg.number = 12U;
        amo[2].value.physical.reg.width_bits = 64U;
        amo[2].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        REQUIRE(minic_loongarch64_machine_validate_source_asm(
            &target, &asm_instruction, NULL));
        output = tmpfile();
        REQUIRE(output != NULL);
        REQUIRE(minic_loongarch64_machine_emit_source_asm(
            output, &function, &asm_instruction, &labels, NULL));
        REQUIRE(fseek(output, 0, SEEK_SET) == 0);
        count = fread(text, 1U, sizeof(text) - 1U, output);
        REQUIRE(fclose(output) == 0);
        text[count] = '\0';
        REQUIRE(strstr(text, "amadd.d $t2, $t0, $t1\n") != NULL);
        amo[1].value.address.displacement = 8;
        REQUIRE(!minic_loongarch64_machine_validate_source_asm(
            &target, &asm_instruction, NULL));
        amo[1].value.address.displacement = 0;
        amo[1].asm_constraint = "+m";
        REQUIRE(!minic_loongarch64_machine_validate_source_asm(
            &target, &asm_instruction, NULL));
        amo[1].asm_constraint = "+ZB";
        asm_instruction.source_asm_template =
            "amadd.d %[ret], %[val], %[ptr]; syscall 0";
        asm_instruction.source_asm_length =
            strlen(asm_instruction.source_asm_template);
        REQUIRE(!minic_loongarch64_machine_validate_source_asm(
            &target, &asm_instruction, NULL));
    }
    {
        MinicMachineInstruction fpr = {0};
        MinicMachineOperand gp = {0};
        MinicMachineRegisterEffect fixed = {0};
        fpr.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
        fpr.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
                      MINIC_MACHINE_EFFECT_VOLATILE;
        fpr.source_asm_machine_form_id = 6U;
        fpr.source_asm_template = "movfr2gr.d %0, $f0";
        fpr.source_asm_length = strlen(fpr.source_asm_template);
        fpr.operands = &gp;
        fpr.operand_count = 1U;
        fpr.implicit = &fixed;
        fpr.implicit_count = 1U;
        gp.kind = MINIC_MACHINE_OPERAND_REGISTER;
        gp.asm_constraint = "=r";
        gp.value.physical.reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
        gp.value.physical.reg.number = 12U;
        gp.value.physical.reg.width_bits = 64U;
        gp.value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        fixed.reg.bank = MINIC_MACHINE_REGISTER_FLOAT;
        fixed.reg.number = 0U;
        fixed.reg.width_bits = 64U;
        fixed.access = MINIC_MACHINE_REGISTER_READ;
        REQUIRE(minic_loongarch64_machine_validate_source_asm(
            &target, &fpr, NULL));
        fixed.reg.number = 1U;
        REQUIRE(!minic_loongarch64_machine_validate_source_asm(
            &target, &fpr, NULL));
        fixed.reg.number = 0U;
        fixed.access = MINIC_MACHINE_REGISTER_WRITE;
        REQUIRE(!minic_loongarch64_machine_validate_source_asm(
            &target, &fpr, NULL));
        fpr.implicit_count = 0U;
        REQUIRE(!minic_loongarch64_machine_validate_source_asm(
            &target, &fpr, NULL));
    }
    puts("LOONG_MACHINE_SOURCE_ASM=PASS exact_form=1 linux_bug_table=1 typed_constraint=1 substituted_gnu=1 forge_rejection=1 amo_zb_base=1 fpr_implicit=1");
    return 0;
}
