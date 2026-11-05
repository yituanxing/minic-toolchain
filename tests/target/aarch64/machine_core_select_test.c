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

/* Signed i64 -> i128 -> _Bool/i64 and a dynamic alloca in one Core function.
 * Exercises both i128 limbs, signed extension, actual overflow CFG, 16-byte
 * stack rounding and x19-anchored local value slots after SP moves. */
static bool run_residual_case(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract abi = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext context = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_type, s64_type, s128_type, bool_type,
        u64_type, pointer_type;
    MinicCoreBlockId entry;
    MinicCoreValueId values[6];
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    const bool reachable[1] = {true};
    size_t offsets[6] = {0U, SIZE_MAX, 8U, 16U, 24U, 32U};
    size_t wide_offsets[6] = {SIZE_MAX, 48U, SIZE_MAX, SIZE_MAX,
        SIZE_MAX, SIZE_MAX};
    char assembly[16384] = {0};
    FILE *out = NULL;
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
    type.size = type.alignment = 8U;
    type.value.integer.bit_width = 64U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &type, &s64_type)) goto cleanup;
    type.size = type.alignment = 16U;
    type.value.integer.bit_width = 128U;
    if (!minic_core_type_register(&types, &type, &s128_type)) goto cleanup;
    type.size = type.alignment = 1U;
    type.value.integer.bit_width = 8U;
    type.value.integer.is_signed = false;
    type.value.integer.is_bool = true;
    if (!minic_core_type_register(&types, &type, &bool_type)) goto cleanup;
    type.size = type.alignment = 8U;
    type.value.integer.bit_width = 64U;
    type.value.integer.is_bool = false;
    if (!minic_core_type_register(&types, &type, &u64_type)) goto cleanup;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    type.value.pointer.pointee = void_type;
    if (!minic_core_type_register(&types, &type, &pointer_type)) goto cleanup;
    core.name = malloc(sizeof("a64_residual"));
    if (core.name == NULL) goto cleanup;
    memcpy(core.name, "a64_residual", sizeof("a64_residual"));
    core.name_length = sizeof("a64_residual") - 1U;
    if (!minic_core_function_define_signature(&core, &types, void_type,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins.core_type_id = s64_type;
    ins.value.integer_value = -1;
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &values[0])) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION;
    ins.core_type_id = s128_type;
    ins.value.operand = values[0];
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &values[1])) goto cleanup;
    ins.core_type_id = bool_type;
    ins.value.operand = values[1];
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &values[2])) goto cleanup;
    ins.core_type_id = s64_type;
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &values[3])) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins.core_type_id = u64_type;
    ins.value.integer_value = 31;
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &values[4])) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC;
    ins.core_type_id = pointer_type;
    ins.value.operand = values[4];
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &values[5])) goto cleanup;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types) ||
        !minic_core_function_verify(&core)) goto cleanup;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    abi.frame_alignment = 16U;
    abi.frame_size = 96U;
    abi.save_offset = 80U;
    abi.has_dynamic_stack = true;
    abi.saved_entry_sp_offset = SIZE_MAX;
    plan.value_count = 6U;
    plan.value_offsets = offsets;
    plan.wide_offsets = wide_offsets;
    context = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &abi, .frame_plan = &plan,
        .name = core.name
    };
    stage = "selection";
    if (!minic_aarch64_machine_select_core_subset(&machine, &context))
        goto cleanup;
    stage = "coverage-and-preflight";
    if (!machine.has_dynamic_stack || machine.block_count != 4U ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto cleanup;
    stage = "serialize";
    out = tmpfile();
    if (out == NULL ||
        !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto cleanup;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[n] = '\0';
    stage = "physical-invariants";
    ok = strstr(assembly, "  asr x10, x9, #63\n") != NULL &&
        strstr(assembly, "  orr x9, x9, x10\n") != NULL &&
        strstr(assembly, "  adds x10, x9, #15\n") != NULL &&
        strstr(assembly, "  b.hs .Lminic_mir_a64_residual_3\n") != NULL &&
        strstr(assembly, "  and x10, x10, #-16\n") != NULL &&
        strstr(assembly, "  sub sp, sp, x10\n") != NULL &&
        strstr(assembly, "  brk #0\n") != NULL;
    if (ok) {
        /* Reject the ENTIRE Core function when the Common frame planner
         * hasn't reserved a dynamic-frame base. No legacy alloca fallback. */
        stage = "fail-closed-without-dynamic-frame";
        minic_machine_function_destroy(&machine);
        abi.has_dynamic_stack = false;
        ok = !minic_aarch64_machine_select_core_subset(&machine, &context);
    }
cleanup:
    if (!ok) {
        fprintf(stderr, "A64 residual case failed at %s\n", stage);
        if (strcmp(stage, "physical-invariants") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Large record copies must own a physical CFG loop. MachineBuilder block
 * creation changes the insertion point; the original Core block has to
 * branch into that loop, not accidentally make the done block its source. */
static bool run_record_loop_case(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract abi = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64ArgumentPlan incoming = {0};
    MinicDirectLP64BlockContext context = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_type, record_type, ptr_type;
    MinicCoreTypeId parameters[2];
    MinicCoreBlockId entry;
    MinicCoreValueId pointer[2];
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    const bool reachable[1] = {true};
    size_t offsets[2] = {0U, 8U};
    char assembly[8192] = {0};
    FILE *out = NULL;
    size_t n;
    bool ok = false;
    const char *stage = "construct";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_type)) goto cleanup;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 128U;
    type.alignment = 8U;
    if (!minic_core_type_register(&types, &type, &record_type)) goto cleanup;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    type.value.pointer.pointee = record_type;
    if (!minic_core_type_register(&types, &type, &ptr_type)) goto cleanup;
    core.name = malloc(sizeof("a64_record_loop"));
    if (core.name == NULL) goto cleanup;
    memcpy(core.name, "a64_record_loop", sizeof("a64_record_loop"));
    core.name_length = sizeof("a64_record_loop") - 1U;
    parameters[0] = parameters[1] = ptr_type;
    if (!minic_core_function_define_signature(&core, &types, void_type,
            parameters, 2U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = ptr_type;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &pointer[0])) goto cleanup;
    ins.value.parameter_index = 1U;
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &pointer[1])) goto cleanup;
    ins.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    ins.core_type_id = record_type;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.record_copy.source_address = pointer[0];
    ins.value.record_copy.destination_address = pointer[1];
    ins.value.record_copy.source_volatile = true;
    ins.value.record_copy.destination_volatile = true;
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
    abi.frame_size = 32U;
    abi.frame_alignment = 16U;
    abi.saved_entry_sp_offset = SIZE_MAX;
    plan.value_count = 2U;
    plan.value_offsets = offsets;
    incoming.argument_count = 2U;
    incoming.arguments[0].gp_first = 0U;
    incoming.arguments[0].gp_registers = 1U;
    incoming.arguments[1].gp_first = 1U;
    incoming.arguments[1].gp_registers = 1U;
    context = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &abi, .frame_plan = &plan,
        .incoming_plan = &incoming, .name = core.name
    };
    stage = "selection";
    if (!minic_aarch64_machine_select_core_subset(&machine, &context))
        goto cleanup;
    stage = "coverage-and-preflight";
    if (machine.block_count != 4U ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto cleanup;
    stage = "serialize";
    out = tmpfile();
    if (out == NULL ||
        !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto cleanup;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[n] = '\0';
    stage = "loop-invariants";
    ok = strstr(assembly, "  b .Lminic_mir_a64_record_loop_2\n") != NULL &&
        strstr(assembly, "  ldrb w11, [x9, #0]\n") != NULL &&
        strstr(assembly, "  strb w11, [x10, #0]\n") != NULL &&
        strstr(assembly, "  cbnz x12, .Lminic_mir_a64_record_loop_2\n") != NULL &&
        strstr(assembly, "  ret\n") != NULL;
cleanup:
    if (!ok) {
        fprintf(stderr, "A64 record-copy case failed at %s\n", stage);
        if (strcmp(stage, "loop-invariants") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Port of the proven A64 block_emit parameter_object ingress: both the
 * register/stack eightbyte route and by-reference record copying now use
 * typed Machine operations, with Common ABI/object placement unmodified. */
static bool run_record_parameter_case(size_t bytes, bool indirect,
    bool incoming_stack) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_type, record_type;
    MinicCoreInstruction ins = {0};
    MinicCoreBlockId entry;
    MinicCoreTerminator ret = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract abi = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64ArgumentPlan incoming = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[1] = {true};
    size_t object_offset[1] = {32U};
    size_t writes = 0U, reads = 0U, auxiliary = 0U, i, j;
    FILE *out = NULL;
    char assembly[16384];
    size_t length;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("a64_record_parameter"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "a64_record_parameter",
        sizeof("a64_record_parameter"));
    core.name_length = sizeof("a64_record_parameter") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_type)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = 1U;
    type.value.record.is_complete = true;
    if (!minic_core_type_register(&types, &type, &record_type) ||
        !minic_core_function_define_signature(&core, &types, void_type,
            &record_type, 1U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    core.objects = calloc(1U, sizeof(*core.objects));
    if (core.objects == NULL) goto done;
    core.objects[0].core_type_id = record_type;
    core.object_count = core.object_capacity = 1U;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT;
    ins.core_type_id = void_type;
    ins.value.parameter_object.parameter_index = 0U;
    ins.value.parameter_object.object_id = 0U;
    if (!minic_core_function_append_effect_instruction(&core, entry, &ins))
        goto done;
    ret.kind = MINIC_CORE_TERMINATOR_RETURN;
    ret.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &ret) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    abi.frame_size = 256U;
    abi.frame_alignment = 16U;
    abi.saved_entry_sp_offset = SIZE_MAX;
    plan.object_count = 1U;
    plan.object_offsets = object_offset;
    incoming.argument_count = 1U;
    incoming.arguments[0].kind = indirect ?
        MINIC_LP64_ARGUMENT_INDIRECT_RECORD : MINIC_LP64_ARGUMENT_RECORD;
    incoming.arguments[0].size_bytes = bytes;
    incoming.arguments[0].chunks = indirect ? 1U : (bytes + 7U) / 8U;
    if (incoming_stack) {
        incoming.arguments[0].stack_first = 0U;
        incoming.arguments[0].stack_slots = indirect ? 1U :
            incoming.arguments[0].chunks;
    } else {
        incoming.arguments[0].gp_first = 0U;
        incoming.arguments[0].gp_registers = indirect ? 1U :
            incoming.arguments[0].chunks;
    }
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &abi, .frame_plan = &plan,
        .incoming_plan = &incoming, .name = core.name
    };
    stage = "selection";
    if (!minic_aarch64_machine_select_core_subset(&machine, &ctx)) goto done;
    stage = "coverage";
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        goto done;
    stage = "gnu-preflight";
    if (!minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    for (i = 0U; i < machine.block_count; ++i) {
        const MinicMachineBlock *block = &machine.blocks[i];
        if (block->machine_auxiliary && i != 0U) ++auxiliary;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *m = &block->instructions[j];
            if (!m->has_core_origin || m->core_instruction_id != 0U) continue;
            if (m->opcode == MINIC_A64_MACHINE_STRB) ++writes;
            if (m->opcode == MINIC_A64_MACHINE_LDRB) ++reads;
        }
    }
    stage = "physical-serialization";
    out = tmpfile();
    if (out == NULL ||
        !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto done;
    length = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[length] = '\0';
    stage = "physical-invariants";
    ok = writes == (bytes > 64U ? 1U : bytes) &&
        reads == (indirect ? (bytes > 64U ? 1U : bytes) : 0U) &&
        (bytes > 64U ? auxiliary >= 2U : auxiliary == 0U) &&
        strstr(assembly, indirect ? "  strb w11, [x10, #0]\n" :
            "  strb w9, [x10, #0]\n") != NULL &&
        (incoming_stack ||
         strstr(assembly, "  mov x9, x0\n") != NULL) &&
        strstr(assembly, "  ret\n") != NULL;
    if (ok) {
        MinicMachineFunction invalid = {0};
        stage = "invalid-abi-rejected";
        if (indirect) {
            if (incoming_stack) incoming.arguments[0].stack_slots = 2U;
            else incoming.arguments[0].gp_registers = 2U;
        } else incoming.arguments[0].chunks += 1U;
        ok = !minic_aarch64_machine_select_core_subset(&invalid, &ctx);
        minic_machine_function_destroy(&invalid);
    }
done:
    if (!ok) fprintf(stderr,
        "A64 record ingress failed at %s bytes=%zu indirect=%d stack=%d\n",
        stage, bytes, (int)indirect, (int)incoming_stack);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* The old AArch64 ABI moves i128 through even xN/xN+1 pairs, binary128
 * through qN, and either wide class through a 16-byte incoming stack slot.
 * This is an ABI migration fixture, not a new Core language feature. */
static bool run_wide_abi_parameter_case(bool floating, bool stack) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId wide_type;
    MinicCoreBlockId entry;
    MinicCoreValueId argument;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator ret = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract abi = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64ArgumentPlan incoming = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[1] = {true};
    size_t value_offsets[1] = {SIZE_MAX};
    size_t wide_offsets[1] = {16U};
    FILE *out = NULL;
    char assembly[8192];
    size_t length;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    type.kind = floating ? MINIC_CORE_TYPE_FLOAT : MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 16U;
    if (floating) type.value.floating.format = MINIC_CORE_FLOAT_BINARY128;
    else type.value.integer.bit_width = 128U;
    if (!minic_core_type_register(&types, &type, &wide_type)) goto done;
    core.name = malloc(sizeof("a64_wide_abi"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "a64_wide_abi", sizeof("a64_wide_abi"));
    core.name_length = sizeof("a64_wide_abi") - 1U;
    if (!minic_core_function_define_signature(&core, &types, wide_type,
            &wide_type, 1U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = wide_type;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &argument)) goto done;
    ret.kind = MINIC_CORE_TERMINATOR_RETURN;
    ret.return_value = argument;
    if (!minic_core_function_set_terminator(&core, entry, &ret) ||
        !minic_core_function_attach_type_context(&core, &types) ||
        !minic_core_function_verify(&core)) goto done;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    abi.frame_size = 64U;
    abi.frame_alignment = 16U;
    abi.saved_entry_sp_offset = SIZE_MAX;
    plan.value_count = 1U;
    plan.value_offsets = value_offsets;
    plan.wide_offsets = wide_offsets;
    incoming.argument_count = 1U;
    incoming.arguments[0].kind = MINIC_LP64_ARGUMENT_WIDE;
    incoming.arguments[0].size_bytes = 16U;
    incoming.arguments[0].chunks = floating ? 1U : 2U;
    if (stack) {
        incoming.arguments[0].stack_first = 0U;
        incoming.arguments[0].stack_slots = 2U;
    } else if (floating) {
        incoming.arguments[0].fp_first = 0U;
        incoming.arguments[0].fp_registers = 1U;
    } else {
        incoming.arguments[0].gp_first = 0U;
        incoming.arguments[0].gp_registers = 2U;
    }
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &abi, .frame_plan = &plan,
        .incoming_plan = &incoming, .name = core.name
    };
    stage = "selection";
    if (!minic_aarch64_machine_select_core_subset(&machine, &ctx)) goto done;
    stage = "coverage-and-gnu";
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "serialize";
    out = tmpfile();
    if (out == NULL ||
        !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto done;
    length = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[length] = '\0';
    stage = "physical-abi-shape";
    if (floating)
        ok = machine.generated_features ==
                (MINIC_A64_MACHINE_FEATURE_BASE |
                 MINIC_A64_MACHINE_FEATURE_FP_SIMD) &&
            strstr(assembly, stack ? "  str q9, [sp, #16]\n" :
                "  str q0, [sp, #16]\n") != NULL &&
            strstr(assembly, "  ldr q0, [sp, #16]\n") != NULL;
    else
        ok = machine.generated_features == MINIC_A64_MACHINE_FEATURE_BASE &&
            strstr(assembly, stack ? "  str x9, [sp, #16]\n" :
                "  str x0, [sp, #16]\n") != NULL &&
            strstr(assembly, stack ? "  str x9, [sp, #24]\n" :
                "  str x1, [sp, #24]\n") != NULL &&
            strstr(assembly, "  ldr x0, [sp, #16]\n") != NULL &&
            strstr(assembly, "  ldr x1, [sp, #24]\n") != NULL;
    if (ok) {
        MinicMachineFunction invalid = {0};
        stage = "invalid-wide-abi-rejected";
        if (stack) incoming.arguments[0].stack_slots = 1U;
        else if (floating) incoming.arguments[0].fp_registers = 2U;
        else incoming.arguments[0].gp_registers = 1U;
        ok = !minic_aarch64_machine_select_core_subset(&invalid, &ctx);
        minic_machine_function_destroy(&invalid);
    }
done:
    if (!ok) {
        fprintf(stderr,
            "A64 wide ABI failed at %s float=%d stack=%d\n",
            stage, (int)floating, (int)stack);
        if (strcmp(stage, "physical-abi-shape") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

int main(void) {
    if (!run_case(false) || !run_case(true) ||
        !run_fp_case() || !run_wide_case() || !run_residual_case() ||
        !run_record_loop_case() ||
        !run_record_parameter_case(13U, false, false) ||
        !run_record_parameter_case(13U, false, true) ||
        !run_record_parameter_case(32U, true, false) ||
        !run_record_parameter_case(128U, true, true) ||
        !run_wide_abi_parameter_case(false, false) ||
        !run_wide_abi_parameter_case(false, true) ||
        !run_wide_abi_parameter_case(true, false) ||
        !run_wide_abi_parameter_case(true, true)) {
        fprintf(stderr, "A64_CORE_MIR_CFG=FAIL\n");
        return 1;
    }
    puts("A64_CORE_MIR_CFG=PASS prologue_isolated=1 backedge_to_core0=1 fail_closed=1 fp64=1 i128=1 i128_cast=1 dynamic_alloca=1 record_loop=1 record_parameter=4 wide_abi=4");
    return 0;
}
