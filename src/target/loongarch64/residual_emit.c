#include "target/loongarch64/residual_emit.h"
#include "target/loongarch64/asm_contract.h"
#include "target/loongarch64/simd_asm.h"
#include "target/loongarch64/scalar_normalize.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/lp64_scalar.h"
#include "target/common/direct_program_emit.h"
#include "target/data_layout.h"
#include "target/loongarch64/frame_emit.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* SP may change in functions containing dynamic alloca. The fixed frame
 * uses the existing callee-saved frame anchor, never raw dynamic SP. */
#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)


/* Residual machine lowering after Core family admission. Each case is
 * target owned; planning and type checks are shared with the other LP64
 * backend, preserving AArch64 and LoongArch64 ABI/ISA divergence. */


bool minic_loongarch64_emit_residual_instruction(
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
    size_t id, va_gp, va_count, reject_block;
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
            if (minic_loongarch64_linux_jump_label_asm_goto_core(source)) {
                if (!minic_loongarch64_emit_linux_jump_label_asm_goto(
                        out, source, id, fn->block_count))
                    return minic_multi_direct_machine_reject(
                        fn, "loongarch64", name, "linux_jump_label_asm_goto",
                        reject_block, reject_ins, __LINE__);
                *needs_writeback = false;
                return true;
            }
            if (minic_loongarch64_simd_qualify_opaque(source)) {
                if (!minic_loongarch64_simd_emit_opaque(out, source))
                    return minic_multi_direct_machine_reject(
                        fn, "loongarch64", name, "qualified_simd_asm",
                        reject_block, reject_ins, __LINE__);
                *needs_writeback = false;
                return true;
            }
            if (!minic_loongarch64_qualified_opaque_asm(source) ||
                fwrite(source->template_text, 1U, source->template_length, out) !=
                    source->template_length ||
                fputc('\n', out) == EOF)
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, "qualified_opaque_hardware_asm",
                    reject_block, reject_ins, __LINE__);
            *needs_writeback = false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
            if (!minic_loongarch64_emit_qualified_structured_asm(out, ctx, ins))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, "qualified_hardware_asm",
                    reject_block, reject_ins, __LINE__);
            *needs_writeback = false;
            return true;
        case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
            /* The SHARED semantic guard validates pointer provenance and
             * forbids unwinding higher frames. The native LP64D machine
             * reads the live entry ra in leaf functions or the ABI-owned
             * saved copy after any CALL. Never return a local stack slot
             * address in place of the real caller's instruction pointer. */
            if (!dynamic_frame &&
                minic_multi_direct_call_frame_level0_shape(
                    fn, ins, MINIC_CORE_CALL_FRAME_ADDRESS_FRAME)) {
                /* LoongArch fixed frame has a stable SP until the epilogue.
                 * Preserve ABI frame alignment; do not invent parent FP. */
                if (fputs("  move $t0, $sp\n", out) == EOF)
                    return false;
                break;
            }
            if (dynamic_frame ||
                !minic_multi_direct_call_frame_return_level0_shape(fn, ins))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, "call_frame_return_address",
                    reject_block, reject_ins, __LINE__);
            if (ctx->has_direct_calls) {
                if (!emit_sp_mem(out, "ld.d", "$t0", ctx->save_offset))
                    return false;
            } else if (fputs("  move $t0, $ra\n", out) == EOF) {
                return false;
            }
            break;
        case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE: {
            const MinicCoreValueId source = ins->value.fixed_register_write.source;
            const MinicCoreFixedRegisterBinding *binding;
            /* Shared semantic verifier proves the ABI binding+SSA. The
             * target requalifies exact LoongArch hardware state, and writes
             * the per-CPU register without masquerading as a C store. */
            if (!minic_multi_direct_fixed_register_write_shape(
                    program, target, fn, ins, &binding) ||
                binding->register_name_length != 4U ||
                memcmp(binding->register_name, "$r21", 4U) != 0 ||
                !minic_type_equal(binding->type, minic_type_unsigned_long()) ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)source * 8U) ||
                fputs("  move $r21, $t0\n", out) == EOF)
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, "fixed_register_write",
                    reject_block, reject_ins, __LINE__);
            *needs_writeback = false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
            const MinicCoreFixedRegisterBinding *binding;
            const char *physical;
            /* Fixed TP/r21 are architectural, independent of a realigned
             * local frame. A live SP read in a dynamic/realigned frame must
             * remain explicitly rejected: its value differs across allocas. */
            if (!minic_multi_direct_fixed_register_read_shape(
                    program, target, fn, ins, &binding) ||
                (binding->register_name_length != 3U &&
                 binding->register_name_length != 4U))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, "fixed_register_read",
                    reject_block, reject_ins, __LINE__);
            if (binding->register_name_length == 3U &&
                memcmp(binding->register_name, "$tp", 3U) == 0)
                physical = "$tp";
            else if (binding->register_name_length == 3U &&
                     memcmp(binding->register_name, "$sp", 3U) == 0)
                physical = "$sp";
            else if (binding->register_name_length == 4U &&
                     memcmp(binding->register_name, "$r21", 4U) == 0)
                physical = "$r21";
            else
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, "fixed_register_read",
                    reject_block, reject_ins, __LINE__);
            if (dynamic_frame && strcmp(physical, "$sp") == 0)
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, "fixed_register_read",
                    reject_block, reject_ins, __LINE__);
            if (fprintf(out, "  move $t0, %s\n", physical) < 0)
                return false;
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
                    fn, "loongarch64", name, reject_stage, reject_block, reject_ins, __LINE__);
            if (width == 8U) {
                if (fprintf(out, "  li.d $t0, %" PRId64 "\n",
                            ins->value.integer_value) < 0) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            } else {
                if (fprintf(out, "  li.w $t0, %" PRId64 "\n",
                            (int64_t)(int32_t)(uint32_t)
                                ins->value.integer_value) < 0 ||
                    ((width == 1U || width == 2U) &&
                     !minic_loongarch64_emit_integer_normalize(out, width, sign))) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            (void)sign;
            break;
        }
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
            const char *symbol;
            /* la.local is a PC-relative, base-independent kernel PIE
             * address for a proven internal definition. All other data
             * references use LoongArch's GOT-based la.global sequence. */
            if (!minic_multi_direct_global_address_symbol(fn, ins, &symbol) ||
                fprintf(out, "  %s $t0, %s\n",
                        minic_target_info_elf_object_is_tu_local(program, symbol)
                            ? "la.local" : "la.global", symbol) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            { *needs_writeback = false; return true; }
        }
        case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
            const char *symbol;
            if (!minic_multi_direct_function_address_symbol(fn, ins, &symbol) ||
                fprintf(out, "  %s $t0, %s\n",
                        minic_target_info_elf_function_is_tu_local(program, symbol)
                            ? "la.local" : "la.global", symbol) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            { *needs_writeback = false; return true; }
        }
        case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS: {
            MinicCoreBlockId destination;
            /* Local label is a function-private PC-relative address, never
             * a global/GOT symbol. Share only Core shape with AArch64. */
            if (!minic_multi_direct_block_address_shape(
                    fn, ins, &destination) ||
                fprintf(out, "  la.local $t0, .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)destination) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            { *needs_writeback = false; return true; }
        }
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
            size_t object_offset;
            if (!minic_multi_direct_object_address_location(
                    fn, ins, frame_plan, &object_offset) ||
                fprintf(out, "  li.d $t0, %zu\n"
                             "  add.d $t0, %s, $t0\n",
                        object_offset, dynamic_frame ? "$s0" : "$sp") < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            { *needs_writeback = false; return true; }
        }

        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
            /* The full Core DataLayout byte offset may exceed signed imm12.
             * li.d + add.d preserves the computed record layout. */
            MinicCoreValueId base;
            size_t bytes;
            if (!minic_multi_direct_field_address_shape(
                    fn, ins, &base, &bytes) ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)base * 8U))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (bytes != 0U) {
                if (bytes <= 2047U) {
                    if (fprintf(out, "  addi.d $t0, $t0, %zu\n", bytes) < 0)
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                } else if (fprintf(out,
                            "  li.d $t1, %zu\n"
                            "  add.d $t0, $t0, $t1\n", bytes) < 0)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
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
                    "MINIC_M11_POINTER_DIAG arch=loongarch64 function=%s block=%zu ins=%u base=%u base_pointer=%d index=%u index_integer=%d width_known=%d index_bits=%u stride=%zu subtract=%d\n",
                    name, block_id, (unsigned int)ins_id,
                    (unsigned int)base,
                    minic_type_is_pointer(fn->values[base].type) ? 1 : 0,
                    (unsigned int)index,
                    minic_type_is_integer(fn->values[index].type) ? 1 : 0,
                    have_bits ? 1 : 0, bits, stride,
                    ins->value.pointer_offset.subtract ? 1 : 0);
            }
            if (!minic_multi_direct_pointer_offset_shape(
                    program, target, fn, ins, (size_t)-1, &iw, &signed_index) ||
                /* Full-width stride literal; keep LoongArch64-only assembly. */
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)base * 8U) ||
                !emit_sp_mem(out, iw == 1U
                                  ? (signed_index ? "ld.b" : "ld.bu")
                                  : iw == 2U
                                      ? (signed_index ? "ld.h" : "ld.hu")
                                      : iw == 8U ? "ld.d"
                                      : (signed_index ? "ld.w" : "ld.wu"),
                             "$t1", (size_t)index * 8U))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (stride != 0U) {
                if (fprintf(out, "  li.d $t2, %zu\n"
                                 "  mul.d $t1, $t1, $t2\n"
                                 "  %s $t0, $t0, $t1\n", stride,
                            ins->value.pointer_offset.subtract ? "sub.d" : "add.d") < 0)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
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
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (pointer_to_integer) {
                /* A C pointer cast to _Bool is 0/1, not its low byte and
                 * not the original 64-bit address. The legacy Common
                 * validator returns 0 for a full 64-bit transfer, newer
                 * callers may explicitly report 8; only _Bool reports 1. */
                if ((src_width != 0U && src_width != 1U && src_width != 8U) ||
                    !emit_sp_mem(out, "ld.d", "$t0", (size_t)source * 8U) ||
                    (src_width == 1U &&
                     fputs("  sltu $t0, $zero, $t0\n", out) == EOF))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (!emit_sp_mem(out, (src_pointer || src_width == 8U)
                                 ? "ld.d" : "ld.w", "$t0",
                             (size_t)source * 8U)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (!src_pointer && src_width != 8U &&
                (!minic_loongarch64_emit_integer_normalize(out, src_width, src_signed) ||
                 (src_signed
                    ? fputs("  slli.w $t0, $t0, 0\n", out) == EOF
                    : fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
            /* LoongArch LP64D va_list is a scalar byte cursor.  The
             * register bank saved at the top of this frame runs directly
             * into the caller's overflow stack slots at entry SP. */
            if (va_count != 1U || !minic_type_is_pointer(ins->type) ||
                ins->result >= fn->value_count ||
                fprintf(out, "  li.d $t0, %zu\n",
                        ctx->va_base + va_gp * 8U) < 0 ||
                fprintf(out, "  add.d $t0, %s, $t0\n",
                        dynamic_frame ? "$s0" : "$sp") < 0)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
            const MinicCoreValueId count = ins->value.operand;
            if (!minic_multi_direct_dynamic_stack_alloc_shape(
                    fn, ins, dynamic_frame) ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)count * 8U) ||
                fprintf(out,
                    "  addi.d $t1, $t0, 15\n"
                    "  sltu $t2, $t1, $t0\n"
                    "  beqz $t2, .Lmini_loong_alloc_ok_%zu_%u\n"
                    "  break 0\n"
                    ".Lmini_loong_alloc_ok_%zu_%u:\n"
                    "  li.d $t2, -16\n"
                    "  and $t1, $t1, $t2\n"
                    "  sub.d $sp, $sp, $t1\n"
                    "  move $t0, $sp\n",
                    id, (unsigned int)ins_id, id,
                    (unsigned int)ins_id) < 0)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
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
                    fn, "loongarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            if (cast.source_wide || cast.target_wide) {
                if (cast.source_wide) {
                    if (!emit_sp_mem(out, "ld.d", "$t0",
                                     cast.source_wide_offset) ||
                        !emit_sp_mem(out, "ld.d", "$t1",
                                     cast.source_wide_offset + 8U))
                        return minic_multi_direct_machine_reject(
                            fn, "loongarch64", name, reject_stage,
                            reject_block, reject_ins, __LINE__);
                } else if (!emit_sp_mem(out,
                                 cast.source_width == 8U ? "ld.d" : "ld.w",
                                 "$t0", (size_t)src * 8U) ||
                           (cast.source_width < 8U &&
                            !minic_loongarch64_emit_integer_normalize(
                                out, cast.source_width, cast.source_signed)) ||
                           (cast.source_width == 4U && !cast.source_signed &&
                            fputs("  bstrpick.d $t0, $t0, 31, 0\n",
                                  out) == EOF) ||
                           fputs(cast.source_signed
                                 ? "  srai.d $t1, $t0, 63\n"
                                 : "  move $t1, $zero\n", out) == EOF)
                    return minic_multi_direct_machine_reject(
                        fn, "loongarch64", name, reject_stage,
                        reject_block, reject_ins, __LINE__);
                if (cast.target_wide) {
                    if (!emit_sp_mem(out, "st.d", "$t0",
                                     cast.target_wide_offset) ||
                        !emit_sp_mem(out, "st.d", "$t1",
                                     cast.target_wide_offset + 8U))
                        return minic_multi_direct_machine_reject(
                            fn, "loongarch64", name, reject_stage,
                            reject_block, reject_ins, __LINE__);
                    { *needs_writeback = false; return true; }
                }
                if (cast.target_boolean) {
                    if (fputs("  or $t0, $t0, $t1\n"
                              "  sltu $t0, $zero, $t0\n", out) == EOF)
                        return minic_multi_direct_machine_reject(
                            fn, "loongarch64", name, reject_stage,
                            reject_block, reject_ins, __LINE__);
                } else if (cast.target_width < 8U &&
                           !minic_loongarch64_emit_integer_normalize(
                               out, cast.target_width, cast.target_signed))
                    return minic_multi_direct_machine_reject(
                        fn, "loongarch64", name, reject_stage,
                        reject_block, reject_ins, __LINE__);
                break;
            }
            if (!emit_sp_mem(out,
                    cast.source_width == 8U ? "ld.d" : "ld.w",
                    "$t0", (size_t)src * 8U))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            if (cast.target_boolean) {
                if ((cast.source_width < 4U &&
                     !minic_loongarch64_emit_integer_normalize(
                         out, cast.source_width, cast.source_signed)) ||
                    fputs("  sltu $t0, $zero, $t0\n", out) == EOF)
                    return minic_multi_direct_machine_reject(
                        fn, "loongarch64", name, reject_stage,
                        reject_block, reject_ins, __LINE__);
            } else if (cast.target_width == 8U) {
                if (cast.source_width != 8U &&
                    (!minic_loongarch64_emit_integer_normalize(
                        out, cast.source_width, cast.source_signed) ||
                     (!cast.source_signed &&
                      fputs("  bstrpick.d $t0, $t0, 31, 0\n",
                            out) == EOF)))
                    return minic_multi_direct_machine_reject(
                        fn, "loongarch64", name, reject_stage,
                        reject_block, reject_ins, __LINE__);
            } else if (!minic_loongarch64_emit_integer_normalize(out, cast.target_width,
                                               cast.target_signed))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            break;
        }
        default:
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                              "MINIC_M8_OPCODE_REJECT arch=loongarch64 function=%s block=%zu instruction=%u kind=%d\n",
                              name, block_id, (unsigned int)ins_id, (int)ins->kind);
            return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
    return true;
}

#undef emit_sp_mem
