#include "frontend/parser_internal.h"

#include <limits.h>
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

static bool record_field_starts_parenthesized_pointer_array(const MinicParser *parser) {
    MinicParser probe;
    size_t depth;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_STAR) {
        return false;
    }
    /* The initial '(' was consumed by the probe advance above, but it still
       owns one nesting level until the matching ')' after the field name. */
    depth = 1U;
    for (;;) {
        if (probe.current.kind == MINIC_TOKEN_LPAREN) {
            depth += 1U;
        } else if (probe.current.kind == MINIC_TOKEN_RPAREN) {
            if (depth == 0U) {
                return false;
            }
            depth -= 1U;
            if (depth == 0U) {
                return minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_LBRACKET;
            }
        }
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
}

static bool parse_pointer_to_array_field_declarator(MinicParser *parser,
                                                    MinicType base_type,
                                                    MinicSourceSpan *name_span,
                                                    MinicType *field_type) {
    unsigned int pointer_const_qualifiers;
    unsigned int pointer_volatile_qualifiers;
    MinicType type;
    size_t pointer_depth;
    size_t level;
    bool is_array;

    pointer_depth = 0U;
    pointer_const_qualifiers = 0U;
    pointer_volatile_qualifiers = 0U;
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before pointer-to-array field")) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        pointer_depth += 1U;
        if (pointer_depth > sizeof(unsigned int) * CHAR_BIT || !minic_parser_advance(parser) ||
            !minic_parser_parse_pointer_qualifier_sequence(
                parser, pointer_depth, &pointer_const_qualifiers, &pointer_volatile_qualifiers)) {
            return false;
        }
    }
    if (pointer_depth == 0U || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected pointer-to-array field name");
        return false;
    }
    *name_span = parser->current.span;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after pointer-to-array field") ||
        !minic_parser_parse_array_declarator_suffix(parser, base_type, true, &type, &is_array) ||
        !is_array) {
        return false;
    }
    for (level = 0U; level < pointer_depth; ++level) {
        unsigned int bit = 1U << level;
        if (!minic_type_pointer_to(type, &type)) {
            minic_parser_error(parser, "cannot build pointer-to-array field type");
            return false;
        }
        if ((pointer_const_qualifiers & bit) != 0U && !minic_type_add_const(type, &type)) {
            return false;
        }
        if ((pointer_volatile_qualifiers & bit) != 0U && !minic_type_add_volatile(type, &type)) {
            return false;
        }
    }
    *field_type = type;
    return true;
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
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, field_type)) {
        minic_parser_error(parser, "cannot build function pointer field type");
        return false;
    }
    *name_span = declarator.name_span;
    return true;
}

typedef struct MinicRecordFieldAttributeContext {
    MinicType field_type;
    size_t explicit_alignment;
    bool is_packed;
    bool has_field_type;
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
    if (descriptor == NULL) {
        minic_parser_error(parser, "unsupported GNU record field attribute");
        return false;
    }
    if (!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FIELD)) {
        bool function_parse_only;

        function_parse_only =
            context->has_field_type &&
            minic_type_is_pointer(context->field_type) &&
            context->field_type.base_kind == MINIC_TYPE_BASE_FUNCTION &&
            minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION) &&
            (descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
             descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
             descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
             descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW);
        if (function_parse_only) {
            return true;
        }
        minic_parser_error(parser, "unsupported GNU record field attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "record field", &context->explicit_alignment);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_PACKED &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_LAYOUT) {
        context->is_packed = true;
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_NONSTRING &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC) {
        return true;
    }
    if (descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW) {
        return true;
    }
    minic_parser_error(parser, "unsupported GNU record field attribute");
    return false;
}

static bool parse_typed_record_field_attributes(MinicParser *parser,
                                                MinicType field_type,
                                                size_t *explicit_alignment,
                                                bool *is_packed) {
    MinicRecordFieldAttributeContext context;

    if (parser == NULL || explicit_alignment == NULL || is_packed == NULL) {
        return false;
    }
    (void)memset(&context, 0, sizeof(context));
    context.field_type = field_type;
    context.explicit_alignment = *explicit_alignment;
    context.is_packed = *is_packed;
    context.has_field_type = true;
    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_record_field_attribute, &context)) {
        return false;
    }
    *explicit_alignment = context.explicit_alignment;
    *is_packed = context.is_packed;
    return true;
}

static bool
record_field_function_attribute_is_parse_only(const MinicAttributeDescriptor *descriptor) {
    return descriptor != NULL &&
           (descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
            descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
            descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
            descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW);
}

static bool apply_record_field_declaration_attributes(MinicParser *parser,
                                                      const MinicParsedAttributeList *attributes,
                                                      MinicType field_type,
                                                      size_t *explicit_alignment,
                                                      bool *is_packed) {
    MinicRecordFieldAttributeContext context;
    bool is_function_pointer;
    size_t index;

    if (parser == NULL || attributes == NULL || explicit_alignment == NULL || is_packed == NULL) {
        return false;
    }
    context.explicit_alignment = *explicit_alignment;
    context.is_packed = *is_packed;
    is_function_pointer =
        minic_type_is_pointer(field_type) && field_type.base_kind == MINIC_TYPE_BASE_FUNCTION;
    for (index = 0U; index < attributes->count; ++index) {
        const MinicParsedAttribute *attribute;
        const MinicAttributeDescriptor *descriptor;

        attribute = &attributes->values[index];
        descriptor = attribute->descriptor;
        if (descriptor != NULL &&
            minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FIELD)) {
            if (!consume_record_field_attribute(parser, attribute, &context)) {
                return false;
            }
            continue;
        }
        if (is_function_pointer && descriptor != NULL &&
            minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION) &&
            record_field_function_attribute_is_parse_only(descriptor)) {
            continue;
        }
        minic_parser_error(parser, "unsupported GNU declaration-head attribute on record field");
        return false;
    }
    *explicit_alignment = context.explicit_alignment;
    *is_packed = context.is_packed;
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
                                          const MinicParsedAttributeList *declaration_attributes,
                                          size_t declaration_alignment,
                                          bool declaration_packed) {
    MinicSourceSpan name_span;
    MinicType field_type;
    size_t element_count;
    size_t explicit_alignment;
    MinicRecord *mutable_record;
    const MinicRecord *record;
    bool is_array;
    bool is_packed;
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
    if (parser->current.kind == MINIC_TOKEN_LPAREN &&
        record_field_starts_parenthesized_pointer_array(parser)) {
        if (!parse_pointer_to_array_field_declarator(parser, field_type, &name_span, &field_type)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_STAR) {
            if (!parse_function_pointer_field_declarator(
                    parser, field_type, &name_span, &field_type)) {
                return false;
            }
        } else if (!minic_parser_parse_direct_declarator_name(parser, &name_span)) {
            return false;
        }
    } else if (!minic_parser_parse_direct_declarator_name(parser, &name_span)) {
        return false;
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
    is_packed = declaration_packed;
    is_array = false;
    is_flexible_array = false;
    is_zero_length_array = false;
    if (parser->current.kind != MINIC_TOKEN_LBRACKET && minic_type_is_array(field_type)) {
        const MinicArrayType *typedef_array;

        typedef_array = minic_c0_program_array_type(parser->program, field_type.array_type_id);
        if (typedef_array == NULL ||
            (typedef_array->element_count == 0U && !typedef_array->is_zero_length)) {
            minic_parser_error(parser, "record field requires a complete typedef array type");
            return false;
        }
        field_type = typedef_array->element_type;
        if (typedef_array->is_zero_length) {
            /* Direct GNU T field[0] uses one semantic element plus an explicit
               zero-length layout bit. Preserve the same representation when
               the zero-length array arrives through a typedef. */
            element_count = 1U;
            is_zero_length_array = true;
        } else {
            element_count = typedef_array->element_count;
        }
        is_array = true;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        is_array = true;
        size_t bounds[8];
        size_t bound_count;

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
                    MinicType nested_element_type;
                    bool nested_is_array;

                    nested_is_array = false;
                    if (!minic_parser_parse_array_declarator_suffix(
                            parser, field_type, false, &nested_element_type, &nested_is_array) ||
                        !nested_is_array || !minic_type_is_array(nested_element_type)) {
                        if (parser->diagnostic != NULL &&
                            parser->diagnostic->message[0] == '\0') {
                            minic_parser_error(
                                parser,
                                "cannot build fixed inner dimensions of flexible record array");
                        }
                        return false;
                    }
                    /* For T member[][N], the flexible outer dimension has
                       semantic element type T[N]. DataLayout already makes
                       the outer flexible dimension contribute zero sizeof. */
                    field_type = nested_element_type;
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

    if (!parse_typed_record_field_attributes(
            parser, field_type, &explicit_alignment, &is_packed) ||
        !apply_record_field_declaration_attributes(
            parser, declaration_attributes, field_type, &explicit_alignment, &is_packed)) {
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
    mutable_record->fields[mutable_record->field_count - 1U].is_packed = is_packed;
    mutable_record->fields[mutable_record->field_count - 1U].is_array = is_array;
    mutable_record->fields[mutable_record->field_count - 1U].is_flexible_array = is_flexible_array;
    mutable_record->fields[mutable_record->field_count - 1U].is_zero_length_array =
        is_zero_length_array;
    return true;
}

typedef struct MinicRecordTypeAttributeContext {
    size_t explicit_alignment;
    bool is_packed;
    bool is_union;
} MinicRecordTypeAttributeContext;

static bool consume_record_type_attribute(MinicParser *parser,
                                          const MinicParsedAttribute *attribute,
                                          void *opaque_context) {
    MinicRecordTypeAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicRecordTypeAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "unsupported GNU record type attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_PACKED &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_LAYOUT) {
        context->is_packed = true;
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "record", &context->explicit_alignment);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_DESIGNATED_INIT) {
        if (context->is_union) {
            minic_parser_error(parser, "GNU designated_init applies only to struct types");
            return false;
        }
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_MAY_ALIAS) {
        /* MiniC currently performs no strict-alias/TBAA optimization. Keeping
           may_alias as explicit type metadata recognition therefore preserves
           GCC's permissive aliasing contract without changing layout/codegen. */
        return true;
    }
    minic_parser_error(parser, "unsupported GNU record type attribute");
    return false;
}

static bool parse_record_type_attributes(MinicParser *parser,
                                         bool is_union,
                                         size_t *explicit_alignment,
                                         bool *is_packed) {
    MinicRecordTypeAttributeContext context;

    if (parser == NULL || explicit_alignment == NULL || is_packed == NULL) {
        return false;
    }
    context.explicit_alignment = *explicit_alignment;
    context.is_packed = *is_packed;
    context.is_union = is_union;
    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_record_type_attribute, &context)) {
        return false;
    }
    *explicit_alignment = context.explicit_alignment;
    *is_packed = context.is_packed;
    return true;
}

static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
    MinicParsedAttributeList declaration_attributes;
    MinicType base_type;
    const MinicRecord *record;
    size_t declaration_alignment;
    bool declaration_packed;

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
    (void)memset(&declaration_attributes, 0, sizeof(declaration_attributes));
    if (!minic_parser_collect_gnu_attribute_lists(parser, &declaration_attributes) ||
        !minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    /* Attributes between the declaration specifiers and the declarator cannot
       be routed until the declarator shape is known. In particular GCC writes
       function-pointer fields as
           T __attribute__((noreturn)) (*fn)(...);
       where noreturn belongs to the function declarator, not record layout.
       Defer these attributes and let the completed field type route them through
       apply_record_field_declaration_attributes(). */
    {
        MinicParsedAttributeList post_type_attributes;
        size_t attribute_index;

        (void)memset(&post_type_attributes, 0, sizeof(post_type_attributes));
        if (!minic_parser_collect_gnu_attribute_lists(parser, &post_type_attributes)) {
            return false;
        }
        if (post_type_attributes.count >
            MINIC_MAX_PARSED_ATTRIBUTES - declaration_attributes.count) {
            minic_parser_error(parser, "too many GNU record field declaration attributes");
            return false;
        }
        for (attribute_index = 0U; attribute_index < post_type_attributes.count;
             ++attribute_index) {
            declaration_attributes.values[declaration_attributes.count++] =
                post_type_attributes.values[attribute_index];
        }
    }
    declaration_alignment = 0U;
    declaration_packed = false;
    if (minic_type_is_record(base_type) && parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (declaration_attributes.count != 0U) {
            minic_parser_error(
                parser,
                "GNU declaration-head attributes on anonymous record members are unsupported");
            return false;
        }
        if (declaration_alignment != 0U || declaration_packed) {
            minic_parser_error(parser,
                               "GNU layout attributes on anonymous record members are unsupported");
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

    for (;;) {
        if (parser->current.kind == MINIC_TOKEN_COLON) {
            size_t bit_width;

            if (declaration_attributes.count != 0U) {
                minic_parser_error(
                    parser,
                    "GNU declaration-head attributes on unnamed bit-fields are unsupported");
                return false;
            }
            if (declaration_alignment != 0U || declaration_packed) {
                minic_parser_error(
                    parser, "GNU layout attributes on unnamed bit-fields are unsupported");
                return false;
            }
            if (!parse_record_bit_field_width(parser, base_type, true, &bit_width) ||
                !minic_c0_record_add_unnamed_bit_field(
                    parser->program, record_id, base_type, bit_width)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot add unnamed bit-field");
                }
                return false;
            }
        } else {
            if (!parse_record_field_declarator(parser,
                                               record_id,
                                               base_type,
                                               &declaration_attributes,
                                               declaration_alignment,
                                               declaration_packed)) {
                return false;
            }
            record = minic_c0_program_record(parser->program, record_id);
            if (record == NULL || record->field_count == 0U) {
                minic_parser_error(parser, "invalid record after adding field");
                return false;
            }
            if (record->fields[record->field_count - 1U].is_flexible_array &&
                parser->current.kind == MINIC_TOKEN_COMMA) {
                minic_parser_error(parser, "flexible array member must be the last record field");
                return false;
            }
        }

        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            return minic_parser_expect(
                parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record field");
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
}

bool minic_parser_parse_record_definition_specifier(MinicParser *parser, MinicType *record_type) {
    MinicRecordId record_id;
    MinicTokenKind record_keyword;
    size_t explicit_alignment;
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
    explicit_alignment = 0U;
    is_packed = parser->record_pack_alignment == 1U;
    if (!minic_parser_advance(parser) ||
        !parse_record_type_attributes(parser, is_union, &explicit_alignment, &is_packed)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicSourceSpan name_span;
        const MinicRecord *record;

        name_span = parser->current.span;
        record_id = minic_parser_find_record_in_current_scope(parser, name_span);
        if (record_id == MINIC_RECORD_INVALID) {
            if (!minic_c0_program_add_record(parser->program,
                                             parser->source + name_span.begin.offset,
                                             minic_parser_span_length(name_span),
                                             &record_id) ||
                !minic_parser_bind_record_tag(parser, name_span, record_id)) {
                minic_parser_error(parser, "out of memory while adding record");
                return false;
            }
            parser->program->records[record_id].is_union = is_union;
        } else {
            record = minic_c0_program_record(parser->program, record_id);
            if (record == NULL || record->is_complete || record->is_union != is_union) {
                minic_parser_error(parser, "duplicate record definition");
                return false;
            }
        }
        parser->program->records[record_id].pack_alignment = parser->record_pack_alignment;
        parser->program->records[record_id].is_packed =
            parser->program->records[record_id].is_packed || is_packed;
        if (explicit_alignment > parser->program->records[record_id].explicit_alignment) {
            parser->program->records[record_id].explicit_alignment = explicit_alignment;
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
        parser->program->records[record_id].pack_alignment = parser->record_pack_alignment;
        parser->program->records[record_id].explicit_alignment = explicit_alignment;
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
    if (!parse_record_type_attributes(parser, is_union, &explicit_alignment, &is_packed)) {
        return false;
    }
    parser->program->records[record_id].is_packed =
        parser->program->records[record_id].is_packed || is_packed;
    if (explicit_alignment > parser->program->records[record_id].explicit_alignment) {
        parser->program->records[record_id].explicit_alignment = explicit_alignment;
    }
    if (!minic_c0_program_finish_record(parser->program, record_id)) {
        minic_parser_error(parser, "record definition requires at least one field");
        return false;
    }
    *record_type = minic_type_record(record_id);
    return true;
}

static bool parse_record_forward_declaration(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicRecordId record_id;
    MinicTokenKind keyword;
    bool is_union;

    keyword = parser->current.kind;
    is_union = keyword == MINIC_TOKEN_KW_UNION;
    if ((keyword != MINIC_TOKEN_KW_STRUCT && keyword != MINIC_TOKEN_KW_UNION) ||
        !minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record tag in forward declaration");
        return false;
    }
    name_span = parser->current.span;
    record_id = minic_parser_find_record_in_current_scope(parser, name_span);
    if (record_id == MINIC_RECORD_INVALID) {
        if (!minic_c0_program_add_record(parser->program,
                                         parser->source + name_span.begin.offset,
                                         minic_parser_span_length(name_span),
                                         &record_id) ||
            !minic_parser_bind_record_tag(parser, name_span, record_id)) {
            minic_parser_error(parser, "cannot create forward record tag");
            return false;
        }
        parser->program->records[record_id].is_union = is_union;
    } else if (parser->program->records[record_id].is_union != is_union) {
        minic_parser_error(parser, "record tag kind does not match prior declaration");
        return false;
    }
    return minic_parser_advance(parser) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record declaration");
}

bool minic_parser_parse_record_definition(MinicParser *parser) {
    MinicParser probe;
    MinicType record_type;

    if (parser == NULL) {
        return false;
    }

    probe = *parser;
    if (minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_IDENTIFIER &&
        minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_SEMICOLON) {
        return parse_record_forward_declaration(parser);
    }

    return minic_parser_parse_record_definition_specifier(parser, &record_type) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record definition");
}
