#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# A GNU aligned typedef is not a distinct C type for compatibility purposes, but
# it does carry object-layout metadata. Keep that metadata on the flattened type
# value so aliases can propagate it into object/record/array layout.
replace_once(
    "src/frontend/type.h",
    "    unsigned int pointer_volatile_qualifiers;\n    unsigned int pointer_depth;\n",
    "    unsigned int pointer_volatile_qualifiers;\n"
    "    unsigned int pointer_depth;\n"
    "    size_t explicit_alignment;\n",
    "type-alignment-field",
)
replace_once(
    "src/frontend/type.c",
    "    type.pointer_volatile_qualifiers = MINIC_TYPE_QUALIFIER_NONE;\n"
    "    type.pointer_depth = 0U;\n"
    "    return type;\n",
    "    type.pointer_volatile_qualifiers = MINIC_TYPE_QUALIFIER_NONE;\n"
    "    type.pointer_depth = 0U;\n"
    "    type.explicit_alignment = 0U;\n"
    "    return type;\n",
    "integer-type-alignment-init",
)
# The scalar constructor has the same tail; replace the remaining occurrence.
replace_once(
    "src/frontend/type.c",
    "    type.pointer_volatile_qualifiers = MINIC_TYPE_QUALIFIER_NONE;\n"
    "    type.pointer_depth = 0U;\n"
    "    return type;\n",
    "    type.pointer_volatile_qualifiers = MINIC_TYPE_QUALIFIER_NONE;\n"
    "    type.pointer_depth = 0U;\n"
    "    type.explicit_alignment = 0U;\n"
    "    return type;\n",
    "scalar-type-alignment-init",
)

# DataLayout remains the sole owner of object placement. Explicit typedef
# alignment is a minimum alignment and intentionally does not participate in
# type equality/assignment compatibility.
replace_once(
    "src/target/data_layout.c",
    "static bool minic_data_layout_type_depth(const MinicDataLayout *layout,\n",
    r'''static bool minic_data_layout_apply_explicit_alignment(MinicType type, size_t *alignment) {
    if (alignment == NULL) {
        return false;
    }
    if (type.pointer_depth != 0U || type.explicit_alignment == 0U) {
        return true;
    }
    if ((type.explicit_alignment & (type.explicit_alignment - 1U)) != 0U) {
        return false;
    }
    if (type.explicit_alignment > *alignment) {
        *alignment = type.explicit_alignment;
    }
    return true;
}

static bool minic_data_layout_type_depth(const MinicDataLayout *layout,
''',
    "data-layout-alignment-helper",
)
replace_once(
    "src/target/data_layout.c",
    "        *size = layout->integer_size[rank];\n"
    "        *alignment = layout->integer_alignment[rank];\n"
    "        return true;\n",
    "        *size = layout->integer_size[rank];\n"
    "        *alignment = layout->integer_alignment[rank];\n"
    "        return minic_data_layout_apply_explicit_alignment(type, alignment);\n",
    "integer-layout-alignment",
)
replace_once(
    "src/target/data_layout.c",
    "        *size = layout->float_size;\n"
    "        *alignment = layout->float_alignment;\n"
    "        return true;\n",
    "        *size = layout->float_size;\n"
    "        *alignment = layout->float_alignment;\n"
    "        return minic_data_layout_apply_explicit_alignment(type, alignment);\n",
    "float-layout-alignment",
)
replace_once(
    "src/target/data_layout.c",
    "        *size = layout->double_size;\n"
    "        *alignment = layout->double_alignment;\n"
    "        return true;\n",
    "        *size = layout->double_size;\n"
    "        *alignment = layout->double_alignment;\n"
    "        return minic_data_layout_apply_explicit_alignment(type, alignment);\n",
    "double-layout-alignment",
)
replace_once(
    "src/target/data_layout.c",
    "        *size = element_size * array_type->element_count;\n"
    "        *alignment = element_alignment;\n"
    "        return true;\n",
    "        *size = element_size * array_type->element_count;\n"
    "        *alignment = element_alignment;\n"
    "        return minic_data_layout_apply_explicit_alignment(type, alignment);\n",
    "array-layout-alignment",
)
replace_once(
    "src/target/data_layout.c",
    "        return minic_data_layout_record_depth(\n"
    "            layout, program, record, depth + 1U, SIZE_MAX, NULL, size, alignment);\n",
    "        if (!minic_data_layout_record_depth(\n"
    "                layout, program, record, depth + 1U, SIZE_MAX, NULL, size, alignment)) {\n"
    "            return false;\n"
    "        }\n"
    "        return minic_data_layout_apply_explicit_alignment(type, alignment);\n",
    "record-layout-alignment",
)

# Replace the old Linux-discovery bridge. The argument is a real integer
# constant expression, and the resulting minimum alignment propagates with the
# typedef's type value. Reduction and pointer-typedef alignment remain explicit
# capability boundaries until the flattened pointer representation can model
# per-layer alignment without ambiguity.
p = Path("src/frontend/parser_typedef.c")
text = p.read_text()
start = text.index("static bool parse_redundant_typedef_alignment(")
end = text.index("\nbool minic_parser_parse_typedef", start)
replacement = r'''static bool parse_typedef_alignment(MinicParser *parser, MinicType *aliased_type) {
    int64_t alignment_value;
    size_t natural_size;
    size_t natural_alignment;
    size_t alignment;

    if (parser == NULL || aliased_type == NULL ||
        (!typedef_token_text_equals(parser, "__attribute__") &&
         !typedef_token_text_equals(parser, "__attribute"))) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after typedef __attribute__") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '((' in typedef __attribute__")) {
        return false;
    }
    if (!minic_parser_current_attribute_is(
            parser, MINIC_ATTRIBUTE_ALIGNED, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "unsupported GNU typedef attribute");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after typedef aligned") ||
        !minic_parser_parse_integer_constant_expression(parser, &alignment_value) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after typedef alignment") ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in typedef attribute") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected second ')' in typedef attribute")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "typedef alignment requires an integer constant expression");
        }
        return false;
    }
    if (alignment_value <= 0 || (uint64_t)alignment_value > (uint64_t)SIZE_MAX) {
        minic_parser_error(parser, "typedef alignment must be a positive target-size value");
        return false;
    }
    alignment = (size_t)alignment_value;
    if ((alignment & (alignment - 1U)) != 0U) {
        minic_parser_error(parser, "typedef alignment must be a power of two");
        return false;
    }
    if (minic_type_is_pointer(*aliased_type)) {
        minic_parser_error(parser, "aligned pointer typedefs require per-layer type attributes");
        return false;
    }
    if (!minic_data_layout_type(minic_target_info_data_layout(parser->target_info),
                                parser->program,
                                *aliased_type,
                                &natural_size,
                                &natural_alignment)) {
        minic_parser_error(parser, "cannot determine natural typedef alignment");
        return false;
    }
    (void)natural_size;
    if (alignment < natural_alignment) {
        minic_parser_error(parser, "reducing GNU typedef alignment is not supported yet");
        return false;
    }
    aliased_type->explicit_alignment = alignment;
    return true;
}
'''
p.write_text(text[:start] + replacement + text[end:])
replace_once(
    "src/frontend/parser_typedef.c",
    "    if (!parse_redundant_typedef_alignment(parser, aliased_type)) {\n",
    "    if (!parse_typedef_alignment(parser, &aliased_type)) {\n",
    "typedef-alignment-call",
)
