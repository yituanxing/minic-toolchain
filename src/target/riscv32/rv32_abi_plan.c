#include "target/riscv32/rv32_abi_plan.h"
#include "target/riscv32/rv32_internal.h"
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
        bool layout_ok = minic_data_layout_type(
            target->data_layout, program, type, &size, &align);
        if (getenv("MINIC_RV32_FRAME_TRACE") != NULL)
            (void)fprintf(stderr,
                "RV32_RECORD_ABI_SHAPE id=%zu layout=%u bytes=%zu align=%zu\n",
                type.record_id, (unsigned)layout_ok,
                layout_ok ? size : 0U, layout_ok ? align : 0U);
        if (!layout_ok || size > UINT32_MAX)
            return false;
        (void)align;
        entry->record_size = size;
        entry->indirect_record = size > 8U;
        /* GNU empty aggregates occupy no psABI register or stack word. */
        entry->words = size > 8U ? 1U : (unsigned)((size + 3U) / 4U);
        return true;
    }
    return rv32_scalar_storage_words(function, type, &entry->words);
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
        MinicType type;
        if (!minic_core_call_view_argument_type(
                function, &view, i, &type))
            return rv32_abi_reject("call_actual_type", i, minic_type_void());
        /* Record varargs obey the very same ILP32 ABI shape and cursor as
         * scalar varargs: <=2 XLEN words by value, larger via the caller's
         * Core snapshot pointer. The variadic flag below makes the cursor
         * apply psABI even-register alignment for two-word arguments. */
        if (!rv32_plan_shape(program, target, function, type, entry))
            return rv32_abi_reject("call_arg_type", i, type);
        if (entry->words == 0U) continue;
        if (!minic_riscv32_abi_place(&cursor, entry->words,
                view.is_variadic && i >= view.parameter_count,
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
    size_t i, dense = 0U, actual_calls = 0U;
    if (program == NULL || target == NULL ||
        target->data_layout == NULL || function == NULL || plan == NULL ||
        plan->calls != NULL || plan->parameters != NULL ||
        plan->call_count != 0U || plan->parameter_count != 0U ||
        plan->max_outgoing_stack_words != 0U ||
        (function->instruction_count != 0U && function->instructions == NULL))
        return false;
    if (!rv32_plan_incoming(program, target, function, plan)) goto failure;
    if (semantic_calls != NULL) {
        if (semantic_calls->count > function->instruction_count ||
            (semantic_calls->count != 0U && semantic_calls->sites == NULL))
            goto failure;
        actual_calls = semantic_calls->count;
    } else {
        for (i = 0U; i < function->instruction_count; ++i)
            if (minic_core_instruction_is_call(function->instructions[i].kind))
                ++actual_calls;
    }
    if (actual_calls > SIZE_MAX / sizeof(*plan->calls) ||
        (semantic_calls != NULL &&
         (semantic_calls->count != actual_calls ||
          (actual_calls != 0U && semantic_calls->sites == NULL))))
        goto failure;
    plan->call_count = actual_calls;
    if (actual_calls != 0U) {
        plan->calls = calloc(actual_calls, sizeof(*plan->calls));
        if (plan->calls == NULL) goto failure;
    }
    /* When Common prepared CALL sites exist, visit only those sites.
     * The standalone compatibility API keeps the original checked scan. */
    i = 0U;
    while (dense < actual_calls) {
        const MinicCoreInstruction *ins;
        Rv32CallAbiPlan *call;
        const MinicCoreCallView *view = NULL;
        size_t instruction_id;
        if (semantic_calls != NULL) {
            instruction_id = semantic_calls->sites[dense].instruction_id;
            view = &semantic_calls->sites[dense].view;
        } else {
            while (i < function->instruction_count &&
                   !minic_core_instruction_is_call(function->instructions[i].kind))
                ++i;
            instruction_id = i;
        }
        if (instruction_id >= function->instruction_count ||
            (dense != 0U && instruction_id <=
                plan->calls[dense - 1U].instruction_id))
            goto failure;
        ins = &function->instructions[instruction_id];
        if (!minic_core_instruction_is_call(ins->kind)) goto failure;
        call = &plan->calls[dense];
        call->instruction_id = instruction_id;
        if (!rv32_plan_call(program, target, function, ins, view, call)) {
            if (getenv("MINIC_RV32_FRAME_TRACE") != NULL)
                (void)fprintf(stderr,
                    "RV32_ABI_REJECT stage=call_plan instruction=%zu kind=%d\n",
                    instruction_id, (int)ins->kind);
            goto failure;
        }
        if (call->stack_words > plan->max_outgoing_stack_words)
            plan->max_outgoing_stack_words = call->stack_words;
        ++dense;
        if (semantic_calls == NULL) i = instruction_id + 1U;
    }
    if (dense != actual_calls) goto failure;
    plan->semantic_calls = semantic_calls;
    return true;
failure:
    rv32_function_abi_plan_destroy(plan);
    return false;
}

/* Sorted instruction IDs give O(log CALL sites) lookup, without a costly
 * machine-plan slot for every non-CALL Core instruction. */
const Rv32CallAbiPlan *rv32_function_abi_call(
    const Rv32FunctionAbiPlan *plan, MinicCoreInstructionId index) {
    size_t low = 0U, high;
    if (plan == NULL || (plan->call_count != 0U && plan->calls == NULL))
        return NULL;
    if (plan->semantic_calls != NULL) {
        size_t ordinal;
        if (plan->semantic_calls->count != plan->call_count ||
            minic_core_call_index_find(plan->semantic_calls,
                                       (size_t)index, &ordinal) == NULL ||
            ordinal >= plan->call_count ||
            plan->calls[ordinal].instruction_id != (size_t)index ||
            !plan->calls[ordinal].prepared)
            return NULL;
        return &plan->calls[ordinal];
    }
    /* Standalone ILP32 callers lacking a visitor retain sparse lookup. */
    high = plan->call_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        if (plan->calls[mid].instruction_id < (size_t)index)
            low = mid + 1U;
        else
            high = mid;
    }
    return low < plan->call_count &&
           plan->calls[low].instruction_id == (size_t)index &&
           plan->calls[low].prepared ? &plan->calls[low] : NULL;
}
