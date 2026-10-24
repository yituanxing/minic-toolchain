#include "target/riscv32/rv32_abi_plan.h"
#include "target/riscv32/rv32_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* One psABI planner per Core function, used unchanged by physical frame
 * allocation, direct/indirect calls and callee parameter reception.
 * No target-unaware Core or frontend code knows about a0..a7 or ILP32. */
void rv32_function_abi_plan_destroy(Rv32FunctionAbiPlan *plan) {
    size_t i;
    if (plan == NULL) return;
    for (i = 0U; i < plan->call_count; ++i)
        free(plan->calls[i].arguments);
    free(plan->calls);
    free(plan->parameters);
    (void)memset(plan, 0, sizeof(*plan));
}

static bool rv32_plan_incoming(const MinicCoreFunction *function,
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
        if (!rv32_scalar_storage_words(function, function->parameter_types[i],
                                       &entry->words) ||
            !minic_riscv32_abi_place(&cursor, entry->words, false,
                                      &entry->location))
            return false;
    }
    return true;
}

static bool rv32_plan_call(const MinicCoreFunction *function,
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
                function, &view.arguments[i], &type) ||
            !rv32_scalar_storage_words(function, type, &entry->words) ||
            !minic_riscv32_abi_place(&cursor, entry->words,
                view.is_variadic && i >= view.parameter_count,
                &entry->location))
            return false;
    }
    call->view = view;
    call->stack_words = cursor.stack_words;
    call->prepared = true;
    return true;
}

bool rv32_function_abi_plan_build(const MinicCoreFunction *function,
                                  Rv32FunctionAbiPlan *plan) {
    size_t i;
    if (function == NULL || plan == NULL ||
        plan->calls != NULL || plan->parameters != NULL ||
        plan->call_count != 0U || plan->parameter_count != 0U ||
        plan->max_outgoing_stack_words != 0U ||
        (function->instruction_count != 0U &&
         function->instructions == NULL) ||
        function->instruction_count > SIZE_MAX / sizeof(*plan->calls))
        return false;
    if (!rv32_plan_incoming(function, plan)) goto failure;
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
        if (!rv32_plan_call(function, ins, call))
            goto failure;
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
