#include "target/loongarch64/codegen.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* M1/M2: prove correct *actual* GNU ELF/ABI assembly, not just target naming.
 * Each signed 32-bit Core SSA value gets an 8-byte private stack slot.
 * Scalar int parameters, arithmetic, and basic block branches are admitted.
 * No implicit ABI equivalence with RISC-V64/x86-64 is assumed. */
static bool fail(MinicDiagnostic *diag, const char *path, const char *why) {
    if (diag != NULL) {
        diag->path = path;
        diag->line = 1U;
        diag->column = 1U;
        (void)snprintf(diag->message, sizeof(diag->message), "%s", why);
    }
    return false;
}

static bool valid_int(MinicType type) {
    return minic_type_equal(type, minic_type_int());
}

/* Comparisons are Core booleans, not 32-bit C int. Both still fit our
 * private 8-byte SSA slots, with canonical 0/1 in the low 32 bits. */

static bool valid_int_or_bool(MinicType type) {
    return valid_int(type) || minic_type_equal(type, minic_type_bool());
}


/* M8: x86's width/sign principle, target-owned machine encodings.
 * A raw Core slot reserves 8 bytes even when the C object is 1 or 2 bytes.
 * The operand width comes from the target DataLayout, not a hard-coded
 * assumption about typedef names or host char signedness. */
static bool small_integer_width(const MinicC0Program *program,
                                const MinicTargetInfo *target, MinicType type,
                                size_t *width, bool *sign) {
    unsigned int bits;
    MinicType effective;
    if (width == NULL || sign == NULL ||
        !minic_type_is_integer(type) ||
        !minic_c0_type_effective_integer_type(program, type, &effective) ||
        !minic_target_info_integer_width(target, program, type, &bits) ||
        bits == 0U || bits > 32U) return false;
    *width = (bits + 7U) / 8U;
    *sign = minic_type_is_signed_integer(effective);
    return *width == 1U || *width == 2U || *width == 4U;
}

/* GNU AAPCS64/lp64d integer parameter slots are one register wide
 * for ordinary 1/2/4-byte integer types; reject wider widths rather
 * than silently truncate arguments in a 32-bit store. */
static bool valid_parameter_type(const MinicC0Program *program,
                                 const MinicTargetInfo *target, MinicType type) {
    size_t width;
    bool sign;
    return minic_type_is_pointer(type) ||
           small_integer_width(program, target, type, &width, &sign);
}

/* C scalar return ABI is independent from the initial signed-int smoke
 * gate. Core's unsigned char xtime return and its GNU callers must share
 * the actual target integer width; reject unsupported 64/128-bit values. */
static bool valid_return_type(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type) {
    return minic_type_is_void(type) ||
           valid_parameter_type(program, target, type);
}

static bool emit_integer_memory(FILE *out, bool store,
                                size_t width, bool sign) {
    const char *op = NULL;
    if (store) {
        op = width == 1U ? "  st.b $t0, $t1, 0\n" :
             width == 2U ? "  st.h $t0, $t1, 0\n" :
             width == 4U ? "  st.w $t0, $t1, 0\n" : NULL;
    } else {
        op = width == 1U ? (sign ? "  ld.b $t0, $t1, 0\n" :
                                     "  ld.bu $t0, $t1, 0\n") :
             width == 2U ? (sign ? "  ld.h $t0, $t1, 0\n" :
                                     "  ld.hu $t0, $t1, 0\n") :
             width == 4U ? "  ld.w $t0, $t1, 0\n" : NULL;
    }
    return op != NULL && fputs(op, out) != EOF;
}

static bool emit_integer_normalize(FILE *out, size_t width, bool sign) {
    const char *op = width == 1U
        ? (sign ? "  slli.w $t0, $t0, 24\n  srai.w $t0, $t0, 24\n" :
                  "  andi $t0, $t0, 255\n")
        : width == 2U
            ? (sign ? "  slli.w $t0, $t0, 16\n  srai.w $t0, $t0, 16\n" :
                      "  bstrpick.w $t0, $t0, 15, 0\n")
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
static bool emit_record_relocation(FILE *out, const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   const MinicGlobalRelocation *reloc,
                                   size_t width) {
    const char *name = NULL;
    int64_t addend;
    if (reloc == NULL || width != 8U ||
        target->data_layout->pointer_size != 8U) return false;
    if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {
        const MinicGlobalObject *obj =
            minic_c0_program_global_object(program, reloc->target_id);
        if (obj != NULL) name = obj->name;
    } else if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        const MinicFunction *decl =
            minic_c0_program_function(program, reloc->target_id);
        if (decl != NULL) name = minic_c0_function_symbol_name(decl);
    }
    if (name == NULL || name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(
            target->data_layout, program, reloc, &addend)) return false;
    if (addend == 0)
        return fprintf(out, "  .8byte %s\n", name) >= 0;
    return fprintf(out, "  .8byte %s%+" PRId64 "\n", name, addend) >= 0;
}

static bool emit_flat_record_global(FILE *out,
                                    const MinicC0Program *program,
                                    const MinicTargetInfo *target,
                                    const MinicGlobalObject *obj,
                                    size_t storage_size) {
    const MinicRecord *record =
        minic_c0_program_record(program, obj->type.record_id);
    size_t i, cursor = 0U, reloc_index = 0U;
    if (record == NULL || !record->is_complete || record->is_union ||
        record->field_count == 0U ||
        record->field_count != obj->initializer_count ||
        obj->initializer_values == NULL) return false;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        const MinicGlobalRelocation *reloc =
            reloc_index < obj->relocation_count
                ? &obj->relocations[reloc_index] : NULL;
        size_t offset, width, alignment;
        const char *directive;
        uint64_t bits;
        if (field == NULL || field->is_bit_field ||
            field->is_flexible_array || field->element_count != 1U ||
            (!minic_type_is_integer(field->type) &&
             !minic_type_is_pointer(field->type)) ||
            !minic_data_layout_type(target->data_layout, program,
                                    field->type, &width, &alignment) ||
            !minic_data_layout_record_field_offset(
                target->data_layout, program, record, i, &offset) ||
            (width != 1U && width != 2U && width != 4U && width != 8U) ||
            offset < cursor || offset > storage_size ||
            width > storage_size - offset ||
            fprintf(out, "  .zero %zu\n", offset - cursor) < 0)
            return false;
        (void)alignment;
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == i) {
            if (!minic_type_is_pointer(field->type) ||
                obj->initializer_values[i] != 0U ||
                !emit_record_relocation(out, program, target, reloc, width))
                return false;
            ++reloc_index;
        } else {
            bits = obj->initializer_values[i];
            if (width < 8U)
                bits &= (UINT64_C(1) << (width * 8U)) - UINT64_C(1);
            directive = width == 1U ? ".byte" : width == 2U ? ".2byte" :
                        width == 4U ? ".4byte" : ".8byte";
            if (fprintf(out, "  %s %" PRIu64 "\n", directive, bits) < 0)
                return false;
        }
        cursor = offset + width;
    }
    return reloc_index == obj->relocation_count &&
           cursor <= storage_size &&
           fprintf(out, "  .zero %zu\n", storage_size - cursor) >= 0;
}

static bool emit_simple_global(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicGlobalObject *obj) {
    MinicType scalar;
    size_t storage_size, align, width, scalar_align, i;
    const char *directive;
    if (obj == NULL || obj->name == NULL || !obj->name[0] ||
        obj->is_weak || obj->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        obj->section_name != NULL ||
        obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        obj->union_selection_count != 0U ||
        obj->flexible_array_initializer_count != 0U ||
        !minic_data_layout_global_object(target->data_layout, program, obj,
                                         &storage_size, &align)) return false;
    if (minic_type_is_record(obj->type)) {
        if (fprintf(out, "%s\n", obj->is_read_only
                        ? (obj->relocation_count != 0U
                           ? ".section .data.rel.ro" : ".section .rodata")
                        : ".data") < 0 ||
            (!obj->is_internal &&
             fprintf(out, ".globl %s\n", obj->name) < 0) ||
            fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                    align, obj->name, obj->name) < 0 ||
            !emit_flat_record_global(out, program, target, obj,
                                     storage_size) ||
            fprintf(out, ".size %s,%zu\n", obj->name, storage_size) < 0)
            return false;
        return true;
    }
    if (obj->relocation_count != 0U) return false;
    scalar = obj->type;
    while (minic_type_is_array(scalar)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, scalar.array_type_id);
        if (array == NULL || array->element_count == 0U) return false;
        scalar = array->element_type;
    }
    if (!minic_type_is_integer(scalar) ||
        !minic_data_layout_type(target->data_layout, program, scalar,
                                &width, &scalar_align) ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
        width > storage_size ||
        obj->initializer_count > storage_size / width ||
        (obj->initializer_count > 0U && obj->initializer_values == NULL))
        return false;
    (void)scalar_align;
    directive = width == 1U ? ".byte" : width == 2U ? ".2byte" :
                width == 4U ? ".4byte" : ".8byte";
    if (fprintf(out, "%s\n", obj->is_read_only ? ".section .rodata" :
                ((obj->is_zero_initialized || obj->is_tentative) &&
                 obj->initializer_count == 0U ? ".bss" : ".data")) < 0)
        return false;
    if (!obj->is_internal && fprintf(out, ".globl %s\n", obj->name) < 0)
        return false;
    if (fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                align, obj->name, obj->name) < 0) return false;
    if (obj->initializer_count == 0U) {
        if (fprintf(out, "  .zero %zu\n", storage_size) < 0) return false;
    } else {
        for (i = 0U; i < obj->initializer_count; ++i) {
            uint64_t bits = obj->initializer_values[i];
            if (width < 8U)
                bits &= (UINT64_C(1) << (8U * width)) - UINT64_C(1);
            if (fprintf(out, "  %s %" PRIu64 "\n", directive, bits) < 0)
                return false;
        }
        if (storage_size > obj->initializer_count * width &&
            fprintf(out, "  .zero %zu\n",
                    storage_size - obj->initializer_count * width) < 0)
            return false;
    }
    return fprintf(out, ".size %s,%zu\n", obj->name, storage_size) >= 0;
}

/* M7: retain x86's established DataLayout-owned object sizing instead
 * of guessing eight bytes per Core object.  The new-ISA stack slots
 * are SP-positive; naturally aligned objects are laid out after SSA
 * value slots.  Keep bounded <= 8 KiB frames until large-offset ABI
 * access has been independently checked on both targets. */
static bool frame_objects(const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicCoreFunction *fn,
                          size_t sought_id, size_t *sought_offset,
                          size_t *object_end) {
    size_t pos, i;
    if (program == NULL || target == NULL || fn == NULL ||
        sought_id > fn->object_count ||
        fn->value_count > 1024U || fn->object_count > 1024U)
        return false;
    pos = fn->value_count * 8U;
    for (i = 0U; i < fn->object_count; ++i) {
        const MinicCoreObject *obj = &fn->objects[i];
        size_t unit, align, extent;
        if (!minic_data_layout_type(target->data_layout, program,
                                    obj->type, &unit, &align) ||
            obj->element_count == 0U || unit > 8192U / obj->element_count)
            return false;
        extent = unit * obj->element_count;
        if (obj->explicit_alignment > align)
            align = obj->explicit_alignment;
        if (align == 0U || align > 16U || (align & (align - 1U)) != 0U)
            return false;
        if (pos > 8192U - (align - 1U)) return false;
        pos = (pos + align - 1U) & ~(align - 1U);
        if (i == sought_id && sought_offset != NULL)
            *sought_offset = pos;
        if (extent > 8192U - pos) return false;
        pos += extent;
    }
    if (sought_id == fn->object_count && sought_offset != NULL)
        *sought_offset = pos;
    if (object_end != NULL) *object_end = pos;
    return true;
}

/* lp64d load/store immediates are signed 12-bit. Keep the original
 * direct encoding for small frames; for real KeyExpansion-sized frames,
 * materialize the full SP-relative address in reserved $t3.  No other
 * emitter instruction may treat $t3 as a live Core value. */
static bool emit_sp_mem(FILE *out, const char *op, const char *reg,
                        size_t offset) {
    if (offset <= 2047U)
        return fprintf(out, "  %s %s, $sp, %zu\n", op, reg, offset) >= 0;
    if (offset > 16384U) return false;
    return fprintf(out, "  li.d $t3, %zu\n"
                        "  add.d $t3, $sp, $t3\n"
                        "  %s %s, $t3, 0\n", offset, op, reg) >= 0;
}
static bool emit_sp_adjust(FILE *out, size_t bytes, bool restore) {
    if (bytes == 0U) return true;
    if (bytes > 8192U) return false;
    return fprintf(out, "  li.d $t3, %zu\n"
                        "  %s $sp, $sp, $t3\n", bytes,
                   restore ? "add.d" : "sub.d") >= 0;
}

static bool emit_function(FILE *out, const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections) {
    const char *name = minic_c0_function_symbol_name(decl);
    const MinicCoreBlock *block;
    size_t frame, i, object_end, save_offset;
    bool has_direct_calls = false;
    if (getenv("MINIC_M1_TRACE") != NULL &&
        name != NULL && ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr,
                      "MINIC_M9_GATE arch=loongarch64 fn=%s stage=entry values=%zu objects=%zu blocks=%zu params=%zu return_valid=%d inline=%zu\n",
                      name, fn->value_count, fn->object_count,
                      fn->block_count, fn->parameter_count,
                      valid_return_type(program, target, fn->return_type) ? 1 : 0,
                      fn->inline_asm_count);
    if (name == NULL || name[0] == '\0' ||
        !minic_core_function_verify(fn) ||
        decl->is_constructor || decl->is_weak ||
        decl->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        decl->section_name != NULL || decl->alias_target != MINIC_FUNCTION_INVALID ||
        !valid_return_type(program, target, fn->return_type) ||
        fn->block_count == 0U || fn->block_count > 128U || fn->entry_block != 0U ||
        fn->inline_asm_count != 0U || fn->parameter_count > 10U ||
        fn->value_count > 1024U || fn->object_count > 1024U)
        return false;
    for (i = 0U; i < fn->parameter_count; ++i)
        if (!valid_parameter_type(program, target, fn->parameter_types[i])) return false;
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=loongarch64 fn=%s stage=parameters_valid\n", name);
    /* M2: verify every CFG successor and return before emitting anything.
     * A function's SSA values and local objects retain unique stack slots
     * across blocks, so no architecture-independent register allocator is
     * needed to implement basic control-flow correctly. */
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreTerminator *term = &fn->blocks[i].terminator;
        if (!fn->blocks[i].has_terminator) return false;
        switch (term->kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (term->return_object != MINIC_CORE_OBJECT_INVALID ||
                (minic_type_is_void(fn->return_type)
                 ? term->return_value != MINIC_CORE_VALUE_INVALID
                 : term->return_value == MINIC_CORE_VALUE_INVALID ||
                   term->return_value >= fn->value_count ||
                   !minic_type_equal(fn->values[term->return_value].type,
                                     fn->return_type))) return false;
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (term->branch_target >= fn->block_count) return false;
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
            if (term->conditional.when_true >= fn->block_count ||
                term->conditional.when_false >= fn->block_count ||
                term->conditional.condition >= fn->value_count ||
                !valid_int_or_bool(fn->values[term->conditional.condition].type))
                return false;
            break;
        default:
            return false;
        }
    }
    /* Object byte extents and alignment are validated by frame_objects.
     * This admits real repeated local arrays without overlapping slots. */
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=loongarch64 fn=%s stage=terminators_valid\n", name);
    if (!frame_objects(program, target, fn, fn->object_count, NULL,
                       &object_end)) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_REJECT arch=loongarch64 function=%s values=%zu objects=%zu\n",
                          name, fn->value_count, fn->object_count);
        return false;
    }
    /* Keep this first direct-call ABI step narrow: signed int values in
     * the first eight argument registers, no variadics or indirect calls.
     * Returning through a nested call requires preserving the ISA link
     * register in a dedicated frame slot. */
    for (i = 0U; i < fn->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &fn->instructions[i];
        size_t j, begin, count;
        const MinicCoreCallee *callee;
        if (ins->kind != MINIC_CORE_INSTRUCTION_CALL) continue;
        has_direct_calls = true;
        if (ins->value.call.callee_id >= fn->callee_count ||
            ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID ||
            (minic_type_is_void(ins->type)
             ? ins->result != MINIC_CORE_VALUE_INVALID
             : ins->result == MINIC_CORE_VALUE_INVALID ||
               ins->result >= fn->value_count ||
               !minic_type_equal(fn->values[ins->result].type, ins->type)) ||
            !valid_return_type(program, target, ins->type))
            return false;
        callee = &fn->callees[ins->value.call.callee_id];
        begin = ins->value.call.argument_begin;
        count = ins->value.call.argument_count;
        if (callee->name == NULL || callee->name[0] == '\0' ||
            !valid_return_type(program, target, callee->return_type) ||
            count > 8U || callee->parameter_count > count ||
            (!callee->is_variadic && callee->parameter_count != count) ||
            begin > fn->call_argument_count ||
            count > fn->call_argument_count - begin)
            return false;
        for (j = 0U; j < count; ++j) {
            const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
            MinicType actual;
            size_t actual_width;
            bool actual_signed;
            if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                arg->value.value_id >= fn->value_count) return false;
            actual = fn->values[arg->value.value_id].type;
            if (!valid_parameter_type(program, target, actual)) return false;
            if (j < callee->parameter_count) {
                /* Required fixed parameters preserve their full Core type. */
                if (!valid_parameter_type(program, target,
                                          callee->parameter_types[j]) ||
                    !minic_type_equal(callee->parameter_types[j], actual))
                    return false;
            } else {
                /* On AAPCS64 / Loong lp64d the promoted integer and pointer
                 * varargs use a0..a7 / x0..x7. No FP, long-long,
                 * aggregates, or stack overflow are silently accepted. */
                if (!callee->is_variadic ||
                    (!minic_type_is_pointer(actual) &&
                     (!small_integer_width(program, target, actual,
                                           &actual_width, &actual_signed) ||
                      actual_width != 4U))) return false;
            }
        }
    }
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=loongarch64 fn=%s stage=calls_valid object_end=%zu\n",
                      name, object_end);
    save_offset = (object_end + 7U) & ~(size_t)7U;
    if (save_offset > 8192U - (has_direct_calls ? 8U : 0U))
        return false;
    frame = (save_offset + (has_direct_calls ? 8U : 0U) + 15U) &
            ~(size_t)15U;
    if (frame > 8192U) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_CAP arch=loongarch64 function=%s frame=%zu end=%zu\n",
                          name, frame, object_end);
        return false;
    }
    if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",@progbits\n", name) < 0)
            return false;
    } else if (fprintf(out, ".text\n") < 0) return false;
    if (!decl->is_internal && fprintf(out, ".globl %s\n", name) < 0)
        return false;
    if (fprintf(out, ".p2align 2\n.type %s,@function\n%s:\n", name, name) < 0)
        return false;
    if (!emit_sp_adjust(out, frame, false)) return false;
    if (has_direct_calls &&
        !emit_sp_mem(out, "st.d", "$ra", save_offset)) return false;
    for (size_t block_id = 0U; block_id < fn->block_count; ++block_id) {
        block = &fn->blocks[block_id];
        if (fprintf(out, ".Lmini_loong_f%zu_b%zu:\n", id, block_id) < 0)
            return false;
        for (i = 0U; i < block->instruction_count; ++i) {
        MinicCoreInstructionId ins_id = block->instructions[i];
        const MinicCoreInstruction *ins;
        size_t offset;
        if (ins_id >= fn->instruction_count) return false;
        ins = &fn->instructions[ins_id];
        /* Only recorded in the temporary original-source failure log,
         * never in normal builds or public diagnostic stream. */
        if (getenv("MINIC_M1_TRACE") != NULL &&
            ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
            (void)fprintf(stderr,
                          "MINIC_M9_STEP arch=loongarch64 function=%s block=%zu ins=%u kind=%d result=%u type_int=%d type_ptr=%d\n",
                          name, block_id, (unsigned int)ins_id,
                          (int)ins->kind, (unsigned int)ins->result,
                          minic_type_is_integer(ins->type) ? 1 : 0,
                          minic_type_is_pointer(ins->type) ? 1 : 0);
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE) {
            const MinicCoreValueId addr = ins->value.store.address;
            const MinicCoreValueId value = ins->value.store.stored_value;
            size_t width = 0U;
            bool sign = false, pointer;
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                addr >= fn->value_count || value >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type)) return false;
            pointer = minic_type_is_pointer(fn->values[value].type);
            if (!pointer &&
                !small_integer_width(program, target, fn->values[value].type,
                                     &width, &sign)) return false;
            if (!emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_sp_mem(out, pointer ? "ld.d" : "ld.w", "$t0",
                             (size_t)value * 8U) ||
                (pointer ? fputs("  st.d $t0, $t1, 0\n", out) == EOF
                         : !emit_integer_memory(out, true, width, sign))) return false;
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
            minic_type_is_void(ins->type)) {
            /* Verified in the call preflight: an effect-only void call
             * has no Core result slot. Fall through into the call case. */
            offset = 0U;
        } else {
            if (ins->result == MINIC_CORE_VALUE_INVALID ||
                ins->result >= fn->value_count ||
                ((ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
                   ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS)
                    ? !minic_type_is_pointer(ins->type)
                    : !minic_type_is_integer(ins->type) &&
                      !minic_type_is_pointer(ins->type)))
            {
                if (getenv("MINIC_M1_TRACE") != NULL)
                    (void)fprintf(stderr,
                                  "MINIC_M8_RESULT_REJECT arch=loongarch64 function=%s block=%zu instruction=%u kind=%d result=%u type_integer=%d type_pointer=%d\n",
                                  name, block_id, (unsigned int)ins_id,
                                  (int)ins->kind, (unsigned int)ins->result,
                                  minic_type_is_integer(ins->type) ? 1 : 0,
                                  minic_type_is_pointer(ins->type) ? 1 : 0);
                return false;
            }
            offset = (size_t)ins->result * 8U;
        }
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
            if (ins->value.integer_value < INT32_MIN ||
                ins->value.integer_value > INT32_MAX) return false;
            if (fprintf(out, "  li.w $t0, %" PRId64 "\n", ins->value.integer_value) < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
            if (ins->value.global_id >= fn->global_count ||
                fn->globals[ins->value.global_id].name == NULL ||
                fprintf(out, "  la.global $t0, %s\n",
                        fn->globals[ins->value.global_id].name) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset))
                return false;
            continue;
        case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
            const MinicCoreFunctionSymbolId symbol_id =
                ins->value.function_symbol_id;
            const char *symbol;
            if (symbol_id >= fn->function_symbol_count ||
                !minic_type_is_pointer(ins->type)) return false;
            symbol = fn->function_symbols[symbol_id].name;
            if (symbol == NULL || symbol[0] == '\0' ||
                fprintf(out, "  la.global $t0, %s\n", symbol) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset)) return false;
            continue;
        }
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
            size_t object_offset;
            if (ins->value.object_id >= fn->object_count ||
                !frame_objects(program, target, fn,
                               (size_t)ins->value.object_id,
                               &object_offset, NULL) ||
                fprintf(out, "  li.d $t0, %zu\n"
                             "  add.d $t0, $sp, $t0\n",
                        object_offset) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset)) return false;
            continue;
        }

        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
            const MinicCoreValueId base = ins->value.field_address.base;
            const size_t bytes = ins->value.field_address.byte_offset;
            if (base >= fn->value_count ||
                !minic_type_is_pointer(fn->values[base].type) ||
                bytes > 2047U ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)base * 8U) ||
                (bytes != 0U && fprintf(out, "  addi.d $t0, $t0, %zu\n", bytes) < 0))
                return false;
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
            if (base >= fn->value_count || index >= fn->value_count ||
                !minic_type_is_pointer(fn->values[base].type) ||
                !small_integer_width(program, target, fn->values[index].type,
                                     &iw, &signed_index) ||
                stride > 2047U ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)base * 8U) ||
                !emit_sp_mem(out, iw == 1U
                                  ? (signed_index ? "ld.b" : "ld.bu")
                                  : iw == 2U
                                      ? (signed_index ? "ld.h" : "ld.hu")
                                      : (signed_index ? "ld.w" : "ld.wu"),
                             "$t1", (size_t)index * 8U))
                return false;
            if (stride != 0U) {
                if (fprintf(out, "  addi.d $t2, $zero, %zu\n"
                                 "  mul.d $t1, $t1, $t2\n"
                                 "  %s $t0, $t0, $t1\n", stride,
                            ins->value.pointer_offset.subtract ? "sub.d" : "add.d") < 0)
                    return false;
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
            if (source >= fn->value_count ||
                !minic_type_is_pointer(ins->type) ||
                !minic_core_scalar_bitcast_types_valid(
                    ins->type, fn->values[source].type)) return false;
            src_pointer = minic_type_is_pointer(fn->values[source].type);
            if (!src_pointer &&
                !small_integer_width(program, target,
                                     fn->values[source].type,
                                     &src_width, &src_signed)) return false;
            if (!emit_sp_mem(out, src_pointer ? "ld.d" : "ld.w", "$t0",
                             (size_t)source * 8U)) return false;
            if (!src_pointer &&
                (!emit_integer_normalize(out, src_width, src_signed) ||
                 (src_signed
                    ? fputs("  slli.w $t0, $t0, 0\n", out) == EOF
                    : fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF)))
                return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_LOAD: {
            const MinicCoreValueId addr = ins->value.load.address;
            size_t width = 0U;
            bool sign = false, pointer = minic_type_is_pointer(ins->type);
            if (addr >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                (!pointer && !small_integer_width(program, target,
                                                  ins->type, &width, &sign)) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                (pointer ? fputs("  ld.d $t0, $t1, 0\n", out) == EOF
                         : !emit_integer_memory(out, false, width, sign)))
                return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_CALL: {
            const MinicCoreCallee *callee = &fn->callees[ins->value.call.callee_id];
            size_t j;
            for (j = 0U; j < ins->value.call.argument_count; ++j) {
                const MinicCoreCallArgument *arg =
                    &fn->call_arguments[ins->value.call.argument_begin + j];
                if (!emit_sp_mem(out, minic_type_is_pointer(
                                        fn->values[arg->value.value_id].type)
                                        ? "ld.d" : "ld.w",
                             j == 0U ? "$a0" : j == 1U ? "$a1" :
                             j == 2U ? "$a2" : j == 3U ? "$a3" :
                             j == 4U ? "$a4" : j == 5U ? "$a5" :
                             j == 6U ? "$a6" : "$a7",
                             (size_t)arg->value.value_id * 8U))
                    return false;
            }
            if (fprintf(out, "  bl %s\n", callee->name) < 0)
                return false;
            if (minic_type_is_void(ins->type)) continue;
            if (fputs("  or $t0, $a0, $zero\n", out) == EOF)
                return false;
            if (minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!small_integer_width(program, target, ins->type,
                                         &width, &sign) ||
                    !emit_integer_normalize(out, width, sign)) return false;
            }
            break;
        }
        case MINIC_CORE_INSTRUCTION_PARAMETER: {
            size_t index = (size_t)ins->value.parameter_index;
            if (index >= fn->parameter_count) return false;
            if (index < 8U) {
                if (!emit_sp_mem(out, minic_type_is_pointer(ins->type) ? "st.d" : "st.w",
                             index == 0U ? "$a0" : index == 1U ? "$a1" :
                             index == 2U ? "$a2" : index == 3U ? "$a3" :
                             index == 4U ? "$a4" : index == 5U ? "$a5" :
                             index == 6U ? "$a6" : "$a7",
                             offset))
                    return false;
            } else {
                /* LoongArch lp64d: eight integer argument registers; the
                 * ninth and tenth int use 8-byte caller stack slots.
                 * Stack slots are addressed after allocating our frame.
                 * The largest offset is 2016+8, within signed imm12. */
                size_t incoming = frame + (index - 8U) * 8U;
                if (!emit_sp_mem(out, minic_type_is_pointer(ins->type)
                                        ? "ld.d" : "ld.w", "$t0", incoming) ||
                    !emit_sp_mem(out, minic_type_is_pointer(ins->type)
                                        ? "st.d" : "st.w", "$t0", offset))
                    return false;
            }
            continue;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_equal(fn->values[lhs].type, ins->type) ||
                !small_integer_width(program, target, fn->values[lhs].type,
                                     &lw, &ls) ||
                !small_integer_width(program, target, fn->values[rhs].type,
                                     &rw, &rs) ||
                minic_type_is_bool_integer(ins->type) ||
                !emit_sp_mem(out, "ld.w", "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "ld.w", "$t1", (size_t)rhs * 8U) ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                    ? "  sll.w $t0, $t0, $t1\n"
                    : (ls ? "  sra.w $t0, $t0, $t1\n"
                          : "  srl.w $t0, $t0, $t1\n"), out) == EOF ||
                !emit_integer_normalize(out, lw, ls)) return false;
            (void)rs;
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (operand >= fn->value_count ||
                !minic_type_equal(fn->values[operand].type, ins->type) ||
                !small_integer_width(program, target, ins->type, &width, &sign) ||
                minic_type_is_bool_integer(ins->type) ||
                !emit_sp_mem(out, "ld.w", "$t0", (size_t)operand * 8U) ||
                fputs("  sub.w $t0, $zero, $t0\n", out) == EOF ||
                !emit_integer_normalize(out, width, sign)) return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY: {
            MinicCoreValueId lhs = ins->value.binary.left;
            MinicCoreValueId rhs = ins->value.binary.right;
            const char *opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD
                ? "  add.w $t0, $t0, $t1\n" :
                ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT
                    ? "  sub.w $t0, $t0, $t1\n" : "  mul.w $t0, $t0, $t1\n";
            size_t lw, rw;
            bool ls, rs;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !small_integer_width(program, target, fn->values[lhs].type, &lw, &ls) ||
                !small_integer_width(program, target, fn->values[rhs].type, &rw, &rs) ||
                lw != rw || ls != rs ||
                !minic_type_equal(fn->values[lhs].type, ins->type) ||
                minic_type_is_bool_integer(ins->type) ||
                !emit_sp_mem(out, "ld.w", "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "ld.w", "$t1", (size_t)rhs * 8U) ||
                fputs(opcode, out) == EOF ||
                !emit_integer_normalize(out, lw, ls)) return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            const char *opcode;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !small_integer_width(program, target, fn->values[lhs].type,
                                     &lw, &ls) ||
                !small_integer_width(program, target, fn->values[rhs].type,
                                     &rw, &rs) ||
                lw != 4U || rw != 4U || ls != rs ||
                !emit_sp_mem(out, "ld.w", "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "ld.w", "$t1", (size_t)rhs * 8U))
                return false;
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
                ? "  and $t0, $t0, $t1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
                    ? "  xor $t0, $t0, $t1\n"
                    : "  or $t0, $t0, $t1\n";
            if (fputs(opcode, out) == EOF) return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
            MinicCoreValueId lhs = ins->value.binary.left;
            MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !small_integer_width(program, target, fn->values[lhs].type, &lw, &ls) ||
                !small_integer_width(program, target, fn->values[rhs].type, &rw, &rs) ||
                lw != 4U || rw != 4U || ls != rs ||
                !emit_sp_mem(out, "ld.w", "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "ld.w", "$t1", (size_t)rhs * 8U) ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE
                          ? (ls ? "  div.w $t0, $t0, $t1\n"
                                : "  div.wu $t0, $t0, $t1\n")
                          : (ls ? "  mod.w $t0, $t0, $t1\n"
                                : "  mod.wu $t0, $t0, $t1\n"), out) == EOF)
                return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL &&
                lhs < fn->value_count && rhs < fn->value_count &&
                minic_type_is_pointer(fn->values[lhs].type) &&
                minic_type_is_pointer(fn->values[rhs].type)) {
                if (!emit_sp_mem(out, "ld.d", "$t0", (size_t)lhs * 8U) ||
                    !emit_sp_mem(out, "ld.d", "$t1", (size_t)rhs * 8U) ||
                    fputs("  xor $t0, $t0, $t1\n"
                          "  sltui $t0, $t0, 1\n", out) == EOF)
                    return false;
                break;
            }
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !small_integer_width(program, target, fn->values[lhs].type, &lw, &ls) ||
                !small_integer_width(program, target, fn->values[rhs].type, &rw, &rs) ||
                lw != 4U || rw != 4U || ls != rs ||
                !emit_sp_mem(out, "ld.w", "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "ld.w", "$t1", (size_t)rhs * 8U) ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS
                          ? (ls ? "  slt $t0, $t0, $t1\n"
                                : "  sltu $t0, $t0, $t1\n")
                          : "  xor $t0, $t0, $t1\n  sltui $t0, $t0, 1\n", out) == EOF)
                return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO: {
            const MinicCoreValueId v = ins->value.operand;
            bool pointer;
            if (v >= fn->value_count) return false;
            pointer = minic_type_is_pointer(fn->values[v].type);
            if (!pointer && !valid_int_or_bool(fn->values[v].type))
                return false;
            if (!emit_sp_mem(out, pointer ? "ld.d" : "ld.w", "$t0",
                             (size_t)v * 8U) ||
                fputs(pointer ? "  sltui $t0, $t0, 1\n"
                              : "  sltui $t0, $t0, 1\n", out) == EOF)
                return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
            MinicCoreValueId src = ins->value.operand;
            size_t src_width, dst_width;
            bool src_sign, dst_sign;
            if (src >= fn->value_count ||
                !small_integer_width(program, target,
                                     fn->values[src].type, &src_width, &src_sign) ||
                !small_integer_width(program, target, ins->type,
                                     &dst_width, &dst_sign) ||
                !emit_sp_mem(out, "ld.w", "$t0", (size_t)src * 8U) ||
                !emit_integer_normalize(out, dst_width, dst_sign)) return false;
            (void)src_width;
            (void)src_sign;
            break;
        }
        default:
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                              "MINIC_M8_OPCODE_REJECT arch=loongarch64 function=%s block=%zu instruction=%u kind=%d\n",
                              name, block_id, (unsigned int)ins_id, (int)ins->kind);
            return false;
        }
        if (!emit_sp_mem(out, minic_type_is_pointer(ins->type)
                              ? "st.d" : "st.w", "$t0", offset)) return false;
    }
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if ((!minic_type_is_void(fn->return_type) &&
                 !emit_sp_mem(out, minic_type_is_pointer(fn->return_type)
                                         ? "ld.d" : "ld.w",
                                   "$a0",
                                   (size_t)block->terminator.return_value * 8U)) ||
                (has_direct_calls && !emit_sp_mem(out, "ld.d", "$ra", save_offset)) ||
                (!emit_sp_adjust(out, frame, true)) ||
                fputs("  ret\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
            if (!emit_sp_mem(out, "ld.w", "$t0",
                             (size_t)block->terminator.conditional.condition * 8U) ||
                fprintf(out,
                        "  bnez $t0, .Lmini_loong_f%zu_b%u\n"
                        "  b .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.conditional.when_true,
                        id, (unsigned int)block->terminator.conditional.when_false) < 0)
                return false;
            break;
        default: return false;
        }
    }
    if (fprintf(out, ".size %s,.-%s\n", name, name) < 0) return false;
    return true;
}

bool minic_loongarch64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    MinicDiagnostic *diagnostic) {
    size_t i;
    FILE *out;
    bool success = true;
    if (path == NULL || program == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        (core_function_count > 0U && core_functions == NULL) ||
        core_function_count != program->function_count)
        return fail(diagnostic, path, "LoongArch64: invalid Core or target configuration");
    if (program->file_asm_count != 0U)
        return fail(diagnostic, path, "LoongArch64: file asm not supported");
    out = fopen(path, "wb");
    if (out == NULL)
        return fail(diagnostic, path, "LoongArch64: cannot open GNU assembly output");
    for (i = 0U; i < program->global_object_count && success; ++i) {
        const MinicGlobalObject *obj = &program->global_objects[i];
        if (!obj->is_extern)
            success = emit_simple_global(out, program, target, obj);
        else if (obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID)
            success = false;
        if (!success && getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr, "MINIC_M4_GLOBAL_FRONTIER arch=loongarch64 name=%s init=%zu reloc=%zu\n",
                          obj->name != NULL ? obj->name : "<unnamed>",
                          obj->initializer_count, obj->relocation_count);
    }
    for (i = 0U; i < program->function_count && success; ++i) {
        const MinicFunction *decl = &program->functions[i];
        if (decl->is_defined && (!decl->is_internal || decl->is_referenced)) {
            success = emit_function(out, program, target, decl, &core_functions[i],
                                    i, function_sections);
            if (!success && getenv("MINIC_M1_TRACE") != NULL) {
                const MinicCoreFunction *fn = &core_functions[i];
                (void)fprintf(stderr,
                              "MINIC_M1_FRONTIER arch=loongarch64 function=%s "
                              "core_blocks=%zu core_objects=%zu parameters=%zu "
                              "values=%zu instructions=%zu\n",
                              fn->name != NULL ? fn->name : "<unnamed>",
                              fn->block_count, fn->object_count,
                              fn->parameter_count, fn->value_count,
                              fn->instruction_count);
                (void)minic_core_function_dump(stderr, fn);
            }
        }
    }
    if (success && fprintf(out, ".section .note.GNU-stack,\"\",@progbits\n") < 0)
        success = false;
    if (fclose(out) != 0) success = false;
    if (!success) {
        (void)remove(path);
        return fail(diagnostic, path,
                    "LoongArch64: Core instruction/ABI not yet supported by M1 emitter");
    }
    return true;
}
