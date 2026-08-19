from pathlib import Path

root = Path(__file__).resolve().parents[2]
function_path = root / "src/frontend/parser_function.c"
statement_path = root / "src/frontend/parser_statement.c"

# File-scope external inferred scalar arrays already have canonical incomplete
# ArrayType + GlobalObject entities. Route braced initialization to the shared
# static-storage owner instead of the legacy pointer/integer positional parsers.
text = function_path.read_text()
anchor = '''    object = &parser->program->global_objects[object_id];
    if ((reused_existing && !minic_c0_global_object_begin_definition(parser->program, object_id)) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array")) {
        return false;
    }

    if (minic_type_is_pointer(element_type)) {
'''
replacement = '''    object = &parser->program->global_objects[object_id];
    if ((reused_existing && !minic_c0_global_object_begin_definition(parser->program, object_id)) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array")) {
        return false;
    }

    if (inferred_bound && parser->current.kind == MINIC_TOKEN_LBRACE) {
        MinicType object_type;

        object_type = object->type;
        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize inferred external scalar array");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external array definition");
    }

    if (minic_type_is_pointer(element_type)) {
'''
if anchor not in text:
    raise SystemExit("external inferred array routing anchor missing")
function_path.write_text(text.replace(anchor, replacement, 1))

text = statement_path.read_text()

# Inferred static locals: preserve the existing string-literal special case, but
# replace the brace-list mini-parser with the canonical incomplete ArrayType +
# shared static-storage initializer. This gives C99/GNU designators and typed
# relocations exactly the same owner as file-scope static arrays.
helper_start = text.index("static bool parse_inferred_static_local_array(")
brace_start = text.index("    if (parser->current.kind == MINIC_TOKEN_LBRACE) {\n", helper_start)
end_marker = '''    minic_parser_error(parser,
                       "inferred static local array requires a string or brace initializer");
'''
brace_end = text.index(end_marker, brace_start)
new_brace = r'''    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        char symbol_name[96];
        MinicType object_type;
        MinicGlobalObjectId object_id;
        int symbol_length;

        if (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) {
            minic_parser_error(
                parser,
                "brace-initialized inferred static array requires integer or pointer elements");
            return false;
        }
        symbol_length = snprintf(symbol_name,
                                 sizeof(symbol_name),
                                 "__minic_static_local_%zu_%zu",
                                 (size_t)parser->current_function,
                                 parser->program->global_object_count);
        if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name) ||
            !minic_c0_program_add_incomplete_array_type(
                parser->program, element_type, &object_type) ||
            !minic_c0_program_add_global_object(parser->program,
                                                symbol_name,
                                                (size_t)symbol_length,
                                                object_type,
                                                true,
                                                minic_type_is_const(element_type),
                                                &object_id) ||
            !minic_parser_parse_static_storage_initializer_value(
                parser, object_id, object_type) ||
            !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize inferred static local array");
            }
            return false;
        }
        *out_object_id = object_id;
        return true;
    }

'''
text = text[:brace_start] + new_brace + text[brace_end:]

# Fixed static locals: remove the one-dimensional integer-only initialized-array
# restriction and the positional integer mini-parser. The constructed canonical
# ArrayType already describes every dimension, so the shared owner can recurse.
restriction = '''    if (parser->current.kind == MINIC_TOKEN_EQUAL &&
        (bound_count != 1U || !minic_type_is_integer(declared_type))) {
        minic_parser_error(
            parser, "initialized static local array currently requires one integer dimension");
        return false;
    }
'''
if restriction not in text:
    raise SystemExit("fixed static-local restriction anchor missing")
text = text.replace(restriction, "", 1)

fixed_start = text.index("static bool parse_static_local_array_declarator(")
init_start = text.index("    if (parser->current.kind == MINIC_TOKEN_EQUAL) {\n", fixed_start)
zero_else = text.index(
    "    } else if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {\n",
    init_start,
)
new_init = r'''    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_static_storage_initializer_value(
                parser, object_id, object_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize static local array storage");
            }
            return false;
        }
'''
text = text[:init_start] + new_init + text[zero_else:]
statement_path.write_text(text)

print("materialized shared static-storage scalar-array initializer ownership")
