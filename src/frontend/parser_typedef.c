#include "frontend/parser_internal.h"
#include "frontend/declaration_sema.h"

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

static bool typedef_starts_parenthesized_pointer_array(const MinicParser *parser) {
    MinicParser probe;
    size_t depth;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_STAR) {
        return false;
    }
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
                return minic_parser_advance(&probe) &&
                       probe.current.kind == MINIC_TOKEN_LBRACKET;
            }
        }
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
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
    /* GNU system headers may spell mode arguments with an extra parenthesis
       layer, e.g. __mode__ ((__word__)) after attribute collection. Normalize
       redundant wrappers before matching the target-owned mode identifier. */
    while (end > begin + 1U && parser->source[begin] == '(' &&
           parser->source[end - 1U] == ')') {
        begin += 1U;
        end -= 1U;
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
    } else if (typedef_mode_name_is(mode_name, mode_name_length, "word")) {
        if (!minic_target_info_word_integer_type(
                parser->target_info,
                is_unsigned ? MINIC_INTEGER_SIGN_UNSIGNED : MINIC_INTEGER_SIGN_SIGNED,
                &mapped)) {
            minic_parser_error(parser, "cannot resolve target GNU word integer mode");
            return false;
        }
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
        size_t alignment;

        if (minic_type_is_pointer(*context->aliased_type)) {
            minic_parser_error(parser,
                               "aligned pointer typedefs require per-layer type attributes");
            return false;
        }
        alignment = context->aliased_type->explicit_alignment;
        if (!minic_parser_apply_alignment_attribute(parser, attribute, "typedef", &alignment)) {
            return false;
        }
        /* GCC permits an aligned attribute on a typedef to decrease as well as
           increase the natural alignment. MinicType::explicit_alignment is the
           typedef-carried type alignment; record/object attributes retain their
           separate minimum-alignment owners. */
        context->aliased_type->explicit_alignment = alignment;
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_TRANSPARENT_UNION) {
        MinicDeclarationTransparentUnionStatus status;

        status = minic_declaration_apply_transparent_union(parser->program, *context->aliased_type);
        if (status == MINIC_DECLARATION_TRANSPARENT_UNION_OK) {
            return true;
        }
        if (status == MINIC_DECLARATION_TRANSPARENT_UNION_INCOMPLETE) {
            minic_parser_error(parser, "GNU transparent_union requires a complete non-empty union");
        } else if (status == MINIC_DECLARATION_TRANSPARENT_UNION_UNSUPPORTED_MEMBER) {
            minic_parser_error(parser,
                               "GNU transparent_union v0 requires pointer members with one "
                               "machine representation");
        } else {
            minic_parser_error(parser, "GNU transparent_union requires a union type");
        }
        return false;
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
                                                      MinicType *aliased_type) {
    MinicTypedefAttributeContext context;
    size_t index;
    bool function_target;

    if (parser == NULL || attributes == NULL || aliased_type == NULL) {
        return false;
    }
    context.aliased_type = aliased_type;
    function_target = typedef_type_targets_function(*aliased_type);
    for (index = 0U; index < attributes->count; ++index) {
        const MinicParsedAttribute *attribute;
        const MinicAttributeDescriptor *descriptor;

        attribute = &attributes->values[index];
        descriptor = attribute->descriptor;
        if (descriptor == NULL) {
            minic_parser_error(parser, "unsupported GNU declaration-head typedef attribute");
            return false;
        }
        if (minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) {
            if (typedef_declaration_attribute_class_is_parse_only(descriptor->semantic_class)) {
                continue;
            }
            if (!consume_typedef_attribute(parser, attribute, &context)) {
                return false;
            }
            continue;
        }
        if (function_target &&
            minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION) &&
            typedef_declaration_attribute_class_is_parse_only(descriptor->semantic_class)) {
            continue;
        }
        minic_parser_error(parser, "unsupported GNU declaration-head typedef attribute");
        return false;
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

typedef struct MinicParsedTypedefAlias {
    MinicSourceSpan name_span;
    MinicType type;
} MinicParsedTypedefAlias;

static bool pending_typedef_name_exists(const MinicParser *parser,
                                        const MinicParsedTypedefAlias *aliases,
                                        size_t alias_count,
                                        MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    if (parser == NULL) {
        return false;
    }
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < alias_count; ++index) {
        size_t existing_length = minic_parser_span_length(aliases[index].name_span);
        if (existing_length == name_length &&
            memcmp(parser->source + aliases[index].name_span.begin.offset,
                   parser->source + name_span.begin.offset,
                   name_length) == 0) {
            return true;
        }
    }
    return false;
}

static bool append_pending_typedef_alias(MinicParsedTypedefAlias **aliases,
                                         size_t *alias_count,
                                         size_t *alias_capacity,
                                         MinicSourceSpan name_span,
                                         MinicType type) {
    MinicParsedTypedefAlias *grown;
    size_t new_capacity;

    if (aliases == NULL || alias_count == NULL || alias_capacity == NULL) {
        return false;
    }
    if (*alias_count == *alias_capacity) {
        new_capacity = *alias_capacity == 0U ? 4U : *alias_capacity * 2U;
        if (new_capacity < *alias_capacity ||
            new_capacity > SIZE_MAX / sizeof(**aliases)) {
            return false;
        }
        grown = (MinicParsedTypedefAlias *)realloc(
            *aliases, new_capacity * sizeof(**aliases));
        if (grown == NULL) {
            return false;
        }
        *aliases = grown;
        *alias_capacity = new_capacity;
    }
    (*aliases)[*alias_count].name_span = name_span;
    (*aliases)[*alias_count].type = type;
    *alias_count += 1U;
    return true;
}

static bool parse_one_typedef_declarator(
    MinicParser *parser,
    MinicType base_type,
    const MinicParsedAttributeList *leading_attributes,
    const MinicParsedAttributeList *post_type_attributes,
    MinicParsedTypedefAlias **aliases,
    size_t *alias_count,
    size_t *alias_capacity) {
    MinicSourceSpan name_span;
    MinicType aliased_type;
    bool is_function_declarator;

    if (parser == NULL || leading_attributes == NULL || post_type_attributes == NULL ||
        aliases == NULL || alias_count == NULL || alias_capacity == NULL) {
        return false;
    }
    aliased_type = base_type;
    is_function_declarator = false;
    if (!minic_parser_parse_pointer_declarator(parser, base_type, &aliased_type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (typedef_starts_parenthesized_pointer_array(parser)) {
            bool name_array_inferred;
            bool name_is_array;
            size_t name_array_count;

            if (!minic_parser_parse_parenthesized_pointer_to_array_declarator(
                    parser,
                    aliased_type,
                    &name_span,
                    &aliased_type,
                    &name_is_array,
                    &name_array_inferred,
                    &name_array_count)) {
                return false;
            }
            if (name_is_array) {
                (void)name_array_inferred;
                (void)name_array_count;
                minic_parser_error(
                    parser,
                    "array of parenthesized pointer-to-array typedefs is not supported yet");
                return false;
            }
        } else {
            if (!parse_parenthesized_function_typedef(
                    parser, aliased_type, &name_span, &aliased_type)) {
                return false;
            }
            is_function_declarator = true;
        }
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
    if (minic_parser_find_type_alias(parser, name_span) != MINIC_TYPE_ALIAS_INVALID ||
        pending_typedef_name_exists(parser, *aliases, *alias_count, name_span)) {
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
    if (!apply_typedef_declaration_head_attributes(parser, leading_attributes, &aliased_type) ||
        !apply_typedef_declaration_head_attributes(parser, post_type_attributes, &aliased_type) ||
        !parse_typedef_attributes(parser, &aliased_type)) {
        return false;
    }
    if (!append_pending_typedef_alias(
            aliases, alias_count, alias_capacity, name_span, aliased_type)) {
        minic_parser_error(parser, "out of memory while collecting typedef declarators");
        return false;
    }
    return true;
}

bool minic_parser_parse_typedef(MinicParser *parser) {
    MinicParsedTypedefAlias *aliases;
    MinicParsedAttributeList leading_attributes;
    MinicParsedAttributeList post_type_attributes;
    MinicType base_type;
    size_t alias_capacity;
    size_t alias_count;
    size_t index;

    aliases = NULL;
    alias_capacity = 0U;
    alias_count = 0U;
    leading_attributes.count = 0U;
    post_type_attributes.count = 0U;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'") ||
        !minic_parser_collect_gnu_attribute_lists(parser, &leading_attributes) ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &post_type_attributes)) {
        free(aliases);
        return false;
    }

    for (;;) {
        if (!parse_one_typedef_declarator(parser,
                                          base_type,
                                          &leading_attributes,
                                          &post_type_attributes,
                                          &aliases,
                                          &alias_count,
                                          &alias_capacity)) {
            free(aliases);
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            free(aliases);
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(parser, "expected ',' or ';' after typedef declarator");
        free(aliases);
        return false;
    }
    for (index = 0U; index < alias_count; ++index) {
        MinicTypeAliasId alias_id;

        if (!minic_c0_program_add_type_alias(
                parser->program,
                parser->source + aliases[index].name_span.begin.offset,
                minic_parser_span_length(aliases[index].name_span),
                aliases[index].type,
                &alias_id)) {
            minic_parser_error(parser, "out of memory while adding typedef");
            free(aliases);
            return false;
        }
    }
    free(aliases);
    return minic_parser_advance(parser);
}
