#include "target/aarch64/call_emit.h"
#include "target/common/elf_module_symbol_info.h"
#include "target/aarch64/scalar_normalize.h"
#include "target/aarch64/block_emit.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_call_plan.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/core_abi_value_shape.h"
#include "target/common/core_extended_float_runtime.h"
#include "target/aarch64/abi.h"
#include <stdio.h>

typedef struct MinicA64ScalarShape {
    bool is_void;
    bool is_record;
    bool is_int128;
    bool is_long_double;
    bool is_double;
    bool is_float;
    bool is_integer;
    bool register64;
    size_t integer_width;
    bool integer_signed;
} MinicA64ScalarShape;

/* AAPCS64-specific register widths are derived from the common sealed ABI
 * value shape; register placement remains in the prepared AAPCS64 plan. */
static bool a64_scalar_shape(const MinicCoreFunction *fn,
                             MinicCoreTypeId id, MinicA64ScalarShape *shape) {
    MinicCoreAbiValueShape value;
    if (shape == NULL || !minic_core_abi_value_shape(fn, id, &value))
        return false;
    *shape = (MinicA64ScalarShape){0};
    switch (value.kind) {
    case MINIC_CORE_TYPE_VOID:
        shape->is_void = true;
        return true;
    case MINIC_CORE_TYPE_RECORD:
        shape->is_record = true;
        return true;
    case MINIC_CORE_TYPE_INTEGER:
        shape->is_integer = true;
        if (value.bytes == 16U) {
            shape->is_int128 = true;
            return true;
        }
        shape->integer_width = value.integer_width;
        shape->integer_signed = value.integer_signed;
        shape->register64 = shape->integer_width == 8U;
        return true;
    case MINIC_CORE_TYPE_POINTER:
        shape->register64 = value.bytes == 8U;
        return shape->register64;
    case MINIC_CORE_TYPE_FLOAT:
        shape->is_float = value.float_format == MINIC_CORE_FLOAT_BINARY32;
        shape->is_double = value.float_format == MINIC_CORE_FLOAT_BINARY64;
        shape->is_long_double = value.float_format == MINIC_CORE_FLOAT_BINARY128;
        return true;
    default:
        return false;
    }
}

static bool a64_empty_record(const MinicCoreFunction *fn, MinicCoreTypeId id) {
    return fn->type_context != NULL &&
           minic_multi_empty_record_core(fn->type_context, id);
}

static bool a64_gp_record(const MinicCoreFunction *fn, MinicCoreTypeId id,
                          size_t *bytes, size_t *slots) {
    return fn->type_context != NULL &&
           minic_multi_gp_record_core(fn->type_context, id, bytes, slots);
}

static bool a64_indirect_record(const MinicCoreFunction *fn, MinicCoreTypeId id,
                                size_t *bytes) {
    return fn->type_context != NULL &&
           minic_aarch64_indirect_record_core(fn->type_context, id, bytes);
}

static bool a64_fp_return_record(const MinicCoreFunction *fn,
                                 MinicCoreTypeId id, size_t *count,
                                 size_t *width) {
    return fn->type_context != NULL &&
           minic_multi_fp_return_record_core(fn->type_context, id, count, width);
}

/* One or two INTEGER-class ABI eightbytes. Serialize actual record bytes,
 * not a speculative 64-bit load across a partial object/array boundary.
 * x9-x11 are scratch; x12 remains reserved for large-SP legalization. */
static bool emit_lp64_record_chunk_argument(FILE *out,
                                             const MinicA64FrameView *view,
                                             size_t offset, size_t bytes,
                                             bool in_reg, size_t gp,
                                             size_t stack_slot) {
    size_t b;
    if (view == NULL || bytes == 0U || bytes > 8U ||
        (in_reg && gp >= 8U) || stack_slot > SIZE_MAX / 8U ||
        !minic_a64_emit_owned_address(out, view, offset, 11U) ||
        fputs("  mov x9, xzr\n", out) == EOF)
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  ldrb w10, [x11, #%zu]\n"
                         "  orr x9, x9, x10, lsl #%zu\n",
                    b, b * 8U) < 0) return false;
    return in_reg
        ? fprintf(out, "  mov x%zu, x9\n", gp) >= 0
        : minic_a64_emit_frame_store(out, view,
            (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING, stack_slot * 8U},
            (MinicA64FrameRegister){MINIC_A64_REG_X, 9U},
            (MinicA64ScratchPolicy){0U, 0U});
}

/* Stacked incoming arguments belong to the original entry SP, not the
 * optionally realigned local frame. Use the saved entry address whenever
 * an over-aligned C object forced x19 to become the stable frame base. */
static bool aarch64_emit_incoming_stack_read(FILE *out,
    const MinicDirectLP64BlockContext *ctx,
    MinicA64FrameRegisterClass kind, unsigned int reg,
    size_t stack_slot) {
    const MinicA64FrameView view = {
        ctx->machine_frame->has_dynamic_stack, ctx->machine_frame->frame_size, 0U, ctx->machine_frame->saved_entry_sp_offset
    };
    if (!out || !ctx || stack_slot > SIZE_MAX / 8U) return false;
    return minic_a64_emit_frame_load(out, &view,
        (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING, stack_slot * 8U},
        (MinicA64FrameRegister){kind, reg},
        (MinicA64ScratchPolicy){0U, 0U});
}

/* Outgoing calls and incoming named parameters share the same ABI plan
 * contracts, but native moves, stack adjustments, floating register
 * assignments and record chunk transport remain ISA-owned. */
bool minic_aarch64_emit_abi_instruction(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, size_t offset,
    bool *needs_gp_store) {
    const MinicCoreModule *module;
    const MinicCoreFunction *fn;
    const MinicLP64FramePlan *frame_plan;
    const MinicLP64ArgumentPlan *incoming_plan;
    const MinicA64FrameView frame_view = {
        ctx ? ctx->machine_frame->has_dynamic_stack : false, ctx ? ctx->machine_frame->frame_size : 0U,
        0U, ctx ? ctx->machine_frame->saved_entry_sp_offset : SIZE_MAX
    };
    if (out == NULL || ctx == NULL || ctx->module == NULL ||
        ctx->target == NULL || ctx->fn == NULL || ctx->frame_plan == NULL ||
        ctx->incoming_plan == NULL || ctx->outgoing_calls == NULL ||
        ins == NULL ||
        needs_gp_store == NULL)
        return false;
    module = ctx->module;
    fn = ctx->fn;
    frame_plan = ctx->frame_plan;
    incoming_plan = ctx->incoming_plan;
    *needs_gp_store = false;
    switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_CALL:
        case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
            const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
            const size_t begin = indirect ? ins->value.indirect_call.argument_begin
                                          : ins->value.call.argument_begin;
            const size_t count = indirect ? ins->value.indirect_call.argument_count
                                          : ins->value.call.argument_count;
            const MinicCoreObjectId result_object = indirect
                ? ins->value.indirect_call.result_object
                : ins->value.call.result_object;
            size_t j;
            size_t stack_bytes;
            const MinicLP64PreparedCall *call_plan;
            MinicA64ScalarShape result_shape;
            size_t call_id;
            MinicA64FrameView call_view = frame_view;
            if (fn->instructions == NULL ||
                ins < fn->instructions ||
                ins >= fn->instructions + fn->instruction_count)
                return false;
            call_id = (size_t)(ins - fn->instructions);
            call_plan = minic_lp64_function_call_plan(
                ctx->outgoing_calls, call_id);
            if (call_plan == NULL || call_plan->argument_count != count ||
                !a64_scalar_shape(fn, ins->core_type_id, &result_shape))
                return false;
            stack_bytes = call_plan->stack_bytes;
            call_view.outgoing_sp_bytes = stack_bytes;
            if (stack_bytes != 0U &&
                fprintf(out, "  sub sp, sp, #%zu\n", stack_bytes) < 0)
                return false;
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                const MinicLP64ArgumentLocation *location = &call_plan->arguments[j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots, object_offset, chunk;
                    const MinicCoreObjectId object_id = arg->value.object_id;
                    if (object_id >= fn->object_count) return false;
                    if (location->kind == MINIC_LP64_ARGUMENT_EMPTY_RECORD) {
                        /* Zero-byte AAPCS64 composite: no argument registers
                         * or stack slots, and no phantom caller-side load. */
                        if (location->size_bytes != 0U ||
                            location->chunks != 0U ||
                            location->gp_registers != 0U ||
                            location->fp_registers != 0U ||
                            location->stack_slots != 0U ||
                            !a64_empty_record(fn,
                                fn->objects[object_id].core_type_id))
                            return false;
                        continue;
                    }
                    if (!minic_lp64_frame_plan_object(frame_plan, object_id, &object_offset))
                        return false;
                    if (!a64_gp_record(fn,
                            fn->objects[object_id].core_type_id,
                            &record_size, &slots)) {
                        if (!a64_indirect_record(fn,
                                fn->objects[object_id].core_type_id, &record_size) ||
                            !minic_a64_emit_owned_address(
                                out, &call_view, object_offset, 9U) ||
                            (location->gp_registers != 0U
                             ? fprintf(out, "  mov x%zu, x9\n", location->gp_first) < 0
                             : !minic_a64_emit_frame_store(out, &call_view,
                                 (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING,
                                     location->stack_first * 8U},
                                 (MinicA64FrameRegister){MINIC_A64_REG_X, 9U},
                                 (MinicA64ScratchPolicy){0U, 0U})))
                            return false;
                        continue;
                    }
                    for (chunk = 0U; chunk < slots; ++chunk) {
                        const bool in_reg = location->gp_registers == slots;
                        const size_t payload = record_size - chunk * 8U >= 8U
                            ? 8U : record_size - chunk * 8U;
                        if (!emit_lp64_record_chunk_argument(
                                out, &call_view, object_offset + chunk * 8U,
                                payload, in_reg, in_reg ? location->gp_first + chunk : 0U,
                                in_reg ? 0U : location->stack_first + chunk))
                            return false;
                    }
                    continue;
                }
                const MinicCoreTypeId core_id =
                    fn->values[arg->value.value_id].core_type_id;
                MinicA64ScalarShape arg_shape;
                const size_t slot = minic_lp64_frame_value_offset(frame_plan, arg->value.value_id);
                if (!a64_scalar_shape(fn, core_id, &arg_shape)) return false;
                if (arg_shape.is_int128) {
                    size_t src, limb;
                    if (location->kind != MINIC_LP64_ARGUMENT_WIDE ||
                        !minic_lp64_frame_plan_wide_slot(frame_plan,
                            arg->value.value_id, &src))
                        return false;
                    for (limb = 0U; limb < 2U; ++limb) {
                        if (location->gp_registers == 2U) {
                            if (!minic_a64_emit_owned_slot(out, &call_view,
                                    false, MINIC_A64_REG_X,
                                    (unsigned int)(location->gp_first + limb),
                                    src + limb * 8U))
                                return false;
                        } else if (location->gp_registers == 0U) {
                            if (!minic_a64_emit_owned_slot(out, &call_view,
                                    false, MINIC_A64_REG_X, 9U,
                                    src + limb * 8U) ||
                                !minic_a64_emit_frame_store(out, &call_view,
                                    (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING,
                                        (location->stack_first + limb) * 8U},
                                    (MinicA64FrameRegister){MINIC_A64_REG_X, 9U},
                                    (MinicA64ScratchPolicy){0U, 0U}))
                                return false;
                        } else {
                            return false;
                        }
                    }
                } else if (arg_shape.is_long_double) {
                    size_t src;
                    if (!minic_lp64_frame_plan_wide_slot(frame_plan, arg->value.value_id, &src))
                        return false;
                    if (location->fp_registers != 0U) {
                        if (!minic_a64_emit_owned_slot(out, &call_view,
                                false, MINIC_A64_REG_Q,
                                (unsigned int)location->fp_first, src))
                            return false;
                    } else {
                        if (!minic_a64_emit_owned_slot(out, &call_view,
                                false, MINIC_A64_REG_Q, 9U, src) ||
                            !minic_a64_emit_frame_store(out, &call_view,
                                (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING,
                                    location->stack_first * 8U},
                                (MinicA64FrameRegister){MINIC_A64_REG_Q, 9U},
                                (MinicA64ScratchPolicy){0U, 0U}))
                            return false;
                    }
                } else if (arg_shape.is_double || arg_shape.is_float) {
                    const bool is_float = arg_shape.is_float;
                    const MinicA64FrameRegisterClass fp_kind = is_float
                        ? MINIC_A64_REG_S : MINIC_A64_REG_D;
                    if (location->fp_registers != 0U) {
                        if (!minic_a64_emit_owned_slot(out, &call_view,
                                false, fp_kind,
                                (unsigned int)location->fp_first, slot))
                            return false;
                    } else {
                        if (!minic_a64_emit_owned_slot(out, &call_view,
                                false, fp_kind, 9U, slot) ||
                            !minic_a64_emit_frame_store(out, &call_view,
                                (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING,
                                    location->stack_first * 8U},
                                (MinicA64FrameRegister){fp_kind, 9U},
                                (MinicA64ScratchPolicy){0U, 0U}))
                            return false;
                    }
                } else {
                    const bool wide = arg_shape.register64;
                    const MinicA64FrameRegisterClass kind = wide
                        ? MINIC_A64_REG_X : MINIC_A64_REG_W;
                    if (location->gp_registers != 0U) {
                        if (!minic_a64_emit_owned_slot(out, &call_view,
                                false, kind,
                                (unsigned int)location->gp_first, slot))
                            return false;
                    } else {
                        if (!minic_a64_emit_owned_slot(out, &call_view,
                                false, kind, 9U, slot) ||
                            !minic_a64_emit_frame_store(out, &call_view,
                                (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING,
                                    location->stack_first * 8U},
                                (MinicA64FrameRegister){kind, 9U},
                                (MinicA64ScratchPolicy){0U, 0U}))
                            return false;
                    }
                }
            }
            if (result_shape.is_record) {
                size_t bytes = 0U, dst;
                /* AAPCS64 keeps x8 independent of x0..x7, so setup after
                 * all normal arguments (including stack spills). A nested
                 * callee writes directly to the caller's Core record object.
                 * The same result-object contract covers indirect calls. */
                if (a64_indirect_record(fn, ins->core_type_id,
                                        &bytes) && bytes > 16U) {
                    if (result_object >= fn->object_count ||
                        !minic_lp64_frame_plan_object(
                            frame_plan, result_object, &dst) ||
                        !minic_a64_emit_owned_address(out, &call_view,
                            dst, 8U))
                        return false;
                }
            }
            if (indirect) {
                if (!minic_a64_emit_owned_slot(out, &call_view,
                        false, MINIC_A64_REG_X, 16U,
                        minic_lp64_frame_value_offset(frame_plan, ins->value.indirect_call.callee)) ||
                    fputs("  blr x16\n", out) == EOF)
                    return false;
            } else {
                const MinicCoreCallee *callee =
                    &fn->callees[ins->value.call.callee_id];
                const char *call_symbol =
                    ins->value.call.extended_float_operation == MINIC_CORE_EXT_FLOAT_NONE
                        ? callee->name
                        : minic_core_extended_float_runtime_symbol(
                              ins->value.call.extended_float_operation);
                if (call_symbol == NULL) return false;
                if (minic_multi_elf_module_symbol_facts(
                        module, call_symbol,
                        MINIC_ELF_MODULE_SYMBOL_FUNCTION).undefined_weak) {
                    /* An undefined weak symbol may resolve to address zero.
                     * Direct CALL26 can force a PLT entry, forbidden by the
                     * Linux AArch64 linker script. Reuse the normal AAPCS64
                     * call-clobbered IP0 register without disturbing x0..x8
                     * argument and indirect-record-return channels. */
                    if (fprintf(out, "  adrp x16, :got:%s\n"
                                     "  ldr x16, [x16, :got_lo12:%s]\n"
                                     "  blr x16\n",
                                call_symbol, call_symbol) < 0)
                        return false;
                } else if (fprintf(out, "  bl %s\n", call_symbol) < 0) {
                    return false;
                }
                /* GNU TF status is a Target ABI detail. Core ordered
                 * equality/less/less-equal always return canonical 0/1;
                 * libgcc uses a positive status for unordered NaNs. */
                switch (ins->value.call.extended_float_operation) {
                case MINIC_CORE_EXT_FLOAT_EQUAL:
                    if (fputs("  cmp w0, #0\n  cset w0, eq\n", out) == EOF) return false;
                    break;
                case MINIC_CORE_EXT_FLOAT_LESS:
                    if (fputs("  cmp w0, #0\n  cset w0, lt\n", out) == EOF) return false;
                    break;
                case MINIC_CORE_EXT_FLOAT_LESS_EQUAL:
                    if (fputs("  cmp w0, #0\n  cset w0, le\n", out) == EOF) return false;
                    break;
                default: break;
                }
            }
            if (stack_bytes != 0U &&
                fprintf(out, "  add sp, sp, #%zu\n", stack_bytes) < 0)
                return false;
            if (result_shape.is_void) return true;
            if (result_shape.is_record) {
                const MinicCoreObjectId object_id = result_object;
                size_t bytes, slots, dst, chunk, fp_elements, fp_width;
                if (object_id >= fn->object_count)
                    return false;
                if (a64_empty_record(fn, ins->core_type_id))
                    return a64_empty_record(fn,
                        fn->objects[object_id].core_type_id);
                if (
                    !minic_lp64_frame_plan_object(frame_plan, object_id, &dst))
                    return false;
                if (a64_indirect_record(fn, ins->core_type_id,
                                        &bytes) && bytes > 16U)
                    return true; /* callee already filled result_object via x8 */
                if (a64_fp_return_record(fn, ins->core_type_id,
                                         &fp_elements, &fp_width)) {
                    for (chunk = 0U; chunk < fp_elements; ++chunk) {
                        const size_t off = dst + chunk * fp_width;
                        if (off > UINT32_MAX ||
                            !minic_a64_emit_frame_store(out, &frame_view,
                                minic_a64_owned_slot(off),
                                (MinicA64FrameRegister){fp_width == 4U
                                        ? MINIC_A64_REG_S : MINIC_A64_REG_D,
                                    (unsigned int)chunk},
                                (MinicA64ScratchPolicy){0U, 0U}))
                            return false;
                    }
                    return true;
                }
                if (!a64_gp_record(fn, ins->core_type_id, &bytes, &slots) ||
                    bytes == 0U || bytes > 16U || slots > 2U)
                    return false;
                for (chunk = 0U; chunk < slots; ++chunk) {
                    const MinicA64FrameView view = {
                        ctx->machine_frame->has_dynamic_stack, ctx->machine_frame->frame_size, 0U,
                        ctx->machine_frame->saved_entry_sp_offset
                    };
                    const size_t payload = bytes - chunk * 8U >= 8U
                        ? 8U : bytes - chunk * 8U;
                    if (!minic_aarch64_emit_lp64_record_chunk_parameter(
                            out, &view, dst + chunk * 8U, payload,
                            true, chunk, 0U))
                        return false;
                }
                return true;
            }
            if (result_shape.is_int128) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    !minic_a64_emit_owned_slot(out, &frame_view, true,
                        MINIC_A64_REG_X, 0U, dst) ||
                    !minic_a64_emit_owned_slot(out, &frame_view, true,
                        MINIC_A64_REG_X, 1U, dst + 8U))
                    return false;
                return true;
            }
            if (result_shape.is_long_double) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    !minic_a64_emit_owned_slot(out, &frame_view, true,
                        MINIC_A64_REG_Q, 0U, dst))
                    return false;
                return true;
            }
            if (fputs(result_shape.is_double
                          ? "  fmov x9, d0\n"
                          : result_shape.is_float
                              ? "  fmov w9, s0\n"
                          : result_shape.register64
                              ? "  mov x9, x0\n"
                              : "  mov w9, w0\n", out) == EOF)
                return false;
            if (result_shape.is_integer) {
                if (result_shape.integer_width != 8U &&
                    !minic_aarch64_emit_integer_normalize(
                        out, result_shape.integer_width, result_shape.integer_signed))
                    return false;
            }
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_PARAMETER: {
            const MinicLP64ArgumentLocation *location = NULL;
            MinicA64ScalarShape parameter_shape;
            if (!minic_multi_direct_scalar_parameter_location(
                    fn, ins, incoming_plan, &location) ||
                !a64_scalar_shape(fn, ins->core_type_id, &parameter_shape))
                return false;
            if (parameter_shape.is_int128) {
                size_t dst, limb;
                if (location->kind != MINIC_LP64_ARGUMENT_WIDE ||
                    !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return false;
                for (limb = 0U; limb < 2U; ++limb) {
                    if (location->gp_registers == 2U) {
                        if (!minic_a64_emit_owned_slot(out, &frame_view, true,
                                MINIC_A64_REG_X,
                                (unsigned int)(location->gp_first + limb),
                                dst + limb * 8U))
                            return false;
                    } else if (location->gp_registers == 0U) {
                        if (!aarch64_emit_incoming_stack_read(out, ctx,
                                MINIC_A64_REG_X, 9U,
                                location->stack_first + limb) ||
                            !minic_a64_emit_owned_slot(out, &frame_view, true,
                                MINIC_A64_REG_X, 9U, dst + limb * 8U))
                            return false;
                    }
                    else if (location->gp_registers != 0U)
                        return false;
                }
                return true;
            }
            if (parameter_shape.is_long_double) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return false;
                if (location->fp_registers != 0U) {
                    if (!minic_a64_emit_owned_slot(out, &frame_view, true,
                            MINIC_A64_REG_Q,
                            (unsigned int)location->fp_first, dst))
                        return false;
                } else {
                    if (!aarch64_emit_incoming_stack_read(out, ctx,
                            MINIC_A64_REG_Q, 9U,
                            location->stack_first) ||
                        !minic_a64_emit_owned_slot(out, &frame_view, true,
                            MINIC_A64_REG_Q, 9U, dst))
                        return false;
                }
                return true;
            }
            if (parameter_shape.is_double || parameter_shape.is_float) {
                const bool is_float = parameter_shape.is_float;
                if (location->fp_registers != 0U) {
                    if (!minic_a64_emit_owned_slot(out, &frame_view, true,
                            is_float ? MINIC_A64_REG_S : MINIC_A64_REG_D,
                            (unsigned int)location->fp_first, offset))
                        return false;
                } else if (!aarch64_emit_incoming_stack_read(out, ctx,
                               is_float ? MINIC_A64_REG_S : MINIC_A64_REG_D,
                               9U,
                               location->stack_first) ||
                           !minic_a64_emit_owned_slot(out, &frame_view, true,
                               is_float ? MINIC_A64_REG_S : MINIC_A64_REG_D,
                               9U, offset))
                    return false;
                return true;
            }
            if (location->gp_registers != 0U) {
                if (!minic_a64_emit_owned_slot(out, &frame_view, true,
                        parameter_shape.register64
                            ? MINIC_A64_REG_X : MINIC_A64_REG_W,
                        (unsigned int)location->gp_first, offset))
                    return false;
            } else {
                const bool wide = parameter_shape.register64;
                if (!aarch64_emit_incoming_stack_read(out, ctx,
                        wide ? MINIC_A64_REG_X : MINIC_A64_REG_W,
                        9U, location->stack_first) ||
                    !minic_a64_emit_owned_slot(out, &frame_view, true,
                        wide ? MINIC_A64_REG_X : MINIC_A64_REG_W,
                        9U, offset))
                    return false;
            }
            return true;
        }
        default:
            return false;
    }
}
