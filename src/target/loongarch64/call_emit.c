#include "target/loongarch64/call_emit.h"
#include "target/loongarch64/scalar_normalize.h"
#include "target/loongarch64/block_emit.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_call_plan.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/core_abi_value_shape.h"
#include "target/common/core_call_view.h"
#include "target/loongarch64/frame_emit.h"
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

/* Outgoing locations use raw SP; dynamic frame slots use the s0 base. */
#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

static bool loongarch64_call_type_is(
    const MinicCoreFunction *fn, MinicCoreTypeId id,
    MinicCoreTypeKind kind) {
    return minic_core_abi_kind(fn, id, kind);
}

static bool loongarch64_call_type_wide(
    const MinicCoreFunction *fn, MinicCoreTypeId id) {
    return minic_core_abi_wide_scalar(fn, id);
}

static bool loongarch64_call_float_format(
    const MinicCoreFunction *fn, MinicCoreTypeId id,
    MinicCoreFloatFormat format) {
    return minic_core_abi_float_format(fn, id, format);
}

static bool loongarch64_call_register64(
    const MinicCoreFunction *fn, MinicCoreTypeId id) {
    MinicCoreAbiValueShape value;
    return minic_core_abi_value_shape(fn, id, &value) && value.bytes == 8U &&
           (value.kind == MINIC_CORE_TYPE_POINTER ||
            value.kind == MINIC_CORE_TYPE_INTEGER);
}

static bool loongarch64_call_record_gp(
    const MinicCoreFunction *fn, MinicCoreTypeId id,
    size_t *bytes, size_t *slots) {
    return fn->type_context != NULL &&
           minic_multi_gp_record_core(fn->type_context, id, bytes, slots);
}

static bool loongarch64_call_record_indirect(
    const MinicCoreFunction *fn, MinicCoreTypeId id,
    size_t *bytes) {
    return fn->type_context != NULL &&
           minic_multi_indirect_record_core(fn->type_context, id, bytes);
}

static bool loongarch64_call_record_empty(
    const MinicCoreFunction *fn, MinicCoreTypeId id) {
    return fn->type_context != NULL &&
           minic_multi_empty_record_core(fn->type_context, id);
}

static bool loongarch64_call_record_fp_return(
    const MinicCoreFunction *fn, MinicCoreTypeId id,
    size_t *elements, size_t *width) {
    return fn->type_context != NULL &&
           minic_multi_fp_return_record_core(fn->type_context, id, elements, width);
}

/* LP64D integral aggregate bytes travel in one/two a-registers or
 * eightbyte stack slots. Use byte packing to avoid reading beyond a
 * partially filled final slot, preserving exact Core snapshot bytes. */
static bool emit_lp64_record_chunk_argument(FILE *out, size_t offset,
                                             size_t bytes, bool in_reg,
                                             size_t gp, size_t stack_slot,
                                             bool dynamic_frame) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U) ||
        !minic_loongarch64_emit_frame_address(
            out, "$t2", offset, dynamic_frame) ||
        fputs("  move $t0, $zero\n", out) == EOF)
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

/* Resolve only immutable integer-constant SSA operands. CSR numbers must
 * be known at compilation time; never treat a dynamic GPR as CSR index. */
static bool loongarch_builtin_immediate_value(
    const MinicCoreFunction *fn, MinicCoreValueId value, uint64_t *out) {
    unsigned depth;
    if (fn == NULL || out == NULL || fn->values == NULL ||
        fn->instructions == NULL || value >= fn->value_count)
        return false;
    for (depth = 0U; depth < 5U; ++depth) {
        const MinicCoreInstructionId definition = fn->values[value].definition;
        const MinicCoreInstruction *ins;
        if (definition >= fn->instruction_count)
            return false;
        ins = &fn->instructions[definition];
        if (ins->result != value)
            return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT) {
            *out = (uint64_t)ins->value.integer_value;
            return true;
        }
        if (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION ||
            ins->value.operand >= fn->value_count)
            return false;
        value = ins->value.operand;
    }
    return false;
}

/* Outgoing calls and incoming named parameters share the same ABI plan
 * contracts, but native moves, stack adjustments, floating register
 * assignments and record chunk transport remain ISA-owned. */
bool minic_loongarch64_emit_abi_instruction(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, size_t offset,
    bool *needs_gp_store) {
    const MinicTargetInfo *target;
    const MinicCoreFunction *fn;
    const MinicLP64FramePlan *frame_plan;
    const MinicLP64ArgumentPlan *incoming_plan;
    size_t frame;
    bool dynamic_frame;
    if (out == NULL || ctx == NULL || ctx->module == NULL ||
        ctx->target == NULL || ctx->fn == NULL || ctx->frame_plan == NULL ||
        ctx->incoming_plan == NULL || ctx->outgoing_calls == NULL ||
        ins == NULL ||
        needs_gp_store == NULL)
        return false;
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
            const MinicCoreObjectId result_object = indirect
                ? ins->value.indirect_call.result_object
                : ins->value.call.result_object;
            static const char *const gp_regs[8] = {
                "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"};
            static const char *const fp_regs[8] = {
                "$fa0", "$fa1", "$fa2", "$fa3", "$fa4", "$fa5", "$fa6", "$fa7"};
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
            if (stack_bytes != 0U && !minic_loongarch64_emit_sp_adjust(out, stack_bytes, false))
                return false;
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                const MinicLP64ArgumentLocation *location = &call_plan->arguments[j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots, object_offset, chunk;
                    const MinicCoreObjectId object_id = arg->value.object_id;
                    if (location->kind == MINIC_LP64_ARGUMENT_EMPTY_RECORD) {
                        if (object_id >= fn->object_count ||
                            !loongarch64_call_record_empty(fn,
                                fn->objects[object_id].core_type_id) ||
                            location->size_bytes != 0U ||
                            location->chunks != 0U ||
                            location->gp_registers != 0U ||
                            location->fp_registers != 0U ||
                            location->stack_slots != 0U)
                            return false;
                        continue;
                    }
                    if (!minic_lp64_frame_plan_object(frame_plan, object_id, &object_offset))
                        return false;
                    if (!loongarch64_call_record_gp(fn,
                            fn->objects[object_id].core_type_id,
                            &record_size, &slots)) {
                        const size_t src = object_offset +
                            (dynamic_frame ? 0U : stack_bytes);
                        if (!loongarch64_call_record_indirect(fn,
                                fn->objects[object_id].core_type_id,
                                &record_size) ||
                            !minic_loongarch64_emit_frame_address(
                                out, "$t0", src, dynamic_frame) ||
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
                const MinicCoreTypeId type_id =
                    fn->values[arg->value.value_id].core_type_id;
                const size_t slot = minic_lp64_frame_value_offset(frame_plan, arg->value.value_id) +
                    (dynamic_frame ? 0U : stack_bytes);
                if (loongarch64_call_type_wide(fn, type_id)) {
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
                    if (!emit_sp_mem(out, loongarch64_call_float_format(
                                fn, type_id, MINIC_CORE_FLOAT_BINARY32)
                                           ? "fld.s" : "fld.d",
                                     fp_regs[location->fp_first], slot))
                        return false;
                } else {
                    MinicCoreAbiValueShape value;
                    const bool valid = minic_core_abi_value_shape(fn, type_id, &value);
                    const bool wide = valid && value.bytes == 8U;
                    if (!valid) return false;
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
            if (loongarch64_call_type_is(fn, ins->core_type_id,
                                         MINIC_CORE_TYPE_RECORD)) {
                size_t result_bytes, dst;
                if (loongarch64_call_record_indirect(fn,
                        ins->core_type_id, &result_bytes) &&
                    result_bytes > 16U) {
                    /* The ABI planner reserved $a0 BEFORE placing explicit
                     * parameters in $a1..$a7. Write the hidden destination
                     * after marshaling (which may clobber scratch GARs).
                     * Dynamic alloca addresses use the stable $s0 base. */
                    if (result_object >= fn->object_count ||
                        !minic_lp64_frame_plan_object(
                            frame_plan, result_object, &dst) ||
                        dst > SIZE_MAX - (dynamic_frame ? 0U : stack_bytes) ||
                        fprintf(out, "  li.d $a0, %zu\n"
                                     "  add.d $a0, %s, $a0\n",
                                dst + (dynamic_frame ? 0U : stack_bytes),
                                dynamic_frame ? "$s0" : "$sp") < 0)
                        return false;
                }
            }
            if (indirect) {
                if (!emit_sp_mem(out, "ld.d", "$t2",
                                 minic_lp64_frame_value_offset(frame_plan, ins->value.indirect_call.callee) +
                                 (dynamic_frame ? 0U : stack_bytes)) ||
                    fputs("  jirl $ra, $t2, 0\n", out) == EOF)
                    return false;
            } else {
                const MinicCoreCallee *callee =
                    &fn->callees[ins->value.call.callee_id];
                /* Revalidate the sealed internal Core callee against the
                 * target's machine-form descriptor, independently of the
                 * original GNU source spelling/C parameter type identities.
                 * The ISA descriptor is shared with the Source projection. */
                static const char internal_prefix[] =
                    "__minic_target_builtin_loongarch_";
                if (callee->name != NULL &&
                    strncmp(callee->name, internal_prefix,
                            sizeof(internal_prefix) - 1U) == 0) {
                    MinicCoreCallView builtin_call;
                    const char *mnemonic = NULL;
                    size_t argc = 0U, immediate_index = SIZE_MAX;
                    size_t k;
                    uint64_t imm = 0U;
                    if (!minic_core_call_view_build(fn, ins, &builtin_call) ||
                        !minic_target_info_machine_builtin_form(
                            target, callee->name, strlen(callee->name),
                            &argc, &mnemonic, &immediate_index) ||
                        ins->core_type_id != builtin_call.core_return_type_id ||
                        builtin_call.parameter_count != argc || builtin_call.is_variadic ||
                        (argc != 0U && builtin_call.core_parameter_type_ids == NULL) ||
                        count != argc || count > 3U ||
                        stack_bytes != 0U || mnemonic == NULL)
                        return false;
                    for (k = 0U; k < count; ++k) {
                        const MinicCoreCallArgument *arg =
                            &fn->call_arguments[begin + k];
                        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                            arg->value.value_id >= fn->value_count ||
                            fn->values[arg->value.value_id].core_type_id !=
                                builtin_call.core_parameter_type_ids[k] ||
                            call_plan->arguments[k].gp_first != k ||
                            call_plan->arguments[k].gp_registers != 1U ||
                            call_plan->arguments[k].fp_registers != 0U ||
                            /* Register-only slots have the sentinel, not
                             * stack index zero. Do not reject valid GPR ABI. */
                            call_plan->arguments[k].stack_first != SIZE_MAX)
                            return false;
                    }
                    if (immediate_index != SIZE_MAX) {
                        const MinicCoreCallArgument *immediate_arg =
                            immediate_index < count ?
                                &fn->call_arguments[begin + immediate_index] : NULL;
                        /* The C argument was evaluated and scalar-snapshotted
                         * correctly. Consume the Core source-constant fact
                         * rather than treating the post-snapshot LOAD as
                         * an arbitrary GPR CSR index. Exact CSR range remains
                         * a LoongArch target policy, not a Core restriction. */
                        if (immediate_arg == NULL ||
                            immediate_arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                            !(immediate_arg->has_constant_integer
                              ? (imm = immediate_arg->constant_integer_bits, true)
                              : loongarch_builtin_immediate_value(
                                    fn, immediate_arg->value.value_id, &imm)) ||
                            imm > 16383U)
                            return false;
                    }
                    if (immediate_index != SIZE_MAX) {
                        if (count == 1U) {
                            if (fprintf(out, "  %s $a0, %" PRIu64 "\n",
                                        mnemonic, imm) < 0) return false;
                        } else if (count == 2U) {
                            if (fprintf(out, "  %s $a0, %" PRIu64 "\n",
                                        mnemonic, imm) < 0) return false;
                        } else if (fprintf(out, "  %s $a0, $a1, %" PRIu64 "\n",
                                           mnemonic, imm) < 0) return false;
                    } else if (count == 1U) {
                        if (fprintf(out, "  %s $a0, $a0\n", mnemonic) < 0)
                            return false;
                    } else if (count == 2U &&
                               (strncmp(mnemonic, "crc.", 4U) == 0 ||
                                strncmp(mnemonic, "crcc.", 5U) == 0)) {
                        if (fprintf(out, "  %s $a0, $a0, $a1\n", mnemonic) < 0)
                            return false;
                    } else if (count == 2U) {
                        if (fprintf(out, "  %s $a0, $a1\n", mnemonic) < 0)
                            return false;
                    } else {
                        return false;
                    }
                } else if (fprintf(out, "  bl %s\n", callee->name) < 0) {
                    return false;
                }
            }
            if (stack_bytes != 0U && !minic_loongarch64_emit_sp_adjust(out, stack_bytes, true))
                return false;
            if (loongarch64_call_type_is(fn, ins->core_type_id,
                                         MINIC_CORE_TYPE_VOID)) return true;
            if (loongarch64_call_type_is(fn, ins->core_type_id,
                                         MINIC_CORE_TYPE_RECORD)) {
                const MinicCoreObjectId object_id = result_object;
                size_t bytes, slots, dst, chunk, fp_elements, fp_width;
                if (object_id >= fn->object_count ||
                    !minic_lp64_frame_plan_object(frame_plan, object_id, &dst))
                    return false;
                if (loongarch64_call_record_empty(fn, ins->core_type_id))
                    return true;
                if (loongarch64_call_record_indirect(fn, ins->core_type_id,
                                                     &bytes) && bytes > 16U)
                    return true; /* GNU LP64D callee filled caller's buffer. */
                if (loongarch64_call_record_fp_return(fn, ins->core_type_id,
                                                     &fp_elements, &fp_width)) {
                    for (chunk = 0U; chunk < fp_elements; ++chunk) {
                        const char *reg = chunk == 0U ? "$fa0" : "$fa1";
                        if (!emit_sp_mem(out, fp_width == 4U ? "fst.s" : "fst.d",
                                         reg, dst + chunk * fp_width))
                            return false;
                    }
                    return true;
                }
                if (!loongarch64_call_record_gp(fn, ins->core_type_id,
                                                &bytes, &slots) ||
                    bytes == 0U || bytes > 16U || slots > 2U)
                    return false;
                for (chunk = 0U; chunk < slots; ++chunk) {
                    const size_t payload = bytes - chunk * 8U >= 8U
                        ? 8U : bytes - chunk * 8U;
                    if (!minic_loongarch64_emit_lp64_record_chunk_parameter(out,
                            dst + chunk * 8U, payload, true, chunk, 0U,
                            dynamic_frame, SIZE_MAX))
                        return false;
                }
                return true;
            }
            if (loongarch64_call_type_wide(fn, ins->core_type_id)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    !emit_sp_mem(out, "st.d", "$a0", dst) ||
                    !emit_sp_mem(out, "st.d", "$a1", dst + 8U))
                    return false;
                return true;
            }
            if (fputs(loongarch64_call_float_format(
                         fn, ins->core_type_id,
                         MINIC_CORE_FLOAT_BINARY64)
                          ? "  movfr2gr.d $t0, $fa0\n"
                          : loongarch64_call_float_format(
                                fn, ins->core_type_id,
                                MINIC_CORE_FLOAT_BINARY32)
                              ? "  movfr2gr.s $t0, $fa0\n"
                          : "  or $t0, $a0, $zero\n", out) == EOF)
                return false;
            if (loongarch64_call_type_is(fn, ins->core_type_id,
                                         MINIC_CORE_TYPE_INTEGER)) {
                size_t width;
                bool sign;
                if (!minic_lp64_core_integer_width(fn->type_context,
                              ins->core_type_id, &width, &sign) ||
                    (width != 8U && !minic_loongarch64_emit_integer_normalize(out, width, sign)))
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
            if (loongarch64_call_type_wide(fn, ins->core_type_id)) {
                size_t dst, chunk;
                const size_t used = location->gp_registers;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return false;
                for (chunk = 0U; chunk < 2U; ++chunk) {
                    if (chunk < used) {
                        if (!emit_sp_mem(out, "st.d", gp_regs[location->gp_first + chunk],
                                         dst + chunk * 8U))
                            return false;
                    } else if ((ctx->saved_entry_sp_offset != SIZE_MAX
                                   ? !minic_loongarch64_emit_incoming_stack_mem(
                                         out, "ld.d", "$t0",
                                         (location->stack_first + chunk - used) * 8U,
                                         ctx->saved_entry_sp_offset)
                                   : !emit_sp_mem(out, "ld.d", "$t0",
                                         frame + (location->stack_first + chunk - used) * 8U)) ||
                               !emit_sp_mem(out, "st.d", "$t0",
                                            dst + chunk * 8U))
                        return false;
                }
                return true;
            }
            if ((loongarch64_call_float_format(fn, ins->core_type_id,
                                              MINIC_CORE_FLOAT_BINARY64) ||
                 loongarch64_call_float_format(fn, ins->core_type_id,
                                              MINIC_CORE_FLOAT_BINARY32)) &&
                location->fp_registers != 0U) {
                if (!emit_sp_mem(out, loongarch64_call_float_format(
                                    fn, ins->core_type_id,
                                    MINIC_CORE_FLOAT_BINARY32)
                                      ? "fst.s" : "fst.d",
                                 fp_regs[location->fp_first], offset))
                    return false;
            } else if (location->gp_registers != 0U) {
                if (!emit_sp_mem(out,
                                 loongarch64_call_register64(fn,
                                     ins->core_type_id) ||
                                 loongarch64_call_float_format(fn,
                                     ins->core_type_id, MINIC_CORE_FLOAT_BINARY64)
                                     ? "st.d" : "st.w",
                                 gp_regs[location->gp_first], offset))
                    return false;
                /* The subsequent target-only pass may forward a sole
                 * immediately adjacent use straight from its incoming GAR.
                 * No ABI register is kept live across any machine opcode. */
                if (fprintf(out, "# MINI_LOONG_SSA_PARAMETER %u %zu\n",
                            (unsigned int)ins->result,
                            location->gp_first) < 0)
                    return false;
            } else {
                const char *const load =
                    loongarch64_call_register64(fn, ins->core_type_id) ||
                    loongarch64_call_float_format(fn,
                        ins->core_type_id, MINIC_CORE_FLOAT_BINARY64)
                        ? "ld.d" : "ld.w";
                const char *const store =
                    loongarch64_call_register64(fn, ins->core_type_id) ||
                    loongarch64_call_float_format(fn,
                        ins->core_type_id, MINIC_CORE_FLOAT_BINARY64)
                        ? "st.d" : "st.w";
                if (!(ctx->saved_entry_sp_offset != SIZE_MAX
                          ? minic_loongarch64_emit_incoming_stack_mem(
                                out, load, "$t0",
                                location->stack_first * 8U,
                                ctx->saved_entry_sp_offset)
                          : emit_sp_mem(out, load, "$t0",
                                frame + location->stack_first * 8U)) ||
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
