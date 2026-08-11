#include "frontend/parser_internal.h"

#include <string.h>

static bool
record_has_field(const MinicParser *parser, const MinicRecord *record, MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, index);
        if (field != NULL && field->name_length == name_length &&
            memcmp(field->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return true;
        }
    }
    return false;
}

static bool parse_function_pointer_field_declarator(MinicParser *parser,
                                                    MinicType return_type,
                                                    MinicSourceSpan *name_span,
                                                    MinicType *field_type) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || field_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(parser, true, true, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer fields are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, field_type)) {
        minic_parser_error(parser, "cannot build function pointer field type");
        return false;
    }
    *name_span = declarator.name_span;
    return true;
}

static bool token_text_equals(const MinicParser *parser, MinicToken token, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || token.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(token.span);
    return strlen(text) == length &&
           memcmp(parser->source + token.span.begin.offset, text, length) == 0;
}

static bool parse_packed_record_attribute(MinicParser *parser, bool *is_packed) {
    if (parser == NULL || is_packed == NULL) {
        return false;
    }
    *is_packed = false;
    if (!token_text_equals(parser, parser->current, "__attribute__")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' in __attribute__")) {
        return false;
    }
    if (!minic_parser_current_attribute_is(
            parser, MINIC_ATTRIBUTE_PACKED, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "only packed record attribute is supported here");
        return false;
    }
    *is_packed = true;
    return minic_parser_advance(parser) &&
           minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after packed attribute") &&
           minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after __attribute__");
}

typedef struct MinicRecordFieldAttributeContext {
    size_t explicit_alignment;
} MinicRecordFieldAttributeContext;

static bool consume_record_field_attribute(MinicParser *parser,
                                           const MinicParsedAttribute *attribute,
                                           void *opaque_context) {
    MinicRecordFieldAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicRecordFieldAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FIELD)) {
        minic_parser_error(parser, "unsupported GNU record field attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "record field", &context->explicit_alignment);
    }
    minic_parser_error(parser, "unsupported GNU record field attribute");
    return false;
}

static bool parse_record_field_attributes(MinicParser *parser, size_t *explicit_alignment) {
    MinicRecordFieldAttributeContext context;

    if (parser == NULL || explicit_alignment == NULL) {
        return false;
    }
    context.explicit_alignment = *explicit_alignment;
    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_record_field_attribute, &context)) {
        return false;
    }
    *explicit_alignment = context.explicit_alignment;
    return true;
}

static bool parse_record_bit_field_width(MinicParser *parser,
                                         MinicType field_type,
                                         bool allow_zero,
                                         size_t *bit_width) {
    MinicConstValue width_value;
    MinicExpressionId width_expression;
    unsigned int type_bits;
    int64_t width;

    if (parser == NULL || bit_width == NULL || !minic_type_is_integer(field_type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "bit-field requires an integer type");
        }
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' before bit-field width") ||
        !minic_parser_parse_expression(parser, &width_expression, 0U) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, width_expression, &width_value) ||
        !minic_const_value_as_int64(parser->program, parser->target_info, &width_value, &width)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "bit-field width must be an integer constant expression");
        }
        return false;
    }
    if (!minic_target_info_integer_width(
            parser->target_info, parser->program, field_type, &type_bits)) {
        minic_parser_error(parser, "cannot determine target width of bit-field type");
        return false;
    }
    if (minic_type_is_bool_integer(field_type)) {
        type_bits = 1U;
    }
    if (width < 0 || (uint64_t)width > (uint64_t)type_bits || (!allow_zero && width == 0)) {
        minic_parser_error(parser,
                           allow_zero
                               ? "bit-field width exceeds its integer type"
                               : "named bit-field width must be positive and fit its integer type");
        return false;
    }
    *bit_width = (size_t)width;
    return true;
}

static bool parse_record_field_declarator(MinicParser *parser,
                                          MinicRecordId record_id,
                                          MinicType base_type,
                                          size_t declaration_alignment) {
    MinicSourceSpan name_span;
    MinicType field_type;
    size_t element_count;
    size_t explicit_alignment;
    MinicRecord *mutable_record;
    const MinicRecord *record;
    bool is_array;
    bool is_flexible_array;
    bool is_zero_length_array;

    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL) {
        minic_parser_error(parser, "invalid record while adding field");
        return false;
    }
    if (!minic_parser_parse_pointer_declarator(parser, base_type, &field_type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!parse_function_pointer_field_declarator(parser, field_type, &name_span, &field_type)) {
            return false;
        }
    } else {
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected record field name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    if (minic_type_is_void(field_type)) {
        minic_parser_error(parser, "record field cannot have void type");
        return false;
    }
    if (!minic_parser_require_complete_object_type(
            parser, field_type, "record field cannot use incomplete type by value")) {
        return false;
    }
    if (record_has_field(parser, record, name_span)) {
        minic_parser_error(parser, "duplicate record field");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        size_t bit_width;

        if (!parse_record_bit_field_width(parser, field_type, false, &bit_width) ||
            !minic_c0_record_add_bit_field(parser->program,
                                           record_id,
                                           parser->source + name_span.begin.offset,
                                           minic_parser_span_length(name_span),
                                           field_type,
                                           bit_width)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot add named bit-field");
            }
            return false;
        }
        return true;
    }

    element_count = 1U;
    explicit_alignment = declaration_alignment;
    is_array = false;
    is_flexible_array = false;
    is_zero_length_array = false;
    if (parser->current.kind != MINIC_TOKEN_LBRACKET && minic_type_is_array(field_type)) {
        const MinicArrayType *typedef_array;

        typedef_array = minic_c0_program_array_type(parser->program, field_type.array_type_id);
        if (typedef_array == NULL || typedef_array->element_count == 0U) {
            minic_parser_error(parser, "record field requires a complete typedef array type");
            return false;
        }
        field_type = typedef_array->element_type;
        element_count = typedef_array->element_count;
        is_array = true;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        is_array = true;
        size_t bounds[8];
        size_t bound_count;

        if (minic_type_is_pointer(field_type) && field_type.base_kind == MINIC_TYPE_BASE_FUNCTION) {
            minic_parser_error(parser, "function pointer field arrays are unsupported");
            return false;
        }
        bound_count = 0U;
        while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
                minic_parser_error(parser, "record field supports at most eight array dimensions");
                return false;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
                if (bound_count != 0U) {
                    minic_parser_error(parser,
                                       "only the outermost record array dimension may be flexible");
                    return false;
                }
                if (record->is_union) {
                    minic_parser_error(parser, "flexible array member is not allowed in union");
                    return false;
                }
                if (record->field_count == 0U) {
                    minic_parser_error(parser,
                                       "flexible array member requires a preceding named field");
                    return false;
                }
                is_flexible_array = true;
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
                    minic_parser_error(parser,
                                       "multidimensional flexible record arrays are unsupported");
                    return false;
                }
                break;
            }
            if (bound_count == 0U) {
                if (!minic_parser_parse_record_array_bound(
                        parser, &bounds[bound_count], &is_zero_length_array)) {
                    return false;
                }
            } else if (!minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
                return false;
            }
            bound_count += 1U;
        }

        if (!is_flexible_array && bound_count != 0U) {
            size_t dimension;

            element_count = bounds[0];
            dimension = bound_count;
            while (dimension > 1U) {
                dimension -= 1U;
                if (!minic_c0_program_add_array_type(
                        parser->program, field_type, bounds[dimension], &field_type)) {
                    minic_parser_error(parser, "cannot build multidimensional record array type");
                    return false;
                }
            }
        }
    }

    if (!parse_record_field_attributes(parser, &explicit_alignment)) {
        return false;
    }

    if (!minic_c0_record_add_field(parser->program,
                                   record_id,
                                   parser->source + name_span.begin.offset,
                                   minic_parser_span_length(name_span),
                                   field_type,
                                   element_count)) {
        minic_parser_error(parser, "out of memory while adding record field");
        return false;
    }
    mutable_record = &parser->program->records[record_id];
    mutable_record->fields[mutable_record->field_count - 1U].explicit_alignment =
        explicit_alignment;
    mutable_record->fields[mutable_record->field_count - 1U].is_array = is_array;
    mutable_record->fields[mutable_record->field_count - 1U].is_flexible_array = is_flexible_array;
    mutable_record->fields[mutable_record->field_count - 1U].is_zero_length_array =
        is_zero_length_array;
    return true;
}

typedef struct MinicRecordSuffixAttributeContext {
    MinicRecordId record_id;
    size_t explicit_alignment;
} MinicRecordSuffixAttributeContext;

static bool consume_record_suffix_attribute(MinicParser *parser,
                                            const MinicParsedAttribute *attribute,
                                            void *opaque_context) {
    MinicRecordSuffixAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;
    const MinicRecord *record;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicRecordSuffixAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    record = minic_c0_program_record(parser->program, context->record_id);
    if (descriptor == NULL || record == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "unsupported GNU record suffix attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "record", &context->explicit_alignment);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_DESIGNATED_INIT) {
        if (record->is_union) {
            minic_parser_error(parser, "GNU designated_init applies only to struct types");
            return false;
        }
        return true;
    }
    minic_parser_error(parser, "unsupported GNU record suffix attribute");
    return false;
}

static bool parse_record_suffix_attributes(MinicParser *parser,
                                           MinicRecordId record_id,
                                           size_t *explicit_alignment) {
    MinicRecordSuffixAttributeContext context;

    if (parser == NULL || explicit_alignment == NULL) {
        return false;
    }
    context.record_id = record_id;
    context.explicit_alignment = 0U;
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_record_suffix_attribute, &context)) {
        return false;
    }
    *explicit_alignment = context.explicit_alignment;
    return true;
}

static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
    MinicType base_type;
    const MinicRecord *record;
    size_t declaration_alignment;

    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL) {
        minic_parser_error(parser, "invalid record while adding field");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return minic_parser_advance(parser);
    }
    if (record->field_count > 0U && record->fields[record->field_count - 1U].is_flexible_array) {
        minic_parser_error(parser, "flexible array member must be the last record field");
        return false;
    }
    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    declaration_alignment = 0U;
    if (!parse_record_field_attributes(parser, &declaration_alignment)) {
        return false;
    }
    if (minic_type_is_record(base_type) && parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (declaration_alignment != 0U) {
            minic_parser_error(parser, "GNU alignment on anonymous record members is unsupported");
            return false;
        }
        MinicRecord *mutable_record;

        if (!minic_parser_require_complete_object_type(
                parser, base_type, "anonymous record member requires a complete type") ||
            !minic_c0_record_add_field(parser->program, record_id, "", 0U, base_type, 1U)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot add anonymous record member");
            }
            return false;
        }
        mutable_record = &parser->program->records[record_id];
        mutable_record->fields[mutable_record->field_count - 1U].is_anonymous_member = true;
        return minic_parser_advance(parser);
    }

    if (parser->current.kind == MINIC_TOKEN_COLON) {
        size_t bit_width;

        if (declaration_alignment != 0U) {
            minic_parser_error(parser, "GNU alignment on unnamed bit-fields is unsupported");
            return false;
        }
        if (!parse_record_bit_field_width(parser, base_type, true, &bit_width) ||
            !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after bit-field") ||
            !minic_c0_record_add_unnamed_bit_field(
                parser->program, record_id, base_type, bit_width)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot add unnamed bit-field");
            }
            return false;
        }
        return true;
    }

    for (;;) {
        if (!parse_record_field_declarator(parser, record_id, base_type, declaration_alignment)) {
            return false;
        }
        record = minic_c0_program_record(parser->program, record_id);
        if (record == NULL || record->field_count == 0U) {
            minic_parser_error(parser, "invalid record after adding field");
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            return minic_parser_expect(
                parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record field");
        }
        if (record->fields[record->field_count - 1U].is_flexible_array) {
            minic_parser_error(parser, "flexible array member must be the last record field");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
}

bool minic_parser_parse_record_definition_specifier(MinicParser *parser, MinicType *record_type) {
    MinicRecordId record_id;
    MinicTokenKind record_keyword;
    bool is_packed;
    bool is_union;

    if (record_type == NULL) {
        minic_parser_error(parser, "internal error: missing record type output");
        return false;
    }
    record_keyword = parser->current.kind;
    if (record_keyword != MINIC_TOKEN_KW_STRUCT && record_keyword != MINIC_TOKEN_KW_UNION) {
        minic_parser_error(parser, "expected record keyword");
        return false;
    }
    is_union = record_keyword == MINIC_TOKEN_KW_UNION;
    if (!minic_parser_advance(parser) || !parse_packed_record_attribute(parser, &is_packed)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicSourceSpan name_span;
        const MinicRecord *record;

        name_span = parser->current.span;
        record_id = minic_parser_find_record(parser, name_span);
        if (record_id == MINIC_RECORD_INVALID) {
            if (!minic_c0_program_add_record(parser->program,
                                             parser->source + name_span.begin.offset,
                                             minic_parser_span_length(name_span),
                                             &record_id)) {
                minic_parser_error(parser, "out of memory while adding record");
                return false;
            }
            parser->program->records[record_id].is_union = is_union;
            parser->program->records[record_id].is_packed = is_packed;
        } else {
            record = minic_c0_program_record(parser->program, record_id);
            if (record == NULL || record->is_complete || record->is_union != is_union ||
                (is_packed && record->is_packed != is_packed)) {
                minic_parser_error(parser, "duplicate record definition");
                return false;
            }
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        if (!minic_c0_program_add_anonymous_record(parser->program, &record_id)) {
            minic_parser_error(parser, "out of memory while adding anonymous record");
            return false;
        }
        parser->program->records[record_id].is_union = is_union;
        parser->program->records[record_id].is_packed = is_packed;
    } else {
        minic_parser_error(parser, "expected record tag or '{' after 'struct'");
        return false;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' after record specifier")) {
        return false;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of file");
            return false;
        }
        if (!parse_record_field(parser, record_id)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record fields")) {
        return false;
    }
    {
        size_t explicit_alignment;

        if (!parse_record_suffix_attributes(parser, record_id, &explicit_alignment)) {
            return false;
        }
        if (explicit_alignment != 0U) {
            parser->program->records[record_id].explicit_alignment = explicit_alignment;
        }
    }
    if (!minic_c0_program_finish_record(parser->program, record_id)) {
        minic_parser_error(parser, "record definition requires at least one field");
        return false;
    }
    *record_type = minic_type_record(record_id);
    return true;
}

bool minic_parser_parse_record_definition(MinicParser *parser) {
    MinicParser probe;
    MinicType record_type;
    bool is_forward_declaration;

    if (parser == NULL) {
        return false;
    }

    probe = *parser;
    is_forward_declaration = false;
    if (minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_IDENTIFIER &&
        minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_SEMICOLON) {
        is_forward_declaration = true;
    }

    if (is_forward_declaration) {
        return minic_parser_parse_type_specifiers(parser, &record_type) &&
               minic_type_is_record(record_type) &&
               minic_parser_expect(
                   parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record declaration");
    }

    return minic_parser_parse_record_definition_specifier(parser, &record_type) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record definition");
}
