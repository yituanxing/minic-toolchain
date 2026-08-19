#!/usr/bin/env python3
from pathlib import Path

p = Path('src/frontend/parser_global.c')
text = p.read_text()
start = text.index('static bool parse_static_record_array(')
end = text.index('\nstatic bool parse_static_record(', start)

new = r'''static bool static_record_array_declaration_compatible(const MinicC0Program *program,
                                                       MinicType existing_type,
                                                       MinicType element_type,
                                                       size_t declared_count,
                                                       bool declared_incomplete) {
    const MinicArrayType *existing_array;

    if (program == NULL || !minic_type_is_array(existing_type)) {
        return false;
    }
    existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
    if (existing_array == NULL ||
        !minic_type_equal(existing_array->element_type, element_type)) {
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
    MinicType object_type;
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId existing_id;
    size_t declared_count;
    bool inferred_bound;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {
        return false;
    }
    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        minic_parser_error(parser, "static record array requires a complete non-empty struct type");
        return false;
    }

    declared_count = 0U;
    inferred_bound = false;
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
        minic_parser_error(parser, "multi-dimensional static record arrays are not supported yet");
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
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;

        if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
            if ((inferred_bound &&
                 !minic_c0_program_add_incomplete_array_type(
                     parser->program, element_type, &object_type)) ||
                (!inferred_bound &&
                 !minic_c0_program_add_array_type(
                     parser->program, element_type, declared_count, &object_type)) ||
                !minic_c0_program_add_tentative_global_object(
                    parser->program,
                    parser->source + name_span.begin.offset,
                    minic_parser_span_length(name_span),
                    object_type,
                    true,
                    minic_type_is_const(element_type),
                    &object_id)) {
                minic_parser_error(parser, "cannot create static record array tentative definition");
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
        if ((*has_section && !minic_c0_global_object_set_section(
                                 parser->program, object_id, section_name, *section_name_length)) ||
            (*explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                              parser->program, object_id, *explicit_alignment))) {
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
        if (declared_count == 0U) {
            minic_parser_error(parser,
                               "cannot infer static record array bound from an empty initializer");
            return false;
        }
    }

    if (existing_id != MINIC_GLOBAL_OBJECT_INVALID) {
        MinicGlobalObject *existing;
        const MinicArrayType *existing_array;

        existing = &parser->program->global_objects[existing_id];
        existing_array = minic_type_is_array(existing->type)
                             ? minic_c0_program_array_type(
                                   parser->program, existing->type.array_type_id)
                             : NULL;
        if (existing_array == NULL || !existing->is_internal || !existing->is_tentative ||
            !static_record_array_declaration_compatible(parser->program,
                                                        existing->type,
                                                        element_type,
                                                        declared_count,
                                                        false) ||
            (existing_array->element_count == 0U && !existing_array->is_zero_length &&
             !minic_c0_program_complete_array_type(
                 parser->program, existing->type, declared_count)) ||
            !minic_c0_global_object_begin_definition(parser->program, existing_id)) {
            minic_parser_error(parser, "conflicting static record array definition");
            return false;
        }
        object_id = existing_id;
        object_type = parser->program->global_objects[object_id].type;
    } else {
        if (!minic_c0_program_add_array_type(
                parser->program, element_type, declared_count, &object_type) ||
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
    if ((*has_section && !minic_c0_global_object_set_section(
                             parser->program, object_id, section_name, *section_name_length)) ||
        (*explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                          parser->program, object_id, *explicit_alignment)) ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse static record array initializer");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static record array");
}
'''

text = text[:start] + new + text[end:]
p.write_text(text)

run = Path('tests/compiler/c0/run.sh')
run_text = run.read_text()
if 'run-linux-tail-batch6.sh' not in run_text:
    run.write_text(run_text.rstrip() + '''\n\nMINIC="$minic" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-linux-tail-batch6.sh"\n''')

print('materialized static record array tentative lifecycle')
