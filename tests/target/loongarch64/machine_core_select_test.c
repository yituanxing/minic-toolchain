/* A Core entry need not be block zero. The prologue may only reach the
 * declared entry, and a Core CFG backedge must never re-enter the prologue. */
#include "target/loongarch64/machine_select.h"
#include "target/loongarch64/machine_opcode.h"
#include "target/common/machine_gnu_asm.h"
#include "target/common/machine_core_coverage.h"
#include "target/data_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool test_nonzero_core_entry(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_type;
    MinicCoreBlockId before, entry;
    MinicCoreTerminator term = {0};
    const bool reachable[2] = {true, true};
    FILE *output = NULL;
    char text[2048];
    size_t n;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("loong_entry_nonzero"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "loong_entry_nonzero", sizeof("loong_entry_nonzero"));
    core.name_length = sizeof("loong_entry_nonzero") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_type)) goto done;
    stage = "signature";
    if (!minic_core_function_define_signature(&core, &types,
            void_type, NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false))
        goto done;
    stage = "blocks";
    if (!minic_core_function_add_block(&core, &before) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_BRANCH;
    term.branch_target = entry;
    stage = "terminators";
    if (!minic_core_function_set_terminator(&core, before, &term)) goto done;
    term.branch_target = before;
    /* Production Core verifier currently mandates entry_block == 0. First
     * seal a verified function, then deliberately vary this single field to
     * exercise the target selector's declared-entry mapping. The variant is
     * a target-only synthetic probe, not a CoreVerifier-admitted function. */
    if (!minic_core_function_set_terminator(&core, entry, &term)) goto done;
    stage = "attach";
    if (!minic_core_function_attach_type_context(&core, &types)) goto done;
    core.entry_block = entry;
    stage = "selector";
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .name = core.name
    };
    if (!minic_loongarch64_select_machine(&machine, &ctx) ||
        machine.block_count != 3U ||
        !machine.blocks[0].machine_auxiliary ||
        machine.blocks[1].machine_auxiliary ||
        machine.blocks[2].machine_auxiliary ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_loongarch64_machine_gnu_asm_policy_initialize(
            &target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "serialize";
    output = tmpfile();
    if (output == NULL ||
        !minic_machine_gnu_asm_emit_body(output, &machine, &policy) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0) goto done;
    n = fread(text, 1U, sizeof(text) - 1U, output);
    text[n] = '\0';
    stage = "labels";
    ok = strstr(text, "loong_entry_nonzero:\n") == NULL &&
         strstr(text, "  b .Lminic_mir_loong_entry_nonzero_2\n") != NULL &&
         strstr(text, ".Lminic_mir_loong_entry_nonzero_1:\n") != NULL &&
         strstr(text, ".Lminic_mir_loong_entry_nonzero_2:\n") != NULL &&
         strstr(text, "  b loong_entry_nonzero\n") == NULL;
done:
    if (!ok) fprintf(stderr, "LOONG_CORE_MIR_NONZERO_ENTRY=FAIL stage=%s\n",
                     stage);
    if (output != NULL) fclose(output);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

int main(void) {
    if (!test_nonzero_core_entry()) return 1;
    puts("LOONG_CORE_MIR_NONZERO_ENTRY=PASS prologue_targets_declared_entry=1");
    return 0;
}
