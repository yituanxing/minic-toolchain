#include "target/loongarch64/codegen.h"
#include "target/backend_core_usage.h"
#include "target/backend_global_array.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Report the first failed backend guard only in opt-in trace mode.
 * backend_line points to the precise C condition; block/ins/kind to Core IR.
 * This leaves compilation and fail-closed semantics unchanged. */
static bool machine_emit_reject(const MinicCoreFunction *fn, const char *arch,
                                const char *name, const char *stage,
                                size_t block_id, size_t instruction_id,
                                unsigned int backend_line) {
    if (getenv("MINIC_M1_TRACE") != NULL) {
        const int kind = (fn != NULL && instruction_id < fn->instruction_count)
                             ? (int)fn->instructions[instruction_id].kind : -1;
        (void)fprintf(stderr,
                      "MINIC_BACKEND_REJECT arch=%s function=%s stage=%s "
                      "block=%lld instruction=%lld kind=%d backend_line=%u\n",
                      arch, name != NULL ? name : "<unnamed>", stage,
                      block_id == SIZE_MAX ? -1LL : (long long)block_id,
                      instruction_id == SIZE_MAX ? -1LL : (long long)instruction_id,
                      kind, backend_line);
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

/* M20 LP64 scalar-width gate: only ABI and memory operations accept 64 bits.
 * Narrow arithmetic continues to reject 64-bit values until implemented. */
static bool scalar_integer_width(const MinicC0Program *program,
                                 const MinicTargetInfo *target, MinicType type,
                                 size_t *width, bool *sign) {
    unsigned int bits;
    MinicType effective;
    if (small_integer_width(program, target, type, width, sign)) return true;
    if (width == NULL || sign == NULL || !minic_type_is_integer(type) ||
        !minic_c0_type_effective_integer_type(program, type, &effective) ||
        !minic_target_info_integer_width(target, program, type, &bits) ||
        bits != 64U) return false;
    *width = 8U;
    *sign = minic_type_is_signed_integer(effective);
    return true;
}
static bool register64_type(const MinicC0Program *program,
                            const MinicTargetInfo *target, MinicType type) {
    size_t width;
    bool sign;
    return minic_type_is_pointer(type) ||
           (scalar_integer_width(program, target, type, &width, &sign) &&
            width == 8U);
}
static bool valid_parameter_type(const MinicC0Program *program,
                                 const MinicTargetInfo *target, MinicType type) {
    size_t width;
    bool sign;
    return minic_type_is_pointer(type) || minic_type_is_double(type) ||
           scalar_integer_width(program, target, type, &width, &sign);
}

/* C scalar return ABI is independent from the initial signed-int smoke
 * gate. Core's unsigned char xtime return and its GNU callers must share
 * the actual target integer width; reject unsupported 64/128-bit values. */
static bool valid_return_type(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type) {
    return minic_type_is_void(type) || minic_type_is_double(type) ||
           valid_parameter_type(program, target, type);
}

static bool emit_integer_memory(FILE *out, bool store,
                                size_t width, bool sign) {
    const char *op = NULL;
    if (store) {
        op = width == 1U ? "  st.b $t0, $t1, 0\n" :
             width == 2U ? "  st.h $t0, $t1, 0\n" :
             width == 4U ? "  st.w $t0, $t1, 0\n" :
             width == 8U ? "  st.d $t0, $t1, 0\n" : NULL;
    } else {
        op = width == 1U ? (sign ? "  ld.b $t0, $t1, 0\n" :
                                     "  ld.bu $t0, $t1, 0\n") :
             width == 2U ? (sign ? "  ld.h $t0, $t1, 0\n" :
                                     "  ld.hu $t0, $t1, 0\n") :
             width == 4U ? "  ld.w $t0, $t1, 0\n" :
              width == 8U ? "  ld.d $t0, $t1, 0\n" : NULL;
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

/* Emission of target-neutral ELF STV_* binding metadata. */
static const char *elf_visibility_directive(MinicSymbolVisibility visibility) {
    switch (visibility) {
    case MINIC_SYMBOL_VISIBILITY_DEFAULT: return NULL;
    case MINIC_SYMBOL_VISIBILITY_HIDDEN: return ".hidden";
    case MINIC_SYMBOL_VISIBILITY_INTERNAL: return ".internal";
    case MINIC_SYMBOL_VISIBILITY_PROTECTED: return ".protected";
    default: return NULL;
    }
}
static bool emit_symbol_visibility(FILE *out, MinicSymbolVisibility visibility,
                                   const char *name) {
    const char *directive = elf_visibility_directive(visibility);
    return visibility == MINIC_SYMBOL_VISIBILITY_DEFAULT ||
           (directive != NULL && fprintf(out, "%s %s\n", directive, name) >= 0);
}

static bool emit_simple_global(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicGlobalObject *obj) {
    MinicType scalar;
    size_t storage_size, align, width, scalar_align, i;
    const char *directive;
    if (obj == NULL || obj->name == NULL || !obj->name[0] ||
        obj->is_weak ||
        (obj->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
         elf_visibility_directive(obj->visibility) == NULL) ||
        obj->section_name != NULL ||
        obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        obj->flexible_array_initializer_count != 0U ||
        !minic_data_layout_global_object(target->data_layout, program, obj,
                                         &storage_size, &align)) return false;
    /* .internal/.hidden must be emitted even for flat record globals. */
    if (!obj->is_internal &&
        !emit_symbol_visibility(out, obj->visibility, obj->name))
        return false;
    if (minic_type_is_record(obj->type)) {
        if (obj->union_selection_count != 0U) return false;
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
    /* zlib real-source pressure: z_errmsg[] is a pointer-relocation
     * array, configuration_table[] an array of records with call targets,
     * and inflate/trees expose large arrays of nested flat records.
     * Share proven DataLayout traversal; preserve ISA-owned ELF relocation. */
    if (minic_type_is_array(obj->type)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, obj->type.array_type_id);
        bool array_of_records;
        if (array == NULL) return false;
        array_of_records = minic_type_is_record(array->element_type);
        if (obj->union_selection_count != 0U && !array_of_records) return false;
        if (array_of_records || obj->relocation_count != 0U) {
            const char *section = obj->is_read_only
                ? (obj->relocation_count != 0U
                   ? ".section .data.rel.ro" : ".section .rodata") : ".data";
            if ((obj->initializer_count != 0U &&
                 obj->initializer_values == NULL) ||
                fprintf(out, "%s\n", section) < 0 ||
                (!obj->is_internal &&
                 fprintf(out, ".globl %s\n", obj->name) < 0) ||
                fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                        align, obj->name, obj->name) < 0 ||
                !(array_of_records
                    ? minic_multi_emit_flat_record_array(
                          out, program, target, obj, storage_size,
                          emit_record_relocation)
                    : minic_multi_emit_relocatable_scalar_array(
                          out, program, target, obj, storage_size,
                          emit_record_relocation)) ||
                fprintf(out, ".size %s,%zu\n", obj->name, storage_size) < 0)
                return false;
            return true;
        }
    }
    if (obj->union_selection_count != 0U ||
        obj->relocation_count != 0U) return false;
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
#define MINIC_LARGE_FRAME_MAX 262144U

static bool frame_objects(const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicCoreFunction *fn,
                          size_t sought_id, size_t *sought_offset,
                          size_t *object_end) {
    size_t pos, i;
    if (program == NULL || target == NULL || fn == NULL ||
        sought_id > fn->object_count ||
        fn->value_count > 32768U || fn->object_count > 4096U)
        return false;
    pos = fn->value_count * 8U;
    for (i = 0U; i < fn->object_count; ++i) {
        const MinicCoreObject *obj = &fn->objects[i];
        size_t unit, align, extent;
        if (!minic_data_layout_type(target->data_layout, program,
                                    obj->type, &unit, &align) ||
            obj->element_count == 0U || unit > MINIC_LARGE_FRAME_MAX / obj->element_count)
            return false;
        extent = unit * obj->element_count;
        if (obj->explicit_alignment > align)
            align = obj->explicit_alignment;
        if (align == 0U || align > 16U || (align & (align - 1U)) != 0U)
            return false;
        if (pos > MINIC_LARGE_FRAME_MAX - (align - 1U)) return false;
        pos = (pos + align - 1U) & ~(align - 1U);
        if (i == sought_id && sought_offset != NULL)
            *sought_offset = pos;
        if (extent > MINIC_LARGE_FRAME_MAX - pos) return false;
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
    if (offset > MINIC_LARGE_FRAME_MAX) return false;
    return fprintf(out, "  li.d $t3, %zu\n"
                        "  add.d $t3, $sp, $t3\n"
                        "  %s %s, $t3, 0\n", offset, op, reg) >= 0;
}
static bool emit_sp_adjust(FILE *out, size_t bytes, bool restore) {
    if (bytes == 0U) return true;
    if (bytes > MINIC_LARGE_FRAME_MAX) return false;
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
    const char *reject_stage = "function_preflight";
    size_t reject_block = SIZE_MAX, reject_ins = SIZE_MAX;
    size_t va_count = 0U, va_gp = 0U;
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
            "MINIC_CJSON_STAGE arch=loongarch64 function=%s stage=entry blocks=%zu values=%zu objects=%zu params=%zu calls=%zu\n",
            name, fn->block_count, fn->value_count, fn->object_count,
            fn->parameter_count, fn->callee_count);
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
        (decl->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
         elf_visibility_directive(decl->visibility) == NULL) ||
        decl->section_name != NULL || decl->alias_target != MINIC_FUNCTION_INVALID ||
        !valid_return_type(program, target, fn->return_type) ||
        fn->block_count == 0U || fn->block_count > 1024U || fn->entry_block != 0U ||
        fn->inline_asm_count != 0U || fn->parameter_count > 10U ||
        fn->value_count > 32768U || fn->object_count > 4096U)
        {
            if (getenv("MINIC_BACKEND_CENSUS") != NULL)
                (void)fprintf(stderr,
                              "MINIC_BACKEND_PREFLIGHT arch=loongarch64 function=%s "
                              "blocks=%zu values=%zu objects=%zu params=%zu "
                              "entry=%u inline=%zu return_valid=%d verified=%d "
                              "constructor=%d weak=%d visibility=%d section=%d\n",
                              name != NULL ? name : "<unnamed>",
                              fn->block_count, fn->value_count, fn->object_count,
                              fn->parameter_count, (unsigned int)fn->entry_block,
                              fn->inline_asm_count,
                              valid_return_type(program, target, fn->return_type) ? 1 : 0,
                              minic_core_function_verify(fn) ? 1 : 0,
                              decl->is_constructor ? 1 : 0, decl->is_weak ? 1 : 0,
                              (int)decl->visibility,
                              decl->section_name != NULL ? 1 : 0);
            return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                       reject_block, reject_ins, __LINE__);
        }
    /* LoongArch lp64d variadic gp cursor is a linear 8-byte register
     * save area followed immediately by overflow arguments. */
    if (decl->is_variadic) {
        for (i = 0U; i < fn->parameter_count; ++i) {
            if (minic_type_is_double(fn->parameter_types[i]))
                return machine_emit_reject(fn, "loongarch64", name,
                                           "variadic_fp_fixed", SIZE_MAX,
                                           SIZE_MAX, __LINE__);
            ++va_gp;
        }
    }
    for (i = 0U; i < fn->instruction_count; ++i)
        if (fn->instructions[i].kind ==
            MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS) ++va_count;
    if (va_count != 0U &&
        (!decl->is_variadic || va_count != 1U || va_gp > 8U))
        return machine_emit_reject(fn, "loongarch64", name,
                                   "variadic_preflight", SIZE_MAX,
                                   SIZE_MAX, __LINE__);
    for (i = 0U; i < fn->parameter_count; ++i)
        if (!valid_parameter_type(program, target, fn->parameter_types[i])) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=loongarch64 fn=%s stage=parameters_valid\n", name);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=parameters_ok\n");
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
            (void)fprintf(stderr, "MINIC_CJSON_CFG arch=loongarch64 block=%zu kind=%d has_term=%d condition=%u\n",
                          i, (int)term->kind, fn->blocks[i].has_terminator ? 1 : 0,
                          (unsigned int)term->conditional.condition);
        if (!fn->blocks[i].has_terminator) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        switch (term->kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (term->return_object != MINIC_CORE_OBJECT_INVALID ||
                (minic_type_is_void(fn->return_type)
                 ? term->return_value != MINIC_CORE_VALUE_INVALID
                 : term->return_value == MINIC_CORE_VALUE_INVALID ||
                   term->return_value >= fn->value_count ||
                   !minic_type_equal(fn->values[term->return_value].type,
                                     fn->return_type))) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Verified Core unreachable terminator is a semantic no-return
             * endpoint, not an implicit branch into the next basic block. */
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (term->branch_target >= fn->block_count) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
                (!valid_int_or_bool(fn->values[term->conditional.condition].type) &&
                 !register64_type(program, target,
                     fn->values[term->conditional.condition].type) &&
                 !scalar_integer_width(program, target,
                     fn->values[term->conditional.condition].type,
                     &condition_width, &condition_signed)))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        default:
            return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
    }
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=cfg_ok\n");
    /* Object byte extents and alignment are validated by frame_objects.
     * This admits real repeated local arrays without overlapping slots. */
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=loongarch64 fn=%s stage=terminators_valid\n", name);
    reject_stage = "frame_layout";
    reject_block = SIZE_MAX;
    if (!frame_objects(program, target, fn, fn->object_count, NULL,
                       &object_end)) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_REJECT arch=loongarch64 function=%s values=%zu objects=%zu\n",
                          name, fn->value_count, fn->object_count);
        return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    }
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=frame_objects_ok end=%zu\n", object_end);
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
        if ((minic_type_is_void(ins->type)
             ? ins->result != MINIC_CORE_VALUE_INVALID
             : ins->result == MINIC_CORE_VALUE_INVALID ||
               ins->result >= fn->value_count ||
               !minic_type_equal(fn->values[ins->result].type, ins->type)) ||
            !valid_return_type(program, target, ins->type))
            return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        if (ins->kind == MINIC_CORE_INSTRUCTION_CALL) {
            const MinicCoreCallee *callee;
            if (ins->value.call.callee_id >= fn->callee_count ||
                ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            callee = &fn->callees[ins->value.call.callee_id];
            if (callee->name == NULL || callee->name[0] == '\0')
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            sig = &fn->call_signatures[ins->value.indirect_call.signature_id];
            if (pointee.function_type_id != sig->function_type_id)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            begin = ins->value.indirect_call.argument_begin;
            count = ins->value.indirect_call.argument_count;
            return_type = sig->return_type;
            parameter_types = sig->parameter_types;
            parameter_count = sig->parameter_count;
            is_variadic = sig->is_variadic;
        }
        if (!minic_type_equal(return_type, ins->type) ||
            /* Fixed binary64 call results are transferred from the target FP
             * ABI result register; aggregate and other types still fail closed. */
            !valid_return_type(program, target, return_type) ||
            count > 8U || parameter_count > count ||
            (!is_variadic && parameter_count != count) ||
            begin > fn->call_argument_count ||
            count > fn->call_argument_count - begin ||
            (parameter_count != 0U && parameter_types == NULL))
            return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        for (j = 0U; j < count; ++j) {
            const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
            MinicType actual;
            size_t actual_width;
            bool actual_signed;
            if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                arg->value.value_id >= fn->value_count) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            actual = fn->values[arg->value.value_id].type;
            if (!valid_parameter_type(program, target, actual)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (j < parameter_count) {
                if (!valid_parameter_type(program, target, parameter_types[j]) ||
                    !minic_type_equal(parameter_types[j], actual))
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            } else {
                /* Lua source uses size_t/long-long variadic arguments.
                 * Preserve 32-bit promotions and classify full LP64
                 * integer values in the native general-purpose registers. */
                if (!is_variadic ||
                    (!minic_type_is_double(actual) &&
                     !minic_type_is_pointer(actual) &&
                     (!scalar_integer_width(program, target, actual,
                                            &actual_width, &actual_signed) ||
                      (actual_width != 4U && actual_width != 8U)))) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
        }
    }
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=loongarch64 fn=%s stage=calls_valid object_end=%zu\n",
                      name, object_end);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=calls_ok\n");
    reject_stage = "frame_finalization";
    reject_ins = SIZE_MAX;
    save_offset = (object_end + 7U) & ~(size_t)7U;
    if (save_offset > MINIC_LARGE_FRAME_MAX - (has_direct_calls ? 8U : 0U) - (va_count != 0U ? 64U : 0U))
        return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    frame = (save_offset + (has_direct_calls ? 8U : 0U) +
             (va_count != 0U ? 64U : 0U) + 15U) & ~(size_t)15U;
    if (frame > MINIC_LARGE_FRAME_MAX) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_CAP arch=loongarch64 function=%s frame=%zu end=%zu\n",
                          name, frame, object_end);
        return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    }
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=machine_emit frame=%zu\n", frame);
    if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",@progbits\n", name) < 0)
            return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    } else if (fprintf(out, ".text\n") < 0) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!decl->is_internal && fprintf(out, ".globl %s\n", name) < 0)
        return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!decl->is_internal &&
        !emit_symbol_visibility(out, decl->visibility, name))
        return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (fprintf(out, ".p2align 2\n.type %s,@function\n%s:\n", name, name) < 0)
        return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!emit_sp_adjust(out, frame, false)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (has_direct_calls &&
        !emit_sp_mem(out, "st.d", "$ra", save_offset)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (va_count != 0U) {
        static const char *const regs[8] = {
            "$a0", "$a1", "$a2", "$a3",
            "$a4", "$a5", "$a6", "$a7"};
        for (i = 0U; i < 8U; ++i)
            if (!emit_sp_mem(out, "st.d", regs[i],
                             frame - 64U + i * 8U))
                return machine_emit_reject(fn, "loongarch64", name,
                                           "variadic_save", SIZE_MAX,
                                           SIZE_MAX, __LINE__);
    }
    reject_stage = "machine_emit";
    for (size_t block_id = 0U; block_id < fn->block_count; ++block_id) {
        reject_block = block_id;
        reject_ins = SIZE_MAX;
        reject_stage = "machine_emit";
        block = &fn->blocks[block_id];
        if (fprintf(out, ".Lmini_loong_f%zu_b%zu:\n", id, block_id) < 0)
            return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        for (i = 0U; i < block->instruction_count; ++i) {
        MinicCoreInstructionId ins_id = block->instructions[i];
        const MinicCoreInstruction *ins;
        size_t offset;
        if (ins_id >= fn->instruction_count) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        ins = &fn->instructions[ins_id];
        reject_ins = (size_t)ins_id;
        if (trace_cjson)
            (void)fprintf(stderr,
                "MINIC_CJSON_STEP arch=loongarch64 function=%s block=%zu ins=%u kind=%d result=%u type_integer=%d type_pointer=%d\n",
                name, block_id, (unsigned int)ins_id, (int)ins->kind,
                (unsigned int)ins->result,
                minic_type_is_integer(ins->type) ? 1 : 0,
                minic_type_is_pointer(ins->type) ? 1 : 0);
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
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (!emit_sp_mem(out, "ld.d", "$t1", (size_t)dst * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t2", (size_t)src * 8U))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            for (size_t byte = 0U; byte < bytes; ++byte)
                if (fputs("  ld.bu $t0, $t2, 0\n"
                          "  st.b $t0, $t1, 0\n"
                          "  addi.d $t2, $t2, 1\n"
                          "  addi.d $t1, $t1, 1\n", out) == EOF)
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE) {
            const MinicCoreValueId addr = ins->value.store.address;
            const MinicCoreValueId value = ins->value.store.stored_value;
            size_t width = 0U;
            bool sign = false, pointer;
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                addr >= fn->value_count || value >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            pointer = minic_type_is_pointer(fn->values[value].type);
            if (!pointer && !minic_type_is_double(fn->values[value].type) &&
                !minic_type_is_float(fn->values[value].type) &&
                !scalar_integer_width(program, target, fn->values[value].type,
                                      &width, &sign)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (minic_type_is_double(fn->values[value].type)) width = 8U;
            if (minic_type_is_float(fn->values[value].type)) width = 4U;
            if (!emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_sp_mem(out, (pointer || width == 8U) ? "ld.d" : "ld.w", "$t0",
                             (size_t)value * 8U) ||
                (pointer ? fputs("  st.d $t0, $t1, 0\n", out) == EOF
                         : !emit_integer_memory(out, true, width, sign))) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        if ((ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
             ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) &&
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
                      !minic_type_is_pointer(ins->type) &&
                      !(minic_type_is_float(ins->type) &&
                        (ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE)) &&
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
                         ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL))))
            {
                if (getenv("MINIC_M1_TRACE") != NULL)
                    (void)fprintf(stderr,
                                  "MINIC_M8_RESULT_REJECT arch=loongarch64 function=%s block=%zu instruction=%u kind=%d result=%u type_integer=%d type_pointer=%d\n",
                                  name, block_id, (unsigned int)ins_id,
                                  (int)ins->kind, (unsigned int)ins->result,
                                  minic_type_is_integer(ins->type) ? 1 : 0,
                                  minic_type_is_pointer(ins->type) ? 1 : 0);
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            offset = (size_t)ins->result * 8U;
        }
        switch (ins->kind) {
        /* LoongArch FCC ordered comparisons naturally reject unordered
         * operands: fcmp.ceq/clt/cle is false if either operand is NaN. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
                ? "fcmp.ceq.d" :
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS
                    ? "fcmp.clt.d" : "fcmp.cle.d";
            if (!minic_type_equal(ins->type, minic_type_int()) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_double(fn->values[lhs].type) ||
                !minic_type_is_double(fn->values[rhs].type) ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "fld.d", "$ft1", (size_t)rhs * 8U) ||
                fprintf(out, "  %s $fcc0, $ft0, $ft1\n"
                             "  movcf2gr $t0, $fcc0\n", op) < 0)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        /* M37: source-observed cJSON binary64 subtract and multiply. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD
                ? "  fadd.d $ft0, $ft0, $ft1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT
                    ? "  fsub.d $ft0, $ft0, $ft1\n"
                    : "  fmul.d $ft0, $ft0, $ft1\n";
            if (!minic_type_is_double(ins->type) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_double(fn->values[lhs].type) ||
                !minic_type_is_double(fn->values[rhs].type) ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "fld.d", "$ft1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "fld.d", "$ft1", (size_t)rhs * 8U) ||
                fputs("  fdiv.d $ft0, $ft0, $ft1\n", out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            if (!minic_type_is_double(ins->type) ||
                operand >= fn->value_count ||
                !minic_type_is_double(fn->values[operand].type) ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)operand * 8U) ||
                fputs("  fneg.d $ft0, $ft0\n", out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
            if (!minic_type_is_double(ins->type) ||
                fprintf(out, "  li.d $t0, 0x%016" PRIx64 "\n",
                        ins->value.floating_bits) < 0) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT: {
            size_t width = 0U;
            bool sign = false;
            /* Match x86 Core constant semantics: the destination integer
             * type determines narrowing, not the original int64 payload.
             * Lua exercises out-of-range literals in cast/enum contexts. */
            (void)scalar_integer_width(program, target, ins->type,
                                       &width, &sign);
            if (width == 8U) {
                if (fprintf(out, "  li.d $t0, %" PRId64 "\n",
                            ins->value.integer_value) < 0) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            } else {
                if ((width == 0U &&
                     (ins->value.integer_value < INT32_MIN ||
                      ins->value.integer_value > INT32_MAX)) ||
                    fprintf(out, "  li.w $t0, %" PRId64 "\n",
                            (int64_t)(int32_t)(uint32_t)
                                ins->value.integer_value) < 0 ||
                    ((width == 1U || width == 2U) &&
                     !emit_integer_normalize(out, width, sign))) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            (void)sign;
            break;
        }
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
            if (ins->value.global_id >= fn->global_count ||
                fn->globals[ins->value.global_id].name == NULL ||
                fprintf(out, "  la.global $t0, %s\n",
                        fn->globals[ins->value.global_id].name) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
            const MinicCoreFunctionSymbolId symbol_id =
                ins->value.function_symbol_id;
            const char *symbol;
            if (symbol_id >= fn->function_symbol_count ||
                !minic_type_is_pointer(ins->type)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            symbol = fn->function_symbols[symbol_id].name;
            if (symbol == NULL || symbol[0] == '\0' ||
                fprintf(out, "  la.global $t0, %s\n", symbol) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
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
                !emit_sp_mem(out, "st.d", "$t0", offset)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }

        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
            /* The full Core DataLayout byte offset may exceed signed imm12.
             * li.d + add.d preserves the computed record layout. */
            const MinicCoreValueId base = ins->value.field_address.base;
            const size_t bytes = ins->value.field_address.byte_offset;
            if (base >= fn->value_count ||
                !minic_type_is_pointer(fn->values[base].type) ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)base * 8U))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (bytes != 0U) {
                if (bytes <= 2047U) {
                    if (fprintf(out, "  addi.d $t0, $t0, %zu\n", bytes) < 0)
                        return machine_emit_reject(fn, "loongarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                } else if (fprintf(out,
                            "  li.d $t1, %zu\n"
                            "  add.d $t0, $t0, $t1\n", bytes) < 0)
                    return machine_emit_reject(fn, "loongarch64", name,
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
            if (base >= fn->value_count || index >= fn->value_count ||
                !minic_type_is_pointer(fn->values[base].type) ||
                !scalar_integer_width(program, target, fn->values[index].type,
                                     &iw, &signed_index) ||
                stride > 2047U ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)base * 8U) ||
                !emit_sp_mem(out, iw == 1U
                                  ? (signed_index ? "ld.b" : "ld.bu")
                                  : iw == 2U
                                      ? (signed_index ? "ld.h" : "ld.hu")
                                      : iw == 8U ? "ld.d"
                                      : (signed_index ? "ld.w" : "ld.wu"),
                             "$t1", (size_t)index * 8U))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (stride != 0U) {
                if (fprintf(out, "  addi.d $t2, $zero, %zu\n"
                                 "  mul.d $t1, $t1, $t2\n"
                                 "  %s $t0, $t0, $t1\n", stride,
                            ins->value.pointer_offset.subtract ? "sub.d" : "add.d") < 0)
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
            /* M35: Core pointer->LP64 integer cast keeps the exact address
             * bit pattern (needed for ptrdiff_t in real cJSON parsing). */
            if (source < fn->value_count &&
                minic_type_is_pointer(fn->values[source].type) &&
                minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!minic_core_scalar_bitcast_types_valid(
                        ins->type, fn->values[source].type) ||
                    !scalar_integer_width(program, target, ins->type,
                                          &width, &sign) || width != 8U ||
                    !emit_sp_mem(out, "ld.d", "$t0", (size_t)source * 8U))
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (source >= fn->value_count ||
                !minic_type_is_pointer(ins->type) ||
                !minic_core_scalar_bitcast_types_valid(
                    ins->type, fn->values[source].type)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            src_pointer = minic_type_is_pointer(fn->values[source].type);
            if (!src_pointer &&
                !scalar_integer_width(program, target,
                                      fn->values[source].type,
                                     &src_width, &src_signed)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (!emit_sp_mem(out, (src_pointer || src_width == 8U)
                                 ? "ld.d" : "ld.w", "$t0",
                             (size_t)source * 8U)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (!src_pointer && src_width != 8U &&
                (!emit_integer_normalize(out, src_width, src_signed) ||
                 (src_signed
                    ? fputs("  slli.w $t0, $t0, 0\n", out) == EOF
                    : fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF)))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_LOAD: {
            const MinicCoreValueId addr = ins->value.load.address;
            size_t width = minic_type_is_double(ins->type) ? 8U :
                           minic_type_is_float(ins->type) ? 4U : 0U;
            bool sign = false, pointer = minic_type_is_pointer(ins->type);
            if (addr >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                (!pointer && !minic_type_is_double(ins->type) &&
                 !minic_type_is_float(ins->type) &&
                 !scalar_integer_width(program, target,
                                       ins->type, &width, &sign)) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                (pointer ? fputs("  ld.d $t0, $t1, 0\n", out) == EOF
                         : !emit_integer_memory(out, false, width, sign)))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
            static const char *const gp_regs[8] = {
                "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"};
            static const char *const fp_regs[8] = {
                "$fa0", "$fa1", "$fa2", "$fa3", "$fa4", "$fa5", "$fa6", "$fa7"};
            const size_t parameter_count = indirect
                ? fn->call_signatures[ins->value.indirect_call.signature_id].parameter_count
                : fn->callees[ins->value.call.callee_id].parameter_count;
            const bool is_variadic = indirect
                ? fn->call_signatures[ins->value.indirect_call.signature_id].is_variadic
                : fn->callees[ins->value.call.callee_id].is_variadic;
            size_t j, gp_index = 0U, fp_index = 0U;
            /* M38 lp64d: fixed binary64 uses FP registers, but
             * unnamed variadic doubles consume GP argument registers as
             * 64-bit raw payloads, per the LoongArch ELF psABI. */
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                const MinicType type = fn->values[arg->value.value_id].type;
                const size_t slot = (size_t)arg->value.value_id * 8U;
                if (minic_type_is_double(type) &&
                    is_variadic && j >= parameter_count) {
                    if (gp_index >= 8U ||
                        !emit_sp_mem(out, "ld.d", gp_regs[gp_index++], slot))
                        return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                } else if (minic_type_is_double(type)) {
                    if (fp_index >= 8U ||
                        !emit_sp_mem(out, "fld.d", fp_regs[fp_index++], slot))
                        return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                } else {
                    if (gp_index >= 8U ||
                        !emit_sp_mem(out, register64_type(program, target, type)
                                            ? "ld.d" : "ld.w",
                                     gp_regs[gp_index++], slot))
                        return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                }
            }
            if (indirect) {
                /* $t2 is caller-clobbered, never an argument or SP scratch.
                 * JIRL writes $ra; frame epilogue restores its original value. */
                if (!emit_sp_mem(out, "ld.d", "$t2",
                                 (size_t)ins->value.indirect_call.callee * 8U) ||
                    fputs("  jirl $ra, $t2, 0\n", out) == EOF)
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            } else {
                const MinicCoreCallee *callee =
                    &fn->callees[ins->value.call.callee_id];
                if (fprintf(out, "  bl %s\n", callee->name) < 0)
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            if (minic_type_is_void(ins->type)) continue;
            /* LoongArch lp64d returns IEEE binary64 in fa0, not a0;
             * movfr2gr.d preserves every bit of the FP result. */
            if (fputs(minic_type_is_double(ins->type)
                          ? "  movfr2gr.d $t0, $fa0\n"
                          : "  or $t0, $a0, $zero\n", out) == EOF)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!scalar_integer_width(program, target, ins->type,
                                          &width, &sign) ||
                    (width != 8U && !emit_integer_normalize(out, width, sign)))
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            break;
        }
        case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
            /* LoongArch LP64D va_list is a scalar byte cursor.  The
             * register bank saved at the top of this frame runs directly
             * into the caller's overflow stack slots at entry SP. */
            if (va_count != 1U || !minic_type_is_pointer(ins->type) ||
                ins->result >= fn->value_count ||
                fprintf(out, "  li.d $t0, %zu\n",
                        frame - 64U + va_gp * 8U) < 0 ||
                fputs("  add.d $t0, $sp, $t0\n", out) == EOF)
                return machine_emit_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_PARAMETER: {
            const size_t ordinal = (size_t)ins->value.parameter_index;
            static const char *const gp_regs[8] = {
                "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"};
            static const char *const fp_regs[8] = {
                "$fa0", "$fa1", "$fa2", "$fa3", "$fa4", "$fa5", "$fa6", "$fa7"};
            size_t gp_index = 0U, fp_index = 0U, k;
            if (ordinal >= fn->parameter_count) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            /* LoongArch lp64d independently allocates 8 GP and FP regs. */
            for (k = 0U; k < ordinal; ++k) {
                if (minic_type_is_double(fn->parameter_types[k]))
                    ++fp_index;
                else
                    ++gp_index;
            }
            if (minic_type_is_double(ins->type)) {
                if (fp_index >= 8U ||
                    !emit_sp_mem(out, "fst.d", fp_regs[fp_index], offset))
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                continue;
            }
            if (gp_index < 8U) {
                if (!emit_sp_mem(out, register64_type(program, target, ins->type)
                                        ? "st.d" : "st.w",
                                 gp_regs[gp_index], offset))
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            } else {
                const size_t incoming = frame + (gp_index - 8U) * 8U;
                if (!emit_sp_mem(out, register64_type(program, target, ins->type)
                                        ? "ld.d" : "ld.w", "$t0", incoming) ||
                    !emit_sp_mem(out, register64_type(program, target, ins->type)
                                        ? "st.d" : "st.w", "$t0", offset))
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            continue;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *op;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_equal(fn->values[lhs].type, ins->type) ||
                !scalar_integer_width(program, target, fn->values[lhs].type,
                                      &lw, &ls) ||
                !scalar_integer_width(program, target, fn->values[rhs].type,
                                      &rw, &rs) ||
                (lw != 4U && lw != 8U) ||
                /* C permits 8/16-bit shift-count values after promotion;
                 * the target consumes their low 32-bit SSA slot. */
                minic_type_is_bool_integer(ins->type))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                    ? (wide ? "  sll.d $t0, $t0, $t1\n"
                            : "  sll.w $t0, $t0, $t1\n")
                    : (ls ? (wide ? "  sra.d $t0, $t0, $t1\n"
                                  : "  sra.w $t0, $t0, $t1\n")
                          : (wide ? "  srl.d $t0, $t0, $t1\n"
                                  : "  srl.w $t0, $t0, $t1\n"));
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF ||
                (!wide && !emit_integer_normalize(out, lw, ls)))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            (void)rs;
            break;
        }
        /* Original cJSON uses ~ on signed/unsigned scalar values.
         * NOR against zero is the LoongArch native complement; normalize
         * subword results before storing the Core SSA slot. */
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (operand >= fn->value_count ||
                !minic_type_equal(fn->values[operand].type, ins->type) ||
                !scalar_integer_width(program, target, ins->type, &width, &sign) ||
                minic_type_is_bool_integer(ins->type) ||
                !emit_sp_mem(out, width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)operand * 8U) ||
                fputs("  nor $t0, $t0, $zero\n", out) == EOF ||
                (width != 8U && !emit_integer_normalize(out, width, sign)))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (operand >= fn->value_count ||
                !minic_type_equal(fn->values[operand].type, ins->type) ||
                !scalar_integer_width(program, target, ins->type, &width, &sign) ||
                minic_type_is_bool_integer(ins->type) ||
                !emit_sp_mem(out, width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)operand * 8U) ||
                fputs(width == 8U ? "  sub.d $t0, $zero, $t0\n"
                                  : "  sub.w $t0, $zero, $t0\n", out) == EOF ||
                (width != 8U && !emit_integer_normalize(out, width, sign)))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
                !scalar_integer_width(program, target, fn->values[lhs].type, &lw, &ls) ||
                !scalar_integer_width(program, target, fn->values[rhs].type, &rw, &rs) ||
                lw != rw || ls != rs ||
                !minic_type_equal(fn->values[lhs].type, ins->type) ||
                minic_type_is_bool_integer(ins->type))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            /* M36: real LP64 multiplication uses the full 64-bit operands. */
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD
                ? (wide ? "  add.d $t0, $t0, $t1\n" : "  add.w $t0, $t0, $t1\n")
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT
                    ? (wide ? "  sub.d $t0, $t0, $t1\n" : "  sub.w $t0, $t0, $t1\n")
                    : (wide ? "  mul.d $t0, $t0, $t1\n" : "  mul.w $t0, $t0, $t1\n");
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t1",
                             (size_t)rhs * 8U) ||
                fputs(opcode, out) == EOF ||
                (!wide && !emit_integer_normalize(out, lw, ls)))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
            const char *opcode;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !scalar_integer_width(program, target, fn->values[lhs].type,
                                      &lw, &ls) ||
                !scalar_integer_width(program, target, fn->values[rhs].type,
                                      &rw, &rs) ||
                lw != rw || (lw != 4U && lw != 8U) || ls != rs)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
                ? "  and $t0, $t0, $t1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
                    ? "  xor $t0, $t0, $t1\n"
                    : "  or $t0, $t0, $t1\n";
            if (!emit_sp_mem(out, lw == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                fputs(opcode, out) == EOF)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
                !scalar_integer_width(program, target, fn->values[lhs].type,
                                      &lw, &ls) ||
                !scalar_integer_width(program, target, fn->values[rhs].type,
                                      &rw, &rs) ||
                lw != rw || (lw != 4U && lw != 8U) || ls != rs ||
                !minic_type_equal(fn->values[lhs].type, ins->type))
                return machine_emit_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            wide = lw == 8U;
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE
                ? (wide ? (ls ? "  div.d $t0, $t0, $t1\n"
                               : "  div.du $t0, $t0, $t1\n")
                        : (ls ? "  div.w $t0, $t0, $t1\n"
                               : "  div.wu $t0, $t0, $t1\n"))
                : (wide ? (ls ? "  mod.d $t0, $t0, $t1\n"
                               : "  mod.du $t0, $t0, $t1\n")
                        : (ls ? "  mod.w $t0, $t0, $t1\n"
                               : "  mod.wu $t0, $t0, $t1\n"));
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF)
                return machine_emit_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        }
        /* M33: unsigned 64-bit pointer order for cJSON overlap checks.
         * Operand pointer equivalence is also enforced in Core IR. */
        case MINIC_CORE_INSTRUCTION_POINTER_LESS: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_type_equal(ins->type, minic_type_int()) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_pointer(fn->values[lhs].type) ||
                !minic_type_equal(fn->values[lhs].type, fn->values[rhs].type) ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)rhs * 8U) ||
                fputs("  sltu $t0, $t0, $t1\n", out) == EOF)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
                if (!emit_sp_mem(out, "ld.d", "$t0", (size_t)lhs * 8U) ||
                    !emit_sp_mem(out, "ld.d", "$t1", (size_t)rhs * 8U) ||
                    fputs("  xor $t0, $t0, $t1\n"
                          "  sltui $t0, $t0, 1\n", out) == EOF)
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !scalar_integer_width(program, target, fn->values[lhs].type, &lw, &ls) ||
                !scalar_integer_width(program, target, fn->values[rhs].type, &rw, &rs) ||
                lw != rw || ls != rs || (lw != 4U && lw != 8U))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t1",
                             (size_t)rhs * 8U) ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS
                          ? (ls ? "  slt $t0, $t0, $t1\n"
                                : "  sltu $t0, $t0, $t1\n")
                          : "  xor $t0, $t0, $t1\n  sltui $t0, $t0, 1\n", out) == EOF)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO: {
            const MinicCoreValueId v = ins->value.operand;
            size_t width = 0U;
            bool sign = false;
            bool pointer, wide;
            if (v >= fn->value_count) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            pointer = minic_type_is_pointer(fn->values[v].type);
            if (!pointer && !minic_type_equal(fn->values[v].type, minic_type_bool()) &&
                !scalar_integer_width(program, target, fn->values[v].type,
                                      &width, &sign))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = pointer || width == 8U;
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)v * 8U) ||
                fputs("  sltui $t0, $t0, 1\n", out) == EOF)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            (void)sign;
            break;
        }
        /* M28: IEEE integer/double conversions used by cJSON's saturated
         * valueint assignment. Only signed 32-bit conversion is admitted. */
        /* M31: numeric IEEE binary32->binary64 promotion for float arrays.
         * Reinterpretation/bitcast is incorrect: perform fcvt.d.s. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
            const MinicCoreValueId source = ins->value.operand;
            if (!minic_type_is_float(ins->type) ||
                source >= fn->value_count ||
                !minic_type_is_double(fn->values[source].type) ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)source * 8U) ||
                fputs("  fcvt.s.d $ft0, $ft0\n", out) == EOF ||
                !emit_sp_mem(out, "fst.s", "$ft0", offset))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
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
                ? "  fadd.s $ft0, $ft0, $ft1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                    ? "  fsub.s $ft0, $ft0, $ft1\n"
                    : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                        ? "  fmul.s $ft0, $ft0, $ft1\n"
                        : "  fdiv.s $ft0, $ft0, $ft1\n";
            if (!minic_type_is_float(ins->type) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_float(fn->values[lhs].type) ||
                !minic_type_is_float(fn->values[rhs].type) ||
                !emit_sp_mem(out, "fld.s", "$ft0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "fld.s", "$ft1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF ||
                !emit_sp_mem(out, "fst.s", "$ft0", offset))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            if (src >= fn->value_count || !minic_type_is_double(ins->type) ||
                !minic_type_is_float(fn->values[src].type) ||
                !emit_sp_mem(out, "fld.s", "$ft0", (size_t)src * 8U) ||
                fputs("  fcvt.d.s $ft0, $ft0\n", out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            if (src >= fn->value_count || !minic_type_is_double(ins->type) ||
                !scalar_integer_width(program, target, fn->values[src].type,
                                      &width, &sign) ||
                !emit_sp_mem(out, width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)src * 8U) ||
                (width < 4U && !emit_integer_normalize(out, width, sign)))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (width == 8U && !sign) {
                /* LoongArch base FP has signed ffint.d.l, but not an
                 * unsigned 64-bit scalar conversion. Split at 32 bits:
                 * both pieces are exact in FP64, and fadd rounds ONCE.
                 * This avoids the double-rounding trap of (x>>1)*2. */
                if (fputs("  srli.d $t1, $t0, 32\n"
                          "  bstrpick.d $t0, $t0, 31, 0\n"
                          "  movgr2fr.d $ft0, $t1\n"
                          "  ffint.d.l $ft0, $ft0\n"
                          "  movgr2fr.d $ft1, $t0\n"
                          "  ffint.d.l $ft1, $ft1\n"
                          "  li.d $t2, 0x41f0000000000000\n"
                          "  movgr2fr.d $ft2, $t2\n"
                          "  fmul.d $ft0, $ft0, $ft2\n"
                          "  fadd.d $ft0, $ft0, $ft1\n", out) == EOF)
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                        reject_block, reject_ins, __LINE__);
            } else if (width == 4U && !sign) {
                /* ld.w sign-extends on LP64; zero-extend unsigned int
                 * before signed-long-to-double conversion. */
                if (fputs("  bstrpick.d $t0, $t0, 31, 0\n"
                          "  movgr2fr.d $ft0, $t0\n"
                          "  ffint.d.l $ft0, $ft0\n", out) == EOF)
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                        reject_block, reject_ins, __LINE__);
            } else {
                if (fputs(width == 8U
                              ? "  movgr2fr.d $ft0, $t0\n"
                                "  ffint.d.l $ft0, $ft0\n"
                              : "  movgr2fr.w $ft0, $t0\n"
                                "  ffint.d.w $ft0, $ft0\n", out) == EOF)
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                        reject_block, reject_ins, __LINE__);
            }
            if (!emit_sp_mem(out, "fst.d", "$ft0", offset))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            if (src >= fn->value_count ||
                !minic_type_is_double(fn->values[src].type) ||
                !scalar_integer_width(program, target, ins->type, &width, &sign) ||
                width != 4U || !sign ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)src * 8U) ||
                fputs("  ftintrz.w.d $ft0, $ft0\n"
                      "  movfr2gr.s $t0, $ft0\n", out) == EOF)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break; /* Normal 32-bit SSA store. */
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
            const MinicCoreValueId src = ins->value.operand;
            size_t src_width, dst_width;
            bool src_sign, dst_sign;
            if (src >= fn->value_count ||
                !scalar_integer_width(program, target,
                                      fn->values[src].type, &src_width, &src_sign) ||
                !scalar_integer_width(program, target, ins->type,
                                      &dst_width, &dst_sign) ||
                !emit_sp_mem(out, src_width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)src * 8U))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (dst_width == 8U) {
                if (src_width != 8U &&
                    (!emit_integer_normalize(out, src_width, src_sign) ||
                     (!src_sign &&
                      fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF)))
                    return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                /* LoongArch ld.w sign-extends: unsigned32 needs masking. */
            } else if (!emit_integer_normalize(out, dst_width, dst_sign))
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        default:
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                              "MINIC_M8_OPCODE_REJECT arch=loongarch64 function=%s block=%zu instruction=%u kind=%d\n",
                              name, block_id, (unsigned int)ins_id, (int)ins->kind);
            return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
        if (!emit_sp_mem(out, (register64_type(program, target, ins->type) ||
                               minic_type_is_double(ins->type))
                              ? "st.d" : "st.w", "$t0", offset)) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    }
        reject_stage = "terminator_emit";
        reject_ins = SIZE_MAX;
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if ((!minic_type_is_void(fn->return_type) &&
                 !(minic_type_is_double(fn->return_type)
                       ? emit_sp_mem(out, "fld.d", "$fa0",
                                     (size_t)block->terminator.return_value * 8U)
                       : emit_sp_mem(out, register64_type(program, target, fn->return_type)
                                          ? "ld.d" : "ld.w", "$a0",
                                     (size_t)block->terminator.return_value * 8U))) ||
                (has_direct_calls && !emit_sp_mem(out, "ld.d", "$ra", save_offset)) ||
                (!emit_sp_adjust(out, frame, true)) ||
                fputs("  ret\n", out) == EOF) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Preserve Core CFG semantics: trap instead of falling through. */
            if (fputs("  break 0\n", out) == EOF) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
            const MinicCoreValueId cond = block->terminator.conditional.condition;
            const bool wide = register64_type(program, target, fn->values[cond].type);
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)cond * 8U) ||
                fprintf(out,
                        "  bnez $t0, .Lmini_loong_f%zu_b%u\n"
                        "  b .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.conditional.when_true,
                        id, (unsigned int)block->terminator.conditional.when_false) < 0)
                return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        default: return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
    }
    reject_stage = "function_finish";
    reject_block = SIZE_MAX;
    if (fprintf(out, ".size %s,.-%s\n", name, name) < 0) return machine_emit_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
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
    const bool census = getenv("MINIC_BACKEND_CENSUS") != NULL;
    size_t scanned_functions = 0U, rejected_functions = 0U;
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
    /* Linux-style full-TU census, opt-in only: identify the first reject
     * in EVERY function in one compiler invocation. The partial .s is
     * ALWAYS discarded if a function fails; normal builds fail fast. */
    for (i = 0U; i < program->function_count && (success || census); ++i) {
        const MinicFunction *decl = &program->functions[i];
        if (decl->is_defined && (!decl->is_internal || decl->is_referenced)) {
            ++scanned_functions;
            if (!emit_function(out, program, target, decl, &core_functions[i],
                               i, function_sections)) {
                const MinicCoreFunction *fn = &core_functions[i];
                ++rejected_functions;
                success = false;
                if (getenv("MINIC_M1_TRACE") != NULL) {
                    (void)fprintf(stderr,
                                  "MINIC_M1_FRONTIER arch=loongarch64 function=%s "
                                  "core_blocks=%zu core_objects=%zu parameters=%zu "
                                  "values=%zu instructions=%zu\n",
                                  fn->name != NULL ? fn->name : "<unnamed>",
                                  fn->block_count, fn->object_count,
                                  fn->parameter_count, fn->value_count,
                                  fn->instruction_count);
                    if (!census) (void)minic_core_function_dump(stderr, fn);
                }
            }
        }
    }
    if (census)
        minic_backend_core_usage_dump("loongarch64", program, target,
                                      core_functions, core_function_count);
    if (census)
        (void)fprintf(stderr,
                      "MINIC_BACKEND_CENSUS arch=loongarch64 scanned=%zu rejected=%zu\n",
                      scanned_functions, rejected_functions);
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
