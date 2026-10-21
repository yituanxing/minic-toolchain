#include "target/aarch64/codegen.h"
#include "target/common/global_emit.h"
#include "target/common/direct_program_emit.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_frame.h"
#include "target/common/lp64_aggregate.h"
#include "target/aarch64/abi.h"

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


static bool emit_integer_memory(FILE *out, bool store,
                                size_t width, bool sign) {
    const char *op = NULL;
    if (store) {
        op = width == 1U ? "  strb w9, [x10]\n" :
             width == 2U ? "  strh w9, [x10]\n" :
             width == 4U ? "  str w9, [x10]\n" :
             width == 8U ? "  str x9, [x10]\n" : NULL;
    } else {
        op = width == 1U ? (sign ? "  ldrsb w9, [x10]\n" :
                                     "  ldrb w9, [x10]\n") :
             width == 2U ? (sign ? "  ldrsh w9, [x10]\n" :
                                     "  ldrh w9, [x10]\n") :
             width == 4U ? "  ldr w9, [x10]\n" :
              width == 8U ? "  ldr x9, [x10]\n" : NULL;
    }
    return op != NULL && fputs(op, out) != EOF;
}

static bool emit_integer_normalize(FILE *out, size_t width, bool sign) {
    const char *op = width == 1U
        ? (sign ? "  sxtb w9, w9\n" :
                  "  uxtb w9, w9\n")
        : width == 2U
            ? (sign ? "  sxth w9, w9\n" :
                      "  uxth w9, w9\n")
            : width == 4U ? "" : NULL;
    return op != NULL && fputs(op, out) != EOF;
}

/* Source-driven tinyAES M4 frontier. Reuse existing target DataLayout to
 * size ordinary integer globals and flat arrays, but keep ELF emission
 * in the ISA backend. Unsupported aggregate/relocation cases must fail
 * closed, rather than silently manufacture incorrect object bytes. */
/* cJSON's first original file-scope globals are a flat error record
 * {null pointer, zero size_t} and a function-pointer hooks record.
 * Reuse the validated shared target DataLayout for *byte offsets* and
 * function/object relocation addends. Only bounded flat scalar records
 * belong here; recursive records/unions/bitfields fail closed. */




/* Emission of target-neutral ELF STV_* binding metadata. */



/* External default-visibility ELF symbols may be interposed by glibc.
 * AArch64 ADR_PREL_PG_HI21 cannot address optind@@GLIBC from a PIE. Use
 * GOT for external data/code references. Keep only proven TU-local static
 * definitions on the already-tested direct ADRP path (AES RoundKey). */
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



/* M7: retain x86's established DataLayout-owned object sizing instead
 * of guessing eight bytes per Core object.  The new-ISA stack slots
 * are SP-positive; naturally aligned objects are laid out after SSA
 * value slots.  Keep bounded <= 8 KiB frames until large-offset ABI
 * access has been independently checked on both targets. */
#define MINIC_LARGE_FRAME_MAX 1048576U




/* Preserve 16-byte long-double SSA values outside 8-byte scalar slots. */


/* Target-local final legalization of out-of-range SP-relative memory access.
 * Full Core SSA stack slots are preserved, and x12 is a reserved scratch.
 * This pass does not transform small-frame GNU instructions. */
static bool legalize_large_sp_memory(const char *path) {
    FILE *input = fopen(path, "rb"), *output, *rewritten;
    char line[512], copy[8192];
    size_t size;
    bool success = true;
    if (input == NULL) return false;
    output = tmpfile();
    if (output == NULL) { (void)fclose(input); return false; }
    while (fgets(line, sizeof(line), input) != NULL) {
        char op[16] = {0}, reg[16] = {0};
        size_t offset = 0U;
        const bool original_sp =
            sscanf(line, "  %15s %15[^,], [sp, #%zu]",
                   op, reg, &offset) == 3;
        const bool fixed_x19 = !original_sp &&
            sscanf(line, "  %15s %15[^,], [x19, #%zu]",
                   op, reg, &offset) == 3;
        if ((original_sp || fixed_x19) &&
            (strncmp(op, "ldr", 3U) == 0 ||
             strncmp(op, "str", 3U) == 0) && offset > 4095U) {
            const uint64_t imm = (uint64_t)offset;
            unsigned int shift;
            if (fprintf(output, "  movz x12, #%" PRIu64 "\n",
                        imm & UINT64_C(0xffff)) < 0) {
                success = false; break;
            }
            for (shift = 16U; shift <= 48U; shift += 16U) {
                if (((imm >> shift) & UINT64_C(0xffff)) != 0U &&
                    fprintf(output, "  movk x12, #%" PRIu64
                            ", lsl #%u\n",
                            (imm >> shift) & UINT64_C(0xffff), shift) < 0) {
                    success = false; break;
                }
            }
            if (!success ||
                fputs(fixed_x19 ? "  add x12, x19, x12\n" :
                                   "  add x12, sp, x12\n", output) == EOF ||
                fprintf(output, "  %s %s, [x12]\n", op, reg) < 0) {
                success = false; break;
            }
        } else if (fputs(line, output) == EOF) {
            success = false; break;
        }
    }
    if (ferror(input) != 0 || fclose(input) != 0) success = false;
    if (success && fseek(output, 0L, SEEK_SET) != 0) success = false;
    rewritten = success ? fopen(path, "wb") : NULL;
    if (rewritten == NULL) success = false;
    if (success) {
        while ((size = fread(copy, 1U, sizeof(copy), output)) != 0U)
            if (fwrite(copy, 1U, size, rewritten) != size) {
                success = false; break;
            }
        if (ferror(output) != 0 || fclose(rewritten) != 0) success = false;
    }
    if (fclose(output) != 0) success = false;
    return success;
}


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

static bool emit_lp64_record_chunk_parameter(FILE *out, size_t offset,
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

static bool emit_function_body(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections) {
    const char *name = minic_c0_function_symbol_name(decl);
    const MinicCoreBlock *block;
    size_t frame, i, object_end, save_offset, ld_base;
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
    if (name == NULL || name[0] == '\0' ||
        !minic_core_function_verify(fn) ||
        decl->is_weak ||
        (decl->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
         minic_multi_elf_visibility_directive(decl->visibility) == NULL) ||
        decl->section_name != NULL || decl->alias_target != MINIC_FUNCTION_INVALID ||
        !minic_lp64_valid_return_type(program, target, fn->return_type) ||
        fn->block_count == 0U || fn->block_count > 2048U || fn->entry_block != 0U ||
        fn->inline_asm_count != 0U || fn->parameter_count > 16U ||
        fn->value_count > 32768U || fn->object_count > 8192U)
        {
            if (getenv("MINIC_BACKEND_CENSUS") != NULL)
                (void)fprintf(stderr,
                              "MINIC_BACKEND_PREFLIGHT arch=aarch64 function=%s "
                              "blocks=%zu values=%zu objects=%zu params=%zu "
                              "entry=%u inline=%zu return_valid=%d verified=%d "
                              "constructor=%d weak=%d visibility=%d section=%d\n",
                              name != NULL ? name : "<unnamed>",
                              fn->block_count, fn->value_count, fn->object_count,
                              fn->parameter_count, (unsigned int)fn->entry_block,
                              fn->inline_asm_count,
                              minic_lp64_valid_return_type(program, target, fn->return_type) ? 1 : 0,
                              minic_core_function_verify(fn) ? 1 : 0,
                              decl->is_constructor ? 1 : 0, decl->is_weak ? 1 : 0,
                              (int)decl->visibility,
                              decl->section_name != NULL ? 1 : 0);
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                       reject_block, reject_ins, __LINE__);
        }
    /* One independent va_start cursor, with AAPCS64 gp/fp register banks.
     * For other variadic shapes fail closed until a GNU ABI test exists. */
    if (decl->is_variadic) {
        for (i = 0U; i < fn->parameter_count; ++i) {
            if (minic_type_is_double(fn->parameter_types[i]) ||
                minic_type_is_float(fn->parameter_types[i])) ++va_fp;
            else ++va_gp;
        }
    }
    for (i = 0U; i < fn->instruction_count; ++i) {
        if (fn->instructions[i].kind ==
            MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS) ++va_count;
        if (fn->instructions[i].kind ==
            MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC) dynamic_frame = true;
    }
    if (va_count != 0U &&
        (!decl->is_variadic || va_count != 1U ||
         va_gp > 8U || va_fp > 8U))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, "variadic_preflight",
                                   SIZE_MAX, SIZE_MAX, __LINE__);
    for (i = 0U; i < fn->parameter_count; ++i) {
        size_t record_size, record_slots;
        if (!minic_lp64_valid_parameter_type(program, target, fn->parameter_types[i]) &&
            !minic_multi_gp_record(program, target, fn->parameter_types[i],
                                   &record_size, &record_slots) &&
            !minic_aarch64_indirect_record(program, target,
                                         fn->parameter_types[i], &record_size))
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                       reject_block, reject_ins, __LINE__);
    }
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=aarch64 fn=%s stage=parameters_valid\n", name);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=aarch64 stage=parameters_ok\n");
    /* M2: verify every CFG successor and return before emitting anything.
     * A function's SSA values and local objects retain unique stack slots
     * across blocks, so no architecture-independent register allocator is
     * needed to implement basic control-flow correctly. */
    reject_stage = "cfg_preflight";
    for (i = 0U; i < fn->block_count; ++i) {
        reject_block = i;
        const MinicCoreTerminator *term = &fn->blocks[i].terminator;
        /* M32 diagnostics: identify the exact CFG gate from the original
         * 80-block cJSON_Compare rather than guessing at opcode support. */
        if (trace_cjson && strcmp(name, "cJSON_Compare") == 0)
            (void)fprintf(stderr, "MINIC_CJSON_CFG arch=aarch64 block=%zu kind=%d has_term=%d condition=%u\n",
                          i, (int)term->kind, fn->blocks[i].has_terminator ? 1 : 0,
                          (unsigned int)term->conditional.condition);
        if (!fn->blocks[i].has_terminator) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        switch (term->kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (term->return_object != MINIC_CORE_OBJECT_INVALID ||
                (minic_type_is_void(fn->return_type)
                 ? term->return_value != MINIC_CORE_VALUE_INVALID
                 : term->return_value == MINIC_CORE_VALUE_INVALID ||
                   term->return_value >= fn->value_count ||
                   !minic_type_equal(fn->values[term->return_value].type,
                                     fn->return_type))) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            if (term->indirect_target >= fn->value_count ||
                !minic_type_is_pointer(fn->values[term->indirect_target].type))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Verified Core unreachable terminator is a semantic no-return
             * endpoint, not an implicit branch into the next basic block. */
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (term->branch_target >= fn->block_count) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
            size_t condition_width = 0U;
            bool condition_signed = false;
            /* A Core condition can be an original C narrow integer:
             * cJSON's input-byte loop tests unsigned char directly.
             * The branch emission zero-tests the low 32-bit SSA slot. */
            if (term->conditional.when_true >= fn->block_count ||
                term->conditional.when_false >= fn->block_count ||
                term->conditional.condition >= fn->value_count ||
                (!minic_lp64_valid_int_or_bool(fn->values[term->conditional.condition].type) &&
                 !minic_lp64_register64_type(program, target,
                     fn->values[term->conditional.condition].type) &&
                 !minic_lp64_scalar_integer_width(program, target,
                     fn->values[term->conditional.condition].type,
                     &condition_width, &condition_signed)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        default:
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
    }
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=aarch64 stage=cfg_ok\n");
    /* Object byte extents and alignment are validated by minic_lp64_frame_objects.
     * This admits real repeated local arrays without overlapping slots. */
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=aarch64 fn=%s stage=terminators_valid\n", name);
    reject_stage = "frame_layout";
    reject_block = SIZE_MAX;
    if (!minic_lp64_frame_objects(program, target, fn, fn->object_count, NULL,
                       &object_end)) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_REJECT arch=aarch64 function=%s values=%zu objects=%zu\n",
                          name, fn->value_count, fn->object_count);
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    }
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=aarch64 stage=frame_objects_ok end=%zu\n", object_end);
    ld_base = (object_end + 15U) & ~(size_t)15U;
    if (!minic_lp64_binary128_slot(fn, ld_base, (MinicCoreValueId)fn->value_count,
                        &object_end))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, "binary128_layout",
                                   SIZE_MAX, SIZE_MAX, __LINE__);
    /* M24: direct and first-class indirect scalar calls share the same
     * eight integer-register ABI. Aggregate, FP, overflow arguments and
     * unsupported variadic tail types are still rejected before emission. */
    reject_stage = "call_preflight";
    for (i = 0U; i < fn->instruction_count; ++i) {
        reject_ins = i;
        const MinicCoreInstruction *ins = &fn->instructions[i];
        const MinicType *parameter_types;
        MinicType return_type;
        size_t j, begin, count, parameter_count;
        bool is_variadic;
        if (ins->kind != MINIC_CORE_INSTRUCTION_CALL &&
            ins->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL) continue;
        has_direct_calls = true; /* Also preserve the link register for blr/jirl. */
        /* GNU div_t/ldiv_t and similar small integer-only aggregate
         * results travel in one or two GP registers. Core represents their
         * destination as a result_object, NOT a scalar SSA result value.
         * Keep large/FP-class/indirect aggregate returns unsupported. */
        bool small_record_result = false;
        size_t return_record_bytes = 0U, return_record_slots = 0U;
        if (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
            minic_type_is_record(ins->type) &&
            ins->value.call.result_object < fn->object_count &&
            ins->result == MINIC_CORE_VALUE_INVALID &&
            fn->objects[ins->value.call.result_object].element_count == 1U &&
            minic_type_equal(fn->objects[ins->value.call.result_object].type,
                             ins->type) &&
            minic_multi_gp_record(program, target, ins->type,
                                  &return_record_bytes, &return_record_slots) &&
            return_record_bytes > 0U && return_record_bytes <= 16U &&
            return_record_slots >= 1U && return_record_slots <= 2U)
            small_record_result = true;
        if (!small_record_result &&
            ((minic_type_is_void(ins->type)
             ? ins->result != MINIC_CORE_VALUE_INVALID
             : ins->result == MINIC_CORE_VALUE_INVALID ||
               ins->result >= fn->value_count ||
               !minic_type_equal(fn->values[ins->result].type, ins->type)) ||
            !minic_lp64_valid_return_type(program, target, ins->type))) {
            if (getenv("MINIC_BACKEND_CENSUS") != NULL)
                (void)fprintf(stderr,
                    "MINIC_CALL_RESULT_REJECT arch=aarch64 function=%s "
                    "instruction=%zu return_base=%d is_float128=%d "
                    "return_valid=%d value_match=%d\n",
                    name, i, (int)ins->type.base_kind,
                    minic_type_is_float128(ins->type) ? 1 : 0,
                    minic_lp64_valid_return_type(program, target, ins->type) ? 1 : 0,
                    ins->result < fn->value_count &&
                    minic_type_equal(fn->values[ins->result].type, ins->type) ? 1 : 0);
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_CALL) {
            const MinicCoreCallee *callee;
            if (ins->value.call.callee_id >= fn->callee_count ||
                (ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID &&
                 !small_record_result))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            callee = &fn->callees[ins->value.call.callee_id];
            if (callee->name == NULL || callee->name[0] == '\0')
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            begin = ins->value.call.argument_begin;
            count = ins->value.call.argument_count;
            return_type = callee->return_type;
            parameter_types = callee->parameter_types;
            parameter_count = callee->parameter_count;
            is_variadic = callee->is_variadic;
        } else {
            const MinicCoreCallSignature *sig;
            MinicType pointee;
            const MinicCoreValueId function_pointer =
                ins->value.indirect_call.callee;
            if (ins->value.indirect_call.signature_id >= fn->call_signature_count ||
                function_pointer >= fn->value_count ||
                !minic_type_pointee(fn->values[function_pointer].type, &pointee) ||
                !minic_type_is_function(pointee))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            sig = &fn->call_signatures[ins->value.indirect_call.signature_id];
            if (pointee.function_type_id != sig->function_type_id)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            begin = ins->value.indirect_call.argument_begin;
            count = ins->value.indirect_call.argument_count;
            return_type = sig->return_type;
            parameter_types = sig->parameter_types;
            parameter_count = sig->parameter_count;
            is_variadic = sig->is_variadic;
        }
        /* Batch Lua triage: expose actual ABI gate dimensions rather than
         * spending a runner cycle on each failed translation unit. */
        if (getenv("MINIC_BACKEND_CENSUS") != NULL && count > 8U)
            (void)fprintf(stderr,
                          "MINIC_BACKEND_CALL_GATE arch=aarch64 function=%s "
                          "instruction=%zu count=%zu fixed=%zu variadic=%d "
                          "indirect=%d return_valid=%d\n",
                          name, i, count, parameter_count,
                          is_variadic ? 1 : 0,
                          ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL ? 1 : 0,
                          minic_lp64_valid_return_type(program, target, return_type) ? 1 : 0);
        if (!minic_type_equal(return_type, ins->type) ||
            /* Fixed binary64 call results are transferred from the target FP
             * ABI result register; aggregate and other types still fail closed. */
            (!small_record_result &&
             !minic_lp64_valid_return_type(program, target, return_type)) ||
            /* Original BusyBox ascii has a 24-argument printf. The existing
             * AAPCS64/LP64D call emitter already classifies outgoing GP/FP
             * and spill arguments by cursor, without fixed 16-element
             * arrays. Admit bounded larger calls, keeping frame/argument
             * index overflow checks fail-closed. */
            count > 64U || parameter_count > count ||
            (!is_variadic && parameter_count != count) ||
            begin > fn->call_argument_count ||
            count > fn->call_argument_count - begin ||
            (parameter_count != 0U && parameter_types == NULL))
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        for (j = 0U; j < count; ++j) {
            const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
            MinicType actual;
            size_t actual_width;
            bool actual_signed;
            if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                const MinicCoreObjectId object_id = arg->value.object_id;
                size_t record_size, record_slots;
                /* Original BusyBox ipcrm/ipcs pass union semun by
                 * value to variadic semctl(). A single integer-class
                 * eightbyte uses the ordinary GP/overflow integer ABI,
                 * including when the argument is beyond the fixed list.
                 * Larger variadic aggregates and FP/HFA records remain
                 * explicitly unsupported rather than guessed. */
                if (object_id >= fn->object_count ||
                    fn->objects[object_id].element_count != 1U ||
                    (j < parameter_count &&
                     !minic_type_equal(fn->objects[object_id].type,
                                       parameter_types[j])) ||
                    (j >= parameter_count && !is_variadic) ||
                    (!minic_multi_gp_record(program, target,
                                            fn->objects[object_id].type,
                                            &record_size, &record_slots) &&
                     (j >= parameter_count ||
                      !minic_aarch64_indirect_record(program, target,
                             fn->objects[object_id].type, &record_size))) ||
                    (j >= parameter_count &&
                     (record_size > 8U || record_slots != 1U)))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                continue;
            }
            if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                arg->value.value_id >= fn->value_count) {
                if (getenv("MINIC_BACKEND_CENSUS") != NULL)
                    (void)fprintf(stderr,
                        "MINIC_SQLITE_CALL_ARGUMENT arch=aarch64 function=%s argument=%zu arg_kind=%d fixed=%zu total=%zu expected_base=%d\n",
                        name, j, (int)arg->kind, parameter_count, count,
                        j < parameter_count ? (int)parameter_types[j].base_kind : -1);
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            }
            actual = fn->values[arg->value.value_id].type;
            if (!minic_lp64_valid_parameter_type(program, target, actual)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (j < parameter_count) {
                if (!minic_lp64_valid_parameter_type(program, target, parameter_types[j]) ||
                    !minic_type_equal(parameter_types[j], actual))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            } else {
                /* Lua source uses size_t/long-long variadic arguments.
                 * Preserve 32-bit promotions and classify full LP64
                 * integer values in the native general-purpose registers. */
                if (!is_variadic ||
                    (!minic_type_is_long_double(actual) &&
                     !minic_type_is_double(actual) &&
                     !minic_type_is_pointer(actual) &&
                     (!minic_lp64_scalar_integer_width(program, target, actual,
                                            &actual_width, &actual_signed) ||
                      (actual_width != 4U && actual_width != 8U)))) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
        }
    }
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=aarch64 fn=%s stage=calls_valid object_end=%zu\n",
                      name, object_end);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=aarch64 stage=calls_ok\n");
    reject_stage = "frame_finalization";
    reject_ins = SIZE_MAX;
    save_offset = (object_end + 7U) & ~(size_t)7U;
    if (va_count != 0U) {
        if (save_offset > MINIC_LARGE_FRAME_MAX - 240U)
            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                       reject_stage, reject_block,
                                       reject_ins, __LINE__);
        va_base = (save_offset + 15U) & ~(size_t)15U;
        /* 32-byte va_list tag, 8 gp slots, 8 x 16-byte fp slots. */
        save_offset = va_base + 224U;
    }
    if (save_offset > MINIC_LARGE_FRAME_MAX - (has_direct_calls ? 8U : 0U) -
                      (dynamic_frame ? 8U : 0U))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    frame = (save_offset + (has_direct_calls ? 8U : 0U) +
             (dynamic_frame ? 8U : 0U) + 15U) & ~(size_t)15U;
    if (frame > MINIC_LARGE_FRAME_MAX) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_CAP arch=aarch64 function=%s frame=%zu end=%zu\n",
                          name, frame, object_end);
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
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
    for (size_t block_id = 0U; block_id < fn->block_count; ++block_id) {
        reject_block = block_id;
        reject_ins = SIZE_MAX;
        reject_stage = "machine_emit";
        block = &fn->blocks[block_id];
        if (fprintf(out, ".Lmini_arm_f%zu_b%zu:\n", id, block_id) < 0 ||
            (block->source_label_id != SIZE_MAX &&
             !fn->suppress_source_label_aliases &&
             fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0))
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        for (i = 0U; i < block->instruction_count; ++i) {
        MinicCoreInstructionId ins_id = block->instructions[i];
        const MinicCoreInstruction *ins;
        size_t offset;
        if (ins_id >= fn->instruction_count) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        ins = &fn->instructions[ins_id];
        reject_ins = (size_t)ins_id;
        if (trace_cjson)
            (void)fprintf(stderr,
                "MINIC_CJSON_STEP arch=aarch64 function=%s block=%zu ins=%u kind=%d result=%u type_integer=%d type_pointer=%d\n",
                name, block_id, (unsigned int)ins_id, (int)ins->kind,
                (unsigned int)ins->result,
                minic_type_is_integer(ins->type) ? 1 : 0,
                minic_type_is_pointer(ins->type) ? 1 : 0);
        /* Only recorded in the temporary original-source failure log,
         * never in normal builds or public diagnostic stream. */
        if (getenv("MINIC_M1_TRACE") != NULL &&
            ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
            (void)fprintf(stderr,
                          "MINIC_M9_STEP arch=aarch64 function=%s block=%zu ins=%u kind=%d result=%u type_int=%d type_ptr=%d\n",
                          name, block_id, (unsigned int)ins_id,
                          (int)ins->kind, (unsigned int)ins->result,
                          minic_type_is_integer(ins->type) ? 1 : 0,
                          minic_type_is_pointer(ins->type) ? 1 : 0);
        /* A Core compiler barrier is an effect-only ordering constraint
         * (GNU empty asm with a memory clobber), not a hardware fence and
         * never an SSA value. Like the existing x86/RV64 backends, preserve
         * source instruction order and emit no machine instruction. The
         * previous result-slot gate rejected this valid Core kind in the
         * original BusyBox test/gzip/netstat/top applets on both ISAs. */
        if (ins->kind == MINIC_CORE_INSTRUCTION_COMPILER_BARRIER) {
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                !minic_type_is_void(ins->type))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            continue;
        }
        /* M23: copy the *exact* target-sized aggregate bytes between Core
         * addresses, preserving padding and nested hooks (no re-layout).
         * An effect-only record copy has no SSA result slot. */
        if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_COPY) {
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
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (fprintf(out, "  ldr x10, [sp, #%zu]\n"
                             "  ldr x11, [sp, #%zu]\n",
                        (size_t)dst * 8U, (size_t)src * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            for (size_t byte = 0U; byte < bytes; ++byte)
                if (fputs("  ldrb w9, [x11]\n"
                          "  strb w9, [x10]\n"
                          "  add x11, x11, #1\n"
                          "  add x10, x10, #1\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
            const size_t position = ins->value.parameter_object.parameter_index;
            const MinicCoreObjectId obj_id = ins->value.parameter_object.object_id;
            size_t size = 0U, slots = 0U, object_offset, gp = 0U, fp = 0U;
            size_t stack = 0U, k, chunk;
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                position >= fn->parameter_count ||
                obj_id >= fn->object_count ||
                !minic_type_equal(fn->objects[obj_id].type,
                                  fn->parameter_types[position]) ||
                fn->objects[obj_id].element_count != 1U ||
                (!minic_multi_gp_record(program, target,
                         fn->parameter_types[position], &size, &slots) &&
                 !minic_aarch64_indirect_record(program, target,
                         fn->parameter_types[position], &size)) ||
                !minic_lp64_frame_objects(program, target, fn, obj_id,
                               &object_offset, NULL))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            for (k = 0U; k < position; ++k) {
                const MinicType previous = fn->parameter_types[k];
                size_t other_size, other_slots;
                if (minic_multi_gp_record(program, target, previous,
                                          &other_size, &other_slots)) {
                    if (gp + other_slots <= 8U) gp += other_slots;
                    else { gp = 8U; stack += other_slots; }
                } else if (minic_type_is_double(previous) ) {
                    if (fp++ >= 8U) ++stack;
                } else {
                    if (gp++ >= 8U) ++stack;
                }
            }
            if (slots == 0U) {
                /* Composite by-reference parameter: copy the caller-owned
                 * bytes to this callee's independently addressable object. */
                if ((gp < 8U
                     ? fprintf(out, "  mov x10, x%zu\n", gp) < 0
                     : fprintf(out, "  ldr x10, [sp, #%zu]\n",
                               frame + stack * 8U) < 0) ||
                    fprintf(out, "  movz x11, #%zu\n",
                            object_offset & 65535U) < 0 ||
                    (object_offset > 65535U &&
                     fprintf(out, "  movk x11, #%zu, lsl #16\n",
                             object_offset >> 16U) < 0) ||
                    fputs("  add x11, sp, x11\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                for (k = 0U; k < size; ++k) {
                    if (fputs("  ldrb w9, [x10], #1\n"
                              "  strb w9, [x11], #1\n", out) == EOF)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                }
                continue;
            }
            for (chunk = 0U; chunk < slots; ++chunk) {
                const bool in_reg = gp + slots <= 8U;
                const size_t payload = size - chunk * 8U >= 8U
                    ? 8U : size - chunk * 8U;
                if (!emit_lp64_record_chunk_parameter(out,
                        object_offset + chunk * 8U, payload,
                        in_reg, gp + chunk, frame + (stack + chunk) * 8U))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
            }
            continue;
        }
        /* Preserve both halves of volatile/nonvolatile int128 stores.
         * Unlike atomic stores this is two plain 64-bit memory writes. */
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            ins->value.store.stored_value < fn->value_count &&
            minic_type_is_int128_integer(
                fn->values[ins->value.store.stored_value].type)) {
            const MinicCoreValueId addr = ins->value.store.address;
            const MinicCoreValueId value = ins->value.store.stored_value;
            size_t src;
            if (addr >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                !minic_lp64_binary128_slot(fn, ld_base, value, &src) ||
                fprintf(out, "  ldr x10, [sp, #%zu]\n"
                             "  ldr x9, [sp, #%zu]\n"
                             "  ldr x11, [sp, #%zu]\n"
                             "  str x9, [x10]\n"
                             "  str x11, [x10, #8]\n",
                        (size_t)addr * 8U, src, src + 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            ins->value.store.stored_value < fn->value_count &&
            minic_type_is_long_double(
                fn->values[ins->value.store.stored_value].type)) {
            const MinicCoreValueId addr = ins->value.store.address;
            const MinicCoreValueId value = ins->value.store.stored_value;
            size_t src;
            if (addr >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                !minic_lp64_binary128_slot(fn, ld_base, value, &src) ||
                fprintf(out, "  ldr x10, [sp, #%zu]\n"
                             "  ldr q9, [sp, #%zu]\n"
                             "  str q9, [x10]\n",
                        (size_t)addr * 8U, src) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE ||
            ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE) {
            const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
            const MinicCoreValueId addr = atomic ? ins->value.atomic_store.address
                                                  : ins->value.store.address;
            const MinicCoreValueId value = atomic ? ins->value.atomic_store.stored_value
                                                   : ins->value.store.stored_value;
            MinicType pointee;
            size_t width = 0U, pointee_width = 0U, pointee_align = 0U;
            bool sign = false, pointer;
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                addr >= fn->value_count || value >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            pointer = minic_type_is_pointer(fn->values[value].type);
            /* Ordinary naturally aligned scalar loads/stores are the
             * relaxed atomic primitive on these LP64 ISAs. Preserve the
             * Core memory effect, but reject every stronger memory order
             * and non-lock-free aggregate/FP operand explicitly. */
            if (atomic &&
                (ins->value.atomic_store.memory_order != 0U ||
                 !minic_type_pointee(fn->values[addr].type, &pointee) ||
                 !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
                 !minic_data_layout_type(target->data_layout, program,
                                         pointee, &pointee_width, &pointee_align)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                          reject_block, reject_ins, __LINE__);

            if (!pointer && !minic_type_is_double(fn->values[value].type) &&
                !minic_type_is_float(fn->values[value].type) &&
                !minic_lp64_scalar_integer_width(program, target, fn->values[value].type,
                                      &width, &sign)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (minic_type_is_double(fn->values[value].type)) width = 8U;
            if (minic_type_is_float(fn->values[value].type)) width = 4U;
            if (atomic &&
                (minic_type_is_double(fn->values[value].type) ||
                 minic_type_is_float(fn->values[value].type) ||
                 (pointer ? pointee_width != 8U :
                  (width != 1U && width != 2U &&
                   width != 4U && width != 8U) ||
                  width != pointee_width)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                          reject_block, reject_ins, __LINE__);
            (void)pointee_align;
            if (fprintf(out, "  ldr x10, [sp, #%zu]\n", (size_t)addr * 8U) < 0 ||
                fprintf(out, (pointer || width == 8U) ? "  ldr x9, [sp, #%zu]\n"
                                                : "  ldr w9, [sp, #%zu]\n",
                        (size_t)value * 8U) < 0 ||
                (pointer ? fputs("  str x9, [x10]\n", out) == EOF
                         : !emit_integer_memory(out, true, width, sign))) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        if ((ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
             ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) &&
            (minic_type_is_void(ins->type) ||
             (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
              minic_type_is_record(ins->type) &&
              ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID))) {
            /* Verified in call preflight: effect-only calls
             * has no Core result slot. Fall through into the call case. */
            offset = 0U;
        } else {
            if (ins->result == MINIC_CORE_VALUE_INVALID ||
                ins->result >= fn->value_count ||
                ((ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
                   ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS)
                    ? !minic_type_is_pointer(ins->type)
                    : !minic_type_is_integer(ins->type) &&
                      !minic_type_is_pointer(ins->type) &&
                      !(minic_type_is_float(ins->type) &&
                        (ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                         ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE)) &&
                      !(minic_type_is_long_double(ins->type) &&
                        (ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                         ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL)) &&
                      !(minic_type_is_double(ins->type) &&
                        (ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY ||
                         ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                         ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE ||
                         ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR ||
                         ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL))))
            {
                if (getenv("MINIC_M1_TRACE") != NULL)
                    (void)fprintf(stderr,
                                  "MINIC_M8_RESULT_REJECT arch=aarch64 function=%s block=%zu instruction=%u kind=%d result=%u type_integer=%d type_pointer=%d\n",
                                  name, block_id, (unsigned int)ins_id,
                                  (int)ins->kind, (unsigned int)ins->result,
                                  minic_type_is_integer(ins->type) ? 1 : 0,
                                  minic_type_is_pointer(ins->type) ? 1 : 0);
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            offset = (size_t)ins->result * 8U;
        }
        switch (ins->kind) {
        /* Ordered IEEE comparisons: AArch64 FCMP sets C=1 on unordered.
         * 'lt' (N!=V) is WRONG for NaN. Use unsigned lo/ls conditions:
         * those are true only for ordered < or <= respectively. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *condition = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
                ? "eq" : ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ? "lo" : "ls";
            if (!minic_type_equal(ins->type, minic_type_int()) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_double(fn->values[lhs].type) ||
                !minic_type_is_double(fn->values[rhs].type) ||
                fprintf(out,
                        "  ldr d9, [sp, #%zu]\n"
                        "  ldr d10, [sp, #%zu]\n"
                        "  fcmp d9, d10\n"
                        "  cset w9, %s\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U,
                        condition) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        /* M37: cJSON compare_double's a-b and tolerance multiplication.
         * Keep full IEEE-754 binary64 payloads in FP registers/SSA slots. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD
                ? "  fadd d9, d9, d10\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT
                    ? "  fsub d9, d9, d10\n"
                    : "  fmul d9, d9, d10\n";
            if (!minic_type_is_double(ins->type) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_double(fn->values[lhs].type) ||
                !minic_type_is_double(fn->values[rhs].type) ||
                fprintf(out, "  ldr d9, [sp, #%zu]\n"
                             "  ldr d10, [sp, #%zu]\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0 ||
                fputs(op, out) == EOF ||
                fprintf(out, "  str d9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_type_is_double(ins->type) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_double(fn->values[lhs].type) ||
                !minic_type_is_double(fn->values[rhs].type) ||
                fprintf(out,
                        "  ldr d9, [sp, #%zu]\n"
                        "  ldr d10, [sp, #%zu]\n"
                        "  fdiv d9, d9, d10\n"
                        "  str d9, [sp, #%zu]\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue; /* Result is already in its full 64-bit SSA slot. */
        }
        /* Unary IEEE-754 sign flip includes -0 and all NaN payloads. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            if (!minic_type_is_double(ins->type) ||
                operand >= fn->value_count ||
                !minic_type_is_double(fn->values[operand].type) ||
                fprintf(out, "  ldr d9, [sp, #%zu]\n"
                             "  fneg d9, d9\n"
                             "  str d9, [sp, #%zu]\n",
                        (size_t)operand * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT: {
            if (minic_type_is_long_double(ins->type)) {
                size_t dst, half;
                if (!minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                for (half = 0U; half < 2U; ++half) {
                    const uint64_t bits = half == 0U
                        ? ins->value.floating128_bits.low
                        : ins->value.floating128_bits.high;
                    unsigned int shift;
                    if (fprintf(out, "  movz x9, #%" PRIu64 "\n",
                                bits & UINT64_C(0xffff)) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                    for (shift = 16U; shift <= 48U; shift += 16U)
                        if (((bits >> shift) & UINT64_C(0xffff)) != 0U &&
                            fprintf(out, "  movk x9, #%" PRIu64
                                         ", lsl #%u\n",
                                    (bits >> shift) & UINT64_C(0xffff),
                                    shift) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                    if (fprintf(out, "  str x9, [sp, #%zu]\n",
                                dst + half * 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            const uint64_t bits = ins->value.floating_bits;
            unsigned int shift;
            if ((!minic_type_is_double(ins->type) &&
                 !minic_type_is_float(ins->type)) ||
                fprintf(out, "  movz x9, #%" PRIu64 "\n",
                        bits & UINT64_C(0xffff)) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            for (shift = 16U; shift <= 48U; shift += 16U) {
                if (((bits >> shift) & UINT64_C(0xffff)) != 0U &&
                    fprintf(out, "  movk x9, #%" PRIu64 ", lsl #%u\n",
                            (bits >> shift) & UINT64_C(0xffff), shift) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT: {
            size_t width = 0U;
            bool sign = false;
            /* Match x86 Core constant semantics: the destination integer
             * type determines narrowing, not the original int64 payload.
             * Lua exercises out-of-range literals in cast/enum contexts. */
            (void)minic_lp64_scalar_integer_width(program, target, ins->type,
                                       &width, &sign);
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
                if (width == 0U &&
                    (ins->value.integer_value < INT32_MIN ||
                     ins->value.integer_value > INT32_MAX)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                bits = (uint32_t)(int32_t)ins->value.integer_value;
                if (fprintf(out, "  movz w9, #%" PRIu32 "\n",
                            bits & UINT32_C(0xffff)) < 0 ||
                    ((bits >> 16U) != 0U &&
                     fprintf(out, "  movk w9, #%" PRIu32 ", lsl #16\n",
                             bits >> 16U) < 0)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                if ((width == 1U || width == 2U) &&
                    !emit_integer_normalize(out, width, sign))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            }
            (void)sign;
            break;
        }
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
            const char *symbol;
            if (ins->value.global_id >= fn->global_count ||
                fn->globals[ins->value.global_id].name == NULL)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            symbol = fn->globals[ins->value.global_id].name;
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
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
            const MinicCoreFunctionSymbolId symbol_id =
                ins->value.function_symbol_id;
            const char *symbol;
            if (symbol_id >= fn->function_symbol_count ||
                !minic_type_is_pointer(ins->type)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            symbol = fn->function_symbols[symbol_id].name;
            if (symbol == NULL || symbol[0] == '\0')
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
            continue;
        }
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
            size_t object_offset;
            if (ins->value.object_id >= fn->object_count ||
                !minic_lp64_frame_objects(program, target, fn,
                               (size_t)ins->value.object_id,
                               &object_offset, NULL) ||
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
            continue;
        }

        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
            /* Core already computed the DataLayout record byte displacement.
             * Large zlib structures exceed the ISA add-immediate range. */
            const MinicCoreValueId base = ins->value.field_address.base;
            const size_t bytes = ins->value.field_address.byte_offset;
            if (base >= fn->value_count ||
                !minic_type_is_pointer(fn->values[base].type) ||
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
            if (base >= fn->value_count || index >= fn->value_count ||
                !minic_type_is_pointer(fn->values[base].type) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[index].type,
                                     &iw, &signed_index) ||
                stride > 65535U ||
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
            /* Original AES_ECB_encrypt converts uint8_t* to state_t*.
             * Both are LP64 pointer representations: preserve all 64 bits.
             * Do NOT incorrectly turn integer/pointer casts into a no-op. */
            const MinicCoreValueId source = ins->value.operand;
            size_t src_width = 0U;
            bool src_signed = false;
            bool src_pointer;
            /* M35: cJSON parse_number computes after_end - number_c_string.
             * Preserve all LP64 pointer bits for signed/unsigned long casts.
             * Sub-width truncation remains fail-closed until tested. */
            if (source < fn->value_count &&
                minic_type_is_pointer(fn->values[source].type) &&
                minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!minic_core_scalar_bitcast_types_valid(
                        ins->type, fn->values[source].type) ||
                    !minic_lp64_scalar_integer_width(program, target, ins->type,
                                          &width, &sign) || width != 8U ||
                    fprintf(out, "  ldr x9, [sp, #%zu]\n",
                            (size_t)source * 8U) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (source >= fn->value_count ||
                !minic_type_is_pointer(ins->type) ||
                !minic_core_scalar_bitcast_types_valid(
                    ins->type, fn->values[source].type)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            src_pointer = minic_type_is_pointer(fn->values[source].type);
            if (!src_pointer &&
                !minic_lp64_scalar_integer_width(program, target,
                                      fn->values[source].type,
                                     &src_width, &src_signed)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (fprintf(out, (src_pointer || src_width == 8U)
                          ? "  ldr x9, [sp, #%zu]\n"
                          : "  ldr w9, [sp, #%zu]\n",
                        (size_t)source * 8U) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (!src_pointer && src_width != 8U &&
                (!emit_integer_normalize(out, src_width, src_signed) ||
                 fputs(src_signed ? "  sxtw x9, w9\n"
                                  : "  uxtw x9, w9\n", out) == EOF))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
            /* Complete 32/64-bit signed+unsigned GNU checked arithmetic.
             * Result bytes are written through Core's pointer; x9 carries
             * the boolean status to the normal SSA destination afterward. */
            const MinicCoreValueId lhs = ins->value.integer_overflow.left;
            const MinicCoreValueId rhs = ins->value.integer_overflow.right;
            const MinicCoreValueId address = ins->value.integer_overflow.result_address;
            MinicType value_type;
            size_t width;
            bool is_signed;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                address >= fn->value_count ||
                !minic_type_pointee(fn->values[address].type, &value_type) ||
                !minic_type_is_integer(value_type) ||
                minic_type_is_bool_integer(value_type) ||
                minic_type_is_const(value_type) ||
                minic_type_is_volatile(value_type) ||
                !minic_type_equal(value_type, fn->values[lhs].type) ||
                !minic_type_equal(value_type, fn->values[rhs].type) ||
                !minic_lp64_scalar_integer_width(program, target, value_type,
                                      &width, &is_signed) ||
                (width != 4U && width != 8U))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            const bool wide = width == 8U;
            const int op = ins->value.integer_overflow.operator_kind;
            if (fprintf(out, wide
                      ? "  ldr x9, [sp, #%zu]\n  ldr x10, [sp, #%zu]\n"
                      : "  ldr w9, [sp, #%zu]\n  ldr w10, [sp, #%zu]\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0 ||
                fprintf(out, "  ldr x11, [sp, #%zu]\n",
                        (size_t)address * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (op == MINIC_CORE_INTEGER_OVERFLOW_ADD ||
                op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
                const bool sub = op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT;
                const char *instruction = wide
                    ? (sub ? "  subs x9, x9, x10\n"
                           : "  adds x9, x9, x10\n")
                    : (sub ? "  subs w9, w9, w10\n"
                           : "  adds w9, w9, w10\n");
                const char *flag = is_signed ? "vs" : sub ? "cc" : "cs";
                if (fputs(instruction, out) == EOF ||
                    fprintf(out, "  cset w10, %s\n", flag) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
            } else if (op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
                if (wide) {
                    if (fputs(is_signed ? "  smulh x12, x9, x10\n"
                                        : "  umulh x12, x9, x10\n", out) == EOF ||
                        fputs("  mul x9, x9, x10\n", out) == EOF ||
                        fputs(is_signed
                             ? "  asr x10, x9, #63\n  cmp x12, x10\n"
                             : "  cmp x12, #0\n", out) == EOF)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                } else if (fputs(is_signed
                            ? "  smull x12, w9, w10\n"
                              "  sxtw x10, w12\n  cmp x12, x10\n"
                            : "  umull x12, w9, w10\n"
                              "  lsr x10, x12, #32\n  cmp x10, #0\n",
                            out) == EOF ||
                           fputs("  mov w9, w12\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                if (fputs("  cset w10, ne\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
            } else return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (fputs(wide ? "  str x9, [x11]\n"
                          : "  str w9, [x11]\n", out) == EOF ||
                fputs("  mov w9, w10\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
        case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
            const MinicCoreValueId operand = ins->value.operand;
            unsigned int width;
            if (operand >= fn->value_count ||
                !minic_type_is_integer(ins->type) ||
                !minic_type_is_unsigned_integer(fn->values[operand].type) ||
                !minic_target_info_integer_width(target, program,
                          fn->values[operand].type, &width) ||
                (width != 8U && width != 16U &&
                 width != 32U && width != 64U) ||
                fprintf(out, width == 64U
                           ? "  ldr x9, [sp, #%zu]\n"
                           : "  ldr w9, [sp, #%zu]\n",
                        (size_t)operand * 8U) < 0 ||
                (width != 64U && width < 32U &&
                 fprintf(out, "  and w9, w9, #%u\n",
                         (1U << width) - 1U) < 0))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CTZ &&
                fputs(width == 64U ? "  rbit x9, x9\n"
                                   : "  rbit w9, w9\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (fputs(width == 64U ? "  clz x9, x9\n"
                                   : "  clz w9, w9\n", out) == EOF ||
                (width < 32U &&
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ &&
                 fprintf(out, "  sub w9, w9, #%u\n", 32U - width) < 0))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_LOAD:
        case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD: {
            if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                minic_type_is_int128_integer(ins->type)) {
                const MinicCoreValueId addr = ins->value.load.address;
                size_t dst;
                if (addr >= fn->value_count ||
                    !minic_type_is_pointer(fn->values[addr].type) ||
                    !minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst) ||
                    fprintf(out, "  ldr x10, [sp, #%zu]\n"
                                 "  ldr x9, [x10]\n"
                                 "  ldr x11, [x10, #8]\n"
                                 "  str x9, [sp, #%zu]\n"
                                 "  str x11, [sp, #%zu]\n",
                            (size_t)addr * 8U, dst, dst + 8U) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                continue;
            }
            if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                minic_type_is_long_double(ins->type)) {
                const MinicCoreValueId addr = ins->value.load.address;
                size_t dst;
                if (addr >= fn->value_count ||
                    !minic_type_is_pointer(fn->values[addr].type) ||
                    !minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst) ||
                    fprintf(out, "  ldr x10, [sp, #%zu]\n"
                                 "  ldr q9, [x10]\n"
                                 "  str q9, [sp, #%zu]\n",
                            (size_t)addr * 8U, dst) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                continue;
            }
            const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
            const MinicCoreValueId addr = atomic ? ins->value.atomic_load.address
                                                  : ins->value.load.address;
            MinicType pointee;
            size_t pointee_width = 0U, pointee_align = 0U;
            size_t width = minic_type_is_double(ins->type) ? 8U :
                           minic_type_is_float(ins->type) ? 4U : 0U;
            bool sign = false, pointer = minic_type_is_pointer(ins->type);
            if (atomic &&
                (ins->value.atomic_load.memory_order != 0U ||
                 addr >= fn->value_count ||
                 !minic_type_pointee(fn->values[addr].type, &pointee) ||
                 !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
                 !minic_data_layout_type(target->data_layout, program,
                                         pointee, &pointee_width, &pointee_align) ||
                 (pointer ? pointee_width != 8U :
                     !minic_lp64_scalar_integer_width(program, target, ins->type,
                                           &width, &sign) ||
                     (width != 1U && width != 2U &&
                      width != 4U && width != 8U) ||
                     pointee_width != width)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                          reject_block, reject_ins, __LINE__);
            (void)pointee_align;
            if (addr >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                (!pointer && !minic_type_is_double(ins->type) &&
                 !minic_type_is_float(ins->type) &&
                 !minic_lp64_scalar_integer_width(program, target,
                                       ins->type, &width, &sign)) ||
                fprintf(out, "  ldr x10, [sp, #%zu]\n", (size_t)addr * 8U) < 0 ||
                (pointer ? fputs("  ldr x9, [x10]\n", out) == EOF
                         : !emit_integer_memory(out, false, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_CALL:
        case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
            const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
            const size_t begin = indirect ? ins->value.indirect_call.argument_begin
                                          : ins->value.call.argument_begin;
            const size_t count = indirect ? ins->value.indirect_call.argument_count
                                          : ins->value.call.argument_count;
            size_t j, gp_index = 0U, fp_index = 0U, overflow_slots = 0U;
            size_t stack_index = 0U, stack_bytes;
            /* AAPCS64 has independent x0-x7/v0-v7 argument banks.
             * Overflow arguments follow source order at the caller's SP;
             * reserve this area temporarily below the existing SSA frame.
             * All SSA reads below use the shifted SP-relative address. */
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots;
                    const MinicCoreObject *object =
                        &fn->objects[arg->value.object_id];
                    if (!minic_multi_gp_record(program, target, object->type,
                                               &record_size, &slots)) {
                        if (!minic_aarch64_indirect_record(program, target,
                                                         object->type, &record_size))
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                reject_stage, reject_block, reject_ins, __LINE__);
                        if (gp_index++ >= 8U) ++overflow_slots;
                        continue;
                    }
                    if (gp_index + slots <= 8U)
                        gp_index += slots;
                    else {
                        gp_index = 8U;
                        overflow_slots += slots;
                    }
                    continue;
                }
                const MinicType type = fn->values[arg->value.value_id].type;
                if (minic_type_is_long_double(type)) {
                    if (fp_index++ >= 8U) {
                        overflow_slots += overflow_slots & 1U;
                        overflow_slots += 2U;
                    }
                } else if (minic_type_is_double(type) || minic_type_is_float(type)) {
                    if (fp_index++ >= 8U) ++overflow_slots;
                } else if (gp_index++ >= 8U) ++overflow_slots;
            }
            stack_bytes = (overflow_slots * 8U + 15U) & ~(size_t)15U;
            if (stack_bytes != 0U &&
                fprintf(out, "  sub sp, sp, #%zu\n", stack_bytes) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            gp_index = fp_index = 0U;
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots, object_offset, chunk;
                    const MinicCoreObjectId object_id = arg->value.object_id;
                    if (!minic_lp64_frame_objects(program, target, fn, object_id,
                                       &object_offset, NULL))
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                              reject_stage, reject_block, reject_ins, __LINE__);
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
                            (gp_index < 8U
                             ? fprintf(out, "  mov x%zu, x9\n", gp_index) < 0
                             : fprintf(out, "  str x9, [sp, #%zu]\n",
                                       stack_index * 8U) < 0))
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                reject_stage, reject_block, reject_ins, __LINE__);
                        if (gp_index++ >= 8U) ++stack_index;
                        continue;
                    }
                    for (chunk = 0U; chunk < slots; ++chunk) {
                        const bool in_reg = gp_index + slots <= 8U;
                        const size_t payload = record_size - chunk * 8U >= 8U
                            ? 8U : record_size - chunk * 8U;
                        if (!emit_lp64_record_chunk_argument(
                                out, object_offset + stack_bytes + chunk * 8U,
                                payload, in_reg, gp_index + chunk,
                                stack_index + chunk))
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                  reject_stage, reject_block, reject_ins, __LINE__);
                    }
                    if (gp_index + slots <= 8U)
                        gp_index += slots;
                    else {
                        gp_index = 8U;
                        stack_index += slots;
                    }
                    continue;
                }
                const MinicType type = fn->values[arg->value.value_id].type;
                const size_t slot = (size_t)arg->value.value_id * 8U + stack_bytes;
                if (minic_type_is_long_double(type)) {
                    size_t src;
                    if (!minic_lp64_binary128_slot(fn, ld_base, arg->value.value_id, &src))
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                    src += stack_bytes;
                    if (fp_index < 8U) {
                        if (fprintf(out, "  ldr q%zu, [sp, #%zu]\n",
                                    fp_index, src) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                    } else {
                        stack_index += stack_index & 1U;
                        if (fprintf(out, "  ldr q9, [sp, #%zu]\n"
                                         "  str q9, [sp, #%zu]\n",
                                    src, stack_index * 8U) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                        stack_index += 2U;
                    }
                    ++fp_index;
                } else if (minic_type_is_double(type) || minic_type_is_float(type)) {
                    const bool is_float = minic_type_is_float(type);
                    if (fp_index < 8U) {
                        if (fprintf(out, is_float ? "  ldr s%zu, [sp, #%zu]\n"
                                                  : "  ldr d%zu, [sp, #%zu]\n",
                                    fp_index, slot) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    } else if (fprintf(out, is_float
                                       ? "  ldr s9, [sp, #%zu]\n"
                                         "  str s9, [sp, #%zu]\n"
                                       : "  ldr d9, [sp, #%zu]\n"
                                         "  str d9, [sp, #%zu]\n",
                                       slot, stack_index++ * 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    ++fp_index;
                } else {
                    const bool wide = minic_lp64_register64_type(program, target, type);
                    if (gp_index < 8U) {
                        if (fprintf(out, wide ? "  ldr x%zu, [sp, #%zu]\n"
                                              : "  ldr w%zu, [sp, #%zu]\n",
                                    gp_index, slot) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    } else if (fprintf(out,
                                       wide ? "  ldr x9, [sp, #%zu]\n"
                                              "  str x9, [sp, #%zu]\n"
                                            : "  ldr w9, [sp, #%zu]\n"
                                              "  str w9, [sp, #%zu]\n",
                                       slot, stack_index++ * 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    ++gp_index;
                }
            }
            if (indirect) {
                if (fprintf(out, "  ldr x16, [sp, #%zu]\n"
                                 "  blr x16\n",
                            (size_t)ins->value.indirect_call.callee * 8U +
                            stack_bytes) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            } else {
                const MinicCoreCallee *callee =
                    &fn->callees[ins->value.call.callee_id];
                if (fprintf(out, "  bl %s\n", callee->name) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            }
            if (stack_bytes != 0U &&
                fprintf(out, "  add sp, sp, #%zu\n", stack_bytes) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (minic_type_is_void(ins->type)) continue;
            if (minic_type_is_record(ins->type)) {
                const MinicCoreObjectId object_id =
                    ins->value.call.result_object;
                size_t bytes, slots, dst, chunk;
                if (indirect || object_id >= fn->object_count ||
                    !minic_multi_gp_record(program, target, ins->type,
                                           &bytes, &slots) ||
                    bytes == 0U || bytes > 16U || slots > 2U ||
                    !minic_lp64_frame_objects(program, target, fn, object_id,
                                   &dst, NULL))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                for (chunk = 0U; chunk < slots; ++chunk) {
                    const size_t payload = bytes - chunk * 8U >= 8U
                        ? 8U : bytes - chunk * 8U;
                    if (!emit_lp64_record_chunk_parameter(out,
                            dst + chunk * 8U, payload, true, chunk, 0U))
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst) ||
                    fprintf(out, "  str q0, [sp, #%zu]\n", dst) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                continue;
            }
            if (fputs(minic_type_is_double(ins->type)
                          ? "  fmov x9, d0\n"
                          : minic_type_is_float(ins->type)
                              ? "  fmov w9, s0\n"
                          : minic_lp64_register64_type(program, target, ins->type)
                              ? "  mov x9, x0\n"
                              : "  mov w9, w0\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!minic_lp64_scalar_integer_width(program, target, ins->type,
                                          &width, &sign) ||
                    (width != 8U && !emit_integer_normalize(out, width, sign)))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            }
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
                 width != 8U && !emit_integer_normalize(out, width, sign)))
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
        case MINIC_CORE_INSTRUCTION_PARAMETER: {
            const size_t ordinal = (size_t)ins->value.parameter_index;
            size_t gp_index = 0U, fp_index = 0U, overflow_index = 0U, k;
            if (ordinal >= fn->parameter_count)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            /* AAPCS64: independent x/d argument cursors, with one unified
             * incoming 8-byte stack-slot sequence for exhausted banks. */
            for (k = 0U; k < ordinal; ++k) {
                size_t record_bytes = 0U, slots = 0U;
                const MinicType prior = fn->parameter_types[k];
                if (minic_multi_gp_record(program, target, prior,
                                          &record_bytes, &slots)) {
                    /* An AAPCS64 integer-class record consumes its entire
                     * 1/2-GP register group, or ALL of its stack slots.
                     * A scalar following a record must start AFTER that
                     * group, never reuse the final record eightbyte. */
                    if (gp_index + slots <= 8U)
                        gp_index += slots;
                    else {
                        gp_index = 8U;
                        overflow_index += slots;
                    }
                } else if (minic_type_is_long_double(prior)) {
                    if (fp_index++ >= 8U) {
                        overflow_index += overflow_index & 1U;
                        overflow_index += 2U;
                    }
                } else if (minic_type_is_double(prior) ||
                           minic_type_is_float(prior)) {
                    if (fp_index++ >= 8U) ++overflow_index;
                } else if (gp_index++ >= 8U) ++overflow_index;
            }
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                if (fp_index < 8U) {
                    if (fprintf(out, "  str q%zu, [sp, #%zu]\n",
                                fp_index, dst) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                } else {
                    overflow_index += overflow_index & 1U;
                    if (fprintf(out, "  ldr q9, [sp, #%zu]\n"
                                     "  str q9, [sp, #%zu]\n",
                                frame + overflow_index * 8U, dst) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            if (minic_type_is_double(ins->type) ||
                minic_type_is_float(ins->type)) {
                const bool is_float = minic_type_is_float(ins->type);
                if (fp_index < 8U) {
                    if (fprintf(out, is_float ? "  str s%zu, [sp, #%zu]\n"
                                              : "  str d%zu, [sp, #%zu]\n",
                                fp_index, offset) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                } else if (fprintf(out, is_float
                                     ? "  ldr s9, [sp, #%zu]\n"
                                       "  str s9, [sp, #%zu]\n"
                                     : "  ldr d9, [sp, #%zu]\n"
                                       "  str d9, [sp, #%zu]\n",
                                   frame + overflow_index * 8U, offset) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                continue;
            }
            if (gp_index < 8U) {
                if (fprintf(out, minic_lp64_register64_type(program, target, ins->type)
                                 ? "  str x%zu, [sp, #%zu]\n"
                                 : "  str w%zu, [sp, #%zu]\n",
                            gp_index, offset) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
            } else if (fprintf(out, minic_lp64_register64_type(program, target, ins->type)
                               ? "  ldr x9, [sp, #%zu]\n"
                                 "  str x9, [sp, #%zu]\n"
                               : "  ldr w9, [sp, #%zu]\n"
                                 "  str w9, [sp, #%zu]\n",
                               frame + overflow_index * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT &&
                minic_type_is_int128_integer(ins->type)) {
                const MinicCoreValueId rhs = ins->value.binary.right;
                size_t src_offset, dst_offset, rw;
                bool rs;
                const bool sign = minic_type_is_signed_integer(ins->type);
                if (lhs >= fn->value_count || rhs >= fn->value_count ||
                    !minic_type_is_int128_integer(fn->values[lhs].type) ||
                    !minic_lp64_scalar_integer_width(program, target,
                        fn->values[rhs].type, &rw, &rs) ||
                    !minic_lp64_binary128_slot(fn, ld_base, lhs, &src_offset) ||
                    !minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst_offset) ||
                    fprintf(out, rw == 8U ?
                            "  ldr x10, [sp, #%zu]\n" :
                            "  ldr w10, [sp, #%zu]\n", (size_t)rhs * 8U) < 0 ||
                    fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                 "  ldr x11, [sp, #%zu]\n",
                            src_offset, src_offset + 8U) < 0 ||
                    fputs("  cbz x10, 2f\n"
                          "  cmp x10, #64\n"
                          "  b.hs 1f\n"
                          "  neg x13, x10\n"
                          "  lsl x13, x11, x13\n"
                          "  lsr x9, x9, x10\n"
                          "  orr x9, x9, x13\n", out) == EOF ||
                    fputs(sign ? "  asr x11, x11, x10\n" :
                                 "  lsr x11, x11, x10\n", out) == EOF ||
                    fputs("  b 2f\n"
                          "1:\n"
                          "  sub x10, x10, #64\n", out) == EOF ||
                    fputs(sign ? "  asr x9, x11, x10\n"
                                 "  asr x11, x11, #63\n" :
                                 "  lsr x9, x11, x10\n"
                                 "  mov x11, xzr\n", out) == EOF ||
                    fprintf(out, "2:\n"
                                 "  str x9, [sp, #%zu]\n"
                                 "  str x11, [sp, #%zu]\n",
                            dst_offset, dst_offset + 8U) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                (void)rs;
                continue;
            }
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *op;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_equal(fn->values[lhs].type, ins->type) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type,
                                      &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type,
                                      &rw, &rs) ||
                (lw != 1U && lw != 2U && lw != 4U && lw != 8U) ||
                /* C permits 8/16-bit shift-count values after promotion;
                 * the target consumes their low 32-bit SSA slot. */
                minic_type_is_bool_integer(ins->type))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                     ? (wide ? "  lsl x9, x9, x10\n"
                             : "  lsl w9, w9, w10\n")
                     : (ls ? (wide ? "  asr x9, x9, x10\n"
                                   : "  asr w9, w9, w10\n")
                           : (wide ? "  lsr x9, x9, x10\n"
                                   : "  lsr w9, w9, w10\n"));
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                /* Narrow signed shifts need sign-extension BEFORE the
                 * arithmetic shift; normalizing only the result is late. */
                (lw < 4U && !emit_integer_normalize(out, lw, ls)) ||
                fprintf(out, rw == 8U ? "  ldr x10, [sp, #%zu]\n"
                                     : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(op, out) == EOF ||
                (!wide && !emit_integer_normalize(out, lw, ls)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            (void)rs;
            break;
        }
        /* Original cJSON uses ~ on signed/unsigned scalar values.
         * Preserve C-width truncation for narrow integers and the full
         * 64-bit complement for LP64 values. */
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (operand >= fn->value_count ||
                !minic_type_equal(fn->values[operand].type, ins->type) ||
                !minic_lp64_scalar_integer_width(program, target, ins->type, &width, &sign) ||
                minic_type_is_bool_integer(ins->type) ||
                fprintf(out, width == 8U ? "  ldr x9, [sp, #%zu]\n"
                                         : "  ldr w9, [sp, #%zu]\n",
                        (size_t)operand * 8U) < 0 ||
                fputs(width == 8U ? "  mvn x9, x9\n" : "  mvn w9, w9\n",
                      out) == EOF ||
                (width != 8U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (operand >= fn->value_count ||
                !minic_type_equal(fn->values[operand].type, ins->type) ||
                !minic_lp64_scalar_integer_width(program, target, ins->type, &width, &sign) ||
                minic_type_is_bool_integer(ins->type) ||
                fprintf(out, width == 8U ? "  ldr x9, [sp, #%zu]\n"
                                         : "  ldr w9, [sp, #%zu]\n",
                        (size_t)operand * 8U) < 0 ||
                fputs(width == 8U ? "  neg x9, x9\n" : "  neg w9, w9\n",
                      out) == EOF ||
                (width != 8U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY: {
            MinicCoreValueId lhs = ins->value.binary.left;
            MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *opcode;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type, &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type, &rw, &rs) ||
                lw != rw || ls != rs ||
                !minic_type_equal(fn->values[lhs].type, ins->type) ||
                minic_type_is_bool_integer(ins->type))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            /* M36: target-native LP64 multiplication, including cJSON's
             * size_t newsize = needed * 2; low 64 bits follow C unsigned
             * arithmetic; signed overflow is undefined at the source. */
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD
                ? (wide ? "  add x9, x9, x10\n" : "  add w9, w9, w10\n")
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT
                    ? (wide ? "  sub x9, x9, x10\n" : "  sub w9, w9, w10\n")
                    : (wide ? "  mul x9, x9, x10\n" : "  mul w9, w9, w10\n");
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(opcode, out) == EOF ||
                (!wide && !emit_integer_normalize(out, lw, ls)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *opcode;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type,
                                      &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type,
                                      &rw, &rs) ||
                lw != rw || (lw != 1U && lw != 2U && lw != 4U && lw != 8U) ||
                ls != rs || minic_type_is_bool_integer(ins->type))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
                ? (wide ? "  and x9, x9, x10\n" : "  and w9, w9, w10\n")
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
                    ? (wide ? "  eor x9, x9, x10\n" : "  eor w9, w9, w10\n")
                    : (wide ? "  orr x9, x9, x10\n" : "  orr w9, w9, w10\n");
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(opcode, out) == EOF ||
                (lw != 8U && !emit_integer_normalize(out, lw, ls)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs, wide;
            const char *op;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type,
                                      &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type,
                                      &rw, &rs) ||
                lw != rw || (lw != 4U && lw != 8U) || ls != rs ||
                !minic_type_equal(fn->values[lhs].type, ins->type))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE
                ? (wide ? (ls ? "  sdiv x9, x9, x10\n"
                               : "  udiv x9, x9, x10\n")
                        : (ls ? "  sdiv w9, w9, w10\n"
                               : "  udiv w9, w9, w10\n"))
                : (wide ? (ls ? "  sdiv x11, x9, x10\n"
                               "  msub x9, x11, x10, x9\n"
                             : "  udiv x11, x9, x10\n"
                               "  msub x9, x11, x10, x9\n")
                        : (ls ? "  sdiv w11, w9, w10\n"
                               "  msub w9, w11, w10, w9\n"
                             : "  udiv w11, w9, w10\n"
                               "  msub w9, w11, w10, w9\n"));
            if (fputs(op, out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        /* M33: cJSON_SetValuestring uses pointer ordering for overlap
         * checks; the Core verifier has confirmed compatible pointer types.
         * Compare the entire 64-bit address unsigned (not low 32 bits). */
        case MINIC_CORE_INSTRUCTION_POINTER_LESS: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_type_equal(ins->type, minic_type_int()) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_pointer(fn->values[lhs].type) ||
                !minic_type_equal(fn->values[lhs].type, fn->values[rhs].type) ||
                fprintf(out, "  ldr x9, [sp, #%zu]\n"
                             "  ldr x10, [sp, #%zu]\n"
                             "  cmp x9, x10\n"
                             "  cset w9, lo\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs, wide;
            if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL &&
                lhs < fn->value_count && rhs < fn->value_count &&
                minic_type_is_pointer(fn->values[lhs].type) &&
                minic_type_is_pointer(fn->values[rhs].type)) {
                if (fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                 "  ldr x10, [sp, #%zu]\n"
                                 "  cmp x9, x10\n  cset w9, eq\n",
                            (size_t)lhs * 8U, (size_t)rhs * 8U) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type, &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type, &rw, &rs) ||
                lw != rw || ls != rs || (lw != 4U && lw != 8U))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS
                          ? (wide
                              ? (ls ? "  cmp x9, x10\n  cset w9, lt\n"
                                    : "  cmp x9, x10\n  cset w9, lo\n")
                              : (ls ? "  cmp w9, w10\n  cset w9, lt\n"
                                    : "  cmp w9, w10\n  cset w9, lo\n"))
                          : (wide ? "  cmp x9, x10\n  cset w9, eq\n"
                                  : "  cmp w9, w10\n  cset w9, eq\n"), out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO: {
            const MinicCoreValueId v = ins->value.operand;
            size_t width = 0U;
            bool sign = false;
            bool pointer, wide;
            if (v >= fn->value_count) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            pointer = minic_type_is_pointer(fn->values[v].type);
            if (!pointer && !minic_type_equal(fn->values[v].type, minic_type_bool()) &&
                !minic_lp64_scalar_integer_width(program, target, fn->values[v].type,
                                      &width, &sign))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = pointer || width == 8U;
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)v * 8U) < 0 ||
                fputs(wide ? "  cmp x9, #0\n  cset w9, eq\n"
                           : "  cmp w9, #0\n  cset w9, eq\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            (void)sign;
            break;
        }
        /* M28: cJSON_CreateNumber converts INT_MIN to binary64, and after
         * saturation converts an in-range double to signed int.
         * Keep wider and unsigned cases fail-closed until ABI-tested. */
        /* M31: cJSON_CreateFloatArray's float element must be interpreted
         * numerically, not zero-extended as its 32-bit IEEE bit pattern. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
            const MinicCoreValueId source = ins->value.operand;
            if (!minic_type_is_float(ins->type) ||
                source >= fn->value_count ||
                !minic_type_is_double(fn->values[source].type) ||
                fprintf(out, "  ldr d9, [sp, #%zu]\n"
                             "  fcvt s9, d9\n"
                             "  str s9, [sp, #%zu]\n",
                        (size_t)source * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
        case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
        case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD
                ? "  fadd s9, s9, s10\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                    ? "  fsub s9, s9, s10\n"
                    : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                        ? "  fmul s9, s9, s10\n"
                        : "  fdiv s9, s9, s10\n";
            if (!minic_type_is_float(ins->type) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_float(fn->values[lhs].type) ||
                !minic_type_is_float(fn->values[rhs].type) ||
                fprintf(out, "  ldr s9, [sp, #%zu]\n"
                             "  ldr s10, [sp, #%zu]\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0 ||
                fputs(op, out) == EOF ||
                fprintf(out, "  str s9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            if (src >= fn->value_count || !minic_type_is_double(ins->type) ||
                !minic_type_is_float(fn->values[src].type) ||
                fprintf(out,
                        "  ldr s9, [sp, #%zu]\n"
                        "  fcvt d9, s9\n"
                        "  str d9, [sp, #%zu]\n",
                        (size_t)src * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            /* Numeric C conversion: AArch64 has signed AND unsigned 32/64
             * integer-to-double instructions. Narrow source values retain
             * their Core signedness through explicit normalization. */
            if (src >= fn->value_count || !minic_type_is_double(ins->type) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[src].type,
                                      &width, &sign) ||
                fprintf(out, width == 8U
                             ? "  ldr x9, [sp, #%zu]\n"
                             : "  ldr w9, [sp, #%zu]\n",
                        (size_t)src * 8U) < 0 ||
                (width < 4U && !emit_integer_normalize(out, width, sign)) ||
                fputs(width == 8U
                          ? (sign ? "  scvtf d9, x9\n"
                                  : "  ucvtf d9, x9\n")
                          : (sign ? "  scvtf d9, w9\n"
                                  : "  ucvtf d9, w9\n"), out) == EOF ||
                fprintf(out, "  str d9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            const char *conversion;
            /* Lua's lua_Integer is signed LP64; C double-to-integer
             * conversion truncates toward zero. Keep unsigned64 fail-closed
             * until its full-range ABI oracle is introduced. */
            if (src >= fn->value_count ||
                !minic_type_is_double(fn->values[src].type) ||
                !minic_lp64_scalar_integer_width(program, target, ins->type, &width, &sign) ||
                (width != 1U && width != 2U &&
                 width != 4U && width != 8U))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            /* A64 natively supports unsigned 64-bit binary64 conversion.
             * C-defined finite inputs truncate toward zero; NaN/overflow
             * inputs remain outside the C conversion contract. */
            conversion = width == 8U
                ? (sign ? "  fcvtzs x9, d9\n"
                        : "  fcvtzu x9, d9\n")
                : sign ? "  fcvtzs w9, d9\n"
                       : "  fcvtzu w9, d9\n";
            if (fprintf(out, "  ldr d9, [sp, #%zu]\n",
                        (size_t)src * 8U) < 0 ||
                fputs(conversion, out) == EOF ||
                (width < 4U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
            const MinicCoreValueId count = ins->value.operand;
            if (!dynamic_frame || ins->result >= fn->value_count ||
                count >= fn->value_count || !minic_type_is_pointer(ins->type) ||
                !minic_type_equal(fn->values[ins->result].type, ins->type) ||
                !minic_type_equal(fn->values[count].type,
                                  minic_type_unsigned_long()) ||
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
            size_t src_width, dst_width;
            bool src_sign, dst_sign;
            /* RV64-proven pair model: keep int128 as low/high 64-bit halves,
             * in isolated 16-byte SSA slots. No silent XLEN truncation. */
            if (src < fn->value_count &&
                (minic_type_is_int128_integer(ins->type) ||
                 minic_type_is_int128_integer(fn->values[src].type))) {
                const bool src128 = minic_type_is_int128_integer(fn->values[src].type);
                const bool dst128 = minic_type_is_int128_integer(ins->type);
                size_t src_offset = 0U, dst_offset = 0U;
                if (src128) {
                    if (!minic_lp64_binary128_slot(fn, ld_base, src, &src_offset) ||
                        fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                     "  ldr x10, [sp, #%zu]\n",
                                src_offset, src_offset + 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                } else {
                    if (!minic_lp64_scalar_integer_width(program, target,
                            fn->values[src].type, &src_width, &src_sign) ||
                        fprintf(out, src_width == 8U ?
                                "  ldr x9, [sp, #%zu]\n" :
                                "  ldr w9, [sp, #%zu]\n", (size_t)src * 8U) < 0 ||
                        (src_width < 8U &&
                         (!emit_integer_normalize(out, src_width, src_sign) ||
                          (src_sign && fputs("  sxtw x9, w9\n", out) == EOF))) ||
                        fputs(src_sign ? "  asr x10, x9, #63\n" :
                                         "  mov x10, xzr\n", out) == EOF)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                }
                if (dst128) {
                    if (!minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst_offset) ||
                        fprintf(out, "  str x9, [sp, #%zu]\n"
                                     "  str x10, [sp, #%zu]\n",
                                dst_offset, dst_offset + 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                    continue;
                }
                if (!minic_lp64_scalar_integer_width(program, target, ins->type,
                                           &dst_width, &dst_sign) ||
                    (dst_width < 8U &&
                     !emit_integer_normalize(out, dst_width, dst_sign)))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                break;
            }
            /* Inventory both source/destination C integer widths before
             * admitting a cast. The SQLite 128-bit helper includes a type
             * conversion outside the already-proven 32/64 scalar subset. */
            if (getenv("MINIC_BACKEND_CENSUS") != NULL) {
                size_t source_bytes = 0U, dest_bytes = 0U;
                bool source_sign = false, dest_sign = false;
                const bool source_ok = src < fn->value_count &&
                    minic_lp64_scalar_integer_width(program, target,
                        fn->values[src].type, &source_bytes, &source_sign);
                const bool dest_ok = minic_lp64_scalar_integer_width(
                    program, target, ins->type, &dest_bytes, &dest_sign);
                if (!source_ok || !dest_ok)
                    (void)fprintf(stderr,
                        "MINIC_SQLITE_CONVERSION_LIMIT arch=aarch64 function=%s"
                        " ins=%zu source_ok=%d src_bytes=%zu src_sign=%d"
                        " dest_ok=%d dst_bytes=%zu dst_sign=%d"
                        " source_kind=%d dest_kind=%d\n",
                        name, (size_t)ins_id, source_ok, source_bytes, source_sign,
                        dest_ok, dest_bytes, dest_sign,
                        src < fn->value_count
                            ? (int)fn->values[src].type.base_kind : -1,
                        (int)ins->type.base_kind);
            }
            if (src >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target,
                                      fn->values[src].type, &src_width, &src_sign) ||
                !minic_lp64_scalar_integer_width(program, target, ins->type,
                                      &dst_width, &dst_sign) ||
                fprintf(out, src_width == 8U
                            ? "  ldr x9, [sp, #%zu]\n"
                            : "  ldr w9, [sp, #%zu]\n",
                        (size_t)src * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (dst_width == 8U) {
                if (src_width != 8U &&
                    (!emit_integer_normalize(out, src_width, src_sign) ||
                     (src_sign && fputs("  sxtw x9, w9\n", out) == EOF)))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                /* Unsigned narrow source already zero-extends on w writes. */
            } else if (!emit_integer_normalize(out, dst_width, dst_sign))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
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
        if (fprintf(out, (minic_lp64_register64_type(program, target, ins->type) ||
                          minic_type_is_double(ins->type))
                             ? "  str x9, [sp, #%zu]\n"
                             : "  str w9, [sp, #%zu]\n", offset) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    }
        /* Branches use the function's own local ELF labels; x9 is a
         * scratch register and w0 is only defined along return paths. */
        reject_stage = "terminator_emit";
        reject_ins = SIZE_MAX;
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (minic_type_is_long_double(fn->return_type)) {
                size_t src;
                if (!minic_lp64_binary128_slot(fn, ld_base,
                                    block->terminator.return_value, &src) ||
                    fprintf(out, "  ldr q0, [sp, #%zu]\n", src) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
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
                fputs("  ret\n", out) == EOF) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            if (fprintf(out, "  ldr x9, [sp, #%zu]\n  br x9\n",
                        (size_t)block->terminator.indirect_target * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Preserve Core CFG semantics: trap instead of falling through. */
            if (fputs("  brk #0\n", out) == EOF) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_arm_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
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
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
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
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        default: return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
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

/* Translate only dynamic-frame functions through a private assembly buffer.
 * The existing AArch64 emitter speaks [sp,#offset] for its fixed SSA frame.
 * For alloca functions, those values belong to callee-saved x19 while SP
 * may move; source-owned outgoing stack argument slots remain relative to SP.
 * This is a target-local text legalization, just like large-offset handling,
 * and leaves all non-dynamic code generation byte-for-byte unchanged. */
static bool copy_dynamic_frame_asm(FILE *out, FILE *input) {
    char line[1024];
    bool anchored = false;
    size_t spill = 0U;
    while (fgets(line, sizeof(line), input) != NULL) {
        char *base, *end;
        unsigned int rd, rn;
        size_t value;
        if (strchr(line, '\n') == NULL && !feof(input)) return false;
        if (strcmp(line, "  mov x19, sp\n") == 0) {
            anchored = true;
        } else if (strcmp(line, "  mov sp, x19\n") == 0) {
            /* Epilogue must load the saved x19 from the restored SP.
             * This unanchors only the current RETURN terminator, not
             * subsequent textually later CFG blocks: they may be reached
             * by a branch from earlier blocks and still use dynamic SP. */
            anchored = false;
            spill = 0U;
        } else if (!anchored && strcmp(line, "  ret\n") == 0) {
            if (fputs(line, out) == EOF) return false;
            anchored = true;
            continue;
        } else if (anchored && sscanf(line, "  sub sp, sp, #%zu", &value) == 1) {
            /* Only the bounded outgoing call argument area uses sub #imm.
             * Dynamic allocations use sub sp, sp, x10, never an immediate. */
            if (spill != 0U || value == 0U || value > 512U) return false;
            spill = value;
        } else if (anchored && sscanf(line, "  add sp, sp, #%zu", &value) == 1) {
            if (spill != value) return false;
            spill = 0U;
        }
        base = anchored ? strstr(line, "[sp, #") : NULL;
        if (base != NULL) {
            unsigned long long offset;
            offset = strtoull(base + 6U, &end, 10);
            if (end == base + 6U || *end != ']' ||
                offset > (unsigned long long)MINIC_LARGE_FRAME_MAX + 512U)
                return false;
            if (offset >= (unsigned long long)spill) {
                const size_t fixed = (size_t)(offset - spill);
                if (fprintf(out, "%.*s[x19, #%zu]%s",
                            (int)(base - line), line, fixed, end + 1) < 0)
                    return false;
                continue;
            }
            /* This byte-range is the caller-owned outgoing spill area. */
        } else if (anchored &&
                   sscanf(line, "  add x%u, sp, x%u", &rd, &rn) == 2 &&
                   rd == rn && rd >= 9U && rd <= 13U) {
            if (fprintf(out, "  add x%u, x19, x%u\n", rd, rn) < 0 ||
                /* Record call arguments pre-add spill bytes to their Core
                 * object offsets. Remove exactly that adjustment after
                 * rebasing from SP to fixed x19. */
                (spill != 0U &&
                 fprintf(out, "  sub x%u, x%u, #%zu\n",
                         rd, rd, spill) < 0))
                return false;
            continue;
        }
        if (fputs(line, out) == EOF) return false;
    }
    return ferror(input) == 0 && spill == 0U;
}

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
                    !copy_dynamic_frame_asm(out, temporary)))
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
        legalize_large_sp_memory
    };
    return minic_multi_direct_write_program(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, diagnostic);
}
