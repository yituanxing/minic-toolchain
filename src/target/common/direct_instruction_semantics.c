#include "target/common/direct_instruction_semantics.h"
#include "target/common/lp64_scalar.h"
#include "target/data_layout.h"

#include <stdint.h>

bool minic_multi_direct_record_copy_extent(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *bytes) {
    MinicCoreValueId dst, src;
    MinicType dst_type, src_type, dst_unqualified, src_unqualified;
    size_t alignment;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        bytes == NULL)
        return false;
    dst = ins->value.record_copy.destination_address;
    src = ins->value.record_copy.source_address;
    if (ins->kind != MINIC_CORE_INSTRUCTION_RECORD_COPY ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_record(ins->type) ||
        dst >= fn->value_count || src >= fn->value_count ||
        !minic_type_pointee(fn->values[dst].type, &dst_type) ||
        !minic_type_pointee(fn->values[src].type, &src_type) ||
        !minic_type_unqualified(dst_type, &dst_unqualified) ||
        !minic_type_unqualified(src_type, &src_unqualified) ||
        !minic_type_equal(dst_unqualified, ins->type) ||
        !minic_type_equal(src_unqualified, ins->type) ||
        !minic_data_layout_type(target->data_layout, program, ins->type,
                                bytes, &alignment) ||
        alignment == 0U || *bytes > 2048U)
        return false;
    return true;
}

bool minic_multi_direct_parameter_object_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming_plan,
    const MinicLP64FramePlan *frame_plan,
    const MinicLP64ArgumentLocation **location, size_t *object_offset) {
    size_t position;
    MinicCoreObjectId obj_id;
    const MinicLP64ArgumentLocation *loc;
    if (fn == NULL || ins == NULL || incoming_plan == NULL ||
        frame_plan == NULL || location == NULL || object_offset == NULL)
        return false;
    position = ins->value.parameter_object.parameter_index;
    obj_id = ins->value.parameter_object.object_id;
    if (ins->kind != MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT ||
        position >= incoming_plan->argument_count ||
        position >= fn->parameter_count)
        return false;
    loc = &incoming_plan->arguments[position];
    if (ins->result != MINIC_CORE_VALUE_INVALID ||
        obj_id >= fn->object_count ||
        !minic_type_equal(fn->objects[obj_id].type, fn->parameter_types[position]) ||
        fn->objects[obj_id].element_count != 1U ||
        (loc->kind != MINIC_LP64_ARGUMENT_RECORD &&
         loc->kind != MINIC_LP64_ARGUMENT_INDIRECT_RECORD) ||
        !minic_lp64_frame_plan_object(frame_plan, obj_id, object_offset))
        return false;
    *location = loc;
    return true;
}

bool minic_multi_direct_scalar_store_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    bool atomic;
    MinicCoreValueId addr, value;
    MinicType pointee;
    size_t pointee_width = 0U, pointee_align = 0U;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL || is_pointer == NULL)
        return false;
    atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
    addr = atomic ? ins->value.atomic_store.address
                  : ins->value.store.address;
    value = atomic ? ins->value.atomic_store.stored_value
                   : ins->value.store.stored_value;
    if ((ins->kind != MINIC_CORE_INSTRUCTION_STORE && !atomic) ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        addr >= fn->value_count || value >= fn->value_count ||
        !minic_type_is_pointer(fn->values[addr].type))
        return false;
    *width = 0U;
    *is_signed = false;
    *is_pointer = minic_type_is_pointer(fn->values[value].type);
    /* These are the existing *relaxed-only*, scalar, naturally-aligned
     * admission rules. This is not a general atomic memory model. */
    if (atomic &&
        (ins->value.atomic_store.memory_order != 0U ||
         !minic_type_pointee(fn->values[addr].type, &pointee) ||
         !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
         !minic_data_layout_type(target->data_layout, program,
                                 pointee, &pointee_width, &pointee_align)))
        return false;
    if (!*is_pointer && !minic_type_is_double(fn->values[value].type) &&
        !minic_type_is_float(fn->values[value].type) &&
        !minic_lp64_scalar_integer_width(program, target, fn->values[value].type,
                                         width, is_signed))
        return false;
    if (minic_type_is_double(fn->values[value].type))
        *width = 8U;
    if (minic_type_is_float(fn->values[value].type))
        *width = 4U;
    if (atomic &&
        (minic_type_is_double(fn->values[value].type) ||
         minic_type_is_float(fn->values[value].type) ||
         (*is_pointer ? pointee_width != 8U :
          (*width != 1U && *width != 2U &&
           *width != 4U && *width != 8U) ||
          *width != pointee_width)))
        return false;
    (void)pointee_align;
    return true;
}
