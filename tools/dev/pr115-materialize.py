from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


path = "src/frontend/parser_statement.c"

replace_once(
    path,
    '''static bool parse_inferred_static_local_array(MinicParser *parser,\n                                              MinicType element_type,\n                                              MinicSourceSpan name_span) {\n''',
    '''static bool parse_inferred_static_local_array(MinicParser *parser,\n                                              MinicType element_type,\n                                              MinicSourceSpan name_span,\n                                              MinicGlobalObjectId *out_object_id) {\n''',
)
replace_once(
    path,
    '''        parser->program->global_objects[object_id].type = object_type;\n        parser->program->global_objects[object_id].is_read_only = minic_type_is_const(element_type);\n        return minic_parser_bind_scoped_global_object(parser, name_span, object_id);\n''',
    '''        parser->program->global_objects[object_id].type = object_type;\n        parser->program->global_objects[object_id].is_read_only = minic_type_is_const(element_type);\n        if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n            return false;\n        }\n        *out_object_id = object_id;\n        return true;\n''',
)
replace_once(
    path,
    '''        return true;\n    }\n\n    minic_parser_error(parser,\n                       "inferred static local array requires a string or brace initializer");\n''',
    '''        *out_object_id = object_id;\n        return true;\n    }\n\n    minic_parser_error(parser,\n                       "inferred static local array requires a string or brace initializer");\n''',
)

replace_once(
    path,
    '''static bool parse_static_local_record_initializer(MinicParser *parser,\n                                                  MinicType declared_type,\n                                                  MinicSourceSpan name_span) {\n''',
    '''static bool parse_static_local_record_initializer(MinicParser *parser,\n                                                  MinicType declared_type,\n                                                  MinicSourceSpan name_span,\n                                                  MinicGlobalObjectId *out_object_id) {\n''',
)
replace_once(
    path,
    '''    if (!minic_parser_expect(\n            parser, MINIC_TOKEN_RBRACE, "expected '}' after static record initializer") ||\n        !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n        return false;\n    }\n    return true;\n}\n\nstatic bool add_implicitly_zero_initialized_static_local''',
    '''    if (!minic_parser_expect(\n            parser, MINIC_TOKEN_RBRACE, "expected '}' after static record initializer") ||\n        !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n        return false;\n    }\n    *out_object_id = object_id;\n    return true;\n}\n\nstatic bool add_implicitly_zero_initialized_static_local''',
)

replace_once(
    path,
    '''static bool add_implicitly_zero_initialized_static_local(MinicParser *parser,\n                                                         MinicType declared_type,\n                                                         MinicSourceSpan name_span) {\n''',
    '''static bool add_implicitly_zero_initialized_static_local(MinicParser *parser,\n                                                         MinicType declared_type,\n                                                         MinicSourceSpan name_span,\n                                                         MinicGlobalObjectId *out_object_id) {\n''',
)
replace_once(
    path,
    '''    if (!minic_c0_program_add_global_object(parser->program,\n                                            symbol_name,\n                                            (size_t)symbol_length,\n                                            declared_type,\n                                            true,\n                                            minic_type_is_const(declared_type),\n                                            &object_id) ||\n        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||\n        !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n            minic_parser_error(parser, "cannot create implicit-zero static local storage");\n        }\n        return false;\n    }\n    return true;\n}\n\nstatic bool parse_static_local_array_declarator''',
    '''    if (!minic_c0_program_add_global_object(parser->program,\n                                            symbol_name,\n                                            (size_t)symbol_length,\n                                            declared_type,\n                                            true,\n                                            minic_type_is_const(declared_type),\n                                            &object_id) ||\n        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||\n        !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n            minic_parser_error(parser, "cannot create implicit-zero static local storage");\n        }\n        return false;\n    }\n    *out_object_id = object_id;\n    return true;\n}\n\nstatic bool parse_static_local_array_declarator''',
)

replace_once(
    path,
    '''static bool parse_static_local_array_declarator(MinicParser *parser, MinicType base_type) {\n''',
    '''static bool parse_static_local_array_declarator(MinicParser *parser,\n                                                MinicType base_type,\n                                                MinicGlobalObjectId *out_object_id) {\n''',
)
replace_once(
    path,
    '''    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {\n        return false;\n    }\n''',
    '''    if (out_object_id == NULL) {\n        return false;\n    }\n    *out_object_id = MINIC_GLOBAL_OBJECT_INVALID;\n    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {\n        return false;\n    }\n''',
)
replace_once(
    path,
    '''            return parse_inferred_static_local_array(parser, declared_type, name_span);\n''',
    '''            return parse_inferred_static_local_array(\n                parser, declared_type, name_span, out_object_id);\n''',
)
replace_once(
    path,
    '''            return add_implicitly_zero_initialized_static_local(parser, declared_type, name_span);\n''',
    '''            return add_implicitly_zero_initialized_static_local(\n                parser, declared_type, name_span, out_object_id);\n''',
)
replace_once(
    path,
    '''            return parse_static_local_record_initializer(parser, declared_type, name_span);\n''',
    '''            return parse_static_local_record_initializer(\n                parser, declared_type, name_span, out_object_id);\n''',
)
replace_once(
    path,
    '''            return true;\n        }\n\n        if (!parse_static_local_integer_constant(\n''',
    '''            *out_object_id = scalar_object_id;\n            return true;\n        }\n\n        if (!parse_static_local_integer_constant(\n''',
)
replace_once(
    path,
    '''        if ((scalar_value == 0 &&\n             !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id)) ||\n            (scalar_value != 0 && !minic_c0_global_object_add_initializer(\n                                      parser->program, scalar_object_id, scalar_value)) ||\n            !minic_parser_bind_scoped_global_object(parser, name_span, scalar_object_id)) {\n            minic_parser_error(parser, "cannot finalize static local integer storage");\n            return false;\n        }\n        return true;\n''',
    '''        if ((scalar_value == 0 &&\n             !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id)) ||\n            (scalar_value != 0 && !minic_c0_global_object_add_initializer(\n                                      parser->program, scalar_object_id, scalar_value)) ||\n            !minic_parser_bind_scoped_global_object(parser, name_span, scalar_object_id)) {\n            minic_parser_error(parser, "cannot finalize static local integer storage");\n            return false;\n        }\n        *out_object_id = scalar_object_id;\n        return true;\n''',
)
replace_once(
    path,
    '''    return minic_parser_bind_scoped_global_object(parser, name_span, object_id);\n}\n\nstatic bool consume_static_local_interleaved_attribute''',
    '''    if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n        return false;\n    }\n    *out_object_id = object_id;\n    return true;\n}\n\ntypedef struct MinicStaticLocalAttributeContext {\n    char section_name[256];\n    size_t section_name_length;\n    bool has_section;\n} MinicStaticLocalAttributeContext;\n\nstatic bool consume_static_local_interleaved_attribute''',
)

replace_once(
    path,
    '''    const MinicAttributeDescriptor *descriptor;\n\n    (void)opaque_context;\n    if (parser == NULL || attribute == NULL) {\n        return false;\n    }\n''',
    '''    MinicStaticLocalAttributeContext *context;\n    const MinicAttributeDescriptor *descriptor;\n\n    if (parser == NULL || attribute == NULL || opaque_context == NULL) {\n        return false;\n    }\n    context = (MinicStaticLocalAttributeContext *)opaque_context;\n''',
)
replace_once(
    path,
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {\n        return true;\n    }\n    minic_parser_error(\n''',
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {\n        return true;\n    }\n    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {\n        return minic_parser_apply_section_attribute(parser,\n                                                    attribute,\n                                                    context->section_name,\n                                                    sizeof(context->section_name),\n                                                    &context->section_name_length,\n                                                    &context->has_section);\n    }\n    minic_parser_error(\n''',
)

replace_once(
    path,
    '''static bool parse_static_local_declaration(MinicParser *parser) {\n    MinicType base_type;\n\n    if (parser->current_function == MINIC_FUNCTION_INVALID ||\n''',
    '''static bool parse_static_local_declaration(MinicParser *parser) {\n    MinicStaticLocalAttributeContext attributes;\n    MinicType base_type;\n\n    (void)memset(&attributes, 0, sizeof(attributes));\n    if (parser->current_function == MINIC_FUNCTION_INVALID ||\n''',
)
replace_once(
    path,
    '''        !minic_parser_parse_gnu_attribute_lists(\n            parser, consume_static_local_interleaved_attribute, NULL)) {\n''',
    '''        !minic_parser_parse_gnu_attribute_lists(\n            parser, consume_static_local_interleaved_attribute, &attributes)) {\n''',
)
replace_once(
    path,
    '''    for (;;) {\n        if (!parse_static_local_array_declarator(parser, base_type)) {\n            return false;\n        }\n        if (parser->current.kind != MINIC_TOKEN_COMMA) {\n''',
    '''    for (;;) {\n        MinicGlobalObjectId object_id;\n\n        if (!parse_static_local_array_declarator(parser, base_type, &object_id)) {\n            return false;\n        }\n        if (attributes.has_section &&\n            !minic_c0_global_object_set_section(parser->program,\n                                                object_id,\n                                                attributes.section_name,\n                                                attributes.section_name_length)) {\n            minic_parser_error(parser, "cannot apply GNU section to static local object");\n            return false;\n        }\n        if (parser->current.kind != MINIC_TOKEN_COMMA) {\n''',
)

fixture = Path("tests/compiler/c0/gnu_static_local_interleaved_attribute.c")
text = fixture.read_text()
old = '''static int scalar_value(void)\n{\n    static int __attribute__((__unused__)) value = 7;\n    return value;\n}\n'''
new = '''static int scalar_value(void)\n{\n    static int __attribute__((__unused__)) value = 7;\n    return value;\n}\n\nstatic int section_value(void)\n{\n    static _Bool __attribute__((__section__(".data..once"))) already_done;\n    static int __attribute__((section(".data.localpair"))) first, second;\n    already_done = 1;\n    first = 3;\n    second = 4;\n    return (int)already_done + first + second;\n}\n'''
if text.count(old) != 1:
    raise SystemExit("fixture scalar anchor mismatch")
text = text.replace(old, new, 1)
old = '''    return record_value() == 0 && scalar_value() == 7 ? 0 : 1;\n'''
new = '''    return record_value() == 0 && scalar_value() == 7 && section_value() == 8 ? 0 : 1;\n'''
if text.count(old) != 1:
    raise SystemExit("fixture main anchor mismatch")
fixture.write_text(text.replace(old, new, 1))

script = Path("tests/compiler/c0/run-gnu-static-local-interleaved-attribute.sh")
text = script.read_text()
old = '''grep -F '__minic_static_local_' "$work/output.s" >/dev/null\n'''
new = '''grep -F '__minic_static_local_' "$work/output.s" >/dev/null\ngrep -F '.section .data..once' "$work/output.s" >/dev/null\ntest "$(grep -c -F '.section .data.localpair' "$work/output.s")" -eq 2\n'''
if text.count(old) != 1:
    raise SystemExit("focused static symbol anchor mismatch")
text = text.replace(old, new, 1)
old = '''printf '%s\\n'   'PASS compiler/c0/gnu_static_local_interleaved_attribute placement=type-before-declarator unused=informational record-empty-init=zero scalar=preserved layout-bearing=fail-closed'\n'''
new = '''printf '%s\\n'   'PASS compiler/c0/gnu_static_local_interleaved_attribute placement=type-before-declarator unused=informational section=global-object declaration-wide=2 aligned=fail-closed'\n'''
if text.count(old) != 1:
    raise SystemExit("focused summary anchor mismatch")
script.write_text(text.replace(old, new, 1))
