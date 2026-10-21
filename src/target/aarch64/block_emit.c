#include "target/aarch64/block_emit.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"
#include "target/aarch64/abi.h"
#include "target/data_layout.h"
#include "target/common/direct_program_emit.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The instruction-owned aggregate ingress and CFG terminator cases are
 * independent of the function-level preflight and frame lifetime. Machine
 * register syntax remains strictly owned by this ISA. */

bool minic_aarch64_emit_lp64_record_chunk_parameter(FILE *out, size_t offset,
                                              size_t bytes, bool in_reg,
                                              size_t gp, size_t incoming_offset) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U) ||
        fprintf(out, "  movz x11, #%zu\n", offset & 65535U) < 0 ||
        (offset > 65535U &&
         fprintf(out, "  movk x11, #%zu, lsl #16\n", offset >> 16U) < 0) ||
        fputs("  add x11, sp, x11\n", out) == EOF ||
        (in_reg
         ? fprintf(out, "  mov x9, x%zu\n", gp) < 0
         : fprintf(out, "  ldr x9, [sp, #%zu]\n", incoming_offset) < 0))
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  strb w9, [x11, #%zu]\n", b) < 0 ||
            (b + 1U < bytes && fputs("  lsr x9, x9, #8\n", out) == EOF))
            return false;
    return true;
}

bool minic_aarch64_emit_record_copy(FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreInstruction *ins) {
const MinicCoreValueId dst = ins->value.record_copy.destination_address;
            const MinicCoreValueId src = ins->value.record_copy.source_address;
            MinicType dst_type, src_type, dst_unqualified, src_unqualified;
            size_t bytes, alignment;
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                !minic_type_is_record(ins->type) ||
                dst >= fn->value_count || src >= fn->value_count ||
                !minic_type_pointee(fn->values[dst].type, &dst_type) ||
                !minic_type_pointee(fn->values[src].type, &src_type) ||
                !minic_type_unqualified(dst_type, &dst_unqualified) ||
                !minic_type_unqualified(src_type, &src_unqualified) ||
                !minic_type_equal(dst_unqualified, ins->type) ||
                !minic_type_equal(src_unqualified, ins->type) ||
                !minic_data_layout_type(target->data_layout, program, ins->type,
                                        &bytes, &alignment) ||
                alignment == 0U || bytes > 2048U)
                return false;
            if (fprintf(out, "  ldr x10, [sp, #%zu]\n"
                             "  ldr x11, [sp, #%zu]\n",
                        (size_t)dst * 8U, (size_t)src * 8U) < 0)
                return false;
            for (size_t byte = 0U; byte < bytes; ++byte)
                if (fputs("  ldrb w9, [x11]\n"
                          "  strb w9, [x10]\n"
                          "  add x11, x11, #1\n"
                          "  add x10, x10, #1\n", out) == EOF)
                    return false;
            return true;
}

bool minic_aarch64_emit_parameter_object(FILE *out,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming_plan,
    const MinicLP64FramePlan *frame_plan, size_t frame) {
const size_t position = ins->value.parameter_object.parameter_index;
            const MinicCoreObjectId obj_id = ins->value.parameter_object.object_id;
            const MinicLP64ArgumentLocation *location =
                position < incoming_plan.argument_count
                    ? &incoming_plan.arguments[position] : NULL;
            size_t size, slots, object_offset, k, chunk;
            if (location == NULL || ins->result != MINIC_CORE_VALUE_INVALID ||
                obj_id >= fn->object_count ||
                !minic_type_equal(fn->objects[obj_id].type,
                                  fn->parameter_types[position]) ||
                fn->objects[obj_id].element_count != 1U ||
                (location->kind != MINIC_LP64_ARGUMENT_RECORD &&
                 location->kind != MINIC_LP64_ARGUMENT_INDIRECT_RECORD) ||
                !minic_lp64_frame_plan_object(frame_plan, obj_id, &object_offset))
                return false;
            size = location->size_bytes;
            slots = location->kind == MINIC_LP64_ARGUMENT_RECORD
                ? location->chunks : 0U;
            if (location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
                /* Composite by-reference parameter: copy the caller-owned
                 * bytes to this callee's independently addressable object. */
                if ((location->gp_registers != 0U
                     ? fprintf(out, "  mov x10, x%zu\n", location->gp_first) < 0
                     : fprintf(out, "  ldr x10, [sp, #%zu]\n",
                               frame + location->stack_first * 8U) < 0) ||
                    fprintf(out, "  movz x11, #%zu\n",
                            object_offset & 65535U) < 0 ||
                    (object_offset > 65535U &&
                     fprintf(out, "  movk x11, #%zu, lsl #16\n",
                             object_offset >> 16U) < 0) ||
                    fputs("  add x11, sp, x11\n", out) == EOF)
                    return false;
                for (k = 0U; k < size; ++k) {
                    if (fputs("  ldrb w9, [x10], #1\n"
                              "  strb w9, [x11], #1\n", out) == EOF)
                        return false;
                }
                return true;
            }
            for (chunk = 0U; chunk < slots; ++chunk) {
                const bool in_reg = chunk < location->gp_registers;
                const size_t payload = size - chunk * 8U >= 8U
                    ? 8U : size - chunk * 8U;
                if (!minic_aarch64_emit_lp64_record_chunk_parameter(out,
                        object_offset + chunk * 8U, payload,
                        in_reg, in_reg ? location->gp_first + chunk : 0U,
                        in_reg ? 0U : frame +
                                     (location->stack_first + chunk) * 8U))
                    return false;
            }
            return true;
}

bool minic_aarch64_emit_terminator(FILE *out,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreBlock *block,
    size_t id, size_t block_id, const MinicLP64FramePlan *frame_plan,
    size_t frame, size_t save_offset, bool has_direct_calls,
    bool dynamic_frame) {
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (minic_type_is_long_double(fn->return_type)) {
                size_t src;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, block->terminator.return_value, &src) ||
                    fprintf(out, "  ldr q0, [sp, #%zu]\n", src) < 0)
                    return false;
            }
            if ((!minic_type_is_void(fn->return_type) &&
                 !minic_type_is_long_double(fn->return_type) &&
                 fprintf(out, minic_type_is_double(fn->return_type)
                                  ? "  ldr d0, [sp, #%zu]\n"
                                  : minic_type_is_float(fn->return_type)
                                      ? "  ldr s0, [sp, #%zu]\n"
                                  : minic_lp64_register64_type(program, target, fn->return_type)
                                      ? "  ldr x0, [sp, #%zu]\n"
                                      : "  ldr w0, [sp, #%zu]\n",
                         (size_t)block->terminator.return_value * 8U) < 0) ||
                (has_direct_calls && fprintf(out, "  ldr x30, [sp, #%zu]\n",
                                             save_offset) < 0) ||
                (dynamic_frame &&
                 (fputs("  mov sp, x19\n", out) == EOF ||
                  fprintf(out, "  ldr x19, [sp, #%zu]\n",
                          save_offset + (has_direct_calls ? 8U : 0U)) < 0)) ||
                (frame != 0U &&
                 (fprintf(out, "  movz x12, #%zu\n", frame & 65535U) < 0 ||
                  (frame > 65535U &&
                   fprintf(out, "  movk x12, #%zu, lsl #16\n", frame >> 16U) < 0) ||
                  fputs("  add sp, sp, x12\n", out) == EOF)) ||
                fputs("  ret\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            if (fprintf(out, "  ldr x9, [sp, #%zu]\n  br x9\n",
                        (size_t)block->terminator.indirect_target * 8U) < 0)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Preserve Core CFG semantics: trap instead of falling through. */
            if (fputs("  brk #0\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_arm_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
            const MinicCoreValueId cond = block->terminator.conditional.condition;
            const bool wide = minic_lp64_register64_type(program, target, fn->values[cond].type);
            /* Large computed-goto VM CFGs exceed cbnz's branch reach. */
            if (fn->block_count > 256U) {
                if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                     : "  ldr w9, [sp, #%zu]\n",
                            (size_t)cond * 8U) < 0 ||
                    fprintf(out, wide
                            ? "  cbz x9, .Lmini_arm_f%zu_false_%zu\n"
                            : "  cbz w9, .Lmini_arm_f%zu_false_%zu\n",
                            id, block_id) < 0 ||
                    fprintf(out, "  b .Lmini_arm_f%zu_b%u\n"
                                 ".Lmini_arm_f%zu_false_%zu:\n"
                                 "  b .Lmini_arm_f%zu_b%u\n",
                            id, (unsigned int)block->terminator.conditional.when_true,
                            id, block_id,
                            id, (unsigned int)block->terminator.conditional.when_false) < 0)
                    return false;
                break;
            }
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)cond * 8U) < 0 ||
                fprintf(out, wide
                            ? "  cbnz x9, .Lmini_arm_f%zu_b%u\n"
                              "  b .Lmini_arm_f%zu_b%u\n"
                            : "  cbnz w9, .Lmini_arm_f%zu_b%u\n"
                              "  b .Lmini_arm_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.conditional.when_true,
                        id, (unsigned int)block->terminator.conditional.when_false) < 0)
                return false;
            break;
        }
        default: return false;
        }
    return true;
}
