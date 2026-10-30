#include "target/common/direct_machine_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Probe {
    unsigned int families, stores, terminators, rejects, fail_stage;
    bool saw_variadic_double;
    const char *stage;
    size_t failed_block, failed_instruction;
} Probe;
static Probe state;

bool minic_multi_direct_machine_instruction_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool allow_variadic_double_result, MinicDirectMachineInstructionShape *shape) {
    state.saw_variadic_double = allow_variadic_double_result;
    if (fn == NULL || ins == NULL || shape == NULL || state.fail_stage == 4U)
        return false;
    shape->result_offset = (size_t)ins->result * 8U;
    shape->family = ins->kind == MINIC_CORE_INSTRUCTION_COMPILER_BARRIER
        ? MINIC_DIRECT_MACHINE_COMPILER_BARRIER
        : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT
        ? MINIC_DIRECT_MACHINE_INTEGER
        : MINIC_DIRECT_MACHINE_ABI;
    return true;
}
bool minic_multi_direct_machine_reject(
    const MinicCoreFunction *fn, const char *arch, const char *name,
    const char *stage, size_t block_id, size_t instruction_id,
    unsigned int line) {
    (void)fn; (void)name; (void)line;
    if (strcmp(arch, "aarch64") != 0) abort();
    ++state.rejects;
    state.stage = stage;
    state.failed_block = block_id;
    state.failed_instruction = instruction_id;
    return false;
}
static bool family(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, MinicCoreInstructionId id,
    size_t block_id, const MinicDirectMachineInstructionShape *shape,
    bool *writeback) {
    (void)ctx; (void)block_id;
    ++state.families;
    *writeback = shape->family == MINIC_DIRECT_MACHINE_INTEGER;
    return state.fail_stage != 1U &&
        fprintf(out, "F%u.%u\n", (unsigned)id, (unsigned)shape->family) >= 0 &&
        ins != NULL;
}
static bool gp(FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, size_t offset, bool include_double) {
    (void)ctx; (void)ins;
    ++state.stores;
    return state.fail_stage != 2U &&
        fprintf(out, "G%zu.%u\n", offset, (unsigned)include_double) >= 0;
}
static bool term(FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreBlock *block, size_t block_id) {
    (void)ctx; (void)block;
    ++state.terminators;
    return state.fail_stage != 3U && fprintf(out, "T%zu\n", block_id) >= 0;
}
static int run(unsigned int failure) {
    MinicCoreInstruction ins[3] = {
        { .kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT, .result = 1U },
        { .kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,
          .result = MINIC_CORE_VALUE_INVALID },
        { .kind = MINIC_CORE_INSTRUCTION_CALL, .result = 2U }
    };
    MinicCoreInstructionId first[] = {0U, 1U}, second[] = {2U};
    MinicCoreBlock blocks[2] = {{0}, {0}};
    MinicCoreFunction fn = {0};
    MinicC0Program program = {0};
    MinicTargetInfo target = {0};
    MinicLP64FramePlan frame = {0};
    size_t value_offsets[] = {0U, 24U, 8U};
    MinicLP64ArgumentPlan incoming = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicDirectMachineDispatch dispatch = {
        "aarch64", "mini_arm", true, family, gp, term
    };
    FILE *out = tmpfile();
    char got[256] = {0};
    size_t n;
    bool ok;
    if (out == NULL) return 1;
    blocks[0].instructions = first;
    blocks[0].instruction_count = 2U;
    blocks[0].source_label_id = SIZE_MAX;
    blocks[0].has_terminator = true;
    blocks[0].terminator.kind = MINIC_CORE_TERMINATOR_BRANCH;
    blocks[0].terminator.branch_target = 1U;
    blocks[1].instructions = second;
    blocks[1].instruction_count = 1U;
    blocks[1].source_label_id = 7U;
    blocks[1].has_terminator = true;
    blocks[1].terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    fn.blocks = blocks;
    fn.block_count = 2U;
    fn.instructions = ins;
    fn.instruction_count = 3U;
    fn.value_count = 3U;
    frame.value_offsets = value_offsets;
    frame.value_count = 3U;
    if (failure == 5U)
        value_offsets[1] = SIZE_MAX;
    ctx.program = &program;
    ctx.target = &target;
    ctx.fn = &fn;
    ctx.frame_plan = &frame;
    ctx.incoming_plan = &incoming;
    ctx.name = "test";
    ctx.id = 4U;
    memset(&state, 0, sizeof(state));
    state.fail_stage = failure;
    ok = minic_multi_direct_machine_dispatch(out, &ctx, &dispatch);
    if (fseek(out, 0L, SEEK_SET) != 0) return 2;
    n = fread(got, 1U, sizeof(got) - 1U, out);
    if (fclose(out) != 0 || n == 0U) return 3;
    if ((ok != (failure == 0U)) || state.saw_variadic_double != true ||
        (state.rejects != (failure == 0U ? 0U : 1U)))
        return 4;
    if (failure == 0U) {
        if (state.families != 2U || state.stores != 1U ||
            state.terminators != 2U ||
            strstr(got, ".Lmini_arm_f4_b0:\n") != got ||
            strstr(got, ".Lmini_arm_f4_b1:\n.Luser_7:\n") == NULL ||
            strstr(got, "G24.0\n") == NULL)
            return 5;
    } else if (failure == 1U) {
        if (strcmp(state.stage, "machine_emit") != 0 ||
            state.failed_block != 0U || state.failed_instruction != 0U)
            return 6;
    } else if (failure == 2U) {
        if (strcmp(state.stage, "machine_emit") != 0 ||
            state.failed_block != 0U || state.failed_instruction != 0U)
            return 7;
    } else if (failure == 3U) {
        if (strcmp(state.stage, "terminator_emit") != 0 ||
            state.failed_block != 0U || state.failed_instruction != SIZE_MAX)
            return 8;
    } else if (failure == 4U) {
        if (strcmp(state.stage, "machine_emit") != 0 ||
            state.failed_block != 0U || state.failed_instruction != 0U)
            return 9;
    } else if (failure == 5U) {
        /* An unmapped source SSA value is a hard rejection, never a silent
         * fall-through to the previous value_id * 8 byte offset. */
        if (strcmp(state.stage, "ssa_frame_slot") != 0 ||
            state.failed_block != 0U || state.failed_instruction != 0U ||
            state.families != 0U || state.stores != 0U)
            return 10;
    }
    return 0;
}
int main(void) {
    unsigned int fail;
    for (fail = 0U; fail <= 5U; ++fail)
        if (run(fail) != 0) return (int)(fail + 10U);
    puts("MINIC_DIRECT_LP64_DISPATCH=PASS families=2 barriers=1 failure_paths=5 unmapped_ssa=reject");
    return 0;
}
