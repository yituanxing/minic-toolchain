#!/usr/bin/env python3
"""Materialize transactional static aggregate-array initializer ownership."""
from pathlib import Path


path = Path("src/frontend/parser_global.c")
text = path.read_text()
marker = "typedef struct MinicStaticAggregateArrayAction {"
if marker in text:
    raise SystemExit(0)

start_marker = "static bool clone_static_aggregate_range_element("
end_marker = "\nstatic bool parse_static_array_constant("
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("src/frontend/parser_global.c: aggregate-array materialization anchors not found")

replacement = r'''typedef struct MinicStaticAggregateArrayAction {
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
    resized = (MinicStaticAggregateArrayAction *)realloc(
        *actions, new_capacity * sizeof(*resized));
    if (resized == NULL) {
        minic_parser_error(parser, "out of memory while growing static aggregate array actions");
        return false;
    }
    (void)memset(resized + old_capacity,
                 0,
                 (new_capacity - old_capacity) * sizeof(*resized));
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
    action->relocations = relocation_count == 0U
                              ? NULL
                              : (MinicGlobalRelocation *)malloc(
                                    relocation_count * sizeof(*action->relocations));
    action->union_selections = union_selection_count == 0U
                                   ? NULL
                                   : (MinicGlobalUnionSelection *)malloc(
                                         union_selection_count * sizeof(*action->union_selections));
    if (action->values == NULL ||
        (relocation_count != 0U && action->relocations == NULL) ||
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
            minic_parser_error(parser, "static aggregate array relocation precedes captured action");
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

static bool materialize_static_aggregate_array_action(
    MinicParser *parser,
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
        if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
            recorded = minic_c0_global_object_add_label_relocation(
                parser->program,
                object_id,
                relocation->location_kind,
                location_index,
                (MinicStatementId)relocation->target_id);
        } else if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
            recorded = relocation->has_explicit_pointer_cast
                           ? minic_c0_global_object_add_function_relocation_cast(
                                 parser->program,
                                 object_id,
                                 relocation->location_kind,
                                 location_index,
                                 (MinicFunctionId)relocation->target_id)
                           : minic_c0_global_object_add_function_relocation(
                                 parser->program,
                                 object_id,
                                 relocation->location_kind,
                                 location_index,
                                 (MinicFunctionId)relocation->target_id);
        } else {
            recorded = relocation->has_explicit_pointer_cast
                           ? minic_c0_global_object_add_object_relocation_path_addend_cast(
                                 parser->program,
                                 object_id,
                                 relocation->location_kind,
                                 location_index,
                                 (MinicGlobalObjectId)relocation->target_id,
                                 relocation->target_member_indices,
                                 relocation->target_member_depth,
                                 relocation->target_byte_addend)
                           : minic_c0_global_object_add_object_relocation_path_addend(
                                 parser->program,
                                 object_id,
                                 relocation->location_kind,
                                 location_index,
                                 (MinicGlobalObjectId)relocation->target_id,
                                 relocation->target_member_indices,
                                 relocation->target_member_depth,
                                 relocation->target_byte_addend);
        }
        if (!recorded) {
            minic_parser_error(parser, "cannot materialize static aggregate array relocation");
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
        if (!minic_c0_global_object_select_union_member(parser->program,
                                                        object_id,
                                                        initializer_slot,
                                                        selection->record_id,
                                                        selection->field_index)) {
            minic_parser_error(parser, "cannot materialize static aggregate array union selection");
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

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (!minic_parser_parse_array_designator(
                    parser, element_count, infer_bound, &first, &last) ||
                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "static aggregate array designator extent overflows");
                }
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
        if (!minic_parser_parse_static_storage_initializer_value(
                parser, object_id, element_type) ||
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
    if (infer_bound && extent == 0U) {
        minic_parser_error(parser, "cannot infer static array bound from an empty initializer");
        goto done;
    }
    for (index = 0U; index < extent; ++index) {
        size_t owner;

        if (!minic_array_initializer_plan_final_owner(&plan, index, &owner)) {
            minic_parser_error(parser, "cannot resolve static aggregate array initializer owner");
            goto done;
        }
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
    minic_array_initializer_plan_destroy(&plan);
    return success;
}
'''

path.write_text(text[:start] + replacement + text[end:])
