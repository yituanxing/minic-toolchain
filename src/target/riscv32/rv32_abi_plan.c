#include "target/riscv32/rv32_abi_plan.h"
#include "target/riscv32/rv32_internal.h"
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
        if (!minic_data_layout_type(target->data_layout, program, type,
                                    &size, &align) ||
            size == 0U || size > UINT32_MAX)
            return false;
        (void)align;
        entry->record_size = size;
        entry->indirect_record = size > 8U;
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
                           Rv32CallAbiPlan *call) {
    MinicRiscv32AbiCursor cursor = {0U, 0U, false};
    MinicCoreCallView view;
    size_t i;
    if (!minic_core_call_view_build(function, instruction, &view) ||
        (instruction->kind == MINIC_CORE_INSTRUCTION_CALL &&
         instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID) ||
        view.argument_count > SIZE_MAX / sizeof(*call->arguments))
        return false;
    if (view.argument_count != 0U) {
        call->arguments = calloc(view.argument_count, sizeof(*call->arguments));
        if (call->arguments == NULL) return false;
    }
    for (i = 0U; i < view.argument_count; ++i) {
        Rv32AbiWordLocation *entry = &call->arguments[i];
        MinicType type;
        if (!minic_core_call_argument_actual_type(
                function, &view.arguments[i], &type))
            return rv32_abi_reject("call_actual_type", i, minic_type_void());
        if (minic_type_is_record(type) &&
            (view.is_variadic && i >= view.parameter_count))
            return rv32_abi_reject("variadic_record_unqualified", i, type);
        if (!rv32_plan_shape(program, target, function, type, entry))
            return rv32_abi_reject("call_arg_type", i, type);
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

bool rv32_function_abi_plan_build(const MinicC0Program *program,
                                  const MinicTargetInfo *target,
                                  const MinicCoreFunction *function,
                                  Rv32FunctionAbiPlan *plan) {
    size_t i;
    if (program == NULL || target == NULL ||
        target->data_layout == NULL || function == NULL || plan == NULL ||
        plan->calls != NULL || plan->parameters != NULL ||
        plan->call_count != 0U || plan->parameter_count != 0U ||
        plan->max_outgoing_stack_words != 0U ||
        (function->instruction_count != 0U &&
         function->instructions == NULL) ||
        function->instruction_count > SIZE_MAX / sizeof(*plan->calls))
        return false;
    if (!rv32_plan_incoming(program, target, function, plan)) goto failure;
    plan->call_count = function->instruction_count;
    if (plan->call_count != 0U) {
        plan->calls = calloc(plan->call_count, sizeof(*plan->calls));
        if (plan->calls == NULL) goto failure;
    }
    for (i = 0U; i < plan->call_count; ++i) {
        const MinicCoreInstruction *ins = &function->instructions[i];
        Rv32CallAbiPlan *call = &plan->calls[i];
        if (ins->kind != MINIC_CORE_INSTRUCTION_CALL &&
            ins->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL)
            continue;
        if (!rv32_plan_call(program, target, function, ins, call)) {
            if (getenv("MINIC_RV32_FRAME_TRACE") != NULL)
                (void)fprintf(stderr,
                    "RV32_ABI_REJECT stage=call_plan instruction=%zu kind=%d\n",
                    i, (int)ins->kind);
            goto failure;
        }
        if (call->stack_words > plan->max_outgoing_stack_words)
            plan->max_outgoing_stack_words = call->stack_words;
    }
    return true;
failure:
    rv32_function_abi_plan_destroy(plan);
    return false;
}

const Rv32CallAbiPlan *rv32_function_abi_call(
    const Rv32FunctionAbiPlan *plan, MinicCoreInstructionId index) {
    if (plan == NULL || (size_t)index >= plan->call_count ||
        plan->calls == NULL || !plan->calls[index].prepared)
        return NULL;
    return &plan->calls[index];
}
