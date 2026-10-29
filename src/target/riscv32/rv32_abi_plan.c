#include "target/riscv32/rv32_abi_plan.h"
#include "target/riscv32/rv32_internal.h"
#include "target/common/aggregate_shape.h"
#include "target/common/core_instruction_family.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Group ABI frontier reports in the same pristine upstream-source
 * census. A diagnostic is emitted only for a real unsupported shape and
 * only when the already existing acceptance script opts in. */
static bool rv32_abi_reject(const char *stage, size_t index,
                            MinicType type) {
    if (getenv("MINIC_RV32_FRAME_TRACE") != NULL)
        (void)fprintf(stderr,
            "RV32_ABI_REJECT stage=%s index=%zu kind=%d depth=%u rank=%d\n",
            stage, index, (int)type.base_kind,
            (unsigned)type.pointer_depth, (int)type.integer_rank);
    return false;
}

/* One psABI planner per Core function, used unchanged by physical frame
 * allocation, direct/indirect calls and callee parameter reception.
 * No target-unaware Core or frontend code knows about a0..a7 or ILP32. */
void rv32_function_abi_plan_destroy(Rv32FunctionAbiPlan *plan) {
    size_t i;
    if (plan == NULL) return;
    if (plan->calls != NULL)
        for (i = 0U; i < plan->call_count; ++i)
            free(plan->calls[i].arguments);
    free(plan->calls);
    free(plan->parameters);
    minic_core_call_index_handle_destroy(&plan->semantic_calls);
    (void)memset(plan, 0, sizeof(*plan));
}

/* Use the same target DataLayout as frame objects. The psABI passes
 * aggregates of at most 2*XLEN bits in one/two GP words; larger
 * aggregates are passed by reference to a full value snapshot. */
static bool rv32_plan_shape(const MinicC0Program *program,
                             const MinicTargetInfo *target,
                             const MinicCoreFunction *function,
                             MinicType type, Rv32AbiWordLocation *entry) {
    size_t size, align;
    if (minic_type_is_record(type)) {
        MinicAggregateStorageShape shape;
        bool layout_ok = minic_common_aggregate_storage_shape(
            program, target->data_layout, type, 4U, &shape);
        size = layout_ok ? shape.bytes : 0U;
        align = layout_ok ? shape.alignment : 0U;
        if (getenv("MINIC_RV32_FRAME_TRACE") != NULL)
            (void)fprintf(stderr,
                "RV32_RECORD_ABI_SHAPE id=%zu layout=%u bytes=%zu align=%zu\n",
                type.record_id, (unsigned)layout_ok,
                layout_ok ? size : 0U, layout_ok ? align : 0U);
        if (!layout_ok || size > UINT32_MAX)
            return false;
        entry->alignment = align;
        entry->record_size = size;
        entry->indirect_record = size > 8U;
        /* GNU empty aggregates occupy no psABI register or stack word. */
        entry->words = size > 8U ? 1U : (unsigned)shape.words;
        return true;
    }
    if (!rv32_scalar_storage_words(function, type, &entry->words) ||
        !minic_data_layout_type(target->data_layout, program, type, &size, &align) ||
        align == 0U)
        return false;
    entry->alignment = align;
    return true;
}

static bool rv32_plan_incoming(const MinicC0Program *program,
                              const MinicTargetInfo *target,
                              const MinicCoreFunction *function,
                              Rv32FunctionAbiPlan *plan) {
    MinicRiscv32AbiCursor cursor = {0U, 0U, false};
    size_t i;
    Rv32AbiWordLocation result_shape = {0};
    /* The ILP32 ABI planner is the sole return-type admission owner.
     * Scalar and record definitions share the same checked shape used
     * for incoming parameters and outgoing calls. Machine emission does
     * not walk the signature again. */
    if (!minic_type_is_void(function->return_type)) {
        if (!rv32_plan_shape(program, target, function,
                             function->return_type, &result_shape))
            return rv32_abi_reject("incoming_return", 0U, function->return_type);
        if (minic_type_is_record(function->return_type)) {
            plan->record_return_size = result_shape.record_size;
            plan->indirect_return = result_shape.indirect_record;
            /* Hidden sret pointer occupies a0 before all named parameters. */
            if (plan->indirect_return)
                cursor.register_words = 1U;
        }
    }
    if (function->parameter_count != 0U &&
        function->parameter_types == NULL) return false;
    if (function->parameter_count > SIZE_MAX / sizeof(*plan->parameters))
        return false;
    plan->parameter_count = function->parameter_count;
    if (plan->parameter_count != 0U) {
        plan->parameters = calloc(plan->parameter_count,
                                  sizeof(*plan->parameters));
        if (plan->parameters == NULL) return false;
    }
    for (i = 0U; i < plan->parameter_count; ++i) {
        Rv32AbiWordLocation *entry = &plan->parameters[i];
        if (!rv32_plan_shape(program, target, function,
                              function->parameter_types[i], entry))
            return rv32_abi_reject("incoming_type", i, function->parameter_types[i]);
        if (entry->words == 0U) continue;
        if (!minic_riscv32_abi_place(&cursor, entry->words, false,
                                      entry->alignment,
                                      &entry->location))
            return rv32_abi_reject("incoming_place", i, function->parameter_types[i]);
    }
    plan->named_gpr_words = cursor.register_words;
    plan->named_stack_words = cursor.stack_words;
    return true;
}

static bool rv32_plan_call(const MinicC0Program *program,
                           const MinicTargetInfo *target,
                           const MinicCoreFunction *function,
                           const MinicCoreInstruction *instruction,
                           const MinicCoreCallView *semantic_view,
                           Rv32CallAbiPlan *call) {
    MinicRiscv32AbiCursor cursor = {0U, 0U, false};
    MinicCoreCallView view;
    Rv32AbiWordLocation result_shape = {0};
    size_t i;
    if (!(semantic_view != NULL ? (view = *semantic_view, true) :
          minic_core_call_view_build(function, instruction, &view)) ||
        view.argument_count > SIZE_MAX / sizeof(*call->arguments))
        return false;
    if (view.return_form == MINIC_CORE_CALL_RETURN_OBJECT) {
        if (!rv32_plan_shape(program, target, function, view.return_type,
                             &result_shape))
            return false;
        call->record_return_size = result_shape.record_size;
        call->indirect_return = result_shape.indirect_record;
        if (call->indirect_return)
            cursor.register_words = 1U;
    }
    if (view.argument_count != 0U) {
        call->arguments = calloc(view.argument_count, sizeof(*call->arguments));
        if (call->arguments == NULL) return false;
    }
    for (i = 0U; i < view.argument_count; ++i) {
        Rv32AbiWordLocation *entry = &call->arguments[i];
        MinicType actual, type;
        if (!minic_core_call_view_argument_type(
                function, &view, i, &actual))
            return rv32_abi_reject("call_actual_type", i, minic_type_void());
        /* psABI placement of fixed parameters belongs to the *declared*
         * prototype, not the C expression's pre-conversion type. RV64,
         * x86 SysV and Direct LP64 already use this distinction. If an
         * explicit Core conversion is absent, do not silently marshal a
         * 1-word value as a 2-word formal (or vice versa). */
        type = i < view.parameter_count ? view.parameter_types[i] : actual;
        if (i < view.parameter_count && !minic_type_equal(type, actual))
            return rv32_abi_reject("call_fixed_type", i, type);
        /* Record varargs obey the same ILP32 ABI shape and cursor as scalar
         * varargs: <=2 XLEN words by value, larger via a Core snapshot
         * pointer. Even-register pairing depends on the actual 8-byte ABI
         * alignment, not on an aggregate's 8-byte size alone. */
        if (!rv32_plan_shape(program, target, function, type, entry))
            return rv32_abi_reject("call_arg_type", i, type);
        if (entry->words == 0U) continue;
        if (!minic_riscv32_abi_place(&cursor, entry->words,
                view.is_variadic && i >= view.parameter_count,
                entry->alignment,
                &entry->location))
            return rv32_abi_reject("call_place", i, type);
    }
    call->view = view;
    call->stack_words = cursor.stack_words;
    call->prepared = true;
    return true;
}

/* Sparse CALL ownership: Common owns the validated Core call index.
 * ILP32 keeps one target ABI plan per CALL, not per Core instruction. */
bool rv32_function_abi_plan_build(const MinicC0Program *program,
                                  const MinicTargetInfo *target,
                                  const MinicCoreFunction *function,
                                  const MinicCoreCallIndex *semantic_calls,
                                  Rv32FunctionAbiPlan *plan) {
    const MinicCoreCallIndex *calls;
    size_t ordinal, previous_id = SIZE_MAX;
    if (program == NULL || target == NULL ||
        target->data_layout == NULL || function == NULL || plan == NULL ||
        plan->calls != NULL || plan->parameters != NULL ||
        plan->call_count != 0U || plan->parameter_count != 0U ||
        plan->max_outgoing_stack_words != 0U ||
        plan->semantic_calls.index != NULL ||
        (function->instruction_count != 0U && function->instructions == NULL))
        return false;
    if (!rv32_plan_incoming(program, target, function, plan) ||
        !minic_core_call_index_handle_prepare(
            function, semantic_calls, &plan->semantic_calls))
        goto failure;
    calls = plan->semantic_calls.index;
    if (calls->count > SIZE_MAX / sizeof(*plan->calls))
        goto failure;
    plan->call_count = calls->count;
    if (calls->count != 0U) {
        plan->calls = calloc(calls->count, sizeof(*plan->calls));
        if (plan->calls == NULL) goto failure;
    }
    for (ordinal = 0U; ordinal < calls->count; ++ordinal) {
        const size_t id = calls->sites[ordinal].instruction_id;
        const MinicCoreInstruction *ins;
        Rv32CallAbiPlan *call = &plan->calls[ordinal];
        if (id >= function->instruction_count ||
            (previous_id != SIZE_MAX && id <= previous_id))
            goto failure;
        ins = &function->instructions[id];
        if (!minic_core_instruction_is_call(ins->kind))
            goto failure;
        call->instruction_id = id;
        if (!rv32_plan_call(program, target, function, ins,
                            &calls->sites[ordinal].view, call)) {
            if (getenv("MINIC_RV32_FRAME_TRACE") != NULL)
                (void)fprintf(stderr,
                    "RV32_ABI_REJECT stage=call_plan instruction=%zu kind=%d\n",
                    id, (int)ins->kind);
            goto failure;
        }
        if (call->stack_words > plan->max_outgoing_stack_words)
            plan->max_outgoing_stack_words = call->stack_words;
        previous_id = id;
    }
    return true;
failure:
    rv32_function_abi_plan_destroy(plan);
    return false;
}

/* Common is the ONLY sparse CALL lookup in both visitor and standalone
 * ILP32. Physical register/stack placement remains owned by this plan. */
const Rv32CallAbiPlan *rv32_function_abi_call(
    const Rv32FunctionAbiPlan *plan, MinicCoreInstructionId index) {
    const MinicCoreCallIndex *calls;
    size_t ordinal;
    if (plan == NULL || plan->semantic_calls.index == NULL ||
        (plan->call_count != 0U && plan->calls == NULL))
        return NULL;
    calls = plan->semantic_calls.index;
    if (calls->count != plan->call_count ||
        minic_core_call_index_find(calls, (size_t)index, &ordinal) == NULL ||
        ordinal >= plan->call_count ||
        plan->calls[ordinal].instruction_id != (size_t)index ||
        !plan->calls[ordinal].prepared)
        return NULL;
    return &plan->calls[ordinal];
}
