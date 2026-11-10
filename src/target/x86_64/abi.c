#include "target/common/aggregate_shape.h"
#include "target/common/lp64_scalar.h"
#include "target/x86_64/abi.h"
#include <stdint.h>

/* The SysV INTEGER parameter and return banks have different orderings;
 * Win64 uses four positional argument registers and one scalar GP return.
 * Keep this physical mapping independent of all Core instruction families. */
static const char *const x86_sysv_gp_arguments[] = {
    "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
};
static const char *const x86_win64_gp_arguments[] = {
    "%rcx", "%rdx", "%r8", "%r9"
};
static const char *const x86_sysv_gp_results[] = {"%rax", "%rdx"};
static const char *const x86_win64_gp_results[] = {"%rax"};
static const char *const x86_win64_extra_nonvolatile_gp[] = {"%rdi", "%rsi"};

const MinicX86AbiRegisters *minic_x86_64_abi_registers(
    MinicFunctionCallingConvention convention) {
    static const MinicX86AbiRegisters sysv = {
        .gp_arguments = x86_sysv_gp_arguments,
        .gp_argument_count = 6U,
        .fp_argument_count = 8U,
        .gp_results = x86_sysv_gp_results,
        .gp_result_count = 2U,
    };
    static const MinicX86AbiRegisters win64 = {
        .gp_arguments = x86_win64_gp_arguments,
        .gp_argument_count = 4U,
        .fp_argument_count = 4U,
        .gp_results = x86_win64_gp_results,
        .gp_result_count = 1U,
        .extra_nonvolatile_gp = x86_win64_extra_nonvolatile_gp,
        .extra_nonvolatile_gp_count = 2U,
        .extra_nonvolatile_xmm_count = 10U,
    };
    switch (convention) {
    case MINIC_CALLING_CONVENTION_DEFAULT: return &sysv;
    case MINIC_CALLING_CONVENTION_MS_X64: return &win64;
    default: return NULL;
    }
}

/* SysV AMD64 Core classifiers use only verified CoreType layout, preserving
 * the existing one-/two-eightbyte decisions, hidden-result rules and ABI
 * identity checks. C rank/qualifier rules belong to source qualification. */
static const MinicCoreType *x86_core_type(const MinicCoreTypeContext *context,
                                         MinicCoreTypeId id) {
    return minic_core_type_get(context, minic_core_type_unqualified(context, id));
}

bool minic_x86_64_core_scalar_width(const MinicCoreTypeContext *context,
                                    MinicCoreTypeId id, size_t *bytes,
                                    bool *is_signed) {
    const MinicCoreType *type = x86_core_type(context, id);
    if (bytes == NULL || is_signed == NULL || type == NULL ||
        !type->has_layout) return false;
    if (type->kind == MINIC_CORE_TYPE_POINTER) {
        if (type->size != 8U) return false;
        *bytes = 8U;
        *is_signed = false;
        return true;
    }
    return minic_lp64_core_integer_width(context, id, bytes, is_signed);
}

bool minic_x86_64_core_storage_width(const MinicCoreTypeContext *context,
                                     MinicCoreTypeId id, size_t *bytes,
                                     bool *is_signed) {
    const MinicCoreType *type = x86_core_type(context, id);
    if (type != NULL && type->kind == MINIC_CORE_TYPE_FLOAT && type->has_layout &&
        (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
         type->value.floating.format == MINIC_CORE_FLOAT_BINARY64)) {
        const size_t width = type->value.floating.format == MINIC_CORE_FLOAT_BINARY32
                                 ? 4U : 8U;
        if (bytes == NULL || is_signed == NULL || type->size != width) return false;
        *bytes = width;
        *is_signed = false;
        return true;
    }
    return minic_x86_64_core_scalar_width(context, id, bytes, is_signed);
}

static bool x86_core_integer_record_tree(const MinicCoreTypeContext *context,
                                         MinicCoreTypeId id, size_t absolute,
                                         size_t max_bytes, unsigned int depth) {
    const MinicCoreType *type = x86_core_type(context, id);
    size_t index;
    if (depth > 12U || type == NULL || !type->has_layout ||
        type->alignment == 0U || type->size > max_bytes ||
        absolute > max_bytes - type->size) return false;
    if (type->kind == MINIC_CORE_TYPE_POINTER ||
        type->kind == MINIC_CORE_TYPE_INTEGER)
        return max_bytes == 8U ||
            (type->size != 0U && type->alignment <= 8U &&
             absolute % type->alignment == 0U);
    if (type->kind == MINIC_CORE_TYPE_ARRAY) {
        const MinicCoreType *element = x86_core_type(context, type->value.array.element);
        if (!type->value.array.is_complete ||
            type->value.array.count == 0U || element == NULL ||
            !element->has_layout || element->size == 0U ||
            type->value.array.count > SIZE_MAX / element->size ||
            type->value.array.count * element->size != type->size) return false;
        for (index = 0U; index < type->value.array.count; ++index)
            if (!x86_core_integer_record_tree(context, type->value.array.element,
                                               absolute + index * element->size,
                                               max_bytes, depth + 1U)) return false;
        return true;
    }
    if (type->kind != MINIC_CORE_TYPE_RECORD ||
        !type->value.record.is_complete || type->alignment > 8U) return false;
    /* A zero-sized GNU record is SysV NO_CLASS, as in the C classifier. */
    if (type->size == 0U) return true;
    if (type->value.record.field_count != 0U &&
        type->value.record.fields == NULL) return false;
    for (index = 0U; index < type->value.record.field_count; ++index) {
        const MinicCoreRecordField *field = &type->value.record.fields[index];
        const MinicCoreType *member = x86_core_type(context, field->type_id);
        const size_t offset = field->byte_offset;
        if (member == NULL || !member->has_layout ||
            member->alignment == 0U || offset > type->size ||
            offset > max_bytes - absolute) return false;
        if (field->is_bit_field) {
            size_t begin_bits, end_bits;
            if (member->kind != MINIC_CORE_TYPE_INTEGER ||
                field->bit_offset >= 8U || field->bit_width > 64U ||
                (max_bytes == 16U && offset >= type->size) ||
                field->bit_offset > (type->size - offset) * 8U ||
                field->bit_width > (type->size - offset) * 8U - field->bit_offset)
                return false;
            begin_bits = (absolute + offset) * 8U + field->bit_offset;
            end_bits = begin_bits + field->bit_width;
            if (end_bits > max_bytes * 8U ||
                (max_bytes == 16U && field->bit_width != 0U &&
                 begin_bits / 64U != (end_bits - 1U) / 64U)) return false;
            continue;
        }
        if ((max_bytes == 8U && offset % member->alignment != 0U) ||
            member->size > type->size - offset) return false;
        if (member->size == 0U) {
            if (member->kind != MINIC_CORE_TYPE_RECORD) return false;
            continue;
        }
        if (!x86_core_integer_record_tree(context, field->type_id,
                                          absolute + offset, max_bytes,
                                          depth + 1U)) return false;
    }
    return true;
}

bool minic_x86_64_core_one_gp_record(const MinicCoreTypeContext *context,
                                     MinicCoreTypeId id, size_t *record_bytes) {
    MinicAggregateStorageShape shape;
    const MinicCoreType *type = x86_core_type(context, id);
    if (type == NULL || type->kind != MINIC_CORE_TYPE_RECORD ||
        record_bytes == NULL ||
        !minic_common_aggregate_storage_shape_core(context, id, 8U, &shape) ||
        shape.bytes == 0U || shape.bytes > 8U || shape.alignment > 8U ||
        !x86_core_integer_record_tree(context, id, 0U, 8U, 0U)) return false;
    *record_bytes = shape.bytes;
    return true;
}

bool minic_x86_64_core_two_gp_record(const MinicCoreTypeContext *context,
                                     MinicCoreTypeId id) {
    MinicAggregateStorageShape shape;
    const MinicCoreType *type = x86_core_type(context, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_RECORD &&
        minic_common_aggregate_storage_shape_core(context, id, 8U, &shape) &&
        shape.bytes > 8U && shape.bytes <= 16U && shape.alignment <= 8U &&
        x86_core_integer_record_tree(context, id, 0U, 16U, 0U);
}

bool minic_x86_64_core_memory_result_record(const MinicCoreTypeContext *context,
                                             MinicCoreTypeId id, size_t *bytes) {
    const MinicCoreType *record = x86_core_type(context, id);
    const MinicCoreType *layout = minic_core_type_get(context, id);
    if (bytes == NULL || record == NULL || layout == NULL ||
        record->kind != MINIC_CORE_TYPE_RECORD ||
        !layout->has_layout || !record->value.record.is_complete) return false;
    *bytes = layout->size;
    return layout->size > 16U && layout->size <= 4096U &&
        layout->alignment != 0U && layout->alignment <= 16U;
}

bool minic_x86_64_core_record_abi_compatible(const MinicCoreTypeContext *context,
                                             MinicCoreTypeId actual,
                                             MinicCoreTypeId declared) {
    const MinicCoreType *a = minic_core_type_get(context, actual);
    const MinicCoreType *b = minic_core_type_get(context, declared);
    const MinicCoreType *record = x86_core_type(context, actual);
    /* Top-level CV is irrelevant to by-value parameter compatibility,
     * but an explicit ABI alignment alias is not. C unqualified() also
     * retains explicit_alignment, so preserve that stricter old gate. */
    return a != NULL && b != NULL && record != NULL &&
        record->kind == MINIC_CORE_TYPE_RECORD &&
        record->value.record.is_complete && a->has_layout && b->has_layout &&
        minic_core_type_unqualified(context, actual) ==
            minic_core_type_unqualified(context, declared) &&
        a->has_abi_alignment_override == b->has_abi_alignment_override &&
        a->size == b->size && a->alignment == b->alignment;
}

