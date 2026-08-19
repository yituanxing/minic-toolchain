#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one replacement, found {count}")
    p.write_text(text.replace(old, new, 1))


# 1) GNU flatten is an optimization-only function attribute.  Register it in
# the canonical attribute table so the existing function attribute consumer
# can treat it exactly like the other parse-only optimization attributes.
replace_once(
    "src/frontend/attribute.h",
    "    MINIC_ATTRIBUTE_NOINLINE,\n    MINIC_ATTRIBUTE_NOCLONE,\n    MINIC_ATTRIBUTE_EXTERNALLY_VISIBLE,",
    "    MINIC_ATTRIBUTE_NOINLINE,\n    MINIC_ATTRIBUTE_NOCLONE,\n    MINIC_ATTRIBUTE_FLATTEN,\n    MINIC_ATTRIBUTE_EXTERNALLY_VISIBLE,",
)

replace_once(
    "src/frontend/attribute.c",
    '''    {\n        "__noclone__",\n        sizeof("__noclone__") - 1U,\n        MINIC_ATTRIBUTE_NOCLONE,\n        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION,\n        0U,\n        0U,\n        true,\n    },\n    MINIC_ATTRIBUTE_ENTRY("externally_visible",''',
    '''    {\n        "__noclone__",\n        sizeof("__noclone__") - 1U,\n        MINIC_ATTRIBUTE_NOCLONE,\n        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION,\n        0U,\n        0U,\n        true,\n    },\n    MINIC_ATTRIBUTE_ENTRY("flatten",\n                          MINIC_ATTRIBUTE_FLATTEN,\n                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    MINIC_ATTRIBUTE_ENTRY("__flatten__",\n                          MINIC_ATTRIBUTE_FLATTEN,\n                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    MINIC_ATTRIBUTE_ENTRY("externally_visible",''',
)

# 2) The AST/call representation already has generic RV64 register+stack ABI
# placement.  Lift only the stale representation ceiling; no ABI special case.
replace_once(
    "src/frontend/ast.h",
    "#define MINIC_MAX_FUNCTION_PARAMETERS 32U",
    "#define MINIC_MAX_FUNCTION_PARAMETERS 64U",
)

# 3) Record-field declaration-head attributes need to be collected before the
# type specifier is known, then routed after the final declarator type exists.
# This reuses MinicParsedAttributeList and the existing field/function semantic
# classes rather than introducing a second attribute parser.
replace_once(
    "src/frontend/parser_record.c",
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_NONSTRING &&\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC) {\n        return true;\n    }\n    minic_parser_error(parser, "unsupported GNU record field attribute");''',
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_NONSTRING &&\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC) {\n        return true;\n    }\n    if (descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||\n        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW) {\n        return true;\n    }\n    minic_parser_error(parser, "unsupported GNU record field attribute");''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''    *explicit_alignment = context.explicit_alignment;\n    *is_packed = context.is_packed;\n    return true;\n}\n\nstatic bool parse_record_bit_field_width''',
    '''    *explicit_alignment = context.explicit_alignment;\n    *is_packed = context.is_packed;\n    return true;\n}\n\nstatic bool record_field_function_attribute_is_parse_only(\n    const MinicAttributeDescriptor *descriptor) {\n    return descriptor != NULL &&\n           (descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||\n            descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||\n            descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||\n            descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW);\n}\n\nstatic bool apply_record_field_declaration_attributes(\n    MinicParser *parser,\n    const MinicParsedAttributeList *attributes,\n    MinicType field_type,\n    size_t *explicit_alignment,\n    bool *is_packed) {\n    MinicRecordFieldAttributeContext context;\n    bool is_function_pointer;\n    size_t index;\n\n    if (parser == NULL || attributes == NULL || explicit_alignment == NULL || is_packed == NULL) {\n        return false;\n    }\n    context.explicit_alignment = *explicit_alignment;\n    context.is_packed = *is_packed;\n    is_function_pointer =\n        minic_type_is_pointer(field_type) && field_type.base_kind == MINIC_TYPE_BASE_FUNCTION;\n    for (index = 0U; index < attributes->count; ++index) {\n        const MinicParsedAttribute *attribute;\n        const MinicAttributeDescriptor *descriptor;\n\n        attribute = &attributes->values[index];\n        descriptor = attribute->descriptor;\n        if (descriptor != NULL &&\n            minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FIELD)) {\n            if (!consume_record_field_attribute(parser, attribute, &context)) {\n                return false;\n            }\n            continue;\n        }\n        if (is_function_pointer && descriptor != NULL &&\n            minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION) &&\n            record_field_function_attribute_is_parse_only(descriptor)) {\n            continue;\n        }\n        minic_parser_error(\n            parser, "unsupported GNU declaration-head attribute on record field");\n        return false;\n    }\n    *explicit_alignment = context.explicit_alignment;\n    *is_packed = context.is_packed;\n    return true;\n}\n\nstatic bool parse_record_bit_field_width''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''static bool parse_record_field_declarator(MinicParser *parser,\n                                          MinicRecordId record_id,\n                                          MinicType base_type,\n                                          size_t declaration_alignment,\n                                          bool declaration_packed) {''',
    '''static bool parse_record_field_declarator(\n    MinicParser *parser,\n    MinicRecordId record_id,\n    MinicType base_type,\n    const MinicParsedAttributeList *declaration_attributes,\n    size_t declaration_alignment,\n    bool declaration_packed) {''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''    if (!parse_record_field_attributes(parser, &explicit_alignment, &is_packed)) {\n        return false;\n    }\n\n    if (!minic_c0_record_add_field''',
    '''    if (!parse_record_field_attributes(parser, &explicit_alignment, &is_packed) ||\n        !apply_record_field_declaration_attributes(parser,\n                                                   declaration_attributes,\n                                                   field_type,\n                                                   &explicit_alignment,\n                                                   &is_packed)) {\n        return false;\n    }\n\n    if (!minic_c0_record_add_field''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {\n    MinicType base_type;''',
    '''static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {\n    MinicParsedAttributeList declaration_attributes;\n    MinicType base_type;''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {\n        return false;\n    }\n    declaration_alignment = 0U;''',
    '''    (void)memset(&declaration_attributes, 0, sizeof(declaration_attributes));\n    if (!minic_parser_collect_gnu_attribute_lists(parser, &declaration_attributes) ||\n        !minic_parser_parse_type_specifiers(parser, &base_type)) {\n        return false;\n    }\n    declaration_alignment = 0U;''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''    if (minic_type_is_record(base_type) && parser->current.kind == MINIC_TOKEN_SEMICOLON) {\n        if (declaration_alignment != 0U || declaration_packed) {''',
    '''    if (minic_type_is_record(base_type) && parser->current.kind == MINIC_TOKEN_SEMICOLON) {\n        if (declaration_attributes.count != 0U) {\n            minic_parser_error(\n                parser, "GNU declaration-head attributes on anonymous record members are unsupported");\n            return false;\n        }\n        if (declaration_alignment != 0U || declaration_packed) {''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''    if (parser->current.kind == MINIC_TOKEN_COLON) {\n        size_t bit_width;\n\n        if (declaration_alignment != 0U || declaration_packed) {''',
    '''    if (parser->current.kind == MINIC_TOKEN_COLON) {\n        size_t bit_width;\n\n        if (declaration_attributes.count != 0U) {\n            minic_parser_error(\n                parser, "GNU declaration-head attributes on unnamed bit-fields are unsupported");\n            return false;\n        }\n        if (declaration_alignment != 0U || declaration_packed) {''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''        if (!parse_record_field_declarator(\n                parser, record_id, base_type, declaration_alignment, declaration_packed)) {''',
    '''        if (!parse_record_field_declarator(parser,\n                                           record_id,\n                                           base_type,\n                                           &declaration_attributes,\n                                           declaration_alignment,\n                                           declaration_packed)) {''',
)

# 4) A forward alias may inherit a section from an earlier declaration.  The
# alias has no body, so require that any inherited section agrees with the
# eventual target instead of rejecting the mere presence of section metadata.
replace_once(
    "src/frontend/ast_verifier.c",
    '''    return true;\n}\n\nbool minic_c0_program_verify_target''',
    '''    return true;\n}\n\nstatic bool function_alias_section_matches(const MinicFunction *alias,\n                                           const MinicFunction *target) {\n    if (alias == NULL || target == NULL) {\n        return false;\n    }\n    if (alias->section_name == NULL) {\n        return true;\n    }\n    return target->section_name != NULL &&\n           alias->section_name_length == target->section_name_length &&\n           memcmp(alias->section_name, target->section_name, alias->section_name_length) == 0;\n}\n\nbool minic_c0_program_verify_target''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''            if (alias_target_function == NULL || !alias_target_function->is_defined ||\n                function->is_defined || function->section_name != NULL ||\n                !function_alias_signature_matches(program, function, alias_target_function)) {''',
    '''            if (alias_target_function == NULL || !alias_target_function->is_defined ||\n                function->is_defined ||\n                !function_alias_section_matches(function, alias_target_function) ||\n                !function_alias_signature_matches(program, function, alias_target_function)) {''',
)

# Keep the new focused gate in the permanent C0 suite.
replace_once(
    "tests/compiler/c0/run.sh",
    '''MINIC="$minic" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-gnu-function-copy-alias.sh"\n''',
    '''MINIC="$minic" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-gnu-function-copy-alias.sh"\n\nMINIC="$minic" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-linux-tail-batch3.sh"\n''',
)

print("materialized linux tail batch3")
