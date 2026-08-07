#include "frontend/parser_internal.h"

static bool minic_parser_is_integer_type_specifier(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_KW_CHAR || kind == MINIC_TOKEN_KW_INT ||
           kind == MINIC_TOKEN_KW_LONG || kind == MINIC_TOKEN_KW_SIGNED ||
           kind == MINIC_TOKEN_KW_UNSIGNED;
}

bool minic_parser_require_complete_object_type(MinicParser *parser,
                                               MinicType type,
                                               const char *message) {
    const MinicRecord *record;

    if (!minic_type_is_record(type)) {
        return true;
    }
    record = minic_c0_program_record(parser->program, type.record_id);
    if (record != NULL && record->is_complete) {
        return true;
    }
    minic_parser_error(parser, "%s", message);
    return false;
}

bool minic_parser_parse_type_specifiers(MinicParser *parser, MinicType *type) {
    MinicType parsed_type;
    bool is_const;

    if (type == NULL) {
        minic_parser_error(parser, "internal error: missing parsed type output");
        return false;
    }

    is_const = false;
    if (parser->current.kind == MINIC_TOKEN_KW_CONST) {
        is_const = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    if (minic_parser_is_integer_type_specifier(parser->current.kind)) {
        bool saw_char = false;
        bool saw_int = false;
        bool saw_long = false;
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
                if (saw_long) {
                    minic_parser_error(parser, "long long is not supported");
                    return false;
                }
                saw_long = true;
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
        if (saw_char) {
            if (saw_long || saw_int) {
                minic_parser_error(parser, "char cannot be combined with int or long");
                return false;
            }
            if (saw_signed) {
                minic_parser_error(parser, "signed char is not supported");
                return false;
            }
            parsed_type = saw_unsigned ? minic_type_unsigned_char() : minic_type_char();
        } else if (saw_long) {
            parsed_type = saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
        } else {
            parsed_type = saw_unsigned ? minic_type_unsigned_int() : minic_type_int();
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
    } else if (parser->current.kind == MINIC_TOKEN_KW_STRUCT) {
        MinicRecordId record_id;
        const MinicRecord *record;

        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected record tag after 'struct'");
            return false;
        }
        record_id = minic_parser_find_record(parser, parser->current.span);
        record = minic_c0_program_record(parser->program, record_id);
        if (record == NULL) {
            minic_parser_error(parser, "use of undeclared record tag");
            return false;
        }
        parsed_type = minic_type_record(record_id);
        if (!minic_parser_advance(parser)) {
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

    if (is_const && !minic_type_add_const(parsed_type, &parsed_type)) {
        minic_parser_error(parser, "cannot apply const qualifier");
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
        while (parser->current.kind == MINIC_TOKEN_KW_CONST) {
            if (!minic_type_add_const(parsed_type, &parsed_type)) {
                minic_parser_error(parser, "cannot apply pointer const qualifier");
                return false;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    *type = parsed_type;
    return true;
}

bool minic_parser_parse_type_name(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, type)) {
        return false;
    }
    return minic_parser_require_complete_object_type(
        parser, *type, "incomplete record type requires pointer declarator");
}
