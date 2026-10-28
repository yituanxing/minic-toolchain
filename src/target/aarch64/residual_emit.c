#include "target/aarch64/residual_emit.h"
#include "target/aarch64/asm_contract.h"
#include "target/aarch64/scalar_normalize.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/lp64_scalar.h"
#include "target/common/direct_program_emit.h"
#include "target/data_layout.h"
#include "target/aarch64/abi.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static bool aarch64_static_object(const MinicC0Program *program,
                                  const char *symbol) {
    size_t i;
    if (program == NULL || symbol == NULL) return false;
    for (i = 0U; i < program->global_object_count; ++i) {
        const MinicGlobalObject *obj = &program->global_objects[i];
        if (obj->name != NULL && strcmp(obj->name, symbol) == 0 &&
            obj->is_internal && !obj->is_extern &&
            !obj->is_block_scope_extern_only) return true;
    }
    return false;
}

static bool aarch64_static_function(const MinicC0Program *program,
                                    const char *symbol) {
    size_t i;
    if (program == NULL || symbol == NULL) return false;
    for (i = 0U; i < program->function_count; ++i) {
        const MinicFunction *fn = &program->functions[i];
        const char *name = fn->assembler_name != NULL
            ? fn->assembler_name : fn->name;
        if (name != NULL && strcmp(name, symbol) == 0 &&
            fn->is_defined && fn->is_internal) return true;
    }
    return false;
}



/* Residual machine lowering after Core family admission. Each case is
 * target owned; planning and type checks are shared with the other LP64
 * backend, preserving AArch64 and LoongArch64 ABI/ISA divergence. */


bool minic_aarch64_emit_residual_instruction(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    MinicCoreInstructionId ins_id, size_t block_id,
    size_t offset, bool *needs_writeback) {
    const MinicC0Program *program;
    const MinicTargetInfo *target;
    const MinicCoreFunction *fn;
    const MinicLP64FramePlan *frame_plan;
    const MinicCoreInstruction *ins;
    const char *name;
    const char *reject_stage = "machine_emit";
    size_t id, frame, va_base, va_fp, va_gp, va_count, reject_block;
    size_t reject_ins;
    bool dynamic_frame;
    if (out == NULL || ctx == NULL || needs_writeback == NULL || ctx->fn == NULL ||
        ctx->program == NULL || ctx->target == NULL ||
        ctx->frame_plan == NULL || ctx->name == NULL ||
        ins_id >= ctx->fn->instruction_count)
        return false;
    program = ctx->program;
    target = ctx->target;
    fn = ctx->fn;
    frame_plan = ctx->frame_plan;
    ins = &fn->instructions[ins_id];
    name = ctx->name;
    id = ctx->id;
    frame = ctx->frame;
    va_base = ctx->va_base;
    va_fp = ctx->va_fp;
    va_gp = ctx->va_gp;
    va_count = ctx->va_count;
    reject_block = block_id;
    reject_ins = (size_t)ins_id;
    dynamic_frame = ctx->dynamic_frame;
    *needs_writeback = true;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
            const size_t asm_id = ins->value.inline_asm_id;
            const MinicCoreInlineAsm *source = asm_id < fn->inline_asm_count
                ? &fn->inline_asms[asm_id] : NULL;
            if (!minic_aarch64_qualified_opaque_asm(source) ||
                fwrite(source->template_text, 1U, source->template_length, out) !=
                    source->template_length ||
                fputc('\n', out) == EOF)
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, "qualified_opaque_hardware_asm",
                    reject_block, reject_ins, __LINE__);
            *needs_writeback = false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
            *needs_writeback = false;
            return minic_aarch64_emit_qualified_structured_asm(out, ctx, ins);
        case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS: {
            /* Same typed Core builtin as RV32/RV64, but the AAPCS64
             * machine owns x30 and the saved-LR frame offset.  A call inside
             * this function replaces live x30, so non-leaf must fetch the
             * ENTRY return PC from its saved slot. Never counterfeit an
             * unwind of another frame. */
            if (!dynamic_frame &&
                minic_multi_direct_call_frame_level0_shape(
                    fn, ins, MINIC_CORE_CALL_FRAME_ADDRESS_FRAME)) {
                /* The fixed AAPCS64 frame has a stable 16-byte aligned SP.
                 * The result denotes THIS frame, not x29's caller chain. */
                if (fputs("  mov x9, sp\n", out) == EOF)
                    return false;
                break;
            }
            if (dynamic_frame ||
                !minic_multi_direct_call_frame_return_level0_shape(fn, ins) ||
                (ctx->has_direct_calls &&
                 (ctx->save_offset > 32760U ||
                  (ctx->save_offset & 7U) != 0U)))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, "call_frame_return_address",
                    reject_block, reject_ins, __LINE__);
            if (ctx->has_direct_calls) {
                if (fprintf(out, "  ldr x9, [sp, #%zu]\n",
                            ctx->save_offset) < 0)
                    return false;
            } else if (fputs("  mov x9, x30\n", out) == EOF) {
                return false;
            }
            break;
        }
        case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
            const MinicCoreFixedRegisterBinding *binding;
            /* Do NOT virtualize the stack pointer into an ELF global.  The
             * original Linux current_stack_pointer is the LIVE AAPCS64 sp.
             * The Core SSA destination is written by the existing common
             * direct-machine GP result dispatcher, exactly like the x86
             * equivalent. Dynamic-alloca frame relocation is deliberately
             * not qualified by this first machine owner. */
            if (dynamic_frame ||
                !minic_multi_direct_fixed_register_read_shape(
                    program, target, fn, ins, &binding) ||
                binding->register_name_length != 2U ||
                memcmp(binding->register_name, "sp", 2U) != 0 ||
                fputs("  mov x9, sp\n", out) == EOF)
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, "fixed_register_read",
                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT: {
            size_t width = 0U;
            bool sign = false;
            /* Match x86 Core constant semantics: the destination integer
             * type determines narrowing, not the original int64 payload.
             * Lua exercises out-of-range literals in cast/enum contexts. */
            if (!minic_multi_direct_constant_integer_shape(
                    program, target, ins, &width, &sign))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block, reject_ins, __LINE__);
            if (width == 8U) {
                /* Full 64-bit constant: preserve high halves AND sign bits. */
                const uint64_t bits = (uint64_t)ins->value.integer_value;
                unsigned int shift;
                if (fprintf(out, "  movz x9, #%" PRIu64 "\n",
                            bits & UINT64_C(0xffff)) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                for (shift = 16U; shift <= 48U; shift += 16U) {
                    if (((bits >> shift) & UINT64_C(0xffff)) != 0U &&
                        fprintf(out, "  movk x9, #%" PRIu64 ", lsl #%u\n",
                                (bits >> shift) & UINT64_C(0xffff), shift) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                }
            } else {
                uint32_t bits;
                /* Non-integer Core pseudo-bools retain the old 32-bit
                 * conservative gate; real 8/16/32-bit values wrap to width. */
                bits = (uint32_t)(int32_t)ins->value.integer_value;
                if (fprintf(out, "  movz w9, #%" PRIu32 "\n",
                            bits & UINT32_C(0xffff)) < 0 ||
                    ((bits >> 16U) != 0U &&
                     fprintf(out, "  movk w9, #%" PRIu32 ", lsl #16\n",
                             bits >> 16U) < 0)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                if ((width == 1U || width == 2U) &&
                    !minic_aarch64_emit_integer_normalize(out, width, sign))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            }
            (void)sign;
            break;
        }
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
            const char *symbol;
            if (!minic_multi_direct_global_address_symbol(fn, ins, &symbol))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block, reject_ins, __LINE__);
            if (aarch64_static_object(program, symbol)) {
                /* RoundKey and other proven static-TU objects: preserve
                 * direct static-PC-relative addresses. */
                if (fprintf(out, "  adrp x9, %s\n"
                                 "  add x9, x9, :lo12:%s\n",
                            symbol, symbol) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
            } else {
                /* External libc optind and default-visible globals require
                 * GOT relocation in PIE. This also links in static ELF. */
                if (fprintf(out, "  adrp x9, :got:%s\n"
                                 "  ldr x9, [x9, :got_lo12:%s]\n",
                            symbol, symbol) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
            }
            if (fprintf(out, "  str x9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            { *needs_writeback = false; return true; }
        }
        case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
            const char *symbol;
            if (!minic_multi_direct_function_address_symbol(fn, ins, &symbol))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (aarch64_static_function(program, symbol)) {
                if (fprintf(out, "  adrp x9, %s\n"
                                 "  add x9, x9, :lo12:%s\n",
                            symbol, symbol) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
            } else if (fprintf(out, "  adrp x9, :got:%s\n"
                                    "  ldr x9, [x9, :got_lo12:%s]\n",
                               symbol, symbol) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            if (fprintf(out, "  str x9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            { *needs_writeback = false; return true; }
        }
        case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS: {
            MinicCoreBlockId destination;
            /* Verified Core CFG label, materialized PC-relatively. ADRP+ADD
             * avoids ADR's reach limit in large original Linux functions. */
            if (!minic_multi_direct_block_address_shape(
                    fn, ins, &destination) ||
                fprintf(out,
                        "  adrp x9, .Lmini_arm_f%zu_b%u\n"
                        "  add x9, x9, :lo12:.Lmini_arm_f%zu_b%u\n"
                        "  str x9, [sp, #%zu]\n",
                        id, (unsigned int)destination,
                        id, (unsigned int)destination, offset) < 0)
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            { *needs_writeback = false; return true; }
        }
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
            size_t object_offset;
            if (!minic_multi_direct_object_address_location(
                    fn, ins, frame_plan, &object_offset) ||
                /* Large Lua VM stack objects exceed the 16-bit movz
                 * immediate. Keep the exact DataLayout offset and
                 * materialize it in the same reserved x9 scratch. */
                fprintf(out, "  movz x9, #%zu\n",
                        object_offset & 65535U) < 0 ||
                (object_offset > 65535U &&
                 fprintf(out, "  movk x9, #%zu, lsl #16\n",
                         object_offset >> 16U) < 0) ||
                fputs("  add x9, sp, x9\n", out) == EOF ||
                fprintf(out, "  str x9, [sp, #%zu]\n", offset) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            { *needs_writeback = false; return true; }
        }

        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
            /* Core already computed the DataLayout record byte displacement.
             * Large zlib structures exceed the ISA add-immediate range. */
            MinicCoreValueId base;
            size_t bytes;
            if (!minic_multi_direct_field_address_shape(
                    fn, ins, &base, &bytes) ||
                fprintf(out, "  ldr x9, [sp, #%zu]\n",
                        (size_t)base * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (bytes != 0U && bytes <= 4095U) {
                if (fprintf(out, "  add x9, x9, #%zu\n", bytes) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            } else if (bytes > 4095U) {
                const uint64_t imm = (uint64_t)bytes;
                unsigned int shift;
                if (fprintf(out, "  movz x10, #%" PRIu64 "\n",
                            imm & UINT64_C(0xffff)) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
                for (shift = 16U; shift <= 48U; shift += 16U) {
                    if (((imm >> shift) & UINT64_C(0xffff)) != 0U &&
                        fprintf(out, "  movk x10, #%" PRIu64 ", lsl #%u\n",
                                (imm >> shift) & UINT64_C(0xffff), shift) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                                   reject_block, reject_ins, __LINE__);
                }
                if (fputs("  add x9, x9, x10\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            }
            break;
        }
        case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
            const MinicCoreValueId base = ins->value.pointer_offset.base;
            const MinicCoreValueId index = ins->value.pointer_offset.index;
            const size_t stride = ins->value.pointer_offset.element_size;
            size_t iw;
            bool signed_index;
            if (getenv("MINIC_M1_TRACE") != NULL && base < fn->value_count &&
                index < fn->value_count) {
                unsigned int bits = 0U;
                bool have_bits = minic_target_info_integer_width(
                    target, program, fn->values[index].type, &bits);
                (void)fprintf(stderr,
                    "MINIC_M11_POINTER_DIAG arch=aarch64 function=%s block=%zu ins=%u base=%u base_pointer=%d index=%u index_integer=%d width_known=%d index_bits=%u stride=%zu subtract=%d\n",
                    name, block_id, (unsigned int)ins_id,
                    (unsigned int)base,
                    minic_type_is_pointer(fn->values[base].type) ? 1 : 0,
                    (unsigned int)index,
                    minic_type_is_integer(fn->values[index].type) ? 1 : 0,
                    have_bits ? 1 : 0, bits, stride,
                    ins->value.pointer_offset.subtract ? 1 : 0);
            }
            if (!minic_multi_direct_pointer_offset_shape(
                    program, target, fn, ins, 65535U, &iw, &signed_index) ||
                fprintf(out, "  ldr x9, [sp, #%zu]\n", (size_t)base * 8U) < 0 ||
                fprintf(out,
                        iw == 1U ? (signed_index
                                     ? "  ldrsb x10, [sp, #%zu]\n"
                                     : "  ldrb w10, [sp, #%zu]\n")
                        : iw == 2U ? (signed_index
                                     ? "  ldrsh x10, [sp, #%zu]\n"
                                     : "  ldrh w10, [sp, #%zu]\n")
                        : iw == 8U ? "  ldr x10, [sp, #%zu]\n"
                        : (signed_index
                                     ? "  ldrsw x10, [sp, #%zu]\n"
                                     : "  ldr w10, [sp, #%zu]\n"),
                        (size_t)index * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (stride != 0U) {
                if (fprintf(out, "  movz x11, #%zu\n"
                                 "  mul x10, x10, x11\n"
                                 "  %s x9, x9, x10\n", stride,
                            ins->value.pointer_offset.subtract ? "sub" : "add") < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            break;
        }
        case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST: {
            const MinicCoreValueId source = ins->value.operand;
            size_t src_width = 0U;
            bool src_signed = false;
            bool src_pointer = false;
            bool pointer_to_integer = false;
            if (!minic_multi_direct_scalar_bitcast_shape(
                    program, target, fn, ins, &pointer_to_integer,
                    &src_width, &src_signed, &src_pointer))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (pointer_to_integer) {
                if (fprintf(out, "  ldr x9, [sp, #%zu]\n",
                            (size_t)source * 8U) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (fprintf(out, (src_pointer || src_width == 8U)
                          ? "  ldr x9, [sp, #%zu]\n"
                          : "  ldr w9, [sp, #%zu]\n",
                        (size_t)source * 8U) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (!src_pointer && src_width != 8U &&
                (!minic_aarch64_emit_integer_normalize(out, src_width, src_signed) ||
                 fputs(src_signed ? "  sxtw x9, w9\n"
                                  : "  uxtw x9, w9\n", out) == EOF))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR: {
            /* AAPCS64 va_list: {__stack, __gr_top, __vr_top,
             * __gr_offs, __vr_offs}. Read from the correct register bank
             * (GP slots = 8 bytes, FP slots = 16 bytes) or from the
             * incoming overflow stack, then update the mutable cursor. */
            const bool fp = minic_type_is_double(ins->type);
            size_t width = 8U;
            bool sign = false;
            if (ins->result >= fn->value_count ||
                ins->value.operand >= fn->value_count ||
                !minic_type_is_pointer(
                    fn->values[ins->value.operand].type) ||
                (!fp && !minic_type_is_pointer(ins->type) &&
                 !minic_lp64_scalar_integer_width(program, target, ins->type,
                                       &width, &sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (fprintf(out,
                        "  ldr x9, [sp, #%zu]\n"
                        "  ldrsw x10, [x9, #%u]\n"
                        "  cmp w10, #0\n"
                        "  b.ge .Lmini_arm_va_f%zu_r%u_stack\n"
                        "  ldr x11, [x9, #%u]\n"
                        "  add x11, x11, x10\n"
                        "  ldr x13, [x11]\n"
                        "  add w10, w10, #%u\n"
                        "  str w10, [x9, #%u]\n"
                        "  b .Lmini_arm_va_f%zu_r%u_done\n"
                        ".Lmini_arm_va_f%zu_r%u_stack:\n"
                        "  ldr x11, [x9]\n"
                        "  ldr x13, [x11]\n"
                        "  add x11, x11, #8\n"
                        "  str x11, [x9]\n"
                        ".Lmini_arm_va_f%zu_r%u_done:\n"
                        "  mov x9, x13\n",
                        (size_t)ins->value.operand * 8U,
                        fp ? 28U : 24U,
                        id, (unsigned int)ins->result,
                        fp ? 16U : 8U,
                        fp ? 16U : 8U,
                        fp ? 28U : 24U,
                        id, (unsigned int)ins->result,
                        id, (unsigned int)ins->result,
                        id, (unsigned int)ins->result) < 0 ||
                (!fp && !minic_type_is_pointer(ins->type) &&
                 width != 8U && !minic_aarch64_emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
            const size_t stack_top = frame;
            const size_t gr_top = va_base + 96U;
            const size_t vr_top = va_base + 224U;
            if (va_count != 1U || !minic_type_is_pointer(ins->type) ||
                ins->result >= fn->value_count)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            /* Linux AAPCS64 va_list layout:
             * void *__stack, *__gr_top, *__vr_top; int __gr_offs, __vr_offs.
             * The GP and FP save areas are owned by our entry prologue. */
            if (fprintf(out, "  movz x9, #%zu\n", va_base & 65535U) < 0 ||
                (va_base > 65535U &&
                 fprintf(out, "  movk x9, #%zu, lsl #16\n",
                         va_base >> 16U) < 0) ||
                fputs("  add x9, sp, x9\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            if (fprintf(out, "  movz x10, #%zu\n",
                        stack_top & 65535U) < 0 ||
                (stack_top > 65535U &&
                 fprintf(out, "  movk x10, #%zu, lsl #16\n",
                         stack_top >> 16U) < 0) ||
                fputs("  add x10, sp, x10\n  str x10, [x9]\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            if (fprintf(out, "  movz x10, #%zu\n", gr_top & 65535U) < 0 ||
                (gr_top > 65535U &&
                 fprintf(out, "  movk x10, #%zu, lsl #16\n", gr_top >> 16U) < 0) ||
                fputs("  add x10, sp, x10\n  str x10, [x9, #8]\n", out) == EOF ||
                fprintf(out, "  movz x10, #%zu\n", vr_top & 65535U) < 0 ||
                (vr_top > 65535U &&
                 fprintf(out, "  movk x10, #%zu, lsl #16\n", vr_top >> 16U) < 0) ||
                fputs("  add x10, sp, x10\n  str x10, [x9, #16]\n", out) == EOF ||
                fprintf(out, va_gp == 8U
                            ? "  movz w10, #0\n"
                            : "  movn w10, #%zu\n",
                        va_gp == 8U ? 0U : (8U - va_gp) * 8U - 1U) < 0 ||
                fputs("  str w10, [x9, #24]\n", out) == EOF ||
                fprintf(out, va_fp == 8U
                            ? "  movz w10, #0\n"
                            : "  movn w10, #%zu\n",
                        va_fp == 8U ? 0U : (8U - va_fp) * 16U - 1U) < 0 ||
                fputs("  str w10, [x9, #28]\n", out) == EOF ||
                fprintf(out, "  movz x9, #%zu\n", va_base & 65535U) < 0 ||
                (va_base > 65535U &&
                 fprintf(out, "  movk x9, #%zu, lsl #16\n",
                         va_base >> 16U) < 0) ||
                fputs("  add x9, sp, x9\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
            const MinicCoreValueId count = ins->value.operand;
            if (!minic_multi_direct_dynamic_stack_alloc_shape(
                    fn, ins, dynamic_frame) ||
                fprintf(out,
                    "  ldr x9, [sp, #%zu]\n"
                    "  adds x10, x9, #15\n"
                    "  b.cs .Lmini_arm_alloc_overflow_%zu_%u\n"
                    "  and x10, x10, #-16\n"
                    "  sub sp, sp, x10\n"
                    "  mov x9, sp\n"
                    "  b .Lmini_arm_alloc_done_%zu_%u\n"
                    ".Lmini_arm_alloc_overflow_%zu_%u:\n"
                    "  brk #0\n"
                    ".Lmini_arm_alloc_done_%zu_%u:\n",
                    (size_t)count * 8U,
                    id, (unsigned int)ins_id,
                    id, (unsigned int)ins_id,
                    id, (unsigned int)ins_id,
                    id, (unsigned int)ins_id) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
            const MinicCoreValueId src = ins->value.operand;
            MinicDirectIntegerCastPlan cast;
            if (!minic_multi_direct_integer_cast_plan(
                    program, target, fn, ins, frame_plan, &cast))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);

            /* int128 SSA slots have independently owned 16-byte addresses.
             * Cast admission resolves both wide locations once. */
            if (cast.source_wide || cast.target_wide) {
                if (cast.source_wide) {
                    if (fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                     "  ldr x10, [sp, #%zu]\n",
                                cast.source_wide_offset,
                                cast.source_wide_offset + 8U) < 0)
                        return minic_multi_direct_machine_reject(
                            fn, "aarch64", name, reject_stage,
                            reject_block, reject_ins, __LINE__);
                } else if (fprintf(out, cast.source_width == 8U
                                       ? "  ldr x9, [sp, #%zu]\n"
                                       : "  ldr w9, [sp, #%zu]\n",
                                   (size_t)cast.operand * 8U) < 0 ||
                           (cast.source_width < 8U &&
                            (!minic_aarch64_emit_integer_normalize(
                                  out, cast.source_width, cast.source_signed) ||
                             (cast.source_signed &&
                              fputs("  sxtw x9, w9\n", out) == EOF))) ||
                           fputs(cast.source_signed
                                     ? "  asr x10, x9, #63\n"
                                     : "  mov x10, xzr\n", out) == EOF)
                    return minic_multi_direct_machine_reject(
                        fn, "aarch64", name, reject_stage,
                        reject_block, reject_ins, __LINE__);
                if (cast.target_wide) {
                    if (fprintf(out, "  str x9, [sp, #%zu]\n"
                                     "  str x10, [sp, #%zu]\n",
                                cast.target_wide_offset,
                                cast.target_wide_offset + 8U) < 0)
                        return minic_multi_direct_machine_reject(
                            fn, "aarch64", name, reject_stage,
                            reject_block, reject_ins, __LINE__);
                    { *needs_writeback = false; return true; }
                }
                /* A uint128 -> _Bool must inspect BOTH limbs. In
                 * particular 0x80000000000000000000000000000000 is true. */
                if (cast.target_boolean) {
                    if (fputs("  orr x9, x9, x10\n"
                              "  cmp x9, #0\n  cset w9, ne\n", out) == EOF)
                        return minic_multi_direct_machine_reject(
                            fn, "aarch64", name, reject_stage,
                            reject_block, reject_ins, __LINE__);
                } else if (cast.target_width < 8U &&
                           !minic_aarch64_emit_integer_normalize(out,
                               cast.target_width, cast.target_signed))
                    return minic_multi_direct_machine_reject(
                        fn, "aarch64", name, reject_stage,
                        reject_block, reject_ins, __LINE__);
                break;
            }
            if (fprintf(out, cast.source_width == 8U
                                 ? "  ldr x9, [sp, #%zu]\n"
                                 : "  ldr w9, [sp, #%zu]\n",
                        (size_t)src * 8U) < 0)
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            if (cast.target_boolean) {
                /* Normalize source's declared width, then implement C _Bool
                 * truth-value conversion rather than dropping high bits. */
                if ((cast.source_width < 4U &&
                     !minic_aarch64_emit_integer_normalize(
                         out, cast.source_width, cast.source_signed)) ||
                    fputs(cast.source_width == 8U
                          ? "  cmp x9, #0\n  cset w9, ne\n"
                          : "  cmp w9, #0\n  cset w9, ne\n", out) == EOF)
                    return minic_multi_direct_machine_reject(
                        fn, "aarch64", name, reject_stage,
                        reject_block, reject_ins, __LINE__);
            } else if (cast.target_width == 8U) {
                if (cast.source_width != 8U &&
                    (!minic_aarch64_emit_integer_normalize(
                        out, cast.source_width, cast.source_signed) ||
                     (cast.source_signed &&
                      fputs("  sxtw x9, w9\n", out) == EOF)))
                    return minic_multi_direct_machine_reject(
                        fn, "aarch64", name, reject_stage,
                        reject_block, reject_ins, __LINE__);
            } else if (!minic_aarch64_emit_integer_normalize(out, cast.target_width,
                                               cast.target_signed))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            break;
        }
        default:
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                              "MINIC_M8_OPCODE_REJECT arch=aarch64 function=%s block=%zu instruction=%u kind=%d\n",
                              name, block_id, (unsigned int)ins_id, (int)ins->kind);
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
    return true;
}
