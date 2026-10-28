#include "target/x86_64/machine_internal.h"
#include "target/common/core_call_view.h"
#include "target/common/core_instruction_family.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/x86_64/abi.h"
#include "target/x86_64/asm_contract.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native x86-64 machine stage, cleanly separated from function/TU owner.
 * Native x87, SysV, integer and memory emitters retain their tested
 * register and instruction sequences. */
bool minic_x86_64_emit_core_instruction(FILE *out, const MinicC0Program *program,
                                 const MinicTargetInfo *target, const MinicCoreFunction *fn,
                                 const MinicX86Frame *frame, const MinicCoreInstruction *ins,
                                 size_t function_id, size_t instruction_id) {
    MinicCoreValueId left, right;
    size_t bytes;
    bool sign;
    if (ins == NULL) return false;
    /* Machine-independent family categorization, native emission per ISA. */
    if (minic_core_instruction_semantic_family(ins->kind) ==
        MINIC_CORE_FAMILY_FLOATING_SCALAR)
        return minic_x86_64_emit_fp_instruction(out, program, target, fn,
                                                frame, ins);
    if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM)
        return minic_x86_64_emit_structured_asm(out, program, target, fn, frame, ins);
    if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
        const size_t asm_id = ins->value.inline_asm_id;
        const MinicCoreInlineAsm *a = fn != NULL &&
            asm_id < fn->inline_asm_count ? &fn->inline_asms[asm_id] : NULL;
        if (a == NULL || a->is_goto || a->register_clobber_count != 0U ||
            !minic_x86_64_linux_bug_table_opaque(
                a->template_text, a->template_length))
            return false;
        return fputs("  ", out) != EOF &&
               fwrite(a->template_text, 1U, a->template_length, out) ==
                   a->template_length &&
               (a->template_length == 0U ||
                a->template_text[a->template_length - 1U] == '\n' ||
                fputc('\n', out) != EOF);
    }
    if (minic_core_instruction_semantic_family(ins->kind) ==
        MINIC_CORE_FAMILY_CALL)
        return minic_x86_64_emit_call_instruction(out, program, target, fn,
                                                  frame, ins, instruction_id);
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS: {
        const MinicCoreCallFrameAddressKind kind =
            ins->value.call_frame_address.kind;
        const MinicCallFrameAddressKind source_kind =
            kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN
                ? MINIC_CALL_FRAME_ADDRESS_RETURN
                : MINIC_CALL_FRAME_ADDRESS_FRAME;
        /* SysV prologue retains caller-PC at 8(rbp) and an invariant RBP
         * frame anchor. No fabricated code pointer or chain unwinding. */
        if (!minic_multi_direct_call_frame_level0_shape(fn, ins, kind) ||
            !minic_target_info_call_frame_address_supported(
                target, source_kind, ins->value.call_frame_address.level) ||
            fprintf(out, kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN
                             ? "  movq 8(%%rbp), %%rax\n"
                             : "  movq %%rbp, %%rax\n") < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        if (minic_type_is_int128_integer(ins->type)) {
            if (fprintf(out, "  movabsq $%" PRId64 ", %%rax\n",
                        ins->value.integer_value) < 0 ||
                fprintf(out, minic_type_is_signed_integer(ins->type)
                                    ? "  movq %%rax, %%rdx\n  sarq $63, %%rdx\n"
                                    : "  xorq %%rdx, %%rdx\n") < 0)
                return false;
            return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        }
        if (!minic_x86_64_scalar_width(program, target, ins->type, &bytes, &sign) ||
            fprintf(out, "  movabsq $%" PRId64 ", %%rax\n", ins->value.integer_value) < 0)
            return false;
        return x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_PARAMETER: {
        static const char *const regs[] = {
            "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
        size_t position = ins->value.parameter_index;
        const MinicX86ArgLocation *location;
        if (position >= frame->incoming_arguments.argument_count ||
            frame->incoming_arguments.locations == NULL)
            return false;
        location = &frame->incoming_arguments.locations[position];
        if (minic_type_is_int128_integer(ins->type)) {
            if (location->kind != MINIC_X86_ARG_I128_TWO_GP)
                return false;
            if (!location->on_stack) {
                if (location->gp_index > 4U ||
                    (frame->saves_incoming_gp
                     ? fprintf(out,
                         "  movq -%zu(%%rbp), %%r10\n"
                         "  movq -%zu(%%rbp), %%r11\n",
                         frame->incoming_gp_save_offset +
                             location->gp_index * 8U,
                         frame->incoming_gp_save_offset +
                             (location->gp_index + 1U) * 8U)
                     : fprintf(out,
                         "  movq %s, %%r10\n"
                         "  movq %s, %%r11\n",
                         regs[location->gp_index],
                         regs[location->gp_index + 1U])) < 0)
                    return false;
            } else if (fprintf(out,
                    "  movq %zu(%%rbp), %%r10\n"
                    "  movq %zu(%%rbp), %%r11\n",
                    16U + location->stack_index * 8U,
                    24U + location->stack_index * 8U) < 0)
                return false;
            return x86_i128_store(out, frame, ins->result, "%r10", "%r11");
        }
        if (minic_type_is_long_double(ins->type)) {
            if (location->kind != MINIC_X86_ARG_X87_MEMORY ||
                fprintf(out, "  fldt %zu(%%rbp)\n",
                        16U + location->stack_index * 8U) < 0)
                return false;
            return x86_x87_store(out, frame, ins->result);
        }
        if (minic_type_is_double(ins->type) || minic_type_is_float(ins->type)) {
            if (location->kind != MINIC_X86_ARG_SSE) return false;
            if (!location->on_stack) {
                if (fprintf(out, minic_type_is_double(ins->type)
                             ? "  movq %%xmm%zu, %%rax\n"
                             : "  movd %%xmm%zu, %%eax\n",
                            location->fp_index) < 0)
                    return false;
            } else if (fprintf(out, "  movq %zu(%%rbp), %%rax\n",
                               16U + location->stack_index * 8U) < 0)
                return false;
        } else {
            if (location->kind != MINIC_X86_ARG_INTEGER) return false;
            if (!location->on_stack) {
                if ((frame->saves_incoming_gp
                     ? fprintf(out, "  movq -%zu(%%rbp), %%rax\n",
                         frame->incoming_gp_save_offset +
                             location->gp_index * 8U)
                     : fprintf(out, "  movq %s, %%rax\n",
                         regs[location->gp_index])) < 0)
                    return false;
            } else if (fprintf(out, "  movq %zu(%%rbp), %%rax\n",
                               16U + location->stack_index * 8U) < 0)
                return false;
            if (!x86_normalize(out, program, target, ins->type)) return false;
        }
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;
        /* One Core binding/type/SSA validator is shared with AArch64.
         * x86 owns only its ABI-specific live RSP move and frame store.
         * Unknown fixed registers remain rejected in TargetInfo. */
        if (!minic_multi_direct_fixed_register_read_shape(
                program, target, fn, ins, &binding) ||
            binding->register_name_length != 3U ||
            memcmp(binding->register_name, "rsp", 3U) != 0)
            return false;
        return fprintf(out, "  movq %%rsp, %%rax\n") >= 0 &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT: {
        static const char *const regs[] = {
            "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
        const size_t position = ins->value.parameter_object.parameter_index;
        const MinicCoreObjectId object = ins->value.parameter_object.object_id;
        const MinicX86ArgLocation *location;
        if (position >= frame->incoming_arguments.argument_count ||
            frame->incoming_arguments.locations == NULL ||
            object >= fn->object_count ||
            !minic_type_equal(fn->objects[object].type,
                              fn->parameter_types[position]))
            return false;
        location = &frame->incoming_arguments.locations[position];
        if (location->kind != MINIC_X86_ARG_RECORD_ONE_GP &&
            location->kind != MINIC_X86_ARG_RECORD_TWO_GP &&
            location->kind != MINIC_X86_ARG_RECORD_MEMORY)
            return false;
        if (!x86_object_address(out, frame, object, "%r10"))
            return false;
        if (location->kind == MINIC_X86_ARG_RECORD_MEMORY) {
            size_t j, source;
            if (!location->on_stack || location->stack_index > SIZE_MAX / 8U ||
                location->stack_index * 8U > (size_t)INT32_MAX - 16U ||
                location->record_bytes > (size_t)INT32_MAX - 16U -
                                          location->stack_index * 8U)
                return false;
            source = 16U + location->stack_index * 8U;
            for (j = 0U; j + 8U <= location->record_bytes; j += 8U)
                if (fprintf(out,
                    "  movq %zu(%%rbp), %%rax\n"
                    "  movq %%rax, %zu(%%r10)\n",
                    source + j, j) < 0) return false;
            for (; j < location->record_bytes; ++j)
                if (fprintf(out,
                    "  movzbq %zu(%%rbp), %%rax\n"
                    "  movb %%al, %zu(%%r10)\n",
                    source + j, j) < 0) return false;
            return true;
        }
        if (location->kind == MINIC_X86_ARG_RECORD_ONE_GP) {
            if (!location->on_stack) {
                if (fprintf(out, "  movq %s, %%rax\n",
                            regs[location->gp_index]) < 0) return false;
            } else if (fprintf(out, "  movq %zu(%%rbp), %%rax\n",
                               16U + location->stack_index * 8U) < 0)
                return false;
            return x86_one_gp_store(out, location->record_bytes);
        }
        if (!location->on_stack)
            return fprintf(out,
                "  movq %s, 0(%%r10)\n"
                "  movq %s, 8(%%r10)\n",
                regs[location->gp_index],
                regs[location->gp_index + 1U]) >= 0;
        return fprintf(out,
            "  movq %zu(%%rbp), %%rax\n"
            "  movq %%rax, 0(%%r10)\n"
            "  movq %zu(%%rbp), %%rax\n"
            "  movq %%rax, 8(%%r10)\n",
            16U + location->stack_index * 8U,
            24U + location->stack_index * 8U) >= 0;
    }
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (ins->value.global_id >= fn->global_count ||
            fn->globals[ins->value.global_id].name == NULL) return false;
        /* A symbol can be an interposable definition from a shared library.
         * LEA symbol(%%rip) is R_X86_64_PC32 and fails in default PIE for
         * e.g. an external libc object; GOTPCREL resolves its runtime address
         * in both PIE and non-PIE. Linkers relax local symbols to LEA. */
        return fprintf(out, "  movq %s@GOTPCREL(%%rip), %%rax\n",
                       fn->globals[ins->value.global_id].name) >= 0 &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        if (ins->value.function_symbol_id >= fn->function_symbol_count ||
            fn->function_symbols[ins->value.function_symbol_id].name == NULL) return false;
        /* Taking the address of an external function must use its GOT slot
         * rather than emitting a non-PIE-compatible PC32 relocation. */
        return fprintf(out, "  movq %s@GOTPCREL(%%rip), %%rax\n",
                       fn->function_symbols[ins->value.function_symbol_id].name) >= 0 &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR: {
        /* SysV __va_list_tag is {u32 gp_offset,u32 fp_offset,
         * void *overflow_arg_area,void *reg_save_area}.
         * GP entries consume 8 bytes (0..48), SSE entries 16 bytes
         * (48..176), and overflow arguments consume eight stack bytes.
         * Only scalar integer, pointer and double are supported here. */
        bool is_fp = minic_type_is_double(ins->type);
        size_t ignored_width;
        bool ignored_signed;
        if (ins->result >= fn->value_count ||
            ins->value.operand >= fn->value_count ||
            !minic_type_is_pointer(fn->values[ins->value.operand].type) ||
            (!is_fp && !minic_x86_64_scalar_width(program, target, ins->type,
                                         &ignored_width, &ignored_signed)))
            return false;
        if (!x86_value_load(out, frame, ins->value.operand, "%r10"))
            return false;
        if (fprintf(out, "  movl %u(%%r10), %%ecx\n"
                         "  cmpl $%u, %%ecx\n"
                         "  jae .Lmini_x86_va_%zu_%u_stack\n"
                         "  movq 16(%%r10), %%r11\n"
                         "  movq (%%r11,%%rcx), %%rax\n"
                         "  addl $%u, %u(%%r10)\n"
                         "  jmp .Lmini_x86_va_%zu_%u_done\n"
                         ".Lmini_x86_va_%zu_%u_stack:\n"
                         "  movq 8(%%r10), %%r11\n"
                         "  movq (%%r11), %%rax\n"
                         "  addq $8, %%r11\n"
                         "  movq %%r11, 8(%%r10)\n"
                         ".Lmini_x86_va_%zu_%u_done:\n",
                    is_fp ? 4U : 0U, is_fp ? 176U : 48U,
                    function_id, (unsigned)ins->result,
                    is_fp ? 16U : 8U, is_fp ? 4U : 0U,
                    function_id, (unsigned)ins->result,
                    function_id, (unsigned)ins->result,
                    function_id, (unsigned)ins->result) < 0)
            return false;
        if (!is_fp && !x86_normalize(out, program, target, ins->type))
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY: {
        /* 24-byte SysV __va_list_tag = two u32 offsets plus two pointers.
         * Copy three aligned eightbytes into the private tag allocated by
         * VARIADIC_ARGUMENT_ADDRESS. Do not alias the source cursor. */
        if (!x86_value_load(out, frame,
                            ins->value.variadic_copy.destination, "%r10") ||
            !x86_value_load(out, frame,
                            ins->value.variadic_copy.source, "%r11"))
            return false;
        return fprintf(out,
                "  movq 0(%%r11), %%rax\n"
                "  movq %%rax, 0(%%r10)\n"
                "  movq 8(%%r11), %%rax\n"
                "  movq %%rax, 8(%%r10)\n"
                "  movq 16(%%r11), %%rax\n"
                "  movq %%rax, 16(%%r10)\n") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
        size_t i;
        size_t tag_offset = 0U;
        if (frame->varargs_count == 0U || ins->result >= fn->value_count ||
            !minic_type_is_pointer(ins->type)) return false;
        for (i = 0U; i < frame->varargs_count; ++i) {
            if (frame->varargs_results[i] == ins->result) {
                tag_offset = frame->varargs_tag_base_offset + (i + 1U) * 24U;
                break;
            }
        }
        if (i == frame->varargs_count) return false;
        /* %rax is a pointer to the 24-byte x86 SysV __va_list_tag.
         * Native libc's vsnprintf reads and updates exactly this layout. */
        if (fprintf(out,
                    "  leaq -%zu(%%rbp), %%rax\n"
                    "  movl $%u, 0(%%rax)\n"
                    "  movl $%u, 4(%%rax)\n"
                    "  leaq %zu(%%rbp), %%r10\n"
                    "  movq %%r10, 8(%%rax)\n"
                    "  leaq -%zu(%%rbp), %%r10\n"
                    "  movq %%r10, 16(%%rax)\n",
                    tag_offset, frame->varargs_gp_offset, frame->varargs_fp_offset,
                    16U + frame->varargs_fixed_stack_slots * 8U,
                    frame->varargs_regsave_offset) < 0) return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (ins->value.object_id >= frame->object_count) return false;
        if (frame->objects_from_aligned_rsp) {
            if (fprintf(out, "  leaq %zu(%%rsp), %%rax\n",
                        frame->object_offsets[ins->value.object_id]) < 0) return false;
        } else if (fprintf(out, "  leaq -%zu(%%rbp), %%rax\n",
                           frame->object_offsets[ins->value.object_id]) < 0) {
            return false;
        }
        return x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        /* Byte-preserving copy between already-evaluated addresses.
         * Do not assume field layout or alignment; match RV64 semantics. */
        MinicType dst_pointee, src_pointee, dst_unqualified, src_unqualified;
        size_t record_size, record_alignment, copied;
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_record(ins->type) ||
            ins->value.record_copy.destination_address >= fn->value_count ||
            ins->value.record_copy.source_address >= fn->value_count ||
            !minic_type_pointee(fn->values[ins->value.record_copy.destination_address].type,
                                &dst_pointee) ||
            !minic_type_pointee(fn->values[ins->value.record_copy.source_address].type,
                                &src_pointee) ||
            !minic_type_unqualified(dst_pointee, &dst_unqualified) ||
            !minic_type_unqualified(src_pointee, &src_unqualified) ||
            !minic_type_equal(dst_unqualified, ins->type) ||
            !minic_type_equal(src_unqualified, ins->type) ||
            !minic_data_layout_type(target->data_layout, program, ins->type,
                                    &record_size, &record_alignment) ||
            record_alignment == 0U ||
            !x86_value_load(out, frame, ins->value.record_copy.destination_address, "%r10") ||
            !x86_value_load(out, frame, ins->value.record_copy.source_address, "%r11"))
            return false;
        /* GNU zero-size records have no bytes to copy. */
        copied = 0U;
        while (copied < record_size) {
            size_t chunk = record_size - copied;
            size_t offset;
            if (chunk > 2048U) chunk = 2048U;
            for (offset = 0U; offset < chunk; ++offset) {
                if (fprintf(out,
                            "  movzbq %zu(%%r11), %%rax\n"
                            "  movb %%al, %zu(%%r10)\n",
                            offset, offset) < 0)
                    return false;
            }
            copied += chunk;
            if (copied < record_size &&
                fprintf(out, "  addq $%zu, %%r10\n  addq $%zu, %%r11\n",
                        chunk, chunk) < 0)
                return false;
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
        MinicCoreValueId source = ins->value.operand;
        MinicType source_type;
        if (source >= fn->value_count) return false;
        source_type = fn->values[source].type;
        if (minic_type_is_int128_integer(ins->type)) {
            if (minic_type_is_int128_integer(source_type)) {
                if (!x86_i128_load(out, frame, source, "%rax", "%rdx"))
                    return false;
            } else {
                size_t source_bytes;
                bool signed_source;
                if (!minic_x86_64_scalar_width(program, target, source_type,
                                      &source_bytes, &signed_source) ||
                    !x86_value_load(out, frame, source, "%rax") ||
                    fprintf(out, signed_source
                                ? "  movq %%rax, %%rdx\n  sarq $63, %%rdx\n"
                                : "  xorq %%rdx, %%rdx\n") < 0)
                    return false;
            }
            return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        }
        if (minic_type_is_int128_integer(source_type))
            return x86_i128_load(out, frame, source, "%rax", "%rdx") &&
                   x86_normalize(out, program, target, ins->type) &&
                   x86_value_store(out, frame, ins->result);
        return x86_value_load(out, frame, source, "%rax") &&
               x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST: {
        const MinicCoreValueId operand = ins->value.operand;
        size_t src_bytes;
        bool src_sign;
        if (operand >= fn->value_count ||
            !minic_core_scalar_bitcast_types_valid(ins->type, fn->values[operand].type) ||
            !minic_x86_64_scalar_width(program, target, fn->values[operand].type,
                              &src_bytes, &src_sign) ||
            !minic_x86_64_scalar_width(program, target, ins->type, &bytes, &sign)) return false;
        /* Core bitcast models pointer/integer conversions, not only same-width
         * reinterpretation. Source values are already canonical in 64-bit
         * Core slots, and the destination normalization applies truncation. */
        (void)src_bytes;
        (void)src_sign;
        return x86_value_load(out, frame, operand, "%rax") &&
               x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_LOAD:
        if (minic_type_is_long_double(ins->type))
            return x86_value_load(out, frame, ins->value.load.address, "%r10") &&
                   fprintf(out, "  fldt (%%r10)\n") >= 0 &&
                   x86_x87_store(out, frame, ins->result);
        if (minic_type_is_int128_integer(ins->type))
            return x86_value_load(out, frame, ins->value.load.address, "%r10") &&
                   fprintf(out, "  movq (%%r10), %%rax\n"
                                "  movq 8(%%r10), %%rdx\n") >= 0 &&
                   x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        if (!minic_x86_64_storage_width(program, target, ins->type, &bytes, &sign)) return false;
        return x86_value_load(out, frame, ins->value.load.address, "%r10") &&
               x86_load_memory(out, bytes, sign) &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD: {
        const MinicCoreValueId addr = ins->value.atomic_load.address;
        MinicType pointee;
        if (ins->value.atomic_load.memory_order != 0U ||
            addr >= fn->value_count ||
            !minic_type_pointee(fn->values[addr].type, &pointee) ||
            !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
            !minic_x86_64_storage_width(program, target, pointee, &bytes, &sign))
            return false;
        /* Aligned scalar x86 MOV has atomicity at supported widths. */
        return x86_value_load(out, frame, addr, "%r10") &&
               x86_load_memory(out, bytes, sign) &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE: {
        const MinicCoreValueId src = ins->value.atomic_store.stored_value;
        const MinicCoreValueId addr = ins->value.atomic_store.address;
        MinicType pointee;
        if (ins->value.atomic_store.memory_order != 0U ||
            src >= fn->value_count || addr >= fn->value_count ||
            !minic_type_pointee(fn->values[addr].type, &pointee) ||
            !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
            !minic_x86_64_storage_width(program, target, pointee, &bytes, &sign))
            return false;
        /* Naturally aligned 1/2/4/8-byte scalar MOV writes are atomic on
         * x86-64. Only relaxed semantics are implemented: sequentially
         * consistent operations require stronger target instructions. */
        return x86_value_load(out, frame, addr, "%r10") &&
               x86_value_load(out, frame, src, "%rax") &&
               x86_store_memory(out, bytes);
    }
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreValueId src = ins->value.store.stored_value;
        if (src >= fn->value_count) return false;
        if (minic_type_is_long_double(fn->values[src].type))
            return x86_value_load(out, frame, ins->value.store.address, "%r10") &&
                   x86_x87_load(out, frame, src) &&
                   fprintf(out, "  fstpt (%%r10)\n") >= 0;
        if (minic_type_is_int128_integer(fn->values[src].type))
            return x86_value_load(out, frame, ins->value.store.address, "%r10") &&
                   x86_i128_load(out, frame, src, "%rax", "%rdx") &&
                   fprintf(out, "  movq %%rax, (%%r10)\n"
                                "  movq %%rdx, 8(%%r10)\n") >= 0;
        if (src >= fn->value_count ||
            !minic_x86_64_storage_width(program, target, fn->values[src].type, &bytes, &sign))
            return false;
        return x86_value_load(out, frame, ins->value.store.address, "%r10") &&
               x86_value_load(out, frame, src, "%rax") &&
               x86_store_memory(out, bytes);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicCoreValueId lhs = ins->value.integer_overflow.left;
        MinicCoreValueId rhs = ins->value.integer_overflow.right;
        MinicCoreValueId address = ins->value.integer_overflow.result_address;
        size_t width;
        bool signed_result;
        const char *opcode = NULL, *flag = NULL;
        if (!minic_core_integer_overflow_shape(
                program, target, fn, ins, &width, &signed_result))
            return false;
        switch (ins->value.integer_overflow.operator_kind) {
        case MINIC_CORE_INTEGER_OVERFLOW_ADD:
            opcode = width == 8U ? "addq" : "addl";
            flag = signed_result ? "seto" : "setc";
            break;
        case MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT:
            opcode = width == 8U ? "subq" : "subl";
            flag = signed_result ? "seto" : "setc";
            break;
        case MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY:
            if (signed_result) opcode = width == 8U ? "imulq" : "imull";
            else opcode = width == 8U ? "mulq" : "mull";
            flag = "seto"; /* MUL/IMUL set OF iff the truncated result loses bits. */
            break;
        default:
            return false;
        }
        if (!x86_value_load(out, frame, lhs, "%rax") ||
            !x86_value_load(out, frame, rhs, "%r10"))
            return false;
        if (ins->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY &&
            !signed_result) {
            if (fprintf(out, width == 8U
                            ? "  mulq %%r10\n" : "  mull %%r10d\n") < 0)
                return false;
        } else if (fprintf(out,
                           width == 8U ? "  %s %%r10, %%rax\n"
                                       : "  %s %%r10d, %%eax\n", opcode) < 0)
            return false;
        /* Snapshot the arithmetic flag before any later arithmetic.
         * Two-address result memory is written before returning the bool. */
        if (fprintf(out, "  %s %%r11b\n", flag) < 0 ||
            !x86_value_load(out, frame, address, "%r10") ||
            fprintf(out, width == 8U
                           ? "  movq %%rax, (%%r10)\n"
                           : "  movl %%eax, (%%r10)\n") < 0 ||
            fprintf(out, "  movzbq %%r11b, %%rax\n") < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
        const MinicCoreValueId operand = ins->value.operand;
        unsigned int width;
        /* Baseline x86-64 BSR/BSF avoid BMI1/LZCNT feature assumptions.
         * The Core operand is the promoted unsigned integer type, not
         * necessarily 64 bits: clz(unsigned int) must return 31 for 1. */
        if (!minic_core_integer_bitscan_shape(
                program, target, fn, ins, &width) ||
            !x86_value_load(out, frame, operand, "%rax"))
            return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ) {
            if (fprintf(out,
                "  testq %%rax, %%rax\n"
                "  jz 1f\n"
                "  bsrq %%rax, %%r10\n"
                "  movl $%u, %%eax\n"
                "  subl %%r10d, %%eax\n"
                "  jmp 2f\n"
                "1:\n"
                "  movl $%u, %%eax\n"
                "2:\n", width - 1U, width) < 0) return false;
        } else if (fprintf(out,
                "  testq %%rax, %%rax\n"
                "  jz 1f\n"
                "  bsfq %%rax, %%rax\n"
                "  jmp 2f\n"
                "1:\n"
                "  movl $%u, %%eax\n"
                "2:\n", width) < 0) return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (ins->value.operand >= fn->value_count) return false;
        if (minic_type_is_int128_integer(fn->values[ins->value.operand].type)) {
            if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO) return false;
            if (!x86_i128_load(out, frame, ins->value.operand, "%rax", "%rdx"))
                return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE) {
                if (fprintf(out, "  negq %%rax\n  adcq $0, %%rdx\n  negq %%rdx\n") < 0)
                    return false;
            } else if (fprintf(out, "  notq %%rax\n  notq %%rdx\n") < 0)
                return false;
            return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        }
        if (!x86_value_load(out, frame, ins->value.operand, "%rax")) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE) {
            if (fprintf(out, "  negq %%rax\n") < 0) return false;
        } else if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT) {
            if (fprintf(out, "  notq %%rax\n") < 0) return false;
        } else if (fprintf(out, "  testq %%rax, %%rax\n  sete %%al\n  movzbq %%al, %%rax\n") < 0)
            return false;
        return x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        left = ins->value.binary.left;
        right = ins->value.binary.right;
        if (left >= fn->value_count || right >= fn->value_count)
            return false;
        if (minic_type_is_int128_integer(fn->values[left].type)) {
            /* 128-bit x86 registers are pairs of 64-bit machine registers.
             * Only explicitly handled operations are admitted. */
            if (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_ADD &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_LESS &&
                ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_EQUAL)
                return false;
            if (!x86_i128_load(out, frame, left, "%rax", "%rdx"))
                return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
                ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) {
                if (minic_type_is_int128_integer(fn->values[right].type) ||
                    !x86_value_load(out, frame, right, "%rcx"))
                    return false;
                /* Shift by >=128 is undefined C behavior. The >=64 limb case
                 * is needed by SQLite's high-64 and high-32 extraction. */
                if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) {
                    bool signed_value = minic_type_is_signed_integer(ins->type);
                    if (fprintf(out,
                        "  cmpq $64, %%rcx\n"
                        "  jae 1f\n"
                        "  shrdq %%cl, %%rdx, %%rax\n"
                        "  %s %%cl, %%rdx\n"
                        "  jmp 2f\n"
                        "1:\n"
                        "  subq $64, %%rcx\n"
                        "  movq %%rdx, %%rax\n"
                        "  %s %%cl, %%rax\n"
                        "  %s\n"
                        "2:\n",
                        signed_value ? "sarq" : "shrq",
                        signed_value ? "sarq" : "shrq",
                        signed_value ? "  sarq $63, %rdx" : "  xorq %rdx, %rdx") < 0)
                        return false;
                } else if (fprintf(out,
                        "  cmpq $64, %%rcx\n"
                        "  jae 1f\n"
                        "  shldq %%cl, %%rax, %%rdx\n"
                        "  shlq %%cl, %%rax\n"
                        "  jmp 2f\n"
                        "1:\n"
                        "  subq $64, %%rcx\n"
                        "  movq %%rax, %%rdx\n"
                        "  shlq %%cl, %%rdx\n"
                        "  xorq %%rax, %%rax\n"
                        "2:\n") < 0)
                    return false;
                return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
            }
            if (!minic_type_is_int128_integer(fn->values[right].type) ||
                !x86_i128_load(out, frame, right, "%r10", "%r11"))
                return false;
            switch (ins->kind) {
            case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
                if (fprintf(out, "  addq %%r10, %%rax\n  adcq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
                if (fprintf(out, "  subq %%r10, %%rax\n  sbbq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
                if (fprintf(out,
                    "  movq %%rdx, %%r8\n"
                    "  imulq %%r10, %%r8\n"
                    "  movq %%r11, %%r9\n"
                    "  imulq %%rax, %%r9\n"
                    "  mulq %%r10\n"
                    "  addq %%r8, %%rdx\n"
                    "  addq %%r9, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
                if (fprintf(out, "  andq %%r10, %%rax\n  andq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
                if (fprintf(out, "  orq %%r10, %%rax\n  orq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
                if (fprintf(out, "  xorq %%r10, %%rax\n  xorq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
                /* Compare high words with source signedness; when high
                 * words match, compare the low words unsigned. Do the low
                 * comparison first to avoid clobbering the input with setcc. */
                if (fprintf(out,
                    "  cmpq %%r10, %%rax\n"
                    "  setb %%cl\n"
                    "  cmpq %%r11, %%rdx\n"
                    "%s"
                    "  sete %%dl\n"
                    "  andb %%cl, %%dl\n"
                    "  orb %%dl, %%al\n"
                    "  movzbq %%al, %%rax\n",
                    minic_type_is_signed_integer(fn->values[left].type)
                      ? "  setl %al\n" : "  setb %al\n") < 0)
                    return false;
                return x86_value_store(out, frame, ins->result);
            case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
                if (fprintf(out,
                    "  cmpq %%r10, %%rax\n"
                    "  sete %%cl\n"
                    "  cmpq %%r11, %%rdx\n"
                    "  sete %%al\n"
                    "  andb %%cl, %%al\n"
                    "  movzbq %%al, %%rax\n") < 0)
                    return false;
                return x86_value_store(out, frame, ins->result);
            default: return false;
            }
            return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        }
        if (!x86_value_load(out, frame, left, "%rax") ||
            !x86_value_load(out, frame, right, "%r10")) return false;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
            if (fprintf(out, "  addq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
            if (fprintf(out, "  subq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
            if (fprintf(out, "  imulq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
            if (!minic_x86_64_scalar_width(program, target, fn->values[left].type, &bytes, &sign) ||
                fprintf(out, sign ? "  cqto\n  idivq %%r10\n" :
                                    "  xorq %%rdx, %%rdx\n  divq %%r10\n") < 0)
                return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER &&
                fprintf(out, "  movq %%rdx, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
            if (fprintf(out, "  andq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
            if (fprintf(out, "  orq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
            if (fprintf(out, "  xorq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
            if (fprintf(out, "  movb %%r10b, %%cl\n  shlq %%cl, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
            if (!minic_x86_64_scalar_width(program, target, fn->values[left].type, &bytes, &sign) ||
                fprintf(out, sign ? "  movb %%r10b, %%cl\n  sarq %%cl, %%rax\n"
                                  : "  movb %%r10b, %%cl\n  shrq %%cl, %%rax\n") < 0)
                return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
            if (!minic_x86_64_scalar_width(program, target, fn->values[left].type, &bytes, &sign) ||
                fprintf(out, "  cmpq %%r10, %%rax\n") < 0) return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL) {
                if (fprintf(out, "  sete %%al\n") < 0) return false;
            } else if (ins->kind == MINIC_CORE_INSTRUCTION_POINTER_LESS || !sign) {
                if (fprintf(out, "  setb %%al\n") < 0) return false;
            } else if (fprintf(out, "  setl %%al\n") < 0) return false;
            if (fprintf(out, "  movzbq %%al, %%rax\n") < 0) return false;
            break;
        default: return false;
        }
        return x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return x86_value_load(out, frame, ins->value.field_address.base, "%rax") &&
               fprintf(out, "  addq $%zu, %%rax\n", ins->value.field_address.byte_offset) >= 0 &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        if (!x86_value_load(out, frame, ins->value.pointer_offset.base, "%rax") ||
            !x86_value_load(out, frame, ins->value.pointer_offset.index, "%r10") ||
            fprintf(out, "  imulq $%zu, %%r10, %%r10\n",
                    ins->value.pointer_offset.element_size) < 0 ||
            fprintf(out, ins->value.pointer_offset.subtract
                         ? "  subq %%r10, %%rax\n" : "  addq %%r10, %%rax\n") < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
        MinicCoreValueId count = ins->value.operand;
        /* The current dynamically aligned object frame is rsp-relative.
         * Alloca changes rsp, so reject this unqualified combination
         * rather than silently addressing the wrong local object. */
        if (frame->objects_from_aligned_rsp ||
            ins->result >= fn->value_count || count >= fn->value_count ||
            !minic_type_is_pointer(ins->type) ||
            !minic_type_equal(fn->values[ins->result].type, ins->type) ||
            !minic_type_equal(fn->values[count].type, minic_type_unsigned_long()) ||
            !x86_value_load(out, frame, count, "%r10") ||
            fprintf(out,
                    "  addq $15, %%r10\n"
                    "  jnc 1f\n"
                    "  ud2\n"
                    "1:\n"
                    "  andq $-16, %%r10\n"
                    "  subq %%r10, %%rsp\n"
                    "  movq %%rsp, %%rax\n") < 0)
            return false;
        /* New rsp stays 16-byte aligned, so subsequent SysV calls may
         * reserve/reclaim outgoing argument space normally. Epilogue
         * uses leave to reclaim every dynamic allocation at once. */
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    default:
        return false;
    }
}

bool minic_x86_64_emit_core_terminator(FILE *out,
                                const MinicC0Program *program,
                                const MinicTargetInfo *target,
                                const MinicX86Frame *frame,
                                const MinicCoreTerminator *term,
                                const MinicCoreFunction *fn, size_t function_id) {
    if (term == NULL) return false;
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (term->return_object != MINIC_CORE_OBJECT_INVALID) {
            size_t bytes;
            if (!program || !target || !frame || !fn ||
                term->return_value != MINIC_CORE_VALUE_INVALID ||
                term->return_object >= fn->object_count ||
                !minic_type_is_record(fn->return_type) ||
                !minic_type_equal(fn->return_type,
                    fn->objects[term->return_object].type) ||
                !x86_object_address(out, frame, term->return_object, "%r11"))
                return false;
            if (minic_x86_64_one_gp_record(program, target, fn->return_type, &bytes)) {
                if (!x86_one_gp_load(out,bytes)) return false;
            } else if (minic_x86_64_two_gp_record(program, target, fn->return_type)) {
                if (fputs("  movq 0(%r11), %rax\n"
                          "  movq 8(%r11), %rdx\n",out)==EOF)return false;
            } else {
                /* Unclassified SSE or MEMORY/sret SysV results are rejected;
                 * never return a record using an invented scalar ABI. */
                return false;
            }
            return fprintf(out,"  jmp .Lmini_x86_f%zu_ret\n",function_id)>=0;
        }
        if (term->return_value != MINIC_CORE_VALUE_INVALID) {
            if (minic_type_is_long_double(fn->return_type))
                return x86_x87_load(out, frame, term->return_value) &&
                       fprintf(out, "  jmp .Lmini_x86_f%zu_ret\n", function_id) >= 0;
            if (minic_type_is_int128_integer(fn->return_type))
                return x86_i128_load(out, frame, term->return_value,
                                     "%rax", "%rdx") &&
                       fprintf(out, "  jmp .Lmini_x86_f%zu_ret\n", function_id) >= 0;
            if (!x86_value_load(out, frame, term->return_value, "%rax"))
                return false;
            if (minic_type_is_double(fn->return_type)) {
                if (fprintf(out, "  movq %%rax, %%xmm0\n") < 0) return false;
            } else if (minic_type_is_float(fn->return_type)) {
                if (fprintf(out, "  movd %%eax, %%xmm0\n") < 0) return false;
            }
        }
        return fprintf(out, "  jmp .Lmini_x86_f%zu_ret\n", function_id) >= 0;
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        /* Lua lvm.c uses GNU computed-goto dispatch. Core supplies an SSA
         * code pointer; only target instruction selection differs from RV64. */
        return x86_value_load(out, frame, term->indirect_target, "%rax") &&
               fprintf(out, "  jmp *%%rax\n") >= 0;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(out, "  jmp .Lmini_x86_f%zu_b%u\n",
                       function_id, (unsigned int)term->branch_target) >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return x86_value_load(out, frame, term->conditional.condition, "%rax") &&
               fprintf(out, "  testq %%rax, %%rax\n"
                            "  jne .Lmini_x86_f%zu_b%u\n"
                            "  jmp .Lmini_x86_f%zu_b%u\n",
                       function_id, (unsigned int)term->conditional.when_true,
                       function_id, (unsigned int)term->conditional.when_false) >= 0;
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return fprintf(out, "  ud2\n") >= 0;
    default: return false;
    }
}

