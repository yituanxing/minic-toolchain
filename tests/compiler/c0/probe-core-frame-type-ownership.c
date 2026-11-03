#include "target/common/core_object_intervals.h"
#include "target/common/core_object_reuse.h"
#include "target/common/core_spill_layout.h"

#include <stdint.h>
#include <stdio.h>

#define REQUIRE(expr) do { if (!(expr)) return __LINE__; } while (0)

static bool integer_type(MinicCoreTypeContext *ctx, unsigned int bits,
                         MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = bits / 8U;
    type.alignment = type.size;
    type.value.integer.bit_width = bits;
    type.value.integer.is_signed = true;
    return minic_core_type_register(ctx, &type, id);
}

static int run(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId i32, i64;
    MinicCoreValue values[1] = {0};
    MinicCoreInstruction instruction = {0};
    MinicCoreInstructionId ids[1] = {0U};
    MinicCoreBlock block = {0};
    MinicCoreObject object = {0};
    MinicCoreFunction function = {0};
    MinicCoreObjectIntervalAnalysis analysis = {0};
    CoreScalarObjectInterval candidate = {0}, interval = {0};
    MinicCoreObjectReusePlan reuse = {0};
    size_t offsets[1] = {SIZE_MAX};
    size_t bytes = SIZE_MAX, failed_block = SIZE_MAX;

    minic_core_type_context_initialize(&types);
    REQUIRE(integer_type(&types, 32U, &i32));
    REQUIRE(integer_type(&types, 64U, &i64));

    /* SSA and frame-object storage have no source C descriptors. */
    values[0].core_type_id = i64;
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.core_type_id = i64;
    instruction.result = 0U;
    block.instructions = ids;
    block.instruction_count = 1U;
    block.has_terminator = true;
    block.terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    block.terminator.return_value = MINIC_CORE_VALUE_INVALID;
    block.terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    object.core_type_id = i32;
    object.element_count = 1U;
    function.type_context = &types;
    function.values = values;
    function.value_count = 1U;
    function.instructions = &instruction;
    function.instruction_count = 1U;
    function.blocks = &block;
    function.block_count = 1U;
    function.objects = &object;
    function.object_count = 1U;

    REQUIRE(minic_core_function_reused_spill_layout(
        &function, 4U, 0U, offsets, &bytes, &failed_block));
    REQUIRE(offsets[0] == 0U && bytes == 8U && failed_block == SIZE_MAX);
    offsets[0] = SIZE_MAX;
    bytes = SIZE_MAX;
    REQUIRE(minic_core_function_reused_spill_layout(
        &function, 8U, 0U, offsets, &bytes, NULL));
    REQUIRE(offsets[0] == 0U && bytes == 8U);

    /* Supplying a positive interval index isolates the Core scalar
     * classification from the independent escape/lifetime proof. */
    candidate.reusable = true;
    candidate.block_index = 0U;
    candidate.first_position = 0U;
    candidate.last_position = 0U;
    analysis.interval_index = &candidate;
    REQUIRE(minic_core_object_interval(
        &function, 0U, 8U, &analysis, &interval));
    REQUIRE(interval.reusable && !interval.can_share_across_blocks);
    object.core_type_id = MINIC_CORE_TYPE_INVALID;
    REQUIRE(minic_core_object_interval(
        &function, 0U, 8U, &analysis, &interval));
    REQUIRE(!interval.reusable);
    object.core_type_id = i32;
    REQUIRE(minic_core_object_reuse_plan_build(
        &function, 8U, true, &reuse));
    REQUIRE(reuse.prepared && reuse.object_count == 1U);
    minic_core_object_reuse_plan_destroy(&reuse);

    values[0].core_type_id = MINIC_CORE_TYPE_INVALID;
    offsets[0] = SIZE_MAX;
    REQUIRE(!minic_core_function_reused_spill_layout(
        &function, 4U, 0U, offsets, &bytes, &failed_block));
    REQUIRE(failed_block == 0U && offsets[0] == SIZE_MAX);
    values[0].core_type_id = i64;
    function.values = NULL;
    REQUIRE(!minic_core_function_reused_spill_layout(
        &function, 4U, 0U, offsets, &bytes, &failed_block));
    function.values = values;
    function.objects = NULL;
    REQUIRE(!minic_core_object_reuse_plan_build(
        &function, 8U, true, &reuse));
    function.objects = &object;
    function.type_context = NULL;
    REQUIRE(!minic_core_function_reused_spill_layout(
        &function, 4U, 0U, offsets, &bytes, &failed_block));
    REQUIRE(!minic_core_object_interval(
        &function, 0U, 8U, &analysis, &interval));
    REQUIRE(!minic_core_object_reuse_plan_build(
        &function, 8U, true, &reuse));
    REQUIRE(!reuse.prepared && reuse.intervals == NULL && reuse.slots == NULL);
    minic_core_type_context_destroy(&types);
    return 0;
}

int main(void) {
    int failed = run();
    if (failed != 0) {
        (void)fprintf(stderr, "core_frame_type_ownership failed at %d\n", failed);
        return 1;
    }
    (void)puts("CORE_FRAME_TYPE_OWNERSHIP=PASS Core-only object+interval+spill ILP32_LP64 C-free malformed missing-context");
    return 0;
}
