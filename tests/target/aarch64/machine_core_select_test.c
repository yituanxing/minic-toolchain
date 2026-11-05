/* A real Core CFG, with a backedge to Core block zero, must never branch to
 * the external function symbol (which would execute the prologue twice).
 * This is distinct from synthetic frame-only MachineIR instruction tests. */
#include "target/aarch64/machine_core_select.h"
#include "target/aarch64/machine_opcode.h"
#include "target/common/machine_core_coverage.h"
#include "target/common/machine_gnu_asm.h"
#include "target/data_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool build_cycle(MinicCoreFunction *core,
    MinicCoreTypeContext *types, bool unsupported) {
    MinicCoreType type = {0};
    MinicCoreTypeId void_type;
    MinicCoreTerminator term = {0};
    MinicCoreBlockId entry, backedge;
    type.kind = MINIC_CORE_TYPE_VOID;
    minic_core_function_initialize(core);
    core->name = malloc(sizeof("a64_cycle"));
    if (core->name == NULL) return false;
    memcpy(core->name, "a64_cycle", sizeof("a64_cycle"));
    core->name_length = sizeof("a64_cycle") - 1U;
    if (!minic_core_type_register(types, &type, &void_type) ||
        !minic_core_function_define_signature(core, types,
            void_type, NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(core, &entry) ||
        !minic_core_function_add_block(core, &backedge)) return false;
    if (unsupported) {
        MinicCoreInstruction instruction = {0};
        instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
        instruction.core_type_id = void_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        if (!minic_core_function_append_effect_instruction(core,
                entry, &instruction)) return false;
    }
    term.kind = MINIC_CORE_TERMINATOR_BRANCH;
    term.branch_target = backedge;
    if (!minic_core_function_set_terminator(core, entry, &term)) return false;
    term.branch_target = entry;
    return minic_core_function_set_terminator(core, backedge, &term) &&
        minic_core_function_attach_type_context(core, types) &&
        minic_core_function_verify(core);
}

static bool run_case(bool unsupported) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract abi = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext context = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    const bool reachable[2] = {true, true};
    FILE *output = NULL;
    char text[4096];
    size_t length;
    bool ok = false;
    const char *failure_stage = "construct-core";
    minic_core_type_context_initialize(&types);
    if (!build_cycle(&core, &types, unsupported)) goto done;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    abi.frame_alignment = 16U;
    abi.saved_entry_sp_offset = SIZE_MAX;
    context = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &abi, .frame_plan = &plan,
        .name = core.name
    };
    if (unsupported) {
        failure_stage = "fail-closed-unsupported";
        ok = !minic_aarch64_machine_select_core_subset(&machine, &context);
        goto done;
    }
    failure_stage = "select-and-preflight";
    if (!minic_aarch64_machine_select_core_subset(&machine, &context))
        goto done;
    failure_stage = "structure-and-preflight";
    if (machine.generated_features != MINIC_A64_MACHINE_FEATURE_BASE ||
        machine.block_count != 3U ||
        !machine.blocks[0].machine_auxiliary ||
        machine.blocks[1].machine_auxiliary ||
        machine.blocks[2].machine_auxiliary ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    failure_stage = "serialize";
    output = tmpfile();
    if (output == NULL ||
        !minic_machine_gnu_asm_emit_body(output, &machine, &policy) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0) goto done;
    length = fread(text, 1U, sizeof(text) - 1U, output);
    text[length] = '\0';
    failure_stage = "text-branch-invariants";
    /* Function entry branches to the Core entry's private label. The second
     * branch (the Core backedge) also targets that label, never a64_cycle. */
    ok = strstr(text, "a64_cycle:\n") == NULL &&
         strstr(text, "  b .Lminic_mir_a64_cycle_1\n") != NULL &&
         strstr(text, "  b .Lminic_mir_a64_cycle_2\n") != NULL &&
         strstr(text, ".Lminic_mir_a64_cycle_1:\n") != NULL &&
         strstr(text, ".Lminic_mir_a64_cycle_2:\n") != NULL &&
         strstr(text, "  b a64_cycle\n") == NULL;
done:
    if (!ok) fprintf(stderr, "A64 CFG test failed at %s (unsupported=%d)\n",
        failure_stage, unsupported ? 1 : 0);
    if (output != NULL) fclose(output);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* A complete Core binary64 expression crosses typed physical FP registers
 * and returns in the AAPCS64 d0 bank. This catches fake FP-as-GP emission,
 * omitted Core float arithmetic and a scalar-only return terminator. */
static bool run_fp_case(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract abi = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext context = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    const bool reachable[1] = {true};
    MinicCoreType type = {0};
    MinicCoreTypeId double_type;
    MinicCoreBlockId entry;
    MinicCoreValueId values[3];
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    size_t offsets[3] = {0U, 8U, 16U};
    FILE *output = NULL;
    char assembly[8192];
    size_t n;
    bool ok = false;
    const char *stage = "construct";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    type.value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    if (!minic_core_type_register(&types, &type, &double_type)) goto cleanup;
    core.name = malloc(sizeof("a64_fp"));
    if (core.name == NULL) goto cleanup;
    memcpy(core.name, "a64_fp", sizeof("a64_fp"));
    core.name_length = sizeof("a64_fp") - 1U;
    if (!minic_core_function_define_signature(&core, &types, double_type,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
    ins.core_type_id = double_type;
    ins.value.floating_bits = UINT64_C(0x3ff0000000000000);
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &values[0])) goto cleanup;
    ins.value.floating_bits = UINT64_C(0x4000000000000000);
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &values[1])) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_DOUBLE_ADD;
    ins.value.binary.left = values[0];
    ins.value.binary.right = values[1];
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &values[2])) goto cleanup;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = values[2];
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types) ||
        !minic_core_function_verify(&core)) goto cleanup;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    abi.frame_alignment = 16U;
    abi.frame_size = 32U;
    abi.saved_entry_sp_offset = SIZE_MAX;
    plan.value_count = 3U;
    plan.value_offsets = offsets;
    context = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &abi, .frame_plan = &plan, .name = core.name
    };
    stage = "selection";
    if (!minic_aarch64_machine_select_core_subset(&machine, &context))
        goto cleanup;
    stage = "preflight";
    if (machine.generated_features !=
            (MINIC_A64_MACHINE_FEATURE_BASE |
             MINIC_A64_MACHINE_FEATURE_FP_SIMD) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto cleanup;
    stage = "serialization";
    output = tmpfile();
    if (output == NULL ||
        !minic_machine_gnu_asm_emit_body(output, &machine, &policy) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0)
        goto cleanup;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, output);
    assembly[n] = '\0';
    stage = "binary64-invariants";
    ok = strstr(assembly, "  fadd d9, d9, d10\n") != NULL &&
         strstr(assembly, "  ldr d0, [sp, #16]\n") != NULL &&
         strstr(assembly, "  movz x9, #0, lsl #0\n") != NULL &&
         strstr(assembly, "  movk x9, #16368, lsl #48\n") != NULL;
cleanup:
    if (!ok) {
        fprintf(stderr, "A64 FP case failed at %s\n", stage);
        if (strcmp(stage, "binary64-invariants") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (output != NULL) fclose(output);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Target ABI ingress, i128 memory loads and stores must all pass through
 * typed MachineIR. In particular there must be TWO 64-bit physical memory
 * effects, and no truncation of the upper 64-bit limb. */
static bool run_wide_case(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract abi = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext context = {0};
    MinicLP64ArgumentPlan incoming = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    const bool reachable[1] = {true};
    MinicCoreType type = {0};
    MinicCoreTypeId void_type, i128_type, pointer_type;
    MinicCoreBlockId entry;
    MinicCoreValueId pointer_value, wide_value;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    size_t offsets[2] = {0U, SIZE_MAX};
    size_t wide_offsets[2] = {SIZE_MAX, 16U};
    FILE *output = NULL;
    char assembly[8192];
    size_t n;
    bool ok = false;
    const char *stage = "construct";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_type)) goto cleanup;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 16U;
    type.value.integer.bit_width = 128U;
    type.value.integer.is_signed = false;
    if (!minic_core_type_register(&types, &type, &i128_type)) goto cleanup;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    type.value.pointer.pointee = i128_type;
    if (!minic_core_type_register(&types, &type, &pointer_type)) goto cleanup;
    core.name = malloc(sizeof("a64_wide"));
    if (core.name == NULL) goto cleanup;
    memcpy(core.name, "a64_wide", sizeof("a64_wide"));
    core.name_length = sizeof("a64_wide") - 1U;
    if (!minic_core_function_define_signature(&core, &types, void_type,
            &pointer_type, 1U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = pointer_type;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &pointer_value)) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_LOAD;
    ins.core_type_id = i128_type;
    ins.value.load.address = pointer_value;
    ins.value.load.is_volatile = true;
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &wide_value)) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_STORE;
    ins.core_type_id = void_type;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.store.address = pointer_value;
    ins.value.store.stored_value = wide_value;
    ins.value.store.is_volatile = true;
    if (!minic_core_function_append_effect_instruction(&core, entry, &ins))
        goto cleanup;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types) ||
        !minic_core_function_verify(&core)) goto cleanup;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    abi.frame_alignment = 16U;
    abi.frame_size = 48U;
    abi.saved_entry_sp_offset = SIZE_MAX;
    plan.value_count = 2U;
    plan.value_offsets = offsets;
    plan.wide_offsets = wide_offsets;
    incoming.argument_count = 1U;
    incoming.arguments[0].gp_first = 0U;
    incoming.arguments[0].gp_registers = 1U;
    incoming.arguments[0].stack_first = SIZE_MAX;
    incoming.arguments[0].fp_first = SIZE_MAX;
    context = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &abi, .frame_plan = &plan,
        .incoming_plan = &incoming, .name = core.name
    };
    stage = "selection";
    if (!minic_aarch64_machine_select_core_subset(&machine, &context))
        goto cleanup;
    stage = "preflight";
    if (machine.generated_features != MINIC_A64_MACHINE_FEATURE_BASE ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto cleanup;
    output = tmpfile();
    stage = "serialization";
    if (output == NULL ||
        !minic_machine_gnu_asm_emit_body(output, &machine, &policy) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0)
        goto cleanup;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, output);
    assembly[n] = '\0';
    stage = "i128-memory-invariants";
    ok = strstr(assembly, "  ldr x9, [x10, #0]\n") != NULL &&
         strstr(assembly, "  ldr x11, [x10, #8]\n") != NULL &&
         strstr(assembly, "  str x9, [x10, #0]\n") != NULL &&
         strstr(assembly, "  str x11, [x10, #8]\n") != NULL;
cleanup:
    if (!ok) {
        fprintf(stderr, "A64 i128 case failed at %s\n", stage);
        if (strcmp(stage, "i128-memory-invariants") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (output != NULL) fclose(output);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

int main(void) {
    if (!run_case(false) || !run_case(true) ||
        !run_fp_case() || !run_wide_case()) {
        fprintf(stderr, "A64_CORE_MIR_CFG=FAIL\n");
        return 1;
    }
    puts("A64_CORE_MIR_CFG=PASS prologue_isolated=1 backedge_to_core0=1 fail_closed=1 fp64=1 i128=1");
    return 0;
}
