#!/usr/bin/env python3
from pathlib import Path


def replace_between(path: str, start: str, end: str, replacement: str) -> None:
    target = Path(path)
    text = target.read_text()
    start_index = text.find(start)
    if start_index < 0:
        raise SystemExit(f"{path}: missing start anchor {start!r}")
    end_index = text.find(end, start_index)
    if end_index < 0:
        raise SystemExit(f"{path}: missing end anchor {end!r}")
    if text.find(start, start_index + 1) >= 0:
        raise SystemExit(f"{path}: start anchor is not unique {start!r}")
    target.write_text(text[:start_index] + replacement + text[end_index:])


replace_between(
    "src/frontend/lexer.c",
    "static bool minic_lexer_scan_character_constant(MinicLexer *lexer,\n",
    "void minic_lexer_initialize(MinicLexer *lexer,\n",
    r'''static bool minic_lexer_scan_character_constant(MinicLexer *lexer,
                                                MinicToken *token,
                                                MinicDiagnostic *diagnostic,
                                                MinicSourcePosition begin) {
    char character;

    minic_lexer_advance(lexer);
    character = minic_lexer_peek(lexer);
    if (character == '\0') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(lexer, diagnostic, begin, "unterminated character constant");
        return false;
    }
    if (character == '\n' || character == '\r') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(
            lexer, diagnostic, minic_lexer_position(lexer), "newline in character constant");
        return false;
    }
    if (character == '\'') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(lexer, diagnostic, begin, "empty character constant");
        return false;
    }
    if (character == '\\') {
        minic_lexer_advance(lexer);
        character = minic_lexer_peek(lexer);
        if (character == '\0') {
            token->span.end = minic_lexer_position(lexer);
            minic_lexer_set_message(lexer, diagnostic, begin, "unterminated character constant");
            return false;
        }
        if (character == '\n' || character == '\r') {
            token->span.end = minic_lexer_position(lexer);
            minic_lexer_set_message(
                lexer, diagnostic, minic_lexer_position(lexer), "newline in character constant");
            return false;
        }
        if (character == 'x') {
            minic_lexer_advance(lexer);
            if (!minic_is_hexadecimal_digit(minic_lexer_peek(lexer))) {
                token->span.end = minic_lexer_position(lexer);
                minic_lexer_set_message(
                    lexer, diagnostic, begin, "hexadecimal character escape requires a digit");
                return false;
            }
            do {
                minic_lexer_advance(lexer);
            } while (minic_is_hexadecimal_digit(minic_lexer_peek(lexer)));
        } else if (character >= '0' && character <= '7') {
            unsigned int digit_count;

            digit_count = 0U;
            do {
                minic_lexer_advance(lexer);
                digit_count += 1U;
            } while (digit_count < 3U && minic_lexer_peek(lexer) >= '0' &&
                     minic_lexer_peek(lexer) <= '7');
        } else {
            minic_lexer_advance(lexer);
        }
    } else {
        minic_lexer_advance(lexer);
    }
    character = minic_lexer_peek(lexer);
    if (character == '\0') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(lexer, diagnostic, begin, "unterminated character constant");
        return false;
    }
    if (character == '\n' || character == '\r') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(
            lexer, diagnostic, minic_lexer_position(lexer), "newline in character constant");
        return false;
    }
    if (character != '\'') {
        token->span.end = minic_lexer_position(lexer);
        minic_lexer_set_message(
            lexer, diagnostic, begin, "multi-character constants are not supported yet");
        return false;
    }

    minic_lexer_advance(lexer);
    token->kind = MINIC_TOKEN_CHARACTER_CONSTANT;
    token->span.end = minic_lexer_position(lexer);
    return true;
}

''',
)

replace_between(
    "src/frontend/parser_constant.c",
    "static bool parse_character_value(MinicParser *parser, int *value) {\n",
    "static size_t integer_digit_end(const MinicParser *parser, MinicSourceSpan span) {\n",
    r'''static bool parse_character_value(MinicParser *parser, int *value) {
    MinicSourceSpan span;
    size_t length;
    size_t offset;
    char character;

    span = parser->current.span;
    length = span.end.offset - span.begin.offset;
    if (length < 3U) {
        minic_parser_error(parser, "invalid character constant");
        return false;
    }
    offset = span.begin.offset + 1U;
    character = parser->source[offset];
    if (character != '\\') {
        if (length != 3U) {
            minic_parser_error(parser, "invalid character constant");
            return false;
        }
        *value = (int)(unsigned char)character;
        return minic_parser_advance(parser);
    }

    character = parser->source[offset + 1U];
    if (character == 'x') {
        unsigned int parsed;
        size_t digit_offset;

        if (length < 5U) {
            minic_parser_error(parser, "invalid hexadecimal character escape");
            return false;
        }
        parsed = 0U;
        for (digit_offset = offset + 2U; digit_offset + 1U < span.end.offset; ++digit_offset) {
            int digit_value;

            digit_value = hexadecimal_digit_value(parser->source[digit_offset]);
            if (digit_value < 0 ||
                parsed > ((unsigned int)UCHAR_MAX - (unsigned int)digit_value) / 16U) {
                minic_parser_error(parser, "hexadecimal character escape is out of range");
                return false;
            }
            parsed = parsed * 16U + (unsigned int)digit_value;
        }
        *value = (int)parsed;
        return minic_parser_advance(parser);
    }
    if (character >= '0' && character <= '7') {
        unsigned int parsed;
        unsigned int digit_count;
        size_t digit_offset;

        parsed = 0U;
        digit_count = 0U;
        digit_offset = offset + 1U;
        while (digit_offset + 1U < span.end.offset && digit_count < 3U &&
               parser->source[digit_offset] >= '0' && parser->source[digit_offset] <= '7') {
            unsigned int digit;

            digit = (unsigned int)(parser->source[digit_offset] - '0');
            if (parsed > ((unsigned int)UCHAR_MAX - digit) / 8U) {
                minic_parser_error(parser, "octal character escape is out of range");
                return false;
            }
            parsed = parsed * 8U + digit;
            digit_count += 1U;
            digit_offset += 1U;
        }
        if (digit_count == 0U || digit_offset + 1U != span.end.offset) {
            minic_parser_error(parser, "invalid octal character escape");
            return false;
        }
        *value = (int)parsed;
        return minic_parser_advance(parser);
    }
    if (length != 4U) {
        minic_parser_error(parser, "invalid character escape");
        return false;
    }
    switch (character) {
    case 'a':
        *value = '\a';
        break;
    case 'b':
        *value = '\b';
        break;
    case 'f':
        *value = '\f';
        break;
    case 'n':
        *value = '\n';
        break;
    case 'r':
        *value = '\r';
        break;
    case 't':
        *value = '\t';
        break;
    case 'v':
        *value = '\v';
        break;
    case '\\':
        *value = '\\';
        break;
    case '\'':
        *value = '\'';
        break;
    case '"':
        *value = '"';
        break;
    case '?':
        *value = '?';
        break;
    default:
        minic_parser_error(parser, "unsupported character escape");
        return false;
    }
    return minic_parser_advance(parser);
}

''',
)

replace_between(
    "src/frontend/parser_statement.c",
    "static bool parse_case(MinicParser *parser) {\n",
    "static bool parse_default(MinicParser *parser) {\n",
    r'''static bool parse_case(MinicParser *parser) {
    MinicParserSwitchContext *context;
    MinicStatement statement;
    const MinicExpression *lower_constant;
    const MinicExpression *upper_constant;
    MinicExpression folded_constant;
    MinicExpressionId lower_expression_id;
    MinicExpressionId upper_expression_id;
    MinicType constant_type;
    MinicSourceSpan constant_span;
    int64_t lower_value;
    int64_t upper_value;
    int64_t candidate;
    size_t range_count;
    size_t index;
    bool is_range;

    context = current_switch_context(parser);
    if (context == NULL) {
        minic_parser_error(parser, "case label requires an enclosing switch");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_CASE;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &lower_expression_id, 0U)) {
        return false;
    }
    lower_constant = minic_c0_program_expression(parser->program, lower_expression_id);
    if (lower_constant == NULL ||
        !case_integer_constant_value(parser->program, lower_expression_id, &lower_value)) {
        minic_parser_error(parser, "case label currently requires an integer constant expression");
        return false;
    }
    constant_type = lower_constant->type;
    constant_span = lower_constant->span;
    upper_value = lower_value;
    upper_expression_id = MINIC_EXPRESSION_INVALID;
    is_range = parser->current.kind == MINIC_TOKEN_ELLIPSIS;
    if (is_range) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &upper_expression_id, 0U)) {
            return false;
        }
        upper_constant = minic_c0_program_expression(parser->program, upper_expression_id);
        if (upper_constant == NULL ||
            !case_integer_constant_value(parser->program, upper_expression_id, &upper_value)) {
            minic_parser_error(parser,
                               "GNU case range upper bound must be an integer constant expression");
            return false;
        }
        if (upper_value < lower_value) {
            minic_parser_error(parser, "GNU case range upper bound is below lower bound");
            return false;
        }
        constant_span.end = upper_constant->span.end;
    }

    range_count = 0U;
    candidate = lower_value;
    for (;;) {
        if (context->case_count + range_count >= MINIC_PARSER_MAX_SWITCH_CASES) {
            minic_parser_error(parser, "switch case count exceeds implementation limit");
            return false;
        }
        for (index = 0U; index < context->case_count; ++index) {
            if (context->case_values[index] == candidate) {
                minic_parser_error(parser, "duplicate case value");
                return false;
            }
        }
        range_count += 1U;
        if (candidate == upper_value) {
            break;
        }
        candidate += 1;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after case value")) {
        return false;
    }
    statement.span.end = parser->current.span.begin;

    candidate = lower_value;
    for (;;) {
        MinicStatement case_statement;

        (void)memset(&folded_constant, 0, sizeof(folded_constant));
        folded_constant.kind = MINIC_EXPRESSION_INTEGER;
        folded_constant.span = constant_span;
        folded_constant.type = constant_type;
        folded_constant.value_category = MINIC_VALUE_RVALUE;
        folded_constant.value.integer_value = candidate;

        case_statement = statement;
        if (!minic_parser_add_expression(parser, &folded_constant, &case_statement.expression) ||
            !minic_parser_add_statement(parser, &case_statement)) {
            return false;
        }
        context->case_values[context->case_count] = candidate;
        context->case_count += 1U;
        if (candidate == upper_value) {
            break;
        }
        candidate += 1;
    }
    return true;
}

''',
)

print("staged C octal character escapes and GNU switch case ranges")
