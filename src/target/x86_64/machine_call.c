#include "target/x86_64/machine_internal.h"
#include "target/x86_64/abi.h"
#include "target/common/core_call_view.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Entire SysV/x87 call ABI transport: prepare args, lower direct/indirect
 * calls, preserve target return values. No redundant machine dispatch path. */
static bool x86_emit_call_arguments(FILE *out, const MinicC0Program *program,
                                    const MinicTargetInfo *target,
                                    const MinicCoreFunction *fn,
                                    const MinicX86Frame *frame,
                                    size_t begin, size_t count,
                                    const MinicType *fixed_types,
                                    size_t fixed_count, bool variadic,
                                    size_t *allocated_stack_bytes) {
    static const char *const integer_regs[] =
        {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    size_t i, integer_index = 0U, float_index = 0U;
    size_t stack_slots = 0U, aligned_bytes = 0U;
    size_t ignored_bytes;
    bool ignored_sign;
    if (allocated_stack_bytes == NULL ||
        (fixed_count != 0U && fixed_types == NULL) ||
        count < fixed_count || (!variadic && count != fixed_count) ||
        begin > fn->call_argument_count ||
        count > fn->call_argument_count - begin) return false;

    /* Preflight types and stack allocation before emitting argument moves. */
    for (i = 0U; i < count; ++i) {
        const MinicCoreCallArgument *arg = &fn->call_arguments[begin + i];
        MinicType type;
        if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            size_t record_bytes;
            if (arg->value.object_id >= fn->object_count ||
                (i >= fixed_count && !variadic)) return false;
            type = i < fixed_count ? fixed_types[i] :
                    fn->objects[arg->value.object_id].type;
            if (!minic_type_equal(fn->objects[arg->value.object_id].type, type))
                return false;
            if (minic_x86_64_one_gp_record(program, target, type, &record_bytes)) {
                if (integer_index < 6U) ++integer_index;
                else ++stack_slots;
            } else {
                if (!minic_x86_64_two_gp_record(program, target, type)) return false;
                if (integer_index <= 4U) integer_index += 2U;
                else stack_slots += 2U;
            }
            continue;
        }
        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            arg->value.value_id >= fn->value_count) return false;
        type = i < fixed_count ? fixed_types[i] : fn->values[arg->value.value_id].type;
        if (minic_type_is_double(type) || minic_type_is_float(type)) {
            if (float_index < 8U) ++float_index;
            else ++stack_slots;
        } else if (minic_type_is_long_double(type)) {
            /* SysV x87 stack arguments require a 16-byte start. Keep
             * preflight in lockstep with the emission pass below. */
            stack_slots += stack_slots & 1U;
            stack_slots += 2U;
        } else {
            if (!minic_x86_64_scalar_width(program, target, type, &ignored_bytes,
                                  &ignored_sign)) return false;
            if (integer_index < 6U) ++integer_index;
            else ++stack_slots;
        }
    }
    if (stack_slots > (size_t)INT32_MAX / 8U ||
        !minic_x86_64_align(stack_slots * 8U, 16U, &aligned_bytes) ||
        aligned_bytes > (size_t)INT32_MAX) return false;
    if (aligned_bytes != 0U &&
        fprintf(out, "  subq $%zu, %%rsp\n", aligned_bytes) < 0) return false;
    *allocated_stack_bytes = aligned_bytes;
    integer_index = 0U;
    float_index = 0U;
    stack_slots = 0U;
    for (i = 0U; i < count; ++i) {
        const MinicCoreCallArgument *arg = &fn->call_arguments[begin + i];
        MinicType type;
        if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            const MinicCoreObjectId object = arg->value.object_id;
            size_t record_bytes;
            if (object >= fn->object_count ||
                (i >= fixed_count && !variadic)) return false;
            type = i < fixed_count ? fixed_types[i] : fn->objects[object].type;
            if (!minic_type_equal(fn->objects[object].type, type) ||
                !x86_object_address(out, frame, object, "%r11")) return false;
            if (minic_x86_64_one_gp_record(program, target, type, &record_bytes)) {
                if (!x86_one_gp_load(out, record_bytes)) return false;
                if (integer_index < 6U) {
                    if (fprintf(out, "  movq %%rax, %s\n",
                                integer_regs[integer_index]) < 0) return false;
                    ++integer_index;
                } else {
                    if (fprintf(out, "  movq %%rax, %zu(%%rsp)\n",
                                stack_slots * 8U) < 0) return false;
                    ++stack_slots;
                }
                continue;
            }
            if (!minic_x86_64_two_gp_record(program, target, type)) return false;
            if (integer_index <= 4U) {
                if (fprintf(out,
                    "  movq 0(%%r11), %s\n"
                    "  movq 8(%%r11), %s\n",
                    integer_regs[integer_index], integer_regs[integer_index + 1U]) < 0)
                    return false;
                integer_index += 2U;
            } else {
                if (fprintf(out,
                    "  movq 0(%%r11), %%rax\n"
                    "  movq %%rax, %zu(%%rsp)\n"
                    "  movq 8(%%r11), %%rax\n"
                    "  movq %%rax, %zu(%%rsp)\n",
                    stack_slots * 8U, (stack_slots + 1U) * 8U) < 0)
                    return false;
                stack_slots += 2U;
            }
            continue;
        }
        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            arg->value.value_id >= fn->value_count) return false;
        type = i < fixed_count ? fixed_types[i] : fn->values[arg->value.value_id].type;
        if (minic_type_is_long_double(type)) {
            size_t off;
            /* Match the preflight slot cursor and incoming callee cursor.
             * Otherwise odd preceding stack args silently misplace x87
             * values by eight bytes in mixed MiniC/GNU calls. */
            stack_slots += stack_slots & 1U;
            if (!minic_type_is_long_double(fn->values[arg->value.value_id].type) ||
                frame->value_offsets == NULL) return false;
            off = frame->value_offsets[arg->value.value_id];
            if (off < 16U || fprintf(out,
                "  movq -%zu(%%rbp), %%rax\n"
                "  movq %%rax, %zu(%%rsp)\n"
                "  movq -%zu(%%rbp), %%rax\n"
                "  movq %%rax, %zu(%%rsp)\n",
                off, stack_slots * 8U, off - 8U,
                (stack_slots + 1U) * 8U) < 0) return false;
            stack_slots += 2U;
            continue;
        }
        if (!x86_value_load(out, frame, arg->value.value_id, "%rax")) return false;
        if (minic_type_is_double(type) || minic_type_is_float(type)) {
            if (float_index < 8U) {
                if (fprintf(out,
                            minic_type_is_double(type)
                                ? "  movq %%rax, %%xmm%zu\n"
                                : "  movd %%eax, %%xmm%zu\n",
                            float_index) < 0) return false;
                ++float_index;
            } else {
                if (fprintf(out, "  movq %%rax, %zu(%%rsp)\n", stack_slots * 8U) < 0)
                    return false;
                ++stack_slots;
            }
        } else if (integer_index < 6U) {
            if (fprintf(out, "  movq %%rax, %s\n", integer_regs[integer_index]) < 0)
                return false;
            ++integer_index;
        } else {
            if (fprintf(out, "  movq %%rax, %zu(%%rsp)\n", stack_slots * 8U) < 0)
                return false;
            ++stack_slots;
        }
    }
    /* SysV variadic calls advertise the number of used XMM argument regs. */
    return !variadic || fprintf(out, "  movb $%zu, %%al\n", float_index) >= 0;
}

static bool x86_emit_call_result(FILE *out, const MinicC0Program *program,
                                 const MinicTargetInfo *target,
                                 const MinicX86Frame *frame,
                                 MinicType return_type, MinicCoreValueId result) {
    if (result == MINIC_CORE_VALUE_INVALID)
        return minic_type_is_void(return_type);
    if (minic_type_is_int128_integer(return_type))
        return x86_i128_store(out, frame, result, "%rax", "%rdx");
    if (minic_type_is_long_double(return_type))
        return x86_x87_store(out, frame, result);
    if (minic_type_is_double(return_type)) {
        if (fprintf(out, "  movq %%xmm0, %%rax\n") < 0) return false;
    } else if (minic_type_is_float(return_type)) {
        if (fprintf(out, "  movd %%xmm0, %%eax\n") < 0) return false;
    } else if (!x86_normalize(out, program, target, return_type)) {
        return false;
    }
    return x86_value_store(out, frame, result);
}

/* Core represents long-double operations as typed libcalls because RV64
 * uses 128-bit TFmode. Intercept ONLY these exact Core-generated helper
 * signatures for the x86-64 x87/80-bit target. Sending an x87 value to
 * __addtf3/__extenddftf2 would silently use the wrong floating format. */
static bool x86_emit_x87_core_helper(FILE *out, const MinicCoreFunction *fn,
                                      const MinicX86Frame *frame,
                                      const MinicCoreCallee *callee,
                                      const MinicCoreInstruction *ins) {
    const char *name = callee->name;
    size_t begin = ins->value.call.argument_begin;
    size_t count = ins->value.call.argument_count;
    MinicCoreValueId a, b = MINIC_CORE_VALUE_INVALID;
    bool result_ld, source_ld;
    if (name == NULL || begin > fn->call_argument_count ||
        count > fn->call_argument_count - begin ||
        ins->result >= fn->value_count ||
        (count != 1U && count != 2U)) return false;
    if (fn->call_arguments[begin].kind != MINIC_CORE_CALL_ARGUMENT_VALUE)
        return false;
    a = fn->call_arguments[begin].value.value_id;
    if (a >= fn->value_count) return false;
    if (count == 2U) {
        if (fn->call_arguments[begin + 1U].kind != MINIC_CORE_CALL_ARGUMENT_VALUE)
            return false;
        b = fn->call_arguments[begin + 1U].value.value_id;
        if (b >= fn->value_count) return false;
    }
    result_ld = minic_type_is_long_double(callee->return_type);
    source_ld = minic_type_is_long_double(fn->values[a].type);
    if (!minic_type_equal(fn->values[ins->result].type, callee->return_type))
        return false;
    if (count == 1U) {
        if (result_ld && !source_ld && minic_type_is_double(fn->values[a].type) &&
            strcmp(name, "__extenddftf2") == 0)
            return fprintf(out, "  fldl -%zu(%%rbp)\n",
                           frame->value_offsets[a]) >= 0 &&
                   x86_x87_store(out, frame, ins->result);
        if (result_ld && !source_ld && minic_type_is_float(fn->values[a].type) &&
            strcmp(name, "__extendsftf2") == 0)
            return fprintf(out, "  flds -%zu(%%rbp)\n",
                           frame->value_offsets[a]) >= 0 &&
                   x86_x87_store(out, frame, ins->result);
        if (source_ld && minic_type_is_double(callee->return_type) &&
            strcmp(name, "__trunctfdf2") == 0)
            return x86_x87_load(out, frame, a) &&
                   fprintf(out, "  fstpl -%zu(%%rbp)\n",
                           frame->value_offsets[ins->result]) >= 0;
        if (source_ld && minic_type_is_float(callee->return_type) &&
            strcmp(name, "__trunctfsf2") == 0)
            return x86_x87_load(out, frame, a) &&
                   fprintf(out, "  fstps -%zu(%%rbp)\n",
                           frame->value_offsets[ins->result]) >= 0;
        if (source_ld && minic_type_is_integer(callee->return_type)) {
            bool unsigned_value = minic_type_is_unsigned_integer(callee->return_type);
            bool to_64 =
                strcmp(name, unsigned_value ? "__fixunstfdi" : "__fixtfdi") == 0;
            bool to_32 =
                strcmp(name, unsigned_value ? "__fixunstfsi" : "__fixtfsi") == 0;
            if (!to_64 && !to_32) return false;
            if (!x86_x87_load(out, frame, a)) return false;
            if (unsigned_value && to_64) {
                /* SysV C truncates long double -> uint64 for values in
                 * [0,2^64). Signed FISTTP alone fails at 2^63. Compare
                 * against exact binary64 2^63, subtract it for the high
                 * half, then set the integer high bit. */
                if (fprintf(out,
                        "  subq $16, %%rsp\n"
                        "  movabsq $0x43e0000000000000, %%r10\n"
                        "  movq %%r10, (%%rsp)\n"
                        "  fldl (%%rsp)\n"
                        "  fucomip %%st(1), %%st\n"
                        "  jbe 1f\n"
                        "  fisttpq 8(%%rsp)\n"
                        "  movq 8(%%rsp), %%rax\n"
                        "  jmp 2f\n"
                        "1:\n"
                        "  fldl (%%rsp)\n"
                        "  fsubrp %%st, %%st(1)\n"
                        "  fisttpq 8(%%rsp)\n"
                        "  movq 8(%%rsp), %%rax\n"
                        "  btsq $63, %%rax\n"
                        "2:\n"
                        "  addq $16, %%rsp\n") < 0) return false;
                return x86_value_store(out, frame, ins->result);
            }
            /* All representable uint32 values fit signed 64-bit. Use the
             * qword FISTTP destination even for 32-bit unsigned values. */
            if (fprintf(out,
                    "  subq $16, %%rsp\n"
                    "  fisttpq 8(%%rsp)\n"
                    "  movq 8(%%rsp), %%rax\n"
                    "  addq $16, %%rsp\n") < 0) return false;
            if (to_32 &&
                fprintf(out, unsigned_value
                          ? "  movl %%eax, %%eax\n"
                          : "  movslq %%eax, %%rax\n") < 0) return false;
            return x86_value_store(out, frame, ins->result);
        }
        if (result_ld && !source_ld && minic_type_is_integer(fn->values[a].type)) {
            bool unsigned_value = minic_type_is_unsigned_integer(fn->values[a].type);
            bool from_64 =
                strcmp(name, unsigned_value ? "__floatunditf" : "__floatditf") == 0;
            bool from_32 =
                strcmp(name, unsigned_value ? "__floatunsitf" : "__floatsitf") == 0;
            if (!from_64 && !from_32) return false;
            /* fild accepts signed integers. For unsigned 64-bit values
             * with bit 63 set, add precisely 2^64 as an x87 double-memory
             * constant after the signed fild; 80-bit extended precision
             * represents every uint64 exactly. Do not truncate to binary64. */
            if (unsigned_value && from_64) {
                if (!x86_value_load(out, frame, a, "%r10") ||
                    fprintf(out,
                        "  subq $16, %%rsp\n"
                        "  movq %%r10, (%%rsp)\n"
                        "  fildq (%%rsp)\n"
                        "  testq %%r10, %%r10\n"
                        "  jns 1f\n"
                        "  movabsq $0x43f0000000000000, %%r10\n"
                        "  movq %%r10, (%%rsp)\n"
                        "  faddl (%%rsp)\n"
                        "1:\n"
                        "  addq $16, %%rsp\n") < 0) return false;
                return x86_x87_store(out, frame, ins->result);
            }
            if (fprintf(out, from_32 && !unsigned_value
                    ? "  fildl -%zu(%%rbp)\n"
                    : "  fildq -%zu(%%rbp)\n",
                    frame->value_offsets[a]) < 0) return false;
            return x86_x87_store(out, frame, ins->result);
        }
        return false;
    }
    if (!source_ld || !minic_type_is_long_double(fn->values[b].type) ||
        !x86_x87_load(out, frame, a) || !x86_x87_load(out, frame, b))
        return false;
    if (result_ld) {
        const char *instruction =
            strcmp(name, "__addtf3") == 0 ? "faddp" :
            /* GAS AT&T st(0),st(1) spellings reverse the apparently
             * intuitive subtraction/division order.  After fld(left),
             * fld(right), fsubrp/fdivrp compute LEFT op RIGHT.
             * The former fsubp/fdivp silently computed RIGHT op LEFT. */
            strcmp(name, "__subtf3") == 0 ? "fsubrp" :
            strcmp(name, "__multf3") == 0 ? "fmulp" :
            strcmp(name, "__divtf3") == 0 ? "fdivrp" : NULL;
        if (instruction == NULL) return false;
        return fprintf(out, "  %s %%st, %%st(1)\n", instruction) >= 0 &&
               x86_x87_store(out, frame, ins->result);
    }
    if (!minic_type_is_integer(callee->return_type) ||
        (strcmp(name, "__eqtf2") != 0 &&
         strcmp(name, "__lttf2") != 0 &&
         strcmp(name, "__letf2") != 0)) return false;
    /* After fldt(left), fldt(right), compare ST(1)=left to ST(0)=right;
     * fxch ensures fucomip sees left as the first operand. Unordered
     * comparisons return +1 (libgcc helper contract) except eq != 0. */
    if (fprintf(out,
          "  fxch %%st(1)\n"
          "  fucomip %%st(1), %%st\n"
          "  fstp %%st(0)\n") < 0) return false;
    if (strcmp(name, "__eqtf2") == 0) {
        if (fprintf(out,
              "  setne %%al\n"
              "  setp %%r10b\n"
              "  orb %%r10b, %%al\n"
              "  movzbl %%al, %%eax\n") < 0) return false;
    } else if (fprintf(out,
                "  movl $1, %%eax\n"
                "  jp 1f\n"
                "  jb 2f\n"
                "  je 3f\n"
                "  jmp 1f\n"
                "2:\n"
                "  movl $-1, %%eax\n"
                "  jmp 1f\n"
                "3:\n"
                "  xorl %%eax, %%eax\n"
                "1:\n") < 0) return false;
    return x86_value_store(out, frame, ins->result);
}


bool minic_x86_64_emit_call_instruction(
    FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicX86Frame *frame, const MinicCoreInstruction *ins) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || frame == NULL || ins == NULL) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_CALL: {
        MinicCoreCallView call;
        const MinicCoreCallee *callee;
        size_t begin, count;
        size_t outgoing_bytes = 0U;
        if (!minic_core_call_view_build(fn, ins, &call) ||
            call.callee == NULL)
            return false;
        callee = call.callee;
        begin = call.argument_begin;
        count = call.argument_count;
        if (ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID ||
            callee->name == NULL) {
            if (getenv("MINIC_X86_CALL_TRACE") != NULL)
                (void)fprintf(stderr,
                    "X86_CALL_GATE function=%s callee=%s stage=record-result "
                    "result_object=%" PRIu32 " arg_count=%zu fixed=%zu\n",
                    fn->name, callee->name != NULL ? callee->name : "?",
                    ins->value.call.result_object, count, callee->parameter_count);
            return false;
        }
        if (minic_type_is_long_double(callee->return_type) ||
            (callee->parameter_count != 0U &&
             minic_type_is_long_double(callee->parameter_types[0]))) {
            if (strncmp(callee->name, "__", 2U) == 0 &&
                (strstr(callee->name, "tf") != NULL ||
                 strstr(callee->name, "TF") != NULL)) {
                bool handled = x86_emit_x87_core_helper(out, fn, frame, callee, ins);
                if (!handled && getenv("MINIC_X86_CALL_TRACE") != NULL)
                    (void)fprintf(stderr,
                        "X86_CALL_GATE function=%s callee=%s stage=x87-helper "
                        "return_ld=%d argc=%zu first_ld=%d\n",
                        fn->name, callee->name,
                        minic_type_is_long_double(callee->return_type) ? 1 : 0,
                        count,
                        count != 0U &&
                        fn->call_arguments[begin].kind == MINIC_CORE_CALL_ARGUMENT_VALUE &&
                        minic_type_is_long_double(
                            fn->values[fn->call_arguments[begin].value.value_id].type)
                            ? 1 : 0);
                return handled;
            }
        }
        if (!x86_emit_call_arguments(out, program, target, fn, frame,
                                     begin, count, callee->parameter_types,
                                     callee->parameter_count, callee->is_variadic,
                                     &outgoing_bytes)) {
            if (getenv("MINIC_X86_CALL_TRACE") != NULL) {
                size_t j;
                (void)fprintf(stderr,
                    "X86_CALL_GATE function=%s callee=%s stage=arguments "
                    "count=%zu fixed=%zu variadic=%d\n",
                    fn->name, callee->name, count, callee->parameter_count,
                    callee->is_variadic ? 1 : 0);
                for (j = 0U; j < count && begin + j < fn->call_argument_count; ++j) {
                    const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                    (void)fprintf(stderr,
                        "X86_CALL_ARG callee=%s index=%zu kind=%d "
                        "value=%" PRIu32 " param_record=%d\n",
                        callee->name, j, (int)arg->kind,
                        arg->kind == MINIC_CORE_CALL_ARGUMENT_VALUE
                            ? arg->value.value_id : arg->value.object_id,
                        j < callee->parameter_count &&
                        minic_type_is_record(callee->parameter_types[j]) ? 1 : 0);
                }
            }
            return false;
        }
        if (fprintf(out, "  call %s@PLT\n", callee->name) < 0 ||
            (outgoing_bytes != 0U &&
             fprintf(out, "  addq $%zu, %%rsp\n", outgoing_bytes) < 0))
            return false;
        if (!x86_emit_call_result(out, program, target, frame,
                                  callee->return_type, ins->result)) {
            if (getenv("MINIC_X86_CALL_TRACE") != NULL)
                (void)fprintf(stderr,
                    "X86_CALL_GATE function=%s callee=%s stage=return "
                    "long_double=%d double=%d record=%d\n",
                    fn->name, callee->name,
                    minic_type_is_long_double(callee->return_type) ? 1 : 0,
                    minic_type_is_double(callee->return_type) ? 1 : 0,
                    minic_type_is_record(callee->return_type) ? 1 : 0);
            return false;
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
        MinicCoreCallView call;
        const MinicCoreValueId callee = ins->value.indirect_call.callee;
        const MinicCoreCallSignature *signature;
        size_t begin, count;
        size_t outgoing_bytes = 0U;
        if (!minic_core_call_view_build(fn, ins, &call) ||
            call.signature == NULL)
            return false;
        signature = call.signature;
        begin = call.argument_begin;
        count = call.argument_count;
        if (!x86_emit_call_arguments(out, program, target, fn, frame, begin, count,
                                     signature->parameter_types, signature->parameter_count,
                                     signature->is_variadic, &outgoing_bytes) ||
            !x86_value_load(out, frame, callee, "%r11") ||
            fprintf(out, "  call *%%r11\n") < 0 ||
            (outgoing_bytes != 0U &&
             fprintf(out, "  addq $%zu, %%rsp\n", outgoing_bytes) < 0))
            return false;
        return x86_emit_call_result(out, program, target, frame,
                                    signature->return_type, ins->result);
    }
    default:
        return false;
    }
}
