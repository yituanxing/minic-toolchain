#include "target/aarch64/machine_source_asm.h"
#include <stdio.h>
#include <string.h>

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
    out = tmpfile();
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
    puts("A64_MACHINE_SOURCE_ASM=PASS exact_opaque=1 arbitrary_rejected=1");
    return 0;
}
