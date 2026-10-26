#ifndef MINIC_TARGET_RISCV64_FRAME_LAYOUT_H
#define MINIC_TARGET_RISCV64_FRAME_LAYOUT_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/riscv64/asm_plan.h"
#include "target/riscv64/abi.h"
#include "target/common/core_object_reuse.h"
#include "target/riscv64/record_snapshot.h"
#include "target/riscv64/function_abi_plan.h"
#include "target/riscv64/frame_requirements.h"
#include "target/common/core_object_storage.h"
#include "target/common/core_function_facts.h"
#include <stdbool.h>
#include <stddef.h>

/* Immutable frame assignments are target-owned. Core IDs remain semantic. */
typedef struct MinicRiscv64CoreFrame {
    size_t frame_size;
    size_t object_count;
    size_t value_count;
    size_t value_slot_count;
    size_t *object_offsets;
    MinicCoreObjectStoragePlan object_storage;
    bool borrows_object_storage;
    MinicCoreObjectReusePlan object_reuse;
    /* Borrowed from the Common Function Plan, never owned by RV64. */
    const MinicCoreCallIndex *semantic_calls;
    /* Standalone emitters without the visitor own this fallback CALL index;
     * production frames only borrow the visitor's Common CALL snapshot. */
    MinicCoreCallIndex standalone_calls;
    /* RV64-owned ABI location plan, prepared once from Common CALL views. */
    MinicRiscv64CallPlanCache outgoing_calls;
    size_t *value_offsets;
    MinicRiscv64AbiArgumentLocation *parameter_locations;
    size_t parameter_location_count;
    MinicRiscv64AbiCursor named_parameter_cursor;
    bool has_named_parameter_cursor;
    /* M177_CONSTANT_SWITCH_CFG_OWNER: suppress target emission of Core blocks
       that have no executable predecessor after semantic CFG pruning. */
    bool *reachable_blocks;
    size_t value_base_offset;
    size_t outgoing_argument_size;
    size_t dynamic_call_area_size;
    size_t return_address_offset;
    /* M167D_INDIRECT_RECORD_RETURN: psABI hidden result pointer is incoming
       state and must survive arbitrary calls before a Core RETURN. */
    size_t hidden_result_pointer_offset;
    size_t entry_sp_offset;
    size_t saved_frame_base_offset;
    size_t stack_alignment;
    size_t structured_asm_callee_saved_offset;
    MinicRiscv64AsmFunctionPlan asm_plan;
    size_t varargs_offset;
    size_t varargs_size;
    size_t integer_parameter_count;
    size_t variadic_fixed_stack_slots;
    bool saves_return_address;
    bool has_hidden_result_pointer;
    bool has_dynamic_stack_alignment;
    bool has_dynamic_stack_alloc;
    bool preserves_structured_asm_callee_saved;
    bool has_variadic_argument_address;
} MinicRiscv64CoreFrame;

/* Consume a prevalidated ABI plan. This transfers ownership of parameter
 * locations to the frame; the input plan becomes empty on success. */
bool minic_riscv64_core_frame_initialize_with_storage(
    const MinicC0Program *, const MinicCoreFunction *,
    MinicRiscv64CoreFrame *, MinicRiscv64FunctionAbiPlan *, bool, bool,
    const MinicCoreFunctionFacts *, const MinicCoreObjectStoragePlan *);
void minic_riscv64_core_frame_destroy(MinicRiscv64CoreFrame *);
bool minic_riscv64_core_frame_parameter_location(
    const MinicRiscv64CoreFrame *, size_t, MinicRiscv64AbiArgumentLocation *);
bool minic_riscv64_core_frame_object_offset(
    const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64CoreFrame *, MinicCoreObjectId, size_t *);
bool minic_riscv64_core_frame_value_offset(
    const MinicRiscv64CoreFrame *, MinicCoreValueId, size_t *);
bool minic_riscv64_core_frame_mark_reachable_blocks(
    const MinicCoreFunction *, MinicRiscv64CoreFrame *);
#endif
