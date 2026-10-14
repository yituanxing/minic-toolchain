#include "linker_script_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool append_command(MiniLdScript *script, MiniLdScriptCommand command) {
    MiniLdScriptCommand *next;
    if (script->command_count == script->command_capacity) {
        size_t capacity = script->command_capacity == 0U
                              ? 32U
                              : script->command_capacity * 2U;
        if (capacity < script->command_capacity ||
            capacity > SIZE_MAX / sizeof(*script->commands)) {
            return false;
        }
        next = realloc(script->commands,
                       capacity * sizeof(*script->commands));
        if (next == NULL) {
            return false;
        }
        script->commands = next;
        script->command_capacity = capacity;
    }
    script->commands[script->command_count++] = command;
    return true;
}

static bool append_section_item(MiniLdScriptOutputSection *section,
                                MiniLdScriptSectionItem item) {
    MiniLdScriptSectionItem *next;
    if (section->item_count == section->item_capacity) {
        size_t capacity = section->item_capacity == 0U
                              ? 16U
                              : section->item_capacity * 2U;
        if (capacity < section->item_capacity ||
            capacity > SIZE_MAX / sizeof(*section->items)) {
            return false;
        }
        next = realloc(section->items,
                       capacity * sizeof(*section->items));
        if (next == NULL) {
            return false;
        }
        section->items = next;
        section->item_capacity = capacity;
    }
    section->items[section->item_count++] = item;
    return true;
}

static bool append_pattern_text(ScriptParser *parser,
                                bool keep,
                                bool sort,
                                MiniLdScriptOutputSection *section) {
    char buffer[512];
    size_t size = 0U;
    MiniLdScriptSectionItem item;

    if (parser->token.kind != TOKEN_IDENTIFIER) {
        return minild_script_parser_error(parser,
                                          "expected-input-section-pattern");
    }
    if (parser->token.length >= sizeof(buffer)) {
        return minild_script_parser_error(parser,
                                          "input-section-pattern-too-long");
    }
    memcpy(buffer, parser->token.begin, parser->token.length);
    size = parser->token.length;
    if (!minild_script_parser_next(parser)) {
        return false;
    }
    if (parser->token.kind == TOKEN_MINUS) {
        if (size + 1U >= sizeof(buffer)) {
            return minild_script_parser_error(parser,
                                              "input-section-pattern-too-long");
        }
        buffer[size++] = '-';
        if (!minild_script_parser_next(parser) ||
            parser->token.kind != TOKEN_IDENTIFIER ||
            size + parser->token.length >= sizeof(buffer)) {
            return minild_script_parser_error(parser,
                                              "invalid-hyphenated-section-pattern");
        }
        memcpy(buffer + size, parser->token.begin, parser->token.length);
        size += parser->token.length;
        if (!minild_script_parser_next(parser)) {
            return false;
        }
    }
    if (parser->token.kind == TOKEN_PLUS) {
        if (size + 1U >= sizeof(buffer)) {
            return minild_script_parser_error(parser,
                                              "input-section-pattern-too-long");
        }
        buffer[size++] = '+';
        if (!minild_script_parser_next(parser)) {
            return false;
        }
    }
    if (parser->token.kind == TOKEN_STAR) {
        if (size + 1U >= sizeof(buffer)) {
            return minild_script_parser_error(parser,
                                              "input-section-pattern-too-long");
        }
        buffer[size++] = '*';
        if (!minild_script_parser_next(parser)) {
            return false;
        }
    }
    buffer[size] = '\0';

    memset(&item, 0, sizeof(item));
    item.kind = MINILD_SCRIPT_SECTION_PATTERN;
    item.value.pattern.text = minild_script_strdup_range(buffer, size);
    item.value.pattern.keep = keep;
    item.value.pattern.sort = sort;
    item.value.pattern.common = strcmp(buffer, "COMMON") == 0;
    if (item.value.pattern.text == NULL ||
        !append_section_item(section, item)) {
        free(item.value.pattern.text);
        return minild_script_parser_error(parser,
                                          "out-of-memory:pattern");
    }
    return true;
}

static bool parse_pattern_list(ScriptParser *parser,
                               bool keep,
                               MiniLdScriptOutputSection *section) {
    if (!minild_script_expect(parser,
                              TOKEN_STAR,
                              "expected-'*'-before-section-patterns") ||
        !minild_script_expect(parser,
                              TOKEN_LPAREN,
                              "expected-'('-before-section-patterns")) {
        return false;
    }

    while (parser->token.kind != TOKEN_RPAREN) {
        bool sort = false;
        if (minild_script_token_is(parser, "SORT")) {
            sort = true;
            if (!minild_script_parser_next(parser) ||
                !minild_script_expect(parser,
                                      TOKEN_LPAREN,
                                      "expected-'('-after-SORT") ||
                !append_pattern_text(parser, keep, true, section) ||
                !minild_script_expect(parser,
                                      TOKEN_RPAREN,
                                      "expected-')'-after-SORT")) {
                return false;
            }
        } else if (!append_pattern_text(parser, keep, sort, section)) {
            return false;
        }
    }
    return minild_script_parser_next(parser);
}

static bool parse_section_body(ScriptParser *parser,
                               MiniLdScriptOutputSection *section) {
    if (!minild_script_expect(parser,
                              TOKEN_LBRACE,
                              "expected-'{'-for-output-section")) {
        return false;
    }

    while (parser->token.kind != TOKEN_RBRACE) {
        if (parser->token.kind == TOKEN_EOF) {
            return minild_script_parser_error(parser,
                                              "unexpected-eof-in-output-section");
        }
        if (minild_script_token_is(parser, "KEEP")) {
            if (!minild_script_parser_next(parser) ||
                !minild_script_expect(parser,
                                      TOKEN_LPAREN,
                                      "expected-'('-after-KEEP") ||
                !parse_pattern_list(parser, true, section) ||
                !minild_script_expect(parser,
                                      TOKEN_RPAREN,
                                      "expected-')'-after-KEEP")) {
                return false;
            }
            continue;
        }
        if (minild_script_token_is(parser, "BYTE")) {
            MiniLdScriptSectionItem item;
            MiniLdScriptExprId expression;
            memset(&item, 0, sizeof(item));
            if (!minild_script_parser_next(parser) ||
                !minild_script_expect(parser,
                                      TOKEN_LPAREN,
                                      "expected-'('-after-BYTE") ||
                !minild_script_parse_expression(parser, &expression) ||
                !minild_script_expect(parser,
                                      TOKEN_RPAREN,
                                      "expected-')'-after-BYTE")) {
                return false;
            }
            (void)minild_script_consume(parser, TOKEN_SEMICOLON);
            item.kind = MINILD_SCRIPT_SECTION_BYTE;
            item.value.expression = expression;
            if (!append_section_item(section, item)) {
                return minild_script_parser_error(parser,
                                                  "out-of-memory:BYTE");
            }
            continue;
        }
        if (minild_script_token_is(parser, "CONSTRUCTORS")) {
            MiniLdScriptSectionItem item;
            memset(&item, 0, sizeof(item));
            item.kind = MINILD_SCRIPT_SECTION_CONSTRUCTORS;
            if (!minild_script_parser_next(parser) ||
                !append_section_item(section, item)) {
                return minild_script_parser_error(parser,
                                                  "out-of-memory:CONSTRUCTORS");
            }
            continue;
        }
        if (parser->token.kind == TOKEN_STAR) {
            if (!parse_pattern_list(parser, false, section)) {
                return false;
            }
            continue;
        }
        if (parser->token.kind == TOKEN_IDENTIFIER) {
            ScriptToken name = parser->token;
            char *symbol_name;
            MiniLdScriptExprId expression;
            MiniLdScriptSectionItem item;
            bool is_dot;

            if (!minild_script_parser_next(parser)) {
                return false;
            }
            if (parser->token.kind != TOKEN_EQUAL) {
                parser->token = name;
                return minild_script_parser_error(parser,
                                                  "unsupported-output-section-item");
            }

            symbol_name = minild_script_strdup_range(name.begin, name.length);
            is_dot = name.length == 1U && name.begin[0] == '.';
            if (symbol_name == NULL ||
                !minild_script_parser_next(parser) ||
                !minild_script_parse_expression(parser, &expression) ||
                !minild_script_expect(parser,
                                      TOKEN_SEMICOLON,
                                      "expected-';'-after-assignment")) {
                free(symbol_name);
                return false;
            }

            memset(&item, 0, sizeof(item));
            if (is_dot) {
                item.kind = MINILD_SCRIPT_SECTION_SET_DOT;
                item.value.expression = expression;
                free(symbol_name);
            } else {
                item.kind = MINILD_SCRIPT_SECTION_DEFINE_SYMBOL;
                item.value.symbol.name = symbol_name;
                item.value.symbol.expression = expression;
            }
            if (!append_section_item(section, item)) {
                if (item.kind == MINILD_SCRIPT_SECTION_DEFINE_SYMBOL) {
                    free(item.value.symbol.name);
                }
                return minild_script_parser_error(parser,
                                                  "out-of-memory:section-assignment");
            }
            continue;
        }
        return minild_script_parser_error(parser,
                                          "unsupported-output-section-item");
    }
    return minild_script_parser_next(parser);
}

static bool parse_output_section(ScriptParser *parser,
                                 char *name,
                                 bool discard,
                                 MiniLdScriptExprId address) {
    MiniLdScriptOutputSection section;
    MiniLdScriptCommand command;

    memset(&section, 0, sizeof(section));
    section.name = name;
    section.discard = discard;
    section.address = address;
    section.at = MINILD_SCRIPT_EXPR_NONE;
    section.align = MINILD_SCRIPT_EXPR_NONE;

    if (!minild_script_expect(parser,
                              TOKEN_COLON,
                              "expected-':'-after-output-section")) {
        return false;
    }
    while (parser->token.kind != TOKEN_LBRACE) {
        if (minild_script_token_is(parser, "AT")) {
            if (!minild_script_parser_next(parser) ||
                !minild_script_expect(parser,
                                      TOKEN_LPAREN,
                                      "expected-'('-after-AT") ||
                !minild_script_parse_expression(parser, &section.at) ||
                !minild_script_expect(parser,
                                      TOKEN_RPAREN,
                                      "expected-')'-after-AT")) {
                return false;
            }
        } else if (minild_script_token_is(parser, "ALIGN")) {
            if (!minild_script_parser_next(parser) ||
                !minild_script_expect(parser,
                                      TOKEN_LPAREN,
                                      "expected-'('-after-ALIGN") ||
                !minild_script_parse_expression(parser, &section.align) ||
                !minild_script_expect(parser,
                                      TOKEN_RPAREN,
                                      "expected-')'-after-ALIGN")) {
                return false;
            }
        } else {
            return minild_script_parser_error(parser,
                                              "unsupported-output-section-attribute");
        }
    }

    if (!parse_section_body(parser, &section)) {
        return false;
    }
    while (parser->token.kind == TOKEN_COLON) {
        char **names;
        char *name_copy;
        if (!minild_script_parser_next(parser) ||
            parser->token.kind != TOKEN_IDENTIFIER) {
            return minild_script_parser_error(parser,
                                              "expected-output-section-phdr-name");
        }
        name_copy = minild_script_strdup_range(parser->token.begin,
                                                parser->token.length);
        if (name_copy == NULL ||
            section.phdr_name_count == SIZE_MAX / sizeof(*names)) {
            free(name_copy);
            return minild_script_parser_error(parser,
                                              "out-of-memory:section-phdr");
        }
        names = realloc(section.phdr_names,
                        (section.phdr_name_count + 1U) * sizeof(*names));
        if (names == NULL) {
            free(name_copy);
            return minild_script_parser_error(parser,
                                              "out-of-memory:section-phdr");
        }
        section.phdr_names = names;
        section.phdr_names[section.phdr_name_count++] = name_copy;
        if (!minild_script_parser_next(parser)) {
            return false;
        }
    }
    memset(&command, 0, sizeof(command));
    command.kind = MINILD_SCRIPT_OUTPUT_SECTION;
    command.value.section = section;
    return append_command(parser->script, command) ||
           minild_script_parser_error(parser,
                                      "out-of-memory:output-section");
}

static bool parse_sections(ScriptParser *parser) {
    if (!minild_script_parser_next(parser) ||
        !minild_script_expect(parser,
                              TOKEN_LBRACE,
                              "expected-'{'-after-SECTIONS")) {
        return false;
    }

    while (parser->token.kind != TOKEN_RBRACE) {
        char *name;
        MiniLdScriptExprId address = MINILD_SCRIPT_EXPR_NONE;
        ScriptToken first;

        if (parser->token.kind == TOKEN_EOF) {
            return minild_script_parser_error(parser,
                                              "unexpected-eof-in-SECTIONS");
        }
        if (parser->token.kind == TOKEN_SLASH) {
            if (!minild_script_parser_next(parser) ||
                !minild_script_token_is(parser, "DISCARD") ||
                !minild_script_parser_next(parser) ||
                !minild_script_expect(parser,
                                      TOKEN_SLASH,
                                      "expected-'/'-after-DISCARD")) {
                return minild_script_parser_error(parser,
                                                  "invalid-DISCARD-section");
            }
            name = minild_script_strdup_range("/DISCARD/", 9U);
            if (name == NULL) {
                return minild_script_parser_error(parser,
                                                  "out-of-memory:DISCARD");
            }
            if (!parse_output_section(parser,
                                      name,
                                      true,
                                      MINILD_SCRIPT_EXPR_NONE)) {
                free(name);
                return false;
            }
            continue;
        }
        if (parser->token.kind != TOKEN_IDENTIFIER) {
            return minild_script_parser_error(parser,
                                              "expected-SECTIONS-command");
        }

        /* GNU PROVIDE is conditional: referenced undefined symbols are
         * defined, but ordinary existing definitions are never replaced,
         * and unreferenced declarations do not become ELF exports. */
        if (minild_script_token_is(parser, "PROVIDE")) {
            MiniLdScriptCommand command;
            MiniLdScriptExprId expression;
            char *symbol_name;
            if (!minild_script_parser_next(parser) ||
                !minild_script_expect(parser, TOKEN_LPAREN,
                                      "expected-'('-after-PROVIDE") ||
                parser->token.kind != TOKEN_IDENTIFIER) {
                return minild_script_parser_error(parser,
                                                  "expected-PROVIDE-symbol");
            }
            symbol_name = minild_script_strdup_range(parser->token.begin,
                                                      parser->token.length);
            if (symbol_name == NULL ||
                !minild_script_parser_next(parser) ||
                !minild_script_expect(parser, TOKEN_EQUAL,
                                      "expected-'='-in-PROVIDE") ||
                !minild_script_parse_expression(parser, &expression) ||
                !minild_script_expect(parser, TOKEN_RPAREN,
                                      "expected-')'-after-PROVIDE")) {
                free(symbol_name);
                return false;
            }
            (void)minild_script_consume(parser, TOKEN_SEMICOLON);
            memset(&command, 0, sizeof(command));
            command.kind = MINILD_SCRIPT_DEFINE_SYMBOL;
            command.value.symbol.name = symbol_name;
            command.value.symbol.expression = expression;
            command.value.symbol.provide = true;
            if (!append_command(parser->script, command)) {
                free(symbol_name);
                return minild_script_parser_error(parser,
                                                  "out-of-memory:PROVIDE");
            }
            continue;
        }

        first = parser->token;
        name = minild_script_strdup_range(first.begin, first.length);
        if (name == NULL || !minild_script_parser_next(parser)) {
            free(name);
            return false;
        }
        if (parser->token.kind == TOKEN_EQUAL) {
            MiniLdScriptExprId expression;
            MiniLdScriptCommand command;
            bool is_dot = first.length == 1U && first.begin[0] == '.';

            if (!minild_script_parser_next(parser) ||
                !minild_script_parse_expression(parser, &expression) ||
                !minild_script_expect(parser,
                                      TOKEN_SEMICOLON,
                                      "expected-';'-after-assignment")) {
                free(name);
                return false;
            }
            memset(&command, 0, sizeof(command));
            if (is_dot) {
                command.kind = MINILD_SCRIPT_SET_DOT;
                command.value.expression = expression;
                free(name);
            } else {
                command.kind = MINILD_SCRIPT_DEFINE_SYMBOL;
                command.value.symbol.name = name;
                command.value.symbol.expression = expression;
            }
            if (!append_command(parser->script, command)) {
                return minild_script_parser_error(parser,
                                                  "out-of-memory:command");
            }
            continue;
        }

        if (parser->token.kind != TOKEN_COLON) {
            if (!minild_script_parse_expression(parser, &address)) {
                free(name);
                return false;
            }
        }
        if (!parse_output_section(parser, name, false, address)) {
            free(name);
            return false;
        }
    }
    return minild_script_parser_next(parser);
}

/* Preserve the full four-PHDR Linux vDSO contract in the script AST.
 * The shared ELF layout may only use these once each output section's
 * :phdr mapping and explicit flags are honored by the writer.
 */
static bool parse_phdrs(ScriptParser *parser) {
    if (!minild_script_parser_next(parser) ||
        !minild_script_expect(parser, TOKEN_LBRACE,
                              "expected-'{'-after-PHDRS")) {
        return false;
    }
    while (parser->token.kind != TOKEN_RBRACE) {
        MiniLdScriptPhdr phdr;
        MiniLdScriptPhdr *grown;
        if (parser->token.kind != TOKEN_IDENTIFIER) {
            return minild_script_parser_error(parser,
                                              "expected-PHDR-name");
        }
        memset(&phdr, 0, sizeof(phdr));
        phdr.name = minild_script_strdup_range(parser->token.begin,
                                                parser->token.length);
        if (phdr.name == NULL || !minild_script_parser_next(parser) ||
            parser->token.kind != TOKEN_IDENTIFIER) {
            free(phdr.name);
            return minild_script_parser_error(parser,
                                              "expected-PHDR-type");
        }
        phdr.type_name = minild_script_strdup_range(parser->token.begin,
                                                     parser->token.length);
        if (phdr.type_name == NULL ||
            (strcmp(phdr.type_name, "PT_LOAD") != 0 &&
             strcmp(phdr.type_name, "PT_DYNAMIC") != 0 &&
             strcmp(phdr.type_name, "PT_NOTE") != 0 &&
             strcmp(phdr.type_name, "PT_GNU_EH_FRAME") != 0) ||
            !minild_script_parser_next(parser)) {
            free(phdr.name);
            free(phdr.type_name);
            return minild_script_parser_error(parser,
                                              "unsupported-PHDR-type");
        }
        while (parser->token.kind != TOKEN_SEMICOLON) {
            if (parser->token.kind == TOKEN_EOF ||
                parser->token.kind == TOKEN_RBRACE) {
                free(phdr.name);
                free(phdr.type_name);
                return minild_script_parser_error(parser,
                                                  "expected-PHDR-semicolon");
            }
            if (minild_script_token_is(parser, "FLAGS")) {
                if (phdr.have_flags ||
                    !minild_script_parser_next(parser) ||
                    !minild_script_expect(parser, TOKEN_LPAREN,
                                          "expected-'('-after-PHDR-FLAGS") ||
                    parser->token.kind != TOKEN_NUMBER ||
                    parser->token.number > UINT32_MAX) {
                    free(phdr.name);
                    free(phdr.type_name);
                    return minild_script_parser_error(parser,
                                                      "invalid-PHDR-FLAGS");
                }
                phdr.have_flags = true;
                phdr.flags = (uint32_t)parser->token.number;
                if (!minild_script_parser_next(parser) ||
                    !minild_script_expect(parser, TOKEN_RPAREN,
                                          "expected-')'-after-PHDR-FLAGS")) {
                    free(phdr.name);
                    free(phdr.type_name);
                    return false;
                }
            } else if (minild_script_token_is(parser, "FILEHDR")) {
                phdr.filehdr = true;
                if (!minild_script_parser_next(parser)) {
                    free(phdr.name);
                    free(phdr.type_name);
                    return false;
                }
            } else if (minild_script_token_is(parser, "PHDRS")) {
                phdr.phdrs = true;
                if (!minild_script_parser_next(parser)) {
                    free(phdr.name);
                    free(phdr.type_name);
                    return false;
                }
            } else {
                free(phdr.name);
                free(phdr.type_name);
                return minild_script_parser_error(parser,
                                                  "unsupported-PHDR-attribute");
            }
        }
        if (parser->script->phdr_count ==
            parser->script->phdr_capacity) {
            size_t next = parser->script->phdr_capacity == 0U
                              ? 4U : parser->script->phdr_capacity * 2U;
            if (next < parser->script->phdr_capacity ||
                next > SIZE_MAX / sizeof(*grown)) {
                free(phdr.name);
                free(phdr.type_name);
                return minild_script_parser_error(parser,
                                                  "out-of-memory:PHDRS");
            }
            grown = realloc(parser->script->phdrs,
                            next * sizeof(*grown));
            if (grown == NULL) {
                free(phdr.name);
                free(phdr.type_name);
                return minild_script_parser_error(parser,
                                                  "out-of-memory:PHDRS");
            }
            parser->script->phdrs = grown;
            parser->script->phdr_capacity = next;
        }
        parser->script->phdrs[parser->script->phdr_count++] = phdr;
        if (!minild_script_parser_next(parser)) {
            return false;
        }
    }
    return minild_script_parser_next(parser);
}


/* Linux RISC-V's VERSION block contains one named node and explicit
 * global exports, followed by local: *;. Do not silently discard nodes
 * or wildcard semantics: reject anything outside this supported grammar.
 */
static bool parse_version(ScriptParser *parser) {
    MiniLdScript *script = parser->script;
    bool global = false;
    bool local = false;

    if (script->version_name != NULL ||
        !minild_script_parser_next(parser) ||
        !minild_script_expect(parser, TOKEN_LBRACE,
                              "expected-'{'-after-VERSION") ||
        parser->token.kind != TOKEN_IDENTIFIER) {
        return minild_script_parser_error(parser,
                                          "unsupported-VERSION-structure");
    }
    script->version_name = minild_script_strdup_range(parser->token.begin,
                                                       parser->token.length);
    if (script->version_name == NULL ||
        !minild_script_parser_next(parser) ||
        !minild_script_expect(parser, TOKEN_LBRACE,
                              "expected-'{'-after-version-name")) {
        return false;
    }
    while (parser->token.kind != TOKEN_RBRACE) {
        char *name;
        char **grown;
        size_t capacity;
        if (minild_script_token_is(parser, "global") ||
            minild_script_token_is(parser, "local")) {
            global = minild_script_token_is(parser, "global");
            local = !global;
            if (!minild_script_parser_next(parser) ||
                !minild_script_expect(parser, TOKEN_COLON,
                                      "expected-':'-after-version-scope")) {
                return false;
            }
            continue;
        }
        if (!global && !local) {
            return minild_script_parser_error(parser,
                                              "missing-VERSION-scope");
        }
        if (local && parser->token.kind == TOKEN_STAR) {
            if (script->version_local_all ||
                !minild_script_parser_next(parser) ||
                !minild_script_expect(parser, TOKEN_SEMICOLON,
                                      "expected-';'-after-local-wildcard")) {
                return false;
            }
            script->version_local_all = true;
            continue;
        }
        if (!global || parser->token.kind != TOKEN_IDENTIFIER) {
            return minild_script_parser_error(parser,
                                              "unsupported-VERSION-pattern");
        }
        name = minild_script_strdup_range(parser->token.begin,
                                           parser->token.length);
        if (name == NULL || !minild_script_parser_next(parser) ||
            !minild_script_expect(parser, TOKEN_SEMICOLON,
                                  "expected-';'-after-VERSION-export")) {
            free(name);
            return false;
        }
        if (script->version_global_count == script->version_global_capacity) {
            capacity = script->version_global_capacity == 0U ? 8U
                       : script->version_global_capacity * 2U;
            if (capacity < script->version_global_capacity ||
                capacity > SIZE_MAX / sizeof(*grown)) {
                free(name);
                return minild_script_parser_error(parser,
                                                  "out-of-memory:VERSION");
            }
            grown = realloc(script->version_globals,
                            capacity * sizeof(*grown));
            if (grown == NULL) {
                free(name);
                return minild_script_parser_error(parser,
                                                  "out-of-memory:VERSION");
            }
            script->version_globals = grown;
            script->version_global_capacity = capacity;
        }
        script->version_globals[script->version_global_count++] = name;
    }
    if (!minild_script_parser_next(parser) ||
        !minild_script_expect(parser, TOKEN_SEMICOLON,
                              "expected-';'-after-version-node") ||
        !minild_script_expect(parser, TOKEN_RBRACE,
                              "expected-'}'-after-VERSION")) {
        return false;
    }
    return true;
}

static bool parse_directive_with_identifier(ScriptParser *parser,
                                            char **value_out) {
    if (!minild_script_parser_next(parser) ||
        !minild_script_expect(parser,
                              TOKEN_LPAREN,
                              "expected-'('-after-directive") ||
        parser->token.kind != TOKEN_IDENTIFIER) {
        return minild_script_parser_error(parser,
                                          "expected-identifier-in-directive");
    }

    free(*value_out);
    *value_out = minild_script_strdup_range(parser->token.begin,
                                            parser->token.length);
    if (*value_out == NULL ||
        !minild_script_parser_next(parser) ||
        !minild_script_expect(parser,
                              TOKEN_RPAREN,
                              "expected-')'-after-directive")) {
        return false;
    }
    return true;
}

void minild_script_initialize(MiniLdScript *script) {
    memset(script, 0, sizeof(*script));
}

void minild_script_destroy(MiniLdScript *script) {
    size_t i;

    if (script == NULL) {
        return;
    }
    free(script->entry_symbol);
    free(script->output_arch);

    for (i = 0U; i < script->expression_count; ++i) {
        free(script->expressions[i].name);
    }
    for (i = 0U; i < script->command_count; ++i) {
        MiniLdScriptCommand *command = &script->commands[i];
        if (command->kind == MINILD_SCRIPT_DEFINE_SYMBOL) {
            free(command->value.symbol.name);
        } else if (command->kind == MINILD_SCRIPT_OUTPUT_SECTION) {
            MiniLdScriptOutputSection *section = &command->value.section;
            size_t j;

            free(section->name);
            for (j = 0U; j < section->item_count; ++j) {
                MiniLdScriptSectionItem *item = &section->items[j];
                if (item->kind == MINILD_SCRIPT_SECTION_PATTERN) {
                    free(item->value.pattern.text);
                } else if (item->kind ==
                           MINILD_SCRIPT_SECTION_DEFINE_SYMBOL) {
                    free(item->value.symbol.name);
                }
            }
            free(section->items);
            for (j = 0U; j < section->phdr_name_count; ++j) {
                free(section->phdr_names[j]);
            }
            free(section->phdr_names);
        }
    }
    for (i = 0U; i < script->phdr_count; ++i) {
        free(script->phdrs[i].name);
        free(script->phdrs[i].type_name);
    }
    free(script->phdrs);
    free(script->version_name);
    for (i = 0U; i < script->version_global_count; ++i) {
        free(script->version_globals[i]);
    }
    free(script->version_globals);

    free(script->expressions);
    free(script->commands);
    memset(script, 0, sizeof(*script));
}

bool minild_script_parse_file(const char *path,
                              MiniLdScript *script,
                              FILE *diagnostics) {
    ScriptParser parser;
    bool ok = false;

    if (path == NULL || script == NULL || diagnostics == NULL) {
        return false;
    }

    minild_script_destroy(script);
    minild_script_initialize(script);
    memset(&parser, 0, sizeof(parser));
    parser.path = path;
    parser.line = 1U;
    parser.column = 1U;
    parser.script = script;
    parser.diagnostics = diagnostics;

    if (!minild_script_read_entire_file(path,
                                        &parser.source,
                                        &parser.source_size)) {
        fprintf(diagnostics,
                "minic-ld: cannot-read-linker-script:%s:%s\n",
                path,
                strerror(errno));
        return false;
    }
    if (!minild_script_parser_next(&parser)) {
        goto done;
    }

    while (parser.token.kind != TOKEN_EOF) {
        if (minild_script_token_is(&parser, "OUTPUT_ARCH")) {
            if (!parse_directive_with_identifier(&parser,
                                                 &script->output_arch)) {
                goto done;
            }
        } else if (minild_script_token_is(&parser, "ENTRY")) {
            if (!parse_directive_with_identifier(&parser,
                                                 &script->entry_symbol)) {
                goto done;
            }
        } else if (minild_script_token_is(&parser, "SECTIONS")) {
            if (!parse_sections(&parser)) {
                goto done;
            }
        } else if (minild_script_token_is(&parser, "PHDRS")) {
            if (!parse_phdrs(&parser)) {
                goto done;
            }
        } else if (minild_script_token_is(&parser, "VERSION")) {
            if (!parse_version(&parser)) {
                goto done;
            }
        } else if (parser.token.kind == TOKEN_IDENTIFIER) {
            ScriptToken name = parser.token;
            char *symbol_name;
            MiniLdScriptExprId expression;
            MiniLdScriptCommand command;

            if (!minild_script_parser_next(&parser) ||
                parser.token.kind != TOKEN_EQUAL) {
                minild_script_parser_error(
                    &parser,
                    "unsupported-top-level-linker-script-command");
                goto done;
            }
            symbol_name = minild_script_strdup_range(name.begin,
                                                     name.length);
            if (symbol_name == NULL ||
                !minild_script_parser_next(&parser) ||
                !minild_script_parse_expression(&parser, &expression) ||
                !minild_script_expect(&parser,
                                      TOKEN_SEMICOLON,
                                      "expected-';'-after-assignment")) {
                free(symbol_name);
                goto done;
            }
            memset(&command, 0, sizeof(command));
            command.kind = MINILD_SCRIPT_DEFINE_SYMBOL;
            command.value.symbol.name = symbol_name;
            command.value.symbol.expression = expression;
            if (!append_command(script, command)) {
                free(symbol_name);
                minild_script_parser_error(&parser,
                                           "out-of-memory:command");
                goto done;
            }
        } else {
            minild_script_parser_error(
                &parser,
                "unsupported-top-level-linker-script-command");
            goto done;
        }
    }

    {
        size_t i;
        for (i = 0U; i < script->command_count; ++i) {
            const MiniLdScriptCommand *command = &script->commands[i];
            size_t j;
            if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION) {
                continue;
            }
            for (j = 0U; j < command->value.section.phdr_name_count; ++j) {
                const char *name = command->value.section.phdr_names[j];
                size_t k;
                for (k = 0U; k < script->phdr_count; ++k) {
                    if (strcmp(name, script->phdrs[k].name) == 0) {
                        break;
                    }
                }
                if (k == script->phdr_count) {
                    minild_script_parser_error(&parser,
                                              "unknown-output-section-PHDR");
                    goto done;
                }
            }
        }
    }
    ok = true;

done:
    free(parser.source);
    if (!ok) {
        minild_script_destroy(script);
    }
    return ok;
}
