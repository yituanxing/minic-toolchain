#include "minipp_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char *minipp_duplicate_range(const char *text, size_t size) {
    char *copy = malloc(size + 1U);
    if (copy == NULL) {
        return NULL;
    }
    if (size != 0U) {
        memcpy(copy, text, size);
    }
    copy[size] = '\0';
    return copy;
}

static const char *minipp_skip_horizontal_space(const char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\v' || *text == '\f') {
        ++text;
    }
    return text;
}

static size_t minipp_identifier_length(const char *text) {
    size_t size = 0U;
    unsigned char first = (unsigned char)text[0];

    if (!(text[0] == '_' || isalpha(first) != 0)) {
        return 0U;
    }
    ++size;
    while (text[size] != '\0') {
        unsigned char ch = (unsigned char)text[size];
        if (!(text[size] == '_' || isalnum(ch) != 0)) {
            break;
        }
        ++size;
    }
    return size;
}

static MiniPpMacro *minipp_find_macro(MiniPpState *state, const char *name) {
    size_t index;

    for (index = 0U; index < state->macro_count; ++index) {
        if (strcmp(state->macros[index].name, name) == 0) {
            return &state->macros[index];
        }
    }
    return NULL;
}

static bool minipp_reserve_macros(MiniPpState *state, size_t required) {
    size_t capacity;
    MiniPpMacro *next;

    if (required <= state->macro_capacity) {
        return true;
    }
    capacity = state->macro_capacity == 0U ? 32U : state->macro_capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    next = realloc(state->macros, capacity * sizeof(*next));
    if (next == NULL) {
        return false;
    }
    state->macros = next;
    state->macro_capacity = capacity;
    return true;
}

static bool minipp_define_macro(MiniPpState *state,
                                const char *name,
                                const char *replacement) {
    MiniPpMacro *macro = minipp_find_macro(state, name);
    char *next_replacement = minipp_duplicate_range(replacement,
                                                     strlen(replacement));
    if (next_replacement == NULL) {
        return false;
    }

    if (macro != NULL) {
        free(macro->replacement);
        macro->replacement = next_replacement;
        return true;
    }

    if (!minipp_reserve_macros(state, state->macro_count + 1U)) {
        free(next_replacement);
        return false;
    }
    macro = &state->macros[state->macro_count];
    macro->name = minipp_duplicate_range(name, strlen(name));
    if (macro->name == NULL) {
        free(next_replacement);
        return false;
    }
    macro->replacement = next_replacement;
    ++state->macro_count;
    return true;
}

static void minipp_undefine_macro(MiniPpState *state, const char *name) {
    size_t index;

    for (index = 0U; index < state->macro_count; ++index) {
        if (strcmp(state->macros[index].name, name) == 0) {
            free(state->macros[index].name);
            free(state->macros[index].replacement);
            if (index + 1U < state->macro_count) {
                memmove(&state->macros[index],
                        &state->macros[index + 1U],
                        (state->macro_count - index - 1U) *
                            sizeof(state->macros[0]));
            }
            --state->macro_count;
            return;
        }
    }
}

static void minipp_state_destroy(MiniPpState *state) {
    size_t index;

    for (index = 0U; index < state->macro_count; ++index) {
        free(state->macros[index].name);
        free(state->macros[index].replacement);
    }
    free(state->macros);
    free(state->conditionals);
    memset(state, 0, sizeof(*state));
}

static bool minipp_parse_command_line_define(MiniPpState *state,
                                              const char *definition) {
    const char *equals = strchr(definition, '=');
    char *name;
    const char *replacement;
    size_t name_size;
    bool ok;

    if (equals == NULL) {
        name_size = strlen(definition);
        replacement = "1";
    } else {
        name_size = (size_t)(equals - definition);
        replacement = equals + 1;
    }
    if (name_size == 0U) {
        fprintf(state->diagnostics, "minic-cpp: invalid-D:%s\n", definition);
        return false;
    }

    name = minipp_duplicate_range(definition, name_size);
    if (name == NULL) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        return false;
    }
    if (minipp_identifier_length(name) != name_size) {
        fprintf(state->diagnostics, "minic-cpp: invalid-D:%s\n", definition);
        free(name);
        return false;
    }
    ok = minipp_define_macro(state, name, replacement);
    free(name);
    if (!ok) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
    }
    return ok;
}

static bool minipp_reserve_conditionals(MiniPpState *state, size_t required) {
    size_t capacity;
    MiniPpConditional *next;

    if (required <= state->conditional_capacity) {
        return true;
    }
    capacity = state->conditional_capacity == 0U ? 16U :
                                                  state->conditional_capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    next = realloc(state->conditionals, capacity * sizeof(*next));
    if (next == NULL) {
        return false;
    }
    state->conditionals = next;
    state->conditional_capacity = capacity;
    return true;
}

static bool minipp_push_conditional(MiniPpState *state, bool condition) {
    MiniPpConditional *conditional;

    if (!minipp_reserve_conditionals(state, state->conditional_count + 1U)) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        return false;
    }
    conditional = &state->conditionals[state->conditional_count++];
    conditional->parent_active = state->active;
    conditional->branch_taken = state->active && condition;
    conditional->current_active = state->active && condition;
    conditional->else_seen = false;
    state->active = conditional->current_active;
    return true;
}

static bool minipp_parse_integer_text(const char *text, bool *value) {
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 0);
    if (errno != 0 || end == text) {
        return false;
    }
    end = (char *)minipp_skip_horizontal_space(end);
    if (*end == '\n') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    *value = parsed != 0L;
    return true;
}

static bool minipp_eval_if_expression(MiniPpState *state,
                                      const char *expression,
                                      bool *value) {
    const char *text = minipp_skip_horizontal_space(expression);
    size_t length;

    if (strncmp(text, "defined", 7U) == 0 &&
        !(text[7] == '_' || isalnum((unsigned char)text[7]) != 0)) {
        char name_buffer[256];
        const char *name_text;
        size_t name_size;

        text = minipp_skip_horizontal_space(text + 7);
        if (*text == '(') {
            ++text;
            text = minipp_skip_horizontal_space(text);
            name_text = text;
            name_size = minipp_identifier_length(name_text);
            if (name_size == 0U || name_size >= sizeof(name_buffer)) {
                return false;
            }
            text = minipp_skip_horizontal_space(name_text + name_size);
            if (*text != ')') {
                return false;
            }
        } else {
            name_text = text;
            name_size = minipp_identifier_length(name_text);
            if (name_size == 0U || name_size >= sizeof(name_buffer)) {
                return false;
            }
        }
        memcpy(name_buffer, name_text, name_size);
        name_buffer[name_size] = '\0';
        *value = minipp_find_macro(state, name_buffer) != NULL;
        return true;
    }

    length = minipp_identifier_length(text);
    if (length != 0U) {
        char name_buffer[256];
        MiniPpMacro *macro;

        if (length >= sizeof(name_buffer)) {
            return false;
        }
        memcpy(name_buffer, text, length);
        name_buffer[length] = '\0';
        text = minipp_skip_horizontal_space(text + length);
        if (*text == '\n') {
            ++text;
        }
        if (*text != '\0') {
            return false;
        }
        macro = minipp_find_macro(state, name_buffer);
        if (macro == NULL) {
            *value = false;
            return true;
        }
        return minipp_parse_integer_text(macro->replacement, value);
    }

    return minipp_parse_integer_text(text, value);
}

static bool minipp_parse_define(MiniPpState *state, const char *rest) {
    const char *name_text = minipp_skip_horizontal_space(rest);
    size_t name_size = minipp_identifier_length(name_text);
    const char *replacement_start;
    const char *replacement_end;
    char *name;
    char *replacement;
    bool ok;

    if (name_size == 0U) {
        fprintf(state->diagnostics, "minic-cpp: invalid-define\n");
        return false;
    }
    if (name_text[name_size] == '(') {
        fprintf(state->diagnostics,
                "minic-cpp: unsupported-function-macro:%.*s\n",
                (int)name_size,
                name_text);
        return false;
    }

    replacement_start = minipp_skip_horizontal_space(name_text + name_size);
    replacement_end = replacement_start + strlen(replacement_start);
    while (replacement_end > replacement_start &&
           (replacement_end[-1] == '\n' ||
            replacement_end[-1] == '\r' ||
            replacement_end[-1] == ' ' ||
            replacement_end[-1] == '\t')) {
        --replacement_end;
    }

    name = minipp_duplicate_range(name_text, name_size);
    replacement = minipp_duplicate_range(replacement_start,
                                         (size_t)(replacement_end -
                                                  replacement_start));
    if (name == NULL || replacement == NULL) {
        free(name);
        free(replacement);
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        return false;
    }
    ok = minipp_define_macro(state, name, replacement);
    free(name);
    free(replacement);
    if (!ok) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
    }
    return ok;
}

static bool minipp_parse_undef(MiniPpState *state, const char *rest) {
    const char *name_text = minipp_skip_horizontal_space(rest);
    size_t name_size = minipp_identifier_length(name_text);
    char *name;

    if (name_size == 0U) {
        fprintf(state->diagnostics, "minic-cpp: invalid-undef\n");
        return false;
    }
    name = minipp_duplicate_range(name_text, name_size);
    if (name == NULL) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        return false;
    }
    minipp_undefine_macro(state, name);
    free(name);
    return true;
}

static bool minipp_handle_conditional_directive(MiniPpState *state,
                                                 const char *directive,
                                                 const char *rest) {
    if (strcmp(directive, "ifdef") == 0 ||
        strcmp(directive, "ifndef") == 0) {
        const char *name_text = minipp_skip_horizontal_space(rest);
        size_t name_size = minipp_identifier_length(name_text);
        char *name;
        bool defined;
        bool condition;

        if (name_size == 0U) {
            fprintf(state->diagnostics, "minic-cpp: invalid-%s\n", directive);
            return false;
        }
        name = minipp_duplicate_range(name_text, name_size);
        if (name == NULL) {
            fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
            return false;
        }
        defined = minipp_find_macro(state, name) != NULL;
        free(name);
        condition = strcmp(directive, "ifdef") == 0 ? defined : !defined;
        return minipp_push_conditional(state, condition);
    }

    if (strcmp(directive, "if") == 0) {
        bool condition = false;
        if (state->active &&
            !minipp_eval_if_expression(state, rest, &condition)) {
            fprintf(state->diagnostics,
                    "minic-cpp: unsupported-if-expression:%s",
                    rest);
            return false;
        }
        return minipp_push_conditional(state, condition);
    }

    if (strcmp(directive, "elif") == 0) {
        MiniPpConditional *conditional;
        bool condition = false;

        if (state->conditional_count == 0U) {
            fprintf(state->diagnostics, "minic-cpp: elif-without-if\n");
            return false;
        }
        conditional = &state->conditionals[state->conditional_count - 1U];
        if (conditional->else_seen) {
            fprintf(state->diagnostics, "minic-cpp: elif-after-else\n");
            return false;
        }
        if (conditional->parent_active && !conditional->branch_taken &&
            !minipp_eval_if_expression(state, rest, &condition)) {
            fprintf(state->diagnostics,
                    "minic-cpp: unsupported-elif-expression:%s",
                    rest);
            return false;
        }
        conditional->current_active =
            conditional->parent_active && !conditional->branch_taken && condition;
        conditional->branch_taken =
            conditional->branch_taken || conditional->current_active;
        state->active = conditional->current_active;
        return true;
    }

    if (strcmp(directive, "else") == 0) {
        MiniPpConditional *conditional;

        if (state->conditional_count == 0U) {
            fprintf(state->diagnostics, "minic-cpp: else-without-if\n");
            return false;
        }
        conditional = &state->conditionals[state->conditional_count - 1U];
        if (conditional->else_seen) {
            fprintf(state->diagnostics, "minic-cpp: duplicate-else\n");
            return false;
        }
        conditional->else_seen = true;
        conditional->current_active =
            conditional->parent_active && !conditional->branch_taken;
        conditional->branch_taken = true;
        state->active = conditional->current_active;
        return true;
    }

    if (strcmp(directive, "endif") == 0) {
        MiniPpConditional *conditional;

        if (state->conditional_count == 0U) {
            fprintf(state->diagnostics, "minic-cpp: endif-without-if\n");
            return false;
        }
        conditional = &state->conditionals[state->conditional_count - 1U];
        state->active = conditional->parent_active;
        --state->conditional_count;
        return true;
    }

    return false;
}

static bool minipp_handle_directive(MiniPpState *state,
                                    const char *line,
                                    bool *handled) {
    const char *text = minipp_skip_horizontal_space(line);
    const char *directive_text;
    const char *rest;
    size_t directive_size;
    char directive[32];

    *handled = false;
    if (*text != '#') {
        return true;
    }
    *handled = true;
    text = minipp_skip_horizontal_space(text + 1);
    directive_text = text;
    directive_size = minipp_identifier_length(directive_text);
    if (directive_size == 0U) {
        return true;
    }
    if (directive_size >= sizeof(directive)) {
        fprintf(state->diagnostics, "minic-cpp: directive-too-long\n");
        return false;
    }
    memcpy(directive, directive_text, directive_size);
    directive[directive_size] = '\0';
    rest = directive_text + directive_size;

    if (strcmp(directive, "if") == 0 ||
        strcmp(directive, "ifdef") == 0 ||
        strcmp(directive, "ifndef") == 0 ||
        strcmp(directive, "elif") == 0 ||
        strcmp(directive, "else") == 0 ||
        strcmp(directive, "endif") == 0) {
        return minipp_handle_conditional_directive(state, directive, rest);
    }

    if (!state->active) {
        return true;
    }

    if (strcmp(directive, "define") == 0) {
        return minipp_parse_define(state, rest);
    }
    if (strcmp(directive, "undef") == 0) {
        return minipp_parse_undef(state, rest);
    }

    fprintf(state->diagnostics,
            "minic-cpp: unsupported-directive:%s\n",
            directive);
    return false;
}

static bool minipp_process_source(MiniPpState *state,
                                  const MiniPpString *input,
                                  MiniPpString *output) {
    size_t offset = 0U;

    while (offset < input->size) {
        size_t end = offset;
        MiniPpString stripped;
        bool handled = false;

        while (end < input->size && input->data[end] != '\n') {
            ++end;
        }
        if (end < input->size) {
            ++end;
        }

        minipp_string_init(&stripped);
        if (!minipp_strip_comments_line(state,
                                        input->data + offset,
                                        end - offset,
                                        &stripped)) {
            minipp_string_destroy(&stripped);
            fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
            return false;
        }

        if (!minipp_string_append_char(&stripped, '\0')) {
            minipp_string_destroy(&stripped);
            fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
            return false;
        }
        --stripped.size;

        if (!minipp_handle_directive(state, stripped.data, &handled)) {
            minipp_string_destroy(&stripped);
            return false;
        }
        if (!handled && state->active) {
            if (!minipp_expand_text(state, stripped.data, output)) {
                minipp_string_destroy(&stripped);
                fprintf(state->diagnostics, "minic-cpp: expansion-failed\n");
                return false;
            }
        }

        minipp_string_destroy(&stripped);
        offset = end;
    }

    if (state->in_block_comment) {
        fprintf(state->diagnostics, "minic-cpp: unterminated-comment\n");
        return false;
    }
    if (state->conditional_count != 0U) {
        fprintf(state->diagnostics, "minic-cpp: unterminated-conditional\n");
        return false;
    }
    return true;
}

int minipp_preprocess_file(const char *input_path,
                           const char *output_path,
                           const MiniPpConfig *config,
                           FILE *diagnostics) {
    MiniPpState state;
    MiniPpString input;
    MiniPpString output;
    size_t index;
    bool ok;

    if (!config->suppress_line_markers) {
        fprintf(diagnostics,
                "minic-cpp: m0-requires-P:line-markers-not-yet-implemented\n");
        return 2;
    }
    if (!config->inhibit_predefined_macros) {
        fprintf(diagnostics,
                "minic-cpp: m0-requires-undef:predefined-macros-not-yet-implemented\n");
        return 2;
    }
    if (!config->no_standard_includes) {
        fprintf(diagnostics,
                "minic-cpp: m0-requires-nostdinc:include-search-not-yet-implemented\n");
        return 2;
    }

    memset(&state, 0, sizeof(state));
    state.active = true;
    state.diagnostics = diagnostics;
    minipp_string_init(&input);
    minipp_string_init(&output);

    for (index = 0U; index < config->define_count; ++index) {
        if (!minipp_parse_command_line_define(&state, config->defines[index])) {
            minipp_state_destroy(&state);
            minipp_string_destroy(&input);
            minipp_string_destroy(&output);
            return 1;
        }
    }
    for (index = 0U; index < config->undefine_count; ++index) {
        minipp_undefine_macro(&state, config->undefines[index]);
    }

    ok = minipp_read_file(input_path, &input, diagnostics) &&
         minipp_process_source(&state, &input, &output) &&
         minipp_write_file(output_path,
                           output.data == NULL ? "" : output.data,
                           output.size,
                           diagnostics);

    minipp_state_destroy(&state);
    minipp_string_destroy(&input);
    minipp_string_destroy(&output);
    return ok ? 0 : 1;
}
