#include "target/loongarch64/call_emit.h"
#include "target/loongarch64/block_emit.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_call_plan.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_aggregate.h"
#include "target/loongarch64/frame_emit.h"
#include <stdio.h>

/* Outgoing locations use raw SP; dynamic frame slots use the s0 base. */
#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

/* LP64D integral aggregate bytes travel in one/two a-registers or
 * eightbyte stack slots. Use byte packing to avoid reading beyond a
 * partially filled final slot, preserving exact Core snapshot bytes. */
static bool emit_lp64_record_chunk_argument(FILE *out, size_t offset,
                                             size_t bytes, bool in_reg,
                                             size_t gp, size_t stack_slot,
                                             bool dynamic_frame) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U) ||
        fprintf(out, "  li.d $t2, %zu\n"
                     "  add.d $t2, %s, $t2\n"
                     "  move $t0, $zero\n", offset,
                dynamic_frame ? "$s0" : "$sp") < 0)
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  ld.bu $t1, $t2, %zu\n"
                         "  slli.d $t1, $t1, %zu\n"
                         "  or $t0, $t0, $t1\n",
                    b, b * 8U) < 0) return false;
    if (in_reg)
        return fprintf(out, "  or $a%zu, $t0, $zero\n", gp) >= 0;
    return minic_loongarch64_emit_sp_mem(out, "st.d", "$t0", stack_slot * 8U);
}

/* Outgoing calls and incoming named parameters share the same ABI plan
 * contracts, but native moves, stack adjustments, floating register
 * assignments and record chunk transport remain ISA-owned. */
bool minic_loongarch64_emit_abi_instruction(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, size_t offset,
    bool (*normalize)(FILE *, size_t, bool),
    bool *needs_gp_store) {
    const MinicC0Program *program;
    const MinicTargetInfo *target;
    const MinicCoreFunction *fn;
    const MinicLP64FramePlan *frame_plan;
    const MinicLP64ArgumentPlan *incoming_plan;
    size_t frame;
    bool dynamic_frame;
    if (out == NULL || ctx == NULL || ctx->program == NULL ||
        ctx->target == NULL || ctx->fn == NULL || ctx->frame_plan == NULL ||
        ctx->incoming_plan == NULL || ins == NULL || normalize == NULL ||
        needs_gp_store == NULL)
        return false;
    program = ctx->program;
    target = ctx->target;
    fn = ctx->fn;
    frame_plan = ctx->frame_plan;
    incoming_plan = ctx->incoming_plan;
    frame = ctx->frame;
    dynamic_frame = ctx->dynamic_frame;
    *needs_gp_store = false;
    switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_CALL:
        case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
            const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
            const size_t begin = indirect ? ins->value.indirect_call.argument_begin
                                          : ins->value.call.argument_begin;
            const size_t count = indirect ? ins->value.indirect_call.argument_count
                                          : ins->value.call.argument_count;
            static const char *const gp_regs[8] = {
                "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"};
            static const char *const fp_regs[8] = {
                "$fa0", "$fa1", "$fa2", "$fa3", "$fa4", "$fa5", "$fa6", "$fa7"};
            size_t j;
            size_t stack_bytes;
            MinicLP64ArgumentPlan call_plan;
            /* Resolve ABI locations once before allocating outgoing stack
             * space; the same plan drives each argument's source cursor. */
            if (!minic_lp64_plan_direct_call(
                    program, target, fn, ins,
                    MINIC_LP64_ABI_LOONGARCH64_LP64D,
                    minic_multi_indirect_record,
                    &call_plan))
                return false;
            stack_bytes = call_plan.stack_bytes;
            if (stack_bytes != 0U && !minic_loongarch64_emit_sp_adjust(out, stack_bytes, false))
                return false;
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                const MinicLP64ArgumentLocation *location = &call_plan.arguments[j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots, object_offset, chunk;
                    const MinicCoreObjectId object_id = arg->value.object_id;
                    if (!minic_lp64_frame_plan_object(frame_plan, object_id, &object_offset))
                        return false;
                    if (!minic_multi_gp_record(program, target,
                            fn->objects[object_id].type, &record_size, &slots)) {
                        const size_t src = object_offset +
                            (dynamic_frame ? 0U : stack_bytes);
                        if (!minic_multi_indirect_record(program, target,
                                fn->objects[object_id].type, &record_size) ||
                            fprintf(out, "  li.d $t0, %zu\n"
                                         "  add.d $t0, %s, $t0\n",
                                    src, dynamic_frame ? "$s0" : "$sp") < 0 ||
                            (location->gp_registers != 0U
                             ? fprintf(out, "  move $a%zu, $t0\n", location->gp_first) < 0
                             : !minic_loongarch64_emit_frame_mem(out, "st.d", "$t0",
                                               location->stack_first * 8U, false)))
                            return false;
                        continue;
                    }
                    for (chunk = 0U; chunk < slots; ++chunk) {
                        const bool in_reg = chunk < location->gp_registers;
                        const size_t stack_slot = in_reg ? 0U :
                            location->stack_first + chunk - location->gp_registers;
                        const size_t payload = record_size - chunk * 8U >= 8U
                            ? 8U : record_size - chunk * 8U;
                        if (!emit_lp64_record_chunk_argument(
                                out, object_offset + (dynamic_frame ? 0U : stack_bytes) + chunk * 8U,
                                payload, in_reg, in_reg ? location->gp_first + chunk : 0U,
                                stack_slot, dynamic_frame))
                            return false;
                    }
                    continue;
                }
                const MinicType type = fn->values[arg->value.value_id].type;
                const size_t slot = (size_t)arg->value.value_id * 8U +
                    (dynamic_frame ? 0U : stack_bytes);
                if (minic_type_is_long_double(type)) {
                    size_t src, chunk;
                    const size_t used = location->gp_registers;
                    if (!minic_lp64_frame_plan_wide_slot(frame_plan, arg->value.value_id, &src))
                        return false;
                    for (chunk = 0U; chunk < 2U; ++chunk) {
                        if (chunk < used) {
                            if (!emit_sp_mem(out, "ld.d", gp_regs[location->gp_first + chunk],
                                             src + (dynamic_frame ? 0U : stack_bytes) + chunk * 8U))
                                return false;
                        } else if (!emit_sp_mem(out, "ld.d", "$t0",
                                                src + (dynamic_frame ? 0U : stack_bytes) + chunk * 8U) ||
                                   !minic_loongarch64_emit_frame_mem(out, "st.d", "$t0",
                                                   (location->stack_first + chunk - used) * 8U,
                                                   false))
                            return false;
                    }
                    continue;
                }
                if (location->fp_registers != 0U) {
                    if (!emit_sp_mem(out, minic_type_is_float(type)
                                           ? "fld.s" : "fld.d",
                                     fp_regs[location->fp_first], slot))
                        return false;
                } else {
                    const bool wide = minic_type_is_double(type) ||
                        minic_lp64_register64_type(program, target, type);
                    if (location->gp_registers != 0U) {
                        if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                                         gp_regs[location->gp_first], slot))
                            return false;
                    } else if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                                            "$t0", slot) ||
                               /* The LoongArch LP64D stack argument slot is
                                * 8 bytes even for 32-bit int/unsigned int.
                                * ld.w sign-extends both (ABI requirement),
                                * so st.d must write all 64 bits. st.w left
                                * the upper half uninitialized and could
                                * poison a variadic callee's 8-byte va slot. */
                               !minic_loongarch64_emit_frame_mem(out, "st.d",
                                            "$t0", location->stack_first * 8U, false))
                        return false;
                }
            }
            if (indirect) {
                if (!emit_sp_mem(out, "ld.d", "$t2",
                                 (size_t)ins->value.indirect_call.callee * 8U +
                                 (dynamic_frame ? 0U : stack_bytes)) ||
                    fputs("  jirl $ra, $t2, 0\n", out) == EOF)
                    return false;
            } else {
                const MinicCoreCallee *callee =
                    &fn->callees[ins->value.call.callee_id];
                if (fprintf(out, "  bl %s\n", callee->name) < 0)
                    return false;
            }
            if (stack_bytes != 0U && !minic_loongarch64_emit_sp_adjust(out, stack_bytes, true))
                return false;
            if (minic_type_is_void(ins->type)) return true;
            if (minic_type_is_record(ins->type)) {
                const MinicCoreObjectId object_id =
                    ins->value.call.result_object;
                size_t bytes, slots, dst, chunk;
                if (indirect || object_id >= fn->object_count ||
                    !minic_multi_gp_record(program, target, ins->type,
                                           &bytes, &slots) ||
                    bytes == 0U || bytes > 16U || slots > 2U ||
                    !minic_lp64_frame_plan_object(frame_plan, object_id, &dst))
                    return false;
                for (chunk = 0U; chunk < slots; ++chunk) {
                    const size_t payload = bytes - chunk * 8U >= 8U
                        ? 8U : bytes - chunk * 8U;
                    if (!minic_loongarch64_emit_lp64_record_chunk_parameter(out,
                            dst + chunk * 8U, payload, true, chunk, 0U, dynamic_frame))
                        return false;
                }
                return true;
            }
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    !emit_sp_mem(out, "st.d", "$a0", dst) ||
                    !emit_sp_mem(out, "st.d", "$a1", dst + 8U))
                    return false;
                return true;
            }
            if (fputs(minic_type_is_double(ins->type)
                          ? "  movfr2gr.d $t0, $fa0\n"
                          : minic_type_is_float(ins->type)
                              ? "  movfr2gr.s $t0, $fa0\n"
                          : "  or $t0, $a0, $zero\n", out) == EOF)
                return false;
            if (minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!minic_lp64_scalar_integer_width(program, target, ins->type,
                                          &width, &sign) ||
                    (width != 8U && !emit_integer_normalize(out, width, sign)))
                    return false;
            }
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_PARAMETER: {
            static const char *const gp_regs[8] = {
                "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"};
            static const char *const fp_regs[8] = {
                "$fa0", "$fa1", "$fa2", "$fa3", "$fa4", "$fa5", "$fa6", "$fa7"};
            const MinicLP64ArgumentLocation *location = NULL;
            if (!minic_multi_direct_scalar_parameter_location(
                    fn, ins, incoming_plan, &location))
                return false;
            if (minic_type_is_long_double(ins->type)) {
                size_t dst, chunk;
                const size_t used = location->gp_registers;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return false;
                for (chunk = 0U; chunk < 2U; ++chunk) {
                    if (chunk < used) {
                        if (!emit_sp_mem(out, "st.d", gp_regs[location->gp_first + chunk],
                                         dst + chunk * 8U))
                            return false;
                    } else if (!emit_sp_mem(out, "ld.d", "$t0",
                                            frame + (location->stack_first + chunk - used) * 8U) ||
                               !emit_sp_mem(out, "st.d", "$t0",
                                            dst + chunk * 8U))
                        return false;
                }
                return true;
            }
            if ((minic_type_is_double(ins->type) ||
                 minic_type_is_float(ins->type)) && location->fp_registers != 0U) {
                if (!emit_sp_mem(out, minic_type_is_float(ins->type)
                                      ? "fst.s" : "fst.d",
                                 fp_regs[location->fp_first], offset))
                    return false;
            } else if (location->gp_registers != 0U) {
                if (!emit_sp_mem(out,
                                 minic_lp64_register64_type(program, target, ins->type) ||
                                 minic_type_is_double(ins->type) ? "st.d" : "st.w",
                                 gp_regs[location->gp_first], offset))
                    return false;
            } else {
                const char *const load =
                    minic_lp64_register64_type(program, target, ins->type) ||
                    minic_type_is_double(ins->type) ? "ld.d" : "ld.w";
                const char *const store =
                    minic_lp64_register64_type(program, target, ins->type) ||
                    minic_type_is_double(ins->type) ? "st.d" : "st.w";
                if (!emit_sp_mem(out, load, "$t0",
                                 frame + location->stack_first * 8U) ||
                    !emit_sp_mem(out, store, "$t0", offset))
                    return false;
            }
            return true;
        }
        default:
            return false;
    }
}

#undef emit_sp_mem
