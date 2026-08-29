#include "minipp_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool minipp_is_identifier_start(char value) {
    unsigned char ch = (unsigned char)value;
    return value == '_' || isalpha(ch) != 0;
}

static bool minipp_is_identifier_continue(char value) {
    unsigned char ch = (unsigned char)value;
    return value == '_' || isalnum(ch) != 0;
}

static const MiniPpMacro *minipp_find_macro(const MiniPpState *state,
                                            const char *name,
                                            size_t name_size) {
    size_t index;

    for (index = 0U; index < state->macro_count; ++index) {
        const MiniPpMacro *macro = &state->macros[index];
        if (strlen(macro->name) == name_size &&
            memcmp(macro->name, name, name_size) == 0) {
            return macro;
        }
    }
    return NULL;
}

static bool minipp_macro_is_disabled(const char *name,
                                     const char *const *disabled,
                                     size_t disabled_count) {
    size_t index;

    for (index = 0U; index < disabled_count; ++index) {
        if (strcmp(disabled[index], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool minipp_expand_text_recursive(MiniPpState *state,
                                         const char *text,
                                         MiniPpString *out,
                                         const char *const *disabled,
                                         size_t disabled_count,
                                         size_t depth) {
    size_t index = 0U;

    if (depth > MINIPP_MAX_EXPANSION_DEPTH) {
        fprintf(state->diagnostics, "minic-cpp: macro-expansion-depth\n");
        return false;
    }

    while (text[index] != '\0') {
        if (text[index] == '"' || text[index] == '\'') {
            char quote = text[index];
            if (!minipp_string_append_char(out, text[index])) {
                return false;
            }
            ++index;
            while (text[index] != '\0') {
                char value = text[index];
                if (!minipp_string_append_char(out, value)) {
                    return false;
                }
                ++index;
                if (value == '\\' && text[index] != '\0') {
                    if (!minipp_string_append_char(out, text[index])) {
                        return false;
                    }
                    ++index;
                    continue;
                }
                if (value == quote) {
                    break;
                }
            }
            continue;
        }

        if (minipp_is_identifier_start(text[index])) {
            size_t start = index;
            size_t length;
            const MiniPpMacro *macro;

            ++index;
            while (minipp_is_identifier_continue(text[index])) {
                ++index;
            }
            length = index - start;
            macro = minipp_find_macro(state, text + start, length);
            if (macro != NULL &&
                !minipp_macro_is_disabled(macro->name,
                                          disabled,
                                          disabled_count)) {
                const char **next_disabled;
                size_t next_count = disabled_count + 1U;
                bool ok;

                next_disabled = malloc(next_count * sizeof(*next_disabled));
                if (next_disabled == NULL) {
                    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                    return false;
                }
                if (disabled_count != 0U) {
                    memcpy(next_disabled,
                           disabled,
                           disabled_count * sizeof(*next_disabled));
                }
                next_disabled[disabled_count] = macro->name;
                ok = minipp_expand_text_recursive(state,
                                                  macro->replacement,
                                                  out,
                                                  next_disabled,
                                                  next_count,
                                                  depth + 1U);
                free(next_disabled);
                if (!ok) {
                    return false;
                }
                continue;
            }

            if (!minipp_string_append_n(out, text + start, length)) {
                return false;
            }
            continue;
        }

        if (!minipp_string_append_char(out, text[index])) {
            return false;
        }
        ++index;
    }

    return true;
}

bool minipp_expand_text(MiniPpState *state,
                        const char *text,
                        MiniPpString *out) {
    return minipp_expand_text_recursive(state, text, out, NULL, 0U, 0U);
}

bool minipp_strip_comments_line(MiniPpState *state,
                                const char *line,
                                size_t line_size,
                                MiniPpString *out) {
    size_t index = 0U;

    while (index < line_size) {
        if (state->in_block_comment) {
            if (index + 1U < line_size &&
                line[index] == '*' &&
                line[index + 1U] == '/') {
                state->in_block_comment = false;
                index += 2U;
                continue;
            }
            if (line[index] == '\n' &&
                !minipp_string_append_char(out, '\n')) {
                return false;
            }
            ++index;
            continue;
        }

        if (line[index] == '"' || line[index] == '\'') {
            char quote = line[index];
            if (!minipp_string_append_char(out, line[index])) {
                return false;
            }
            ++index;
            while (index < line_size) {
                char value = line[index];
                if (!minipp_string_append_char(out, value)) {
                    return false;
                }
                ++index;
                if (value == '\\' && index < line_size) {
                    if (!minipp_string_append_char(out, line[index])) {
                        return false;
                    }
                    ++index;
                    continue;
                }
                if (value == quote) {
                    break;
                }
            }
            continue;
        }

        if (index + 1U < line_size &&
            line[index] == '/' &&
            line[index + 1U] == '/') {
            if (line_size != 0U && line[line_size - 1U] == '\n') {
                return minipp_string_append_char(out, '\n');
            }
            return true;
        }

        if (index + 1U < line_size &&
            line[index] == '/' &&
            line[index + 1U] == '*') {
            if (!minipp_string_append_char(out, ' ')) {
                return false;
            }
            state->in_block_comment = true;
            index += 2U;
            continue;
        }

        if (!minipp_string_append_char(out, line[index])) {
            return false;
        }
        ++index;
    }

    return true;
}
