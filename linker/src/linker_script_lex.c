#include "linker_script_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

char *minild_script_strdup_range(const char *text, size_t length) {
    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

bool minild_script_parser_error(ScriptParser *parser, const char *message) {
    fprintf(parser->diagnostics,
            "minic-ld: linker-script:%s:%zu:%zu:%s\n",
            parser->path,
            parser->token.line,
            parser->token.column,
            message);
    return false;
}

bool minild_script_read_entire_file(const char *path, char **data_out, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    long end;
    size_t size;
    char *data;

    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (end = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    size = (size_t)end;
    data = malloc(size + 1U);
    if (data == NULL) {
        fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        free(data);
        fclose(file);
        return false;
    }
    data[size] = '\0';
    if (fclose(file) != 0) {
        free(data);
        return false;
    }
    *data_out = data;
    *size_out = size;
    return true;
}

static bool identifier_start(unsigned char c) {
    return isalpha(c) != 0 || c == '_' || c == '$' || c == '.';
}

static bool identifier_continue(unsigned char c) {
    return isalnum(c) != 0 || c == '_' || c == '$' || c == '.';
}

static void parser_advance_char(ScriptParser *parser) {
    if (parser->offset >= parser->source_size) {
        return;
    }
    if (parser->source[parser->offset] == '\n') {
        ++parser->line;
        parser->column = 1U;
    } else {
        ++parser->column;
    }
    ++parser->offset;
}

static void parser_skip_space_and_comments(ScriptParser *parser) {
    for (;;) {
        while (parser->offset < parser->source_size &&
               isspace((unsigned char)parser->source[parser->offset]) != 0) {
            parser_advance_char(parser);
        }
        if (parser->offset + 1U < parser->source_size &&
            parser->source[parser->offset] == '/' &&
            parser->source[parser->offset + 1U] == '*') {
            parser_advance_char(parser);
            parser_advance_char(parser);
            while (parser->offset + 1U < parser->source_size &&
                   !(parser->source[parser->offset] == '*' &&
                     parser->source[parser->offset + 1U] == '/')) {
                parser_advance_char(parser);
            }
            if (parser->offset + 1U < parser->source_size) {
                parser_advance_char(parser);
                parser_advance_char(parser);
            }
            continue;
        }
        break;
    }
}

bool minild_script_parser_next(ScriptParser *parser) {
    const char *start;
    size_t start_offset;
    size_t line;
    size_t column;
    unsigned char c;

    parser_skip_space_and_comments(parser);
    start_offset = parser->offset;
    line = parser->line;
    column = parser->column;
    if (start_offset >= parser->source_size) {
        memset(&parser->token, 0, sizeof(parser->token));
        parser->token.kind = TOKEN_EOF;
        parser->token.begin = parser->source + parser->source_size;
        parser->token.line = line;
        parser->token.column = column;
        return true;
    }
    start = parser->source + start_offset;
    c = (unsigned char)*start;
    memset(&parser->token, 0, sizeof(parser->token));
    parser->token.begin = start;
    parser->token.line = line;
    parser->token.column = column;

    if (identifier_start(c)) {
        parser_advance_char(parser);
        while (parser->offset < parser->source_size &&
               identifier_continue((unsigned char)parser->source[parser->offset])) {
            parser_advance_char(parser);
        }
        parser->token.kind = TOKEN_IDENTIFIER;
        parser->token.length = parser->offset - start_offset;
        return true;
    }
    if (isdigit(c) != 0) {
        char *end_pointer;
        unsigned long long value;
        errno = 0;
        value = strtoull(start, &end_pointer, 0);
        if (errno != 0 || end_pointer == start) {
            return minild_script_parser_error(parser, "invalid-number");
        }
        while (parser->offset < (size_t)(end_pointer - parser->source)) {
            parser_advance_char(parser);
        }
        parser->token.kind = TOKEN_NUMBER;
        parser->token.length = parser->offset - start_offset;
        parser->token.number = (uint64_t)value;
        return true;
    }

    parser_advance_char(parser);
    parser->token.length = 1U;
    switch (c) {
    case '{': parser->token.kind = TOKEN_LBRACE; return true;
    case '}': parser->token.kind = TOKEN_RBRACE; return true;
    case '(': parser->token.kind = TOKEN_LPAREN; return true;
    case ')': parser->token.kind = TOKEN_RPAREN; return true;
    case ':': parser->token.kind = TOKEN_COLON; return true;
    case ';': parser->token.kind = TOKEN_SEMICOLON; return true;
    case '=': parser->token.kind = TOKEN_EQUAL; return true;
    case '*': parser->token.kind = TOKEN_STAR; return true;
    case '+': parser->token.kind = TOKEN_PLUS; return true;
    case '-': parser->token.kind = TOKEN_MINUS; return true;
    case '/': parser->token.kind = TOKEN_SLASH; return true;
    case '<':
        if (parser->offset < parser->source_size &&
            parser->source[parser->offset] == '<') {
            parser_advance_char(parser);
            parser->token.kind = TOKEN_SHIFT_LEFT;
            parser->token.length = 2U;
            return true;
        }
        break;
    }
    return minild_script_parser_error(parser, "unexpected-character");
}

bool minild_script_token_is(const ScriptParser *parser, const char *text) {
    size_t length = strlen(text);
    return parser->token.kind == TOKEN_IDENTIFIER &&
           parser->token.length == length &&
           memcmp(parser->token.begin, text, length) == 0;
}

bool minild_script_consume(ScriptParser *parser, ScriptTokenKind kind) {
    if (parser->token.kind != kind) {
        return false;
    }
    return minild_script_parser_next(parser);
}

bool minild_script_expect(ScriptParser *parser,
                          ScriptTokenKind kind,
                          const char *message) {
    if (parser->token.kind != kind) {
        return minild_script_parser_error(parser, message);
    }
    return minild_script_parser_next(parser);
}
