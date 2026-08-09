#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:100]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/token.h",
    "    MINIC_TOKEN_KW_TYPEDEF,\n    MINIC_TOKEN_KW_STATIC,\n",
    "    MINIC_TOKEN_KW_TYPEDEF,\n    MINIC_TOKEN_KW_EXTERN,\n    MINIC_TOKEN_KW_STATIC,\n",
)

replace_once(
    "src/frontend/token.c",
    '''    case MINIC_TOKEN_KW_TYPEDEF:\n        return "typedef";\n    case MINIC_TOKEN_KW_STATIC:\n''',
    '''    case MINIC_TOKEN_KW_TYPEDEF:\n        return "typedef";\n    case MINIC_TOKEN_KW_EXTERN:\n        return "extern";\n    case MINIC_TOKEN_KW_STATIC:\n''',
)

replace_once(
    "src/frontend/lexer.c",
    '''    if (length == 7U && memcmp(text, "typedef", 7U) == 0) {\n        return MINIC_TOKEN_KW_TYPEDEF;\n    }\n    if (length == 6U && memcmp(text, "static", 6U) == 0) {\n''',
    '''    if (length == 7U && memcmp(text, "typedef", 7U) == 0) {\n        return MINIC_TOKEN_KW_TYPEDEF;\n    }\n    if (length == 6U && memcmp(text, "extern", 6U) == 0) {\n        return MINIC_TOKEN_KW_EXTERN;\n    }\n    if (length == 6U && memcmp(text, "static", 6U) == 0) {\n''',
)

replace_once(
    "src/frontend/ast.h",
    '''    bool is_internal;\n    bool is_read_only;\n    bool is_zero_initialized;\n} MinicGlobalObject;\n''',
    '''    bool is_internal;\n    bool is_read_only;\n    bool is_zero_initialized;\n    bool is_extern;\n} MinicGlobalObject;\n''',
)

replace_once(
    "src/frontend/ast.h",
    '''bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,\n                                                 MinicGlobalObjectId global_object_id);\n''',
    '''bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,\n                                                 MinicGlobalObjectId global_object_id);\nbool minic_c0_global_object_set_extern(MinicC0Program *program,\n                                       MinicGlobalObjectId global_object_id);\n''',
)

replace_once(
    "src/frontend/ast_global.c",
    '''bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,\n                                                 MinicGlobalObjectId global_object_id) {\n''',
    '''bool minic_c0_global_object_set_extern(MinicC0Program *program,\n                                       MinicGlobalObjectId global_object_id) {\n    MinicGlobalObject *object;\n\n    if (program == NULL || global_object_id >= program->global_object_count) {\n        return false;\n    }\n    object = &program->global_objects[global_object_id];\n    if (object->initializer_count != 0U || object->function_relocation_count != 0U ||\n        object->is_zero_initialized || object->is_internal) {\n        return false;\n    }\n    object->is_extern = true;\n    return true;\n}\n\nbool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,\n                                                 MinicGlobalObjectId global_object_id) {\n''',
)

replace_once(
    "src/frontend/parser_internal.h",
    '''bool minic_parser_parse_static_global(MinicParser *parser);\n''',
    '''bool minic_parser_parse_static_global(MinicParser *parser);\nbool minic_parser_parse_extern_global(MinicParser *parser);\n''',
)

parser_global = Path("src/frontend/parser_global.c")
text = parser_global.read_text()
marker = "bool minic_parser_parse_static_global(MinicParser *parser) {\n"
if text.count(marker) != 1:
    raise SystemExit("parser_global.c: unexpected static-global marker")
extern_parser = r'''bool minic_parser_parse_extern_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType object_type;
    MinicGlobalObjectId object_id;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_name(parser, &object_type)) {
        return false;
    }
    if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||
        minic_type_is_array(object_type)) {
        minic_parser_error(parser, "unsupported extern object type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected extern object name");
        return false;
    }
    name_span = parser->current.span;
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            false,
                                            minic_type_is_const(object_type),
                                            &object_id) ||
        !minic_c0_global_object_set_extern(parser->program, object_id) ||
        !minic_parser_advance(parser)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot declare extern object");
        }
        return false;
    }
    return minic_parser_expect(parser,
                               MINIC_TOKEN_SEMICOLON,
                               "expected ';' after extern object declaration");
}

'''
parser_global.write_text(text.replace(marker, extern_parser + marker, 1))

replace_once(
    "src/frontend/parser_function.c",
    '''        if (parser.current.kind == MINIC_TOKEN_KW_TYPEDEF) {\n            success = minic_parser_parse_typedef(&parser);\n        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC) {\n''',
    '''        if (parser.current.kind == MINIC_TOKEN_KW_TYPEDEF) {\n            success = minic_parser_parse_typedef(&parser);\n        } else if (parser.current.kind == MINIC_TOKEN_KW_EXTERN) {\n            success = minic_parser_parse_extern_global(&parser);\n        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC) {\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''            minic_type_is_function(object->type) ||\n            (object->is_zero_initialized && object->initializer_count != 0U) ||\n''',
    '''            minic_type_is_function(object->type) ||\n            (object->is_extern &&\n             (object->is_internal || object->is_zero_initialized ||\n              object->initializer_count != 0U || object->function_relocation_count != 0U)) ||\n            (object->is_zero_initialized && object->initializer_count != 0U) ||\n''',
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    '''    for (global_index = 0U; success && global_index < program->global_object_count;\n         ++global_index) {\n        success =\n            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);\n''',
    '''    for (global_index = 0U; success && global_index < program->global_object_count;\n         ++global_index) {\n        if (program->global_objects[global_index].is_extern) {\n            continue;\n        }\n        success =\n            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);\n''',
)

print("staged file-scope extern object declarations")
