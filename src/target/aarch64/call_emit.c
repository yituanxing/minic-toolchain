#include "target/aarch64/call_emit.h"
#include "target/aarch64/scalar_normalize.h"
#include "target/aarch64/block_emit.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_call_plan.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_aggregate.h"
#include "target/aarch64/abi.h"
#include <stdio.h>

/* One or two INTEGER-class ABI eightbytes. Serialize actual record bytes,
 * not a speculative 64-bit load across a partial object/array boundary.
 * x9-x11 are scratch; x12 remains reserved for large-SP legalization. */
static bool emit_lp64_record_chunk_argument(FILE *out, size_t offset,
                                             size_t bytes, bool in_reg,
                                             size_t gp, size_t stack_slot) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U)) return false;
    if (fprintf(out, "  movz x11, #%zu\n", offset & 65535U) < 0 ||
        (offset > 65535U &&
         fprintf(out, "  movk x11, #%zu, lsl #16\n", offset >> 16U) < 0) ||
        fputs("  add x11, sp, x11\n  mov x9, xzr\n", out) == EOF)
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  ldrb w10, [x11, #%zu]\n"
                         "  orr x9, x9, x10, lsl #%zu\n",
                    b, b * 8U) < 0) return false;
    return in_reg
        ? fprintf(out, "  mov x%zu, x9\n", gp) >= 0
        : fprintf(out, "  str x9, [sp, #%zu]\n", stack_slot * 8U) >= 0;
}

/* Outgoing calls and incoming named parameters share the same ABI plan
 * contracts, but native moves, stack adjustments, floating register
 * assignments and record chunk transport remain ISA-owned. */
bool minic_aarch64_emit_abi_instruction(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, size_t offset,
    bool *needs_gp_store) {
    const MinicC0Program *program;
    const MinicTargetInfo *target;
    const MinicCoreFunction *fn;
    const MinicLP64FramePlan *frame_plan;
    const MinicLP64ArgumentPlan *incoming_plan;
    size_t frame;
    if (out == NULL || ctx == NULL || ctx->program == NULL ||
        ctx->target == NULL || ctx->fn == NULL || ctx->frame_plan == NULL ||
        ctx->incoming_plan == NULL || ctx->outgoing_calls == NULL ||
        ins == NULL ||
        needs_gp_store == NULL)
        return false;
    program = ctx->program;
    target = ctx->target;
    fn = ctx->fn;
    frame_plan = ctx->frame_plan;
    incoming_plan = ctx->incoming_plan;
    frame = ctx->frame;
    *needs_gp_store = false;
    switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_CALL:
        case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
            const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
            const size_t begin = indirect ? ins->value.indirect_call.argument_begin
                                          : ins->value.call.argument_begin;
            const size_t count = indirect ? ins->value.indirect_call.argument_count
                                          : ins->value.call.argument_count;
            size_t j;
            size_t stack_bytes;
            const MinicLP64PreparedCall *call_plan;
            size_t call_id;
            if (fn->instructions == NULL ||
                ins < fn->instructions ||
                ins >= fn->instructions + fn->instruction_count)
                return false;
            call_id = (size_t)(ins - fn->instructions);
            call_plan = minic_lp64_function_call_plan(
                ctx->outgoing_calls, call_id);
            if (call_plan == NULL || call_plan->argument_count != count)
                return false;
            stack_bytes = call_plan->stack_bytes;
            if (stack_bytes != 0U &&
                fprintf(out, "  sub sp, sp, #%zu\n", stack_bytes) < 0)
                return false;
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                const MinicLP64ArgumentLocation *location = &call_plan->arguments[j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots, object_offset, chunk;
                    const MinicCoreObjectId object_id = arg->value.object_id;
                    if (!minic_lp64_frame_plan_object(frame_plan, object_id, &object_offset))
                        return false;
                    if (!minic_multi_gp_record(program, target,
                            fn->objects[object_id].type, &record_size, &slots)) {
                        const size_t src = object_offset + stack_bytes;
                        if (!minic_aarch64_indirect_record(program, target,
                                fn->objects[object_id].type, &record_size) ||
                            fprintf(out, "  movz x9, #%zu\n",
                                    src & 65535U) < 0 ||
                            (src > 65535U &&
                             fprintf(out, "  movk x9, #%zu, lsl #16\n",
                                     src >> 16U) < 0) ||
                            fputs("  add x9, sp, x9\n", out) == EOF ||
                            (location->gp_registers != 0U
                             ? fprintf(out, "  mov x%zu, x9\n", location->gp_first) < 0
                             : fprintf(out, "  str x9, [sp, #%zu]\n",
                                       location->stack_first * 8U) < 0))
                            return false;
                        continue;
                    }
                    for (chunk = 0U; chunk < slots; ++chunk) {
                        const bool in_reg = location->gp_registers == slots;
                        const size_t payload = record_size - chunk * 8U >= 8U
                            ? 8U : record_size - chunk * 8U;
                        if (!emit_lp64_record_chunk_argument(
                                out, object_offset + stack_bytes + chunk * 8U,
                                payload, in_reg, in_reg ? location->gp_first + chunk : 0U,
                                in_reg ? 0U : location->stack_first + chunk))
                            return false;
                    }
                    continue;
                }
                const MinicType type = fn->values[arg->value.value_id].type;
                const size_t slot = (size_t)arg->value.value_id * 8U + stack_bytes;
                if (minic_type_is_int128_integer(type)) {
                    size_t src, limb;
                    if (location->kind != MINIC_LP64_ARGUMENT_WIDE ||
                        !minic_lp64_frame_plan_wide_slot(frame_plan,
                            arg->value.value_id, &src))
                        return false;
                    src += stack_bytes;
                    for (limb = 0U; limb < 2U; ++limb) {
                        if (location->gp_registers == 2U) {
                            if (fprintf(out, "  ldr x%zu, [sp, #%zu]\n",
                                    location->gp_first + limb,
                                    src + limb * 8U) < 0)
                                return false;
                        } else if (location->gp_registers == 0U &&
                                   fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                                "  str x9, [sp, #%zu]\n",
                                           src + limb * 8U,
                                           (location->stack_first + limb) * 8U) < 0)
                            return false;
                        else if (location->gp_registers != 0U)
                            return false;
                    }
                } else if (minic_type_is_long_double(type)) {
                    size_t src;
                    if (!minic_lp64_frame_plan_wide_slot(frame_plan, arg->value.value_id, &src))
                        return false;
                    src += stack_bytes;
                    if (location->fp_registers != 0U) {
                        if (fprintf(out, "  ldr q%zu, [sp, #%zu]\n",
                                    location->fp_first, src) < 0)
                            return false;
                    } else {
                        if (fprintf(out, "  ldr q9, [sp, #%zu]\n"
                                         "  str q9, [sp, #%zu]\n",
                                    src, location->stack_first * 8U) < 0)
                            return false;
                    }
                } else if (minic_type_is_double(type) || minic_type_is_float(type)) {
                    const bool is_float = minic_type_is_float(type);
                    if (location->fp_registers != 0U) {
                        if (fprintf(out, is_float ? "  ldr s%zu, [sp, #%zu]\n"
                                                  : "  ldr d%zu, [sp, #%zu]\n",
                                    location->fp_first, slot) < 0)
                            return false;
                    } else if (fprintf(out, is_float
                                       ? "  ldr s9, [sp, #%zu]\n"
                                         "  str s9, [sp, #%zu]\n"
                                       : "  ldr d9, [sp, #%zu]\n"
                                         "  str d9, [sp, #%zu]\n",
                                       slot, location->stack_first * 8U) < 0)
                        return false;
                } else {
                    const bool wide = minic_lp64_register64_type(program, target, type);
                    if (location->gp_registers != 0U) {
                        if (fprintf(out, wide ? "  ldr x%zu, [sp, #%zu]\n"
                                              : "  ldr w%zu, [sp, #%zu]\n",
                                    location->gp_first, slot) < 0)
                            return false;
                    } else if (fprintf(out,
                                       wide ? "  ldr x9, [sp, #%zu]\n"
                                              "  str x9, [sp, #%zu]\n"
                                            : "  ldr w9, [sp, #%zu]\n"
                                              "  str w9, [sp, #%zu]\n",
                                       slot, location->stack_first * 8U) < 0)
                        return false;
                }
            }
            if (minic_type_is_record(ins->type)) {
                size_t bytes = 0U, dst;
                /* AAPCS64 keeps x8 independent of x0..x7, so setup after
                 * all normal arguments (including stack spills). A nested
                 * callee writes directly to the caller's Core record object.
                 * Indirect calls remain fail-closed until their record
                 * result Core operand form is separately verified. */
                if (minic_aarch64_indirect_record(
                        program, target, ins->type, &bytes) && bytes > 16U) {
                    MinicCoreObjectId object_id;
                    size_t result_offset;
                    if (indirect) return false;
                    object_id = ins->value.call.result_object;
                    if (object_id >= fn->object_count ||
                        !minic_lp64_frame_plan_object(
                            frame_plan, object_id, &dst) ||
                        dst > SIZE_MAX - (ctx->dynamic_frame ? 0U : stack_bytes))
                        return false;
                    result_offset = dst +
                        (ctx->dynamic_frame ? 0U : stack_bytes);
                    if (fprintf(out, "  movz x8, #%zu\n",
                                result_offset & 65535U) < 0 ||
                        (result_offset > 65535U &&
                         fprintf(out, "  movk x8, #%zu, lsl #16\n",
                                 result_offset >> 16U) < 0) ||
                        fprintf(out, "  add x8, %s, x8\n",
                                ctx->dynamic_frame ? "x19" : "sp") < 0)
                        return false;
                }
            }
            if (indirect) {
                if (fprintf(out, "  ldr x16, [sp, #%zu]\n"
                                 "  blr x16\n",
                            (size_t)ins->value.indirect_call.callee * 8U +
                            stack_bytes) < 0)
                    return false;
            } else {
                const MinicCoreCallee *callee =
                    &fn->callees[ins->value.call.callee_id];
                if (fprintf(out, "  bl %s\n", callee->name) < 0)
                    return false;
            }
            if (stack_bytes != 0U &&
                fprintf(out, "  add sp, sp, #%zu\n", stack_bytes) < 0)
                return false;
            if (minic_type_is_void(ins->type)) return true;
            if (minic_type_is_record(ins->type)) {
                const MinicCoreObjectId object_id =
                    ins->value.call.result_object;
                size_t bytes, slots, dst, chunk, fp_elements, fp_width;
                if (indirect || object_id >= fn->object_count ||
                    !minic_lp64_frame_plan_object(frame_plan, object_id, &dst))
                    return false;
                if (minic_aarch64_indirect_record(
                        program, target, ins->type, &bytes) && bytes > 16U)
                    return true; /* callee already filled result_object via x8 */
                if (minic_multi_fp_return_record(
                        program, target, ins->type, &fp_elements, &fp_width)) {
                    for (chunk = 0U; chunk < fp_elements; ++chunk) {
                        const size_t off = dst + chunk * fp_width;
                        if (off > UINT32_MAX ||
                            fprintf(out, "  movz x11, #%zu\n", off & 65535U) < 0 ||
                            (off > 65535U &&
                             fprintf(out, "  movk x11, #%zu, lsl #16\n",
                                     off >> 16U) < 0) ||
                            fprintf(out, "  add x11, %s, x11\n"
                                         "  str %c%zu, [x11]\n",
                                    ctx->dynamic_frame ? "x19" : "sp",
                                    fp_width == 4U ? 's' : 'd', chunk) < 0)
                            return false;
                    }
                    return true;
                }
                if (!minic_multi_gp_record(program, target, ins->type,
                                            &bytes, &slots) ||
                    bytes == 0U || bytes > 16U || slots > 2U)
                    return false;
                for (chunk = 0U; chunk < slots; ++chunk) {
                    const size_t payload = bytes - chunk * 8U >= 8U
                        ? 8U : bytes - chunk * 8U;
                    if (!minic_aarch64_emit_lp64_record_chunk_parameter(out,
                            dst + chunk * 8U, payload, true, chunk, 0U))
                        return false;
                }
                return true;
            }
            if (minic_type_is_int128_integer(ins->type)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    fprintf(out, "  str x0, [sp, #%zu]\n"
                                 "  str x1, [sp, #%zu]\n",
                            dst, dst + 8U) < 0)
                    return false;
                return true;
            }
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    fprintf(out, "  str q0, [sp, #%zu]\n", dst) < 0)
                    return false;
                return true;
            }
            if (fputs(minic_type_is_double(ins->type)
                          ? "  fmov x9, d0\n"
                          : minic_type_is_float(ins->type)
                              ? "  fmov w9, s0\n"
                          : minic_lp64_register64_type(program, target, ins->type)
                              ? "  mov x9, x0\n"
                              : "  mov w9, w0\n", out) == EOF)
                return false;
            if (minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!minic_lp64_scalar_integer_width(program, target, ins->type,
                                          &width, &sign) ||
                    (width != 8U && !minic_aarch64_emit_integer_normalize(out, width, sign)))
                    return false;
            }
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_PARAMETER: {
            const MinicLP64ArgumentLocation *location = NULL;
            if (!minic_multi_direct_scalar_parameter_location(
                    fn, ins, incoming_plan, &location))
                return false;
            if (minic_type_is_int128_integer(ins->type)) {
                size_t dst, limb;
                if (location->kind != MINIC_LP64_ARGUMENT_WIDE ||
                    !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return false;
                for (limb = 0U; limb < 2U; ++limb) {
                    if (location->gp_registers == 2U) {
                        if (fprintf(out, "  str x%zu, [sp, #%zu]\n",
                                    location->gp_first + limb, dst + limb * 8U) < 0)
                            return false;
                    } else if (location->gp_registers == 0U &&
                               fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                            "  str x9, [sp, #%zu]\n",
                                       frame + (location->stack_first + limb) * 8U,
                                       dst + limb * 8U) < 0)
                        return false;
                    else if (location->gp_registers != 0U)
                        return false;
                }
                return true;
            }
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return false;
                if (location->fp_registers != 0U) {
                    if (fprintf(out, "  str q%zu, [sp, #%zu]\n",
                                location->fp_first, dst) < 0)
                        return false;
                } else {
                    if (fprintf(out, "  ldr q9, [sp, #%zu]\n"
                                     "  str q9, [sp, #%zu]\n",
                                frame + location->stack_first * 8U, dst) < 0)
                        return false;
                }
                return true;
            }
            if (minic_type_is_double(ins->type) ||
                minic_type_is_float(ins->type)) {
                const bool is_float = minic_type_is_float(ins->type);
                if (location->fp_registers != 0U) {
                    if (fprintf(out, is_float ? "  str s%zu, [sp, #%zu]\n"
                                              : "  str d%zu, [sp, #%zu]\n",
                                location->fp_first, offset) < 0)
                        return false;
                } else if (fprintf(out, is_float
                                     ? "  ldr s9, [sp, #%zu]\n"
                                       "  str s9, [sp, #%zu]\n"
                                     : "  ldr d9, [sp, #%zu]\n"
                                       "  str d9, [sp, #%zu]\n",
                                   frame + location->stack_first * 8U, offset) < 0)
                    return false;
                return true;
            }
            if (location->gp_registers != 0U) {
                if (fprintf(out, minic_lp64_register64_type(program, target, ins->type)
                                 ? "  str x%zu, [sp, #%zu]\n"
                                 : "  str w%zu, [sp, #%zu]\n",
                            location->gp_first, offset) < 0)
                    return false;
            } else if (fprintf(out, minic_lp64_register64_type(program, target, ins->type)
                               ? "  ldr x9, [sp, #%zu]\n"
                                 "  str x9, [sp, #%zu]\n"
                               : "  ldr w9, [sp, #%zu]\n"
                                 "  str w9, [sp, #%zu]\n",
                               frame + location->stack_first * 8U, offset) < 0)
                return false;
            return true;
        }
        default:
            return false;
    }
}
