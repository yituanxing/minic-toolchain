/* Real verified GNU body emission; selected opcodes use target callbacks and
 * source ASM cannot bypass the verifier or the source emitter gate. */
#include "target/common/machine_gnu_asm.h"
#include "target/common/machine_builder.h"

#include <stdio.h>
#include <string.h>

enum ProbeOpcode { PROBE_JUMP = 1, PROBE_RET };

static bool probe_features(const MinicTargetInfo *target,
    uint64_t features, void *context) {
    (void)target;
    (void)context;
    return features == 0U;
}

static bool probe_abi(const MinicTargetInfo *target,
    MinicFunctionCallingConvention convention, void *context) {
    (void)target;
    (void)context;
    return convention == MINIC_CALLING_CONVENTION_DEFAULT;
}

static bool probe_frame(const MinicTargetInfo *target,
    const MinicMachineFunction *fn, const MinicMachineFrameObject *frame,
    void *context) {
    (void)target;
    (void)fn;
    (void)frame;
    (void)context;
    return true;
}

static bool probe_opcode(const MinicTargetInfo *target, uint32_t opcode,
    MinicMachineOpcodeContract *contract, void *context) {
    (void)target;
    (void)context;
    memset(contract, 0, sizeof(*contract));
    if (opcode == PROBE_JUMP) {
        contract->operand_count = 1U;
        contract->operand_kinds[0] = MINIC_MACHINE_OPERAND_BLOCK;
        contract->required_effects = MINIC_MACHINE_EFFECT_BRANCH;
        contract->permitted_effects = MINIC_MACHINE_EFFECT_BRANCH;
        contract->terminates_block = true;
        return true;
    }
    if (opcode == PROBE_RET) {
        contract->required_effects = MINIC_MACHINE_EFFECT_RETURN;
        contract->permitted_effects = MINIC_MACHINE_EFFECT_RETURN;
        contract->terminates_block = true;
        return true;
    }
    return false;
}

static bool probe_selected(const MinicMachineFunction *function,
    const MinicMachineInstruction *ins, void *context) {
    (void)function;
    (void)context;
    return ins->opcode == PROBE_JUMP || ins->opcode == PROBE_RET;
}

static bool probe_source(const MinicTargetInfo *target,
    const MinicMachineInstruction *ins, void *context) {
    (void)target;
    (void)context;
    return ins->source_asm_length == 3U &&
           memcmp(ins->source_asm_template, "nop", 3U) == 0;
}

static bool probe_emit(FILE *output, const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    (void)context;
    if (labels->count != fn->block_count) return false;
    if (ins->opcode == PROBE_JUMP && ins->operand_count == 1U) {
        size_t to = ins->operands[0].value.block_id;
        return to < labels->count &&
            fprintf(output, "\tb %s\n", labels->names[to]) >= 0;
    }
    return ins->opcode == PROBE_RET &&
           fprintf(output, "\tret\n") >= 0;
}

static bool verify_output(FILE *stream, const char *expected) {
    char buf[512];
    size_t count;
    if (fseek(stream, 0L, SEEK_SET) != 0) return false;
    count = fread(buf, 1U, sizeof(buf) - 1U, stream);
    if (ferror(stream)) return false;
    buf[count] = '\0';
    if (strcmp(buf, expected) != 0) {
        fprintf(stderr, "MACHINE_GNU_ASM unexpected body: [%s] expected [%s]\n",
                buf, expected);
        return false;
    }
    return true;
}

static bool build_two_block_function(MinicMachineFunction *function,
    const MinicTargetInfo *target) {
    MinicMachineBuilder builder = {0};
    MinicMachineOperand to = {0};
    size_t entry, exit_block;
    if (!minic_machine_function_initialize(function, target, "mir_probe", 0U) ||
        !minic_machine_builder_initialize(&builder, function) ||
        !minic_machine_builder_create_block(&builder, NULL, false, false,
                                            &entry) ||
        !minic_machine_builder_create_block(&builder, NULL, false, true,
                                            &exit_block) ||
        !minic_machine_builder_position_at(&builder, entry) ||
        !minic_machine_builder_add_successor(&builder, exit_block))
        return false;
    to.kind = MINIC_MACHINE_OPERAND_BLOCK;
    to.value.block_id = exit_block;
    if (!minic_machine_builder_emit_selected(&builder, PROBE_JUMP,
            MINIC_MACHINE_EFFECT_BRANCH, &to, 1U, NULL, 0U) ||
        !minic_machine_builder_position_at(&builder, exit_block))
        return false;
    return minic_machine_builder_emit_selected(&builder, PROBE_RET,
        MINIC_MACHINE_EFFECT_RETURN, NULL, 0U, NULL, 0U);
}

int main(void) {
    MinicTargetInfo target = {0};
    MinicDataLayout layout = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction function = {0};
    MinicMachineFunction source_function = {0};
    MinicMachineBuilder source_builder = {0};
    FILE *output = NULL;
    FILE *rejected = NULL;
    size_t only_block;
    bool success = false;
    unsigned int stage = 0U;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = 8U;
    layout.pointer_alignment = 8U;
    target.data_layout = &layout;
    /* The selector may produce millions of instructions across Linux. The
     * owned representation must not allocate 16 operands and 64 implicit
     * register slots inside every no-operand RET/NOP. */
    if (sizeof(MinicMachineInstruction) > 256U) goto done;
    policy.verify.lookup_opcode = probe_opcode;
    policy.verify.validate_generated_features = probe_features;
    policy.verify.validate_function_abi = probe_abi;
    policy.verify.validate_frame_object = probe_frame;
    policy.verify.validate_selected_instruction = probe_selected;
    policy.verify.validate_source_asm = probe_source;
    policy.emit_selected = probe_emit;
    if (!build_two_block_function(&function, &target)) goto done;
    stage = 1U;

    output = tmpfile();
    if (output == NULL ||
        !minic_machine_gnu_asm_emit_body(output, &function, &policy) ||
        !verify_output(output,
            "\tb .Lminic_mir_mir_probe_1\n"
            ".Lminic_mir_mir_probe_1:\n"
            "\tret\n")) goto done;
    stage = 2U;

    rejected = tmpfile();
    if (rejected == NULL) goto done;
    function.blocks[0].instructions[0].opcode = UINT32_MAX;
    if (minic_machine_gnu_asm_emit_body(rejected, &function, &policy) ||
        !verify_output(rejected, "")) goto done;
    function.blocks[0].instructions[0].opcode = PROBE_JUMP;
    stage = 3U;

    if (!minic_machine_function_initialize(&source_function, &target,
                                           "mir_source", 0U) ||
        !minic_machine_builder_initialize(&source_builder,
                                          &source_function) ||
        !minic_machine_builder_create_block(&source_builder, NULL, false,
                                            false, &only_block)) goto done;
    minic_machine_builder_set_origin(&source_builder, 0U, 0U);
    if (!minic_machine_builder_emit_source_asm(&source_builder,
            "nop", 3U, MINIC_MACHINE_EFFECT_COMPILER_BARRIER,
            NULL, 0U, NULL, 0U, false, false)) goto done;
    stage = 4U;
    minic_machine_builder_clear_origin(&source_builder);
    if (!minic_machine_builder_emit_selected(&source_builder, PROBE_RET,
            MINIC_MACHINE_EFFECT_RETURN, NULL, 0U, NULL, 0U)) goto done;
    if (fseek(rejected, 0L, SEEK_SET) != 0 ||
        minic_machine_gnu_asm_emit_body(rejected, &source_function, &policy) ||
        !verify_output(rejected, "")) goto done;
    stage = 5U;

    success = true;
done:
    if (output != NULL) fclose(output);
    if (rejected != NULL) fclose(rejected);
    minic_machine_function_destroy(&source_function);
    minic_machine_function_destroy(&function);
    if (!success) {
        fprintf(stderr, "MACHINE_GNU_ASM=FAIL stage=%u\n", stage);
        return 1;
    }
    printf("MACHINE_GNU_ASM=PASS instruction_struct_bytes=%zu\n",
           sizeof(MinicMachineInstruction));
    return 0;
}
