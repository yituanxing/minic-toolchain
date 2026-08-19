#!/usr/bin/env python3
from pathlib import Path

p = Path('src/frontend/parser_global.c')
text = p.read_text()
start = text.index('static bool parse_static_pointer_array(')
end = text.index('\nstatic bool static_object_type_is_read_only', start)
new = r'''static bool parse_static_pointer_array(MinicParser *parser,
                                       MinicType element_type,
                                       MinicSourceSpan name_span,
                                       char *section_name,
                                       size_t section_capacity,
                                       size_t *section_name_length,
                                       bool *has_section,
                                       size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId existing_id;
    size_t element_count;
    bool inferred_bound;

    element_count = 0U;
    inferred_bound = false;
    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_pointer(element_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser) ||
            !minic_c0_program_add_incomplete_array_type(
                parser->program, element_type, &object_type)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
               !minic_c0_program_add_array_type(
                   parser->program, element_type, element_count, &object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot build static pointer array type");
        }
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static pointer arrays are not supported yet");
        return false;
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
    object_id = MINIC_GLOBAL_OBJECT_INVALID;
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;
        const MinicArrayType *declared_array;
        bool discard_declared;

        if (inferred_bound) {
            minic_parser_error(parser, "incomplete static pointer array requires an initializer");
            return false;
        }
        if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
            if (!minic_c0_program_add_tentative_global_object(
                    parser->program,
                    parser->source + name_span.begin.offset,
                    minic_parser_span_length(name_span),
                    object_type,
                    true,
                    minic_type_is_const(element_type),
                    &object_id)) {
                minic_parser_error(parser, "cannot create static pointer array tentative definition");
                return false;
            }
        } else {
            existing = minic_c0_program_global_object(parser->program, existing_id);
            existing_array =
                existing != NULL && minic_type_is_array(existing->type)
                    ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                    : NULL;
            declared_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
            discard_declared = object_type.array_type_id + 1U == parser->program->array_type_count &&
                               (existing == NULL ||
                                existing->type.array_type_id != object_type.array_type_id);
            if (existing == NULL || existing_array == NULL || declared_array == NULL ||
                !existing->is_internal ||
                !minic_type_equal(existing_array->element_type, declared_array->element_type) ||
                existing_array->element_count != declared_array->element_count ||
                !minic_c0_global_object_merge_tentative(parser->program, existing_id)) {
                minic_parser_error(parser, "conflicting static pointer array tentative definition");
                return false;
            }
            object_id = existing_id;
            if (discard_declared &&
                !minic_c0_program_discard_last_array_type(parser->program, object_type)) {
                minic_parser_error(parser, "cannot retire transient static pointer array declaration");
                return false;
            }
        }
        if ((*has_section && !minic_c0_global_object_set_section(
                                 parser->program, object_id, section_name, *section_name_length)) ||
            (*explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                              parser->program, object_id, *explicit_alignment))) {
            minic_parser_error(parser, "cannot persist static pointer array metadata");
            return false;
        }
        return minic_parser_advance(parser);
    }

    if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                true,
                                                minic_type_is_const(element_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create static pointer array object");
            return false;
        }
    } else {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;
        const MinicArrayType *declared_array;
        size_t declared_count;
        bool discard_declared;

        existing = minic_c0_program_global_object(parser->program, existing_id);
        existing_array =
            existing != NULL && minic_type_is_array(existing->type)
                ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                : NULL;
        declared_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        declared_count = declared_array == NULL ? 0U : declared_array->element_count;
        discard_declared = object_type.array_type_id + 1U == parser->program->array_type_count &&
                           (existing == NULL || existing->type.array_type_id != object_type.array_type_id);
        if (existing == NULL || existing_array == NULL || declared_array == NULL ||
            !existing->is_internal || !existing->is_tentative ||
            !minic_type_equal(existing_array->element_type, declared_array->element_type) ||
            (existing_array->element_count != 0U && declared_count != 0U &&
             existing_array->element_count != declared_count) ||
            (existing_array->element_count == 0U && declared_count != 0U &&
             !minic_c0_program_complete_array_type(
                 parser->program, existing->type, declared_count)) ||
            !minic_c0_global_object_begin_definition(parser->program, existing_id)) {
            minic_parser_error(parser, "conflicting static pointer array definition");
            return false;
        }
        object_id = existing_id;
        object_type = parser->program->global_objects[object_id].type;
        existing_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        if (existing_array == NULL) {
            return false;
        }
        element_count = existing_array->element_count;
        inferred_bound = element_count == 0U && !existing_array->is_zero_length;
        if (discard_declared &&
            !minic_c0_program_discard_last_array_type(parser->program, declared_array == NULL
                                                                           ? object_type
                                                                           : (MinicType){0})) {
            /* The generic discard helper requires the exact transient type; use the
             * saved declarator type below instead of silently leaking arena state. */
            minic_parser_error(parser, "cannot retire transient static pointer array definition");
            return false;
        }
    }

    if ((*has_section && !minic_c0_global_object_set_section(
                             parser->program, object_id, section_name, *section_name_length)) ||
        (*explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                          parser->program, object_id, *explicit_alignment))) {
        minic_parser_error(parser, "cannot persist static pointer array metadata");
        return false;
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_EQUAL, "expected '=' after static pointer array") ||
        !parse_static_scalar_array_transaction(
            parser, object_id, element_type, element_count, inferred_bound)) {
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static pointer array");
}
'''
# Fix the transient-type retirement expression with a saved exact type rather than
# embedding special ownership in the parser.  This textual adjustment keeps the
# generated C straightforward.
new = new.replace(
    '    MinicType object_type;\n    MinicGlobalObjectId object_id;',
    '    MinicType object_type;\n    MinicType declared_object_type;\n    MinicGlobalObjectId object_id;',
    1,
)
new = new.replace(
    '    existing_id = minic_parser_find_global_object_entity(parser, name_span);',
    '    declared_object_type = object_type;\n    existing_id = minic_parser_find_global_object_entity(parser, name_span);',
    1,
)
new = new.replace(
    '''        if (discard_declared &&
            !minic_c0_program_discard_last_array_type(parser->program, declared_array == NULL
                                                                           ? object_type
                                                                           : (MinicType){0})) {
            /* The generic discard helper requires the exact transient type; use the
             * saved declarator type below instead of silently leaking arena state. */''',
    '''        if (discard_declared &&
            !minic_c0_program_discard_last_array_type(parser->program, declared_object_type)) {''',
)
text = text[:start] + new + text[end:]
p.write_text(text)
print('materialized static pointer array tentative ownership')
