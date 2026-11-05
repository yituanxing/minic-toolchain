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
    puts("LOONG_MACHINE_SOURCE_ASM=PASS exact_form=1 typed_constraint=1 substituted_gnu=1 forge_rejection=1");
    return 0;
}
