#include "target/x86_64/codegen.h"

#include "target/data_layout.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-Core-value 8-byte frame slots, or 16 bytes for int128.
 * The extra limb is x86 backend-owned; Core IR identities are unchanged. */
typedef struct MinicX86Frame {
    size_t value_count;
    size_t *value_offsets;
    size_t *object_offsets;
    size_t object_count;
    size_t frame_size;
    size_t object_base_alignment;
    bool objects_from_aligned_rsp;
    /* SysV x86-64 va_list: gp_offset / fp_offset / overflow / reg_save.
     * Each va_start owns its own 24-byte cursor tag, while the incoming
     * argument registers have one shared 176-byte save area per function. */
    MinicCoreValueId *varargs_results;
    size_t varargs_count;
    size_t varargs_regsave_offset;
    size_t varargs_tag_base_offset;
    size_t varargs_fixed_stack_slots;
    unsigned int varargs_gp_offset;
    unsigned int varargs_fp_offset;
} MinicX86Frame;

static void x86_error(MinicDiagnostic *diag, const char *path, const char *message) {
    if (diag != NULL) {
        diag->path = path;
        diag->line = 1U;
        diag->column = 1U;
        (void)snprintf(diag->message, sizeof(diag->message), "%s", message);
    }
}

static bool x86_align(size_t value, size_t alignment, size_t *out) {
    size_t padding;
    if (out == NULL || alignment == 0U) return false;
    padding = (alignment - value % alignment) % alignment;
    if (value > SIZE_MAX - padding) return false;
    *out = value + padding;
    return true;
}

static bool x86_scalar_width(const MinicC0Program *program,
                             const MinicTargetInfo *target,
                             MinicType type, size_t *bytes, bool *is_signed) {
    unsigned int bits;
    MinicType effective;
    if (bytes == NULL || is_signed == NULL) return false;
    if (minic_type_is_pointer(type)) {
        *bytes = target->data_layout->pointer_size;
        *is_signed = false;
        return *bytes == 8U;
    }
    if (!minic_type_is_integer(type) ||
        !minic_c0_type_effective_integer_type(program, type, &effective) ||
        !minic_target_info_integer_width(target, program, type, &bits) ||
        bits == 0U || bits > 64U) return false;
    *bytes = (bits + 7U) / 8U;
    *is_signed = minic_type_is_signed_integer(effective);
    return *bytes == 1U || *bytes == 2U || *bytes == 4U || *bytes == 8U;
}

/* Values are stored as raw machine bits in eight-byte Core slots.  Floating
 * load/store must not pass through the integer sign-extension path. */
static bool x86_storage_width(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type,
                              size_t *bytes, bool *sign) {
    if (minic_type_is_float(type) || minic_type_is_double(type)) {
        if (bytes == NULL || sign == NULL) return false;
        *bytes = minic_type_is_float(type) ? 4U : 8U;
        *sign = false;
        return true;
    }
    return x86_scalar_width(program, target, type, bytes, sign);
}

/* SQLite's Token is a two-eightbyte INTEGER-class SysV aggregate:
 * one pointer at byte 0, one 32-bit integer at byte 8, size 16.
 * Deliberately do not claim general record ABI classification here.
 * Reject packed/misaligned, floating and nested records until proved. */
static bool x86_two_gp_record(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type) {
    const MinicRecord *rec;
    const MinicRecordField *a, *b;
    size_t bytes, align, a_offset, b_offset, a_bytes, b_bytes, ignored;
    if (program == NULL || target == NULL || !minic_type_is_record(type))
        return false;
    rec = minic_c0_program_record(program, type.record_id);
    if (rec == NULL || !rec->is_complete || rec->is_union ||
        rec->is_packed || rec->field_count != 2U)
        return false;
    a = minic_c0_record_field(rec, 0U);
    b = minic_c0_record_field(rec, 1U);
    if (a == NULL || b == NULL || a->is_packed || b->is_packed ||
        a->is_bit_field || b->is_bit_field ||
        a->element_count != 1U || b->element_count != 1U ||
        a->is_array || b->is_array ||
        !minic_type_is_pointer(a->type) || !minic_type_is_integer(b->type))
        return false;
    return minic_data_layout_type(target->data_layout, program, type, &bytes, &align) &&
           minic_data_layout_type(target->data_layout, program, a->type, &a_bytes, &ignored) &&
           minic_data_layout_type(target->data_layout, program, b->type, &b_bytes, &ignored) &&
           minic_data_layout_record_field_offset(target->data_layout, program, rec, 0U, &a_offset) &&
           minic_data_layout_record_field_offset(target->data_layout, program, rec, 1U, &b_offset) &&
           bytes == 16U && align == 8U &&
           a_offset == 0U && b_offset == 8U &&
           a_bytes == 8U && b_bytes == 4U;
}

/* Shared SysV register-bank/stack cursor for incoming parameters.
 * A two-GP record is indivisible for register allocation: if fewer than
 * two registers remain, both eightbytes go on the argument stack. */
static bool x86_incoming_cursor(const MinicC0Program *program,
                                const MinicTargetInfo *target,
                                const MinicCoreFunction *fn, size_t position,
                                size_t *gpr, size_t *fp, size_t *stack) {
    size_t i, width;
    bool signedness;
    if (fn == NULL || position >= fn->parameter_count ||
        gpr == NULL || fp == NULL || stack == NULL) return false;
    *gpr = *fp = *stack = 0U;
    for (i = 0U; i < position; ++i) {
        MinicType t = fn->parameter_types[i];
        if (minic_type_is_float(t) || minic_type_is_double(t)) {
            if (*fp < 8U) ++*fp;
            else ++*stack;
        } else if (minic_type_is_record(t)) {
            if (!x86_two_gp_record(program, target, t)) return false;
            if (*gpr <= 4U) *gpr += 2U;
            else *stack += 2U;
        } else {
            if (!x86_scalar_width(program, target, t, &width, &signedness))
                return false;
            if (*gpr < 6U) ++*gpr;
            else ++*stack;
        }
    }
    return true;
}

static bool x86_normalize(FILE *out, const MinicC0Program *program,
                          const MinicTargetInfo *target, MinicType type) {
    size_t bytes;
    bool sign;
    if (!x86_scalar_width(program, target, type, &bytes, &sign)) return false;
    if (minic_type_is_bool_integer(type))
        return fprintf(out, "  testq %%rax, %%rax\n  setne %%al\n  movzbq %%al, %%rax\n") >= 0;
    switch (bytes) {
    case 1U: return fprintf(out, sign ? "  movsbq %%al, %%rax\n" : "  movzbq %%al, %%rax\n") >= 0;
    case 2U: return fprintf(out, sign ? "  movswq %%ax, %%rax\n" : "  movzwq %%ax, %%rax\n") >= 0;
    case 4U: return fprintf(out, sign ? "  movslq %%eax, %%rax\n" : "  movl %%eax, %%eax\n") >= 0;
    case 8U: return true;
    }
    return false;
}

static bool x86_value_load(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id,
                           const char *reg) {
    if (frame == NULL || id >= frame->value_count ||
        frame->value_offsets == NULL) return false;
    return fprintf(out, "  movq -%zu(%%rbp), %s\n", frame->value_offsets[id], reg) >= 0;
}

static bool x86_value_store(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id) {
    if (frame == NULL || id >= frame->value_count ||
        frame->value_offsets == NULL) return false;
    return fprintf(out, "  movq %%rax, -%zu(%%rbp)\n", frame->value_offsets[id]) >= 0;
}

/* Low limb at -offset(%rbp), high limb at -offset+8(%rbp). */
static bool x86_i128_load(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id,
                          const char *low, const char *high) {
    size_t offset;
    if (frame == NULL || id >= frame->value_count ||
        frame->value_offsets == NULL || low == NULL || high == NULL) return false;
    offset = frame->value_offsets[id];
    if (offset < 16U) return false;
    return fprintf(out,
        "  movq -%zu(%%rbp), %s\n"
        "  movq -%zu(%%rbp), %s\n", offset, low, offset - 8U, high) >= 0;
}

static bool x86_i128_store(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id,
                           const char *low, const char *high) {
    size_t offset;
    if (frame == NULL || id >= frame->value_count ||
        frame->value_offsets == NULL || low == NULL || high == NULL) return false;
    offset = frame->value_offsets[id];
    if (offset < 16U) return false;
    return fprintf(out,
        "  movq %s, -%zu(%%rbp)\n"
        "  movq %s, -%zu(%%rbp)\n", low, offset, high, offset - 8U) >= 0;
}

static bool x86_object_address(FILE *out, const MinicX86Frame *frame,
                               MinicCoreObjectId id, const char *reg) {
    if (out == NULL || frame == NULL || id >= frame->object_count || reg == NULL)
        return false;
    return frame->objects_from_aligned_rsp
        ? fprintf(out, "  leaq %zu(%%rsp), %s\n", frame->object_offsets[id], reg) >= 0
        : fprintf(out, "  leaq -%zu(%%rbp), %s\n", frame->object_offsets[id], reg) >= 0;
}

static bool x86_frame_init(const MinicC0Program *program, const MinicTargetInfo *target,
                           const MinicCoreFunction *fn, MinicX86Frame *frame) {
    size_t pos, i, value_bytes, maximum_alignment = 16U;
    if (program == NULL || target == NULL || fn == NULL || frame == NULL ||
        fn->value_count > (size_t)INT32_MAX / 8U) return false;
    (void)memset(frame, 0, sizeof(*frame));
    frame->value_count = fn->value_count;
    frame->object_count = fn->object_count;
    value_bytes = 0U;
    if (fn->value_count != 0U) {
        frame->value_offsets = (size_t *)calloc(fn->value_count,
                                                 sizeof(*frame->value_offsets));
        if (frame->value_offsets == NULL) return false;
    }
    for (i = 0U; i < fn->value_count; ++i) {
        size_t slot = minic_type_is_int128_integer(fn->values[i].type) ? 16U : 8U;
        if (value_bytes > (size_t)INT32_MAX - slot) return false;
        value_bytes += slot;
        frame->value_offsets[i] = value_bytes;
    }
    /* Discover semantic va_start sites from Core. A VA cursor is not an RV64
     * sequential argument pointer under SysV x86-64. */
    for (i = 0U; i < fn->instruction_count; ++i) {
        if (fn->instructions[i].kind == MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS)
            ++frame->varargs_count;
    }
    if (frame->varargs_count != 0U) {
        size_t count = 0U, gp_used = 0U, fp_used = 0U, stack_used = 0U;
        size_t width;
        bool ignored_signed;
        if (frame->varargs_count > SIZE_MAX / sizeof(*frame->varargs_results) ||
            (frame->varargs_results = (MinicCoreValueId *)calloc(
                 frame->varargs_count, sizeof(*frame->varargs_results))) == NULL)
            return false;
        for (i = 0U; i < fn->instruction_count; ++i) {
            const MinicCoreInstruction *ins = &fn->instructions[i];
            if (ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS) {
                if (ins->result >= fn->value_count) return false;
                frame->varargs_results[count++] = ins->result;
            }
        }
        for (i = 0U; i < fn->parameter_count; ++i) {
            MinicType p = fn->parameter_types[i];
            if (minic_type_is_double(p) || minic_type_is_float(p)) {
                if (fp_used < 8U) ++fp_used;
                else ++stack_used;
            } else {
                if (!x86_scalar_width(program, target, p, &width, &ignored_signed))
                    return false; /* Aggregate SysV classification not yet implemented. */
                if (gp_used < 6U) ++gp_used;
                else ++stack_used;
            }
        }
        if (stack_used > ((size_t)INT32_MAX - 16U) / 8U ||
            value_bytes > (size_t)INT32_MAX - 176U ||
            !x86_align(value_bytes + 176U, 16U, &frame->varargs_regsave_offset) ||
            frame->varargs_count > ((size_t)INT32_MAX -
                                    frame->varargs_regsave_offset) / 24U)
            return false;
        frame->varargs_tag_base_offset = frame->varargs_regsave_offset;
        frame->varargs_fixed_stack_slots = stack_used;
        frame->varargs_gp_offset = (unsigned int)(gp_used * 8U);
        frame->varargs_fp_offset = (unsigned int)(48U + fp_used * 16U);
    }
    if (fn->object_count != 0U) {
        if (fn->object_count > SIZE_MAX / sizeof(*frame->object_offsets)) return false;
        frame->object_offsets = (size_t *)calloc(fn->object_count, sizeof(*frame->object_offsets));
        if (frame->object_offsets == NULL) return false;
    }
    /* %rbp is only 16-byte aligned by the SysV function entry invariant.
     * Above 16, a separate %rsp-relative, dynamically aligned object base
     * is needed.  An offset aligned relative to %rbp is NOT enough. */
    for (i = 0U; i < fn->object_count; ++i) {
        const MinicCoreObject *obj = &fn->objects[i];
        size_t size, alignment;
        if (!minic_data_layout_type(target->data_layout, program, obj->type,
                                    &size, &alignment)) return false;
        if (obj->explicit_alignment > alignment) alignment = obj->explicit_alignment;
        if (alignment == 0U || (alignment & (alignment - 1U)) != 0U ||
            alignment > (size_t)INT32_MAX) return false;
        if (alignment > maximum_alignment) maximum_alignment = alignment;
    }
    frame->object_base_alignment = maximum_alignment;
    frame->objects_from_aligned_rsp = maximum_alignment > 16U;
    pos = frame->objects_from_aligned_rsp ? 0U :
          frame->varargs_count != 0U
              ? frame->varargs_tag_base_offset + frame->varargs_count * 24U
              : value_bytes;
    for (i = 0U; i < fn->object_count; ++i) {
        const MinicCoreObject *obj = &fn->objects[i];
        size_t size, alignment;
        if (!minic_data_layout_type(target->data_layout, program, obj->type,
                                    &size, &alignment) ||
            obj->element_count == 0U || size > SIZE_MAX / obj->element_count)
            return false;
        size *= obj->element_count;
        if (obj->explicit_alignment > alignment) alignment = obj->explicit_alignment;
        if (frame->objects_from_aligned_rsp) {
            /* Positive offsets from the aligned %rsp denote object START. */
            if (!x86_align(pos, alignment, &pos) || pos > SIZE_MAX - size)
                return false;
            frame->object_offsets[i] = pos;
            pos += size;
        } else {
            /* Negative offsets from aligned %rbp denote object START.
             * Align the END of the reserved extent, not its low address. */
            if (pos > SIZE_MAX - size ||
                !x86_align(pos + size, alignment, &pos))
                return false;
            frame->object_offsets[i] = pos;
        }
        if (pos > (size_t)INT32_MAX) return false;
    }
    if (frame->objects_from_aligned_rsp) {
        /* This slack leaves dynamically aligned objects below all 8-byte
         * Core slots even when %rsp rounds down by alignment - 1 bytes. */
        size_t fixed_bytes = frame->varargs_count != 0U
            ? frame->varargs_tag_base_offset + frame->varargs_count * 24U
            : value_bytes;
        if (pos > SIZE_MAX - fixed_bytes ||
            pos + fixed_bytes > SIZE_MAX - maximum_alignment)
            return false;
        pos += fixed_bytes + maximum_alignment;
    }
    return x86_align(pos, 16U, &frame->frame_size) &&
           frame->frame_size <= (size_t)INT32_MAX;
}

static void x86_frame_destroy(MinicX86Frame *frame) {
    if (frame == NULL) return;
    free(frame->value_offsets);
    free(frame->object_offsets);
    free(frame->varargs_results);
    (void)memset(frame, 0, sizeof(*frame));
}

static bool x86_load_memory(FILE *out, size_t bytes, bool sign) {
    switch (bytes) {
    case 1U: return fprintf(out, sign ? "  movsbq (%%r10), %%rax\n" : "  movzbq (%%r10), %%rax\n") >= 0;
    case 2U: return fprintf(out, sign ? "  movswq (%%r10), %%rax\n" : "  movzwq (%%r10), %%rax\n") >= 0;
    case 4U: return fprintf(out, sign ? "  movslq (%%r10), %%rax\n" : "  movl (%%r10), %%eax\n") >= 0;
    case 8U: return fprintf(out, "  movq (%%r10), %%rax\n") >= 0;
    }
    return false;
}

static bool x86_store_memory(FILE *out, size_t bytes) {
    switch (bytes) {
    case 1U: return fprintf(out, "  movb %%al, (%%r10)\n") >= 0;
    case 2U: return fprintf(out, "  movw %%ax, (%%r10)\n") >= 0;
    case 4U: return fprintf(out, "  movl %%eax, (%%r10)\n") >= 0;
    case 8U: return fprintf(out, "  movq %%rax, (%%r10)\n") >= 0;
    }
    return false;
}

/* cJSON's static error/hook tables require initialized records containing
 * pointers to objects and functions. Keep this as a target-owned simple-record
 * emitter; nested aggregates and bit-fields still fail closed. */
static const char *x86_data_directive(size_t size) {
    return size == 1U ? ".byte" : size == 2U ? ".word" :
           size == 4U ? ".long" : size == 8U ? ".quad" : NULL;
}

static bool x86_emit_data_bits(FILE *out, size_t size, uint64_t bits) {
    const char *directive = x86_data_directive(size);
    if (directive == NULL) return false;
    if (size < 8U) bits &= (UINT64_C(1) << (size * 8U)) - UINT64_C(1);
    return fprintf(out, "  %s %" PRIu64 "\n", directive, bits) >= 0;
}

static bool x86_emit_relocation(FILE *out, const MinicC0Program *program,
                                const MinicTargetInfo *target,
                                const MinicGlobalRelocation *reloc, size_t size) {
    const char *name = NULL;
    const char *directive = x86_data_directive(size);
    int64_t addend;
    if (reloc == NULL || directive == NULL || size != 8U) return false;
    if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
        /* Lua lvm.c's dispatch table has one relocation for each local
         * GNU label. Core already records the statement ID and emits its
         * .Luser_<id> alias at the owning basic block, just as RV64 does. */
        const MinicStatement *label = minic_c0_program_statement(program, reloc->target_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               fprintf(out, "  %s .Luser_%zu\n", directive, reloc->target_id) >= 0;
    }
    if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {
        const MinicGlobalObject *object = minic_c0_program_global_object(program, reloc->target_id);
        if (object != NULL) name = object->name;
    } else if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        const MinicFunction *function = minic_c0_program_function(program, reloc->target_id);
        if (function != NULL) name = minic_c0_function_symbol_name(function);
    }
    if (name == NULL || name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(target->data_layout,
                                                           program, reloc, &addend)) return false;
    if (addend == 0)
        return fprintf(out, "  %s %s\n", directive, name) >= 0;
    return fprintf(out, "  %s %s%+" PRId64 "\n", directive, name, addend) >= 0;
}

/* Lua 5.5.0 has top-level records with selected nested union members.
 * Reuse Core's flattened initializer slots and selected-member metadata,
 * and the shared DataLayout for the exact target byte offsets. This is a
 * bounded target-owned emitter: bit-fields/flexible fields fail closed. */
static bool x86_emit_recursive_global_value(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, MinicType type, size_t *initializer_index,
    size_t *relocation_index, size_t *matched_union_selections,
    size_t *emitted_size, unsigned int depth) {
    size_t type_size, type_alignment;
    if (out == NULL || program == NULL || target == NULL || object == NULL ||
        initializer_index == NULL || relocation_index == NULL ||
        matched_union_selections == NULL || emitted_size == NULL || depth > 32U ||
        !minic_data_layout_type(target->data_layout, program, type,
                                &type_size, &type_alignment))
        return false;
    (void)type_alignment;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type) ||
        minic_type_is_float(type) || minic_type_is_double(type)) {
        const MinicGlobalRelocation *reloc =
            *relocation_index < object->relocation_count
                ? &object->relocations[*relocation_index] : NULL;
        if (*initializer_index >= object->initializer_count ||
            x86_data_directive(type_size) == NULL)
            return false;
        if (reloc != NULL &&
            reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
            reloc->location_index == *initializer_index) {
            if (object->initializer_values[*initializer_index] != 0U ||
                (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
                !x86_emit_relocation(out, program, target, reloc, type_size))
                return false;
            ++*relocation_index;
        } else if (!x86_emit_data_bits(
                       out, type_size, object->initializer_values[*initializer_index]))
            return false;
        ++*initializer_index;
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *arr =
            minic_c0_program_array_type(program, type.array_type_id);
        size_t cursor = 0U, i;
        if (arr == NULL || arr->element_count == 0U) return false;
        for (i = 0U; i < arr->element_count; ++i) {
            size_t emitted;
            if (!x86_emit_recursive_global_value(
                    out, program, target, object, arr->element_type,
                    initializer_index, relocation_index, matched_union_selections,
                    &emitted, depth + 1U) ||
                emitted > type_size - cursor)
                return false;
            cursor += emitted;
        }
        if (fprintf(out, "  .zero %zu\n", type_size - cursor) < 0)
            return false;
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record =
            minic_c0_program_record(program, type.record_id);
        size_t begin = 0U, end, start_slot = *initializer_index;
        size_t union_span = 0U, cursor = 0U, i;
        if (record == NULL || !record->is_complete || record->field_count == 0U)
            return false;
        end = record->field_count;
        if (record->is_union) {
            size_t selected = 0U;
            if (minic_c0_global_object_union_member_selection(
                    program, object, start_slot, type.record_id, &selected)) {
                if (!minic_c0_global_object_union_member_initializer_span(
                        program, object, start_slot, type.record_id, &union_span))
                    return false;
                ++*matched_union_selections;
            }
            if (selected >= record->field_count) return false;
            begin = selected;
            end = selected + 1U;
        }
        for (i = begin; i < end; ++i) {
            const MinicRecordField *field = minic_c0_record_field(record, i);
            size_t field_offset, element;
            if (field == NULL || field->is_bit_field ||
                field->is_flexible_array || field->element_count == 0U)
                return false;
            if (field->is_zero_length_array) continue;
            if (record->is_union) {
                field_offset = 0U;
            } else if (!minic_data_layout_record_field_offset(
                           target->data_layout, program, record, i,
                           &field_offset)) {
                return false;
            }
            if (field_offset < cursor || field_offset > type_size ||
                fprintf(out, "  .zero %zu\n", field_offset - cursor) < 0)
                return false;
            cursor = field_offset;
            for (element = 0U; element < field->element_count; ++element) {
                size_t emitted;
                if (!x86_emit_recursive_global_value(
                        out, program, target, object, field->type,
                        initializer_index, relocation_index, matched_union_selections,
                        &emitted, depth + 1U) ||
                    emitted > type_size - cursor)
                    return false;
                cursor += emitted;
            }
        }
        if (cursor > type_size ||
            fprintf(out, "  .zero %zu\n", type_size - cursor) < 0)
            return false;
        if (record->is_union && union_span != 0U) {
            size_t end_slot, slot;
            if (start_slot > SIZE_MAX - union_span ||
                (end_slot = start_slot + union_span) > object->initializer_count ||
                *initializer_index > end_slot) return false;
            for (slot = *initializer_index; slot < end_slot; ++slot) {
                const MinicGlobalRelocation *reloc =
                    *relocation_index < object->relocation_count
                        ? &object->relocations[*relocation_index] : NULL;
                if (object->initializer_values[slot] != 0U ||
                    (reloc != NULL &&
                     reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                     reloc->location_index < end_slot))
                    return false;
            }
            *initializer_index = end_slot;
        }
        *emitted_size = type_size;
        return true;
    }
    return false;
}

static bool x86_emit_recursive_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size) {
    size_t initializer_index = 0U, relocation_index = 0U;
    size_t matched_union_selections = 0U, emitted_size;
    if (!minic_type_is_record(object->type) ||
        !x86_emit_recursive_global_value(
            out, program, target, object, object->type,
            &initializer_index, &relocation_index, &matched_union_selections,
            &emitted_size, 0U))
        return false;
    return emitted_size == storage_size &&
           initializer_index == object->initializer_count &&
           relocation_index == object->relocation_count &&
           matched_union_selections == object->union_selection_count;
}

static bool x86_emit_record_global(FILE *out, const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   const MinicGlobalObject *obj, size_t storage_size) {
    const MinicRecord *record;
    size_t i, cursor = 0U, relocation_index = 0U;
    if (!minic_type_is_record(obj->type)) return false;
    record = minic_c0_program_record(program, obj->type.record_id);
    if (record == NULL || !record->is_complete || record->is_union ||
        record->field_count != obj->initializer_count) return false;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        const MinicGlobalRelocation *reloc =
            relocation_index < obj->relocation_count ? &obj->relocations[relocation_index] : NULL;
        size_t off, width, align;
        if (field == NULL || field->is_bit_field || field->is_flexible_array ||
            field->element_count != 1U ||
            (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type) &&
             !minic_type_is_float(field->type) && !minic_type_is_double(field->type)) ||
            !minic_data_layout_type(target->data_layout, program, field->type, &width, &align) ||
            x86_data_directive(width) == NULL ||
            !minic_data_layout_record_field_offset(target->data_layout, program, record, i, &off) ||
            off < cursor || off > storage_size || width > storage_size - off ||
            fprintf(out, "  .zero %zu\n", off - cursor) < 0) return false;
        (void)align;
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == i) {
            if (obj->initializer_values[i] != 0U ||
                !x86_emit_relocation(out, program, target, reloc, width)) return false;
            ++relocation_index;
        } else if (!x86_emit_data_bits(out, width, obj->initializer_values[i])) {
            return false;
        }
        cursor = off + width;
    }
    return relocation_index == obj->relocation_count && cursor <= storage_size &&
           fprintf(out, "  .zero %zu\n", storage_size - cursor) >= 0;
}

/* A flat array of pointer/scalar constants with per-element ELF relocation.
 * zlib's z_errmsg[] is a const pointer array to ten string literals.
 * Validate every relocation slot before writing an address-sized expression. */
static bool x86_emit_relocatable_scalar_array(FILE *out, const MinicC0Program *program,
                                              const MinicTargetInfo *target,
                                              const MinicGlobalObject *obj,
                                              size_t storage_size) {
    const MinicArrayType *arr;
    size_t element_bytes, alignment, i, relocation_index = 0U;
    if (!minic_type_is_array(obj->type)) return false;
    arr = minic_c0_program_array_type(program, obj->type.array_type_id);
    if (arr == NULL ||
        (!minic_type_is_pointer(arr->element_type) &&
         !minic_type_is_integer(arr->element_type)) ||
        !minic_data_layout_type(target->data_layout, program, arr->element_type,
                                &element_bytes, &alignment) ||
        element_bytes == 0U || arr->element_count > SIZE_MAX / element_bytes ||
        arr->element_count * element_bytes != storage_size ||
        arr->element_count != obj->initializer_count)
        return false;
    (void)alignment;
    for (i = 0U; i < arr->element_count; ++i) {
        const MinicGlobalRelocation *reloc =
            relocation_index < obj->relocation_count ?
                &obj->relocations[relocation_index] : NULL;
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == i) {
            if (obj->initializer_values[i] != 0U ||
                !x86_emit_relocation(out, program, target, reloc, element_bytes))
                return false;
            ++relocation_index;
        } else if (!x86_emit_data_bits(out, element_bytes,
                                       obj->initializer_values[i])) return false;
    }
    return relocation_index == obj->relocation_count;
}

/* zlib's level configuration table is an array of flat records containing
 * four shorts and one function pointer. Core already flattens its initializer
 * values and relocation slots in logical field order. Target layout owns
 * record padding and the ELF relocation expressions. This deliberately does
 * NOT claim support for arbitrary nested aggregates, unions or bit-fields. */
static bool x86_emit_flat_record_array(FILE *out, const MinicC0Program *program,
                                       const MinicTargetInfo *target,
                                       const MinicGlobalObject *object,
                                       size_t storage_size) {
    const MinicArrayType *array;
    const MinicRecord *record;
    size_t record_bytes, record_align, element, field_index;
    size_t initializer_index = 0U, relocation_index = 0U;
    size_t matched_union_selections = 0U;
    if (!minic_type_is_array(object->type)) return false;
    array = minic_c0_program_array_type(program, object->type.array_type_id);
    if (array == NULL || array->element_count == 0U ||
        !minic_type_is_record(array->element_type)) return false;
    record = minic_c0_program_record(program, array->element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union ||
        record->field_count == 0U ||
        !minic_data_layout_type(target->data_layout, program, array->element_type,
                                &record_bytes, &record_align) ||
        record_bytes == 0U || array->element_count > SIZE_MAX / record_bytes ||
        array->element_count * record_bytes != storage_size ||
        record->field_count > SIZE_MAX / array->element_count ||
        record->field_count * array->element_count != object->initializer_count)
        return false;
    (void)record_align;
    for (element = 0U; element < array->element_count; ++element) {
        size_t cursor = 0U;
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field = minic_c0_record_field(record, field_index);
            const MinicGlobalRelocation *reloc =
                relocation_index < object->relocation_count
                    ? &object->relocations[relocation_index] : NULL;
            size_t offset, field_bytes, field_align;
            MinicType emitted_type;
            size_t emitted_bytes, emitted_alignment, emitted_offset = 0U;
            if (field == NULL || field->is_bit_field || field->is_flexible_array ||
                field->element_count != 1U ||
                !minic_data_layout_type(target->data_layout, program, field->type,
                                        &field_bytes, &field_align) ||
                !minic_data_layout_record_field_offset(target->data_layout, program,
                                                       record, field_index, &offset) ||
                offset < cursor || offset > record_bytes ||
                field_bytes > record_bytes - offset ||
                fprintf(out, "  .zero %zu\n", offset - cursor) < 0)
                return false;
            (void)field_align;
            emitted_type = field->type;
            if (minic_type_is_record(field->type)) {
                const MinicRecord *nested = minic_c0_program_record(
                    program, field->type.record_id);
                const MinicRecordField *member;
                size_t selected = 0U, selection_span = 0U;
                if (nested == NULL || !nested->is_complete || !nested->is_union ||
                    nested->field_count == 0U) return false;
                if (minic_c0_global_object_union_member_selection(
                        program, object, initializer_index, field->type.record_id,
                        &selected)) {
                    if (!minic_c0_global_object_union_member_initializer_span(
                            program, object, initializer_index, field->type.record_id,
                            &selection_span) ||
                        (selection_span != 0U && selection_span != 1U)) return false;
                    ++matched_union_selections;
                }
                if (selected >= nested->field_count) return false;
                member = minic_c0_record_field(nested, selected);
                if (member == NULL || member->element_count != 1U ||
                    member->is_bit_field || member->is_flexible_array ||
                    !minic_data_layout_record_field_offset(
                        target->data_layout, program, nested, selected, &emitted_offset) ||
                    emitted_offset != 0U) return false;
                emitted_type = member->type;
            }
            if ((!minic_type_is_integer(emitted_type) &&
                 !minic_type_is_pointer(emitted_type) &&
                 !minic_type_is_float(emitted_type) &&
                 !minic_type_is_double(emitted_type)) ||
                !minic_data_layout_type(target->data_layout, program, emitted_type,
                                        &emitted_bytes, &emitted_alignment) ||
                x86_data_directive(emitted_bytes) == NULL ||
                emitted_bytes > field_bytes) return false;
            (void)emitted_alignment;
            if (reloc != NULL &&
                reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                reloc->location_index == initializer_index) {
                if (object->initializer_values[initializer_index] != 0U ||
                    (!minic_type_is_pointer(emitted_type) &&
                     !minic_type_is_integer(emitted_type)) ||
                    !x86_emit_relocation(out, program, target, reloc, emitted_bytes))
                    return false;
                ++relocation_index;
            } else if (!x86_emit_data_bits(out, emitted_bytes,
                                            object->initializer_values[initializer_index])) {
                return false;
            }
            if (fprintf(out, "  .zero %zu\n", field_bytes - emitted_bytes) < 0)
                return false;
            ++initializer_index;
            cursor = offset + field_bytes;
        }
        if (fprintf(out, "  .zero %zu\n", record_bytes - cursor) < 0)
            return false;
    }
    return initializer_index == object->initializer_count &&
           relocation_index == object->relocation_count &&
           matched_union_selections == object->union_selection_count;
}

/* Lua's HOOKKEY/CLIBS/progname pointers are zero-filled objects whose
 * initializer is represented by an ELF relocation, not by a scalar literal
 * slot. The shared DataLayout validates each relocation byte offset. Keep
 * symbol expression emission target-owned; do not invent a generic ABI. */
static bool x86_emit_zero_relocation_global(FILE *out, const MinicC0Program *program,
                                            const MinicTargetInfo *target,
                                            const MinicGlobalObject *obj,
                                            size_t storage_size) {
    size_t cursor = 0U, i, width;
    if (out == NULL || program == NULL || target == NULL || obj == NULL ||
        !obj->is_zero_initialized || obj->initializer_count != 0U ||
        obj->relocation_count == 0U ||
        (!minic_type_is_pointer(obj->type) && !minic_type_is_array(obj->type)))
        return false;
    width = target->data_layout->pointer_size;
    if (width != 8U) return false;
    for (i = 0U; i < obj->relocation_count; ++i) {
        const MinicGlobalRelocation *reloc = &obj->relocations[i];
        size_t offset;
        if (!minic_data_layout_global_relocation_offset(
                target->data_layout, program, obj, reloc, &offset) ||
            offset < cursor || offset > storage_size ||
            width > storage_size - offset ||
            fprintf(out, "  .zero %zu\n", offset - cursor) < 0 ||
            !x86_emit_relocation(out, program, target, reloc, width))
            return false;
        cursor = offset + width;
    }
    return fprintf(out, "  .zero %zu\n", storage_size - cursor) >= 0;
}

/* First real-project pressure point: tiny-AES declares read-only byte tables.
 * Emit only plain scalar/array initializers; aggregate/relocation cases remain
 * explicit failures until a project requires them. */
static bool x86_emit_global(FILE *out, const MinicC0Program *program,
                            const MinicTargetInfo *target, const MinicGlobalObject *obj) {
    MinicType scalar;
    size_t storage_size, alignment, width, scalar_align, i;
    bool array_of_records, all_zero_aggregate = false;
    const char *directive;
    if (obj == NULL || obj->name == NULL || obj->name[0] == '\0' ||
        obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        !minic_data_layout_global_object(target->data_layout, program, obj,
                                         &storage_size, &alignment)) return false;
    scalar = obj->type;
    while (minic_type_is_array(scalar)) {
        const MinicArrayType *arr = minic_c0_program_array_type(program, scalar.array_type_id);
        if (arr == NULL) return false;
        scalar = arr->element_type;
    }
    array_of_records = minic_type_is_array(obj->type) && minic_type_is_record(scalar);
    if (obj->union_selection_count != 0U && !array_of_records &&
        !minic_type_is_record(obj->type)) return false;
    if (minic_type_is_record(scalar)) {
        /* Record and flat record-array payloads are emitted separately. */
        width = 0U;
    } else {
        if ((!minic_type_is_integer(scalar) && !minic_type_is_pointer(scalar) &&
             !minic_type_is_float(scalar) && !minic_type_is_double(scalar)) ||
            !minic_data_layout_type(target->data_layout, program, scalar,
                                    &width, &scalar_align))
            return false;
        (void)scalar_align;
    }
    directive = x86_data_directive(width);
    if (!minic_type_is_record(obj->type) && !array_of_records &&
        (directive == NULL || width == 0U ||
         obj->initializer_count > storage_size / width)) return false;
    if (obj->initializer_count != 0U && obj->initializer_values == NULL) return false;
    /* A fully zero aggregate is bytewise zero for this x86 target even
     * when its type contains nested records, bit-fields and padding.
     * Never take this shortcut for relocations or a nonzero slot. */
    if (minic_type_is_record(obj->type) && obj->relocation_count == 0U &&
        obj->initializer_count != 0U) {
        all_zero_aggregate = true;
        for (i = 0U; i < obj->initializer_count; ++i) {
            if (obj->initializer_values[i] != 0U) {
                all_zero_aggregate = false;
                break;
            }
        }
    }
    if (obj->section_name != NULL) {
        if (fprintf(out, ".section %s,\"%s\",@progbits\n",
                    obj->section_name, obj->is_read_only ? "a" : "aw") < 0) return false;
    } else if (fprintf(out, "%s\n", obj->is_read_only ?
                       (obj->relocation_count != 0U ? ".section .data.rel.ro" : ".section .rodata") :
                       (obj->is_zero_initialized || obj->is_tentative) &&
                       obj->initializer_count == 0U && obj->relocation_count == 0U ?
                           ".bss" : ".data") < 0)
        return false;
    if (!obj->is_internal) {
        const char *visibility_directive = NULL;
        if (fprintf(out, obj->is_weak ? ".weak %s\n" : ".globl %s\n", obj->name) < 0)
            return false;
        switch (obj->visibility) {
        case MINIC_SYMBOL_VISIBILITY_DEFAULT: break;
        case MINIC_SYMBOL_VISIBILITY_HIDDEN: visibility_directive = ".hidden"; break;
        case MINIC_SYMBOL_VISIBILITY_INTERNAL: visibility_directive = ".internal"; break;
        case MINIC_SYMBOL_VISIBILITY_PROTECTED: visibility_directive = ".protected"; break;
        default: return false;
        }
        if (visibility_directive != NULL &&
            fprintf(out, "%s %s\n", visibility_directive, obj->name) < 0)
            return false;
    }
    if (fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                alignment, obj->name, obj->name) < 0) return false;
    if (all_zero_aggregate ||
        ((obj->is_zero_initialized || obj->is_tentative) &&
         obj->initializer_count == 0U && obj->relocation_count == 0U)) {
        if (fprintf(out, "  .zero %zu\n", storage_size) < 0) return false;
    } else if (obj->relocation_count != 0U && obj->is_zero_initialized &&
               obj->initializer_count == 0U) {
        if (!x86_emit_zero_relocation_global(
                out, program, target, obj, storage_size)) return false;
    } else if (minic_type_is_record(obj->type)) {
        const MinicRecord *record = minic_c0_program_record(
            program, obj->type.record_id);
        /* Use the already-shared flattened initializer slot representation
         * for nested records/arrays, even without a selected union.
         * Preserve the established flat-record emission for legacy
         * RECORD_FIELD relocation locations. */
        if (record == NULL) return false;
        if (obj->union_selection_count != 0U ||
            obj->initializer_count != record->field_count) {
            if (!x86_emit_recursive_record_global(
                    out, program, target, obj, storage_size)) return false;
        } else if (!x86_emit_record_global(
                       out, program, target, obj, storage_size)) return false;
    } else if (array_of_records) {
        const MinicArrayType *array = minic_c0_program_array_type(
            program, obj->type.array_type_id);
        const MinicRecord *element_record = array != NULL
            ? minic_c0_program_record(program, array->element_type.record_id)
            : NULL;
        if (array == NULL || element_record == NULL ||
            array->element_count == 0U ||
            element_record->field_count > SIZE_MAX / array->element_count)
            return false;
        if (element_record->field_count * array->element_count !=
            obj->initializer_count) {
            /* SQLite aXformType keeps an inline character array in each
             * record. Its extra flattened slots are already represented in
             * shared initializer metadata; use the existing recursive
             * layout path instead of pretending each record is flat. */
            size_t initializer_index = 0U, relocation_index = 0U;
            size_t matched_union_selections = 0U, emitted_size = 0U;
            if (!x86_emit_recursive_global_value(
                    out, program, target, obj, obj->type, &initializer_index,
                    &relocation_index, &matched_union_selections,
                    &emitted_size, 0U) ||
                emitted_size != storage_size ||
                initializer_index != obj->initializer_count ||
                relocation_index != obj->relocation_count ||
                matched_union_selections != obj->union_selection_count)
                return false;
        } else if (!x86_emit_flat_record_array(
                       out, program, target, obj, storage_size))
            return false;
    } else if (minic_type_is_array(obj->type) && obj->relocation_count != 0U) {
        if (!x86_emit_relocatable_scalar_array(out, program, target, obj,
                                               storage_size)) return false;
    } else if (obj->relocation_count != 0U) {
        /* Simple pointer globals: relocatable target address or function. */
        if (obj->initializer_count != 1U || obj->relocation_count != 1U ||
            obj->relocations[0].location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR ||
            obj->relocations[0].location_index != 0U ||
            obj->initializer_values[0] != 0U || width != storage_size ||
            !x86_emit_relocation(out, program, target, &obj->relocations[0], width)) return false;
    } else {
        for (i = 0U; i < obj->initializer_count; ++i) {
            if (!x86_emit_data_bits(out, width, obj->initializer_values[i])) return false;
        }
        if (storage_size > obj->initializer_count * width &&
            fprintf(out, "  .zero %zu\n",
                    storage_size - obj->initializer_count * width) < 0) return false;
    }
    return fprintf(out, ".size %s,%zu\n", obj->name, storage_size) >= 0;
}

/* SysV uses independent integer and SSE register sequences.  Both direct
 * and indirect calls share this small classification/placement routine; an
 * unsupported argument class or register overflow fails instead of being
 * silently passed through the wrong register bank. */
/* SysV has separate six-GPR and eight-SSE argument banks; exhausted bank
 * values are stack-passed in source order.  This is the existing scalar ABI,
 * not aggregate eightbyte classification (which remains fail-closed). */
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
            if (i >= fixed_count || arg->value.object_id >= fn->object_count ||
                !minic_type_equal(fn->objects[arg->value.object_id].type, fixed_types[i]) ||
                !x86_two_gp_record(program, target, fixed_types[i]))
                return false;
            if (integer_index <= 4U) integer_index += 2U;
            else stack_slots += 2U;
            continue;
        }
        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            arg->value.value_id >= fn->value_count) return false;
        type = i < fixed_count ? fixed_types[i] : fn->values[arg->value.value_id].type;
        if (minic_type_is_double(type) || minic_type_is_float(type)) {
            if (float_index < 8U) ++float_index;
            else ++stack_slots;
        } else {
            if (!x86_scalar_width(program, target, type, &ignored_bytes,
                                  &ignored_sign)) return false;
            if (integer_index < 6U) ++integer_index;
            else ++stack_slots;
        }
    }
    if (stack_slots > (size_t)INT32_MAX / 8U ||
        !x86_align(stack_slots * 8U, 16U, &aligned_bytes) ||
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
            MinicCoreObjectId object = arg->value.object_id;
            if (i >= fixed_count ||
                !x86_two_gp_record(program, target, fixed_types[i]) ||
                !x86_object_address(out, frame, object, "%r11"))
                return false;
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
    if (minic_type_is_double(return_type)) {
        if (fprintf(out, "  movq %%xmm0, %%rax\n") < 0) return false;
    } else if (minic_type_is_float(return_type)) {
        if (fprintf(out, "  movd %%xmm0, %%eax\n") < 0) return false;
    } else if (!x86_normalize(out, program, target, return_type)) {
        return false;
    }
    return x86_value_store(out, frame, result);
}

static bool x86_emit_instruction(FILE *out, const MinicC0Program *program,
                                 const MinicTargetInfo *target, const MinicCoreFunction *fn,
                                 const MinicX86Frame *frame, const MinicCoreInstruction *ins,
                                 size_t function_id) {
    MinicCoreValueId left, right;
    size_t bytes;
    bool sign;
    if (ins == NULL) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (!minic_type_is_float(ins->type) && !minic_type_is_double(ins->type))
            return false;
        return fprintf(out, "  movabsq $%" PRIu64 ", %%rax\n",
                       ins->value.floating_bits) >= 0 &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
        const char *opcode =
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD ? "addsd" :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT ? "subsd" :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY ? "mulsd" : "divsd";
        if (!minic_type_is_double(ins->type) ||
            ins->value.binary.left >= fn->value_count ||
            ins->value.binary.right >= fn->value_count ||
            !x86_value_load(out, frame, ins->value.binary.left, "%rax") ||
            !x86_value_load(out, frame, ins->value.binary.right, "%r10") ||
            fprintf(out,
                    "  movq %%rax, %%xmm0\n"
                    "  movq %%r10, %%xmm1\n"
                    "  %s %%xmm1, %%xmm0\n"
                    "  movq %%xmm0, %%rax\n", opcode) < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
        const MinicCoreValueId operand = ins->value.operand;
        size_t source_bytes;
        bool source_signed;
        if (operand >= fn->value_count || !minic_type_is_double(ins->type) ||
            !x86_scalar_width(program, target, fn->values[operand].type,
                              &source_bytes, &source_signed) ||
            !x86_value_load(out, frame, operand, "%rax"))
            return false;
        if (fprintf(out, "  cvtsi2sdq %%rax, %%xmm0\n") < 0) return false;
        /* Signed cvtsi2sdq is also valid for positive unsigned values.
         * A negative interpretation of an unsigned 64-bit value requires
         * adding exactly 2^64 (encoded as a binary64 constant). */
        if (!source_signed && source_bytes == 8U &&
            fprintf(out,
                    "  testq %%rax, %%rax\n"
                    "  jns 1f\n"
                    "  movabsq $0x43f0000000000000, %%r10\n"
                    "  movq %%r10, %%xmm1\n"
                    "  addsd %%xmm1, %%xmm0\n"
                    "1:\n") < 0)
            return false;
        return fprintf(out, "  movq %%xmm0, %%rax\n") >= 0 &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
        const MinicCoreValueId operand = ins->value.operand;
        size_t dst_bytes;
        bool dst_signed;
        if (operand >= fn->value_count ||
            !minic_type_is_double(fn->values[operand].type) ||
            !minic_type_is_integer(ins->type) ||
            !x86_scalar_width(program, target, ins->type, &dst_bytes, &dst_signed) ||
            !x86_value_load(out, frame, operand, "%rax") ||
            fprintf(out, "  movq %%rax, %%xmm0\n") < 0)
            return false;
        if (!dst_signed && dst_bytes == 8U) {
            /* For C-defined uint64 results, subtract 2^63 before converting
             * the high half, then restore the high bit. Out-of-range and NaN
             * float-to-int conversions have undefined C behavior. */
            if (fprintf(out,
                        "  movabsq $0x43e0000000000000, %%r10\n"
                        "  movq %%r10, %%xmm1\n"
                        "  ucomisd %%xmm1, %%xmm0\n"
                        "  jb 1f\n"
                        "  subsd %%xmm1, %%xmm0\n"
                        "  cvttsd2siq %%xmm0, %%rax\n"
                        "  btsq $63, %%rax\n"
                        "  jmp 2f\n"
                        "1:\n"
                        "  cvttsd2siq %%xmm0, %%rax\n"
                        "2:\n") < 0)
                return false;
        } else if (fprintf(out, "  cvttsd2siq %%xmm0, %%rax\n") < 0) {
            return false;
        }
        return x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
        const MinicCoreValueId operand = ins->value.operand;
        const bool widening = ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE;
        if (operand >= fn->value_count ||
            (widening && (!minic_type_is_float(fn->values[operand].type) ||
                          !minic_type_is_double(ins->type))) ||
            (!widening && (!minic_type_is_double(fn->values[operand].type) ||
                           !minic_type_is_float(ins->type))) ||
            !x86_value_load(out, frame, operand, "%rax"))
            return false;
        if (fprintf(out,
                    widening ? "  movd %%eax, %%xmm0\n"
                               "  cvtss2sd %%xmm0, %%xmm0\n"
                               "  movq %%xmm0, %%rax\n"
                             : "  movq %%rax, %%xmm0\n"
                               "  cvtsd2ss %%xmm0, %%xmm0\n"
                               "  movd %%xmm0, %%eax\n") < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: {
        const char *opcode =
            ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD ? "addss" :
            ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT ? "subss" :
            ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY ? "mulss" : "divss";
        if (!minic_type_is_float(ins->type) ||
            ins->value.binary.left >= fn->value_count ||
            ins->value.binary.right >= fn->value_count ||
            !x86_value_load(out, frame, ins->value.binary.left, "%rax") ||
            !x86_value_load(out, frame, ins->value.binary.right, "%r10") ||
            fprintf(out, "  movd %%eax, %%xmm0\n"
                         "  movd %%r10d, %%xmm1\n"
                         "  %s %%xmm1, %%xmm0\n"
                         "  movd %%xmm0, %%eax\n", opcode) < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        if (!minic_type_is_double(ins->type) ||
            !x86_value_load(out, frame, ins->value.operand, "%rax") ||
            fprintf(out, "  movabsq $9223372036854775808, %%r10\n"
                         "  xorq %%r10, %%rax\n") < 0) return false;
        return x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
        const char *test =
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL ? "sete" :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ? "setb" : "setbe";
        if (ins->value.binary.left >= fn->value_count ||
            ins->value.binary.right >= fn->value_count ||
            !minic_type_is_double(fn->values[ins->value.binary.left].type) ||
            !minic_type_is_double(fn->values[ins->value.binary.right].type) ||
            !x86_value_load(out, frame, ins->value.binary.left, "%rax") ||
            !x86_value_load(out, frame, ins->value.binary.right, "%r10") ||
            fprintf(out, "  movq %%rax, %%xmm0\n"
                         "  movq %%r10, %%xmm1\n"
                         "  ucomisd %%xmm1, %%xmm0\n"
                         "  %s %%al\n"
                         "  setnp %%r10b\n"
                         "  andb %%r10b, %%al\n"
                         "  movzbq %%al, %%rax\n", test) < 0)
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
        if (!x86_scalar_width(program, target, ins->type, &bytes, &sign) ||
            fprintf(out, "  movabsq $%" PRId64 ", %%rax\n", ins->value.integer_value) < 0)
            return false;
        return x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_PARAMETER: {
        static const char *const regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
        size_t gpr, xmm, stack_index;
        const size_t position = ins->value.parameter_index;
        if (!x86_incoming_cursor(program, target, fn, position,
                                 &gpr, &xmm, &stack_index))
            return false;
        if (minic_type_is_double(ins->type) || minic_type_is_float(ins->type)) {
            if (xmm < 8U) {
                if (fprintf(out, minic_type_is_double(ins->type)
                             ? "  movq %%xmm%zu, %%rax\n"
                             : "  movd %%xmm%zu, %%eax\n", xmm) < 0)
                    return false;
            } else if (fprintf(out, "  movq %zu(%%rbp), %%rax\n",
                               16U + stack_index * 8U) < 0) return false;
        } else {
            if (gpr < 6U) {
                if (fprintf(out, "  movq %s, %%rax\n", regs[gpr]) < 0)
                    return false;
            } else if (fprintf(out, "  movq %zu(%%rbp), %%rax\n",
                               16U + stack_index * 8U) < 0) return false;
            if (!x86_normalize(out, program, target, ins->type)) return false;
        }
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT: {
        static const char *const regs[] = {"%rdi","%rsi","%rdx","%rcx","%r8","%r9"};
        size_t gpr, xmm, stack_index;
        size_t position = ins->value.parameter_object.parameter_index;
        MinicCoreObjectId object = ins->value.parameter_object.object_id;
        if (!x86_incoming_cursor(program, target, fn, position,
                                 &gpr, &xmm, &stack_index) ||
            !x86_two_gp_record(program, target, fn->parameter_types[position]) ||
            object >= fn->object_count ||
            !minic_type_equal(fn->objects[object].type, fn->parameter_types[position]) ||
            !x86_object_address(out, frame, object, "%r10"))
            return false;
        (void)xmm;
        if (gpr <= 4U)
            return fprintf(out,
                "  movq %s, 0(%%r10)\n"
                "  movq %s, 8(%%r10)\n", regs[gpr], regs[gpr + 1U]) >= 0;
        return fprintf(out,
            "  movq %zu(%%rbp), %%rax\n"
            "  movq %%rax, 0(%%r10)\n"
            "  movq %zu(%%rbp), %%rax\n"
            "  movq %%rax, 8(%%r10)\n",
            16U + stack_index * 8U, 24U + stack_index * 8U) >= 0;
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
            (!is_fp && !x86_scalar_width(program, target, ins->type,
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
                if (!x86_scalar_width(program, target, source_type,
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
            !x86_scalar_width(program, target, fn->values[operand].type,
                              &src_bytes, &src_sign) ||
            !x86_scalar_width(program, target, ins->type, &bytes, &sign)) return false;
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
        if (minic_type_is_int128_integer(ins->type))
            return x86_value_load(out, frame, ins->value.load.address, "%r10") &&
                   fprintf(out, "  movq (%%r10), %%rax\n"
                                "  movq 8(%%r10), %%rdx\n") >= 0 &&
                   x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        if (!x86_storage_width(program, target, ins->type, &bytes, &sign)) return false;
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
            !x86_storage_width(program, target, pointee, &bytes, &sign))
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
            !x86_storage_width(program, target, pointee, &bytes, &sign))
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
        if (minic_type_is_int128_integer(fn->values[src].type))
            return x86_value_load(out, frame, ins->value.store.address, "%r10") &&
                   x86_i128_load(out, frame, src, "%rax", "%rdx") &&
                   fprintf(out, "  movq %%rax, (%%r10)\n"
                                "  movq %%rdx, 8(%%r10)\n") >= 0;
        if (src >= fn->value_count ||
            !x86_storage_width(program, target, fn->values[src].type, &bytes, &sign))
            return false;
        return x86_value_load(out, frame, ins->value.store.address, "%r10") &&
               x86_value_load(out, frame, src, "%rax") &&
               x86_store_memory(out, bytes);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicCoreValueId lhs = ins->value.integer_overflow.left;
        MinicCoreValueId rhs = ins->value.integer_overflow.right;
        MinicCoreValueId address = ins->value.integer_overflow.result_address;
        MinicType result_type;
        size_t width;
        bool signed_result;
        const char *opcode = NULL, *flag = NULL;
        if (lhs >= fn->value_count || rhs >= fn->value_count ||
            address >= fn->value_count ||
            !minic_type_pointee(fn->values[address].type, &result_type) ||
            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
            minic_type_is_const(result_type) || minic_type_is_volatile(result_type) ||
            !minic_type_equal(result_type, fn->values[lhs].type) ||
            !minic_type_equal(result_type, fn->values[rhs].type) ||
            !x86_scalar_width(program, target, result_type, &width, &signed_result) ||
            (width != 4U && width != 8U))
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
        if (operand >= fn->value_count || !minic_type_is_integer(ins->type) ||
            !minic_type_is_unsigned_integer(fn->values[operand].type) ||
            !minic_target_info_integer_width(target, program,
                                               fn->values[operand].type, &width) ||
            (width != 8U && width != 16U && width != 32U && width != 64U) ||
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
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT)
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
            if (!x86_scalar_width(program, target, fn->values[left].type, &bytes, &sign) ||
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
            if (!x86_scalar_width(program, target, fn->values[left].type, &bytes, &sign) ||
                fprintf(out, sign ? "  movb %%r10b, %%cl\n  sarq %%cl, %%rax\n"
                                  : "  movb %%r10b, %%cl\n  shrq %%cl, %%rax\n") < 0)
                return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
            if (!x86_scalar_width(program, target, fn->values[left].type, &bytes, &sign) ||
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
    case MINIC_CORE_INSTRUCTION_CALL: {
        const size_t begin = ins->value.call.argument_begin;
        const size_t count = ins->value.call.argument_count;
        const size_t callee_id = ins->value.call.callee_id;
        const MinicCoreCallee *callee;
        size_t outgoing_bytes = 0U;
        if (callee_id >= fn->callee_count) return false;
        callee = &fn->callees[callee_id];
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
        const size_t signature_id = ins->value.indirect_call.signature_id;
        const MinicCoreValueId callee = ins->value.indirect_call.callee;
        const size_t begin = ins->value.indirect_call.argument_begin;
        const size_t count = ins->value.indirect_call.argument_count;
        const MinicCoreCallSignature *signature;
        MinicType function_type;
        size_t outgoing_bytes = 0U;
        if (signature_id >= fn->call_signature_count || callee >= fn->value_count ||
            !minic_type_pointee(fn->values[callee].type, &function_type) ||
            !minic_type_is_function(function_type)) return false;
        signature = &fn->call_signatures[signature_id];
        if (function_type.function_type_id != signature->function_type_id ||
            !x86_emit_call_arguments(out, program, target, fn, frame, begin, count,
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
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    default:
        return false;
    }
}

static bool x86_emit_terminator(FILE *out, const MinicX86Frame *frame,
                                const MinicCoreTerminator *term,
                                const MinicCoreFunction *fn, size_t function_id) {
    if (term == NULL) return false;
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (term->return_object != MINIC_CORE_OBJECT_INVALID) return false;
        if (term->return_value != MINIC_CORE_VALUE_INVALID) {
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

static bool x86_emit_function(FILE *out, const MinicC0Program *program,
                              const MinicTargetInfo *target, const MinicFunction *declaration,
                              const MinicCoreFunction *fn, size_t function_id,
                              bool function_sections, MinicDiagnostic *diag, const char *path) {
    MinicX86Frame frame = {0};
    const char *name = minic_c0_function_symbol_name(declaration);
    const char *vis;
    size_t block_index;
    bool success = false;
    if (name == NULL || name[0] == '\0' ||
        !minic_core_function_verify(fn) ||
        !x86_frame_init(program, target, fn, &frame)) {
        x86_frame_destroy(&frame);
        x86_error(diag, path, "x86-64 cannot allocate or verify Core function frame");
        return false;
    }
    if (declaration->section_name != NULL) {
        if (fprintf(out, ".section %s,\"ax\",@progbits\n", declaration->section_name) < 0)
            goto done;
    } else if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",@progbits\n", name) < 0) goto done;
    } else if (fprintf(out, ".text\n") < 0) goto done;
    if (!declaration->is_internal) {
        if (fprintf(out, declaration->is_weak ? ".weak %s\n" : ".globl %s\n", name) < 0)
            goto done;
        vis = declaration->visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN ? ".hidden" :
              declaration->visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL ? ".internal" :
              declaration->visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected" : NULL;
        if (declaration->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
            (vis == NULL || fprintf(out, "%s %s\n", vis, name) < 0)) goto done;
    }
    if (fprintf(out, ".type %s,@function\n%s:\n"
                     "  pushq %%rbp\n  movq %%rsp, %%rbp\n", name, name) < 0)
        goto done;
    if (frame.frame_size != 0U &&
        fprintf(out, "  subq $%zu, %%rsp\n", frame.frame_size) < 0) goto done;
    if (frame.objects_from_aligned_rsp &&
        fprintf(out, "  andq $-%zu, %%rsp\n", frame.object_base_alignment) < 0)
        goto done;
    if (frame.varargs_count != 0U) {
        static const char *const gp_registers[] =
            {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
        size_t k;
        if (fprintf(out, "  leaq -%zu(%%rbp), %%r10\n",
                    frame.varargs_regsave_offset) < 0) goto done;
        for (k = 0U; k < 6U; ++k) {
            if (fprintf(out, "  movq %s, %zu(%%r10)\n",
                        gp_registers[k], k * 8U) < 0) goto done;
        }
        for (k = 0U; k < 8U; ++k) {
            if (fprintf(out, "  movdqu %%xmm%zu, %zu(%%r10)\n",
                        k, 48U + k * 16U) < 0) goto done;
        }
    }
    if (fprintf(out, "  jmp .Lmini_x86_f%zu_b%u\n",
                function_id, (unsigned int)fn->entry_block) < 0) goto done;
    for (block_index = 0U; block_index < fn->block_count; ++block_index) {
        const MinicCoreBlock *block = &fn->blocks[block_index];
        size_t k;
        if (fprintf(out, ".Lmini_x86_f%zu_b%zu:\n", function_id, block_index) < 0) goto done;
        if (block->source_label_id != SIZE_MAX && !fn->suppress_source_label_aliases &&
            fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0) goto done;
        for (k = 0U; k < block->instruction_count; ++k) {
            const MinicCoreInstructionId id = block->instructions[k];
            if (id >= fn->instruction_count ||
                !x86_emit_instruction(out, program, target, fn, &frame,
                                      &fn->instructions[id], function_id)) {
                char message[256];
                (void)snprintf(message, sizeof(message),
                               "x86-64 Core instruction unsupported: function %s, kind %d",
                               name, id < fn->instruction_count ? (int)fn->instructions[id].kind : -1);
                x86_error(diag, path, message);
                goto done;
            }
        }
        if (!block->has_terminator ||
            !x86_emit_terminator(out, &frame, &block->terminator, fn, function_id)) {
            x86_error(diag, path, "x86-64 Core terminator unsupported");
            goto done;
        }
    }
    if (fprintf(out, ".Lmini_x86_f%zu_ret:\n  leave\n  ret\n"
                     ".size %s,.-%s\n", function_id, name, name) < 0) goto done;
    success = true;
done:
    x86_frame_destroy(&frame);
    return success;
}

bool minic_x86_64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *core_functions, size_t core_function_count,
    bool function_sections, MinicDiagnostic *diagnostic) {
    FILE *out;
    size_t i;
    bool success = true;
    if (path == NULL || program == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        (core_function_count != 0U && core_functions == NULL) ||
        core_function_count != program->function_count) {
        x86_error(diagnostic, path, "x86-64 invalid program or target");
        return false;
    }
    if (program->file_asm_count != 0U) {
        x86_error(diagnostic, path, "x86-64 file-scope asm not implemented yet");
        return false;
    }
    out = fopen(path, "wb");
    if (out == NULL) {
        x86_error(diagnostic, path, "x86-64 cannot create assembly output");
        return false;
    }
    for (i = 0U; i < program->global_object_count && success; ++i) {
        const MinicGlobalObject *obj = &program->global_objects[i];
        if (obj->is_extern) {
            if (obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID) {
                x86_error(diagnostic, path, "x86-64 external global alias unsupported");
                success = false;
            }
            continue;
        }
        success = x86_emit_global(out, program, target, obj);
        if (!success) {
            char message[256];
            (void)snprintf(message, sizeof(message),
                           "x86-64 global initializer/relocation unsupported: %s "
                           "(record=%d init=%zu reloc=%zu union=%zu zero=%d tentative=%d)",
                           obj->name != NULL ? obj->name : "<unnamed>",
                           (int)minic_type_is_record(obj->type),
                           obj->initializer_count, obj->relocation_count,
                           obj->union_selection_count,
                           (int)obj->is_zero_initialized, (int)obj->is_tentative);
            x86_error(diagnostic, path, message);
        }
    }
    for (i = 0U; i < program->function_count && success; ++i) {
        const MinicFunction *f = &program->functions[i];
        if (!f->is_defined || (f->is_internal && !f->is_referenced)) continue;
        if (f->alias_target != MINIC_FUNCTION_INVALID) {
            x86_error(diagnostic, path, "x86-64 function alias not implemented yet");
            success = false;
        } else {
            success = x86_emit_function(out, program, target, f, &core_functions[i],
                                        i, function_sections, diagnostic, path);
        }
    }
    /* ELF/GNU-stack contract: emitted objects never request executable stack. */
    if (success && fprintf(out, ".section .note.GNU-stack,\"\",@progbits\n") < 0)
        success = false;
    if (fclose(out) != 0) success = false;
    if (!success) (void)remove(path);
    return success;
}
