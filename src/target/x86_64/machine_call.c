#include "target/x86_64/machine_internal.h"
#include "target/x86_64/abi.h"
#include "target/x86_64/call_plan.h"
#include "target/common/core_call_view.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Entire SysV/x87 call ABI transport: prepare args, lower direct/indirect
 * calls, preserve target return values. No redundant machine dispatch path. */
/* SysV argument classification is completed by the same planner used for
 * incoming parameters; this function only transfers Core values/records
 * into already selected native registers and outgoing stack locations. */
static bool x86_emit_call_arguments_planned(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreCallView *view, const MinicX86ArgumentPlan *plan) {
    static const char *const gp_regs[] =
        {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    size_t i;
    if (out == NULL || fn == NULL || frame == NULL || view == NULL ||
        plan == NULL || plan->argument_count != view->argument_count ||
        (plan->argument_count != 0U && plan->locations == NULL))
        return false;
    if (plan->stack_bytes != 0U &&
        fprintf(out, "  subq $%zu, %%rsp\n", plan->stack_bytes) < 0)
        return false;
    for (i = 0U; i < view->argument_count; ++i) {
        const MinicCoreCallArgument *arg = &view->arguments[i];
        const MinicX86ArgLocation *loc = &plan->locations[i];
        MinicType type = i < view->parameter_count
            ? view->parameter_types[i]
            : (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT
                ? fn->objects[arg->value.object_id].type
                : fn->values[arg->value.value_id].type);
        if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            const MinicCoreObjectId object = arg->value.object_id;
            if (object >= fn->object_count ||
                !minic_x86_64_record_abi_compatible(
                    fn->objects[object].type, type))
                return false;
            if (loc->kind == MINIC_X86_ARG_RECORD_EMPTY)
                continue;
            /* Outgoing stack reservation changes RSP-relative local-object
             * addresses in highly aligned frames. Compensate precisely. */
            if (frame->objects_from_aligned_rsp) {
                if (frame->object_offsets[object] >
                    SIZE_MAX - plan->stack_bytes ||
                    fprintf(out, "  leaq %zu(%%rsp), %%r11\n",
                            frame->object_offsets[object] +
                            plan->stack_bytes) < 0) return false;
            } else if (!x86_object_address(out, frame, object, "%r11"))
                return false;
            if (loc->kind == MINIC_X86_ARG_RECORD_ONE_GP) {
                if (!x86_one_gp_load(out, loc->record_bytes)) return false;
                if (!loc->on_stack) {
                    if (fprintf(out, "  movq %%rax, %s\n",
                                gp_regs[loc->gp_index]) < 0) return false;
                } else if (fprintf(out, "  movq %%rax, %zu(%%rsp)\n",
                                   loc->stack_index * 8U) < 0) return false;
                continue;
            }
            if (loc->kind == MINIC_X86_ARG_RECORD_MEMORY) {
                /* Scalar registers holding prior arguments are live. Copy
                 * stack-passed >16-byte record without touching them, and
                 * never overread the exact final partial eightbyte. */
                size_t j, dest = loc->stack_index * 8U;
                if (loc->stack_index > SIZE_MAX / 8U ||
                    dest > SIZE_MAX - loc->record_bytes) return false;
                for (j = 0U; j + 8U <= loc->record_bytes; j += 8U)
                    if (fprintf(out,
                          "  movq %zu(%%r11), %%rax\n"
                          "  movq %%rax, %zu(%%rsp)\n", j, dest + j) < 0)
                        return false;
                for (; j < loc->record_bytes; ++j)
                    if (fprintf(out,
                          "  movzbq %zu(%%r11), %%rax\n"
                          "  movb %%al, %zu(%%rsp)\n", j, dest + j) < 0)
                        return false;
                continue;
            }
            if (loc->kind != MINIC_X86_ARG_RECORD_TWO_GP) return false;
            if (!loc->on_stack) {
                if (fprintf(out,
                    "  movq 0(%%r11), %s\n"
                    "  movq 8(%%r11), %s\n",
                    gp_regs[loc->gp_index],
                    gp_regs[loc->gp_index + 1U]) < 0)
                    return false;
            } else if (fprintf(out,
                    "  movq 0(%%r11), %%rax\n"
                    "  movq %%rax, %zu(%%rsp)\n"
                    "  movq 8(%%r11), %%rax\n"
                    "  movq %%rax, %zu(%%rsp)\n",
                    loc->stack_index * 8U,
                    (loc->stack_index + 1U) * 8U) < 0)
                return false;
            continue;
        }
        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            arg->value.value_id >= fn->value_count)
            return false;
        if (loc->kind == MINIC_X86_ARG_I128_TWO_GP) {
            /* Use only caller-saved scratch registers outside the six GP
             * argument registers. Using rdx here would clobber an earlier
             * prepared argument before the actual CALL instruction. */
            if (!minic_type_is_int128_integer(type) ||
                !minic_type_is_int128_integer(
                    fn->values[arg->value.value_id].type) ||
                !x86_i128_load(out, frame, arg->value.value_id,
                               "%r10", "%r11"))
                return false;
            if (!loc->on_stack) {
                if (loc->gp_index > 4U ||
                    fprintf(out, "  movq %%r10, %s\n"
                                 "  movq %%r11, %s\n",
                            gp_regs[loc->gp_index],
                            gp_regs[loc->gp_index + 1U]) < 0)
                    return false;
            } else if (fprintf(out,
                    "  movq %%r10, %zu(%%rsp)\n"
                    "  movq %%r11, %zu(%%rsp)\n",
                    loc->stack_index * 8U,
                    (loc->stack_index + 1U) * 8U) < 0)
                return false;
            continue;
        }
        if (loc->kind == MINIC_X86_ARG_X87_MEMORY) {
            size_t offset;
            if (!minic_type_is_long_double(fn->values[arg->value.value_id].type) ||
                frame->value_offsets == NULL)
                return false;
            offset = frame->value_offsets[arg->value.value_id];
            if (offset < 16U || fprintf(out,
                "  movq -%zu(%%rbp), %%rax\n"
                "  movq %%rax, %zu(%%rsp)\n"
                "  movq -%zu(%%rbp), %%rax\n"
                "  movq %%rax, %zu(%%rsp)\n",
                offset, loc->stack_index * 8U, offset - 8U,
                (loc->stack_index + 1U) * 8U) < 0) return false;
            continue;
        }
        if (!x86_value_load(out, frame, arg->value.value_id, "%rax"))
            return false;
        if (loc->kind == MINIC_X86_ARG_SSE) {
            if (!loc->on_stack) {
                if (fprintf(out, minic_type_is_double(type)
                            ? "  movq %%rax, %%xmm%zu\n"
                            : "  movd %%eax, %%xmm%zu\n",
                            loc->fp_index) < 0)
                    return false;
            } else if (fprintf(out, "  movq %%rax, %zu(%%rsp)\n",
                               loc->stack_index * 8U) < 0) return false;
        } else if (loc->kind == MINIC_X86_ARG_INTEGER) {
            if (!loc->on_stack) {
                if (fprintf(out, "  movq %%rax, %s\n",
                            gp_regs[loc->gp_index]) < 0) return false;
            } else if (fprintf(out, "  movq %%rax, %zu(%%rsp)\n",
                               loc->stack_index * 8U) < 0) return false;
        } else return false;
    }
    /* Variadic SysV callees receive the number of used SSE argument regs. */
    return !plan->is_variadic ||
           fprintf(out, "  movb $%zu, %%al\n", plan->end.fp_used) >= 0;
}

/* Machine CALL emission consumes the one SysV plan built for its
 * Common Core CALL identity; external standalone emission still works. */
static bool x86_emit_call_arguments(
    FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicX86Frame *frame, const MinicCoreCallView *call,
    const MinicX86ArgumentPlan *cached_plan,
    size_t *allocated_stack_bytes) {
    MinicX86ArgumentPlan fallback = {0};
    const MinicX86ArgumentPlan *plan;
    bool ok;
    if (allocated_stack_bytes == NULL || call == NULL || frame == NULL)
        return false;
    plan = cached_plan;
    if (frame->semantic_calls != NULL && plan == NULL) return false;
    if (plan == NULL) {
        if (!minic_x86_64_plan_outgoing_arguments(
                program, target, fn, call, &fallback))
            return false;
        plan = &fallback;
    }
    ok = x86_emit_call_arguments_planned(out, fn, frame, call, plan);
    if (ok) *allocated_stack_bytes = plan->stack_bytes;
    minic_x86_64_argument_plan_destroy(&fallback);
    return ok;
}

/* The SysV INTEGER-class aggregate return travels in RAX/RDX.
 * MEMORY/sret or SSE-class aggregate results remain fail-closed. */
static bool x86_emit_call_record_result(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreCallee *callee, MinicCoreObjectId object) {
    size_t bytes;
    if (object >= fn->object_count ||
        !minic_type_is_record(callee->return_type) ||
        !minic_x86_64_record_abi_compatible(
            fn->objects[object].type, callee->return_type))
        return false;
    if (minic_x86_64_memory_result_record(
            program, target, callee->return_type, &bytes))
        /* The callee has already written the exact object via the sret
         * pointer, not an RAX/RDX proxy. */
        return true;
    if (!x86_object_address(out, frame, object, "%r10")) return false;
    if (minic_x86_64_one_gp_record(program, target, callee->return_type, &bytes))
        return x86_one_gp_store(out, bytes);
    if (minic_x86_64_two_gp_record(program, target, callee->return_type))
        return fprintf(out,
            "  movq %%rax, 0(%%r10)\n"
            "  movq %%rdx, 8(%%r10)\n") >= 0;
    return false;
}

static bool x86_prepare_call_sret(FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicX86Frame *frame, const MinicCoreCallView *call,
    MinicCoreObjectId object, size_t allocated_stack_bytes) {
    size_t bytes;
    if (!minic_x86_64_memory_result_record(
            program, target, call->return_type, &bytes))
        return true;
    if (call->return_form != MINIC_CORE_CALL_RETURN_OBJECT ||
        object >= fn->object_count ||
        !minic_x86_64_record_abi_compatible(
            fn->objects[object].type, call->return_type))
        return false;
    /* Argument marshalling has already reserved stack call space. Correct
     * an RSP-relative high-alignment object address for that reservation,
     * and preserve all explicit GP arguments, which now start at RSI. */
    if (frame->objects_from_aligned_rsp) {
        if (frame->object_offsets[object] > SIZE_MAX - allocated_stack_bytes)
            return false;
        return fprintf(out, "  leaq %zu(%%rsp), %%rdi\n",
            frame->object_offsets[object] + allocated_stack_bytes) >= 0;
    }
    return x86_object_address(out, frame, object, "%rdi");
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


/* The semantic CALL ordinal indexes the identical, verified ABI snapshot.
 * Production resolves one sparse ID per call, not two binary searches.
 * Standalone callers keep the existing checked view + ABI fallback. */
static bool x86_resolve_call_view(
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, size_t instruction_id,
    MinicCoreCallView *view, const MinicX86ArgumentPlan **planned) {
    size_t ordinal;
    const MinicCoreCallView *semantic;
    if (fn == NULL || frame == NULL || ins == NULL ||
        view == NULL || planned == NULL)
        return false;
    *planned = NULL;
    if (frame->semantic_calls == NULL)
        return minic_core_call_view_build(fn, ins, view);
    semantic = minic_core_call_index_find(
        frame->semantic_calls, instruction_id, &ordinal);
    if (semantic == NULL ||
        frame->outgoing_call_plans.count != frame->semantic_calls->count ||
        ordinal >= frame->outgoing_call_plans.count ||
        frame->outgoing_call_plans.sites == NULL ||
        frame->outgoing_call_plans.sites[ordinal].instruction_id != instruction_id)
        return false;
    *view = *semantic;
    *planned = &frame->outgoing_call_plans.sites[ordinal].plan;
    return true;
}

bool minic_x86_64_emit_call_instruction(
    FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicX86Frame *frame, const MinicCoreInstruction *ins,
    size_t instruction_id) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || frame == NULL || ins == NULL) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_CALL: {
        MinicCoreCallView call;
        const MinicX86ArgumentPlan *cached_plan;
        const MinicCoreCallee *callee;
        size_t begin, count;
        size_t outgoing_bytes = 0U;
        if (!x86_resolve_call_view(fn, frame, ins, instruction_id,
                                   &call, &cached_plan))
            return false;
        if (call.callee == NULL)
            return false;
        callee = call.callee;
        begin = call.argument_begin;
        count = call.argument_count;
        if (callee->name == NULL) {
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
                                     &call, cached_plan, &outgoing_bytes)) {
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
        if (!x86_prepare_call_sret(out, program, target, fn, frame,
                &call, ins->value.call.result_object, outgoing_bytes) ||
            fprintf(out, "  call %s@PLT\n", callee->name) < 0 ||
            (outgoing_bytes != 0U &&
             fprintf(out, "  addq $%zu, %%rsp\n", outgoing_bytes) < 0))
            return false;
        if (!(ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID
                  ? (ins->result == MINIC_CORE_VALUE_INVALID &&
                     x86_emit_call_record_result(
                         out, program, target, fn, frame, callee,
                         ins->value.call.result_object))
                  : x86_emit_call_result(out, program, target, frame,
                                         callee->return_type, ins->result))) {
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
        const MinicX86ArgumentPlan *cached_plan;
        const MinicCoreValueId callee = ins->value.indirect_call.callee;
        const MinicCoreCallSignature *signature;
        size_t outgoing_bytes = 0U;
        if (!x86_resolve_call_view(fn, frame, ins, instruction_id,
                                   &call, &cached_plan))
            return false;
        if (call.signature == NULL)
            return false;
        signature = call.signature;
        if (!x86_emit_call_arguments(out, program, target, fn, frame,
                                     &call, cached_plan, &outgoing_bytes) ||
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
