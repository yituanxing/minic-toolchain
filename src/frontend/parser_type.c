#include "frontend/parser_internal.h"

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

    if (parser->current.kind == MINIC_TOKEN_KW_INT) {
        parsed_type = minic_type_int();
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_KW_UNSIGNED) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_CHAR) {
            parsed_type = minic_type_unsigned_char();
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else {
            parsed_type = minic_type_unsigned_int();
            if (parser->current.kind == MINIC_TOKEN_KW_INT && !minic_parser_advance(parser)) {
                return false;
            }
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
        if (record == NULL || !record->is_complete) {
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
    }
    *type = parsed_type;
    return true;
}

bool minic_parser_parse_type_name(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    return minic_parser_parse_type_specifiers(parser, &base_type) &&
           minic_parser_parse_pointer_declarator(parser, base_type, type);
}