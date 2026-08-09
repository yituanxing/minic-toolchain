#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


# Reuse the same token-level null-pointer parser already used by static globals.
# This keeps global and function-scope static pointer initialization semantics aligned.
replace_once(
    "src/frontend/parser_internal.h",
    "bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value);\n",
    "bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value);\n"
    "bool minic_parser_parse_zero_pointer_constant(MinicParser *parser);\n",
    "zero pointer parser declaration",
)

path = Path("src/frontend/parser_global.c")
text = path.read_text()
old = "static bool parse_zero_pointer_constant(MinicParser *parser) {"
if text.count(old) != 1:
    raise SystemExit(f"zero pointer parser definition: expected 1 match, found {text.count(old)}")
text = text.replace(old, "bool minic_parser_parse_zero_pointer_constant(MinicParser *parser) {", 1)
text = text.replace("parse_zero_pointer_constant(parser)",
                    "minic_parser_parse_zero_pointer_constant(parser)")
path.write_text(text)

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
anchor = text.index("static bool parse_static_local_array_declarator(MinicParser *parser, MinicType base_type) {\n")
start = text.index("    if (bound_count == 0U) {\n", anchor)
end = text.index("    if (parser->current.kind == MINIC_TOKEN_EQUAL) {\n", start)

replacement = r'''    if (bound_count == 0U) {
        char scalar_symbol_name[96];
        MinicGlobalObjectId scalar_object_id;
        MinicExpressionId scalar_initializer_id;
        int scalar_value;
        int scalar_symbol_length;

        if (parser->current.kind != MINIC_TOKEN_EQUAL) {
            minic_parser_error(parser,
                               "static local object currently requires an initializer or fixed array declarator");
            return false;
        }
        if (minic_type_is_record(declared_type)) {
            return parse_static_local_record_initializer(parser, declared_type, name_span);
        }
        if (!minic_type_is_integer(declared_type) && !minic_type_is_pointer(declared_type)) {
            minic_parser_error(parser,
                               "static local scalar currently requires an integer or pointer type");
            return false;
        }

        scalar_symbol_length = snprintf(scalar_symbol_name,
                                        sizeof(scalar_symbol_name),
                                        "__minic_static_local_%zu_%zu",
                                        (size_t)parser->current_function,
                                        parser->program->global_object_count);
        if (scalar_symbol_length <= 0 ||
            (size_t)scalar_symbol_length >= sizeof(scalar_symbol_name)) {
            minic_parser_error(parser, "cannot build static local scalar symbol name");
            return false;
        }
        if (!minic_c0_program_add_global_object(parser->program,
                                                scalar_symbol_name,
                                                (size_t)scalar_symbol_length,
                                                declared_type,
                                                true,
                                                minic_type_is_const(declared_type),
                                                &scalar_object_id) ||
            !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static scalar")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot begin static local scalar initializer");
            }
            return false;
        }

        if (minic_type_is_pointer(declared_type)) {
            if (!minic_parser_parse_zero_pointer_constant(parser) ||
                !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id) ||
                !minic_parser_bind_static_local(parser, name_span, scalar_object_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot finalize static local null pointer storage");
                }
                return false;
            }
            return true;
        }

        if (!minic_parser_parse_expression(parser, &scalar_initializer_id, 0U) ||
            !static_record_integer_constant(
                parser->program, scalar_initializer_id, &scalar_value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "static local integer requires a supported constant initializer");
            }
            return false;
        }
        if ((scalar_value == 0 &&
             !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id)) ||
            (scalar_value != 0 &&
             !minic_c0_global_object_add_initializer(
                 parser->program, scalar_object_id, scalar_value)) ||
            !minic_parser_bind_static_local(parser, name_span, scalar_object_id)) {
            minic_parser_error(parser, "cannot finalize static local integer storage");
            return false;
        }
        return true;
    }
'''

path.write_text(text[:start] + replacement + text[end:])
print("staged static local integer/null-pointer scalars as internal global storage")
