#include "target/common/aggregate_shape.h"
#include "target/common/lp64_scalar.h"
#include "target/x86_64/abi.h"
#include "target/data_layout.h"
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
        .extra_nonvolatile_xmm_first = 6U,
        .extra_nonvolatile_xmm_count = 10U,
    };
    switch (convention) {
    case MINIC_CALLING_CONVENTION_DEFAULT: return &sysv;
    case MINIC_CALLING_CONVENTION_MS_X64: return &win64;
    default: return NULL;
    }
}

/* Extracted SysV AMD64 classifier: integer widths, conservative aggregates
 * and register-bank/stack ingress. The existing rules stay byte-for-byte;
 * target-specific machine emission remains in machine_emit.c and machine_call.c. */

bool minic_x86_64_scalar_width(const MinicC0Program *program,
                             const MinicTargetInfo *target,
                             MinicType type, size_t *bytes, bool *is_signed) {
    if (bytes == NULL || is_signed == NULL || target == NULL ||
        target->data_layout == NULL) return false;
    if (minic_type_is_pointer(type)) {
        *bytes = target->data_layout->pointer_size;
        *is_signed = false;
        return *bytes == 8U;
    }
    return minic_lp64_scalar_integer_width(program, target, type,
                                            bytes, is_signed);
}

/* Values are stored as raw machine bits in eight-byte Core slots.  Floating
 * load/store must not pass through the integer sign-extension path. */
bool minic_x86_64_storage_width(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type,
                              size_t *bytes, bool *sign) {
    if (minic_type_is_float(type) || minic_type_is_double(type)) {
        if (bytes == NULL || sign == NULL) return false;
        *bytes = minic_type_is_float(type) ? 4U : 8U;
        *sign = false;
        return true;
    }
    return minic_x86_64_scalar_width(program, target, type, bytes, sign);
}

/* One SysV INTEGER-tree walk supplies both the one- and two-eightbyte
 * classifiers. The only policy difference is the admissible top-level
 * storage extent and whether a field may cross the eightbyte boundary.
 * The ABI's recursive, naturally aligned scalar-leaf rule is not a machine
 * opcode rule; machine_emit.c owns the actual register/byte movement. */
static bool x86_integer_record_tree(const MinicC0Program *program,
    const MinicTargetInfo *target, MinicType type, size_t absolute,
    size_t max_bytes, unsigned int depth) {
    const MinicRecord *record;
    MinicType plain;
    size_t bytes, alignment, index;
    if (depth > 12U || !minic_type_unqualified(type, &plain) ||
        !minic_data_layout_type(target->data_layout, program, plain,
                                &bytes, &alignment) ||
        !alignment || bytes > max_bytes || absolute > max_bytes - bytes)
        return false;
    if (minic_type_is_pointer(plain) || minic_type_is_integer(plain)) {
        /* The original one-eightbyte walk checked this alignment at the
         * parent field; the two-eightbyte walk checked every scalar leaf. */
        return max_bytes == 8U ||
            (bytes != 0U && alignment <= 8U && absolute % alignment == 0U);
    }
    if (!minic_type_is_record(plain) || alignment > 8U)
        return false;
    record = minic_c0_program_record(program, plain.record_id);
    if (record == NULL || !record->is_complete) return false;
    /* GNU zero-sized record members have SysV NO_CLASS, not MEMORY. */
    if (bytes == 0U) return true;
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field = minic_c0_record_field(record, index);
        size_t field_bytes, field_alignment, offset, bit_offset;
        if (field == NULL || field->is_flexible_array ||
            field->is_zero_length_array || field->element_count == 0U ||
            !minic_data_layout_type(target->data_layout, program, field->type,
                                    &field_bytes, &field_alignment) ||
            !minic_data_layout_record_field_layout(target->data_layout, program,
                                                    record, index, &offset, &bit_offset) ||
            !field_alignment || offset > bytes ||
            absolute + offset > max_bytes)
            return false;
        if (field->is_bit_field) {
            size_t begin_bits, end_bits;
            if (!minic_type_is_integer(field->type) || bit_offset >= 8U ||
                field->bit_width > 64U ||
                (max_bytes == 16U && offset >= bytes) ||
                bit_offset > (bytes - offset) * 8U ||
                field->bit_width > (bytes - offset) * 8U - bit_offset)
                return false;
            begin_bits = (absolute + offset) * 8U + bit_offset;
            end_bits = begin_bits + field->bit_width;
            if (end_bits > max_bytes * 8U ||
                (max_bytes == 16U && field->bit_width != 0U &&
                 (begin_bits / 64U) != ((end_bits - 1U) / 64U)))
                return false;
            continue;
        }
        if ((max_bytes == 8U && offset % field_alignment != 0U) ||
            (field_bytes != 0U &&
             field->element_count > (bytes - offset) / field_bytes))
            return false;
        if (field_bytes == 0U) {
            if (!minic_type_is_record(field->type)) return false;
            continue;
        }
        for (size_t element = 0U; element < field->element_count; ++element) {
            if (!x86_integer_record_tree(program, target, field->type,
                  absolute + offset + element * field_bytes,
                  max_bytes, depth + 1U)) return false;
        }
    }
    return true;
}

bool minic_x86_64_one_gp_record(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type,
                              size_t *record_bytes) {
    MinicAggregateStorageShape shape;
    if (program == NULL || target == NULL || record_bytes == NULL ||
        !minic_type_is_record(type) ||
        !minic_common_aggregate_storage_shape(
            program, target->data_layout, type, 8U, &shape) ||
        (shape.bytes != 1U && shape.bytes != 2U &&
         shape.bytes != 4U && shape.bytes != 8U) ||
        shape.alignment > 8U ||
        !x86_integer_record_tree(program, target, type, 0U, 8U, 0U))
        return false;
    *record_bytes = shape.bytes;
    return true;
}

bool minic_x86_64_two_gp_record(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type) {
    MinicAggregateStorageShape shape;
    if (program == NULL || target == NULL || !minic_type_is_record(type))
        return false;
    return minic_common_aggregate_storage_shape(
               program, target->data_layout, type, 8U, &shape) &&
           shape.bytes == 16U && shape.alignment <= 8U &&
           x86_integer_record_tree(program, target, type, 0U, 16U, 0U);
}

bool minic_x86_64_record_abi_compatible(MinicType actual, MinicType declared) {
    MinicType normalized_actual, normalized_declared;
    /* C erases a parameter's top-level `const`/`volatile` for by-value
     * ABI placement. Preserve nested pointee qualifiers and record identity. */
    return minic_type_is_record(actual) && minic_type_is_record(declared) &&
        minic_type_unqualified(actual, &normalized_actual) &&
        minic_type_unqualified(declared, &normalized_declared) &&
        minic_type_equal(normalized_actual, normalized_declared);
}

bool minic_x86_64_memory_result_record(const MinicC0Program *program,
                                      const MinicTargetInfo *target,
                                      MinicType type, size_t *bytes) {
    size_t alignment;
    if (program == NULL || target == NULL || bytes == NULL ||
        !minic_type_is_record(type) ||
        !minic_data_layout_type(target->data_layout, program, type,
                                bytes, &alignment)) return false;
    /* The current ABI subset admits exact, bounded >16-byte sret records.
     * Unclassified <=16-byte SSE/mixed/unaligned records remain fail-closed,
     * never silently impersonated by a memory return. */
    return *bytes > 16U && *bytes <= 4096U &&
           alignment != 0U && alignment <= 16U;
}

