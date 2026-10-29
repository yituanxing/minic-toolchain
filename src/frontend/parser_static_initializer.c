#include "frontend/parser_static_initializer.h"
#include "frontend/parser_static_constants.h"
#include "frontend/declaration_sema.h"
#include "frontend/initializer.h"
#include "frontend/semantic_snapshot.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Own static constants, aggregate slots, record and array initialization. */

static bool
append_static_constant_zero(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type);

static bool append_static_field_zeros(MinicParser *parser,
                                      MinicGlobalObjectId object_id,
                                      const MinicRecordField *field) {
    size_t element_index;

    if (field == NULL || field->element_count == 0U) {
        return false;
    }
    /* Flexible and GNU zero-length array members participate in record layout,
     * but contribute no scalar initializer slot to the fixed object extent. */
    if (field->is_flexible_array || field->is_zero_length_array) {
        return true;
    }
    for (element_index = 0U; element_index < field->element_count; ++element_index) {
        if (!append_static_constant_zero(parser, object_id, field->type)) {
            return false;
        }
    }
    return true;
}

static bool
append_static_constant_zero(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type) ||
        minic_type_is_float(type) || minic_type_is_double(type)) {
        return minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U);
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_index;

        array_type = minic_c0_program_array_type(parser->program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            if (!append_static_constant_zero(parser, object_id, array_type->element_type)) {
                return false;
            }
        }
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(parser->program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type);

static bool static_integer_address_constant_delta(const MinicParser *parser,
                                                  MinicExpressionId expression_id,
                                                  int64_t *delta) {
    MinicConstValue value;

    return parser != NULL && delta != NULL &&
           minic_const_eval_integer(parser->program, parser->target_info, expression_id, &value) &&
           minic_const_value_as_int64(parser->program, parser->target_info, &value, delta);
}

static bool static_integer_address_relocation_target(const MinicParser *parser,
                                                     MinicExpressionId expression_id,
                                                     MinicStaticObjectRelocationTarget *target) {
    const MinicExpression *expression;

    if (parser == NULL || target == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST || expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(parser->program, expression->value.unary.operand);
        if (operand != NULL && minic_type_is_pointer(operand->type)) {
            return minic_parser_static_symbol_object_relocation_path(
                parser, expression->value.unary.operand, target);
        }
        return static_integer_address_relocation_target(
            parser, expression->value.unary.operand, target);
    }
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        MinicConstValue condition;
        bool is_zero;
        MinicExpressionId selected_id;

        if (!minic_const_eval_integer(parser->program,
                                      parser->target_info,
                                      expression->value.conditional.condition,
                                      &condition) ||
            !minic_const_value_is_zero(
                parser->program, parser->target_info, &condition, &is_zero)) {
            return false;
        }
        selected_id = !is_zero && expression->value.conditional.uses_condition_value
                          ? expression->value.conditional.condition
                          : (is_zero ? expression->value.conditional.when_false
                                     : expression->value.conditional.when_true);
        return static_integer_address_relocation_target(parser, selected_id, target);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        MinicStaticObjectRelocationTarget base;
        int64_t delta;

        if (static_integer_address_relocation_target(
                parser, expression->value.binary.left, &base) &&
            static_integer_address_constant_delta(parser, expression->value.binary.right, &delta)) {
            if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) {
                if (delta == INT64_MIN) {
                    return false;
                }
                delta = -delta;
            }
            if (!minic_parser_static_symbol_add_pointer_offset(base.byte_addend, delta, &base.byte_addend)) {
                return false;
            }
            *target = base;
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
            static_integer_address_constant_delta(parser, expression->value.binary.left, &delta) &&
            static_integer_address_relocation_target(
                parser, expression->value.binary.right, &base) &&
            minic_parser_static_symbol_add_pointer_offset(base.byte_addend, delta, &base.byte_addend)) {
            *target = base;
            return true;
        }
    }
    return false;
}

static bool static_integer_address_slot_supported(const MinicParser *parser, MinicType type) {
    const MinicDataLayout *layout;
    size_t alignment;
    size_t size;

    layout = parser == NULL ? NULL : minic_target_info_data_layout(parser->target_info);
    return layout != NULL && minic_type_is_integer(type) &&
           minic_data_layout_type(layout, parser->program, type, &size, &alignment) &&
           size == layout->pointer_size;
}

/* A symbolic integer static initializer is a link-time address, not an
 * arithmetic constant. Decode it once for both scalar objects and aggregate
 * slots, retaining the target symbol, member path and signed byte addend. */
static bool parse_static_integer_address_value(MinicParser *parser,
                                               MinicType type,
                                               MinicStaticIntegerAddressValue *value) {
    MinicConstValue constant;
    MinicConstValue converted;
    MinicExpressionId expression_id;

    if (parser == NULL || value == NULL || !minic_type_is_integer(type) ||
        !minic_parser_parse_expression(parser, &expression_id, 0U) ||
        !minic_c0_assignment_compatible(parser->program, type, expression_id)) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0')
            minic_parser_error(parser, "static integer initializer type mismatch");
        return false;
    }
    (void)memset(value, 0, sizeof(*value));
    value->function_id = MINIC_FUNCTION_INVALID;
    if (minic_const_eval_integer(parser->program, parser->target_info,
                                 expression_id, &constant) &&
        minic_const_value_convert_integer(parser->program, parser->target_info,
                                          &constant, type, &converted)) {
        value->bits = converted.bits;
        return true;
    }
    if (minic_const_eval_arithmetic_to_integer(parser->program, parser->target_info,
                                                expression_id, type, &converted)) {
        value->bits = converted.bits;
        return true;
    }
    if (static_integer_address_slot_supported(parser, type)) {
        if (minic_parser_static_symbol_function_relocation(
                parser->program, expression_id, &value->function_id)) {
            value->has_relocation = true;
            value->is_function = true;
            return true;
        }
        if (static_integer_address_relocation_target(parser, expression_id,
                                                      &value->object_target)) {
            value->has_relocation = true;
            return true;
        }
    }
    minic_parser_error(parser, "integer initializer requires an arithmetic constant or "
                               "pointer-sized symbolic address");
    return false;
}

static bool record_static_integer_address_relocation(
    MinicParser *parser, MinicGlobalObjectId object_id,
    MinicGlobalRelocationLocationKind location_kind, size_t location_index,
    const MinicStaticIntegerAddressValue *value) {
    const MinicStaticObjectRelocationTarget *target;

    if (!value->has_relocation) return true;
    if (value->is_function)
        return minic_c0_global_object_add_function_relocation(
            parser->program, object_id, location_kind, location_index, value->function_id);
    target = &value->object_target;
    return minic_c0_global_object_add_integer_object_relocation_path_addend(
        parser->program, object_id, location_kind, location_index,
        target->object_id, target->member_indices, target->member_depth,
        target->byte_addend);
}

bool minic_parser_parse_static_integer_object_initializer(
    MinicParser *parser, MinicGlobalObjectId object_id, MinicType type) {
    MinicStaticIntegerAddressValue value;

    if (parser == NULL || object_id >= parser->program->global_object_count ||
        !parse_static_integer_address_value(parser, type, &value) ||
        (value.has_relocation
             ? (!record_static_integer_address_relocation(
                    parser, object_id, MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR, 0U, &value) ||
                !minic_c0_global_object_set_zero_initialized(parser->program, object_id))
             : !minic_c0_global_object_add_initializer_bits(
                   parser->program, object_id, value.bits))) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0')
            minic_parser_error(parser, "cannot record static symbolic integer initializer");
        return false;
    }
    return true;
}

static bool parse_static_scalar_constant_at(MinicParser *parser,
                                            MinicGlobalObjectId object_id,
                                            MinicType type,
                                            bool overwrite,
                                            size_t overwrite_slot) {
    bool braced;

    if (parser == NULL || object_id >= parser->program->global_object_count) {
        return false;
    }
    braced = parser->current.kind == MINIC_TOKEN_LBRACE;
    if (braced && !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(type)) {
        MinicStaticIntegerAddressValue value;

        if (!parse_static_integer_address_value(parser, type, &value)) {
            return false;
        }
        if (overwrite) {
            if (!minic_c0_global_object_replace_aggregate_initializer_bits(
                    parser->program, object_id, overwrite_slot, value.bits)) {
                minic_parser_error(parser, "cannot replace backward static scalar initializer");
                return false;
            }
        } else if (!minic_c0_global_object_add_initializer_bits(
                       parser->program, object_id, value.bits)) {
            minic_parser_error(parser, "cannot record static aggregate integer initializer");
            return false;
        }
        if (value.has_relocation) {
            size_t relocation_slot;

            relocation_slot =
                overwrite ? overwrite_slot
                          : parser->program->global_objects[object_id].initializer_count - 1U;
            if (!record_static_integer_address_relocation(
                    parser, object_id, MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                    relocation_slot, &value)) {
                minic_parser_error(parser, "cannot record static symbolic integer relocation");
                return false;
            }
        }
    } else if (minic_type_is_float(type) || minic_type_is_double(type)) {
        uint64_t parsed_bits;

        if (!minic_parser_static_symbol_floating_initializer_bits(parser, type, &parsed_bits)) {
            return false;
        }
        if (overwrite) {
            if (!minic_c0_global_object_replace_aggregate_initializer_bits(
                    parser->program, object_id, overwrite_slot, parsed_bits)) {
                minic_parser_error(parser, "cannot replace backward static floating initializer");
                return false;
            }
        } else if (!minic_c0_global_object_add_initializer_bits(
                       parser->program, object_id, parsed_bits)) {
            minic_parser_error(parser, "cannot record static aggregate floating initializer");
            return false;
        }
    } else if (minic_type_is_pointer(type)) {
        MinicStaticPointerInitializer initializer;
        size_t slot_index;

        slot_index = overwrite ? overwrite_slot
                               : parser->program->global_objects[object_id].initializer_count;
        if (!minic_parser_static_symbol_pointer_initializer(parser, type, &initializer)) {
            return false;
        }
        if (initializer.has_relocation) {
            bool recorded;

            if (overwrite &&
                !minic_c0_global_object_replace_aggregate_initializer_bits(
                    parser->program, object_id, overwrite_slot, 0U)) {
                minic_parser_error(parser, "cannot replace backward static relocation slot");
                return false;
            }
            if (!overwrite &&
                !minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U)) {
                minic_parser_error(parser, "cannot reserve nested static relocation slot");
                return false;
            }
            recorded = minic_parser_static_commit_pointer_relocation(
                parser->program, object_id,
                MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                slot_index, &initializer);
            if (!recorded) {
                const char *trace = getenv("CORE_FAST_TRACE");
                if (trace != NULL && trace[0] != '\0' && strcmp(trace, "0") != 0) {
                    const MinicGlobalObject *owner =
                        minic_c0_program_global_object(parser->program, object_id);
                    const MinicGlobalObject *target_object =
                        !initializer.relocation_is_label && !initializer.relocation_is_function
                            ? minic_c0_program_global_object(
                                  parser->program, initializer.relocation_target.object_id)
                            : NULL;
                    MinicType slot_type;
                    bool has_slot_type =
                        owner != NULL &&
                        minic_c0_global_relocation_slot_type(
                            parser->program,
                            owner,
                            MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                            slot_index,
                            &slot_type);
                    (void)fprintf(stderr,
                                  "STATIC_RELOC_DETAIL owner=%.*s slot=%zu owner_init=%zu "
                                  "owner_reloc=%zu slot_type_ok=%d slot_base=%d slot_ptr=%u "
                                  "target=%.*s target_base=%d target_ptr=%u target_depth=%zu "
                                  "addend=%" PRId64 " cast=%d function=%d label=%d\n",
                                  owner != NULL ? (int)owner->name_length : 0,
                                  owner != NULL ? owner->name : "",
                                  slot_index,
                                  owner != NULL ? owner->initializer_count : 0U,
                                  owner != NULL ? owner->relocation_count : 0U,
                                  has_slot_type ? 1 : 0,
                                  has_slot_type ? (int)slot_type.base_kind : -1,
                                  has_slot_type ? slot_type.pointer_depth : 0U,
                                  target_object != NULL ? (int)target_object->name_length : 0,
                                  target_object != NULL ? target_object->name : "",
                                  target_object != NULL ? (int)target_object->type.base_kind : -1,
                                  target_object != NULL ? target_object->type.pointer_depth : 0U,
                                  initializer.relocation_target.member_depth,
                                  initializer.relocation_target.byte_addend,
                                  initializer.has_explicit_pointer_cast ? 1 : 0,
                                  initializer.relocation_is_function ? 1 : 0,
                                  initializer.relocation_is_label ? 1 : 0);
                }
                minic_parser_error(parser, "cannot record nested static symbolic relocation");
                return false;
            }
        } else if (overwrite) {
            if (!minic_c0_global_object_replace_aggregate_initializer_bits(
                    parser->program, object_id, overwrite_slot, initializer.bits)) {
                minic_parser_error(parser, "cannot replace backward static pointer initializer");
                return false;
            }
        } else if (!minic_c0_global_object_add_initializer_bits(
                       parser->program, object_id, initializer.bits)) {
            minic_parser_error(parser, "cannot record static pointer constant bits");
            return false;
        }
    } else {
        return false;
    }
    if (!braced) {
        return true;
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after scalar initializer");
}

static bool
parse_static_scalar_constant(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type) {
    return parse_static_scalar_constant_at(parser, object_id, type, false, 0U);
}

static bool grow_static_array_slots(MinicParser *parser,
                                    MinicStaticArraySlot **slots,
                                    size_t *capacity,
                                    size_t required) {
    MinicStaticArraySlot *resized;
    size_t old_capacity;
    size_t new_capacity;

    if (parser == NULL || slots == NULL || capacity == NULL) {
        return false;
    }
    if (required <= *capacity) {
        return true;
    }
    old_capacity = *capacity;
    new_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2U;
    }
    if (new_capacity < required || new_capacity > SIZE_MAX / sizeof(**slots)) {
        minic_parser_error(parser, "static array initializer slot count overflows");
        return false;
    }
    resized = (MinicStaticArraySlot *)realloc(*slots, new_capacity * sizeof(**slots));
    if (resized == NULL) {
        minic_parser_error(parser, "out of memory while planning static array initializer");
        return false;
    }
    (void)memset(resized + old_capacity, 0, (new_capacity - old_capacity) * sizeof(*resized));
    *slots = resized;
    *capacity = new_capacity;
    return true;
}

static bool parse_static_array_scalar_slot(MinicParser *parser,
                                           MinicType element_type,
                                           MinicStaticArraySlot *slot) {
    bool braced;

    if (parser == NULL || slot == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type))) {
        return false;
    }
    (void)memset(slot, 0, sizeof(*slot));
    braced = parser->current.kind == MINIC_TOKEN_LBRACE;
    if (braced && !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(element_type)) {
        if (!parse_static_integer_address_value(
                parser, element_type, &slot->integer_initializer)) {
            return false;
        }
    } else if (!minic_parser_static_symbol_pointer_initializer(
                   parser, element_type, &slot->pointer_initializer)) {
        return false;
    }
    if (!braced) {
        return true;
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after scalar initializer");
}

static bool
materialize_static_pointer_array_slot(MinicParser *parser,
                                      MinicGlobalObjectId object_id,
                                      size_t slot_index,
                                      const MinicStaticPointerInitializer *initializer) {
    bool recorded;

    if (parser == NULL || initializer == NULL ||
        !minic_c0_global_object_add_initializer_bits(
            parser->program, object_id, initializer->has_relocation ? 0U : initializer->bits)) {
        return false;
    }
    if (!initializer->has_relocation) {
        return true;
    }
    recorded = minic_parser_static_commit_pointer_relocation(
        parser->program, object_id,
        MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
        slot_index, initializer);
    if (!recorded) {
        MinicType slot_type;

        if (!minic_c0_global_relocation_slot_type(parser->program,
                                                  &parser->program->global_objects[object_id],
                                                  MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                                  slot_index,
                                                  &slot_type)) {
            minic_parser_error(parser, "static pointer array relocation slot type is unavailable");
            return false;
        }
        if (initializer->relocation_is_function &&
            !minic_c0_global_relocation_function_target_compatible(
                parser->program,
                slot_type,
                initializer->function_id,
                initializer->has_explicit_pointer_cast)) {
            minic_parser_error(parser, "static pointer array function target type mismatch");
            return false;
        }
        if (!initializer->relocation_is_label && !initializer->relocation_is_function) {
            MinicGlobalRelocation probe;
            size_t depth;

            (void)memset(&probe, 0, sizeof(probe));
            probe.target_kind = MINIC_GLOBAL_RELOCATION_OBJECT;
            probe.target_id = initializer->relocation_target.object_id;
            probe.target_member_depth = initializer->relocation_target.member_depth;
            probe.target_byte_addend = initializer->relocation_target.byte_addend;
            probe.has_explicit_pointer_cast = initializer->has_explicit_pointer_cast;
            for (depth = 0U; depth < probe.target_member_depth; ++depth) {
                probe.target_member_indices[depth] =
                    initializer->relocation_target.member_indices[depth];
            }
            if (!minic_c0_global_relocation_object_target_compatible(
                    parser->program, &probe, slot_type)) {
                minic_parser_error(parser, "static pointer array object target type mismatch");
                return false;
            }
        }
        minic_parser_error(parser, "static pointer array relocation commit rejected");
        return false;
    }
    return true;
}

/* Stream the resolved owner directly into GlobalObject storage.
 * The old implementation allocated and copied a full array of large
 * MinicStaticArraySlot records before serializing them a second time.
 * Only the source action payloads and the compact owner index are needed. */
static bool materialize_static_array_slots(MinicParser *parser,
                                           MinicGlobalObjectId object_id,
                                           MinicType element_type,
                                           const MinicStaticArraySlot *action_values,
                                           const MinicArrayInitializerResolved *resolved,
                                           size_t aggregate_slot_base) {
    const MinicStaticArraySlot zero_slot = {0};
    size_t index;

    if (parser == NULL || resolved == NULL ||
        (resolved->element_count != 0U && resolved->owners == NULL) ||
        (resolved->action_count != 0U && action_values == NULL) ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)))
        return false;
    for (index = 0U; index < resolved->element_count; ++index) {
        const size_t owner = resolved->owners[index];
        const MinicStaticArraySlot *slot;
        if (owner == MINIC_INITIALIZER_ACTION_INVALID) {
            slot = &zero_slot;
        } else {
            if (owner >= resolved->action_count)
                return false;
            slot = &action_values[owner];
        }
        if (minic_type_is_integer(element_type)) {
            size_t aggregate_slot;

            if (aggregate_slot_base > SIZE_MAX - index) {
                minic_parser_error(parser, "static integer array aggregate slot overflows");
                return false;
            }
            aggregate_slot = aggregate_slot_base + index;
            if (!minic_c0_global_object_add_initializer_bits(
                    parser->program, object_id, slot->integer_initializer.bits) ||
                !record_static_integer_address_relocation(
                    parser, object_id, MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                    aggregate_slot, &slot->integer_initializer)) {
                minic_parser_error(parser, "cannot materialize static integer array slot");
                return false;
            }
        } else {
            size_t aggregate_slot;

            if (aggregate_slot_base > SIZE_MAX - index) {
                minic_parser_error(parser, "static pointer array aggregate slot overflows");
                return false;
            }
            aggregate_slot = aggregate_slot_base + index;
            if (!materialize_static_pointer_array_slot(
                    parser, object_id, aggregate_slot, &slot->pointer_initializer)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot materialize static pointer array slot");
                }
                return false;
            }
        }
    }
    return true;
}

bool minic_parser_static_string_element_compatible(MinicParser *parser,
                                                   MinicType element_type,
                                                   MinicTokenKind literal_kind) {
    MinicType expected_type;
    MinicType unqualified_type;

    if (parser == NULL || !minic_type_unqualified(element_type, &unqualified_type)) {
        return false;
    }
    if (literal_kind == MINIC_TOKEN_STRING_LITERAL) {
        return minic_type_is_char_integer(unqualified_type);
    }
    return literal_kind == MINIC_TOKEN_WIDE_STRING_LITERAL &&
           minic_target_info_wide_character_type(parser->target_info, &expected_type) &&
           minic_type_equal(unqualified_type, expected_type);
}

bool minic_parser_static_scalar_array_transaction(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  MinicType element_type,
                                                  size_t element_count,
                                                  bool infer_bound) {
    MinicArrayInitializerPlan plan;
    MinicArrayInitializerResolved resolved = {0};
    MinicStaticArraySlot *action_values;
    const MinicGlobalObject *object;
    size_t action_capacity;
    size_t extent;
    bool success;

    action_values = NULL;
    action_capacity = 0U;
    extent = 0U;
    success = false;
    if (parser != NULL && !infer_bound && element_count != 0U &&
        minic_parser_static_string_element_compatible(
            parser, element_type, parser->current.kind)) {
        return minic_parser_add_bounded_string_literal_initializer(
            parser, object_id, element_count);
    }
    if (parser != NULL && minic_type_is_char_integer(element_type) &&
        parser->current.kind == MINIC_TOKEN_LBRACE) {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (minic_parser_static_string_element_compatible(
                &probe, element_type, probe.current.kind)) {
            size_t string_count;

            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (infer_bound) {
                const MinicGlobalObject *string_object;

                if (!minic_parser_add_string_literal_initializer(
                        parser, object_id, &string_count)) {
                    return false;
                }
                string_object = minic_c0_program_global_object(parser->program, object_id);
                if (string_object == NULL || !minic_type_is_array(string_object->type) ||
                    !minic_c0_program_complete_array_type(
                        parser->program, string_object->type, string_count)) {
                    minic_parser_error(parser, "cannot infer braced string array extent");
                    return false;
                }
            } else if (!minic_parser_add_bounded_string_literal_initializer(
                           parser, object_id, element_count)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_COMMA &&
                !minic_parser_advance(parser)) {
                return false;
            }
            return minic_parser_expect(
                parser, MINIC_TOKEN_RBRACE, "expected '}' after braced string initializer");
        }
    }

    minic_array_initializer_plan_initialize(&plan, element_count, infer_bound);
    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) ||
        (!infer_bound && element_count == 0U) || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid static scalar array initializer");
        }
        goto done;
    }
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t action_id;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (!minic_parser_parse_array_designator(
                    parser, element_count, infer_bound, &first, &last) ||
                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "static array designator extent overflows");
                }
                goto done;
            }
        } else if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
            minic_parser_error(parser, "too many nested static array initializers");
            goto done;
        }
        if (!grow_static_array_slots(parser, &action_values, &action_capacity, action_id + 1U) ||
            !parse_static_array_scalar_slot(parser, element_type, &action_values[action_id])) {
            goto done;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static array initializer");
            goto done;
        }
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after static array initializer")) {
        goto done;
    }
    extent = minic_array_initializer_plan_element_count(&plan);
    if (infer_bound) {
        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL || !minic_type_is_array(object->type) ||
            (extent == 0U
                 ? !minic_c0_program_complete_zero_length_array_type(
                       parser->program, object->type)
                 : !minic_c0_program_complete_array_type(
                       parser->program, object->type, extent))) {
            minic_parser_error(parser, "cannot complete inferred static array type");
            goto done;
        }
    }
    if (!minic_array_initializer_plan_resolve(&plan, &resolved)) {
        minic_parser_error(parser, "cannot resolve static array initializer owners");
        goto done;
    }
    object = minic_c0_program_global_object(parser->program, object_id);
    if (object == NULL ||
        resolved.element_count != extent ||
        !materialize_static_array_slots(parser, object_id, element_type,
                                        action_values, &resolved,
                                        object->initializer_count)) {
        goto done;
    }
    success = true;

done:
    free(action_values);
    minic_array_initializer_resolved_destroy(&resolved);
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

typedef struct MinicStaticAggregateArrayAction {
    uint64_t *values;
    size_t value_count;
    MinicGlobalRelocation *relocations;
    size_t relocation_count;
    MinicGlobalUnionSelection *union_selections;
    size_t union_selection_count;
} MinicStaticAggregateArrayAction;

static void destroy_static_aggregate_array_actions(MinicStaticAggregateArrayAction *actions,
                                                   size_t action_count) {
    size_t index;

    if (actions == NULL) {
        return;
    }
    for (index = 0U; index < action_count; ++index) {
        free(actions[index].values);
        free(actions[index].relocations);
        free(actions[index].union_selections);
    }
    free(actions);
}

static bool grow_static_aggregate_array_actions(MinicParser *parser,
                                                MinicStaticAggregateArrayAction **actions,
                                                size_t *capacity,
                                                size_t required) {
    MinicStaticAggregateArrayAction *resized;
    size_t old_capacity;
    size_t new_capacity;

    if (parser == NULL || actions == NULL || capacity == NULL) {
        return false;
    }
    if (required <= *capacity) {
        return true;
    }
    old_capacity = *capacity;
    new_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2U) {
            minic_parser_error(parser, "static aggregate array action capacity overflows");
            return false;
        }
        new_capacity *= 2U;
    }
    if (new_capacity > SIZE_MAX / sizeof(**actions)) {
        minic_parser_error(parser, "static aggregate array action capacity overflows");
        return false;
    }
    resized = (MinicStaticAggregateArrayAction *)realloc(*actions, new_capacity * sizeof(*resized));
    if (resized == NULL) {
        minic_parser_error(parser, "out of memory while growing static aggregate array actions");
        return false;
    }
    (void)memset(resized + old_capacity, 0, (new_capacity - old_capacity) * sizeof(*resized));
    *actions = resized;
    *capacity = new_capacity;
    return true;
}

static bool capture_static_aggregate_array_action(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  size_t initializer_begin,
                                                  size_t relocation_begin,
                                                  size_t union_selection_begin,
                                                  MinicStaticAggregateArrayAction *action) {
    MinicGlobalObject *object;
    size_t value_count;
    size_t relocation_count;
    size_t union_selection_count;
    size_t index;

    if (parser == NULL || action == NULL || object_id >= parser->program->global_object_count) {
        return false;
    }
    object = &parser->program->global_objects[object_id];
    if (initializer_begin > object->initializer_count ||
        relocation_begin > object->relocation_count ||
        union_selection_begin > object->union_selection_count) {
        return false;
    }
    value_count = object->initializer_count - initializer_begin;
    relocation_count = object->relocation_count - relocation_begin;
    union_selection_count = object->union_selection_count - union_selection_begin;
    if (value_count == 0U || value_count > SIZE_MAX / sizeof(*action->values) ||
        relocation_count > SIZE_MAX / sizeof(*action->relocations) ||
        union_selection_count > SIZE_MAX / sizeof(*action->union_selections)) {
        minic_parser_error(parser, "invalid static aggregate array action payload");
        return false;
    }

    action->values = (uint64_t *)malloc(value_count * sizeof(*action->values));
    action->relocations =
        relocation_count == 0U
            ? NULL
            : (MinicGlobalRelocation *)malloc(relocation_count * sizeof(*action->relocations));
    action->union_selections = union_selection_count == 0U
                                   ? NULL
                                   : (MinicGlobalUnionSelection *)malloc(
                                         union_selection_count * sizeof(*action->union_selections));
    if (action->values == NULL || (relocation_count != 0U && action->relocations == NULL) ||
        (union_selection_count != 0U && action->union_selections == NULL)) {
        free(action->values);
        free(action->relocations);
        free(action->union_selections);
        action->values = NULL;
        action->relocations = NULL;
        action->union_selections = NULL;
        minic_parser_error(parser, "out of memory while capturing static aggregate array action");
        return false;
    }

    (void)memcpy(action->values,
                 object->initializer_values + initializer_begin,
                 value_count * sizeof(*action->values));
    if (relocation_count != 0U) {
        (void)memcpy(action->relocations,
                     object->relocations + relocation_begin,
                     relocation_count * sizeof(*action->relocations));
    }
    if (union_selection_count != 0U) {
        (void)memcpy(action->union_selections,
                     object->union_selections + union_selection_begin,
                     union_selection_count * sizeof(*action->union_selections));
    }
    for (index = 0U; index < relocation_count; ++index) {
        if (action->relocations[index].location_index < initializer_begin) {
            minic_parser_error(parser,
                               "static aggregate array relocation precedes captured action");
            return false;
        }
        action->relocations[index].location_index -= initializer_begin;
    }
    for (index = 0U; index < union_selection_count; ++index) {
        if (action->union_selections[index].initializer_slot < initializer_begin) {
            minic_parser_error(parser,
                               "static aggregate array union selection precedes captured action");
            return false;
        }
        action->union_selections[index].initializer_slot -= initializer_begin;
    }

    action->value_count = value_count;
    action->relocation_count = relocation_count;
    action->union_selection_count = union_selection_count;
    object->initializer_count = initializer_begin;
    object->relocation_count = relocation_begin;
    object->union_selection_count = union_selection_begin;
    return true;
}

static bool
materialize_static_aggregate_array_action(MinicParser *parser,
                                          MinicGlobalObjectId object_id,
                                          const MinicStaticAggregateArrayAction *action) {
    MinicGlobalObject *object;
    size_t destination_begin;
    size_t index;

    if (parser == NULL || action == NULL || object_id >= parser->program->global_object_count) {
        return false;
    }
    object = &parser->program->global_objects[object_id];
    destination_begin = object->initializer_count;
    for (index = 0U; index < action->value_count; ++index) {
        if (!minic_c0_global_object_add_initializer_bits(
                parser->program, object_id, action->values[index])) {
            minic_parser_error(parser, "cannot materialize static aggregate array slots");
            return false;
        }
    }
    for (index = 0U; index < action->union_selection_count; ++index) {
        const MinicGlobalUnionSelection *selection;
        size_t initializer_slot;

        selection = &action->union_selections[index];
        if (selection->initializer_slot > SIZE_MAX - destination_begin) {
            minic_parser_error(parser, "static aggregate array union selection index overflows");
            return false;
        }
        initializer_slot = destination_begin + selection->initializer_slot;
        if (!minic_c0_global_object_select_union_member_with_span(parser->program,
                                                                  object_id,
                                                                  initializer_slot,
                                                                  selection->record_id,
                                                                  selection->field_index,
                                                                  selection->initializer_span)) {
            minic_parser_error(parser, "cannot materialize static aggregate array union selection");
            return false;
        }
    }
    for (index = 0U; index < action->relocation_count; ++index) {
        const MinicGlobalRelocation *relocation;
        size_t location_index;
        bool recorded;

        relocation = &action->relocations[index];
        if (relocation->location_index > SIZE_MAX - destination_begin) {
            minic_parser_error(parser, "static aggregate array relocation index overflows");
            return false;
        }
        location_index = destination_begin + relocation->location_index;
        {
            MinicGlobalRelocation rebased = *relocation;
            rebased.location_kind = MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR;
            rebased.location_index = location_index;
            recorded = minic_c0_global_object_add_relocation(
                parser->program, object_id, &rebased);
        }
        if (!recorded) {
            minic_parser_error(parser,
                               "cannot materialize static aggregate array relocation "
                               "(captured-location=%u target=%u relative-slot=%zu "
                               "destination-slot=%zu)",
                               (unsigned int)relocation->location_kind,
                               (unsigned int)relocation->target_kind,
                               relocation->location_index,
                               location_index);
            return false;
        }
    }
    return true;
}

static bool append_static_chained_array_designator_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType array_type) {
    const MinicArrayType *array;
    size_t first;
    size_t last;
    size_t index;

    if (parser == NULL || !minic_type_is_array(array_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    array = minic_c0_program_array_type(parser->program, array_type.array_type_id);
    if (array == NULL || array->element_count == 0U || array->is_zero_length ||
        !minic_parser_parse_array_designator_component(
            parser, array->element_count, false, &first, &last)) {
        return false;
    }
    if (first != last) {
        minic_parser_error(
            parser, "GNU range designators inside chained static arrays are not supported yet");
        return false;
    }
    for (index = 0U; index < first; ++index) {
        if (!append_static_constant_zero(parser, object_id, array->element_type)) {
            minic_parser_error(parser, "cannot zero-fill chained static array prefix");
            return false;
        }
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (!minic_type_is_array(array->element_type) ||
            !append_static_chained_array_designator_value(parser, object_id, array->element_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "chained array designator requires another array dimension");
            }
            return false;
        }
    } else {
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_EQUAL, "expected '=' after chained array designator") ||
            !minic_parser_parse_static_storage_initializer_value(
                parser, object_id, array->element_type)) {
            return false;
        }
    }
    for (index = first + 1U; index < array->element_count; ++index) {
        if (!append_static_constant_zero(parser, object_id, array->element_type)) {
            minic_parser_error(parser, "cannot zero-fill chained static array suffix");
            return false;
        }
    }
    return true;
}

static bool parse_static_forward_array_initializer(MinicParser *parser,
                                                   MinicGlobalObjectId object_id,
                                                   MinicType element_type,
                                                   size_t element_count,
                                                   bool infer_bound,
                                                   size_t *parsed_extent) {
    MinicArrayInitializerPlan plan;
    MinicArrayInitializerResolved resolved = {0};
    MinicStaticAggregateArrayAction *actions;
    size_t action_capacity;
    size_t action_count;
    size_t extent;
    size_t index;
    bool success;

    actions = NULL;
    action_capacity = 0U;
    action_count = 0U;
    extent = 0U;
    success = false;
    minic_array_initializer_plan_initialize(&plan, element_count, infer_bound);
    if (parser == NULL || object_id >= parser->program->global_object_count ||
        (!infer_bound && element_count == 0U) || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid static aggregate array initializer");
        }
        goto done;
    }
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t action_id;
        const MinicGlobalObject *object;
        size_t initializer_begin;
        size_t relocation_begin;
        size_t union_selection_begin;
        bool chained_designator;

        chained_designator = false;
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (!minic_parser_parse_array_designator_component(
                    parser, element_count, infer_bound, &first, &last) ||
                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "static aggregate array designator extent overflows");
                }
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
                if (first != last) {
                    minic_parser_error(parser, "outer GNU range designator cannot be chained yet");
                    goto done;
                }
                if (!minic_type_is_array(element_type)) {
                    minic_parser_error(parser, "chained array designator exceeds array dimensions");
                    goto done;
                }
                chained_designator = true;
            } else if (!minic_parser_expect(
                           parser, MINIC_TOKEN_EQUAL, "expected '=' after array designator")) {
                goto done;
            }
        } else if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
            minic_parser_error(parser, "too many nested static array initializers");
            goto done;
        }
        if (!grow_static_aggregate_array_actions(
                parser, &actions, &action_capacity, action_id + 1U)) {
            goto done;
        }
        if (action_id >= action_count) {
            action_count = action_id + 1U;
        }
        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL) {
            goto done;
        }
        initializer_begin = object->initializer_count;
        relocation_begin = object->relocation_count;
        union_selection_begin = object->union_selection_count;
        if ((chained_designator
                 ? !append_static_chained_array_designator_value(parser, object_id, element_type)
                 : !minic_parser_parse_static_storage_initializer_value(
                       parser, object_id, element_type)) ||
            !capture_static_aggregate_array_action(parser,
                                                   object_id,
                                                   initializer_begin,
                                                   relocation_begin,
                                                   union_selection_begin,
                                                   &actions[action_id])) {
            goto done;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static array initializer");
            goto done;
        }
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after static array initializer")) {
        goto done;
    }
    extent = minic_array_initializer_plan_element_count(&plan);
    if (!minic_array_initializer_plan_resolve(&plan, &resolved)) {
        minic_parser_error(parser, "cannot resolve static aggregate array initializer owners");
        goto done;
    }
    for (index = 0U; index < extent; ++index) {
        const size_t owner = resolved.owners[index];
        if (owner == MINIC_INITIALIZER_ACTION_INVALID) {
            if (!append_static_constant_zero(parser, object_id, element_type)) {
                minic_parser_error(parser, "cannot zero-fill static aggregate array element");
                goto done;
            }
        } else if (owner >= action_count ||
                   !materialize_static_aggregate_array_action(parser, object_id, &actions[owner])) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot materialize static aggregate array action");
            }
            goto done;
        }
    }
    if (parsed_extent != NULL) {
        *parsed_extent = extent;
    }
    success = true;

done:
    destroy_static_aggregate_array_actions(actions, action_count);
    minic_array_initializer_resolved_destroy(&resolved);
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

static bool parse_static_array_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicArrayType *array_type) {
    MinicType element_type;
    size_t element_count;
    size_t parsed_extent;
    bool infer_bound;

    if (array_type == NULL) {
        minic_parser_error(parser, "invalid static array initializer type");
        return false;
    }
    element_type = array_type->element_type;
    element_count = array_type->element_count;
    infer_bound = element_count == 0U && !array_type->is_zero_length;
    if (element_count == 0U && array_type->is_zero_length) {
        return minic_parser_expect(
                   parser, MINIC_TOKEN_LBRACE, "expected '{' for zero-length array initializer") &&
               minic_parser_expect(
                   parser, MINIC_TOKEN_RBRACE, "zero-length array initializer must be empty");
    }
    if (minic_type_is_integer(element_type) || minic_type_is_pointer(element_type)) {
        return minic_parser_static_scalar_array_transaction(
            parser, object_id, element_type, element_count, infer_bound);
    }
    parsed_extent = 0U;
    if (!parse_static_forward_array_initializer(
            parser, object_id, element_type, element_count, infer_bound, &parsed_extent)) {
        return false;
    }
    if (infer_bound) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL || !minic_type_is_array(object->type) ||
            (parsed_extent == 0U
                 ? !minic_c0_program_complete_zero_length_array_type(
                       parser->program, object->type)
                 : !minic_c0_program_complete_array_type(
                       parser->program, object->type, parsed_extent))) {
            minic_parser_error(parser, "cannot complete inferred static aggregate array type");
            return false;
        }
    }
    return true;
}

typedef struct MinicStaticRecordDesignator {
    size_t field_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t depth;
    bool has_array_index;
    size_t array_index;
} MinicStaticRecordDesignator;

static bool parse_static_record_designator_path(MinicParser *parser,
                                                const MinicRecord *record,
                                                MinicStaticRecordDesignator *designator) {
    const MinicRecord *current_record;

    if (parser == NULL || record == NULL || designator == NULL ||
        parser->current.kind != MINIC_TOKEN_DOT) {
        return false;
    }
    (void)memset(designator, 0, sizeof(*designator));
    current_record = record;
    while (parser->current.kind == MINIC_TOKEN_DOT) {
        MinicRecordFieldPath field_path;
        MinicSourceSpan field_span;
        const MinicRecordField *field;
        size_t path_index;

        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected member name after '.' in initializer");
            return false;
        }
        field_span = parser->current.span;
        if (!minic_parser_find_record_field_path(parser, current_record, field_span, &field_path) ||
            !field_path.found || field_path.ambiguous || field_path.depth == 0U) {
            minic_parser_error(parser,
                               "static record designator requires an unambiguous member path");
            return false;
        }
        if (field_path.depth > MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH - designator->depth) {
            minic_parser_error(parser,
                               "static record designator path exceeds implementation limit");
            return false;
        }
        field = NULL;
        for (path_index = 0U; path_index < field_path.depth; ++path_index) {
            size_t field_index;

            field_index = field_path.field_indices[path_index];
            designator->field_indices[designator->depth++] = field_index;
            field = minic_c0_record_field(current_record, field_index);
            if (field == NULL) {
                return false;
            }
            if (path_index + 1U < field_path.depth) {
                if (field->element_count != 1U || field->is_array || field->is_bit_field ||
                    field->is_flexible_array || !minic_type_is_record(field->type)) {
                    minic_parser_error(
                        parser,
                        "promoted static record designator path requires scalar record members");
                    return false;
                }
                current_record = minic_c0_program_record(parser->program, field->type.record_id);
                if (current_record == NULL || !current_record->is_complete) {
                    minic_parser_error(
                        parser, "static record designator path requires complete record members");
                    return false;
                }
            }
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (field == NULL || !field->is_array || field->element_count == 0U ||
                field->is_bit_field || field->is_flexible_array) {
                minic_parser_error(
                    parser, "array designator after record member requires a fixed array field");
                return false;
            }
            if (!minic_parser_parse_array_designator(
                    parser, field->element_count, false, &first, &last)) {
                return false;
            }
            if (first != last) {
                minic_parser_error(
                    parser, "GNU range designators after record members are not supported yet");
                return false;
            }
            designator->has_array_index = true;
            designator->array_index = first;
            return true;
        }
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            break;
        }
        if (field == NULL || field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_record(field->type)) {
            minic_parser_error(
                parser, "intermediate static record designator member must be a scalar record");
            return false;
        }
        current_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            minic_parser_error(parser,
                               "static record designator path requires complete record members");
            return false;
        }
    }
    return designator->depth != 0U &&
           minic_parser_expect(
               parser, MINIC_TOKEN_EQUAL, "expected '=' after static record designator");
}

static bool static_record_designator_leaf_slot(const MinicC0Program *program,
                                               const MinicRecord *record,
                                               const MinicStaticRecordDesignator *designator,
                                               size_t *slot_index,
                                               const MinicRecordField **leaf_field) {
    const MinicRecord *current_record;
    size_t depth;
    size_t total;

    if (program == NULL || record == NULL || designator == NULL || slot_index == NULL ||
        leaf_field == NULL || designator->depth == 0U) {
        return false;
    }
    current_record = record;
    total = 0U;
    for (depth = 0U; depth < designator->depth; ++depth) {
        const MinicRecordField *field;
        size_t relative;
        size_t field_index;

        field_index = designator->field_indices[depth];
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || field->element_count == 0U || field->is_flexible_array ||
            !minic_c0_global_record_field_initializer_slot(
                program, current_record, field_index, &relative) ||
            total > SIZE_MAX - relative) {
            return false;
        }
        total += relative;
        if (depth + 1U == designator->depth) {
            *slot_index = total;
            *leaf_field = field;
            return true;
        }
        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            return false;
        }
        current_record = minic_c0_program_record(program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            return false;
        }
    }
    return false;
}

static bool overwrite_static_zero_field_value(MinicParser *parser,
                                              MinicGlobalObjectId object_id,
                                              const MinicRecordField *field,
                                              size_t field_base_slot);

static bool try_overwrite_static_zero_noncanonical_union_designator(
    MinicParser *parser,
    MinicGlobalObjectId object_id,
    const MinicRecord *record,
    const MinicStaticRecordDesignator *designator,
    size_t record_base_slot,
    bool *handled) {
    const MinicRecord *current_record;
    const MinicGlobalObject *object;
    size_t depth;
    size_t total;

    if (parser == NULL || record == NULL || designator == NULL || handled == NULL ||
        designator->depth == 0U || object_id >= parser->program->global_object_count) {
        return false;
    }
    *handled = false;
    current_record = record;
    object = &parser->program->global_objects[object_id];
    total = 0U;
    for (depth = 0U; depth < designator->depth; ++depth) {
        const MinicRecordField *field;
        size_t field_index;

        field_index = designator->field_indices[depth];
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array) {
            return true;
        }
        if (current_record->is_union && field_index != 0U) {
            const MinicRecordField *canonical_field;
            MinicRecordId union_record_id;
            size_t canonical_element_slots;
            size_t canonical_slots;
            size_t selected_element_slots;
            size_t selected_slots;
            size_t slot_begin;
            size_t slot_end;
            size_t slot;
            size_t relocation_index;

            *handled = true;
            if (current_record->field_count == 0U ||
                current_record < parser->program->records ||
                current_record >= parser->program->records + parser->program->record_count) {
                minic_parser_error(parser,
                                   "backward noncanonical static union designator requires "
                                   "materialized union storage");
                return false;
            }
            canonical_field = &current_record->fields[0];
            if (canonical_field->element_count == 0U || canonical_field->is_flexible_array ||
                !minic_c0_global_initializer_slot_count(
                    parser->program, canonical_field->type, &canonical_element_slots) ||
                (canonical_element_slots != 0U &&
                 canonical_field->element_count > SIZE_MAX / canonical_element_slots) ||
                !minic_c0_global_initializer_slot_count(
                    parser->program, field->type, &selected_element_slots) ||
                (selected_element_slots != 0U &&
                 field->element_count > SIZE_MAX / selected_element_slots)) {
                minic_parser_error(parser, "cannot resolve static union storage shape");
                return false;
            }
            canonical_slots = canonical_field->element_count * canonical_element_slots;
            selected_slots = field->element_count * selected_element_slots;
            if (record_base_slot > SIZE_MAX - total) {
                return false;
            }
            slot_begin = record_base_slot + total;
            if (slot_begin > object->initializer_count ||
                canonical_slots > object->initializer_count - slot_begin) {
                minic_parser_error(parser, "backward static union storage is not materialized");
                return false;
            }
            /* Repeated promoted designators may initialize multiple leaves of
               the same already-selected anonymous union member, for example
               .min followed by .max. Preserve prior leaf writes when the
               active member already matches this selected field. */
            {
                size_t active_field;
                size_t active_span;

                active_field = SIZE_MAX;
                active_span = 0U;
                if (minic_c0_global_object_union_member_selection(
                        parser->program,
                        object,
                        slot_begin,
                        (MinicRecordId)(current_record - parser->program->records),
                        &active_field) &&
                    active_field == field_index &&
                    minic_c0_global_object_union_member_initializer_span(
                        parser->program,
                        object,
                        slot_begin,
                        (MinicRecordId)(current_record - parser->program->records),
                        &active_span)) {
                    if (active_span == 0U) {
                        MinicRecordId active_record_id;

                        active_record_id =
                            (MinicRecordId)(current_record - parser->program->records);
                        if (slot_begin > object->initializer_count ||
                            selected_slots > object->initializer_count - slot_begin ||
                            !minic_c0_global_object_select_union_member_with_span(
                                parser->program,
                                object_id,
                                slot_begin,
                                active_record_id,
                                field_index,
                                selected_slots)) {
                            minic_parser_error(
                                parser,
                                "cannot upgrade legacy static union initializer span");
                            return false;
                        }
                        active_span = selected_slots;
                        object = &parser->program->global_objects[object_id];
                    }
                    if (active_span < selected_slots) {
                        minic_parser_error(
                            parser,
                            "active static union member span is smaller than selected member");
                        return false;
                    }
                    if (depth + 1U == designator->depth) {
                        return overwrite_static_zero_field_value(
                            parser, object_id, field, slot_begin);
                    }
                    {
                        MinicStaticRecordDesignator suffix;
                        const MinicRecord *selected_record;
                        const MinicRecordField *leaf_field;
                        size_t relative_slot;
                        size_t suffix_index;

                        if (!minic_type_is_record(field->type)) {
                            minic_parser_error(
                                parser,
                                "repeated promoted union designator requires a record active member");
                            return false;
                        }
                        selected_record =
                            minic_c0_program_record(parser->program, field->type.record_id);
                        if (selected_record == NULL || !selected_record->is_complete) {
                            return false;
                        }
                        (void)memset(&suffix, 0, sizeof(suffix));
                        suffix.depth = designator->depth - (depth + 1U);
                        for (suffix_index = 0U; suffix_index < suffix.depth; ++suffix_index) {
                            suffix.field_indices[suffix_index] =
                                designator->field_indices[depth + 1U + suffix_index];
                        }
                        if (!static_record_designator_leaf_slot(parser->program,
                                                                selected_record,
                                                                &suffix,
                                                                &relative_slot,
                                                                &leaf_field) ||
                            relative_slot >= active_span ||
                            slot_begin > SIZE_MAX - relative_slot ||
                            !overwrite_static_zero_field_value(
                                parser, object_id, leaf_field, slot_begin + relative_slot)) {
                            if (parser->diagnostic != NULL &&
                                parser->diagnostic->message[0] == '\0') {
                                minic_parser_error(
                                    parser, "cannot update repeated promoted union designator leaf");
                            }
                            return false;
                        }
                    }
                    return true;
                }
            }
            slot_end = slot_begin + canonical_slots;
            {
                const char *trace = getenv("CORE_FAST_TRACE");
                if (trace != NULL && trace[0] != '\0' && strcmp(trace, "0") != 0) {
                    size_t debug_active;
                    size_t debug_span;
                    bool debug_has_active;
                    bool debug_has_span;

                    debug_active = SIZE_MAX;
                    debug_span = 0U;
                    debug_has_active = minic_c0_global_object_union_member_selection(
                        parser->program,
                        object,
                        slot_begin,
                        (MinicRecordId)(current_record - parser->program->records),
                        &debug_active);
                    debug_has_span = minic_c0_global_object_union_member_initializer_span(
                        parser->program,
                        object,
                        slot_begin,
                        (MinicRecordId)(current_record - parser->program->records),
                        &debug_span);
                    (void)fprintf(stderr,
                                  "STATIC_UNION_REWRITE_DETAIL object=%llu slot=%zu "
                                  "canonical=%zu selected=%zu init_count=%zu field=%zu "
                                  "depth=%zu active_ok=%d active=%zu span_ok=%d span=%zu "
                                  "first_bits=%llu\n",
                                  (unsigned long long)object_id,
                                  slot_begin,
                                  canonical_slots,
                                  selected_slots,
                                  object->initializer_count,
                                  field_index,
                                  designator->depth,
                                  debug_has_active ? 1 : 0,
                                  debug_active,
                                  debug_has_span ? 1 : 0,
                                  debug_span,
                                  slot_begin < object->initializer_count
                                      ? (unsigned long long)object->initializer_values[slot_begin]
                                      : 0ULL);
                }
            }
            for (slot = slot_begin; slot < slot_end; ++slot) {
                if (object->initializer_values[slot] != 0U) {
                    minic_parser_error(
                        parser,
                        "backward static union member can only replace implicit zero storage");
                    return false;
                }
            }
            for (relocation_index = 0U; relocation_index < object->relocation_count;
                 ++relocation_index) {
                const MinicGlobalRelocation *relocation;

                relocation = &object->relocations[relocation_index];
                if (relocation->location_kind ==
                        MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                    relocation->location_index >= slot_begin &&
                    relocation->location_index < slot_end) {
                    minic_parser_error(parser,
                                       "backward static union member cannot overwrite symbolic "
                                       "storage");
                    return false;
                }
            }
            /* A designated active member may require more logical initializer
               slots than the union's canonical first member (for example an
               anonymous { min, max } pair over a scalar first member). Growing
               an already-materialized union is safe without shifting any later
               aggregate slot only when this union is the current logical tail.
               Extend that implicit-zero tail and preserve the expanded span in
               union-selection metadata; non-tail growth remains fail-closed. */
            if (selected_slots > canonical_slots) {
                size_t extra_slots;

                if (slot_end != object->initializer_count) {
                    minic_parser_error(
                        parser,
                        "larger backward static union member requires trailing zero storage");
                    return false;
                }
                extra_slots = selected_slots - canonical_slots;
                while (extra_slots-- > 0U) {
                    if (!minic_c0_global_object_add_initializer(
                            parser->program, object_id, 0)) {
                        minic_parser_error(
                            parser, "cannot extend backward static union zero storage");
                        return false;
                    }
                }
                canonical_slots = selected_slots;
                slot_end = slot_begin + canonical_slots;
                object = &parser->program->global_objects[object_id];
            }
            union_record_id = (MinicRecordId)(current_record - parser->program->records);
            if (!minic_c0_global_object_select_union_member_with_span(parser->program,
                                                                      object_id,
                                                                      slot_begin,
                                                                      union_record_id,
                                                                      field_index,
                                                                      canonical_slots)) {
                minic_parser_error(parser, "cannot replace static union active member");
                return false;
            }
            if (depth + 1U == designator->depth) {
                if (!overwrite_static_zero_field_value(parser, object_id, field, slot_begin)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser, "cannot replace static union active member");
                    }
                    return false;
                }
                return true;
            }
            {
                MinicStaticRecordDesignator suffix;
                const MinicRecord *selected_record;
                const MinicRecordField *leaf_field;
                size_t relative_slot;
                size_t suffix_index;

                if (!minic_type_is_record(field->type)) {
                    minic_parser_error(
                        parser,
                        "promoted noncanonical union designator requires a record active member");
                    return false;
                }
                selected_record =
                    minic_c0_program_record(parser->program, field->type.record_id);
                if (selected_record == NULL || !selected_record->is_complete) {
                    minic_parser_error(
                        parser,
                        "promoted noncanonical union designator requires a complete record member");
                    return false;
                }
                (void)memset(&suffix, 0, sizeof(suffix));
                suffix.depth = designator->depth - (depth + 1U);
                for (suffix_index = 0U; suffix_index < suffix.depth; ++suffix_index) {
                    suffix.field_indices[suffix_index] =
                        designator->field_indices[depth + 1U + suffix_index];
                }
                if (!static_record_designator_leaf_slot(parser->program,
                                                        selected_record,
                                                        &suffix,
                                                        &relative_slot,
                                                        &leaf_field) ||
                    slot_begin > SIZE_MAX - relative_slot ||
                    !overwrite_static_zero_field_value(
                        parser, object_id, leaf_field, slot_begin + relative_slot)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "cannot replace promoted static union designator leaf");
                    }
                    return false;
                }
            }
            return true;
        }
        {
            size_t relative;

            if (!minic_c0_global_record_field_initializer_slot(
                    parser->program, current_record, field_index, &relative) ||
                total > SIZE_MAX - relative) {
                return true;
            }
            total += relative;
        }
        if (depth + 1U == designator->depth) {
            return true;
        }
        if (!minic_type_is_record(field->type)) {
            return true;
        }
        current_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            return true;
        }
    }
    return true;
}

static bool append_static_record_designator_value(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  const size_t *field_indices,
                                                  size_t depth) {
    MinicRecord record_snapshot;
    MinicRecordId record_id;
    const MinicRecordField *field;
    size_t field_index;
    size_t field_limit;
    size_t selected_index;

    if (parser == NULL || record == NULL || !record->is_complete || field_indices == NULL ||
        depth == 0U || record < parser->program->records ||
        record >= parser->program->records + parser->program->record_count) {
        return false;
    }
    record_id = (MinicRecordId)(record - parser->program->records);
    record_snapshot = *record;
    record = &record_snapshot;
    field_limit = record->field_count;
    selected_index = field_indices[0];
    if (selected_index >= field_limit) {
        return false;
    }
    if (record->is_union) {
        MinicRecordId union_record_id;
        size_t union_base_slot;

        union_record_id = record_id;
        union_base_slot = parser->program->global_objects[object_id].initializer_count;
        if (!minic_c0_global_object_select_union_member(
                parser->program, object_id, union_base_slot, union_record_id, selected_index)) {
            minic_parser_error(parser, "cannot record static union active member");
            return false;
        }
    } else {
        for (field_index = 0U; field_index < selected_index; ++field_index) {
            if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                minic_parser_error(parser, "cannot zero-fill static record designator prefix");
                return false;
            }
        }
    }
    field = &record->fields[selected_index];
    if (field->element_count == 0U || field->is_flexible_array) {
        minic_parser_error(parser, "unsupported static record designator field");
        return false;
    }
    if (depth == 1U) {
        if (field->element_count == 1U && !field->is_array) {
            if (!minic_parser_parse_static_storage_initializer_value(
                    parser, object_id, field->type)) {
                return false;
            }
        } else if (field->is_array && minic_type_is_char_integer(field->type) &&
                   parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            if (!minic_parser_add_bounded_string_literal_initializer(
                    parser, object_id, field->element_count)) {
                return false;
            }
        } else if (!parse_static_forward_array_initializer(
                       parser, object_id, field->type, field->element_count, false, NULL)) {
            return false;
        }
    } else {
        const MinicRecord *nested_record;

        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            minic_parser_error(
                parser, "nested static record designator path requires scalar record members");
            return false;
        }
        nested_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (nested_record == NULL ||
            !append_static_record_designator_value(
                parser, object_id, nested_record, field_indices + 1U, depth - 1U)) {
            return false;
        }
    }
    if (!record->is_union) {
        for (field_index = selected_index + 1U; field_index < field_limit; ++field_index) {
            if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                minic_parser_error(parser, "cannot zero-fill static record designator suffix");
                return false;
            }
        }
    }
    return true;
}

static bool append_static_record_array_element_designator_value(MinicParser *parser,
                                                                MinicGlobalObjectId object_id,
                                                                const MinicRecordField *field,
                                                                size_t element_index) {
    size_t index;

    if (parser == NULL || field == NULL || !field->is_array || field->is_bit_field ||
        field->is_flexible_array || field->element_count == 0U ||
        element_index >= field->element_count) {
        return false;
    }
    for (index = 0U; index < element_index; ++index) {
        if (!append_static_constant_zero(parser, object_id, field->type)) {
            minic_parser_error(parser, "cannot zero-fill static record array designator prefix");
            return false;
        }
    }
    if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, field->type)) {
        return false;
    }
    for (index = element_index + 1U; index < field->element_count; ++index) {
        if (!append_static_constant_zero(parser, object_id, field->type)) {
            minic_parser_error(parser, "cannot zero-fill static record array designator suffix");
            return false;
        }
    }
    return true;
}

static bool overwrite_static_zero_record_constant(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  size_t record_base_slot);

static bool overwrite_static_zero_record_value(MinicParser *parser,
                                               MinicGlobalObjectId object_id,
                                               MinicType type,
                                               const MinicRecord *record,
                                               size_t record_base_slot);

static bool overwrite_static_zero_array_constant(MinicParser *parser,
                                                 MinicGlobalObjectId object_id,
                                                 MinicType element_type,
                                                 size_t element_count,
                                                 size_t array_base_slot);

static bool overwrite_static_zero_field_value(MinicParser *parser,
                                              MinicGlobalObjectId object_id,
                                              const MinicRecordField *field,
                                              size_t field_base_slot) {
    if (parser == NULL || field == NULL || field->element_count == 0U || field->is_flexible_array) {
        return false;
    }
    if (field->element_count == 1U && !field->is_array) {
        if (minic_type_is_integer(field->type) || minic_type_is_pointer(field->type)) {
            return parse_static_scalar_constant_at(
                parser, object_id, field->type, true, field_base_slot);
        }
        if (minic_type_is_record(field->type)) {
            const MinicRecord *nested_record;

            nested_record = minic_c0_program_record(parser->program, field->type.record_id);
            return nested_record != NULL &&
                   overwrite_static_zero_record_value(
                       parser, object_id, field->type, nested_record, field_base_slot);
        }
    }
    if (field->is_array && minic_type_is_char_integer(field->type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        return minic_parser_replace_zero_bounded_string_literal_initializer(
            parser, object_id, field_base_slot, field->element_count);
    }
    if (field->is_array) {
        return overwrite_static_zero_array_constant(
            parser, object_id, field->type, field->element_count, field_base_slot);
    }
    minic_parser_error(parser, "backward aggregate designator requires a supported zero subobject");
    return false;
}

static bool overwrite_static_zero_array_element(MinicParser *parser,
                                                MinicGlobalObjectId object_id,
                                                MinicType element_type,
                                                size_t element_base_slot) {
    if (minic_type_is_integer(element_type) || minic_type_is_pointer(element_type)) {
        return parse_static_scalar_constant_at(
            parser, object_id, element_type, true, element_base_slot);
    }
    if (minic_type_is_record(element_type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, element_type.record_id);
        return record != NULL && overwrite_static_zero_record_value(
                                     parser, object_id, element_type, record, element_base_slot);
    }
    if (minic_type_is_array(element_type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(parser->program, element_type.array_type_id);
        return array_type != NULL && array_type->element_count != 0U &&
               overwrite_static_zero_array_constant(parser,
                                                    object_id,
                                                    array_type->element_type,
                                                    array_type->element_count,
                                                    element_base_slot);
    }
    minic_parser_error(parser, "unsupported backward static array element type");
    return false;
}

static bool overwrite_static_zero_array_constant(MinicParser *parser,
                                                 MinicGlobalObjectId object_id,
                                                 MinicType element_type,
                                                 size_t element_count,
                                                 size_t array_base_slot) {
    size_t element_slots;
    size_t next_index;

    if (parser == NULL || element_count == 0U ||
        !minic_c0_global_initializer_slot_count(parser->program, element_type, &element_slots) ||
        parser->current.kind != MINIC_TOKEN_LBRACE || !minic_parser_advance(parser)) {
        return false;
    }
    next_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t first;
        size_t last;
        size_t element_base_slot;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!minic_parser_parse_array_designator(parser, element_count, false, &first, &last)) {
                return false;
            }
            if (first != last) {
                minic_parser_error(
                    parser,
                    "GNU range designators in backward static arrays are not supported yet");
                return false;
            }
        } else {
            first = next_index;
            last = first;
        }
        if (first >= element_count ||
            (element_slots != 0U && first > (SIZE_MAX - array_base_slot) / element_slots)) {
            minic_parser_error(parser, "backward static array initializer index is out of range");
            return false;
        }
        element_base_slot = array_base_slot + first * element_slots;
        if (!overwrite_static_zero_array_element(
                parser, object_id, element_type, element_base_slot)) {
            return false;
        }
        next_index = first + 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in backward static array initializer");
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_RBRACE, "expected '}' after backward static array initializer");
}

static bool overwrite_static_zero_record_value(MinicParser *parser,
                                               MinicGlobalObjectId object_id,
                                               MinicType type,
                                               const MinicRecord *record,
                                               size_t record_base_slot) {
    if (parser == NULL || record == NULL || !record->is_complete || !minic_type_is_record(type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicParser probe;
        MinicType explicit_type;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_LPAREN) {
            if (!minic_parser_advance(parser) ||
                !overwrite_static_zero_record_value(
                    parser, object_id, type, record, record_base_slot) ||
                !minic_parser_expect(parser,
                                     MINIC_TOKEN_RPAREN,
                                     "expected ')' after grouped static record initializer")) {
                return false;
            }
            return true;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_type_name(parser, &explicit_type) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' after static compound literal type")) {
            return false;
        }
        if (!minic_type_is_record(explicit_type) ||
            !minic_type_assignment_compatible(type, explicit_type)) {
            minic_parser_error(parser, "static record compound literal type mismatch");
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_LBRACE) {
            minic_parser_error(parser, "static record compound literal requires initializer list");
            return false;
        }
    }
    return overwrite_static_zero_record_constant(parser, object_id, record, record_base_slot);
}

static bool overwrite_static_zero_record_constant(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  size_t record_base_slot) {
    size_t field_index;
    size_t field_limit;

    if (parser == NULL || record == NULL || !record->is_complete ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    field_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        size_t relative_slot;
        size_t field_base_slot;

        if (parser->current.kind == MINIC_TOKEN_DOT) {
            MinicStaticRecordDesignator designator;
            bool handled_union_zero;

            (void)memset(&designator, 0, sizeof(designator));
            if (!parse_static_record_designator_path(parser, record, &designator) ||
                designator.depth == 0U || designator.has_array_index) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser,
                        "backward aggregate initializer requires an unambiguous member path");
                }
                return false;
            }

            handled_union_zero = false;
            if (!try_overwrite_static_zero_noncanonical_union_designator(parser,
                                                                         object_id,
                                                                         record,
                                                                         &designator,
                                                                         record_base_slot,
                                                                         &handled_union_zero)) {
                return false;
            }
            if (handled_union_zero) {
                field_index = designator.field_indices[0] + 1U;
                if (record->is_union) {
                    if (parser->current.kind == MINIC_TOKEN_COMMA &&
                        !minic_parser_advance(parser)) {
                        return false;
                    }
                    if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                        minic_parser_error(
                            parser,
                            "backward union initializer may initialize only one active member");
                        return false;
                    }
                    return minic_parser_advance(parser);
                }
                goto backward_record_value_done;
            }

            if (designator.depth > 1U) {
                const MinicRecordField *leaf_field;
                size_t leaf_relative_slot;

                if (!static_record_designator_leaf_slot(parser->program,
                                                        record,
                                                        &designator,
                                                        &leaf_relative_slot,
                                                        &leaf_field) ||
                    record_base_slot > SIZE_MAX - leaf_relative_slot ||
                    !overwrite_static_zero_field_value(parser,
                                                       object_id,
                                                       leaf_field,
                                                       record_base_slot + leaf_relative_slot)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "cannot overwrite promoted backward record designator");
                    }
                    return false;
                }
                field_index = designator.field_indices[0] + 1U;
                goto backward_record_value_done;
            }

            field_index = designator.field_indices[0];
        }
        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many backward aggregate initializer fields");
            return false;
        }
        field = &record->fields[field_index];
        if (!minic_c0_global_record_field_initializer_slot(
                parser->program, record, field_index, &relative_slot) ||
            record_base_slot > SIZE_MAX - relative_slot) {
            minic_parser_error(parser, "cannot locate backward aggregate initializer field slot");
            return false;
        }
        field_base_slot = record_base_slot + relative_slot;
        if (!overwrite_static_zero_field_value(parser, object_id, field, field_base_slot)) {
            return false;
        }
        field_index += 1U;

backward_record_value_done:
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in backward aggregate initializer");
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_RBRACE, "expected '}' after backward aggregate initializer");
}

static bool parse_static_union_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicRecord *record) {
    MinicStaticRecordDesignator designator;
    const MinicRecordField *field;
    MinicRecordId record_id;
    size_t selected_index;
    size_t union_base_slot;
    bool has_designator;

    if (parser == NULL || record == NULL || !record->is_complete || !record->is_union ||
        record->field_count == 0U || object_id >= parser->program->global_object_count ||
        record < parser->program->records ||
        record >= parser->program->records + parser->program->record_count ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in union initializer")) {
        return false;
    }
    record_id = (MinicRecordId)(record - parser->program->records);
    union_base_slot = parser->program->global_objects[object_id].initializer_count;
    if (parser->current.kind == MINIC_TOKEN_RBRACE) {
        if (!minic_c0_global_object_select_union_member(
                parser->program, object_id, union_base_slot, record_id, 0U) ||
            !append_static_field_zeros(parser, object_id, &record->fields[0])) {
            return false;
        }
        return minic_parser_advance(parser);
    }

    (void)memset(&designator, 0, sizeof(designator));
    has_designator = parser->current.kind == MINIC_TOKEN_DOT;
    if (has_designator) {
        if (!parse_static_record_designator_path(parser, record, &designator) ||
            designator.depth == 0U) {
            return false;
        }
        selected_index = designator.field_indices[0];
    } else {
        selected_index = 0U;
    }
    if (selected_index >= record->field_count ||
        !minic_c0_global_object_select_union_member(
            parser->program, object_id, union_base_slot, record_id, selected_index)) {
        minic_parser_error(parser, "cannot select static union initializer member");
        return false;
    }
    field = &record->fields[selected_index];
    if (field->element_count == 0U || field->is_flexible_array) {
        minic_parser_error(parser, "unsupported static union initializer member");
        return false;
    }

    if (has_designator && designator.has_array_index) {
        if (designator.depth != 1U || !field->is_array || field->is_bit_field ||
            designator.array_index >= field->element_count ||
            !append_static_record_array_element_designator_value(
                parser, object_id, field, designator.array_index)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "cannot initialize designated static union array member");
            }
            return false;
        }
    } else if (has_designator && designator.depth > 1U) {
        const MinicRecord *nested_record;

        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            minic_parser_error(parser, "nested static union designator requires a record member");
            return false;
        }
        nested_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (nested_record == NULL ||
            !append_static_record_designator_value(parser,
                                                   object_id,
                                                   nested_record,
                                                   designator.field_indices + 1U,
                                                   designator.depth - 1U)) {
            return false;
        }
    } else if (field->element_count == 1U && !field->is_array) {
        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, field->type)) {
            return false;
        }
    } else if (field->is_array && minic_type_is_char_integer(field->type) &&
               parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_parser_add_bounded_string_literal_initializer(
                parser, object_id, field->element_count)) {
            return false;
        }
    } else if (!parse_static_forward_array_initializer(
                   parser, object_id, field->type, field->element_count, false, NULL)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_RBRACE) {
        minic_parser_error(parser, "union initializer may initialize only one active member");
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_static_record_constant(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         const MinicRecord *record) {
    MinicRecord record_snapshot;
    MinicRecordId record_id;
    size_t field_index;
    size_t field_limit;
    size_t materialized_field_limit;
    size_t record_base_slot;

    if (parser == NULL || record == NULL || !record->is_complete ||
        record < parser->program->records ||
        record >= parser->program->records + parser->program->record_count ||
        object_id >= parser->program->global_object_count) {
        return false;
    }
    record_id = (MinicRecordId)(record - parser->program->records);
    /* program->records is a growable arena. Initializer expressions such as
       sizeof(struct { ... }) may append anonymous records and realloc that arena.
       Snapshot the complete descriptor before recursive expression parsing.
       Its fields storage is independently allocated and immutable after the
       record is completed, so the snapshot remains valid across arena growth. */
    record_snapshot = *record;
    record = &record_snapshot;
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    field_index = 0U;
    materialized_field_limit = 0U;
    record_base_slot = parser->program->global_objects[object_id].initializer_count;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicStaticRecordDesignator designator;
        const MinicRecordField *field;
        bool has_designator;
        bool overwrite_materialized_field;

        has_designator = false;
        (void)memset(&designator, 0, sizeof(designator));
        if (parser->current.kind == MINIC_TOKEN_DOT) {
            size_t designator_index;

            if (!parse_static_record_designator_path(parser, record, &designator)) {
                return false;
            }
            designator_index = designator.field_indices[0];
            while (materialized_field_limit < designator_index) {
                if (!append_static_field_zeros(
                        parser, object_id, &record->fields[materialized_field_limit])) {
                    minic_parser_error(parser,
                                       "cannot zero-fill skipped static record designator fields");
                    return false;
                }
                materialized_field_limit += 1U;
            }
            field_index = designator_index;
            has_designator = true;
        }
        if (!has_designator) {
            while (field_index < field_limit &&
                   record->fields[field_index].is_bit_field &&
                   record->fields[field_index].name_length == 0U) {
                if (!append_static_field_zeros(
                        parser, object_id, &record->fields[field_index])) {
                    minic_parser_error(
                        parser, "cannot zero-fill unnamed static record bit-field");
                    return false;
                }
                field_index += 1U;
                materialized_field_limit += 1U;
            }
        }
        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many nested static record initializers");
            return false;
        }
        field = &record->fields[field_index];
        if (field->is_flexible_array) {
            const MinicGlobalObject *object;
            size_t flexible_element_count;
            bool parsed_flexible_tail;

            object = minic_c0_program_global_object(parser->program, object_id);
            flexible_element_count = 0U;
            if (object == NULL || !minic_type_is_record(object->type) ||
                object->type.record_id != record_id ||
                record->is_union || field_index + 1U != record->field_count ||
                field_index != materialized_field_limit || field->is_bit_field ||
                (has_designator && (designator.depth != 1U || designator.has_array_index)) ||
                parser->current.kind != MINIC_TOKEN_LBRACE ||
                !minic_parser_inspect_array_initializer_extent(parser, &flexible_element_count) ||
                flexible_element_count == 0U ||
                !minic_c0_global_object_set_flexible_array_initializer_count(
                    parser->program, object_id, flexible_element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser,
                        "GNU static flexible array initializer requires a supported direct tail");
                }
                return false;
            }
            /* Publish the inspected FAM extent before parsing its elements. Aggregate relocation
             * validation resolves a scalar slot through the complete top-level object shape; the
             * flexible tail therefore has to be visible while its transaction is in progress. */
            if (minic_type_is_integer(field->type) || minic_type_is_pointer(field->type)) {
                parsed_flexible_tail = minic_parser_static_scalar_array_transaction(
                    parser, object_id, field->type, flexible_element_count, false);
            } else {
                parsed_flexible_tail = parse_static_forward_array_initializer(
                    parser, object_id, field->type, flexible_element_count, false, NULL);
            }
            if (!parsed_flexible_tail) {
                /* Parsing the translation unit will fail, but restore the object invariant so
                 * diagnostics and cleanup never observe a committed extent without its payload. */
                parser->program->global_objects[object_id].flexible_array_initializer_count = 0U;
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser,
                        "GNU static flexible array initializer requires a supported direct tail");
                }
                return false;
            }
            materialized_field_limit += 1U;
            field_index += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser,
                                   "expected ',' or '}' after static flexible array initializer");
                return false;
            }
            continue;
        }
        if (field->element_count == 0U) {
            minic_parser_error(parser, "unsupported nested static record field");
            return false;
        }
        overwrite_materialized_field = field_index < materialized_field_limit;
        if (has_designator && designator.has_array_index) {
            size_t relative_slot;
            size_t field_base_slot;
            size_t element_base_slot;
            size_t element_slots;

            if (designator.depth != 1U || !field->is_array || field->is_bit_field ||
                field->is_flexible_array || designator.array_index >= field->element_count) {
                minic_parser_error(parser,
                                   "static record array member designator currently requires a "
                                   "direct fixed array field");
                return false;
            }
            if (overwrite_materialized_field) {
                if (!minic_c0_global_record_field_initializer_slot(
                        parser->program, record, field_index, &relative_slot) ||
                    !minic_c0_global_initializer_slot_count(
                        parser->program, field->type, &element_slots) ||
                    record_base_slot > SIZE_MAX - relative_slot) {
                    minic_parser_error(parser,
                                       "cannot locate static record array member designator field");
                    return false;
                }
                field_base_slot = record_base_slot + relative_slot;
                if (element_slots != 0U &&
                    designator.array_index > (SIZE_MAX - field_base_slot) / element_slots) {
                    minic_parser_error(parser,
                                       "static record array member designator slot overflows");
                    return false;
                }
                element_base_slot = field_base_slot + designator.array_index * element_slots;
                if (!overwrite_static_zero_array_element(
                        parser, object_id, field->type, element_base_slot)) {
                    return false;
                }
            } else {
                if (field_index != materialized_field_limit ||
                    !append_static_record_array_element_designator_value(
                        parser, object_id, field, designator.array_index)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "cannot materialize static record array member designator");
                    }
                    return false;
                }
                materialized_field_limit += 1U;
            }
        } else if (has_designator && designator.depth > 1U) {
            if (overwrite_materialized_field) {
                size_t relative_slot;
                size_t slot_index;
                const MinicRecordField *leaf_field;
                bool handled_union_zero;

                {
                    const MinicRecord *identity_record;

                    identity_record = minic_c0_program_record(parser->program, record_id);
                    if (identity_record == NULL ||
                        !try_overwrite_static_zero_noncanonical_union_designator(
                            parser,
                            object_id,
                            identity_record,
                            &designator,
                            record_base_slot,
                            &handled_union_zero)) {
                        return false;
                    }
                }
                if (handled_union_zero) {
                    /* The selected noncanonical union member denotes the same
                     * already-materialized all-zero bytes. No flattened scalar
                     * rewrite is needed or representable. */
                } else {
                    if (!static_record_designator_leaf_slot(
                            parser->program, record, &designator, &relative_slot, &leaf_field) ||
                        record_base_slot > SIZE_MAX - relative_slot) {
                        minic_parser_error(parser,
                                           "cannot locate backward nested static record "
                                           "designator leaf");
                        return false;
                    }
                    slot_index = record_base_slot + relative_slot;
                    if (!overwrite_static_zero_field_value(
                            parser, object_id, leaf_field, slot_index)) {
                        return false;
                    }
                }
            } else {
                const MinicRecord *nested_record;

                if (field_index != materialized_field_limit || field->element_count != 1U ||
                    field->is_array || field->is_bit_field || !minic_type_is_record(field->type)) {
                    minic_parser_error(
                        parser,
                        "forward nested static record designator requires a scalar record field");
                    return false;
                }
                nested_record = minic_c0_program_record(parser->program, field->type.record_id);
                if (nested_record == NULL ||
                    !append_static_record_designator_value(parser,
                                                           object_id,
                                                           nested_record,
                                                           designator.field_indices + 1U,
                                                           designator.depth - 1U)) {
                    return false;
                }
                materialized_field_limit += 1U;
            }
        } else if (overwrite_materialized_field) {
            size_t relative_slot;
            size_t slot_index;

            if (!minic_c0_global_record_field_initializer_slot(
                    parser->program, record, field_index, &relative_slot) ||
                record_base_slot > SIZE_MAX - relative_slot) {
                minic_parser_error(parser, "cannot locate backward static record designator slot");
                return false;
            }
            slot_index = record_base_slot + relative_slot;
            if (!overwrite_static_zero_field_value(parser, object_id, field, slot_index)) {
                return false;
            }
        } else {
            if (field_index != materialized_field_limit) {
                minic_parser_error(parser, "internal error: invalid static record materialization");
                return false;
            }
            if (field->element_count == 1U && !field->is_array) {
                if (!minic_parser_parse_static_storage_initializer_value(
                        parser, object_id, field->type)) {
                    return false;
                }
            } else if (field->is_array && minic_type_is_char_integer(field->type) &&
                       parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
                if (!minic_parser_add_bounded_string_literal_initializer(
                        parser, object_id, field->element_count)) {
                    return false;
                }
            } else if (!parse_static_forward_array_initializer(
                           parser, object_id, field->type, field->element_count, false, NULL)) {
                return false;
            }
            materialized_field_limit += 1U;
        }
        field_index += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in nested static record initializer");
            return false;
        }
    }
    while (materialized_field_limit < field_limit) {
        if (!append_static_field_zeros(
                parser, object_id, &record->fields[materialized_field_limit])) {
            minic_parser_error(parser, "cannot zero-fill nested static record initializer");
            return false;
        }
        materialized_field_limit += 1U;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer");
}

bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type) ||
        minic_type_is_float(type) || minic_type_is_double(type)) {
        return parse_static_scalar_constant(parser, object_id, type);
    }
    if (minic_type_is_array(type)) {
        return parse_static_array_constant(
            parser, object_id, minic_c0_program_array_type(parser->program, type.array_type_id));
    }
    if (minic_type_is_record(type)) {
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            MinicParser probe;
            MinicType explicit_type;

            probe = *parser;
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            if (probe.current.kind == MINIC_TOKEN_LPAREN) {
                if (!minic_parser_advance(parser) ||
                    !minic_parser_parse_static_storage_initializer_value(parser, object_id, type) ||
                    !minic_parser_expect(parser,
                                         MINIC_TOKEN_RPAREN,
                                         "expected ')' after grouped static record initializer")) {
                    return false;
                }
                return true;
            }
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_type_name(parser, &explicit_type) ||
                !minic_parser_expect(parser,
                                     MINIC_TOKEN_RPAREN,
                                     "expected ')' after static compound literal type")) {
                return false;
            }
            if (!minic_type_is_record(explicit_type) ||
                !minic_type_assignment_compatible(type, explicit_type)) {
                minic_parser_error(parser, "static record compound literal type mismatch");
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_LBRACE) {
                minic_parser_error(parser,
                                   "static record compound literal requires initializer list");
                return false;
            }
        }
        {
            const MinicRecord *record;

            record = minic_c0_program_record(parser->program, type.record_id);
            if (record == NULL) {
                return false;
            }
            return record->is_union ? parse_static_union_constant(parser, object_id, record)
                                    : parse_static_record_constant(parser, object_id, record);
        }
    }
    minic_parser_error(parser, "unsupported nested static aggregate initializer type");
    return false;
}

static bool
parse_static_nested_record_object(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;

    if (!minic_parser_global_begin_definition(parser, type, name_span, &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse nested static record initializer");
        }
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        MinicSourceSpan next_name_span;

        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected static aggregate declarator after ','");
            return false;
        }
        next_name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            return parse_static_nested_record_object(parser, type, next_name_span);
        }
        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            return minic_parser_global_zero_definition(parser, type, next_name_span);
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            return minic_parser_parse_static_zero_declaration_list_after_head(parser,
                                                                               type,
                                                                               type,
                                                                               next_name_span,
                                                                               NULL,
                                                                               0U,
                                                                               false,
                                                                               0U);
        }
        minic_parser_error(parser,
                           "expected '=', ',' or ';' after static aggregate declarator");
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}

static bool probe_static_array_designator_extent(MinicParser *probe, size_t *first, size_t *last) {
    MinicSemanticSnapshot snapshot;
    MinicC0Program *program;

    if (probe == NULL || first == NULL || last == NULL || probe->program == NULL) {
        return false;
    }
    program = probe->program;
    snapshot = minic_semantic_snapshot_capture(program);
    if (!minic_parser_parse_array_designator(probe, 0U, true, first, last)) {
        return false;
    }
    if (!minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, program)) {
        minic_parser_error(probe,
                           "inferred aggregate array designator probe requires a "
                           "side-effect-free integer constant expression");
        return false;
    }
    return true;
}

bool minic_parser_inspect_array_initializer_extent(MinicParser *parser, size_t *element_count) {
    MinicParser probe;
    size_t extent;
    size_t next_index;

    if (parser == NULL || element_count == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    extent = 0U;
    next_index = 0U;
    while (probe.current.kind != MINIC_TOKEN_RBRACE) {
        size_t first;
        size_t last;
        size_t brace_depth;
        size_t parenthesis_depth;
        size_t bracket_depth;

        first = next_index;
        last = next_index;
        if (probe.current.kind == MINIC_TOKEN_LBRACKET &&
            !probe_static_array_designator_extent(&probe, &first, &last)) {
            return false;
        }
        if (last == SIZE_MAX) {
            minic_parser_error(&probe, "inferred aggregate array initializer extent overflows");
            return false;
        }
        next_index = last + 1U;
        if (next_index > extent) {
            extent = next_index;
        }

        brace_depth = 0U;
        parenthesis_depth = 0U;
        bracket_depth = 0U;
        while (probe.current.kind != MINIC_TOKEN_EOF) {
            if (brace_depth == 0U && parenthesis_depth == 0U && bracket_depth == 0U &&
                (probe.current.kind == MINIC_TOKEN_COMMA ||
                 probe.current.kind == MINIC_TOKEN_RBRACE)) {
                break;
            }
            switch (probe.current.kind) {
            case MINIC_TOKEN_LBRACE:
                brace_depth += 1U;
                break;
            case MINIC_TOKEN_RBRACE:
                if (brace_depth == 0U) {
                    return false;
                }
                brace_depth -= 1U;
                break;
            case MINIC_TOKEN_LPAREN:
                parenthesis_depth += 1U;
                break;
            case MINIC_TOKEN_RPAREN:
                if (parenthesis_depth == 0U) {
                    return false;
                }
                parenthesis_depth -= 1U;
                break;
            case MINIC_TOKEN_LBRACKET:
                bracket_depth += 1U;
                break;
            case MINIC_TOKEN_RBRACKET:
                if (bracket_depth == 0U) {
                    return false;
                }
                bracket_depth -= 1U;
                break;
            default:
                break;
            }
            if (!minic_parser_advance(&probe)) {
                return false;
            }
        }
        if (probe.current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            if (probe.current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (probe.current.kind != MINIC_TOKEN_RBRACE) {
            return false;
        }
    }
    *element_count = extent;
    return true;
}

static bool static_record_array_declaration_compatible(const MinicC0Program *program,
                                                       MinicType existing_type,
                                                       MinicType element_type,
                                                       size_t declared_count,
                                                       bool declared_incomplete) {
    const MinicArrayType *existing_array;

    if (program == NULL || !minic_type_is_array(existing_type)) {
        return false;
    }
    existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
    if (existing_array == NULL || !minic_type_equal(existing_array->element_type, element_type)) {
        return false;
    }
    if (declared_incomplete ||
        (existing_array->element_count == 0U && !existing_array->is_zero_length)) {
        return true;
    }
    return !existing_array->is_zero_length && existing_array->element_count == declared_count;
}

static bool parse_static_record_array(MinicParser *parser,
                                      MinicType element_type,
                                      MinicSourceSpan name_span,
                                      char *section_name,
                                      size_t section_capacity,
                                      size_t *section_name_length,
                                      bool *has_section,
                                      size_t *explicit_alignment) {
    const MinicRecord *record;
    MinicType nested_element_type;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId existing_id;
    size_t declared_count;
    bool inferred_bound;
    bool multidimensional;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {
        return false;
    }
    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "static record array requires a complete record type");
        return false;
    }
    /* GNU empty structs are complete zero-size object types. Arrays of them
       likewise occupy zero bytes but remain valid semantic objects; the target
       DataLayout already carries zero-size record behavior. */

    declared_count = 0U;
    inferred_bound = false;
    multidimensional = false;
    nested_element_type = element_type;
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &declared_count)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        bool nested_is_array;

        if (inferred_bound) {
            minic_parser_error(
                parser, "inferred multi-dimensional static record arrays are not supported yet");
            return false;
        }
        nested_is_array = false;
        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, false, &nested_element_type, &nested_is_array) ||
            !nested_is_array) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "cannot build nested static record array declarator type");
            }
            return false;
        }
        multidimensional = true;
    }
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }

    existing_id = minic_parser_find_global_object_entity(parser, name_span);
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;

        if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
            MinicType tentative_element_type;

            tentative_element_type = multidimensional ? nested_element_type : element_type;
            if ((inferred_bound && !minic_c0_program_add_incomplete_array_type(
                                       parser->program, tentative_element_type, &object_type)) ||
                (!inferred_bound &&
                 !minic_c0_program_add_array_type(
                     parser->program, tentative_element_type, declared_count, &object_type)) ||
                !minic_c0_program_add_tentative_global_object(parser->program,
                                                              parser->source +
                                                                  name_span.begin.offset,
                                                              minic_parser_span_length(name_span),
                                                              object_type,
                                                              true,
                                                              minic_type_is_const(element_type),
                                                              &object_id)) {
                minic_parser_error(parser,
                                   "cannot create static record array tentative definition");
                return false;
            }
        } else {
            existing = minic_c0_program_global_object(parser->program, existing_id);
            existing_array =
                existing != NULL && minic_type_is_array(existing->type)
                    ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                    : NULL;
            if (existing == NULL || existing_array == NULL || !existing->is_internal ||
                !static_record_array_declaration_compatible(parser->program,
                                                            existing->type,
                                                            element_type,
                                                            declared_count,
                                                            inferred_bound) ||
                (!inferred_bound && existing_array->element_count == 0U &&
                 !existing_array->is_zero_length &&
                 !minic_c0_program_complete_array_type(
                     parser->program, existing->type, declared_count)) ||
                !minic_c0_global_object_merge_tentative(parser->program, existing_id)) {
                minic_parser_error(parser, "conflicting static record array tentative definition");
                return false;
            }
            object_id = existing_id;
        }
        if (!minic_parser_global_apply_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment)) {
            minic_parser_error(parser, "cannot persist static record array declaration metadata");
            return false;
        }
        return minic_parser_advance(parser);
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||
        parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot inspect static record array initializer");
        }
        return false;
    }
    if (inferred_bound) {
        if (!minic_parser_inspect_array_initializer_extent(parser, &declared_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot infer static record array initializer extent");
            }
            return false;
        }

    }

    if (existing_id != MINIC_GLOBAL_OBJECT_INVALID) {
        MinicGlobalObject *existing;
        const MinicArrayType *existing_array;

        if (multidimensional) {
            minic_parser_error(
                parser, "multi-dimensional static record redeclarations are not supported yet");
            return false;
        }
        existing = &parser->program->global_objects[existing_id];
        existing_array =
            minic_type_is_array(existing->type)
                ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                : NULL;
        if (existing_array == NULL || !existing->is_internal || !existing->is_tentative ||
            !static_record_array_declaration_compatible(
                parser->program, existing->type, element_type, declared_count, false) ||
            (existing_array->element_count == 0U && !existing_array->is_zero_length &&
             (declared_count == 0U
                  ? !minic_c0_program_complete_zero_length_array_type(
                        parser->program, existing->type)
                  : !minic_c0_program_complete_array_type(
                        parser->program, existing->type, declared_count))) ||
            !minic_c0_global_object_begin_definition(parser->program, existing_id)) {
            minic_parser_error(parser, "conflicting static record array definition");
            return false;
        }
        object_id = existing_id;
        object_type = parser->program->global_objects[object_id].type;
    } else {
        if ((declared_count == 0U
                 ? !minic_c0_program_add_zero_length_array_type(
                       parser->program,
                       multidimensional ? nested_element_type : element_type,
                       &object_type)
                 : !minic_c0_program_add_array_type(
                       parser->program,
                       multidimensional ? nested_element_type : element_type,
                       declared_count,
                       &object_type)) ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                true,
                                                minic_type_is_const(element_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create static record array definition");
            return false;
        }
    }
    if (!minic_parser_global_apply_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment) ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse static record array initializer");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static record array");
}
bool minic_parser_static_record_initializer(MinicParser *parser,
                                MinicType type,
                                MinicSourceSpan name_span,
                                char *section_name,
                                size_t section_capacity,
                                size_t *section_name_length,
                                bool *has_section,
                                size_t *explicit_alignment) {
    const MinicRecord *record;

    record = minic_c0_program_record(parser->program, type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "static record global requires a complete record type");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        return parse_static_record_array(parser,
                                         type,
                                         name_span,
                                         section_name,
                                         section_capacity,
                                         section_name_length,
                                         has_section,
                                         explicit_alignment);
    }
    return parse_static_nested_record_object(parser, type, name_span);
}
