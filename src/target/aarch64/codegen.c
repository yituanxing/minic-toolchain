#include "target/aarch64/codegen.h"
#include "target/aarch64/block_emit.h"
#include "target/common/global_emit.h"
#include "target/common/direct_program_emit.h"
#include "target/common/direct_preflight.h"
#include "target/common/direct_function_profile.h"
#include "target/common/direct_call_preflight.h"
#include "target/common/direct_call_plan.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_frame.h"
#include "target/common/lp64_aggregate.h"
#include "target/aarch64/abi.h"
#include "target/aarch64/asm_legalize.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* M1/M2: prove correct *actual* GNU ELF/ABI assembly, not just target naming.
 * Each signed 32-bit Core SSA value gets an 8-byte private stack slot.
 * Scalar int parameters, arithmetic, and basic block branches are admitted.
 * No implicit ABI equivalence with RISC-V64/x86-64 is assumed. */


/* Report the first failed backend guard only in opt-in trace mode.
 * backend_line points to the precise C condition; block/ins/kind to Core IR.
 * This leaves compilation and fail-closed semantics unchanged. */




/* Comparisons are Core booleans, not 32-bit C int. Both still fit our
 * private 8-byte SSA slots, with canonical 0/1 in the low 32 bits. */




/* M8: x86's width/sign principle, target-owned machine encodings.
 * A raw Core slot reserves 8 bytes even when the C object is 1 or 2 bytes.
 * The operand width comes from the target DataLayout, not a hard-coded
 * assumption about typedef names or host char signedness. */


/* M20 LP64 scalar-width gate: only ABI and memory operations accept 64 bits.
 * Narrow arithmetic continues to reject 64-bit values until implemented. */




/* C scalar return ABI is independent from the initial signed-int smoke
 * gate. Core's unsigned char xtime return and its GNU callers must share
 * the actual target integer width; reject unsupported 64/128-bit values. */


static bool emit_function_body_with_frame(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections,
                          MinicLP64FramePlan *frame_plan) {
    const char *name = minic_c0_function_symbol_name(decl);
    MinicLP64ArgumentPlan incoming_plan;
    size_t frame = 0U, i, object_end, save_offset, ld_base;
    bool has_direct_calls = false;
    bool dynamic_frame = false;
    const char *reject_stage = "function_preflight";
    size_t reject_block = SIZE_MAX, reject_ins = SIZE_MAX;
    size_t va_count = 0U, va_base = 0U, va_gp = 0U, va_fp = 0U;
    /* No semantic change: source-specific diagnostics only for the active
     * cJSON failure, never for normal builds or other projects. */
    const bool trace_cjson = getenv("MINIC_M1_TRACE") != NULL &&
                             getenv("MINIC_BACKEND_CENSUS") == NULL &&
                             name != NULL &&
                             (strcmp(name, "cJSON_ParseWithLengthOpts") == 0 ||
                               strcmp(name, "cJSON_Compare") == 0 ||
                               strcmp(name, "parse_number") == 0 ||
                               strcmp(name, "ensure") == 0 ||
                               strcmp(name, "compare_double") == 0 ||
                               strcmp(name, "print_number") == 0);
    if (trace_cjson)
        (void)fprintf(stderr,
            "MINIC_CJSON_STAGE arch=aarch64 function=%s stage=entry blocks=%zu values=%zu objects=%zu params=%zu calls=%zu\n",
            name, fn->block_count, fn->value_count, fn->object_count,
            fn->parameter_count, fn->callee_count);
    if (getenv("MINIC_M1_TRACE") != NULL &&
        name != NULL && ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr,
                      "MINIC_M9_GATE arch=aarch64 fn=%s stage=entry values=%zu objects=%zu blocks=%zu params=%zu return_valid=%d inline=%zu\n",
                      name, fn->value_count, fn->object_count,
                      fn->block_count, fn->parameter_count,
                      minic_lp64_valid_return_type(program, target, fn->return_type) ? 1 : 0,
                      fn->inline_asm_count);
    if (!minic_multi_direct_lp64_function_preflight(program, target, decl, fn,
                                               "aarch64", name))
        return minic_multi_direct_machine_reject(fn, "aarch64", name,
            reject_stage, reject_block, reject_ins, __LINE__);
    /* Validated shared function requirements and incoming ABI locations.
     * Only source/Core shape is shared; AAPCS64/LP64D classifiers differ. */
    {
        MinicDirectLP64FunctionProfile profile;
        MinicDirectLP64ProfileResult result =
            minic_multi_direct_lp64_profile_function(
                program, target, decl, fn, MINIC_LP64_ABI_AAPCS64,
                minic_aarch64_indirect_record, &incoming_plan, &profile);
        if (result != MINIC_DIRECT_LP64_PROFILE_OK) {
            const char *stage =
                result == MINIC_DIRECT_LP64_PROFILE_VARIADIC_FIXED_FLOAT
                    ? "variadic_fp_fixed" :
                result == MINIC_DIRECT_LP64_PROFILE_VARIADIC_SHAPE
                    ? "variadic_preflight" : reject_stage;
            return minic_multi_direct_machine_reject(
                fn, "aarch64", name, stage,
                reject_block, reject_ins, __LINE__);
        }
        va_count = profile.variadic_address_count;
        va_gp = profile.fixed_gp_count;
        va_fp = profile.fixed_fp_count;
        dynamic_frame = profile.has_dynamic_stack;
    }
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=aarch64 fn=%s stage=parameters_valid\n", name);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=aarch64 stage=parameters_ok\n");
    reject_stage = "cfg_preflight";
    if (!minic_multi_direct_lp64_cfg_preflight(program, target, fn,
                                         "aarch64", name, trace_cjson,
                                         &reject_block))
        return minic_multi_direct_machine_reject(fn, "aarch64", name,
            reject_stage, reject_block, reject_ins, __LINE__);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=aarch64 stage=cfg_ok\n");
    /* Object byte extents and alignment are validated by minic_lp64_frame_objects.
     * This admits real repeated local arrays without overlapping slots. */
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=aarch64 fn=%s stage=terminators_valid\n", name);
    reject_stage = "frame_layout";
    reject_block = SIZE_MAX;
    if (!minic_lp64_frame_plan_objects(program, target, fn, frame_plan)) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_REJECT arch=aarch64 function=%s values=%zu objects=%zu\n",
                          name, fn->value_count, fn->object_count);
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    }
    object_end = frame_plan->object_end;
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=aarch64 stage=frame_objects_ok end=%zu\n", object_end);
    ld_base = (object_end + 15U) & ~(size_t)15U;
    if (!minic_lp64_frame_plan_wide(fn, ld_base, frame_plan))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, "binary128_layout",
                                   SIZE_MAX, SIZE_MAX, __LINE__);
    object_end = frame_plan->wide_end;
    /* M24: direct and first-class indirect scalar calls share the same
     * eight integer-register ABI. Aggregate, FP, overflow arguments and
     * unsupported variadic tail types are still rejected before emission. */
    reject_stage = "call_preflight";
    if (!minic_multi_direct_lp64_calls_preflight(
            program, target, fn, "aarch64", name,
            minic_aarch64_indirect_record,
            &reject_ins, &has_direct_calls))
        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                reject_stage, reject_block,
                                                reject_ins, __LINE__);
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=aarch64 fn=%s stage=calls_valid object_end=%zu\n",
                      name, object_end);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=aarch64 stage=calls_ok\n");
    reject_stage = "frame_finalization";
    reject_ins = SIZE_MAX;
    if (!minic_lp64_finalize_direct_frame(
            object_end, has_direct_calls, dynamic_frame, va_count != 0U,
            MINIC_LP64_ABI_AAPCS64,
            &save_offset, &va_base, &frame)) {
        if (getenv("MINIC_M1_TRACE") != NULL && frame > MINIC_LARGE_FRAME_MAX)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_CAP arch=aarch64 function=%s frame=%zu end=%zu\n",
                          name, frame, object_end);
        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                reject_stage, reject_block,
                                                reject_ins, __LINE__);
    }
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=aarch64 stage=machine_emit frame=%zu\n", frame);
    if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",%%progbits\n", name) < 0)
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    } else if (fprintf(out, ".text\n") < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!decl->is_internal && fprintf(out, ".globl %s\n", name) < 0)
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!decl->is_internal &&
        !minic_multi_emit_elf_symbol_visibility(out, decl->visibility, name))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (fprintf(out, ".p2align 2\n.type %s,%%function\n%s:\n", name, name) < 0)
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    /* No illegal AArch64 add/sub immediates on real >4KiB frames. */
    if (frame != 0U &&
        (fprintf(out, "  movz x12, #%zu\n", frame & 65535U) < 0 ||
         (frame > 65535U &&
          fprintf(out, "  movk x12, #%zu, lsl #16\n", frame >> 16U) < 0) ||
         fputs("  sub sp, sp, x12\n", out) == EOF))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (has_direct_calls &&
        fprintf(out, "  str x30, [sp, #%zu]\n",
                save_offset) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    /* x19 is the AAPCS64 callee-saved fixed-frame anchor. A dynamic
     * alloca changes SP, but SSA slots and incoming parameters do not
     * change position. The outgoing call spill area still uses raw SP. */
    if (dynamic_frame &&
        (fprintf(out, "  str x19, [sp, #%zu]\n",
                 save_offset + (has_direct_calls ? 8U : 0U)) < 0 ||
         fputs("  mov x19, sp\n", out) == EOF))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                   reject_block, reject_ins, __LINE__);
    if (va_count != 0U) {
        /* Preserve incoming anonymous gp/fp arguments before any call. */
        for (i = 0U; i < 8U; ++i)
            if (fprintf(out, "  str x%zu, [sp, #%zu]\n",
                        i, va_base + 32U + i * 8U) < 0 ||
                fprintf(out, "  str q%zu, [sp, #%zu]\n",
                        i, va_base + 96U + i * 16U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           "variadic_save", SIZE_MAX,
                                           SIZE_MAX, __LINE__);
    }
    reject_stage = "machine_emit";
    {
        const MinicDirectLP64BlockContext blocks = {
            .program = program, .target = target, .fn = fn,
            .frame_plan = frame_plan, .incoming_plan = &incoming_plan,
            .name = name, .id = id, .frame = frame,
            .save_offset = save_offset, .va_gp = va_gp,
            .va_base = va_base, .va_fp = va_fp,
            .va_count = va_count, .has_direct_calls = has_direct_calls,
            .dynamic_frame = dynamic_frame, .trace_cjson = trace_cjson
        };
        if (!minic_aarch64_emit_core_blocks(out, &blocks)) return false;
    }
    reject_stage = "function_finish";
    reject_block = SIZE_MAX;
    if (fprintf(out, ".size %s,.-%s\n", name, name) < 0 ||
        (decl->is_constructor &&
         fprintf(out, ".section .init_array,\"aw\",%%init_array\n"
                      ".p2align 3\n  .xword %s\n", name) < 0))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                   reject_block, reject_ins, __LINE__);
    /* ELF initializers are relocation entries in .init_array, not
     * executable calls in the current Core block. Mirror x86's existing
     * constructor ABI and let the dynamic loader execute before main. */
    return true;
}

/* The function emitter may reject from many target-specific instruction
 * paths. Centralize the layout lifetime here instead of adding scattered
 * frees or changing those diagnostic/error paths. */
static bool emit_function_body(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicFunction *decl,
                               const MinicCoreFunction *fn, size_t id,
                               bool function_sections) {
    MinicLP64FramePlan frame_plan = {0};
    bool success = emit_function_body_with_frame(
        out, program, target, decl, fn, id, function_sections, &frame_plan);
    minic_lp64_frame_plan_destroy(&frame_plan);
    return success;
}

/* Translate only dynamic-frame functions through a private assembly buffer.
 * The existing AArch64 emitter speaks [sp,#offset] for its fixed SSA frame.
 * For alloca functions, those values belong to callee-saved x19 while SP
 * may move; source-owned outgoing stack argument slots remain relative to SP.
 * This is a target-local text legalization, just like large-offset handling,
 * and leaves all non-dynamic code generation byte-for-byte unchanged. */


static bool emit_function(FILE *out, const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections) {
    size_t i;
    bool dynamic_frame = false, success;
    FILE *temporary;
    for (i = 0U; i < fn->instruction_count; ++i)
        if (fn->instructions[i].kind ==
            MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC) {
            dynamic_frame = true;
            break;
        }
    if (!dynamic_frame)
        return emit_function_body(out, program, target, decl, fn, id,
                                  function_sections);
    temporary = tmpfile();
    if (temporary == NULL) return false;
    success = emit_function_body(temporary, program, target, decl, fn, id,
                                 function_sections);
    if (success && (fflush(temporary) != 0 ||
                    fseek(temporary, 0L, SEEK_SET) != 0 ||
                    !minic_aarch64_copy_dynamic_frame_asm(out, temporary)))
        success = false;
    if (fclose(temporary) != 0) success = false;
    return success;
}

bool minic_aarch64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    MinicDiagnostic *diagnostic) {
    static const MinicMultiDirectProgramConfig config = {
        MINIC_TARGET_ARCH_AARCH64,
        "aarch64",
        "AArch64",
        ".section .note.GNU-stack,\"\",%progbits\n",
        emit_function,
        minic_aarch64_legalize_large_sp_memory
    };
    return minic_multi_direct_write_program(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, diagnostic);
}
