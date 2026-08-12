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
    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer typedefs are not supported yet");
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
    bool is_function_declarator;

    is_function_declarator = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'")) {
        return false;
    }
    {
        MinicType base_type;

        if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
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
                if (declarator.is_variadic) {
                    minic_parser_error(parser, "variadic function typedefs are not supported yet");
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
                parser, aliased_type, false, &aliased_type, &is_array) ||
            !is_array) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot build typedef array declarator type");
            }
            return false;
        }
    }
    if (!parse_typedef_attributes(parser, &aliased_type)) {
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
