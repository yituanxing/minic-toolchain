/* A real Core CFG, with a backedge to Core block zero, must never branch to
 * the external function symbol (which would execute the prologue twice).
 * This is distinct from synthetic frame-only MachineIR instruction tests. */
#include "target/aarch64/machine_core_select.h"
#include "target/aarch64/machine_opcode.h"
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
    if (machine.block_count != 3U ||
        !machine.blocks[0].machine_auxiliary ||
        machine.blocks[1].machine_auxiliary ||
        machine.blocks[2].machine_auxiliary ||
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

int main(void) {
    if (!run_case(false) || !run_case(true)) {
        fprintf(stderr, "A64_CORE_MIR_CFG=FAIL\n");
        return 1;
    }
    puts("A64_CORE_MIR_CFG=PASS prologue_isolated=1 backedge_to_core0=1 fail_closed=1");
    return 0;
}
