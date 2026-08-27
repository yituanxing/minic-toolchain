#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

MinicTypeAliasId minic_parser_find_type_alias(const MinicParser *parser,
                                              MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    if (minic_parser_name_bound(parser, name_span)) {
        return MINIC_TYPE_ALIAS_INVALID;
    }

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->type_alias_count; ++index) {
        const MinicTypeAlias *alias;

        alias = minic_c0_program_type_alias(parser->program, index);
        if (alias != NULL && alias->name_length == name_length &&
            memcmp(alias->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_TYPE_ALIAS_INVALID;
}

static bool parse_parenthesized_function_typedef(MinicParser *parser,
                                                 MinicType return_type,
                                                 MinicSourceSpan *name_span,
                                                 MinicType *aliased_type) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || aliased_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(parser, true, false, &declarator)) {
        return false;
    }
    if (declarator.attributes.count != 0U) {
        minic_parser_error(
            parser,
            "GNU attributes inside function pointer typedef declarators are not implemented yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, aliased_type)) {
        minic_parser_error(parser, "cannot build function pointer typedef type");
        return false;
    }
    *name_span = declarator.name_span;
    return true;
}

typedef struct MinicTypedefAttributeContext {
    MinicType *aliased_type;
} MinicTypedefAttributeContext;

static bool typedef_attribute_mode_name(const MinicParser *parser,
                                        const MinicParsedAttribute *attribute,
                                        const char **name,
                                        size_t *name_length) {
    size_t begin;
    size_t end;

    if (parser == NULL || attribute == NULL || name == NULL || name_length == NULL ||
        !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    begin = attribute->arguments_span.begin.offset + 1U;
    end = attribute->arguments_span.end.offset - 1U;
    while (begin < end &&
           (parser->source[begin] == ' ' || parser->source[begin] == '\t' ||
            parser->source[begin] == '\n' || parser->source[begin] == '\r' ||
            parser->source[begin] == '\f' || parser->source[begin] == '\v')) {
        begin += 1U;
    }
    while (end > begin &&
           (parser->source[end - 1U] == ' ' || parser->source[end - 1U] == '\t' ||
            parser->source[end - 1U] == '\n' || parser->source[end - 1U] == '\r' ||
            parser->source[end - 1U] == '\f' || parser->source[end - 1U] == '\v')) {
        end -= 1U;
    }
    if (end <= begin) {
        return false;
    }
    if (end - begin > 4U && parser->source[begin] == '_' &&
        parser->source[begin + 1U] == '_' && parser->source[end - 2U] == '_' &&
        parser->source[end - 1U] == '_') {
        begin += 2U;
        end -= 2U;
    }
    *name = parser->source + begin;
    *name_length = end - begin;
    return true;
}

static bool typedef_mode_name_is(const char *name, size_t name_length, const char *expected) {
    size_t expected_length;

    if (name == NULL || expected == NULL) {
        return false;
    }
    expected_length = strlen(expected);
    return name_length == expected_length && memcmp(name, expected, expected_length) == 0;
}

static bool apply_integer_typedef_mode(MinicParser *parser,
                                       const MinicParsedAttribute *attribute,
                                       MinicType *type) {
    const char *mode_name;
    size_t mode_name_length;
    unsigned int qualifiers;
    size_t explicit_alignment;
    bool is_unsigned;
    MinicType mapped;

    if (parser == NULL || attribute == NULL || type == NULL ||
        type->base_kind != MINIC_TYPE_BASE_INT || type->pointer_depth != 0U ||
        !typedef_attribute_mode_name(parser, attribute, &mode_name, &mode_name_length)) {
        if (parser != NULL) {
            minic_parser_error(parser, "GNU mode attribute requires a scalar integer typedef");
        }
        return false;
    }
    qualifiers = type->base_qualifiers;
    explicit_alignment = type->explicit_alignment;
    is_unsigned = minic_type_is_unsigned_integer(*type);

    if (typedef_mode_name_is(mode_name, mode_name_length, "QI")) {
        mapped = is_unsigned ? minic_type_unsigned_char() : minic_type_signed_char();
    } else if (typedef_mode_name_is(mode_name, mode_name_length, "HI")) {
        mapped = is_unsigned ? minic_type_unsigned_short() : minic_type_short();
    } else if (typedef_mode_name_is(mode_name, mode_name_length, "SI")) {
        mapped = is_unsigned ? minic_type_unsigned_int() : minic_type_int();
    } else if (typedef_mode_name_is(mode_name, mode_name_length, "DI")) {
        mapped = is_unsigned ? minic_type_unsigned_long_long() : minic_type_long_long();
    } else if (typedef_mode_name_is(mode_name, mode_name_length, "TI")) {
        mapped = is_unsigned ? minic_type_unsigned_int128() : minic_type_int128();
    } else {
        minic_parser_error(parser, "unsupported GNU integer mode");
        return false;
    }
    mapped.base_qualifiers = qualifiers;
    mapped.explicit_alignment = explicit_alignment;
    *type = mapped;
    return true;
}

static bool consume_typedef_attribute(MinicParser *parser,
                                      const MinicParsedAttribute *attribute,
                                      void *opaque_context) {
    MinicTypedefAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicTypedefAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "unsupported GNU typedef attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_MODE) {
        return apply_integer_typedef_mode(parser, attribute, context->aliased_type);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_MAY_ALIAS) {
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        size_t natural_size;
        size_t natural_alignment;
        size_t alignment;

        if (minic_type_is_pointer(*context->aliased_type)) {
            minic_parser_error(parser,
                               "aligned pointer typedefs require per-layer type attributes");
            return false;
        }
        alignment = context->aliased_type->explicit_alignment;
        if (!minic_parser_apply_alignment_attribute(parser, attribute, "typedef", &alignment) ||
            !minic_data_layout_type(minic_target_info_data_layout(parser->target_info),
                                    parser->program,
                                    *context->aliased_type,
                                    &natural_size,
                                    &natural_alignment)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot determine GNU typedef alignment");
            }
            return false;
        }
        (void)natural_size;
        if (alignment < natural_alignment) {
            minic_parser_error(parser, "reducing GNU typedef alignment is not supported yet");
            return false;
        }
        context->aliased_type->explicit_alignment = alignment;
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_TRANSPARENT_UNION) {
        MinicRecord *record;
        size_t field_index;

        if (!minic_type_is_record(*context->aliased_type)) {
            minic_parser_error(parser, "GNU transparent_union requires a union type");
            return false;
        }
        record = &parser->program->records[context->aliased_type->record_id];
        if (!record->is_union) {
            minic_parser_error(parser, "GNU transparent_union requires a union type");
            return false;
        }
        if (!record->is_complete || record->field_count == 0U) {
            minic_parser_error(parser, "GNU transparent_union requires a complete non-empty union");
            return false;
        }
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->is_array || field->is_bit_field ||
                !minic_type_is_pointer(field->type)) {
                minic_parser_error(parser,
                                   "GNU transparent_union v0 requires pointer members with one "
                                   "machine representation");
                return false;
            }
        }
        record->is_transparent_union = true;
        return true;
    }
    minic_parser_error(parser, "unsupported GNU typedef attribute");
    return false;
}

static bool typedef_declaration_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {
    return semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW;
}

static bool typedef_type_targets_function(MinicType type) {
    MinicType pointee;

    if (minic_type_is_function(type)) {
        return true;
    }
    return minic_type_pointee(type, &pointee) && minic_type_is_function(pointee);
}

static bool apply_typedef_declaration_head_attributes(MinicParser *parser,
                                                      const MinicParsedAttributeList *attributes,
                                                      MinicType aliased_type) {
    size_t index;
    bool function_target;

    if (parser == NULL || attributes == NULL) {
        return false;
    }
    function_target = typedef_type_targets_function(aliased_type);
    for (index = 0U; index < attributes->count; ++index) {
        const MinicAttributeDescriptor *descriptor;

        descriptor = attributes->values[index].descriptor;
        if (descriptor == NULL ||
            !typedef_declaration_attribute_class_is_parse_only(descriptor->semantic_class) ||
            ((!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) &&
             (!function_target ||
              !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION)))) {
            minic_parser_error(parser, "unsupported GNU declaration-head typedef attribute");
            return false;
        }
    }
    return true;
}

static bool parse_typedef_attributes(MinicParser *parser, MinicType *aliased_type) {
    MinicTypedefAttributeContext context;

    if (parser == NULL || aliased_type == NULL) {
        return false;
    }
    context.aliased_type = aliased_type;
    return minic_parser_parse_gnu_attribute_lists(parser, consume_typedef_attribute, &context);
}

bool minic_parser_parse_typedef(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType aliased_type;
    MinicTypeAliasId alias_id;
    MinicParsedAttributeList leading_attributes;
    MinicParsedAttributeList post_type_attributes;
    bool is_function_declarator;

    leading_attributes.count = 0U;
    post_type_attributes.count = 0U;
    is_function_declarator = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'") ||
        !minic_parser_collect_gnu_attribute_lists(parser, &leading_attributes)) {
        return false;
    }
    {
        MinicType base_type;

        if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
            !minic_parser_collect_gnu_attribute_lists(parser, &post_type_attributes) ||
            !minic_parser_parse_pointer_declarator(parser, base_type, &aliased_type)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            if (!parse_parenthesized_function_typedef(
                    parser, aliased_type, &name_span, &aliased_type)) {
                return false;
            }
            is_function_declarator = true;
        } else {
            MinicParsedFunctionDeclarator declarator;

            if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected typedef name");
                return false;
            }
            name_span = parser->current.span;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_LPAREN) {
                (void)memset(&declarator, 0, sizeof(declarator));
                declarator.name_span = name_span;
                declarator.has_name = true;
                if (!minic_parser_parse_function_parameter_suffix(parser, &declarator)) {
                    return false;
                }
                if (!minic_parser_build_function_declarator_type(
                        parser, aliased_type, &declarator, &aliased_type)) {
                    minic_parser_error(parser, "cannot build function typedef type");
                    return false;
                }
                is_function_declarator = true;
            }
        }
    }
    if (minic_type_is_void(aliased_type)) {
        minic_parser_error(parser, "typedef cannot name bare void");
        return false;
    }
    if (minic_parser_find_type_alias(parser, name_span) != MINIC_TYPE_ALIAS_INVALID) {
        minic_parser_error(parser, "duplicate typedef name");
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        bool is_array;

        if (is_function_declarator) {
            minic_parser_error(parser, "function typedef array declarators are not supported yet");
            return false;
        }
        if (!minic_parser_parse_array_declarator_suffix(
                parser, aliased_type, true, &aliased_type, &is_array) ||
            !is_array) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot build typedef array declarator type");
            }
            return false;
        }
    }
    if (!apply_typedef_declaration_head_attributes(parser, &leading_attributes, aliased_type) ||
        !apply_typedef_declaration_head_attributes(parser, &post_type_attributes, aliased_type) ||
        !parse_typedef_attributes(parser, &aliased_type)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(parser, "expected ';' after typedef");
        return false;
    }
    if (!minic_c0_program_add_type_alias(parser->program,
                                         parser->source + name_span.begin.offset,
                                         minic_parser_span_length(name_span),
                                         aliased_type,
                                         &alias_id)) {
        minic_parser_error(parser, "out of memory while adding typedef");
        return false;
    }
    return minic_parser_advance(parser);
}
