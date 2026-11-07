/* AArch64 historical source-ASM regression through the real function-level
 * Core -> typed MachineIR -> GNU serialization route. */
#ifndef MINIC_TEST_AARCH64_TYPED_SOURCE_ASM_PROBE_H
#define MINIC_TEST_AARCH64_TYPED_SOURCE_ASM_PROBE_H

#include "target/aarch64/machine_core_select.h"
#include "target/aarch64/machine_opcode.h"
#include "target/common/machine_core_coverage.h"
#include "target/common/machine_gnu_asm.h"
#include "target/data_layout.h"

#include <stdio.h>
#include <stdlib.h>

static bool minic_test_aarch64_typed_source_asm_emit(
    FILE *out, const MinicCoreFunction *original,
    const MinicCoreInstruction *original_instruction) {
    MinicCoreInstruction instruction;
    MinicCoreInstructionId instruction_id = 0U;
    MinicCoreBlock block = {0};
    MinicCoreFunction core;
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan frame_plan = {0};
    MinicDirectLP64BlockContext context = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineCoreCoveragePolicy coverage_policy = {
        .prove_selected_source_asm =
            minic_aarch64_machine_prove_selected_source_asm
    };
    bool reachable[1] = {false};
    size_t value_offsets[16] = {16U, 24U, 32U, 40U};
    bool admitted = false;
    const char *failed_step = "select";
    if (out == NULL || original == NULL || original_instruction == NULL ||
        original->value_count > 16U || original->type_context == NULL ||
        original->inline_asms == NULL || original->inline_asm_count != 1U)
        return false;
    instruction = *original_instruction;
    core = *original;
    core.name = "a64_typed_source_asm_test";
    core.name_length = sizeof("a64_typed_source_asm_test") - 1U;
    core.instructions = &instruction;
    core.instruction_count = 1U;
    core.instruction_capacity = 1U;
    block.instructions = &instruction_id;
    block.instruction_count = 1U;
    block.terminator.kind = MINIC_CORE_TERMINATOR_UNREACHABLE;
    block.has_terminator = true;
    core.blocks = &block;
    core.block_count = 1U;
    core.entry_block = 0U;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    module.type_context = original->type_context;
    frame.frame_alignment = 16U;
    frame.frame_size = 64U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    frame_plan.value_count = core.value_count;
    frame_plan.value_offsets = value_offsets;
    context = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &frame_plan,
        .name = core.name
    };
    if (!minic_aarch64_machine_select_core_subset(&machine, &context))
        goto done;
    failed_step = "reachable";
    if (!minic_core_function_mark_reachable_blocks(&core, reachable, 1U))
        goto done;
    failed_step = "coverage";
    if (!minic_machine_core_coverage_verify_with_policy(
            &core, reachable, &machine, &coverage_policy))
        goto done;
    failed_step = "policy";
    if (!minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy))
        goto done;
    failed_step = "preflight";
    if (!minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    failed_step = "emit";
    if (!minic_machine_gnu_asm_emit_body(out, &machine, &policy)) goto done;
    admitted = true;
done:
    if (!admitted && getenv("MINIC_TYPED_SOURCE_PROBE_DEBUG") != NULL)
        fprintf(stderr, "A64_TYPED_SOURCE_PROBE=REJECT step=%s template=%s\n",
                failed_step, original->inline_asms[0].template_text);
    minic_machine_function_destroy(&machine);
    return admitted;
}

#endif
