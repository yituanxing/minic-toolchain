#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


# Parse the minimal standard external-definition shape currently driven by linenoise:
# `T *name = "literal";`. Reuse an earlier compatible extern declaration if present.
path = Path("src/frontend/parser_function.c")
text = path.read_text()
marker = "static bool parse_function(MinicParser *parser, bool is_internal) {\n"
if text.count(marker) != 1:
    raise SystemExit("parser_function.c: parse_function marker mismatch")
helper = r'''static bool parse_external_pointer_definition(MinicParser *parser,
                                              MinicType object_type,
                                              MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId target_id;
    MinicSourceSpan literal_span;
    MinicType literal_type;
    MinicType literal_pointer_type;
    const MinicArrayType *literal_array;
    MinicGlobalObject *object;

    if (parser == NULL || !minic_type_is_pointer(object_type) ||
        parser->current.kind != MINIC_TOKEN_EQUAL) {
        minic_parser_error(parser, "unsupported external object definition");
        return false;
    }

    object_id = minic_parser_find_global_object(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                false,
                                                minic_type_is_const(object_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external object definition");
            return false;
        }
    } else {
        object = &parser->program->global_objects[object_id];
        if (!object->is_extern || !minic_type_equal(object->type, object_type) ||
            object->initializer_count != 0U || object->function_relocation_count != 0U ||
            object->object_relocation_count != 0U || object->is_zero_initialized) {
            minic_parser_error(parser, "conflicting external object definition");
            return false;
        }
        object->is_extern = false;
    }

    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
        !minic_parser_create_string_literal_object(
            parser, &target_id, &literal_type, &literal_span)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "external pointer definition requires a string literal initializer");
        }
        return false;
    }
    literal_array = minic_c0_program_array_type(parser->program, literal_type.array_type_id);
    if (literal_array == NULL || !minic_type_is_array(literal_type) ||
        !minic_type_pointer_to(literal_array->element_type, &literal_pointer_type) ||
        !minic_type_assignment_compatible(object_type, literal_pointer_type) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
        !minic_c0_global_object_add_object_relocation(parser->program, object_id, 0U, target_id)) {
        minic_parser_error(parser, "external pointer initializer type mismatch");
        return false;
    }
    (void)literal_span;
    return minic_parser_expect(parser,
                               MINIC_TOKEN_SEMICOLON,
                               "expected ';' after external object definition");
}

'''
text = text.replace(marker, helper + marker, 1)

old = r'''    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_parameter_list(
            parser, parameter_name_spans, parameter_types, &parameter_count, true, &is_variadic) ||
'''
# pr73-unnamed-prototype-parameters.py has changed require_names=true to false before this script.
if old not in text:
    old = r'''    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_parameter_list(
            parser, parameter_name_spans, parameter_types, &parameter_count, false, &is_variadic) ||
'''
new = r'''    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (!is_internal && parser->current.kind != MINIC_TOKEN_LPAREN) {
        return parse_external_pointer_definition(parser, return_type, name_span);
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_parameter_list(
            parser, parameter_name_spans, parameter_types, &parameter_count, false, &is_variadic) ||
'''
if text.count(old) != 1:
    raise SystemExit("parser_function.c: function declarator prefix mismatch")
path.write_text(text.replace(old, new, 1))

# Generalize the object-relocation emitter created by pr73-static-pointer-array.py from
# pointer arrays to either a pointer scalar (one slot) or a pointer array (N slots).
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
start_marker = "static bool emit_object_relocs(FILE *file,\n"
end_marker = "static bool minic_riscv64_emit_global_object(FILE *file,\n"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("codegen_function.c: object relocation emitter boundaries not found")
emitter = r'''static bool emit_object_relocs(FILE *file,
                               const MinicC0Program *program,
                               const MinicGlobalObject *object) {
    size_t element_count;
    size_t cursor_element;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || !object->is_zero_initialized ||
        object->object_relocation_count == 0U || object->initializer_count != 0U ||
        object->function_relocation_count != 0U) {
        return false;
    }
    if (minic_type_is_pointer(object->type)) {
        element_count = 1U;
    } else if (minic_type_is_array(object->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        if (array_type == NULL || !minic_type_is_pointer(array_type->element_type)) {
            return false;
        }
        element_count = array_type->element_count;
    } else {
        return false;
    }
    if (element_count == 0U || element_count > SIZE_MAX / 8U ||
        object->storage_size != element_count * 8U) {
        return false;
    }

    cursor_element = 0U;
    for (relocation_index = 0U; relocation_index < object->object_relocation_count;
         ++relocation_index) {
        const MinicGlobalObjectRelocation *relocation;
        const MinicGlobalObject *target;

        relocation = &object->object_relocations[relocation_index];
        target = minic_c0_program_global_object(program, relocation->target_object_id);
        if (target == NULL || target->name_length == 0U ||
            relocation->element_index < cursor_element || relocation->element_index >= element_count ||
            !minic_riscv64_emit_zero_bytes(
                file, (relocation->element_index - cursor_element) * 8U) ||
            fprintf(file, "  .dword %s\n", target->name) < 0) {
            return false;
        }
        cursor_element = relocation->element_index + 1U;
    }
    return cursor_element <= element_count &&
           minic_riscv64_emit_zero_bytes(file, (element_count - cursor_element) * 8U);
}

'''
path.write_text(text[:start] + emitter + text[end:])

print("staged external pointer definitions with string object relocations")
