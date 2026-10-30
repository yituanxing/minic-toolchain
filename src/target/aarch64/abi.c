#include "target/aarch64/abi.h"
#include "target/common/lp64_aggregate.h"

/* AAPCS64 B.3 HFA/HVA classes precede composite indirection. A 48/64-byte
 * homogeneous FP aggregate must not be passed as a GP pointer. Neither
 * conservative rejection nor common LP64 integer classification claims
 * support for unimplemented HFA/vector register marshaling. */
static bool aarch64_special_aggregate_member(
    const MinicC0Program *program, MinicType type, unsigned int depth) {
    size_t i;
    if (program == NULL || depth > 16U) return true;
    /* The frontend retains a pointee's base_kind in pointer/array types.
     * A double* member (including an array of such pointers) is INTEGER
     * class, not an HFA leaf. Resolve the full indirection chain before
     * inspecting floating base_kind. Actual float arrays still reject. */
    if (minic_type_is_pointer(type)) return false;
    if (minic_type_is_array(type)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, type.array_type_id);
        return array == NULL ||
               aarch64_special_aggregate_member(
                   program, array->element_type, depth + 1U);
    }
    switch (type.base_kind) {
    case MINIC_TYPE_BASE_FLOAT:
    case MINIC_TYPE_BASE_DOUBLE:
    case MINIC_TYPE_BASE_LONG_DOUBLE:
    case MINIC_TYPE_BASE_FLOAT128:
    case MINIC_TYPE_BASE_FLOAT32:
    case MINIC_TYPE_BASE_FLOAT64:
    case MINIC_TYPE_BASE_FLOAT32X:
    case MINIC_TYPE_BASE_FLOAT64X:
    case MINIC_TYPE_BASE_AARCH64_FLOAT32X4:
    case MINIC_TYPE_BASE_AARCH64_FLOAT64X2:
    case MINIC_TYPE_BASE_AARCH64_SVE_FLOAT32:
    case MINIC_TYPE_BASE_AARCH64_SVE_FLOAT64:
    case MINIC_TYPE_BASE_AARCH64_SVE_BOOL:
        return true;
    default:
        break;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record =
            minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) return true;
        for (i = 0U; i < record->field_count; ++i) {
            const MinicRecordField *field = minic_c0_record_field(record, i);
            if (field == NULL ||
                aarch64_special_aggregate_member(
                    program, field->type, depth + 1U))
                return true;
        }
        return false;
    }
    return !minic_type_is_integer(type) && !minic_type_is_pointer(type);
}

bool minic_aarch64_indirect_record(const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   MinicType type, size_t *bytes) {
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        !minic_multi_indirect_record(program, target, type, bytes) ||
        aarch64_special_aggregate_member(program, type, 0U))
        return false;
    return true;
}
