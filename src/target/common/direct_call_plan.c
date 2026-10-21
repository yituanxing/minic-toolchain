#include "target/common/direct_call_plan.h"
#include "target/common/lp64_aggregate.h"
#include <stdint.h>
#include <string.h>

/* Locations are ABI-owned; no architecture-specific instructions here. */
static bool minic_lp64_plan_types(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicType *types, size_t count, size_t fixed, bool variadic,
    MinicLP64DirectABI abi, MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *plan) {
    size_t gp = 0U, fp = 0U, stack = 0U, j;
    if (program == NULL || target == NULL || plan == NULL ||
        indirect_record == NULL || count > MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS ||
        (count != 0U && types == NULL) || fixed > count ||
        (!variadic && fixed != count) ||
        (abi != MINIC_LP64_ABI_AAPCS64 &&
         abi != MINIC_LP64_ABI_LOONGARCH64_LP64D))
        return false;
    (void)memset(plan, 0, sizeof(*plan));
    plan->argument_count = count;
    for (j = 0U; j < count; ++j) {
        MinicLP64ArgumentLocation *loc = &plan->arguments[j];
        size_t chunks = 1U, size = 0U;
        const MinicType type = types[j];
        const bool record = minic_type_is_record(type);
        bool by_reference = false;
        loc->gp_first = loc->fp_first = loc->stack_first = SIZE_MAX;
        if (record) {
            if (!minic_multi_gp_record(program, target, type, &size, &chunks)) {
                if (!indirect_record(program, target, type, &size)) return false;
                by_reference = true;
                chunks = 1U;
            }
        }
        loc->size_bytes = size;
        loc->chunks = chunks;
        if (record) {
            if (by_reference) {
                loc->kind = MINIC_LP64_ARGUMENT_INDIRECT_RECORD;
                if (gp < 8U) {
                    loc->gp_first = gp;
                    loc->gp_registers = 1U;
                } else {
                    loc->stack_first = stack++;
                    loc->stack_slots = 1U;
                }
                ++gp;
            } else {
                size_t available = gp < 8U ? 8U - gp : 0U;
                const size_t used = abi == MINIC_LP64_ABI_AAPCS64
                    ? (available >= chunks ? chunks : 0U)
                    : (available < chunks ? available : chunks);
                loc->kind = MINIC_LP64_ARGUMENT_RECORD;
                if (used != 0U) {
                    loc->gp_first = gp;
                    loc->gp_registers = used;
                }
                if (used < chunks) {
                    loc->stack_first = stack;
                    loc->stack_slots = chunks - used;
                    stack += chunks - used;
                }
                gp = abi == MINIC_LP64_ABI_AAPCS64 && used == 0U
                    ? 8U : gp + used;
            }
        } else if (minic_type_is_long_double(type)) {
            loc->kind = MINIC_LP64_ARGUMENT_WIDE;
            if (abi == MINIC_LP64_ABI_AAPCS64) {
                if (fp < 8U) {
                    loc->fp_first = fp;
                    loc->fp_registers = 1U;
                } else {
                    stack += stack & 1U;
                    loc->stack_first = stack;
                    loc->stack_slots = 2U;
                    stack += 2U;
                }
                ++fp;
            } else {
                size_t available, used;
                if (variadic && j >= fixed && gp < 8U && (gp & 1U)) ++gp;
                available = gp < 8U ? 8U - gp : 0U;
                used = available < 2U ? available : 2U;
                if (used != 0U) {
                    loc->gp_first = gp;
                    loc->gp_registers = used;
                } else stack += stack & 1U;
                if (used < 2U) {
                    loc->stack_first = stack;
                    loc->stack_slots = 2U - used;
                    stack += 2U - used;
                }
                gp += used;
            }
        } else if (minic_type_is_double(type) || minic_type_is_float(type)) {
            const bool use_fp = abi == MINIC_LP64_ABI_AAPCS64 ||
                ((!variadic || j < fixed) && fp < 8U);
            if (use_fp) {
                loc->kind = MINIC_LP64_ARGUMENT_FP;
                if (fp < 8U) {
                    loc->fp_first = fp;
                    loc->fp_registers = 1U;
                } else {
                    loc->stack_first = stack++;
                    loc->stack_slots = 1U;
                }
                ++fp;
            } else {
                loc->kind = MINIC_LP64_ARGUMENT_GP;
                if (gp < 8U) {
                    loc->gp_first = gp;
                    loc->gp_registers = 1U;
                } else {
                    loc->stack_first = stack++;
                    loc->stack_slots = 1U;
                }
                ++gp;
            }
        } else {
            loc->kind = MINIC_LP64_ARGUMENT_GP;
            if (gp < 8U) {
                loc->gp_first = gp;
                loc->gp_registers = 1U;
            } else {
                loc->stack_first = stack++;
                loc->stack_slots = 1U;
            }
            ++gp;
        }
    }
    /* Every call is already capped at 64 arguments, each at most two slots. */
    if (stack > (SIZE_MAX - 15U) / 8U) return false;
    plan->stack_slots = stack;
    plan->stack_bytes = (stack * 8U + 15U) & ~(size_t)15U;
    return true;
}

/* The same ABI classifier/placer serves outgoing CALL arguments and the
 * incoming named-parameter signature. ISA-specific value movement stays in
 * codegen; location assignment is performed exactly once per boundary. */
bool minic_lp64_plan_direct_call(const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreInstruction *call, MinicLP64DirectABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *plan) {
    MinicType types[MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS];
    size_t begin, count, fixed, j;
    bool variadic;
    if (fn == NULL || call == NULL) return false;
    if (call->kind == MINIC_CORE_INSTRUCTION_CALL) {
        const MinicCoreCallee *callee;
        if (call->value.call.callee_id >= fn->callee_count) return false;
        callee = &fn->callees[call->value.call.callee_id];
        begin = call->value.call.argument_begin;
        count = call->value.call.argument_count;
        fixed = callee->parameter_count;
        variadic = callee->is_variadic;
    } else if (call->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
        const MinicCoreCallSignature *signature;
        if (call->value.indirect_call.signature_id >= fn->call_signature_count)
            return false;
        signature = &fn->call_signatures[call->value.indirect_call.signature_id];
        begin = call->value.indirect_call.argument_begin;
        count = call->value.indirect_call.argument_count;
        fixed = signature->parameter_count;
        variadic = signature->is_variadic;
    } else return false;
    if (count > MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS || begin > fn->call_argument_count ||
        count > fn->call_argument_count - begin ||
        (count != 0U && fn->call_arguments == NULL))
        return false;
    for (j = 0U; j < count; ++j) {
        const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
        if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            if (arg->value.object_id >= fn->object_count ||
                fn->objects == NULL) return false;
            types[j] = fn->objects[arg->value.object_id].type;
            if (!minic_type_is_record(types[j])) return false;
        } else if (arg->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
            if (arg->value.value_id >= fn->value_count ||
                fn->values == NULL) return false;
            types[j] = fn->values[arg->value.value_id].type;
            if (minic_type_is_record(types[j])) return false;
        } else return false;
    }
    return minic_lp64_plan_types(program, target, types, count, fixed,
                                variadic, abi, indirect_record, plan);
}

/* One-time per-function ingress planning. All function parameters are named
 * and therefore fixed even when the function declares an ellipsis.
 * Reuses exactly the same register/stack classifier as outgoing calls. */
bool minic_lp64_plan_incoming_parameters(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicLP64DirectABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *plan) {
    if (fn == NULL) return false;
    return minic_lp64_plan_types(program, target, fn->parameter_types,
        fn->parameter_count, fn->parameter_count, false, abi,
        indirect_record, plan);
}
