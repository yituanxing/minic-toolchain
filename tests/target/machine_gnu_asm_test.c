/* Real verified GNU body emission; selected opcodes use target callbacks and
 * source ASM cannot bypass the verifier or the source emitter gate. */
#include "target/common/machine_gnu_asm.h"
#include "target/common/core_machine_asm.h"
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

static bool probe_relocation(const MinicTargetInfo *target,
    const char *symbol, uint32_t kind, int64_t addend, void *context) {
    (void)target;
    (void)context;
    return symbol != NULL && kind == 0U && addend == 0;
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
    MinicMachineOperand relocation[2] = {{0}, {0}};
    size_t entry, exit_block;
    if (!minic_machine_function_initialize(function, target, "mir_probe", 0U) ||
        !minic_machine_builder_initialize(&builder, function) ||
        !minic_machine_builder_create_block(&builder, NULL, false, false,
                                            &entry) ||
        !minic_machine_builder_create_block(&builder, NULL, false, true,
                                            &exit_block) ||
        !minic_machine_builder_position_at(&builder, entry) ||
        !minic_machine_builder_add_alias_label(&builder,
                                               ".Lmir_entry_address") ||
        !minic_machine_builder_emit_local_label(&builder,
                                                ".Lmir_before_jump") ||
        !minic_machine_builder_add_successor(&builder, exit_block))
        return false;
    to.kind = MINIC_MACHINE_OPERAND_BLOCK;
    to.value.block_id = exit_block;
    if (!minic_machine_builder_emit_selected(&builder, PROBE_JUMP,
            MINIC_MACHINE_EFFECT_BRANCH, &to, 1U, NULL, 0U) ||
        !minic_machine_builder_position_at(&builder, exit_block) ||
        !minic_machine_builder_add_alias_label(&builder,
                                               ".Lmir_exit_address"))
        return false;
    if (!minic_machine_builder_emit_selected(&builder, PROBE_RET,
            MINIC_MACHINE_EFFECT_RETURN, NULL, 0U, NULL, 0U) ||
        !minic_machine_builder_emit_section_push(&builder,
            "__jump_table", MINIC_MACHINE_SECTION_ALLOC |
            MINIC_MACHINE_SECTION_WRITE, MINIC_MACHINE_SECTION_TYPE_PROGBITS,
            NULL)) return false;
    relocation[0].kind = MINIC_MACHINE_OPERAND_SYMBOL;
    relocation[0].value.symbol.name = ".Lmir_entry_address";
    relocation[1].kind = MINIC_MACHINE_OPERAND_CURRENT_LOCATION;
    return minic_machine_builder_emit_data(&builder, 4U, relocation, 2U) &&
        minic_machine_builder_emit_section_pop(&builder);
}

int main(void) {
    MinicTargetInfo target = {0};
    MinicDataLayout layout = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction function = {0};
    MinicMachineFunction source_function = {0};
    MinicMachineFunction empty_function = {0};
    MinicMachineBuilder source_builder = {0};
    MinicMachineBuilder empty_builder = {0};
    MinicMachineSourceAsmInput typed_asm = {0};
    MinicCoreInlineAsm sealed_empty_asm = {0};
    FILE *output = NULL;
    FILE *rejected = NULL;
    FILE *accepted = NULL;
    FILE *accepted_typed = NULL;
    FILE *accepted_empty = NULL;
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
    policy.verify.validate_relocation = probe_relocation;
    policy.verify.validate_selected_instruction = probe_selected;
    policy.verify.validate_source_asm = probe_source;
    policy.emit_selected = probe_emit;
    if (!build_two_block_function(&function, &target)) goto done;
    stage = 1U;

    output = tmpfile();
    if (output == NULL ||
        !minic_machine_gnu_asm_preflight(&function, &policy) ||
        !minic_machine_gnu_asm_emit_body(output, &function, &policy) ||
        !verify_output(output,
            ".Lmir_entry_address:\n"
            ".Lmir_before_jump:\n"
            "\tb .Lminic_mir_mir_probe_1\n"
            ".Lminic_mir_mir_probe_1:\n"
            ".Lmir_exit_address:\n"
            "\tret\n"
            "\t.pushsection __jump_table,\"aw\",%progbits\n"
            "\t.long .Lmir_entry_address - .\n"
            "\t.popsection\n")) goto done;
    stage = 2U;

    rejected = tmpfile();
    if (rejected == NULL) goto done;
    function.blocks[0].instructions[1].opcode = UINT32_MAX;
    if (minic_machine_gnu_asm_emit_body(rejected, &function, &policy) ||
        !verify_output(rejected, "")) goto done;
    function.blocks[0].instructions[1].opcode = PROBE_JUMP;
    /* Target-independent layout is typed and fail-closed: malformed widths
     * and unbalanced push/pop are rejected before the first output byte. */
    function.blocks[1].instructions[2].directive->data_width_bytes = 3U;
    if (minic_machine_gnu_asm_preflight(&function, &policy) ||
        minic_machine_gnu_asm_emit_body(rejected, &function, &policy) ||
        !verify_output(rejected, "")) goto done;
    function.blocks[1].instructions[2].directive->data_width_bytes = 4U;
    function.blocks[1].instruction_count = 3U;
    if (minic_machine_gnu_asm_preflight(&function, &policy) ||
        minic_machine_gnu_asm_emit_body(rejected, &function, &policy) ||
        !verify_output(rejected, "")) goto done;
    function.blocks[1].instruction_count = 4U;
    /* A forged label alias must be rejected without writing any GNU text. */
    if (!minic_machine_block_add_alias_label(&function, 1U,
                                              ".Lmir_entry_address") ||
        fseek(rejected, 0L, SEEK_SET) != 0 ||
        minic_machine_gnu_asm_preflight(&function, &policy) ||
        minic_machine_gnu_asm_emit_body(rejected, &function, &policy) ||
        !verify_output(rejected, "")) goto done;
    stage = 3U;

    if (!minic_machine_function_initialize(&source_function, &target,
                                           "mir_source", 0U) ||
        !minic_machine_builder_initialize(&source_builder,
                                          &source_function) ||
        !minic_machine_builder_create_block(&source_builder, NULL, false,
                                            false, &only_block)) goto done;
    minic_machine_builder_set_origin(&source_builder, 0U, 0U);
    if (!minic_machine_builder_emit_source_asm(&source_builder,
            "nop", 3U, MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER,
            NULL, 0U, NULL, 0U, false, false)) goto done;
    stage = 4U;
    minic_machine_builder_clear_origin(&source_builder);
    if (!minic_machine_builder_emit_selected(&source_builder, PROBE_RET,
            MINIC_MACHINE_EFFECT_RETURN, NULL, 0U, NULL, 0U)) goto done;
    if (fseek(rejected, 0L, SEEK_SET) != 0 ||
        minic_machine_gnu_asm_preflight(&source_function, &policy) ||
        minic_machine_gnu_asm_emit_body(rejected, &source_function, &policy) ||
        !verify_output(rejected, "")) goto done;
    stage = 5U;

    policy.emit_source_asm = minic_machine_gnu_asm_emit_unbound_source_asm;
    accepted = tmpfile();
    if (accepted == NULL ||
        !minic_machine_gnu_asm_preflight(&source_function, &policy) ||
        !minic_machine_gnu_asm_emit_body(accepted, &source_function, &policy) ||
        !verify_output(accepted, "nop\n\tret\n")) goto done;
    stage = 6U;

    /* The target's Linux/source machine-form proof is immutable MIR data.
     * GNU %= identity is explicit; duplicated occurrence IDs fail closed. */
    source_function.blocks[0].instructions[0].source_asm_machine_form_id = 9U;
    source_function.blocks[0].instructions[0].source_asm_machine_form_variant = 2U;
    source_function.blocks[0].instructions[0].has_source_asm_unique_id = true;
    source_function.blocks[0].instructions[0].source_asm_unique_id = 41U;
    typed_asm.source_bytes = "nop";
    typed_asm.source_byte_length = 3U;
    typed_asm.effects = MINIC_MACHINE_EFFECT_VOLATILE;
    typed_asm.target_machine_form_id = 9U;
    typed_asm.target_machine_form_variant = 2U;
    typed_asm.has_unique_id = true;
    typed_asm.unique_id = 41U;
    minic_machine_builder_set_origin(&source_builder, 0U, 1U);
    if (!minic_machine_builder_emit_source_asm_typed(
            &source_builder, &typed_asm)) goto done;
    {
        MinicMachineInstruction saved =
            source_function.blocks[0].instructions[1];
        source_function.blocks[0].instructions[1] =
            source_function.blocks[0].instructions[2];
        source_function.blocks[0].instructions[2] = saved;
    }
    if (minic_machine_gnu_asm_preflight(&source_function, &policy)) goto done;
    source_function.blocks[0].instructions[1].source_asm_unique_id = 42U;
    accepted_typed = tmpfile();
    if (accepted_typed == NULL ||
        !minic_machine_gnu_asm_preflight(&source_function, &policy) ||
        !minic_machine_gnu_asm_emit_body(
            accepted_typed, &source_function, &policy) ||
        !verify_output(accepted_typed, "nop\nnop\n\tret\n")) goto done;
    source_function.blocks[0].instructions[1].has_source_asm_unique_id =
        false;
    if (minic_machine_gnu_asm_preflight(&source_function, &policy)) goto done;
    source_function.blocks[0].instructions[1].has_source_asm_unique_id =
        true;
    source_function.blocks[0].instructions[2].source_asm_machine_form_id = 1U;
    if (minic_machine_gnu_asm_preflight(&source_function, &policy)) goto done;
    source_function.blocks[0].instructions[2].source_asm_machine_form_id = 0U;
    stage = 7U;

    /* An effect-only GNU asm("") is an ordering barrier, not machine text.
     * It must survive owned MIR verification even if the target has no
     * inline-ASM printer, while malformed empty machine forms fail closed. */
    policy.emit_source_asm = NULL;
    policy.verify.validate_source_asm = NULL;
    if (!minic_machine_function_initialize(&empty_function, &target,
                                           "mir_empty_asm", 0U) ||
        !minic_machine_builder_initialize(&empty_builder, &empty_function) ||
        !minic_machine_builder_create_block(&empty_builder, NULL, false,
                                            false, &only_block)) goto done;
    minic_machine_builder_set_origin(&empty_builder, 0U, 0U);
    sealed_empty_asm.template_text = "";
    sealed_empty_asm.is_volatile = true;
    sealed_empty_asm.has_memory_clobber = true;
    if (minic_machine_builder_emit_core_source_asm(
            &empty_builder, &sealed_empty_asm, 0U,
            NULL, 0U, NULL, 0U, false, false)) goto done;
    sealed_empty_asm.source_asm_unique_id = 77U;
    if (!minic_machine_builder_emit_core_source_asm(
            &empty_builder, &sealed_empty_asm, 0U,
            NULL, 0U, NULL, 0U, false, false)) goto done;
    minic_machine_builder_clear_origin(&empty_builder);
    if (!minic_machine_builder_emit_selected(&empty_builder, PROBE_RET,
            MINIC_MACHINE_EFFECT_RETURN, NULL, 0U, NULL, 0U)) goto done;
    accepted_empty = tmpfile();
    if (accepted_empty == NULL ||
        !minic_machine_gnu_asm_preflight(&empty_function, &policy) ||
        !minic_machine_gnu_asm_emit_body(
            accepted_empty, &empty_function, &policy) ||
        !verify_output(accepted_empty, "\tret\n")) goto done;
    empty_function.blocks[0].instructions[0].effects =
        MINIC_MACHINE_EFFECT_SOURCE_ASM;
    if (minic_machine_gnu_asm_preflight(&empty_function, &policy)) goto done;
    empty_function.blocks[0].instructions[0].effects |=
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    empty_function.blocks[0].instructions[0].source_asm_machine_form_id = 9U;
    if (minic_machine_gnu_asm_preflight(&empty_function, &policy)) goto done;
    stage = 8U;

    success = true;
done:
    if (output != NULL) fclose(output);
    if (rejected != NULL) fclose(rejected);
    if (accepted != NULL) fclose(accepted);
    if (accepted_typed != NULL) fclose(accepted_typed);
    if (accepted_empty != NULL) fclose(accepted_empty);
    minic_machine_function_destroy(&empty_function);
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
