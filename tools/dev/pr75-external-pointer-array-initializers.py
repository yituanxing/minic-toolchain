#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

old = '''    if (parser == NULL || !minic_type_is_integer(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "external array definition requires an integer element type");
        return false;
    }
'''
new = '''    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "external array definition requires an integer or pointer element type");
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"external array element-kind guard: expected 1 match, found {text.count(old)}")
text = text.replace(old, new, 1)

anchor = '''    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array")) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
'''
pointer_branch = r'''    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array")) {
        return false;
    }

    if (minic_type_is_pointer(element_type)) {
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_LBRACE, "expected '{' in external pointer array initializer") ||
            !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
            return false;
        }
        initializer_count = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            MinicGlobalObjectId target_id;
            bool has_relocation;

            if (!inferred_bound && initializer_count >= element_count) {
                minic_parser_error(parser, "too many external pointer array initializers");
                return false;
            }
            target_id = MINIC_GLOBAL_OBJECT_INVALID;
            has_relocation = false;
            if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
                MinicSourceSpan literal_span;
                MinicType literal_type;
                MinicType literal_pointer_type;
                const MinicArrayType *literal_array;

                if (!minic_parser_create_string_literal_object(
                        parser, &target_id, &literal_type, &literal_span)) {
                    return false;
                }
                literal_array = minic_c0_program_array_type(parser->program, literal_type.array_type_id);
                if (literal_array == NULL || !minic_type_is_array(literal_type) ||
                    !minic_type_pointer_to(literal_array->element_type, &literal_pointer_type) ||
                    !minic_type_assignment_compatible(element_type, literal_pointer_type)) {
                    minic_parser_error(parser, "external pointer array string initializer type mismatch");
                    return false;
                }
                (void)literal_span;
                has_relocation = true;
            } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
                MinicType source_pointer_type;
                const MinicGlobalObject *target;

                target_id = minic_parser_find_global_object(parser, parser->current.span);
                target = target_id == MINIC_GLOBAL_OBJECT_INVALID
                             ? NULL
                             : minic_c0_program_global_object(parser->program, target_id);
                if (target == NULL) {
                    minic_parser_error(parser, "external pointer array initializer requires a known object");
                    return false;
                }
                if (minic_type_is_array(target->type)) {
                    const MinicArrayType *target_array;

                    target_array = minic_c0_program_array_type(parser->program, target->type.array_type_id);
                    if (target_array == NULL ||
                        !minic_type_pointer_to(target_array->element_type, &source_pointer_type)) {
                        minic_parser_error(parser, "cannot decay pointer array initializer object");
                        return false;
                    }
                } else if (!minic_type_pointer_to(target->type, &source_pointer_type)) {
                    minic_parser_error(parser, "cannot take address of pointer array initializer object");
                    return false;
                }
                if (!minic_type_assignment_compatible(element_type, source_pointer_type) ||
                    !minic_parser_advance(parser)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser, "external pointer array object initializer type mismatch");
                    }
                    return false;
                }
                has_relocation = true;
            } else {
                int64_t parsed;

                if (!minic_parser_parse_integer_constant_expression(parser, &parsed) || parsed != 0) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser,
                                           "external pointer array scalar initializer must be null");
                    }
                    return false;
                }
            }
            if (has_relocation &&
                !minic_c0_global_object_add_object_relocation(
                    parser->program, object_id, initializer_count, target_id)) {
                minic_parser_error(parser, "cannot record external pointer array relocation");
                return false;
            }
            initializer_count += 1U;

            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser,
                                   "expected ',' or '}' in external pointer array initializer");
                return false;
            }
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_RBRACE, "expected '}' after external pointer array initializer")) {
            return false;
        }
        if (initializer_count == 0U) {
            minic_parser_error(parser, "external pointer array requires at least one initializer");
            return false;
        }
        if (inferred_bound) {
            element_count = initializer_count;
            if (!minic_c0_program_complete_array_type(
                    parser->program, object->type, element_count)) {
                minic_parser_error(parser, "cannot infer external pointer array bound");
                return false;
            }
        }
        object->is_extern = false;
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external pointer array definition");
    }

    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
'''
if text.count(anchor) != 1:
    raise SystemExit(f"external pointer array insertion anchor: expected 1 match, found {text.count(anchor)}")
path.write_text(text.replace(anchor, pointer_branch, 1))
print("staged external pointer arrays with string/object/null relocations")
