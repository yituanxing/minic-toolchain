#include "frontend/attribute.h"
#include "frontend/declaration_sema.h"
#include "frontend/parser_internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Source pragma decoding, independent of external entity construction. */

static void pragma_skip_horizontal_space(const char *text, size_t length, size_t *cursor) {
    while (*cursor < length && (text[*cursor] == ' ' || text[*cursor] == '\t')) {
        *cursor += 1U;
    }
}

static bool pragma_consume_word(const char *text, size_t length, size_t *cursor, const char *word) {
    size_t word_length;

    word_length = strlen(word);
    if (*cursor > length || word_length > length - *cursor ||
        memcmp(text + *cursor, word, word_length) != 0) {
        return false;
    }
    *cursor += word_length;
    return true;
}

static bool pragma_only_trailing_space(const char *text, size_t length, size_t cursor) {
    while (cursor < length) {
        char character = text[cursor++];

        if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
            return false;
        }
    }
    return true;
}

static bool pragma_consume_visibility_name(const char *text,
                                           size_t length,
                                           size_t *cursor,
                                           MinicSymbolVisibility *visibility) {
    size_t probe;

    if (text == NULL || cursor == NULL || visibility == NULL) {
        return false;
    }
    probe = *cursor;
    if (pragma_consume_word(text, length, &probe, "default")) {
        *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    } else {
        probe = *cursor;
        if (pragma_consume_word(text, length, &probe, "hidden")) {
            *visibility = MINIC_SYMBOL_VISIBILITY_HIDDEN;
        } else {
            probe = *cursor;
            if (pragma_consume_word(text, length, &probe, "internal")) {
                *visibility = MINIC_SYMBOL_VISIBILITY_INTERNAL;
            } else {
                probe = *cursor;
                if (!pragma_consume_word(text, length, &probe, "protected")) {
                    return false;
                }
                *visibility = MINIC_SYMBOL_VISIBILITY_PROTECTED;
            }
        }
    }
    *cursor = probe;
    return true;
}

bool minic_parser_parse_preprocessor_directive(MinicParser *parser) {
    const char *text;
    size_t length;
    size_t cursor;
    size_t alignment;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_PREPROCESSOR_DIRECTIVE) {
        return false;
    }
    text = parser->source + parser->current.span.begin.offset;
    length = minic_parser_span_length(parser->current.span);
    cursor = 0U;
    if (cursor >= length || text[cursor] != '#') {
        minic_parser_error(parser, "invalid preprocessor directive token");
        return false;
    }
    cursor += 1U;
    pragma_skip_horizontal_space(text, length, &cursor);
    if (!pragma_consume_word(text, length, &cursor, "pragma")) {
        minic_parser_error(parser, "unsupported preprocessor directive");
        return false;
    }
    pragma_skip_horizontal_space(text, length, &cursor);
    if (pragma_consume_word(text, length, &cursor, "GCC")) {
        size_t option_begin;
        const char *actions[] = {"push", "pop", "ignored", "warning", "error"};
        size_t action_index;
        size_t matched_action;

        pragma_skip_horizontal_space(text, length, &cursor);
        {
            size_t visibility_probe = cursor;

            if (pragma_consume_word(text, length, &visibility_probe, "visibility")) {
                MinicSymbolVisibility visibility;

                cursor = visibility_probe;
                pragma_skip_horizontal_space(text, length, &cursor);
                if (pragma_consume_word(text, length, &cursor, "push")) {
                    pragma_skip_horizontal_space(text, length, &cursor);
                    if (cursor >= length || text[cursor] != '(') {
                        minic_parser_error(parser, "expected '(' after GCC visibility push");
                        return false;
                    }
                    cursor += 1U;
                    pragma_skip_horizontal_space(text, length, &cursor);
                    if (!pragma_consume_visibility_name(text, length, &cursor, &visibility)) {
                        minic_parser_error(parser, "unsupported GCC visibility value");
                        return false;
                    }
                    pragma_skip_horizontal_space(text, length, &cursor);
                    if (cursor >= length || text[cursor] != ')') {
                        minic_parser_error(parser, "expected ')' after GCC visibility value");
                        return false;
                    }
                    cursor += 1U;
                    if (!pragma_only_trailing_space(text, length, cursor)) {
                        minic_parser_error(parser, "unexpected tokens after GCC visibility push");
                        return false;
                    }
                    if (parser->visibility_depth >= MINIC_PARSER_MAX_VISIBILITY_DEPTH) {
                        minic_parser_error(parser,
                                           "GCC visibility stack depth exceeds implementation limit");
                        return false;
                    }
                    parser->visibility_stack[parser->visibility_depth++] =
                        parser->default_visibility;
                    parser->default_visibility = visibility;
                    return minic_parser_advance(parser);
                }
                if (pragma_consume_word(text, length, &cursor, "pop")) {
                    pragma_skip_horizontal_space(text, length, &cursor);
                    if (!pragma_only_trailing_space(text, length, cursor)) {
                        minic_parser_error(parser, "unexpected tokens after GCC visibility pop");
                        return false;
                    }
                    if (parser->visibility_depth == 0U) {
                        minic_parser_error(parser,
                                           "GCC visibility pop has no matching push");
                        return false;
                    }
                    parser->visibility_depth -= 1U;
                    parser->default_visibility =
                        parser->visibility_stack[parser->visibility_depth];
                    return minic_parser_advance(parser);
                }
                minic_parser_error(parser, "unsupported GCC visibility pragma action");
                return false;
            }
        }
        if (!pragma_consume_word(text, length, &cursor, "diagnostic")) {
            minic_parser_error(parser, "unsupported GCC pragma directive");
            return false;
        }
        pragma_skip_horizontal_space(text, length, &cursor);
        matched_action = sizeof(actions) / sizeof(actions[0]);
        for (action_index = 0U; action_index < sizeof(actions) / sizeof(actions[0]);
             ++action_index) {
            size_t probe = cursor;

            if (pragma_consume_word(text, length, &probe, actions[action_index])) {
                matched_action = action_index;
                cursor = probe;
                break;
            }
        }
        if (matched_action == sizeof(actions) / sizeof(actions[0])) {
            minic_parser_error(parser, "unsupported GCC diagnostic pragma action");
            return false;
        }
        pragma_skip_horizontal_space(text, length, &cursor);
        if (matched_action <= 1U) {
            if (!pragma_only_trailing_space(text, length, cursor)) {
                minic_parser_error(parser, "unexpected tokens after GCC diagnostic push/pop");
                return false;
            }
            return minic_parser_advance(parser);
        }
        if (cursor >= length || text[cursor] != '"') {
            minic_parser_error(parser, "GCC diagnostic pragma requires an option string");
            return false;
        }
        cursor += 1U;
        option_begin = cursor;
        while (cursor < length && text[cursor] != '"') {
            if (text[cursor] == '\\' && cursor + 1U < length) {
                cursor += 2U;
            } else {
                cursor += 1U;
            }
        }
        if (cursor >= length || text[cursor] != '"' || cursor == option_begin) {
            minic_parser_error(parser, "invalid GCC diagnostic option string");
            return false;
        }
        cursor += 1U;
        if (!pragma_only_trailing_space(text, length, cursor)) {
            minic_parser_error(parser, "unexpected tokens after GCC diagnostic option");
            return false;
        }
        return minic_parser_advance(parser);
    }
    if (!pragma_consume_word(text, length, &cursor, "pack")) {
        minic_parser_error(parser, "unsupported pragma directive");
        return false;
    }
    pragma_skip_horizontal_space(text, length, &cursor);
    if (cursor >= length || text[cursor] != '(') {
        minic_parser_error(parser, "expected '(' after pragma pack");
        return false;
    }
    cursor += 1U;
    pragma_skip_horizontal_space(text, length, &cursor);

    if (pragma_consume_word(text, length, &cursor, "push")) {
        size_t saved_alignment;

        saved_alignment = parser->record_pack_alignment;
        pragma_skip_horizontal_space(text, length, &cursor);
        alignment = saved_alignment;
        if (cursor < length && text[cursor] == ',') {
            size_t digit_count;

            cursor += 1U;
            pragma_skip_horizontal_space(text, length, &cursor);
            alignment = 0U;
            digit_count = 0U;
            while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
                size_t digit;

                digit = (size_t)(text[cursor] - '0');
                if (alignment > (SIZE_MAX - digit) / 10U) {
                    minic_parser_error(parser, "unsupported pragma pack alignment");
                    return false;
                }
                alignment = alignment * 10U + digit;
                cursor += 1U;
                digit_count += 1U;
            }
            if (digit_count == 0U || alignment == 0U || alignment > 16U ||
                (alignment & (alignment - 1U)) != 0U) {
                minic_parser_error(parser, "unsupported pragma pack alignment");
                return false;
            }
            pragma_skip_horizontal_space(text, length, &cursor);
        }
        if (cursor >= length || text[cursor] != ')') {
            minic_parser_error(parser, "expected ')' after pragma pack push");
            return false;
        }
        cursor += 1U;
        if (!pragma_only_trailing_space(text, length, cursor)) {
            minic_parser_error(parser, "unsupported pragma pack push syntax");
            return false;
        }
        if (parser->record_pack_depth >= MINIC_PARSER_MAX_PACK_DEPTH) {
            minic_parser_error(parser, "pragma pack stack depth exceeds implementation limit");
            return false;
        }
        parser->record_pack_stack[parser->record_pack_depth++] = saved_alignment;
        parser->record_pack_alignment = alignment;
        return minic_parser_advance(parser);
    }

    if (pragma_consume_word(text, length, &cursor, "pop")) {
        pragma_skip_horizontal_space(text, length, &cursor);
        if (cursor >= length || text[cursor] != ')') {
            minic_parser_error(parser, "unsupported pragma pack pop syntax");
            return false;
        }
        cursor += 1U;
        if (!pragma_only_trailing_space(text, length, cursor)) {
            minic_parser_error(parser, "unsupported pragma pack pop syntax");
            return false;
        }
        if (parser->record_pack_depth == 0U) {
            minic_parser_error(parser, "pragma pack pop has no matching push");
            return false;
        }
        parser->record_pack_depth -= 1U;
        parser->record_pack_alignment =
            parser->record_pack_stack[parser->record_pack_depth];
        return minic_parser_advance(parser);
    }

    alignment = 0U;
    if (cursor < length && text[cursor] != ')') {
        size_t digit_count;

        digit_count = 0U;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            size_t digit;

            digit = (size_t)(text[cursor] - '0');
            if (alignment > (SIZE_MAX - digit) / 10U) {
                minic_parser_error(parser, "unsupported pragma pack alignment");
                return false;
            }
            alignment = alignment * 10U + digit;
            cursor += 1U;
            digit_count += 1U;
        }
        if (digit_count == 0U || alignment == 0U || alignment > 16U ||
            (alignment & (alignment - 1U)) != 0U) {
            minic_parser_error(parser, "unsupported pragma pack alignment");
            return false;
        }
    }
    pragma_skip_horizontal_space(text, length, &cursor);
    if (cursor >= length || text[cursor] != ')') {
        minic_parser_error(parser, "expected ')' after pragma pack alignment");
        return false;
    }
    cursor += 1U;
    if (!pragma_only_trailing_space(text, length, cursor)) {
        minic_parser_error(parser, "unsupported pragma pack syntax");
        return false;
    }
    parser->record_pack_alignment = alignment;
    return minic_parser_advance(parser);
}
