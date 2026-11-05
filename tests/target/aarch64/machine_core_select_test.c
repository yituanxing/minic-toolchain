/* A real Core CFG, with a backedge to Core block zero, must never branch to
 * the external function symbol (which would execute the prologue twice).
 * This is distinct from synthetic frame-only MachineIR instruction tests. */
#include "target/aarch64/machine_core_select.h"
#include "target/aarch64/machine_opcode.h"
#include "target/aarch64/abi.h"
#include "target/common/lp64_aggregate.h"
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

/* This is a fixture of the exact production AAPCS64 policy in codegen.c;
 * call locations are sealed by the Common planner, never invented by tests. */
static const MinicDirectLP64AbiPolicy a64_call_test_abi = {
    .name = "aapcs64",
    .gp_argument_registers = 8U,
    .fp_argument_registers = 8U,
    .stack_slot_bytes = 8U,
    .stack_alignment = 16U,
    .indirect_result_argument_registers = 0U,
    .record_registers = MINIC_DIRECT_LP64_RECORD_WHOLE_OR_STACK,
    .wide_registers = MINIC_DIRECT_LP64_WIDE_EVEN_PAIR_OR_STACK,
    .long_double_in_fp_register = true,
    .variadic_floating_in_fp_register = true,
    .variadic_named_double_supported = true,
    .variadic_cursor = MINIC_DIRECT_LP64_VARIADIC_ABI_STATE,
    .variadic_save_bytes = 224U,
    .variadic_save_alignment = 16U,
    .extra_variadic_cursor_bytes = 32U,
    .core_indirect_record = minic_aarch64_indirect_record_core,
    .core_indirect_return = minic_aarch64_indirect_record_core
};

static bool a64_call_test_record_type(MinicCoreTypeContext *types,
    size_t bytes, MinicCoreTypeId *record_id) {
    MinicCoreType type = {0};
    MinicCoreTypeId byte_id, array_id;
    MinicCoreRecordField field = {0};
    if (bytes == 0U) {
        type.kind = MINIC_CORE_TYPE_RECORD;
        type.has_layout = true;
        type.alignment = 1U;
        type.value.record.is_complete = true;
        return minic_core_type_register(types, &type, record_id);
    }
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 1U;
    type.value.integer.bit_width = 8U;
    if (!minic_core_type_register(types, &type, &byte_id)) return false;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_ARRAY;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = 1U;
    type.value.array.element = byte_id;
    type.value.array.count = bytes;
    type.value.array.is_complete = true;
    if (!minic_core_type_register(types, &type, &array_id)) return false;
    field.type_id = array_id;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = 1U;
    type.value.record.is_complete = true;
    type.value.record.fields = &field;
    type.value.record.field_count = 1U;
    return minic_core_type_register(types, &type, record_id);
}

/* Real Common-sealed call sites: integer-class record x0/x1, by-reference
 * record x0 and hidden return x8, all-stack record after x0..x7, and GNU
 * zero-sized aggregate.  Every case retains the original Direct ABI. */
static bool run_record_call_case(unsigned int mode) {
    const bool indirect_record = mode == 1U;
    const bool stack_record = mode == 2U;
    const bool empty_record = mode == 3U;
    const size_t bytes = indirect_record ? 32U : empty_record ? 0U : 13U;
    const size_t dummy_count = stack_record ? 8U : 0U;
    const size_t arg_count = dummy_count + 1U;
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_id, integer_id, record_id, result_id,
        signature_id, parameter_ids[9];
    MinicCoreCallArgument args[9] = {{0}};
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreBlockId entry;
    MinicCoreValueId value;
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64CallPlanCache calls = {0};
    MinicDirectLP64BlockContext ctx = {0};
    const bool reachable[1] = {true};
    size_t value_offsets[8] = {0};
    size_t object_offsets[2] = {160U, 208U};
    size_t begin, failed = SIZE_MAX, i;
    bool preflight = false, has_calls = false, ok = false;
    const char *stage = "setup";
    FILE *out = NULL;
    char assembly[12000];
    size_t length, call_effects = 0U;
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("a64_record_call"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "a64_record_call", sizeof("a64_record_call"));
    core.name_length = sizeof("a64_record_call") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    type.value.integer.bit_width = 64U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &type, &integer_id) ||
        !a64_call_test_record_type(&types, bytes, &record_id) ||
        !minic_core_function_define_signature(&core, &types, void_id,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    result_id = stack_record ? void_id : record_id;
    for (i = 0U; i < dummy_count; ++i) parameter_ids[i] = integer_id;
    parameter_ids[dummy_count] = record_id;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = result_id;
    type.value.function.parameters = parameter_ids;
    type.value.function.parameter_count = arg_count;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    if (!minic_core_type_register(&types, &type, &signature_id)) goto done;
    core.callees = calloc(1U, sizeof(*core.callees));
    core.objects = calloc(2U, sizeof(*core.objects));
    if (core.callees == NULL || core.objects == NULL) goto done;
    core.callee_count = core.callee_capacity = 1U;
    core.object_count = core.object_capacity = 2U;
    core.callees[0].name = malloc(sizeof("external_record_call"));
    if (core.callees[0].name == NULL) goto done;
    memcpy(core.callees[0].name, "external_record_call",
        sizeof("external_record_call"));
    core.callees[0].name_length = sizeof("external_record_call") - 1U;
    core.callees[0].core_function_type_id = signature_id;
    core.objects[0].core_type_id = record_id;
    core.objects[1].core_type_id = record_id;
    for (i = 0U; i < dummy_count; ++i) {
        ins = (MinicCoreInstruction){0};
        ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        ins.core_type_id = integer_id;
        ins.value.integer_value = (int64_t)(i + 1U);
        if (!minic_core_function_append_value_instruction(&core,
                entry, &ins, &value)) goto done;
        args[i].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        args[i].value.value_id = value;
        value_offsets[i] = i * 8U + 16U;
    }
    args[dummy_count].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
    args[dummy_count].value.object_id = 1U;
    if (!minic_core_function_append_call_arguments(&core, args,
            arg_count, &begin)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_CALL;
    ins.core_type_id = result_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.call.callee_id = 0U;
    ins.value.call.argument_begin = begin;
    ins.value.call.argument_count = arg_count;
    ins.value.call.result_object = stack_record ?
        MINIC_CORE_OBJECT_INVALID : 0U;
    if (!minic_core_function_append_effect_instruction(&core,
            entry, &ins)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    stage = "sealed-common-call-plan";
    if (!minic_lp64_plan_function_calls(&core, NULL,
            &a64_call_test_abi, "aarch64", core.name,
            &failed, &preflight, &has_calls, &calls) ||
        !has_calls || calls.call_count != 1U ||
        calls.sites[0]->argument_count != arg_count ||
        (empty_record ? calls.sites[0]->arguments[dummy_count].kind !=
             MINIC_LP64_ARGUMENT_EMPTY_RECORD :
         stack_record ? calls.sites[0]->arguments[dummy_count].stack_slots != 2U :
         indirect_record ? calls.sites[0]->arguments[dummy_count].kind !=
             MINIC_LP64_ARGUMENT_INDIRECT_RECORD :
             calls.sites[0]->arguments[dummy_count].gp_registers != 2U))
        goto done;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_size = 320U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    frame.has_calls = true;
    frame.save_offset = 304U;
    plan.value_count = dummy_count;
    plan.value_offsets = value_offsets;
    plan.object_count = 2U;
    plan.object_offsets = object_offsets;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .outgoing_calls = &calls, .name = core.name
    };
    stage = "typed-select-and-coverage";
    if (!minic_aarch64_machine_select_core_subset(&machine, &ctx) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "gnu-serialize";
    out = tmpfile();
    if (out == NULL ||
        !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto done;
    length = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[length] = '\0';
    for (i = 0U; i < machine.block_count; ++i) {
        const MinicMachineBlock *block = &machine.blocks[i];
        size_t j;
        for (j = 0U; j < block->instruction_count; ++j)
            if (block->instructions[j].has_core_origin &&
                block->instructions[j].core_instruction_id == dummy_count &&
                (block->instructions[j].effects &
                    MINIC_MACHINE_EFFECT_CALL) != 0U) ++call_effects;
    }
    stage = "record-abi-physical-invariants";
    ok = call_effects == 1U &&
        strstr(assembly, "  bl external_record_call\n") != NULL &&
        (empty_record ? strstr(assembly, "  ldrb ") == NULL &&
                        strstr(assembly, "  strb ") == NULL :
         indirect_record ? strstr(assembly, "  mov x0, x9\n") != NULL &&
                           strstr(assembly, "  add x8, ") != NULL :
         stack_record ? strstr(assembly, "  str x11, [sp, #0]\n") != NULL &&
                        strstr(assembly, "  str x11, [sp, #8]\n") != NULL :
                        strstr(assembly, "  mov x0, x11\n") != NULL &&
                        strstr(assembly, "  strb w9, ") != NULL);
    if (!ok) goto done;
    stage = "reject-corrupt-sealed-location";
    minic_machine_function_destroy(&machine);
    calls.sites[0]->arguments[dummy_count].kind = MINIC_LP64_ARGUMENT_GP;
    ok = !minic_aarch64_machine_select_core_subset(&machine, &ctx);
done:
    if (!ok) {
        fprintf(stderr,
            "A64 record CALL failed stage=%s mode=%u failed=%zu preflight=%d\n",
            stage, mode, failed, (int)preflight);
        if (strcmp(stage, "record-abi-physical-invariants") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_lp64_call_plan_cache_destroy(&calls);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* One actual caller(wide)->callee(wide)->caller result, planned by Common:
 * even-GP-pair i128, q-register binary128, and both bank-exhaustion spills. */
static bool run_wide_call_case(bool floating, bool stack) {
    const size_t dummy_count = stack ? 8U : 0U;
    const size_t arg_count = dummy_count + 1U;
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId wide_id, dummy_id, signature_id,
        parameters[9];
    MinicCoreCallArgument args[9] = {{0}};
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreBlockId entry;
    MinicCoreValueId wide_in, wide_out, dummy;
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64ArgumentPlan incoming = {0};
    MinicLP64CallPlanCache calls = {0};
    MinicDirectLP64BlockContext ctx = {0};
    const bool reachable[1] = {true};
    size_t value_offsets[10], wide_offsets[10];
    size_t begin, failed = SIZE_MAX, i;
    bool preflight = false, has_calls = false, ok = false;
    const char *stage = "setup";
    FILE *out = NULL;
    char assembly[20000];
    size_t length;
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    for (i = 0U; i < 10U; ++i)
        value_offsets[i] = wide_offsets[i] = SIZE_MAX;
    type.kind = floating ? MINIC_CORE_TYPE_FLOAT : MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 16U;
    if (floating) type.value.floating.format = MINIC_CORE_FLOAT_BINARY128;
    else type.value.integer.bit_width = 128U;
    if (!minic_core_type_register(&types, &type, &wide_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = floating ? MINIC_CORE_TYPE_FLOAT : MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    if (floating) type.value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    else type.value.integer.bit_width = 64U;
    if (!minic_core_type_register(&types, &type, &dummy_id)) goto done;
    core.name = malloc(sizeof("a64_wide_call"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "a64_wide_call", sizeof("a64_wide_call"));
    core.name_length = sizeof("a64_wide_call") - 1U;
    if (!minic_core_function_define_signature(&core, &types, wide_id,
            &wide_id, 1U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    for (i = 0U; i < dummy_count; ++i) parameters[i] = dummy_id;
    parameters[dummy_count] = wide_id;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = wide_id;
    type.value.function.parameters = parameters;
    type.value.function.parameter_count = arg_count;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    if (!minic_core_type_register(&types, &type, &signature_id)) goto done;
    core.callees = calloc(1U, sizeof(*core.callees));
    if (core.callees == NULL) goto done;
    core.callee_count = core.callee_capacity = 1U;
    core.callees[0].name = malloc(sizeof("external_wide_call"));
    if (core.callees[0].name == NULL) goto done;
    memcpy(core.callees[0].name, "external_wide_call",
        sizeof("external_wide_call"));
    core.callees[0].name_length = sizeof("external_wide_call") - 1U;
    core.callees[0].core_function_type_id = signature_id;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = wide_id;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&core, entry, &ins,
            &wide_in)) goto done;
    wide_offsets[wide_in] = 160U;
    for (i = 0U; i < dummy_count; ++i) {
        ins = (MinicCoreInstruction){0};
        ins.kind = floating ? MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT :
            MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        ins.core_type_id = dummy_id;
        if (floating) ins.value.floating_bits = UINT64_C(0x3ff0000000000000);
        else ins.value.integer_value = (int64_t)(i + 1U);
        if (!minic_core_function_append_value_instruction(&core,
                entry, &ins, &dummy)) goto done;
        value_offsets[dummy] = 16U + i * 8U;
        args[i].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        args[i].value.value_id = dummy;
    }
    args[dummy_count].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    args[dummy_count].value.value_id = wide_in;
    if (!minic_core_function_append_call_arguments(&core,
            args, arg_count, &begin)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_CALL;
    ins.core_type_id = wide_id;
    ins.value.call.callee_id = 0U;
    ins.value.call.argument_begin = begin;
    ins.value.call.argument_count = arg_count;
    ins.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_value_instruction(&core,
            entry, &ins, &wide_out)) goto done;
    wide_offsets[wide_out] = 192U;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = wide_out;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types) ||
        !minic_core_function_verify(&core)) goto done;
    stage = "sealed-common-wide-plan";
    if (!minic_lp64_plan_function_calls(&core, NULL,
            &a64_call_test_abi, "aarch64", core.name,
            &failed, &preflight, &has_calls, &calls) ||
        !has_calls || calls.call_count != 1U ||
        calls.sites[0]->argument_count != arg_count ||
        calls.sites[0]->arguments[dummy_count].kind !=
            MINIC_LP64_ARGUMENT_WIDE ||
        (stack ? calls.sites[0]->arguments[dummy_count].stack_slots != 2U :
            floating ?
                calls.sites[0]->arguments[dummy_count].fp_registers != 1U :
                calls.sites[0]->arguments[dummy_count].gp_registers != 2U) ||
        !minic_lp64_plan_incoming_parameters(&core,
            &a64_call_test_abi, &incoming)) goto done;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_size = 320U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    frame.has_calls = true;
    frame.save_offset = 304U;
    plan.value_count = core.value_count;
    plan.value_offsets = value_offsets;
    plan.wide_offsets = wide_offsets;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .incoming_plan = &incoming,
        .outgoing_calls = &calls, .name = core.name
    };
    stage = "typed-select-and-coverage";
    if (!minic_aarch64_machine_select_core_subset(&machine, &ctx) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "gnu-serialize";
    out = tmpfile();
    if (out == NULL ||
        !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto done;
    length = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[length] = '\0';
    stage = "wide-call-physical-invariants";
    ok = strstr(assembly, "  bl external_wide_call\n") != NULL &&
        (floating ?
            strstr(assembly, "  str q0, [sp, #192]\n") != NULL &&
            (stack ? strstr(assembly, "  str q9, [sp, #0]\n") != NULL :
                strstr(assembly, "  ldr q0, [sp, #160]\n") != NULL) :
            strstr(assembly, "  str x0, [sp, #192]\n") != NULL &&
            strstr(assembly, "  str x1, [sp, #200]\n") != NULL &&
            (stack ? strstr(assembly, "  str x9, [sp, #0]\n") != NULL &&
                     strstr(assembly, "  str x9, [sp, #8]\n") != NULL :
                strstr(assembly, "  ldr x0, [sp, #160]\n") != NULL &&
                strstr(assembly, "  ldr x1, [sp, #168]\n") != NULL));
    if (!ok) goto done;
    stage = "reject-corrupt-wide-location";
    minic_machine_function_destroy(&machine);
    calls.sites[0]->arguments[dummy_count].gp_registers = 1U;
    calls.sites[0]->arguments[dummy_count].fp_registers = 1U;
    ok = !minic_aarch64_machine_select_core_subset(&machine, &ctx);
done:
    if (!ok) {
        fprintf(stderr,
            "A64 wide CALL failed stage=%s floating=%d stack=%d failed=%zu preflight=%d\n",
            stage, (int)floating, (int)stack, failed, (int)preflight);
        if (strcmp(stage, "wide-call-physical-invariants") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_lp64_call_plan_cache_destroy(&calls);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Indirect CALL has exactly the same sealed AAPCS64 aggregate transport as
 * direct CALL, but the address itself is reloaded into IP0 only after x0/x8
 * have been prepared.  This catches accidental argument/callee clobbering. */
static bool run_indirect_record_call_case(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_id, record_id, signature_id, pointer_id;
    MinicCoreCallArgument arg = {0};
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreBlockId entry;
    MinicCoreValueId callee;
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64ArgumentPlan incoming = {0};
    MinicLP64CallPlanCache calls = {0};
    MinicDirectLP64BlockContext ctx = {0};
    const bool reachable[1] = {true};
    size_t value_offsets[1] = {16U};
    size_t object_offsets[2] = {160U, 208U};
    size_t begin, failed = SIZE_MAX;
    bool preflight = false, has_calls = false, ok = false;
    const char *stage = "setup";
    FILE *out = NULL;
    char assembly[10000];
    size_t length;
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("a64_indirect_record_call"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "a64_indirect_record_call",
        sizeof("a64_indirect_record_call"));
    core.name_length = sizeof("a64_indirect_record_call") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_id) ||
        !a64_call_test_record_type(&types, 32U, &record_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = record_id;
    type.value.function.parameters = &record_id;
    type.value.function.parameter_count = 1U;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    if (!minic_core_type_register(&types, &type, &signature_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    type.value.pointer.pointee = signature_id;
    if (!minic_core_type_register(&types, &type, &pointer_id) ||
        !minic_core_function_define_signature(&core, &types, void_id,
            &pointer_id, 1U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    core.call_signatures = calloc(1U, sizeof(*core.call_signatures));
    core.objects = calloc(2U, sizeof(*core.objects));
    if (core.call_signatures == NULL || core.objects == NULL) goto done;
    core.call_signature_count = core.call_signature_capacity = 1U;
    core.call_signatures[0].core_function_type_id = signature_id;
    core.object_count = core.object_capacity = 2U;
    core.objects[0].core_type_id = record_id;
    core.objects[1].core_type_id = record_id;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = pointer_id;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&core,
            entry, &ins, &callee)) goto done;
    arg.kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
    arg.value.object_id = 1U;
    if (!minic_core_function_append_call_arguments(&core,
            &arg, 1U, &begin)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    ins.core_type_id = record_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.indirect_call.callee = callee;
    ins.value.indirect_call.signature_id = 0U;
    ins.value.indirect_call.argument_begin = begin;
    ins.value.indirect_call.argument_count = 1U;
    ins.value.indirect_call.result_object = 0U;
    if (!minic_core_function_append_effect_instruction(&core,
            entry, &ins)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types) ||
        !minic_core_function_verify(&core)) goto done;
    stage = "sealed-indirect-common-plan";
    if (!minic_lp64_plan_function_calls(&core, NULL,
            &a64_call_test_abi, "aarch64", core.name,
            &failed, &preflight, &has_calls, &calls) ||
        !has_calls || calls.call_count != 1U ||
        calls.sites[0]->arguments[0].kind !=
            MINIC_LP64_ARGUMENT_INDIRECT_RECORD ||
        !minic_lp64_plan_incoming_parameters(&core,
            &a64_call_test_abi, &incoming)) goto done;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_size = 320U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    frame.has_calls = true;
    frame.save_offset = 304U;
    plan.value_count = 1U;
    plan.value_offsets = value_offsets;
    plan.object_count = 2U;
    plan.object_offsets = object_offsets;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .incoming_plan = &incoming,
        .outgoing_calls = &calls, .name = core.name
    };
    stage = "typed-select-and-coverage";
    if (!minic_aarch64_machine_select_core_subset(&machine, &ctx) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "gnu-serialize";
    out = tmpfile();
    if (out == NULL ||
        !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto done;
    length = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[length] = '\0';
    stage = "indirect-physical-invariants";
    ok = strstr(assembly, "  mov x0, x9\n") != NULL &&
        strstr(assembly, "  add x8, ") != NULL &&
        strstr(assembly, "  ldr x16, [sp, #16]\n") != NULL &&
        strstr(assembly, "  blr x16\n") != NULL &&
        strstr(assembly, "  bl external_record_call\n") == NULL;
    if (!ok) goto done;
    stage = "reject-bad-indirect-abi";
    minic_machine_function_destroy(&machine);
    calls.sites[0]->arguments[0].kind = MINIC_LP64_ARGUMENT_RECORD;
    ok = !minic_aarch64_machine_select_core_subset(&machine, &ctx);
done:
    if (!ok) {
        fprintf(stderr,
            "A64 INDIRECT record CALL failed stage=%s failed=%zu preflight=%d\n",
            stage, failed, (int)preflight);
        if (strcmp(stage, "indirect-physical-invariants") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_lp64_call_plan_cache_destroy(&calls);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* AAPCS64 homogeneous two-double aggregate results come back through
 * d0/d1, never through GP chunks or the hidden x8 return address. */
static bool run_hfa_record_call_case(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicCoreType type = {0};
    MinicCoreRecordField fields[2] = {{0}};
    MinicCoreTypeId void_id, double_id, record_id, signature_id;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreBlockId entry;
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64CallPlanCache calls = {0};
    MinicDirectLP64BlockContext ctx = {0};
    const bool reachable[1] = {true};
    size_t object_offset = 160U, failed = SIZE_MAX;
    bool preflight = false, has_calls = false, ok = false;
    const char *stage = "setup";
    FILE *out = NULL;
    char assembly[8000];
    size_t length;
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("a64_hfa_call"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "a64_hfa_call", sizeof("a64_hfa_call"));
    core.name_length = sizeof("a64_hfa_call") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    type.value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    if (!minic_core_type_register(&types, &type, &double_id)) goto done;
    fields[0].type_id = fields[1].type_id = double_id;
    fields[1].byte_offset = 8U;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 16U;
    type.alignment = 8U;
    type.value.record.is_complete = true;
    type.value.record.fields = fields;
    type.value.record.field_count = 2U;
    if (!minic_core_type_register(&types, &type, &record_id) ||
        !minic_core_function_define_signature(&core, &types, void_id,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = record_id;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    if (!minic_core_type_register(&types, &type, &signature_id)) goto done;
    core.callees = calloc(1U, sizeof(*core.callees));
    core.objects = calloc(1U, sizeof(*core.objects));
    if (core.callees == NULL || core.objects == NULL) goto done;
    core.callee_count = core.callee_capacity = 1U;
    core.object_count = core.object_capacity = 1U;
    core.objects[0].core_type_id = record_id;
    core.callees[0].name = malloc(sizeof("external_hfa_call"));
    if (core.callees[0].name == NULL) goto done;
    memcpy(core.callees[0].name, "external_hfa_call",
        sizeof("external_hfa_call"));
    core.callees[0].name_length = sizeof("external_hfa_call") - 1U;
    core.callees[0].core_function_type_id = signature_id;
    ins.kind = MINIC_CORE_INSTRUCTION_CALL;
    ins.core_type_id = record_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.call.callee_id = 0U;
    ins.value.call.result_object = 0U;
    if (!minic_core_function_append_effect_instruction(&core,
            entry, &ins)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    stage = "sealed-hfa-call-plan";
    if (!minic_lp64_plan_function_calls(&core, NULL,
            &a64_call_test_abi, "aarch64", core.name,
            &failed, &preflight, &has_calls, &calls) ||
        !has_calls || calls.call_count != 1U ||
        calls.sites[0]->argument_count != 0U) goto done;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_size = 320U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    frame.has_calls = true;
    frame.save_offset = 304U;
    plan.object_count = 1U;
    plan.object_offsets = &object_offset;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .outgoing_calls = &calls, .name = core.name
    };
    stage = "typed-select-and-coverage";
    if (!minic_aarch64_machine_select_core_subset(&machine, &ctx) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "gnu-serialize";
    out = tmpfile();
    if (out == NULL ||
        !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto done;
    length = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[length] = '\0';
    stage = "hfa-result-invariants";
    ok = strstr(assembly, "  bl external_hfa_call\n") != NULL &&
        strstr(assembly, "  str d0, [sp, #160]\n") != NULL &&
        strstr(assembly, "  str d1, [sp, #168]\n") != NULL &&
        strstr(assembly, "  add x8, ") == NULL;
    if (!ok) goto done;
    stage = "reject-missing-hfa-result-object";
    minic_machine_function_destroy(&machine);
    core.instructions[0].value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    ok = !minic_aarch64_machine_select_core_subset(&machine, &ctx);
done:
    if (!ok) {
        fprintf(stderr,
            "A64 HFA CALL failed stage=%s failed=%zu preflight=%d\n",
            stage, failed, (int)preflight);
        if (strcmp(stage, "hfa-result-invariants") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_lp64_call_plan_cache_destroy(&calls);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Old residual_emit's exact ELF policy: undefined weak MUST use GOT even in
 * non-PIC; PIC hidden/local goes direct, PIC preemptible stays via GOT. */
static bool run_symbol_policy_case(unsigned int mode) {
    const bool position_independent = mode != 0U;
    const bool nonpreemptible = mode == 1U;
    const bool got_expected = !nonpreemptible;
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_id, integer_id, pointer_id;
    MinicCoreBlockId entry;
    MinicCoreValueId address;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[1] = {true};
    size_t value_offset = 16U;
    const char *symbol = "a64_symbol_policy_extern";
    bool ok = false;
    const char *stage = "setup";
    FILE *out = NULL;
    char assembly[8192];
    size_t length;
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("a64_symbol_policy"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "a64_symbol_policy", sizeof("a64_symbol_policy"));
    core.name_length = sizeof("a64_symbol_policy") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    if (!minic_core_type_register(&types, &type, &integer_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    type.value.pointer.pointee = integer_id;
    if (!minic_core_type_register(&types, &type, &pointer_id) ||
        !minic_core_function_define_signature(&core, &types, void_id,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    core.globals = calloc(1U, sizeof(*core.globals));
    module.globals = calloc(1U, sizeof(*module.globals));
    if (core.globals == NULL || module.globals == NULL) goto done;
    core.global_count = core.global_capacity = 1U;
    core.globals[0].name = malloc(strlen(symbol) + 1U);
    if (core.globals[0].name == NULL) goto done;
    memcpy(core.globals[0].name, symbol, strlen(symbol) + 1U);
    core.globals[0].name_length = strlen(symbol);
    core.globals[0].core_type_id = integer_id;
    module.globals[0].name = core.globals[0].name;
    module.globals[0].definition_kind =
        MINIC_CORE_MODULE_DEFINITION_DECLARATION;
    module.globals[0].visibility = nonpreemptible ?
        MINIC_CORE_MODULE_VISIBILITY_HIDDEN :
        MINIC_CORE_MODULE_VISIBILITY_DEFAULT;
    module.globals[0].is_weak = mode == 0U;
    module.globals[0].alias_target_index = SIZE_MAX;
    module.globals[0].owner_function_index = SIZE_MAX;
    module.global_count = 1U;
    module.type_context = &types;
    module.finalized = true;
    ins.kind = MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS;
    ins.core_type_id = pointer_id;
    ins.value.global_id = 0U;
    if (!minic_core_function_append_value_instruction(&core,
            entry, &ins, &address)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types) ||
        !minic_core_function_verify(&core)) goto done;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    target.position_independent = position_independent;
    frame.frame_size = 32U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    plan.value_count = 1U;
    plan.value_offsets = &value_offset;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan, .name = core.name
    };
    stage = "typed-select-and-coverage";
    if (!minic_aarch64_machine_select_core_subset(&machine, &ctx) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "gnu-serialize";
    out = tmpfile();
    if (out == NULL ||
        !minic_machine_gnu_asm_emit_body(out, &machine, &policy) ||
        fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0) goto done;
    length = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[length] = '\0';
    stage = "elf-reference-policy";
    ok = got_expected ?
        strstr(assembly,
            "  adrp x9, :got:a64_symbol_policy_extern\n") != NULL &&
        strstr(assembly,
            "  ldr x9, [x9, :got_lo12:a64_symbol_policy_extern]\n") != NULL :
        strstr(assembly,
            "  adrp x9, a64_symbol_policy_extern\n") != NULL &&
        strstr(assembly,
            "  add x9, x9, :lo12:a64_symbol_policy_extern\n") != NULL;
done:
    if (!ok) {
        fprintf(stderr, "A64 ELF symbol policy failed stage=%s mode=%u\n",
            stage, mode);
        if (strcmp(stage, "elf-reference-policy") == 0)
            fprintf(stderr, "%s\n", assembly);
    }
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    free(module.globals);
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
        !run_wide_abi_parameter_case(true, true) ||
        !run_record_call_case(0U) ||
        !run_record_call_case(1U) ||
        !run_record_call_case(2U) ||
        !run_record_call_case(3U) ||
        !run_wide_call_case(false, false) ||
        !run_wide_call_case(false, true) ||
        !run_wide_call_case(true, false) ||
        !run_wide_call_case(true, true) ||
        !run_indirect_record_call_case() ||
        !run_hfa_record_call_case() ||
        !run_symbol_policy_case(0U) ||
        !run_symbol_policy_case(1U) ||
        !run_symbol_policy_case(2U)) {
        fprintf(stderr, "A64_CORE_MIR_CFG=FAIL\n");
        return 1;
    }
    puts("A64_CORE_MIR_CFG=PASS prologue_isolated=1 backedge_to_core0=1 fail_closed=1 fp64=1 i128=1 i128_cast=1 dynamic_alloca=1 record_loop=1 record_parameter=4 wide_abi=4 record_call=4 wide_call=4 indirect_record_call=1 hfa_call=1 symbol_policy=3");
    return 0;
}
