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

/* Exercise the real call planner's sealed layout rather than admitting a
 * second hand-rolled ABI classifier in the selector: 8 INTEGER + 9 SSE
 * values exhaust both register banks and overflow onto three stack slots.
 * A variadic signature additionally requires the final %al SSE count. */
static bool scenario_planned_scalar_call(bool indirect, bool ms) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicTargetInfo target = {0};
    MinicX86Frame frame = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId integer_id, float_id, signature_id, pointer_id;
    MinicCoreTypeId parameter_types[17] = {0};
    MinicCoreTypeId incoming_id;
    MinicCoreBlockId block;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreValueId values[20] = {0};
    MinicX86ArgLocation outgoing_locations[17] = {{0}};
    MinicX86OutgoingCallSite outgoing = {0};
    MinicX86ArgLocation incoming = {0};
    size_t offsets[20] = {0};
    size_t i, argc = indirect ? 2U : ms ? 5U : 17U;
    size_t ins_id = SIZE_MAX;
    size_t call_count = 0U, stack_writes = 0U;
    size_t sse_moves = 0U, variadic_al = 0U, target_r11 = 0U;
    FILE *output = NULL;
    char assembly[18000];
    size_t assembly_length;
    bool reachable[1] = {false};
    bool okay = false;
    const char *stage = "initialize";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("x86_planned_call"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "x86_planned_call", sizeof("x86_planned_call"));
    core.name_length = sizeof("x86_planned_call") - 1U;
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.integer.is_signed = true;
    type.value.integer.bit_width = 64U;
    if (!minic_core_type_register(&types, &type, &integer_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    if (!minic_core_type_register(&types, &type, &float_id)) goto done;
    for (i = 0U; i < 17U; ++i)
        parameter_types[i] = i < 8U ? integer_id : float_id;
    if (indirect) parameter_types[1] = float_id;
    if (ms) {
        parameter_types[1] = float_id;
        parameter_types[3] = float_id;
    }
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = integer_id;
    type.value.function.parameters = parameter_types;
    type.value.function.parameter_count = ms ? 5U : 2U;
    type.value.function.is_variadic = !indirect && !ms;
    type.value.function.calling_convention =
        ms ? MINIC_CALLING_CONVENTION_MS_X64 :
            MINIC_CALLING_CONVENTION_DEFAULT;
    if (!minic_core_type_register(&types, &type, &signature_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = signature_id;
    if (!minic_core_type_register(&types, &type, &pointer_id)) goto done;
    incoming_id = pointer_id;
    if (!minic_core_function_define_signature(&core, &types,
            integer_id, indirect ? &incoming_id : NULL,
            indirect ? 1U : 0U, MINIC_CALLING_CONVENTION_DEFAULT,
            false) || !minic_core_function_add_block(&core, &block))
        goto done;
    core.call_arguments = calloc(argc, sizeof(*core.call_arguments));
    if (core.call_arguments == NULL) goto done;
    core.call_argument_count = argc;
    core.call_argument_capacity = argc;
    if (indirect) {
        core.call_signatures = calloc(1U, sizeof(*core.call_signatures));
        if (core.call_signatures == NULL) goto done;
        core.call_signature_count = 1U;
        core.call_signature_capacity = 1U;
        core.call_signatures[0].core_function_type_id = signature_id;
        ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
        ins.core_type_id = pointer_id;
        ins.value.parameter_index = 0U;
        if (!minic_core_function_append_value_instruction(&core,
                block, &ins, &values[0])) goto done;
    } else {
        core.callees = calloc(1U, sizeof(*core.callees));
        if (core.callees == NULL) goto done;
        core.callee_count = 1U;
        core.callee_capacity = 1U;
        core.callees[0].name = malloc(sizeof("planned_callee"));
        if (core.callees[0].name == NULL) goto done;
        memcpy(core.callees[0].name, "planned_callee",
            sizeof("planned_callee"));
        core.callees[0].name_length = sizeof("planned_callee") - 1U;
        core.callees[0].core_function_type_id = signature_id;
    }
    for (i = 0U; i < argc; ++i) {
        const size_t index = i + (indirect ? 1U : 0U);
        ins = (MinicCoreInstruction){0};
        ins.core_type_id = ms ?
            (i == 1U || i == 3U ? float_id : integer_id) :
            i < 8U ? integer_id : float_id;
        if (indirect && i == 1U) ins.core_type_id = float_id;
        ins.kind = ins.core_type_id == integer_id ?
            MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT :
            MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
        if (ins.kind == MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT)
            ins.value.integer_value = (int64_t)(i + 10U);
        else ins.value.floating_bits = UINT64_C(0x3ff0000000000000);
        if (!minic_core_function_append_value_instruction(&core,
                block, &ins, &values[index])) goto done;
        core.call_arguments[i].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        core.call_arguments[i].value.value_id = values[index];
    }
    ins = (MinicCoreInstruction){0};
    ins.core_type_id = integer_id;
    if (indirect) {
        ins.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
        ins.value.indirect_call.callee = values[0];
        ins.value.indirect_call.signature_id = 0U;
        ins.value.indirect_call.argument_begin = 0U;
        ins.value.indirect_call.argument_count = argc;
        ins.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    } else {
        ins.kind = MINIC_CORE_INSTRUCTION_CALL;
        ins.value.call.callee_id = 0U;
        ins.value.call.argument_begin = 0U;
        ins.value.call.argument_count = argc;
        ins.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    }
    ins_id = core.instruction_count;
    if (!minic_core_function_append_value_instruction(&core,
            block, &ins, &values[argc + (indirect ? 1U : 0U)])) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = values[argc + (indirect ? 1U : 0U)];
    if (!minic_core_function_set_terminator(&core, block, &term) ||
        !minic_core_function_attach_type_context(&core, &types) ||
        !minic_core_function_verify(&core)) goto done;
    stage = "frame";
    for (i = 0U; i < 20U; ++i) offsets[i] = (i + 1U) * 8U;
    frame.value_offsets = offsets;
    frame.value_count = core.value_count;
    frame.frame_size = 256U;
    if (indirect) {
        incoming.kind = MINIC_X86_ARG_INTEGER;
        incoming.gp_index = 0U;
        frame.incoming_arguments.locations = &incoming;
        frame.incoming_arguments.argument_count = 1U;
        frame.incoming_arguments.calling_convention =
            MINIC_CALLING_CONVENTION_DEFAULT;
    }
    for (i = 0U; i < argc; ++i) {
        MinicX86ArgLocation *loc = &outgoing_locations[i];
        if (indirect) {
            loc->kind = i == 0U ? MINIC_X86_ARG_INTEGER : MINIC_X86_ARG_SSE;
            loc->gp_index = 0U;
            loc->fp_index = 0U;
        } else if (ms) {
            loc->kind = i == 1U || i == 3U ?
                MINIC_X86_ARG_SSE : MINIC_X86_ARG_INTEGER;
            loc->gp_index = i;
            loc->fp_index = i;
            if (i >= 4U) {
                loc->on_stack = true;
                loc->stack_index = i;
            }
        } else if (i < 8U) {
            loc->kind = MINIC_X86_ARG_INTEGER;
            loc->gp_index = i;
            if (i >= 6U) {
                loc->on_stack = true;
                loc->stack_index = i - 6U;
            }
        } else {
            loc->kind = MINIC_X86_ARG_SSE;
            loc->fp_index = i - 8U;
            if (i == 16U) {
                loc->on_stack = true;
                loc->stack_index = 2U;
            }
        }
    }
    outgoing.instruction_id = ins_id;
    outgoing.plan.locations = outgoing_locations;
    outgoing.plan.argument_count = argc;
    outgoing.plan.stack_bytes = indirect ? 0U : ms ? 48U : 32U;
    outgoing.plan.end.fp_used = indirect ? 1U : ms ? 2U : 8U;
    outgoing.plan.calling_convention = ms ?
        MINIC_CALLING_CONVENTION_MS_X64 :
        MINIC_CALLING_CONVENTION_DEFAULT;
    outgoing.plan.is_variadic = !indirect && !ms;
    frame.outgoing_call_plans.sites = &outgoing;
    frame.outgoing_call_plans.count = 1U;
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    target.data_layout = minic_x86_64_linux_data_layout();
    if (ms) target.call_lowering_capabilities =
        MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR;
    stage = "select";
    if (!minic_x86_64_machine_select_core_subset(&machine,
            &target, &core, &frame) ||
        !minic_core_function_mark_reachable_blocks(&core,
            reachable, 1U) ||
        !minic_machine_core_coverage_verify(&core,
            reachable, &machine)) goto done;
    for (i = 0U; i < machine.blocks[1].instruction_count; ++i) {
        const MinicMachineInstruction *op =
            &machine.blocks[1].instructions[i];
        if (op->opcode == (indirect ? MINIC_X86_MI_CALL_SYSV_R :
                ms ? MINIC_X86_MI_CALL_MS_SYMBOL :
                    MINIC_X86_MI_CALL_SYSV_SYMBOL)) ++call_count;
        if (op->opcode == MINIC_X86_MI_MOV64_RA) ++stack_writes;
        if (op->opcode == MINIC_X86_MI_MOVQ_GPR_XMM) ++sse_moves;
        if (op->opcode == MINIC_X86_MI_MOV8_IR) ++variadic_al;
        if (indirect && op->opcode == MINIC_X86_MI_MOV64_FRBP_R &&
            op->operand_count == 2U &&
            op->operands[1].kind == MINIC_MACHINE_OPERAND_REGISTER &&
            op->operands[1].value.physical.reg.number == MINIC_X86_REG_R11)
            ++target_r11;
    }
    stage = "machine-ops";
    okay = call_count == 1U &&
        (indirect ? (stack_writes == 0U && sse_moves >= 1U &&
            target_r11 == 1U && variadic_al == 0U) :
        ms ? (stack_writes == 1U && sse_moves >= 2U &&
            variadic_al == 0U) :
            (stack_writes == 3U && sse_moves >= 8U && variadic_al == 1U));
    if (!okay) goto done;
    stage = "gnu-assembly";
    output = tmpfile();
    if (output == NULL ||
        !minic_x86_64_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_emit_body(output, &machine, &policy) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0) {
        okay = false;
        goto done;
    }
    assembly_length = fread(assembly, 1U, sizeof(assembly) - 1U, output);
    assembly[assembly_length] = '\0';
    okay = indirect ? strstr(assembly, "call *%r11") != NULL :
        strstr(assembly, "call planned_callee@PLT") != NULL;
    if (okay && ms)
        okay = strstr(assembly, "subq $48, %rsp") != NULL &&
            strstr(assembly, "32(%rsp)") != NULL &&
            strstr(assembly, "addq $48, %rsp") != NULL;
    if (okay && !indirect && !ms)
        okay = strstr(assembly, "subq $32, %rsp") != NULL &&
            strstr(assembly, "movb $8, %al") != NULL &&
            strstr(assembly, "16(%rsp)") != NULL &&
            strstr(assembly, "addq $32, %rsp") != NULL;
    if (okay) {
        MinicMachineFunction rejected = {0};
        const size_t saved_bytes = outgoing.plan.stack_bytes;
        const size_t saved_count = frame.outgoing_call_plans.count;
        stage = "reject-forged-abi-plan";
        outgoing.plan.stack_bytes = 24U;
        okay = !minic_x86_64_machine_select_core_subset(&rejected,
            &target, &core, &frame);
        minic_machine_function_destroy(&rejected);
        outgoing.plan.stack_bytes = saved_bytes;
        frame.outgoing_call_plans.count = 0U;
        if (okay) {
            rejected = (MinicMachineFunction){0};
            okay = !minic_x86_64_machine_select_core_subset(&rejected,
                &target, &core, &frame);
            minic_machine_function_destroy(&rejected);
        }
        frame.outgoing_call_plans.count = saved_count;
    }
done:
    if (!okay) fprintf(stderr,
        "X86_CORE_MIR=FAIL planned-call indirect=%d ms=%d stage=%s\n",
        indirect ? 1 : 0, ms ? 1 : 0, stage);
    if (output != NULL) fclose(output);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return okay;
}

/* Record transfer comes from the old x86 emitter's exact byte transport.
 * Its generic Core shape and the ABI object sizes are not reclassified by
 * MachineIR. Check both volatile sides, empty record, REP MOVSB, and x86's
 * special overlapping vector copy separately. */
static bool scenario_record_transfer(size_t bytes, unsigned int mode,
    bool volatile_source, bool volatile_destination) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreType t = {0};
    MinicCoreTypeId void_type, byte_type, record_type, pointer_type;
    MinicCoreTypeId parameters[2];
    MinicCoreBlockId block;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreValueId first, second;
    MinicX86Frame frame = {0};
    MinicX86ArgLocation locations[2] = {{0}};
    MinicTargetInfo target = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicCoreObjectStorage storage = {0};
    size_t offsets[2] = {8U, 16U};
    size_t object_offset = 320U;
    bool reachable[1] = {false};
    FILE *output = NULL;
    char assembly[32768];
    size_t output_bytes;
    size_t reads = 0U, writes = 0U, volatile_reads = 0U;
    size_t volatile_writes = 0U, rep = 0U, i;
    bool okay = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("x86_record_transfer"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "x86_record_transfer", sizeof("x86_record_transfer"));
    core.name_length = sizeof("x86_record_transfer") - 1U;
    t.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &t, &void_type)) goto done;
    t = (MinicCoreType){0};
    t.kind = MINIC_CORE_TYPE_INTEGER;
    t.has_layout = true;
    t.size = 1U;
    t.alignment = 1U;
    t.value.integer.bit_width = 8U;
    if (!minic_core_type_register(&types, &t, &byte_type)) goto done;
    t = (MinicCoreType){0};
    t.kind = mode == 2U ? MINIC_CORE_TYPE_VECTOR : MINIC_CORE_TYPE_RECORD;
    t.has_layout = true;
    t.size = bytes;
    t.alignment = mode == 2U ? 16U : 1U;
    if (mode == 2U) {
        t.value.vector.element = byte_type;
        t.value.vector.element_count = 16U;
        t.value.vector.representation = MINIC_CORE_VECTOR_STORAGE_ONLY;
    } else t.value.record.is_complete = true;
    if (!minic_core_type_register(&types, &t, &record_type)) goto done;
    t = (MinicCoreType){0};
    t.kind = MINIC_CORE_TYPE_POINTER;
    t.has_layout = true;
    t.size = 8U;
    t.alignment = 8U;
    t.value.pointer.pointee = record_type;
    if (!minic_core_type_register(&types, &t, &pointer_type)) goto done;
    parameters[0] = pointer_type;
    parameters[1] = pointer_type;
    if (!minic_core_function_define_signature(&core, &types, void_type,
            parameters, 2U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &block)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = pointer_type;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&core,
            block, &ins, &first)) goto done;
    ins.value.parameter_index = 1U;
    if (!minic_core_function_append_value_instruction(&core,
            block, &ins, &second)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.core_type_id = record_type;
    if (mode == 0U) {
        ins.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
        ins.value.record_copy.source_address = first;
        ins.value.record_copy.destination_address = second;
        ins.value.record_copy.source_volatile = volatile_source;
        ins.value.record_copy.destination_volatile = volatile_destination;
    } else if (mode == 1U) {
        ins.kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
        ins.value.record_load.source_address = first;
        ins.value.record_load.destination_object = 0U;
        ins.value.record_load.is_volatile = volatile_source;
        core.objects = calloc(1U, sizeof(*core.objects));
        if (core.objects == NULL) goto done;
        core.objects[0].core_type_id = record_type;
        core.object_count = core.object_capacity = 1U;
    } else {
        ins.kind = MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY;
        ins.value.vector_storage_copy.source_address = first;
        ins.value.vector_storage_copy.destination_address = second;
        ins.value.vector_storage_copy.source_volatile = volatile_source;
        ins.value.vector_storage_copy.destination_volatile =
            volatile_destination;
    }
    if (!minic_core_function_append_effect_instruction(&core,
            block, &ins)) {
        /* Existing Core admission already rejects volatile storage-only
         * vector snapshots: it must never reach x86 machine selection. */
        if (mode == 2U && (volatile_source || volatile_destination))
            okay = true;
        goto done;
    }
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, block, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    target.data_layout = minic_x86_64_linux_data_layout();
    frame.value_offsets = offsets;
    frame.value_count = core.value_count;
    frame.object_count = core.object_count;
    frame.object_offsets = core.object_count != 0U ? &object_offset : NULL;
    frame.frame_size = 512U;
    if (core.object_count != 0U) {
        storage.extent = bytes;
        storage.alignment = 1U;
        frame.object_storage.objects = &storage;
        frame.object_storage.count = 1U;
    }
    for (i = 0U; i < 2U; ++i) {
        locations[i].kind = MINIC_X86_ARG_INTEGER;
        locations[i].gp_index = i;
    }
    frame.incoming_arguments.locations = locations;
    frame.incoming_arguments.argument_count = 2U;
    frame.incoming_arguments.calling_convention =
        MINIC_CALLING_CONVENTION_DEFAULT;
    stage = "select";
    if (mode == 2U && (volatile_source || volatile_destination)) {
        okay = !minic_x86_64_machine_select_core_subset(
            &machine, &target, &core, &frame);
        goto done;
    }
    if (bytes == 0U) {
        okay = !minic_x86_64_machine_select_core_subset(
            &machine, &target, &core, &frame);
        goto done;
    }
    if (!minic_x86_64_machine_select_core_subset(
            &machine, &target, &core, &frame) ||
        !minic_core_function_mark_reachable_blocks(&core,
            reachable, 1U) ||
        !minic_machine_core_coverage_verify(&core,
            reachable, &machine) ||
        !minic_x86_64_machine_gnu_asm_policy(&target, &policy)) goto done;
    stage = "machine-effects";
    for (i = 0U; i < machine.blocks[1].instruction_count; ++i) {
        const MinicMachineInstruction *op =
            &machine.blocks[1].instructions[i];
        if (op->opcode == MINIC_X86_MI_REP_MOVSB) ++rep;
        if (op->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) {
            ++reads;
            if (op->effects & MINIC_MACHINE_EFFECT_VOLATILE)
                ++volatile_reads;
        }
        if (op->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE) {
            ++writes;
            if (op->effects & MINIC_MACHINE_EFFECT_VOLATILE)
                ++volatile_writes;
        }
    }
    if (bytes != 0U && (reads == 0U || writes == 0U ||
         (bytes > 128U) != (rep == 1U) ||
         (volatile_source && volatile_reads == 0U) ||
         (volatile_destination && volatile_writes == 0U))) goto done;
    stage = "gnu-assembly";
    output = tmpfile();
    if (output == NULL ||
        !minic_machine_gnu_asm_emit_body(output, &machine, &policy) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0)
        goto done;
    output_bytes = fread(assembly, 1U, sizeof(assembly) - 1U, output);
    assembly[output_bytes] = '\0';
    okay = bytes == 0U ? rep == 0U : mode == 2U ?
        strstr(assembly, "movq 8(%r11), %rdx") != NULL :
        bytes > 128U ? strstr(assembly, "rep movsb") != NULL :
        strstr(assembly, "movzbq") != NULL;
done:
    if (!okay) fprintf(stderr,
        "X86_CORE_MIR=FAIL record bytes=%zu mode=%u vs=%d vd=%d stage=%s\n",
        bytes, mode, volatile_source ? 1 : 0,
        volatile_destination ? 1 : 0, stage);
    if (output != NULL) fclose(output);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return okay;
}

/* Four existing SysV record-parameter classes from call_plan.c. Typed
 * selection must read their sealed incoming positions and preserve odd
 * payload tails, stack-memory structs, and the empty-record fail-closed
 * policy without inventing a second ABI classifier. */
static bool scenario_parameter_object(size_t bytes,
    MinicX86ArgClass kind, bool stack, bool forge) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_id, record_id;
    MinicCoreBlockId block;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreObjectStorage storage = {0};
    MinicX86ArgLocation location = {0};
    MinicX86Frame frame = {0};
    MinicTargetInfo target = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    size_t unused_offset = 8U, object_offset = 320U;
    bool reachable[1] = {false};
    FILE *output = NULL;
    char assembly[22000];
    size_t length, writes = 0U, rep = 0U, rcx_preserved = 0U, i;
    bool okay = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("x86_parameter_record"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "x86_parameter_record",
        sizeof("x86_parameter_record"));
    core.name_length = sizeof("x86_parameter_record") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = 1U;
    type.value.record.is_complete = true;
    if (!minic_core_type_register(&types, &type, &record_id) ||
        !minic_core_function_define_signature(&core, &types,
            void_id, &record_id, 1U,
            MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &block)) goto done;
    core.objects = calloc(1U, sizeof(*core.objects));
    if (core.objects == NULL) goto done;
    core.objects[0].core_type_id = record_id;
    core.object_count = core.object_capacity = 1U;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT;
    ins.core_type_id = void_id;
    ins.value.parameter_object.parameter_index = 0U;
    ins.value.parameter_object.object_id = 0U;
    if (!minic_core_function_append_effect_instruction(&core,
            block, &ins)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, block, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    target.data_layout = minic_x86_64_linux_data_layout();
    frame.value_offsets = &unused_offset;
    frame.value_count = core.value_count;
    frame.object_count = 1U;
    frame.object_offsets = &object_offset;
    frame.frame_size = 512U;
    storage.extent = bytes;
    storage.alignment = 1U;
    frame.object_storage.objects = &storage;
    frame.object_storage.count = 1U;
    location.kind = kind;
    location.record_bytes = forge ? bytes + 1U : bytes;
    location.on_stack = stack;
    location.gp_index = 0U;
    location.stack_index = 0U;
    frame.incoming_arguments.locations = &location;
    frame.incoming_arguments.argument_count = 1U;
    frame.incoming_arguments.calling_convention =
        MINIC_CALLING_CONVENTION_DEFAULT;
    stage = "select";
    if (kind == MINIC_X86_ARG_RECORD_EMPTY || forge) {
        okay = !minic_x86_64_machine_select_core_subset(
            &machine, &target, &core, &frame);
        goto done;
    }
    if (!minic_x86_64_machine_select_core_subset(
            &machine, &target, &core, &frame) ||
        !minic_core_function_mark_reachable_blocks(&core,
            reachable, 1U) ||
        !minic_machine_core_coverage_verify(&core,
            reachable, &machine) ||
        !minic_x86_64_machine_gnu_asm_policy(&target, &policy)) goto done;
    stage = "machine-effects";
    for (i = 0U; i < machine.blocks[1].instruction_count; ++i) {
        const MinicMachineInstruction *op =
            &machine.blocks[1].instructions[i];
        if (op->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE) ++writes;
        if (op->opcode == MINIC_X86_MI_REP_MOVSB) ++rep;
        if (op->opcode == MINIC_X86_MI_POP64_R &&
            op->operands[0].value.physical.reg.number ==
                MINIC_X86_REG_RCX) ++rcx_preserved;
    }
    if (writes == 0U || (kind == MINIC_X86_ARG_RECORD_MEMORY &&
        bytes > 128U && rep != 1U) ||
        ((bytes == 7U || bytes == 15U) && rcx_preserved == 0U))
        goto done;
    stage = "gnu-asm";
    output = tmpfile();
    if (output == NULL ||
        !minic_machine_gnu_asm_emit_body(output, &machine, &policy) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0)
        goto done;
    length = fread(assembly, 1U, sizeof(assembly) - 1U, output);
    assembly[length] = '\0';
    okay = kind == MINIC_X86_ARG_RECORD_MEMORY ?
        strstr(assembly, "rep movsb") != NULL :
        strstr(assembly, "movb %al, 6(%r10)") != NULL ||
        strstr(assembly, "movb %al, 14(%r10)") != NULL;
done:
    if (!okay) fprintf(stderr,
        "X86_CORE_MIR=FAIL parameter-object bytes=%zu class=%d stack=%d "
        "forge=%d stage=%s\n", bytes, (int)kind, stack ? 1 : 0,
        forge ? 1 : 0, stage);
    if (output != NULL) fclose(output);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return okay;
}

int main(void) {
    if (!scenario(false, false) || !scenario(false, true) ||
        !scenario(true, false) || !scenario_parameter() ||
        !scenario_float() || !scenario_direct_call() ||
        !scenario_planned_scalar_call(false, false) ||
        !scenario_planned_scalar_call(true, false) ||
        !scenario_planned_scalar_call(false, true) ||
        !scenario_record_transfer(0U, 0U, false, false) ||
        !scenario_record_transfer(7U, 0U, true, true) ||
        !scenario_record_transfer(129U, 0U, false, false) ||
        !scenario_record_transfer(32U, 1U, true, false) ||
        !scenario_record_transfer(16U, 2U, false, false) ||
        !scenario_parameter_object(7U,
            MINIC_X86_ARG_RECORD_ONE_GP, false, false) ||
        !scenario_parameter_object(15U,
            MINIC_X86_ARG_RECORD_TWO_GP, false, false) ||
        !scenario_parameter_object(256U,
            MINIC_X86_ARG_RECORD_MEMORY, true, false) ||
        !scenario_parameter_object(0U,
            MINIC_X86_ARG_RECORD_EMPTY, false, false) ||
        !scenario_parameter_object(7U,
            MINIC_X86_ARG_RECORD_ONE_GP, false, true)) return 1;
    puts("X86_CORE_MIR=PASS cycle=1 physical_alu=1 parameter=1 sse2=1 direct_call=1 mixed_vararg_stack=1 indirect_call=1 ms_shadow_stack=1 record_copy=1 record_load=1 vector_storage=1 record_abi=1 volatile=1 empty_record_reject=1 rep_movsb=1 origin=1 fail_closed=1");
    return 0;
}
