from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {label}: {count}: {old[:120]!r}")
    return text.replace(old, new, 1)


def transform_region(text: str, start: str, end: str, edits, label: str) -> str:
    start_at = text.find(start)
    end_at = text.find(end, start_at + len(start))
    if start_at < 0 or end_at < 0:
        raise SystemExit(f"region mismatch {label}: start={start_at} end={end_at}")
    region = text[start_at:end_at]
    for index, (old, new) in enumerate(edits):
        region = replace_once(region, old, new, f"{label}[{index}]")
    return text[:start_at] + region + text[end_at:]


p = Path("src/frontend/parser_statement.c")
text = p.read_text()

text = transform_region(
    text,
    "static bool parse_inferred_static_local_array(",
    "static bool parse_static_local_record_initializer(",
    [
        (
            "static bool parse_inferred_static_local_array(MinicParser *parser,\n"
            "                                              MinicType element_type,\n"
            "                                              MinicSourceSpan name_span) {\n",
            "static bool parse_inferred_static_local_array(MinicParser *parser,\n"
            "                                              MinicType element_type,\n"
            "                                              MinicSourceSpan name_span,\n"
            "                                              MinicGlobalObjectId *out_object_id) {\n",
        ),
        (
            "        parser->program->global_objects[object_id].type = object_type;\n"
            "        parser->program->global_objects[object_id].is_read_only = minic_type_is_const(element_type);\n"
            "        return minic_parser_bind_scoped_global_object(parser, name_span, object_id);\n",
            "        parser->program->global_objects[object_id].type = object_type;\n"
            "        parser->program->global_objects[object_id].is_read_only = minic_type_is_const(element_type);\n"
            "        if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n"
            "            return false;\n"
            "        }\n"
            "        *out_object_id = object_id;\n"
            "        return true;\n",
        ),
        (
            "        return true;\n"
            "    }\n\n"
            "    minic_parser_error(parser,\n"
            "                       \"inferred static local array requires a string or brace initializer\");\n",
            "        *out_object_id = object_id;\n"
            "        return true;\n"
            "    }\n\n"
            "    minic_parser_error(parser,\n"
            "                       \"inferred static local array requires a string or brace initializer\");\n",
        ),
    ],
    "inferred-static-array",
)

text = transform_region(
    text,
    "static bool parse_static_local_record_initializer(",
    "static bool add_implicitly_zero_initialized_static_local(",
    [
        (
            "static bool parse_static_local_record_initializer(MinicParser *parser,\n"
            "                                                  MinicType declared_type,\n"
            "                                                  MinicSourceSpan name_span) {\n",
            "static bool parse_static_local_record_initializer(MinicParser *parser,\n"
            "                                                  MinicType declared_type,\n"
            "                                                  MinicSourceSpan name_span,\n"
            "                                                  MinicGlobalObjectId *out_object_id) {\n",
        ),
        (
            "    if (!minic_parser_expect(\n"
            "            parser, MINIC_TOKEN_RBRACE, \"expected '}' after static record initializer\") ||\n"
            "        !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n"
            "        return false;\n"
            "    }\n"
            "    return true;\n",
            "    if (!minic_parser_expect(\n"
            "            parser, MINIC_TOKEN_RBRACE, \"expected '}' after static record initializer\") ||\n"
            "        !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n"
            "        return false;\n"
            "    }\n"
            "    *out_object_id = object_id;\n"
            "    return true;\n",
        ),
    ],
    "static-record",
)

text = transform_region(
    text,
    "static bool add_implicitly_zero_initialized_static_local(",
    "static bool parse_static_local_array_declarator(",
    [
        (
            "static bool add_implicitly_zero_initialized_static_local(MinicParser *parser,\n"
            "                                                         MinicType declared_type,\n"
            "                                                         MinicSourceSpan name_span) {\n",
            "static bool add_implicitly_zero_initialized_static_local(MinicParser *parser,\n"
            "                                                         MinicType declared_type,\n"
            "                                                         MinicSourceSpan name_span,\n"
            "                                                         MinicGlobalObjectId *out_object_id) {\n",
        ),
        (
            "    return true;\n",
            "    *out_object_id = object_id;\n"
            "    return true;\n",
        ),
    ],
    "implicit-zero-static",
)

text = transform_region(
    text,
    "static bool parse_static_local_array_declarator(",
    "static bool consume_static_local_interleaved_attribute(",
    [
        (
            "static bool parse_static_local_array_declarator(MinicParser *parser, MinicType base_type) {\n",
            "static bool parse_static_local_array_declarator(MinicParser *parser,\n"
            "                                                MinicType base_type,\n"
            "                                                MinicGlobalObjectId *out_object_id) {\n",
        ),
        (
            "    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {\n"
            "        return false;\n"
            "    }\n",
            "    if (out_object_id == NULL) {\n"
            "        return false;\n"
            "    }\n"
            "    *out_object_id = MINIC_GLOBAL_OBJECT_INVALID;\n"
            "    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {\n"
            "        return false;\n"
            "    }\n",
        ),
        (
            "            return parse_inferred_static_local_array(parser, declared_type, name_span);\n",
            "            return parse_inferred_static_local_array(\n"
            "                parser, declared_type, name_span, out_object_id);\n",
        ),
        (
            "            return add_implicitly_zero_initialized_static_local(parser, declared_type, name_span);\n",
            "            return add_implicitly_zero_initialized_static_local(\n"
            "                parser, declared_type, name_span, out_object_id);\n",
        ),
        (
            "            return parse_static_local_record_initializer(parser, declared_type, name_span);\n",
            "            return parse_static_local_record_initializer(\n"
            "                parser, declared_type, name_span, out_object_id);\n",
        ),
        (
            "            return true;\n"
            "        }\n\n"
            "        if (!parse_static_local_integer_constant(\n",
            "            *out_object_id = scalar_object_id;\n"
            "            return true;\n"
            "        }\n\n"
            "        if (!parse_static_local_integer_constant(\n",
        ),
        (
            "        if ((scalar_value == 0 &&\n"
            "             !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id)) ||\n"
            "            (scalar_value != 0 && !minic_c0_global_object_add_initializer(\n"
            "                                      parser->program, scalar_object_id, scalar_value)) ||\n"
            "            !minic_parser_bind_scoped_global_object(parser, name_span, scalar_object_id)) {\n"
            "            minic_parser_error(parser, \"cannot finalize static local integer storage\");\n"
            "            return false;\n"
            "        }\n"
            "        return true;\n",
            "        if ((scalar_value == 0 &&\n"
            "             !minic_c0_global_object_set_zero_initialized(parser->program, scalar_object_id)) ||\n"
            "            (scalar_value != 0 && !minic_c0_global_object_add_initializer(\n"
            "                                      parser->program, scalar_object_id, scalar_value)) ||\n"
            "            !minic_parser_bind_scoped_global_object(parser, name_span, scalar_object_id)) {\n"
            "            minic_parser_error(parser, \"cannot finalize static local integer storage\");\n"
            "            return false;\n"
            "        }\n"
            "        *out_object_id = scalar_object_id;\n"
            "        return true;\n",
        ),
        (
            "    return minic_parser_bind_scoped_global_object(parser, name_span, object_id);\n",
            "    if (!minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n"
            "        return false;\n"
            "    }\n"
            "    *out_object_id = object_id;\n"
            "    return true;\n",
        ),
    ],
    "static-array-declarator",
)

insert_at = text.find("static bool consume_static_local_interleaved_attribute(")
if insert_at < 0:
    raise SystemExit("static-local attribute consumer missing")
context = (
    "typedef struct MinicStaticLocalAttributeContext {\n"
    "    char section_name[256];\n"
    "    size_t section_name_length;\n"
    "    bool has_section;\n"
    "} MinicStaticLocalAttributeContext;\n\n"
)
text = text[:insert_at] + context + text[insert_at:]

text = transform_region(
    text,
    "static bool consume_static_local_interleaved_attribute(",
    "static bool parse_static_local_declaration(",
    [
        (
            "    const MinicAttributeDescriptor *descriptor;\n\n"
            "    (void)opaque_context;\n"
            "    if (parser == NULL || attribute == NULL) {\n"
            "        return false;\n"
            "    }\n",
            "    MinicStaticLocalAttributeContext *context;\n"
            "    const MinicAttributeDescriptor *descriptor;\n\n"
            "    if (parser == NULL || attribute == NULL || opaque_context == NULL) {\n"
            "        return false;\n"
            "    }\n"
            "    context = (MinicStaticLocalAttributeContext *)opaque_context;\n",
        ),
        (
            "    if (descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&\n"
            "        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {\n"
            "        return true;\n"
            "    }\n",
            "    if (descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&\n"
            "        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {\n"
            "        return true;\n"
            "    }\n"
            "    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {\n"
            "        return minic_parser_apply_section_attribute(parser,\n"
            "                                                    attribute,\n"
            "                                                    context->section_name,\n"
            "                                                    sizeof(context->section_name),\n"
            "                                                    &context->section_name_length,\n"
            "                                                    &context->has_section);\n"
            "    }\n",
        ),
    ],
    "static-local-attribute-consumer",
)

text = transform_region(
    text,
    "static bool parse_static_local_declaration(",
    "static bool add_record_copy_assignments(",
    [
        (
            "static bool parse_static_local_declaration(MinicParser *parser) {\n"
            "    MinicType base_type;\n\n",
            "static bool parse_static_local_declaration(MinicParser *parser) {\n"
            "    MinicStaticLocalAttributeContext attributes;\n"
            "    MinicType base_type;\n\n"
            "    (void)memset(&attributes, 0, sizeof(attributes));\n",
        ),
        (
            "        !minic_parser_parse_gnu_attribute_lists(\n"
            "            parser, consume_static_local_interleaved_attribute, NULL)) {\n",
            "        !minic_parser_parse_gnu_attribute_lists(\n"
            "            parser, consume_static_local_interleaved_attribute, &attributes)) {\n",
        ),
        (
            "    for (;;) {\n"
            "        if (!parse_static_local_array_declarator(parser, base_type)) {\n"
            "            return false;\n"
            "        }\n",
            "    for (;;) {\n"
            "        MinicGlobalObjectId object_id;\n\n"
            "        if (!parse_static_local_array_declarator(parser, base_type, &object_id)) {\n"
            "            return false;\n"
            "        }\n"
            "        if (attributes.has_section &&\n"
            "            !minic_c0_global_object_set_section(parser->program,\n"
            "                                                object_id,\n"
            "                                                attributes.section_name,\n"
            "                                                attributes.section_name_length)) {\n"
            "            minic_parser_error(parser, \"cannot apply GNU section to static local object\");\n"
            "            return false;\n"
            "        }\n",
        ),
    ],
    "static-local-declaration",
)

p.write_text(text)

fixture = Path("tests/compiler/c0/gnu_static_local_interleaved_attribute.c")
text = fixture.read_text()
text = replace_once(
    text,
    "static int scalar_value(void)\n{\n"
    "    static int __attribute__((__unused__)) value = 7;\n"
    "    return value;\n"
    "}\n",
    "static int scalar_value(void)\n{\n"
    "    static int __attribute__((__unused__)) value = 7;\n"
    "    return value;\n"
    "}\n\n"
    "static int section_value(void)\n{\n"
    "    static _Bool __attribute__((__section__(\".data..once\"))) already_done;\n"
    "    static int __attribute__((section(\".data.localpair\"))) first, second;\n"
    "    already_done = 1;\n"
    "    first = 3;\n"
    "    second = 4;\n"
    "    return (int)already_done + first + second;\n"
    "}\n",
    "fixture-scalar",
)
text = replace_once(
    text,
    "    return record_value() == 0 && scalar_value() == 7 ? 0 : 1;\n",
    "    return record_value() == 0 && scalar_value() == 7 && section_value() == 8 ? 0 : 1;\n",
    "fixture-main",
)
fixture.write_text(text)

script = Path("tests/compiler/c0/run-gnu-static-local-interleaved-attribute.sh")
text = script.read_text()
text = replace_once(
    text,
    "grep -F '__minic_static_local_' \"$work/output.s\" >/dev/null\n",
    "grep -F '__minic_static_local_' \"$work/output.s\" >/dev/null\n"
    "grep -F '.section .data..once' \"$work/output.s\" >/dev/null\n"
    "test \"$(grep -c -F '.section .data.localpair' \"$work/output.s\")\" -eq 2\n",
    "focused-section-check",
)
text = replace_once(
    text,
    "printf '%s\\n'   'PASS compiler/c0/gnu_static_local_interleaved_attribute placement=type-before-declarator unused=informational record-empty-init=zero scalar=preserved layout-bearing=fail-closed'\n",
    "printf '%s\\n'   'PASS compiler/c0/gnu_static_local_interleaved_attribute placement=type-before-declarator unused=informational section=global-object declaration-wide=2 aligned=fail-closed'\n",
    "focused-summary",
)
script.write_text(text)
