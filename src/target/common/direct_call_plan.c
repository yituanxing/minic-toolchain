#include "target/common/direct_call_plan.h"
#include "target/common/lp64_aggregate.h"
#include <stdint.h>
#include <string.h>

/* Count registers by ABI rather than guessing from ISA opcode names.
 * Passes no target-specific assembly and allocates no dynamic storage. */
bool minic_lp64_plan_direct_call(const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreInstruction *call, MinicLP64DirectFrameABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64DirectCallPlan *plan) {
    size_t begin, count, fixed, gp = 0U, fp = 0U, stack = 0U, j;
    bool variadic;
    if (program == NULL || target == NULL || fn == NULL || call == NULL ||
        indirect_record == NULL || plan == NULL ||
        (abi != MINIC_LP64_FRAME_AAPCS64 &&
         abi != MINIC_LP64_FRAME_LOONGARCH64_LP64D))
        return false;
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
    if (count > MINIC_LP64_MAX_CALL_ARGUMENTS || fixed > count ||
        (!variadic && fixed != count) || begin > fn->call_argument_count ||
        count > fn->call_argument_count - begin)
        return false;
    (void)memset(plan, 0, sizeof(*plan));
    plan->argument_count = count;
    for (j = 0U; j < count; ++j) {
        const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
        MinicLP64ArgumentLocation *loc = &plan->arguments[j];
        size_t chunks = 1U, size = 0U;
        bool record = false, by_reference = false;
        MinicType type;
        loc->gp_first = loc->fp_first = loc->stack_first = SIZE_MAX;
        if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            if (arg->value.object_id >= fn->object_count) return false;
            type = fn->objects[arg->value.object_id].type;
            record = true;
            if (!minic_multi_gp_record(program, target, type, &size, &chunks)) {
                if (!indirect_record(program, target, type, &size)) return false;
                by_reference = true;
                chunks = 1U;
            }
        } else if (arg->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
            if (arg->value.value_id >= fn->value_count) return false;
            type = fn->values[arg->value.value_id].type;
        } else return false;
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
                const size_t used = abi == MINIC_LP64_FRAME_AAPCS64
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
                gp = abi == MINIC_LP64_FRAME_AAPCS64 && used == 0U
                    ? 8U : gp + used;
            }
        } else if (minic_type_is_long_double(type)) {
            loc->kind = MINIC_LP64_ARGUMENT_WIDE;
            if (abi == MINIC_LP64_FRAME_AAPCS64) {
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
            const bool use_fp = abi == MINIC_LP64_FRAME_AAPCS64 ||
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
