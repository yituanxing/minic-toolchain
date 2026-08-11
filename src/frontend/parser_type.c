#include "frontend/parser_internal.h"

#include <string.h>

static bool
minic_parser_token_text_equals(const MinicParser *parser, MinicToken token, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || token.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(token.span);
    return strlen(text) == length &&
           memcmp(parser->source + token.span.begin.offset, text, length) == 0;
}

static bool minic_parser_token_is_gnu_typeof(const MinicParser *parser, MinicToken token) {
    return minic_parser_token_text_equals(parser, token, "typeof") ||
           minic_parser_token_text_equals(parser, token, "__typeof") ||
           minic_parser_token_text_equals(parser, token, "__typeof__");
}

static bool minic_parser_gnu_int128_name(const MinicParser *parser, bool *is_unsigned_name) {
    const char *name;
    size_t length;

    if (is_unsigned_name != NULL) {
        *is_unsigned_name = false;
    }
    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    name = parser->source + parser->current.span.begin.offset;
    length = minic_parser_span_length(parser->current.span);
    if ((length == 8U && memcmp(name, "__int128", 8U) == 0) ||
        (length == 10U && memcmp(name, "__int128_t", 10U) == 0)) {
        return true;
    }
    if ((length == 9U && memcmp(name, "__uint128", 9U) == 0) ||
        (length == 11U && memcmp(name, "__uint128_t", 11U) == 0)) {
        if (is_unsigned_name != NULL) {
            *is_unsigned_name = true;
        }
        return true;
    }
    return false;
}

bool minic_parser_token_starts_type_name(const MinicParser *parser, MinicToken token) {
    bool int128_unsigned;
    MinicParser probe;

    switch (token.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_VOLATILE:
    case MINIC_TOKEN_KW_BOOL:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_SHORT:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_FLOAT:
    case MINIC_TOKEN_KW_DOUBLE:
    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
    case MINIC_TOKEN_KW_UNION:
    case MINIC_TOKEN_KW_ENUM:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        if (parser == NULL) {
            return false;
        }
        if (minic_parser_token_is_gnu_typeof(parser, token)) {
            return true;
        }
        probe = *parser;
        probe.current = token;
        int128_unsigned = false;
        if (minic_parser_gnu_int128_name(&probe, &int128_unsigned)) {
            return true;
        }
        return minic_parser_find_local(parser, token.span) == MINIC_LOCAL_INVALID &&
               minic_parser_find_type_alias(parser, token.span) != MINIC_TYPE_ALIAS_INVALID;
    default:
        return false;
    }
}

static bool minic_parser_try_gnu_int128(MinicParser *parser, MinicType *type, bool *matched) {
    MinicParser probe;
    bool direct_unsigned;
    bool explicit_signed;
    bool explicit_unsigned;

    if (parser == NULL || type == NULL || matched == NULL) {
        return false;
    }
    *matched = false;
    direct_unsigned = false;
    explicit_signed = false;
    explicit_unsigned = false;

    if (parser->current.kind == MINIC_TOKEN_KW_SIGNED ||
        parser->current.kind == MINIC_TOKEN_KW_UNSIGNED) {
        explicit_signed = parser->current.kind == MINIC_TOKEN_KW_SIGNED;
        explicit_unsigned = parser->current.kind == MINIC_TOKEN_KW_UNSIGNED;
        probe = *parser;
        if (!minic_parser_advance(&probe) ||
            !minic_parser_gnu_int128_name(&probe, &direct_unsigned)) {
            return true;
        }
        *parser = probe;
    } else if (!minic_parser_gnu_int128_name(parser, &direct_unsigned)) {
        return true;
    }

    if (direct_unsigned && explicit_signed) {
        minic_parser_error(parser, "signed cannot be combined with __uint128");
        return false;
    }
    *type =
        (direct_unsigned || explicit_unsigned) ? minic_type_unsigned_int128() : minic_type_int128();
    *matched = true;
    return minic_parser_advance(parser);
}

static bool minic_parser_identifier_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

static bool minic_parser_current_identifier_is(const MinicParser *parser, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(text) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, text, length) == 0;
}

static bool minic_parser_skip_gnu_extension_markers(MinicParser *parser) {
    while (minic_parser_current_identifier_is(parser, "__extension__")) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return true;
}

static bool
declaration_identifier_is(const MinicParser *parser, MinicToken token, const char *name) {
    size_t length;

    if (parser == NULL || name == NULL || token.kind != MINIC_TOKEN_IDENTIFIER ||
        token.span.end.offset < token.span.begin.offset) {
        return false;
    }
    length = token.span.end.offset - token.span.begin.offset;
    return strlen(name) == length &&
           memcmp(parser->source + token.span.begin.offset, name, length) == 0;
}

bool minic_parser_token_starts_declaration_specifiers(const MinicParser *parser, MinicToken token) {
    return minic_parser_token_starts_type_name(parser, token) ||
           declaration_identifier_is(parser, token, "register") ||
           declaration_identifier_is(parser, token, "auto");
}

bool minic_parser_parse_local_storage_class(MinicParser *parser, bool *is_register_storage) {
    bool saw_storage_class;

    if (parser == NULL || is_register_storage == NULL) {
        return false;
    }
    *is_register_storage = false;
    saw_storage_class = false;
    while (declaration_identifier_is(parser, parser->current, "register") ||
           declaration_identifier_is(parser, parser->current, "auto")) {
        bool is_register;

        if (saw_storage_class) {
            minic_parser_error(parser, "multiple storage-class specifiers in local declaration");
            return false;
        }
        is_register = declaration_identifier_is(parser, parser->current, "register");
        saw_storage_class = true;
        *is_register_storage = is_register;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return true;
}

static bool minic_parser_is_integer_type_specifier(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_KW_CHAR || kind == MINIC_TOKEN_KW_INT ||
           kind == MINIC_TOKEN_KW_LONG || kind == MINIC_TOKEN_KW_SHORT ||
           kind == MINIC_TOKEN_KW_SIGNED || kind == MINIC_TOKEN_KW_UNSIGNED;
}

bool minic_parser_require_complete_object_type(MinicParser *parser,
                                               MinicType type,
                                               const char *message) {
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, type.record_id);
        if (record != NULL && record->is_complete) {
            return true;
        }
        minic_parser_error(parser, "%s", message);
        return false;
    }
    if (minic_type_is_enum(type)) {
        const MinicEnum *entity;

        entity = minic_c0_program_enum(parser->program, type.enum_id);
        if (entity != NULL && entity->is_complete) {
            return true;
        }
        minic_parser_error(parser, "%s", message);
        return false;
    }
    return true;
}

bool minic_parser_parse_type_specifiers(MinicParser *parser, MinicType *type) {
    MinicType parsed_type;
    bool is_const;
    bool is_volatile;

    if (type == NULL) {
        minic_parser_error(parser, "internal error: missing parsed type output");
        return false;
    }
    if (!minic_parser_skip_gnu_extension_markers(parser)) {
        return false;
    }

    is_const = false;
    is_volatile = false;
    while (parser->current.kind == MINIC_TOKEN_KW_CONST ||
           parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
        if (parser->current.kind == MINIC_TOKEN_KW_CONST) {
            is_const = true;
        } else {
            is_volatile = true;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    {
        bool parsed_gnu_int128 = false;

        if (!minic_parser_try_gnu_int128(parser, &parsed_type, &parsed_gnu_int128)) {
            return false;
        }
        if (parsed_gnu_int128) {
            goto parsed_type_specifiers_done;
        }
    }

    if (minic_parser_token_is_gnu_typeof(parser, parser->current)) {
        MinicSourceSpan typeof_span = parser->current.span;

        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after GNU typeof")) {
            return false;
        }
        if (minic_parser_token_starts_type_name(parser, parser->current)) {
            if (!minic_parser_parse_type_name_preserving_incomplete(parser, &parsed_type)) {
                return false;
            }
        } else {
            MinicExpressionId operand_id;
            const MinicExpression *operand;

            if (!minic_parser_parse_expression_no_decay(parser, &operand_id)) {
                return false;
            }
            operand = minic_c0_program_expression(parser->program, operand_id);
            if (operand == NULL) {
                minic_parser_error(parser, "invalid GNU typeof expression operand");
                return false;
            }
            parsed_type = operand->type;
            if (minic_c0_expression_array_object_info(parser->program, operand, NULL) &&
                !minic_parser_materialize_array_object_type(parser, operand_id, &parsed_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot preserve GNU typeof array operand");
                }
                return false;
            }
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU typeof operand")) {
            return false;
        }
        (void)typeof_span;
        goto parsed_type_specifiers_done;
    }

    if (parser->current.kind == MINIC_TOKEN_KW_BOOL) {
        parsed_type = minic_type_bool();
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (minic_parser_is_integer_type_specifier(parser->current.kind)) {
        bool saw_char = false;
        bool saw_int = false;
        unsigned int long_count = 0U;
        bool saw_short = false;
        bool saw_signed = false;
        bool saw_unsigned = false;

        while (minic_parser_is_integer_type_specifier(parser->current.kind)) {
            switch (parser->current.kind) {
            case MINIC_TOKEN_KW_CHAR:
                if (saw_char) {
                    minic_parser_error(parser, "duplicate char type specifier");
                    return false;
                }
                saw_char = true;
                break;
            case MINIC_TOKEN_KW_INT:
                if (saw_int) {
                    minic_parser_error(parser, "duplicate int type specifier");
                    return false;
                }
                saw_int = true;
                break;
            case MINIC_TOKEN_KW_LONG:
                long_count += 1U;
                if (long_count > 2U) {
                    minic_parser_error(parser, "too many long type specifiers");
                    return false;
                }
                break;
            case MINIC_TOKEN_KW_SHORT:
                if (saw_short) {
                    minic_parser_error(parser, "duplicate short type specifier");
                    return false;
                }
                saw_short = true;
                break;
            case MINIC_TOKEN_KW_SIGNED:
                if (saw_signed) {
                    minic_parser_error(parser, "duplicate signed type specifier");
                    return false;
                }
                saw_signed = true;
                break;
            case MINIC_TOKEN_KW_UNSIGNED:
                if (saw_unsigned) {
                    minic_parser_error(parser, "duplicate unsigned type specifier");
                    return false;
                }
                saw_unsigned = true;
                break;
            default:
                return false;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }

        if (saw_signed && saw_unsigned) {
            minic_parser_error(parser, "conflicting signed and unsigned type specifiers");
            return false;
        }
        if (saw_short && long_count != 0U) {
            minic_parser_error(parser, "short cannot be combined with long");
            return false;
        }
        if (saw_char) {
            if (saw_short || long_count != 0U || saw_int) {
                minic_parser_error(parser, "char cannot be combined with short, int, or long");
                return false;
            }
            parsed_type = saw_signed     ? minic_type_signed_char()
                          : saw_unsigned ? minic_type_unsigned_char()
                                         : minic_type_char();
        } else if (saw_short) {
            parsed_type = saw_unsigned ? minic_type_unsigned_short() : minic_type_short();
        } else if (long_count == 2U) {
            parsed_type = saw_unsigned ? minic_type_unsigned_long_long() : minic_type_long_long();
        } else if (long_count == 1U) {
            parsed_type = saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
        } else {
            parsed_type = saw_unsigned ? minic_type_unsigned_int() : minic_type_int();
        }
    } else if (parser->current.kind == MINIC_TOKEN_KW_FLOAT) {
        parsed_type = minic_type_float();
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_KW_DOUBLE) {
        parsed_type = minic_type_double();
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_KW_VOID) {
        parsed_type = minic_type_void();
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_KW_ENUM) {
        if (!minic_parser_parse_enum_specifier(parser, &parsed_type)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_KW_STRUCT ||
               parser->current.kind == MINIC_TOKEN_KW_UNION) {
        MinicParser probe;
        MinicRecordId record_id;
        MinicTokenKind record_keyword;
        bool is_definition;
        bool is_union;

        record_keyword = parser->current.kind;
        is_union = record_keyword == MINIC_TOKEN_KW_UNION;
        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        is_definition = probe.current.kind == MINIC_TOKEN_LBRACE;
        if (!is_definition && probe.current.kind == MINIC_TOKEN_IDENTIFIER) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            is_definition = probe.current.kind == MINIC_TOKEN_LBRACE;
        }

        if (is_definition) {
            if (!minic_parser_parse_record_definition_specifier(parser, &parsed_type)) {
                return false;
            }
        } else {
            if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser,
                                   "expected record tag or definition after record keyword");
                return false;
            }
            record_id = minic_parser_find_record(parser, parser->current.span);
            if (record_id == MINIC_RECORD_INVALID) {
                if (!minic_c0_program_add_record(parser->program,
                                                 parser->source + parser->current.span.begin.offset,
                                                 minic_parser_span_length(parser->current.span),
                                                 &record_id)) {
                    minic_parser_error(parser, "out of memory while declaring record tag");
                    return false;
                }
                parser->program->records[record_id].is_union = is_union;
            } else if (parser->program->records[record_id].is_union != is_union) {
                minic_parser_error(parser, "record tag kind does not match prior declaration");
                return false;
            }
            parsed_type = minic_type_record(record_id);
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    } else if (minic_parser_identifier_is(parser, "__builtin_va_list")) {
        if (!minic_type_pointer_to(minic_type_void(), &parsed_type) ||
            !minic_parser_advance(parser)) {
            minic_parser_error(parser, "cannot build __builtin_va_list type");
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicTypeAliasId alias_id;
        const MinicTypeAlias *alias;

        alias_id = minic_parser_find_type_alias(parser, parser->current.span);
        alias = minic_c0_program_type_alias(parser->program, alias_id);
        if (alias == NULL) {
            minic_parser_error(parser, "expected type name");
            return false;
        }
        parsed_type = alias->type;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else {
        minic_parser_error(parser, "expected type name");
        return false;
    }

parsed_type_specifiers_done:
    while (parser->current.kind == MINIC_TOKEN_KW_CONST ||
           parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
        if (parser->current.kind == MINIC_TOKEN_KW_CONST) {
            is_const = true;
        } else {
            is_volatile = true;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (is_const && !minic_type_add_const(parsed_type, &parsed_type)) {
        minic_parser_error(parser, "cannot apply const qualifier");
        return false;
    }
    if (is_volatile && !minic_type_add_volatile(parsed_type, &parsed_type)) {
        minic_parser_error(parser, "cannot apply volatile qualifier");
        return false;
    }
    *type = parsed_type;
    return true;
}

bool minic_parser_parse_pointer_declarator(MinicParser *parser,
                                           MinicType base_type,
                                           MinicType *type) {
    MinicType parsed_type;

    if (type == NULL) {
        minic_parser_error(parser, "internal error: missing declarator type output");
        return false;
    }
    parsed_type = base_type;
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        if (!minic_type_pointer_to(parsed_type, &parsed_type) || !minic_parser_advance(parser)) {
            minic_parser_error(parser, "pointer declarator depth is unsupported");
            return false;
        }
        while (parser->current.kind == MINIC_TOKEN_KW_CONST ||
               parser->current.kind == MINIC_TOKEN_KW_VOLATILE ||
               minic_parser_identifier_is(parser, "restrict") ||
               minic_parser_identifier_is(parser, "__restrict")) {
            if (parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
                if (!minic_type_add_volatile(parsed_type, &parsed_type)) {
                    minic_parser_error(parser, "cannot apply pointer volatile qualifier");
                    return false;
                }
            } else if (parser->current.kind == MINIC_TOKEN_KW_CONST) {
                if (!minic_type_add_const(parsed_type, &parsed_type)) {
                    minic_parser_error(parser, "cannot apply pointer const qualifier");
                    return false;
                }
            }
            /* restrict is an aliasing promise, not an ABI/layout qualifier. MiniC does
               not yet perform restrict-based alias optimization, so accepting it here
               preserves observable semantics while keeping the target type unchanged. */
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    *type = parsed_type;
    return true;
}

bool minic_parser_parse_type_name_preserving_incomplete(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    return parser != NULL && type != NULL &&
           minic_parser_parse_type_specifiers(parser, &base_type) &&
           minic_parser_parse_pointer_declarator(parser, base_type, type);
}

bool minic_parser_parse_type_name(MinicParser *parser, MinicType *type) {
    if (!minic_parser_parse_type_name_preserving_incomplete(parser, type)) {
        return false;
    }
    return minic_parser_require_complete_object_type(
        parser, *type, "incomplete record type requires pointer declarator");
}
