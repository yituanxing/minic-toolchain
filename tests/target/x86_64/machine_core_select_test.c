/* Physical x86-64 Core selection: actual Core CFG and physical opcode
 * contracts, no printer-only machine strings or legacy FILE* instruction
 * fallback.  Unsupported instructions must fail the whole function. */
#include "target/x86_64/machine_core_select.h"
#include "target/x86_64/machine_target_state.h"
#include "target/x86_64/machine_opcode.h"
#include "target/common/machine_core_coverage.h"
#include "target/common/machine_gnu_asm.h"
#include "target/data_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool make_core(MinicCoreFunction *core,
    MinicCoreTypeContext *types, bool unsupported, bool scalar) {
    MinicCoreType type = {0};
    MinicCoreTypeId result_type;
    MinicCoreBlockId first, second;
    MinicCoreTerminator term = {0};
    minic_core_function_initialize(core);
    core->name = malloc(sizeof("x86_core"));
    if (core->name == NULL) return false;
    memcpy(core->name, "x86_core", sizeof("x86_core"));
    core->name_length = sizeof("x86_core") - 1U;
    type.kind = scalar ? MINIC_CORE_TYPE_INTEGER : MINIC_CORE_TYPE_VOID;
    type.has_layout = scalar;
    type.size = scalar ? 8U : 0U;
    type.alignment = scalar ? 8U : 0U;
    type.value.integer.bit_width = scalar ? 64U : 0U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(types, &type, &result_type) ||
        !minic_core_function_define_signature(core, types,
            result_type, NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT,
            false) ||
        !minic_core_function_add_block(core, &first) ||
        !minic_core_function_add_block(core, &second)) return false;
    if (unsupported) {
        MinicCoreInstruction instruction = {0};
        instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
        instruction.core_type_id = result_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        if (!minic_core_function_append_effect_instruction(core,
                first, &instruction)) return false;
    }
    if (scalar) {
        MinicCoreInstruction instruction = {0};
        MinicCoreValueId one, two, sum;
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.core_type_id = result_type;
        instruction.value.integer_value = 41;
        if (!minic_core_function_append_value_instruction(core,
                second, &instruction, &one)) return false;
        instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(core,
                second, &instruction, &two)) return false;
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.value.binary.left = one;
        instruction.value.binary.right = two;
        if (!minic_core_function_append_value_instruction(core,
                second, &instruction, &sum)) return false;
        term.kind = MINIC_CORE_TERMINATOR_BRANCH;
        term.branch_target = second;
        if (!minic_core_function_set_terminator(core, first, &term))
            return false;
        term.kind = MINIC_CORE_TERMINATOR_RETURN;
        term.return_value = sum;
    } else {
        term.kind = MINIC_CORE_TERMINATOR_BRANCH;
        term.branch_target = second;
        if (!minic_core_function_set_terminator(core, first, &term))
            return false;
        term.kind = MINIC_CORE_TERMINATOR_BRANCH;
        term.branch_target = first;
    }
    return minic_core_function_set_terminator(core, second, &term) &&
        minic_core_function_attach_type_context(core, types) &&
        minic_core_function_verify(core);
}

static bool scenario(bool unsupported, bool scalar) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicX86Frame frame = {0};
    MinicTargetInfo target = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    FILE *out = NULL;
    char body[16000];
    size_t value_offsets[3] = {8U, 16U, 24U};
    size_t i, n, origins = 0U;
    bool reachable[2] = {false, false};
    bool okay = false;
    const char *stage = "make-core";
    minic_core_type_context_initialize(&types);
    if (!make_core(&core, &types, unsupported, scalar)) goto done;
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    target.data_layout = minic_x86_64_linux_data_layout();
    frame.value_count = core.value_count;
    frame.value_offsets = value_offsets;
    frame.frame_size = 64U;
    stage = "select";
    if (unsupported) {
        okay = !minic_x86_64_machine_select_core_subset(
            &machine, &target, &core, &frame);
        goto done;
    }
    if (!minic_x86_64_machine_select_core_subset(
            &machine, &target, &core, &frame)) goto done;
    stage = "cfg-and-origin";
    if (!minic_core_function_mark_reachable_blocks(&core,
            reachable, 2U) ||
        !minic_machine_core_coverage_verify(&core,
            reachable, &machine) ||
        machine.block_count != 3U ||
        !machine.blocks[0].machine_auxiliary ||
        machine.blocks[1].machine_auxiliary ||
        machine.blocks[2].machine_auxiliary ||
        machine.blocks[0].has_core_block_origin ||
        !machine.blocks[1].has_core_block_origin ||
        machine.blocks[1].core_block_id != 0U ||
        !machine.blocks[2].has_core_block_origin ||
        machine.blocks[2].core_block_id != 1U ||
        !minic_x86_64_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    for (i = 0U; i < machine.block_count; ++i) {
        size_t j;
        for (j = 0U; j < machine.blocks[i].instruction_count; ++j) {
            const MinicMachineInstruction *ins =
                &machine.blocks[i].instructions[j];
            if (ins->has_core_origin) {
                if (i == 0U || ins->core_block_id != (scalar ? 1U : 0U) ||
                    ins->core_instruction_id >= core.instruction_count)
                    goto done;
                ++origins;
            }
        }
    }
    if (scalar && origins < 6U) goto done;
    stage = "gnu-asm";
    out = tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(
            out, &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(body, 1U, sizeof(body) - 1U, out);
    body[n] = '\0';
    stage = "body-invariant";
    okay = strstr(body, "x86_core:\n") == NULL &&
        strstr(body, ".Lminic_mir_x86_core_1:") != NULL &&
        strstr(body, ".Lminic_mir_x86_core_2:") != NULL &&
        strstr(body, "jmp x86_core") == NULL &&
        (!scalar || strstr(body, "addq") != NULL);
done:
    if (!okay) fprintf(stderr,
        "X86_CORE_MIR=FAIL at %s (unsupported=%d scalar=%d)\n",
        stage, unsupported ? 1 : 0, scalar ? 1 : 0);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return okay;
}

static bool scenario_parameter(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicTargetInfo target = {0};
    MinicX86Frame frame = {0};
    MinicMachineFunction machine = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId integer_type;
    MinicCoreBlockId block;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreValueId value;
    MinicX86ArgLocation location = {0};
    size_t offset = 8U;
    bool okay = false;
    bool reachable[1] = {false};
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("x86_parameter"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "x86_parameter", sizeof("x86_parameter"));
    core.name_length = sizeof("x86_parameter") - 1U;
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.integer.is_signed = true;
    type.value.integer.bit_width = 64U;
    if (!minic_core_type_register(&types, &type, &integer_type) ||
        !minic_core_function_define_signature(&core, &types,
            integer_type, &integer_type, 1U,
            MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &block)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = integer_type;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&core, block,
            &ins, &value)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = value;
    if (!minic_core_function_set_terminator(&core, block, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    location.kind = MINIC_X86_ARG_INTEGER;
    location.gp_index = 0U;
    frame.incoming_arguments.locations = &location;
    frame.incoming_arguments.argument_count = 1U;
    frame.incoming_arguments.calling_convention =
        MINIC_CALLING_CONVENTION_DEFAULT;
    frame.value_count = 1U;
    frame.value_offsets = &offset;
    frame.frame_size = 32U;
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    target.data_layout = minic_x86_64_linux_data_layout();
    if (!minic_x86_64_machine_select_core_subset(&machine,
            &target, &core, &frame)) goto done;
    okay = minic_core_function_mark_reachable_blocks(&core,
        reachable, 1U) &&
        minic_machine_core_coverage_verify(&core, reachable, &machine) &&
        machine.block_count == 2U &&
        machine.blocks[1].has_core_block_origin &&
        machine.blocks[1].instruction_count >= 4U;
done:
    if (!okay) fprintf(stderr, "X86_CORE_MIR=FAIL parameter\n");
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return okay;
}

static bool scenario_float(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicTargetInfo target = {0};
    MinicX86Frame frame = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId float_id;
    MinicCoreBlockId block;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreValueId one, two, sum;
    size_t offsets[3] = {8U, 16U, 24U};
    FILE *output = NULL;
    char text[8192];
    size_t length;
    bool okay = false;
    bool reachable[1] = {false};
    const char *stage = "core-setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("x86_float"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "x86_float", sizeof("x86_float"));
    core.name_length = sizeof("x86_float") - 1U;
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    if (!minic_core_type_register(&types, &type, &float_id) ||
        !minic_core_function_define_signature(&core, &types,
            float_id, NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &block)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
    ins.core_type_id = float_id;
    ins.value.floating_bits = UINT64_C(0x3ff0000000000000);
    if (!minic_core_function_append_value_instruction(&core,
            block, &ins, &one)) goto done;
    ins.value.floating_bits = UINT64_C(0x4000000000000000);
    if (!minic_core_function_append_value_instruction(&core,
            block, &ins, &two)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_DOUBLE_ADD;
    ins.value.binary.left = one;
    ins.value.binary.right = two;
    if (!minic_core_function_append_value_instruction(&core,
            block, &ins, &sum)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = sum;
    if (!minic_core_function_set_terminator(&core, block, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    target.data_layout = minic_x86_64_linux_data_layout();
    frame.value_offsets = offsets;
    frame.value_count = core.value_count;
    frame.frame_size = 64U;
    stage = "machine-selection";
    if (!minic_x86_64_machine_select_core_subset(&machine,
            &target, &core, &frame) ||
        !minic_x86_64_machine_gnu_asm_policy(&target, &policy) ||
        !minic_core_function_mark_reachable_blocks(&core,
            reachable, 1U) ||
        !minic_machine_core_coverage_verify(&core,
            reachable, &machine)) goto done;
    stage = "machine-serialization";
    output = tmpfile();
    if (output == NULL ||
        !minic_machine_gnu_asm_emit_body(output, &machine, &policy) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0)
        goto done;
    length = fread(text, 1U, sizeof(text) - 1U, output);
    text[length] = '\0';
    stage = "machine-contents";
    okay = strstr(text, "addsd") != NULL &&
        strstr(text, "movq %xmm0") != NULL;
done:
    if (!okay) fprintf(stderr, "X86_CORE_MIR=FAIL float stage=%s\n", stage);
    if (output != NULL) fclose(output);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return okay;
}

static bool scenario_direct_call(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicTargetInfo target = {0};
    MinicX86Frame frame = {0};
    MinicMachineFunction machine = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId integer_type;
    MinicCoreBlockId block;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreValueId value;
    size_t offset = 8U, i, calls = 0U;
    bool okay = false;
    bool reachable[1] = {false};
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("x86_call"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "x86_call", sizeof("x86_call"));
    core.name_length = sizeof("x86_call") - 1U;
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.integer.is_signed = true;
    type.value.integer.bit_width = 64U;
    if (!minic_core_type_register(&types, &type, &integer_type) ||
        !minic_core_function_define_signature(&core, &types,
            integer_type, NULL, 0U,
            MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &block)) goto done;
    core.callees = calloc(1U, sizeof(*core.callees));
    if (core.callees == NULL) goto done;
    core.callee_count = 1U;
    core.callee_capacity = 1U;
    core.callees[0].name = malloc(sizeof("external_callee"));
    if (core.callees[0].name == NULL) goto done;
    memcpy(core.callees[0].name, "external_callee",
        sizeof("external_callee"));
    core.callees[0].name_length = sizeof("external_callee") - 1U;
    core.callees[0].core_function_type_id = core.core_function_type_id;
    ins.kind = MINIC_CORE_INSTRUCTION_CALL;
    ins.core_type_id = integer_type;
    ins.value.call.callee_id = 0U;
    ins.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_value_instruction(&core,
            block, &ins, &value)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = value;
    if (!minic_core_function_set_terminator(&core, block, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    frame.value_count = 1U;
    frame.value_offsets = &offset;
    frame.frame_size = 32U;
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    target.data_layout = minic_x86_64_linux_data_layout();
    if (!minic_x86_64_machine_select_core_subset(&machine,
            &target, &core, &frame) ||
        !minic_core_function_mark_reachable_blocks(&core,
            reachable, 1U) ||
        !minic_machine_core_coverage_verify(&core,
            reachable, &machine)) goto done;
    for (i = 0U; i < machine.blocks[1].instruction_count; ++i) {
        const MinicMachineInstruction *candidate =
            &machine.blocks[1].instructions[i];
        if (candidate->opcode == MINIC_X86_MI_CALL_SYSV_SYMBOL &&
            candidate->has_core_origin) ++calls;
    }
    okay = calls == 1U;
done:
    if (!okay) fprintf(stderr, "X86_CORE_MIR=FAIL call\n");
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return okay;
}

int main(void) {
    if (!scenario(false, false) || !scenario(false, true) ||
        !scenario(true, false) || !scenario_parameter() ||
        !scenario_float() || !scenario_direct_call()) return 1;
    puts("X86_CORE_MIR=PASS cycle=1 physical_alu=1 parameter=1 sse2=1 direct_call=1 origin=1 fail_closed=1");
    return 0;
}
