#include "target/common/direct_instruction_semantics.h"
#include "core/core_ir.h"
#include "target/common/core_instruction_family.h"
#include "target/common/lp64_scalar.h"
#include "target/data_layout.h"

#include <stdint.h>

/* Source-free Core queries for the live Compiler -> Target path. C descriptor
 * branches below are reserved for standalone pre-snapshot fixture callers. */
static const MinicCoreType *direct_core_type(const MinicCoreFunction *fn,
                                             MinicCoreTypeId id) {
    if (fn == NULL || fn->type_context == NULL) return NULL;
    return minic_core_type_get(fn->type_context,
                               minic_core_type_unqualified(fn->type_context, id));
}

static bool direct_core_kind(const MinicCoreFunction *fn,
                             MinicCoreTypeId id, MinicCoreTypeKind kind) {
    return fn != NULL && fn->type_context != NULL &&
           minic_lp64_core_kind(fn->type_context, id, kind);
}

static bool direct_core_float(const MinicCoreFunction *fn,
                              MinicCoreTypeId id, MinicCoreFloatFormat format) {
    const MinicCoreType *type = direct_core_type(fn, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_FLOAT &&
           type->value.floating.format == format;
}

static bool direct_core_bool(const MinicCoreFunction *fn, MinicCoreTypeId id) {
    const MinicCoreType *type = direct_core_type(fn, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->value.integer.is_bool;
}

static bool direct_core_int_width(const MinicCoreFunction *fn,
                                  MinicCoreTypeId id, size_t *width,
                                  bool *sign) {
    return fn != NULL && fn->type_context != NULL &&
           minic_lp64_core_integer_width(fn->type_context, id, width, sign);
}

static bool direct_core_pointer_pointee(const MinicCoreFunction *fn,
                                        MinicCoreTypeId pointer,
                                        MinicCoreTypeId *pointee) {
    return fn != NULL && fn->type_context != NULL &&
           minic_lp64_core_pointer_pointee(fn->type_context, pointer, pointee);
}

static bool direct_core_equal_unqualified(const MinicCoreFunction *fn,
                                          MinicCoreTypeId left,
                                          MinicCoreTypeId right) {
    return fn != NULL && fn->type_context != NULL &&
           minic_lp64_core_equal_unqualified(fn->type_context, left, right);
}

static bool direct_core_wide(const MinicCoreFunction *fn, MinicCoreTypeId id) {
    const MinicCoreType *type = direct_core_type(fn, id);
    return type != NULL && type->has_layout && type->size == 16U &&
           ((type->kind == MINIC_CORE_TYPE_INTEGER &&
             type->value.integer.bit_width == 128U) ||
            (type->kind == MINIC_CORE_TYPE_FLOAT &&
             type->value.floating.format == MINIC_CORE_FLOAT_EXTENDED));
}

static bool direct_core_register64(const MinicCoreFunction *fn,
                                   MinicCoreTypeId id) {
    size_t width;
    bool sign;
    return direct_core_kind(fn, id, MINIC_CORE_TYPE_POINTER) ||
           (direct_core_int_width(fn, id, &width, &sign) && width == 8U);
}

static bool direct_core_i32(const MinicCoreFunction *fn,
                            MinicCoreTypeId id) {
    const MinicCoreType *type = direct_core_type(fn, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->has_layout && type->size == 4U &&
           type->value.integer.bit_width == 32U &&
           type->value.integer.is_signed && !type->value.integer.is_bool;
}

static bool direct_type_void(const MinicCoreFunction *fn, MinicCoreTypeId id,
                             MinicType legacy) {
    return fn->type_context != NULL
        ? direct_core_kind(fn, id, MINIC_CORE_TYPE_VOID)
        : minic_type_is_void(legacy);
}

static bool direct_type_pointer(const MinicCoreFunction *fn, MinicCoreTypeId id,
                                MinicType legacy) {
    return fn->type_context != NULL
        ? direct_core_kind(fn, id, MINIC_CORE_TYPE_POINTER)
        : minic_type_is_pointer(legacy);
}

static bool direct_type_integer(const MinicCoreFunction *fn, MinicCoreTypeId id,
                                MinicType legacy) {
    return fn->type_context != NULL
        ? direct_core_kind(fn, id, MINIC_CORE_TYPE_INTEGER)
        : minic_type_is_integer(legacy);
}

static bool direct_type_record(const MinicCoreFunction *fn, MinicCoreTypeId id,
                               MinicType legacy) {
    return fn->type_context != NULL
        ? direct_core_kind(fn, id, MINIC_CORE_TYPE_RECORD)
        : minic_type_is_record(legacy);
}

static bool direct_type_bool(const MinicCoreFunction *fn, MinicCoreTypeId id,
                             MinicType legacy) {
    return fn->type_context != NULL
        ? direct_core_bool(fn, id)
        : minic_type_is_bool_integer(legacy);
}

static bool direct_type_float(const MinicCoreFunction *fn, MinicCoreTypeId id,
                              MinicType legacy) {
    return fn->type_context != NULL
        ? direct_core_float(fn, id, MINIC_CORE_FLOAT_BINARY32)
        : minic_type_is_float(legacy);
}

static bool direct_type_double(const MinicCoreFunction *fn, MinicCoreTypeId id,
                               MinicType legacy) {
    return fn->type_context != NULL
        ? direct_core_float(fn, id, MINIC_CORE_FLOAT_BINARY64)
        : minic_type_is_double(legacy);
}

static bool direct_type_long_double(const MinicCoreFunction *fn,
                                    MinicCoreTypeId id, MinicType legacy) {
    return fn->type_context != NULL
        ? direct_core_float(fn, id, MINIC_CORE_FLOAT_EXTENDED)
        : minic_type_is_long_double(legacy);
}

static bool direct_type_int128(const MinicCoreFunction *fn,
                               MinicCoreTypeId id, MinicType legacy) {
    const MinicCoreType *type;
    if (fn->type_context == NULL) return minic_type_is_int128_integer(legacy);
    type = direct_core_type(fn, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->value.integer.bit_width == 128U && type->has_layout;
}

static bool direct_type_equal(const MinicCoreFunction *fn,
                              MinicCoreTypeId lhs_id, MinicCoreTypeId rhs_id,
                              MinicType lhs, MinicType rhs) {
    if (fn->type_context == NULL) return minic_type_equal(lhs, rhs);
    return lhs_id != MINIC_CORE_TYPE_INVALID &&
           minic_core_type_get(fn->type_context, lhs_id) != NULL &&
           lhs_id == rhs_id;
}

static bool direct_type_integer_width(const MinicC0Program *program,
                                      const MinicTargetInfo *target,
                                      const MinicCoreFunction *fn,
                                      MinicCoreTypeId id, MinicType legacy,
                                      size_t *width, bool *sign) {
    return fn->type_context != NULL
        ? direct_core_int_width(fn, id, width, sign)
        : minic_lp64_scalar_integer_width(program, target, legacy, width, sign);
}

bool minic_multi_direct_call_frame_level0_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    MinicCoreCallFrameAddressKind kind) {
    MinicType pointee;
    if (fn != NULL && ins != NULL && fn->type_context != NULL) {
        MinicCoreTypeId core_pointee;
        return (kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN ||
                kind == MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
               ins->kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS &&
               ins->result < fn->value_count &&
               ins->value.call_frame_address.level == 0U &&
               ins->value.call_frame_address.kind == kind &&
               direct_core_pointer_pointee(fn, ins->core_type_id, &core_pointee) &&
               direct_core_kind(fn, core_pointee, MINIC_CORE_TYPE_VOID) &&
               fn->values[ins->result].core_type_id == ins->core_type_id;
    }
    return fn != NULL && ins != NULL &&
           (kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN ||
            kind == MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
           ins->kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS &&
           ins->result < fn->value_count &&
           ins->value.call_frame_address.level == 0U &&
           ins->value.call_frame_address.kind == kind &&
           minic_type_pointee(ins->type, &pointee) &&
           minic_type_is_void(pointee) &&
           minic_type_equal(fn->values[ins->result].type, ins->type);
}

bool minic_multi_direct_call_frame_return_level0_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins) {
    return minic_multi_direct_call_frame_level0_shape(
        fn, ins, MINIC_CORE_CALL_FRAME_ADDRESS_RETURN);
}

bool minic_multi_direct_fixed_register_write_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreFixedRegisterBinding **binding_out) {
    const MinicCoreFixedRegisterBinding *binding;
    const size_t index = ins != NULL
        ? ins->value.fixed_register_write.binding_id : SIZE_MAX;
    const MinicCoreValueId source = ins != NULL
        ? ins->value.fixed_register_write.source : MINIC_CORE_VALUE_INVALID;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        binding_out == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !(fn->type_context != NULL
            ? direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_VOID)
            : minic_type_is_void(ins->type)) ||
        index >= fn->fixed_register_binding_count ||
        fn->fixed_register_bindings == NULL || source >= fn->value_count)
        return false;
    binding = &fn->fixed_register_bindings[index];
    if (binding->is_local || binding->register_name == NULL ||
        binding->register_name_length == 0U ||
        !minic_target_info_fixed_register_write_supported(
            target, binding->register_name, binding->register_name_length) ||
        !(fn->type_context != NULL
            ? binding->core_type_id == fn->values[source].core_type_id
            : minic_type_equal(binding->type, fn->values[source].type)) ||
        !(fn->type_context != NULL
            ? direct_core_register64(fn, binding->core_type_id)
            : minic_lp64_register64_type(program, target, binding->type)))
        return false;
    *binding_out = binding;
    return true;
}

bool minic_multi_direct_fixed_register_read_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreFixedRegisterBinding **binding_out) {
    const MinicCoreFixedRegisterBinding *binding;
    const size_t index = ins != NULL ? ins->value.fixed_register_binding_id : 0U;
    if (!program || !target || !fn || !ins || !binding_out ||
        ins->kind != MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ ||
        ins->result >= fn->value_count ||
        index >= fn->fixed_register_binding_count ||
        fn->fixed_register_bindings == NULL)
        return false;
    binding = &fn->fixed_register_bindings[index];
    if (binding->is_local || !binding->register_name ||
        binding->register_name_length == 0U ||
        !minic_target_info_fixed_register_supported(
            target, binding->register_name, binding->register_name_length) ||
        !(fn->type_context != NULL
            ? binding->core_type_id == ins->core_type_id &&
              fn->values[ins->result].core_type_id == ins->core_type_id
            : minic_type_equal(binding->type, ins->type) &&
              minic_type_equal(fn->values[ins->result].type, ins->type)) ||
        !(fn->type_context != NULL
            ? direct_core_register64(fn, ins->core_type_id)
            : minic_lp64_register64_type(program, target, ins->type)))
        return false;
    *binding_out = binding;
    return true;
}

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
    if (fn->type_context != NULL) {
        MinicCoreTypeId dst_pointee, src_pointee;
        const MinicCoreType *record = direct_core_type(fn, ins->core_type_id);
        if (ins->kind != MINIC_CORE_INSTRUCTION_RECORD_COPY ||
            ins->result != MINIC_CORE_VALUE_INVALID ||
            record == NULL || record->kind != MINIC_CORE_TYPE_RECORD ||
            !record->has_layout || record->alignment == 0U ||
            dst >= fn->value_count || src >= fn->value_count ||
            !direct_core_pointer_pointee(fn, fn->values[dst].core_type_id,
                                         &dst_pointee) ||
            !direct_core_pointer_pointee(fn, fn->values[src].core_type_id,
                                         &src_pointee) ||
            !direct_core_equal_unqualified(fn, dst_pointee, ins->core_type_id) ||
            !direct_core_equal_unqualified(fn, src_pointee, ins->core_type_id) ||
            record->size > (16U * 1024U * 1024U)) return false;
        *bytes = record->size;
        return true;
    }
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
        /* Kernel task_struct and other compound records are often many
         * KiB. Native emitters use counted byte loops for large transfers;
         * this cap bounds the requested runtime copy without imposing the
         * former 2-KiB single-instruction-source expansion limit. */
        alignment == 0U || *bytes > (16U * 1024U * 1024U))
        return false;
    return true;
}

/* GNU C record value snapshots are private bytewise copies of an
 * address-backed lvalue. Core owns type/volatility and DataLayout bounds;
 * native emitters alone own the actual ISA byte operations. */
bool minic_multi_direct_record_load_extent(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan,
    MinicCoreValueId *source, size_t *object_offset, size_t *bytes) {
    MinicCoreObjectId dst;
    MinicType source_pointee, source_unqualified;
    size_t alignment;
    if (!program || !target || !fn || !ins || !frame_plan ||
        !source || !object_offset || !bytes ||
        ins->kind != MINIC_CORE_INSTRUCTION_RECORD_LOAD ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !(fn->type_context != NULL
            ? direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_RECORD)
            : minic_type_is_record(ins->type))) return false;
    dst = ins->value.record_load.destination_object;
    *source = ins->value.record_load.source_address;
    if (fn->type_context != NULL) {
        MinicCoreTypeId pointee_id;
        const MinicCoreType *source_type, *record;
        if (dst >= fn->object_count || *source >= fn->value_count ||
            fn->objects[dst].element_count != 1U ||
            !direct_core_equal_unqualified(fn, fn->objects[dst].core_type_id,
                                           ins->core_type_id) ||
            !direct_core_pointer_pointee(fn, fn->values[*source].core_type_id,
                                         &pointee_id) ||
            !direct_core_equal_unqualified(fn, pointee_id, ins->core_type_id) ||
            (source_type = minic_core_type_get(fn->type_context, pointee_id)) == NULL ||
            ins->value.record_load.is_volatile != source_type->is_volatile ||
            !minic_lp64_frame_plan_object(frame_plan, dst, object_offset) ||
            (record = direct_core_type(fn, ins->core_type_id)) == NULL ||
            record->kind != MINIC_CORE_TYPE_RECORD || !record->has_layout ||
            record->size == 0U || record->alignment == 0U ||
            record->size > (16U * 1024U * 1024U)) return false;
        *bytes = record->size;
        return true;
    }
    if (dst >= fn->object_count || *source >= fn->value_count ||
        fn->objects[dst].element_count != 1U ||
        !minic_type_equal(fn->objects[dst].type, ins->type) ||
        !minic_type_pointee(fn->values[*source].type, &source_pointee) ||
        !minic_type_unqualified(source_pointee, &source_unqualified) ||
        !minic_type_equal(source_unqualified, ins->type) ||
        ins->value.record_load.is_volatile != minic_type_is_volatile(source_pointee) ||
        !minic_lp64_frame_plan_object(frame_plan, dst, object_offset) ||
        !minic_data_layout_type(target->data_layout, program, ins->type,
                                bytes, &alignment) ||
        !*bytes || !alignment || *bytes > (16U * 1024U * 1024U))
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
    MinicType object_value_type, parameter_value_type;
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
    if (fn->type_context != NULL) {
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            obj_id >= fn->object_count ||
            fn->core_parameter_type_ids == NULL ||
            !direct_core_equal_unqualified(fn, fn->objects[obj_id].core_type_id,
                                           fn->core_parameter_type_ids[position]) ||
            fn->objects[obj_id].element_count != 1U ||
            (loc->kind != MINIC_LP64_ARGUMENT_RECORD &&
             loc->kind != MINIC_LP64_ARGUMENT_INDIRECT_RECORD &&
             loc->kind != MINIC_LP64_ARGUMENT_EMPTY_RECORD) ||
            !minic_lp64_frame_plan_object(frame_plan, obj_id, object_offset))
            return false;
        *location = loc;
        return true;
    }
    if (ins->result != MINIC_CORE_VALUE_INVALID ||
        obj_id >= fn->object_count ||
        /* GNU top-level const on a by-value parameter has no ABI effect.
         * Only remove top-level qualification, preserving nested pointee
         * and member qualifiers and the exact record identity. */
        !minic_type_unqualified(fn->objects[obj_id].type, &object_value_type) ||
        !minic_type_unqualified(fn->parameter_types[position], &parameter_value_type) ||
        !minic_type_equal(object_value_type, parameter_value_type) ||
        fn->objects[obj_id].element_count != 1U ||
        (loc->kind != MINIC_LP64_ARGUMENT_RECORD &&
         loc->kind != MINIC_LP64_ARGUMENT_INDIRECT_RECORD &&
         loc->kind != MINIC_LP64_ARGUMENT_EMPTY_RECORD) ||
        !minic_lp64_frame_plan_object(frame_plan, obj_id, object_offset))
        return false;
    *location = loc;
    return true;
}


/* A compiler barrier is an ordering constraint in the Core stream, not a
 * machine fence and not a produced SSA value. */
static bool minic_multi_direct_compiler_barrier_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins) {
    return ins != NULL &&
           ins->kind == MINIC_CORE_INSTRUCTION_COMPILER_BARRIER &&
           ins->result == MINIC_CORE_VALUE_INVALID &&
           direct_type_void(fn, ins->core_type_id, ins->type);
}

/* Both direct LP64 emitters materialize int128 and long double in a 16-byte
 * Core frame slot, but select fundamentally different ISA load/store forms.
 * Validate the common source address and slot identity only once. */
bool minic_multi_direct_wide_store_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan, size_t *value_offset) {
    MinicCoreValueId address, value;
    if (fn == NULL || ins == NULL || frame_plan == NULL ||
        value_offset == NULL || ins->kind != MINIC_CORE_INSTRUCTION_STORE)
        return false;
    address = ins->value.store.address;
    value = ins->value.store.stored_value;
    return address < fn->value_count && value < fn->value_count &&
           direct_type_pointer(fn, fn->values[address].core_type_id,
                               fn->values[address].type) &&
           (direct_type_int128(fn, fn->values[value].core_type_id,
                               fn->values[value].type) ||
            direct_type_long_double(fn, fn->values[value].core_type_id,
                                    fn->values[value].type)) &&
           minic_lp64_frame_plan_wide_slot(frame_plan, value, value_offset);
}

/* One machine-independent result-shape gate for direct LP64 codegen.
 * AArch64 admits binary64 VA_ARG; LoongArch64 does not yet, so callers
 * select that documented ISA capability explicitly. */
static bool minic_multi_direct_result_slot_offset(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool allow_variadic_double_result, size_t *offset) {
    if (fn == NULL || ins == NULL || offset == NULL)
        return false;
    if ((ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) &&
        (direct_type_void(fn, ins->core_type_id, ins->type) ||
         (direct_type_record(fn, ins->core_type_id, ins->type) &&
          (ins->kind == MINIC_CORE_INSTRUCTION_CALL
              ? ins->value.call.result_object
              : ins->value.indirect_call.result_object) != MINIC_CORE_OBJECT_INVALID))) {
        /* Effect-only call or record-result object: no scalar Core slot. */
        *offset = 0U;
        return true;
    }
    if (ins->result == MINIC_CORE_VALUE_INVALID ||
        ins->result >= fn->value_count ||
        ((ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
          ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS)
            ? !direct_type_pointer(fn, ins->core_type_id, ins->type)
            : !direct_type_integer(fn, ins->core_type_id, ins->type) &&
              !direct_type_pointer(fn, ins->core_type_id, ins->type) &&
              !(direct_type_float(fn, ins->core_type_id, ins->type) &&
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
              !(direct_type_long_double(fn, ins->core_type_id, ins->type) &&
                (ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                 ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                 ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL)) &&
              !(direct_type_double(fn, ins->core_type_id, ins->type) &&
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
                 (allow_variadic_double_result &&
                  ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR) ||
                 ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL))))
        return false;
    /* Slot placement is no longer encoded in the semantic shape. The
     * common machine dispatch resolves this validated Core result through
     * the shared LP64 FramePlan's last-use mapping. */
    *offset = 0U;
    return true;
}



/* Instruction kinds and Core operand/result types are not properties of
 * an ISA encoder. Keep the shared IEEE-754 validation here, but leave the
 * actual AArch64/LoongArch64 floating opcodes and register transfers local. */
/* This is the complete numeric machine family implemented by both
 * direct LP64 emitters. SSA/result types remain validated by the separate
 * minic_multi_direct_floating_shape gate, never by opcode alone. */
/* Complete Core integer, pointer-comparison and bit-operation machine
 * family shared by AAPCS64 and LP64D; instruction encodings remain ISA-owned. */
/* The two ISA implementations consume the same Core call/parameter ABI
 * transfer family. Planning remains target-ABI-specific at the call site. */
/* Common Core memory-effect family: ISA byte moves remain independent.
 * The relaxed-only atomic memory-order contract is checked separately. */
static bool minic_multi_direct_abi_transfer_kind(MinicCoreInstructionKind kind) {
    return minic_core_instruction_is_call(kind) ||
           kind == MINIC_CORE_INSTRUCTION_PARAMETER;
}

/* Bounds and scalar result typing are a Core contract, not a per-ISA
 * register assignment rule. The ABI-specific plan already owns whether
 * a named parameter occupies FP/GP registers or caller stack. */
bool minic_multi_direct_scalar_parameter_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming,
    const MinicLP64ArgumentLocation **location) {
    size_t ordinal;
    if (fn == NULL || ins == NULL || incoming == NULL ||
        location == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_PARAMETER ||
        fn->parameter_types == NULL)
        return false;
    ordinal = (size_t)ins->value.parameter_index;
    if (ordinal >= fn->parameter_count ||
        ordinal >= incoming->argument_count ||
        !(fn->type_context != NULL
            ? fn->core_parameter_type_ids != NULL &&
              ins->core_type_id == fn->core_parameter_type_ids[ordinal]
            : minic_type_equal(ins->type, fn->parameter_types[ordinal])) ||
        incoming->arguments[ordinal].kind == MINIC_LP64_ARGUMENT_RECORD ||
        incoming->arguments[ordinal].kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD)
        return false;
    *location = &incoming->arguments[ordinal];
    return true;
}

static bool minic_multi_direct_integer_machine_kind(MinicCoreInstructionKind kind) {
    /* Constants and conversions use distinct ABI/residual lowering in
     * Direct LP64, unlike RV32/RV64 which use their scalar emitter. */
    return kind == MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
           (kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT &&
            kind != MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION &&
            minic_core_instruction_is_integer_scalar(kind));
}

bool minic_multi_direct_floating_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *integer_width, bool *integer_signed) {
    MinicCoreValueId left, right, operand;
    if (fn == NULL || ins == NULL)
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        return direct_type_float(fn, ins->core_type_id, ins->type) ||
               direct_type_double(fn, ins->core_type_id, ins->type) ||
               direct_type_long_double(fn, ins->core_type_id, ins->type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        left = ins->value.binary.left;
        right = ins->value.binary.right;
        return (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL ||
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ||
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL
                    ? (fn->type_context != NULL
                          ? direct_core_i32(fn, ins->core_type_id)
                          : minic_type_equal(ins->type, minic_type_int()))
                    : direct_type_double(fn, ins->core_type_id, ins->type)) &&
               left < fn->value_count && right < fn->value_count &&
               direct_type_double(fn, fn->values[left].core_type_id,
                                  fn->values[left].type) &&
               direct_type_double(fn, fn->values[right].core_type_id,
                                  fn->values[right].type);
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        left = ins->value.binary.left;
        right = ins->value.binary.right;
        return direct_type_float(fn, ins->core_type_id, ins->type) &&
               left < fn->value_count && right < fn->value_count &&
               direct_type_float(fn, fn->values[left].core_type_id,
                                 fn->values[left].type) &&
               direct_type_float(fn, fn->values[right].core_type_id,
                                 fn->values[right].type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        operand = ins->value.operand;
        return direct_type_double(fn, ins->core_type_id, ins->type) &&
               operand < fn->value_count &&
               direct_type_double(fn, fn->values[operand].core_type_id,
                                  fn->values[operand].type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        operand = ins->value.operand;
        return direct_type_float(fn, ins->core_type_id, ins->type) &&
               operand < fn->value_count &&
               direct_type_double(fn, fn->values[operand].core_type_id,
                                  fn->values[operand].type);
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        operand = ins->value.operand;
        return direct_type_double(fn, ins->core_type_id, ins->type) &&
               operand < fn->value_count &&
               direct_type_float(fn, fn->values[operand].core_type_id,
                                 fn->values[operand].type);
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        operand = ins->value.operand;
        return program != NULL && target != NULL &&
               integer_width != NULL && integer_signed != NULL &&
               direct_type_double(fn, ins->core_type_id, ins->type) &&
               operand < fn->value_count &&
               direct_type_integer_width(program, target, fn,
                   fn->values[operand].core_type_id, fn->values[operand].type,
                   integer_width, integer_signed);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        operand = ins->value.operand;
        if (program == NULL || target == NULL ||
            integer_width == NULL || integer_signed == NULL ||
            operand >= fn->value_count ||
            !direct_type_double(fn, fn->values[operand].core_type_id,
                                fn->values[operand].type) ||
            !direct_type_integer_width(program, target, fn,
                ins->core_type_id, ins->type, integer_width, integer_signed))
            return false;
        return *integer_width == 1U || *integer_width == 2U ||
               *integer_width == 4U || *integer_width == 8U;
    default:
        return false;
    }
}

/* Scalar bitcast shape for direct LP64 lowering. Pointer->integer preserves
 * all 64 address bits at pointer width, truncates to 32 bits for C's narrow
 * integer casts, or produces canonical 0/1 for C pointer->bool.
 * source_width reports the integer result width for pointer->integer and
 * the integer input width for integer->pointer; source_signed describes the
 * integer result on pointer->integer and the input otherwise. */
bool minic_multi_direct_scalar_bitcast_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool *pointer_to_integer, size_t *source_width,
    bool *source_signed, bool *source_pointer) {
    MinicCoreValueId source;
    MinicType input;
    size_t output_width;
    bool output_signed;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        pointer_to_integer == NULL || source_width == NULL ||
        source_signed == NULL || source_pointer == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_BITCAST)
        return false;
    source = ins->value.operand;
    if (source >= fn->value_count)
        return false;
    input = fn->values[source].type;
    if (fn->type_context != NULL
            ? !((direct_type_pointer(fn, ins->core_type_id, ins->type) &&
                 (direct_type_pointer(fn, fn->values[source].core_type_id, input) ||
                  direct_type_integer(fn, fn->values[source].core_type_id, input))) ||
                (direct_type_integer(fn, ins->core_type_id, ins->type) &&
                 direct_type_pointer(fn, fn->values[source].core_type_id, input)))
            : !minic_core_scalar_bitcast_types_valid(ins->type, input))
        return false;
    *source_pointer = direct_type_pointer(fn, fn->values[source].core_type_id, input);
    *pointer_to_integer = *source_pointer &&
                          direct_type_integer(fn, ins->core_type_id, ins->type);
    *source_width = 0U;
    *source_signed = false;
    if (*pointer_to_integer) {
        if (!direct_type_integer_width(program, target, fn,
                                       ins->core_type_id, ins->type,
                                       &output_width, &output_signed) ||
            (output_width != 8U && output_width != 4U &&
             !(output_width == 1U &&
               direct_type_bool(fn, ins->core_type_id, ins->type))))
            return false;
        *source_width = output_width;
        *source_signed = output_signed;
        return true;
    }
    if (!direct_type_pointer(fn, ins->core_type_id, ins->type))
        return false;
    return *source_pointer ||
           direct_type_integer_width(program, target, fn,
                                     fn->values[source].core_type_id, input,
                                     source_width, source_signed);
}

/* POINTER_LESS compares two identically typed pointer Core values and
 * produces C int. The unsigned machine comparison is ISA-owned. */
bool minic_multi_direct_pointer_less_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins) {
    MinicCoreValueId lhs, rhs;
    if (fn == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_POINTER_LESS ||
        !(fn->type_context != NULL
            ? direct_core_i32(fn, ins->core_type_id)
            : minic_type_equal(ins->type, minic_type_int())))
        return false;
    lhs = ins->value.binary.left;
    rhs = ins->value.binary.right;
    return lhs < fn->value_count && rhs < fn->value_count &&
           direct_type_pointer(fn, fn->values[lhs].core_type_id,
                               fn->values[lhs].type) &&
           direct_type_equal(fn, fn->values[lhs].core_type_id,
                             fn->values[rhs].core_type_id,
                             fn->values[lhs].type, fn->values[rhs].type);
}

/* Core unary/shift/compare type admission is shared across direct LP64
 * targets. Native extensions and opcode selection remain ISA-owned. */
bool minic_multi_direct_integer_unary_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed) {
    MinicCoreValueId operand;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_NEGATE &&
         ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT))
        return false;
    operand = ins->value.operand;
    return operand < fn->value_count &&
           direct_type_equal(fn, fn->values[operand].core_type_id,
                             ins->core_type_id,
                             fn->values[operand].type, ins->type) &&
           direct_type_integer_width(program, target, fn, ins->core_type_id,
                                     ins->type, width, is_signed) &&
           !direct_type_bool(fn, ins->core_type_id, ins->type);
}

/* File-private Core operand decoders. The public family probes below
 * retain their independent type, boolean, width and ABI admission rules. */
static bool core_direct_integer_pair(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *lw, size_t *rw, bool *ls, bool *rs) {
    MinicCoreValueId lhs, rhs;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        lw == NULL || rw == NULL || ls == NULL || rs == NULL)
        return false;
    lhs = ins->value.binary.left;
    rhs = ins->value.binary.right;
    return lhs < fn->value_count && rhs < fn->value_count &&
           direct_type_integer_width(program, target, fn,
                 fn->values[lhs].core_type_id, fn->values[lhs].type, lw, ls) &&
           direct_type_integer_width(program, target, fn,
                 fn->values[rhs].core_type_id, fn->values[rhs].type, rw, rs);
}

/* This remains a relaxed-only atomic preflight, not a C11 memory model.
 * LOAD and STORE apply separate payload/bit-width tests afterward. */
static bool core_direct_relaxed_atomic_pointee_width(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicCoreValueId address,
    unsigned int order, size_t *width) {
    MinicType pointee;
    size_t align;
    if (program == NULL || target == NULL || fn == NULL || width == NULL ||
        order != 0U || address >= fn->value_count) return false;
    if (fn->type_context != NULL) {
        MinicCoreTypeId pointee_id;
        const MinicCoreType *type;
        if (!direct_core_pointer_pointee(fn, fn->values[address].core_type_id,
                                         &pointee_id)) return false;
        type = direct_core_type(fn, pointee_id);
        if (type == NULL || !type->has_layout || type->alignment == 0U ||
            (type->kind != MINIC_CORE_TYPE_INTEGER &&
             type->kind != MINIC_CORE_TYPE_POINTER)) return false;
        *width = type->size;
        return true;
    }
    if (
        !minic_type_pointee(fn->values[address].type, &pointee) ||
        !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
        !minic_data_layout_type(target->data_layout, program, pointee,
                                width, &align))
        return false;
    (void)align;
    return true;
}

bool minic_multi_direct_integer_shift_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed) {
    MinicCoreValueId lhs;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        left_width == NULL || right_width == NULL ||
        left_signed == NULL || right_signed == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT &&
         ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT))
        return false;
    lhs = ins->value.binary.left;
    return lhs < fn->value_count &&
           direct_type_equal(fn, fn->values[lhs].core_type_id,
                             ins->core_type_id,
                             fn->values[lhs].type, ins->type) &&
           core_direct_integer_pair(program, target, fn, ins,
                                    left_width, right_width,
                                    left_signed, right_signed) &&
           (*left_width == 1U || *left_width == 2U ||
            *left_width == 4U || *left_width == 8U) &&
           !direct_type_bool(fn, ins->core_type_id, ins->type);
}

bool minic_multi_direct_integer_compare_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed) {
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        left_width == NULL || right_width == NULL ||
        left_signed == NULL || right_signed == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_LESS &&
         ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_EQUAL))
        return false;
    return core_direct_integer_pair(program, target, fn, ins,
                                    left_width, right_width,
                                    left_signed, right_signed) &&
           *left_width == *right_width && *left_signed == *right_signed &&
           (*left_width == 4U || *left_width == 8U);
}

bool minic_multi_direct_scalar_zero_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    MinicCoreValueId operand;
    MinicType type;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL || is_pointer == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO)
        return false;
    operand = ins->value.operand;
    if (operand >= fn->value_count) return false;
    type = fn->values[operand].type;
    *is_pointer = direct_type_pointer(fn, fn->values[operand].core_type_id, type);
    *width = 0U;
    *is_signed = false;
    return *is_pointer || direct_type_bool(fn, fn->values[operand].core_type_id, type) ||
           direct_type_integer_width(program, target, fn,
                                     fn->values[operand].core_type_id, type,
                                     width, is_signed);
}

/* AArch64's current direct emitter admits a 16-bit element-stride literal;
 * LoongArch64 materializes full-width strides. Keep the different limits. */
bool minic_multi_direct_pointer_offset_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t maximum_stride, size_t *index_width, bool *index_signed) {
    MinicCoreValueId base, index;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        index_width == NULL || index_signed == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_POINTER_OFFSET)
        return false;
    base = ins->value.pointer_offset.base;
    index = ins->value.pointer_offset.index;
    return base < fn->value_count && index < fn->value_count &&
           direct_type_pointer(fn, fn->values[base].core_type_id,
                               fn->values[base].type) &&
           direct_type_integer_width(program, target, fn,
                  fn->values[index].core_type_id, fn->values[index].type,
                  index_width, index_signed) &&
           ins->value.pointer_offset.element_size <= maximum_stride;
}

/* Arithmetic, bitwise and division/remainder retain distinct result-type
 * and 8/16/32/64-bit restrictions above the common pair decoder. */
bool minic_multi_direct_integer_binary_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed) {
    MinicCoreValueId lhs;
    bool arithmetic, bitwise, division;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        left_width == NULL || right_width == NULL ||
        left_signed == NULL || right_signed == NULL)
        return false;
    arithmetic = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
    bitwise = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND ||
              ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR ||
              ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
    division = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE ||
               ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
    if (!arithmetic && !bitwise && !division)
        return false;
    lhs = ins->value.binary.left;
    if (!core_direct_integer_pair(program, target, fn, ins,
                                  left_width, right_width,
                                  left_signed, right_signed) ||
        *left_width != *right_width || *left_signed != *right_signed)
        return false;
    if (arithmetic)
        return direct_type_equal(fn, fn->values[lhs].core_type_id,
                                 ins->core_type_id, fn->values[lhs].type,
                                 ins->type) &&
               !direct_type_bool(fn, ins->core_type_id, ins->type);
    if (bitwise)
        return (*left_width == 1U || *left_width == 2U ||
                *left_width == 4U || *left_width == 8U) &&
               !direct_type_bool(fn, ins->core_type_id, ins->type);
    return (*left_width == 4U || *left_width == 8U) &&
           direct_type_equal(fn, fn->values[lhs].core_type_id,
                             ins->core_type_id, fn->values[lhs].type,
                             ins->type);
}

/* The direct LP64 backends admit relaxed-only atomic scalar loads.
 * This remains strictly a source/Core value-shape gate. */
bool minic_multi_direct_scalar_load_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    bool atomic;
    MinicCoreValueId address;
    size_t pointee_width = 0U;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL || is_pointer == NULL)
        return false;
    atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
    if (ins->kind != MINIC_CORE_INSTRUCTION_LOAD && !atomic)
        return false;
    address = atomic ? ins->value.atomic_load.address : ins->value.load.address;
    *width = direct_type_double(fn, ins->core_type_id, ins->type) ? 8U :
             direct_type_float(fn, ins->core_type_id, ins->type) ? 4U : 0U;
    *is_signed = false;
    *is_pointer = direct_type_pointer(fn, ins->core_type_id, ins->type);
    if (atomic &&
        (!core_direct_relaxed_atomic_pointee_width(
             program, target, fn, address,
             ins->value.atomic_load.memory_order, &pointee_width) ||
         (*is_pointer ? pointee_width != 8U :
            !direct_type_integer_width(program, target, fn,
                                       ins->core_type_id, ins->type,
                                       width, is_signed) ||
            (*width != 1U && *width != 2U && *width != 4U &&
             *width != 8U) ||
            pointee_width != *width)))
        return false;
    if (address >= fn->value_count ||
        !direct_type_pointer(fn, fn->values[address].core_type_id,
                             fn->values[address].type) ||
        (!*is_pointer && !direct_type_double(fn, ins->core_type_id, ins->type) &&
         !direct_type_float(fn, ins->core_type_id, ins->type) &&
         !direct_type_integer_width(program, target, fn,
                                    ins->core_type_id, ins->type,
                                    width, is_signed)))
        return false;
    return true;
}

/* One frame/source admissibility check for the two 128-bit load paths.
 * Keep actual 128-bit loads and ISA stack address encodings private. */
bool minic_multi_direct_wide_scalar_load_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan, MinicCoreValueId *address,
    size_t *destination_offset) {
    if (fn == NULL || ins == NULL || frame_plan == NULL ||
        address == NULL || destination_offset == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_LOAD ||
        !(fn->type_context != NULL
            ? direct_core_wide(fn, ins->core_type_id)
            : (minic_type_is_int128_integer(ins->type) ||
               minic_type_is_long_double(ins->type))))
        return false;
    *address = ins->value.load.address;
    return *address < fn->value_count &&
           direct_type_pointer(fn, fn->values[*address].core_type_id,
                               fn->values[*address].type) &&
           minic_lp64_frame_plan_wide_slot(frame_plan, ins->result,
                                            destination_offset);
}

bool minic_multi_direct_scalar_store_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    bool atomic;
    MinicCoreValueId addr, value;
    size_t pointee_width = 0U;
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
        !direct_type_pointer(fn, fn->values[addr].core_type_id,
                             fn->values[addr].type))
        return false;
    *width = 0U;
    *is_signed = false;
    *is_pointer = direct_type_pointer(fn, fn->values[value].core_type_id,
                                      fn->values[value].type);
    /* These are the existing *relaxed-only*, scalar, naturally-aligned
     * admission rules. This is not a general atomic memory model. */
    if (atomic &&
        !core_direct_relaxed_atomic_pointee_width(
             program, target, fn, addr,
             ins->value.atomic_store.memory_order, &pointee_width))
        return false;
    if (!*is_pointer && !direct_type_double(fn, fn->values[value].core_type_id,
                                             fn->values[value].type) &&
        !direct_type_float(fn, fn->values[value].core_type_id,
                           fn->values[value].type) &&
        !direct_type_integer_width(program, target, fn,
                                   fn->values[value].core_type_id,
                                   fn->values[value].type, width, is_signed))
        return false;
    if (direct_type_double(fn, fn->values[value].core_type_id,
                           fn->values[value].type))
        *width = 8U;
    if (direct_type_float(fn, fn->values[value].core_type_id,
                          fn->values[value].type))
        *width = 4U;
    if (atomic &&
        (direct_type_double(fn, fn->values[value].core_type_id,
                            fn->values[value].type) ||
         direct_type_float(fn, fn->values[value].core_type_id,
                           fn->values[value].type) ||
         (*is_pointer ? pointee_width != 8U :
          (*width != 1U && *width != 2U &&
           *width != 4U && *width != 8U) ||
          *width != pointee_width)))
        return false;
    return true;
}

/* One dispatch classifier and SSA-result guard for both LP64 direct
 * backends. Effect-only instructions deliberately have no scalar slots. */
bool minic_multi_direct_machine_instruction_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool allow_variadic_double_result,
    MinicDirectMachineInstructionShape *shape) {
    if (fn == NULL || ins == NULL || shape == NULL)
        return false;
    shape->result_offset = 0U;
    if (ins->kind == MINIC_CORE_INSTRUCTION_COMPILER_BARRIER) {
        if (!minic_multi_direct_compiler_barrier_shape(fn, ins))
            return false;
        shape->family = MINIC_DIRECT_MACHINE_COMPILER_BARRIER;
        return true;
    }
    /* Structured GNU asm is already a Core effect, never an SSA value.
     * Native target emission remains independently qualified and fail-closed. */
    if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !direct_type_void(fn, ins->core_type_id, ins->type) ||
            ins->value.structured_inline_asm.inline_asm_id >= fn->inline_asm_count)
            return false;
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
        return true;
    }
    /* A zero-operand opaque hardware effect has no SSA result; the target
     * must independently requalify the full exact template at emission. */
    if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !direct_type_void(fn, ins->core_type_id, ins->type) ||
            ins->value.inline_asm_id >= fn->inline_asm_count)
            return false;
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE) {
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !direct_type_void(fn, ins->core_type_id, ins->type) ||
            ins->value.fixed_register_write.binding_id >=
                fn->fixed_register_binding_count ||
            ins->value.fixed_register_write.source >= fn->value_count)
            return false;
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_COPY) {
        const MinicCoreValueId dst = ins->value.variadic_copy.destination;
        const MinicCoreValueId src = ins->value.variadic_copy.source;
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !direct_type_void(fn, ins->core_type_id, ins->type) ||
            dst >= fn->value_count ||
            src >= fn->value_count ||
            !direct_type_pointer(fn, fn->values[dst].core_type_id,
                                 fn->values[dst].type) ||
            !direct_type_equal(fn, fn->values[dst].core_type_id,
                               fn->values[src].core_type_id,
                               fn->values[dst].type, fn->values[src].type))
            return false;
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD) {
        shape->family = MINIC_DIRECT_MACHINE_RECORD_LOAD;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_COPY) {
        shape->family = MINIC_DIRECT_MACHINE_RECORD_COPY;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
        shape->family = MINIC_DIRECT_MACHINE_PARAMETER_OBJECT;
        return true;
    }
    if (minic_core_instruction_semantic_family(ins->kind) ==
        MINIC_CORE_FAMILY_MEMORY) {
        shape->family = MINIC_DIRECT_MACHINE_MEMORY;
        return (ins->kind != MINIC_CORE_INSTRUCTION_LOAD &&
                ins->kind != MINIC_CORE_INSTRUCTION_ATOMIC_LOAD) ||
               minic_multi_direct_result_slot_offset(
                   fn, ins, allow_variadic_double_result,
                   &shape->result_offset);
    }
    if (!minic_multi_direct_result_slot_offset(
            fn, ins, allow_variadic_double_result, &shape->result_offset))
        return false;
    if (minic_core_instruction_semantic_family(ins->kind) ==
        MINIC_CORE_FAMILY_FLOATING_SCALAR)
        shape->family = MINIC_DIRECT_MACHINE_FP;
    else if (minic_multi_direct_integer_machine_kind(ins->kind))
        shape->family = MINIC_DIRECT_MACHINE_INTEGER;
    else if (minic_multi_direct_abi_transfer_kind(ins->kind))
        shape->family = MINIC_DIRECT_MACHINE_ABI;
    else
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
    return true;
}
