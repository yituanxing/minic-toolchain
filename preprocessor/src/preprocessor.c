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

static void minipp_release_macro_payload(MiniPpMacro *macro) {
    size_t index;

    free(macro->replacement);
    macro->replacement = NULL;
    for (index = 0U; index < macro->param_count; ++index) {
        free(macro->params[index]);
    }
    free(macro->params);
    macro->params = NULL;
    macro->param_count = 0U;
    macro->function_like = false;
    macro->variadic = false;
}

static bool minipp_define_macro_full(MiniPpState *state,
                                     const char *name,
                                     const char *replacement,
                                     char *const *params,
                                     size_t param_count,
                                     bool function_like,
                                     bool variadic) {
    MiniPpMacro *macro = minipp_find_macro(state, name);
    char *next_replacement;
    char **next_params = NULL;
    size_t index;

    next_replacement = minipp_duplicate_range(replacement,
                                              strlen(replacement));
    if (next_replacement == NULL) {
        return false;
    }

    if (param_count != 0U) {
        next_params = calloc(param_count, sizeof(*next_params));
        if (next_params == NULL) {
            free(next_replacement);
            return false;
        }
        for (index = 0U; index < param_count; ++index) {
            next_params[index] = minipp_duplicate_range(params[index],
                                                        strlen(params[index]));
            if (next_params[index] == NULL) {
                while (index != 0U) {
                    --index;
                    free(next_params[index]);
                }
                free(next_params);
                free(next_replacement);
                return false;
            }
        }
    }

    if (macro == NULL) {
        if (!minipp_reserve_macros(state, state->macro_count + 1U)) {
            for (index = 0U; index < param_count; ++index) {
                free(next_params[index]);
            }
            free(next_params);
            free(next_replacement);
            return false;
        }
        macro = &state->macros[state->macro_count];
        memset(macro, 0, sizeof(*macro));
        macro->name = minipp_duplicate_range(name, strlen(name));
        if (macro->name == NULL) {
            for (index = 0U; index < param_count; ++index) {
                free(next_params[index]);
            }
            free(next_params);
            free(next_replacement);
            return false;
        }
        ++state->macro_count;
    } else {
        minipp_release_macro_payload(macro);
    }

    macro->replacement = next_replacement;
    macro->params = next_params;
    macro->param_count = param_count;
    macro->function_like = function_like;
    macro->variadic = variadic;
    return true;
}

static bool minipp_define_macro(MiniPpState *state,
                                const char *name,
                                const char *replacement) {
    return minipp_define_macro_full(state,
                                    name,
                                    replacement,
                                    NULL,
                                    0U,
                                    false,
                                    false);
}

static void minipp_undefine_macro(MiniPpState *state, const char *name) {
    size_t index;

    for (index = 0U; index < state->macro_count; ++index) {
        if (strcmp(state->macros[index].name, name) == 0) {
            free(state->macros[index].name);
            minipp_release_macro_payload(&state->macros[index]);
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
        minipp_release_macro_payload(&state->macros[index]);
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

static void minipp_free_param_list(char **params, size_t param_count) {
    size_t index;

    for (index = 0U; index < param_count; ++index) {
        free(params[index]);
    }
    free(params);
}

static bool minipp_append_param(char ***params,
                                size_t *param_count,
                                size_t *param_capacity,
                                const char *text,
                                size_t size) {
    char **next;
    size_t capacity;

    if (*param_count == *param_capacity) {
        capacity = *param_capacity == 0U ? 4U : *param_capacity * 2U;
        if (capacity < *param_capacity ||
            capacity > SIZE_MAX / sizeof(**params)) {
            return false;
        }
        next = realloc(*params, capacity * sizeof(**params));
        if (next == NULL) {
            return false;
        }
        *params = next;
        *param_capacity = capacity;
    }

    (*params)[*param_count] = minipp_duplicate_range(text, size);
    if ((*params)[*param_count] == NULL) {
        return false;
    }
    ++*param_count;
    return true;
}

static bool minipp_parse_define(MiniPpState *state, const char *rest) {
    const char *name_text = minipp_skip_horizontal_space(rest);
    size_t name_size = minipp_identifier_length(name_text);
    const char *replacement_start;
    const char *replacement_end;
    char **params = NULL;
    size_t param_count = 0U;
    size_t param_capacity = 0U;
    bool function_like = false;
    bool variadic = false;
    char *name;
    char *replacement;
    bool ok;

    if (name_size == 0U) {
        fprintf(state->diagnostics, "minic-cpp: invalid-define\n");
        return false;
    }

    replacement_start = name_text + name_size;
    if (*replacement_start == '(') {
        const char *cursor = replacement_start + 1;

        function_like = true;
        cursor = minipp_skip_horizontal_space(cursor);
        if (*cursor != ')') {
            for (;;) {
                size_t param_size;

                if (strncmp(cursor, "...", 3U) == 0) {
                    if (!minipp_append_param(&params,
                                             &param_count,
                                             &param_capacity,
                                             "__VA_ARGS__",
                                             strlen("__VA_ARGS__"))) {
                        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                        minipp_free_param_list(params, param_count);
                        return false;
                    }
                    variadic = true;
                    cursor = minipp_skip_horizontal_space(cursor + 3);
                    if (*cursor != ')') {
                        fprintf(state->diagnostics,
                                "minic-cpp: invalid-variadic-parameter-list:%.*s\n",
                                (int)name_size,
                                name_text);
                        minipp_free_param_list(params, param_count);
                        return false;
                    }
                    break;
                }

                param_size = minipp_identifier_length(cursor);
                if (param_size == 0U) {
                    fprintf(state->diagnostics,
                            "minic-cpp: invalid-macro-parameter:%.*s\n",
                            (int)name_size,
                            name_text);
                    minipp_free_param_list(params, param_count);
                    return false;
                }
                if (!minipp_append_param(&params,
                                         &param_count,
                                         &param_capacity,
                                         cursor,
                                         param_size)) {
                    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                    minipp_free_param_list(params, param_count);
                    return false;
                }
                cursor = minipp_skip_horizontal_space(cursor + param_size);

                if (strncmp(cursor, "...", 3U) == 0) {
                    variadic = true;
                    cursor = minipp_skip_horizontal_space(cursor + 3);
                    if (*cursor != ')') {
                        fprintf(state->diagnostics,
                                "minic-cpp: invalid-gnu-variadic-parameter:%.*s\n",
                                (int)name_size,
                                name_text);
                        minipp_free_param_list(params, param_count);
                        return false;
                    }
                    break;
                }

                if (*cursor == ')') {
                    break;
                }
                if (*cursor != ',') {
                    fprintf(state->diagnostics,
                            "minic-cpp: invalid-macro-parameter-list:%.*s\n",
                            (int)name_size,
                            name_text);
                    minipp_free_param_list(params, param_count);
                    return false;
                }
                cursor = minipp_skip_horizontal_space(cursor + 1);
            }
        }
        replacement_start = minipp_skip_horizontal_space(cursor + 1);
    } else {
        replacement_start = minipp_skip_horizontal_space(replacement_start);
    }

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
        minipp_free_param_list(params, param_count);
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        return false;
    }

    if (function_like) {
        ok = minipp_define_macro_full(state,
                                      name,
                                      replacement,
                                      params,
                                      param_count,
                                      true,
                                      variadic);
    } else {
        ok = minipp_define_macro(state, name, replacement);
    }

    free(name);
    free(replacement);
    minipp_free_param_list(params, param_count);
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

static bool minipp_process_file(MiniPpState *state,
                                const char *path,
                                MiniPpString *output);

static bool minipp_handle_include(MiniPpState *state,
                                  const char *current_path,
                                  const char *rest,
                                  MiniPpString *output) {
    const char *text = minipp_skip_horizontal_space(rest);
    const char *name_start;
    const char *name_end;
    char terminator;
    bool angled;
    char *name = NULL;
    MiniPpString expanded;
    MiniPpString resolved;
    bool have_expanded = false;
    bool ok = false;

    minipp_string_init(&expanded);

    if (*text != '"' && *text != '<') {
        if (!minipp_expand_text(state, text, &expanded) ||
            !minipp_string_append_char(&expanded, '\0')) {
            if (state->expansion_incomplete) {
                fprintf(state->diagnostics,
                        "minic-cpp: unterminated-include-macro:%s",
                        rest);
            } else {
                fprintf(state->diagnostics,
                        "minic-cpp: include-macro-expansion-failed:%s",
                        rest);
            }
            goto done;
        }
        --expanded.size;
        have_expanded = true;
        text = minipp_skip_horizontal_space(expanded.data);
    }

    if (*text == '"') {
        angled = false;
        terminator = '"';
    } else if (*text == '<') {
        angled = true;
        terminator = '>';
    } else {
        fprintf(state->diagnostics,
                "minic-cpp: unsupported-include-expression:%s",
                have_expanded ? expanded.data : rest);
        goto done;
    }

    name_start = text + 1;
    name_end = name_start;
    while (*name_end != '\0' && *name_end != terminator &&
           *name_end != '\n') {
        ++name_end;
    }
    if (*name_end != terminator || name_end == name_start) {
        fprintf(state->diagnostics,
                "minic-cpp: invalid-include:%s",
                have_expanded ? expanded.data : rest);
        goto done;
    }

    text = name_end + 1;
    for (;;) {
        text = minipp_skip_horizontal_space(text);
        if (*text != '\b') {
            break;
        }
        ++text;
    }
    if (*text == '\n') {
        ++text;
    }
    if (*text != '\0') {
        fprintf(state->diagnostics,
                "minic-cpp: trailing-include-tokens:%s",
                have_expanded ? expanded.data : rest);
        goto done;
    }

    name = minipp_duplicate_range(name_start,
                                  (size_t)(name_end - name_start));
    if (name == NULL) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        goto done;
    }

    if (!minipp_resolve_include(state,
                                current_path,
                                name,
                                angled,
                                &resolved)) {
        fprintf(state->diagnostics,
                "minic-cpp: include-not-found:%s\n",
                name);
        goto done;
    }

    ok = minipp_process_file(state, resolved.data, output);
    minipp_string_destroy(&resolved);

done:
    free(name);
    minipp_string_destroy(&expanded);
    return ok;
}

static bool minipp_emit_pragma(MiniPpState *state,
                               const char *rest,
                               MiniPpString *output) {
    const char *text = minipp_skip_horizontal_space(rest);
    bool pending_space = false;

    if (!minipp_string_append_n(output, "#pragma", 7U)) {
        goto oom;
    }
    if (*text != '\0' && *text != '\n') {
        if (!minipp_string_append_char(output, ' ')) {
            goto oom;
        }
    }

    while (*text != '\0' && *text != '\n') {
        if (*text == ' ' || *text == '\t' ||
            *text == '\v' || *text == '\f') {
            pending_space = true;
            ++text;
            continue;
        }

        if (pending_space) {
            if (!minipp_string_append_char(output, ' ')) {
                goto oom;
            }
            pending_space = false;
        }

        if (*text == '"' || *text == '\'') {
            char quote = *text;
            if (!minipp_string_append_char(output, *text++)) {
                goto oom;
            }
            while (*text != '\0' && *text != '\n') {
                char value = *text;
                if (!minipp_string_append_char(output, value)) {
                    goto oom;
                }
                ++text;
                if (value == '\\' && *text != '\0' && *text != '\n') {
                    if (!minipp_string_append_char(output, *text++)) {
                        goto oom;
                    }
                    continue;
                }
                if (value == quote) {
                    break;
                }
            }
            continue;
        }

        if (!minipp_string_append_char(output, *text++)) {
            goto oom;
        }
    }

    if (!minipp_string_append_char(output, '\n')) {
        goto oom;
    }
    return true;

oom:
    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
    return false;
}

static bool minipp_handle_directive(MiniPpState *state,
                                    const char *current_path,
                                    const char *line,
                                    MiniPpString *output,
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
    if (strcmp(directive, "include") == 0) {
        return minipp_handle_include(state, current_path, rest, output);
    }
    if (strcmp(directive, "pragma") == 0) {
        return minipp_emit_pragma(state, rest, output);
    }

    fprintf(state->diagnostics,
            "minic-cpp: unsupported-directive:%s\n",
            directive);
    return false;
}

static bool minipp_try_flush_pending(MiniPpState *state,
                                     MiniPpString *pending,
                                     MiniPpString *output,
                                     size_t source_line,
                                     bool final) {
    MiniPpString expanded;
    size_t saved_line = state->current_line;
    size_t saved_counter = state->counter_value;
    bool ok;

    if (pending->size == 0U) {
        return true;
    }

    minipp_string_init(&expanded);
    state->current_line = source_line;
    ok = minipp_expand_text(state, pending->data, &expanded);
    state->current_line = saved_line;
    if (!ok) {
        bool incomplete = state->expansion_incomplete;
        minipp_string_destroy(&expanded);
        if (incomplete && !final) {
            state->counter_value = saved_counter;
            return true;
        }
        if (incomplete) {
            fprintf(state->diagnostics,
                    "minic-cpp: unterminated-macro-invocation\n");
        } else {
            fprintf(state->diagnostics, "minic-cpp: expansion-failed\n");
        }
        return false;
    }

    if (!minipp_string_append_n(output,
                                expanded.data == NULL ? "" : expanded.data,
                                expanded.size)) {
        minipp_string_destroy(&expanded);
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        return false;
    }
    minipp_string_destroy(&expanded);

    pending->size = 0U;
    if (pending->data != NULL) {
        pending->data[0] = '\0';
    }
    return true;
}

static bool minipp_build_logical_line_numbers(const MiniPpString *input,
                                              size_t **numbers_out,
                                              size_t *count_out) {
    size_t *numbers = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    size_t physical_line = 1U;
    size_t index = 0U;

#define MINIPP_PUSH_LINE_NUMBER(value) do {                                      \
        if (count == capacity) {                                                 \
            size_t next_capacity = capacity == 0U ? 64U : capacity * 2U;        \
            size_t *next;                                                        \
            if (next_capacity < capacity ||                                     \
                next_capacity > SIZE_MAX / sizeof(*numbers)) {                   \
                free(numbers);                                                   \
                return false;                                                    \
            }                                                                    \
            next = realloc(numbers, next_capacity * sizeof(*numbers));           \
            if (next == NULL) {                                                  \
                free(numbers);                                                   \
                return false;                                                    \
            }                                                                    \
            numbers = next;                                                      \
            capacity = next_capacity;                                            \
        }                                                                        \
        numbers[count++] = (value);                                              \
    } while (0)

    MINIPP_PUSH_LINE_NUMBER(physical_line);
    while (index < input->size) {
        if (input->data[index] == '\\' && index + 1U < input->size) {
            if (input->data[index + 1U] == '\n') {
                ++physical_line;
                index += 2U;
                continue;
            }
            if (input->data[index + 1U] == '\r' &&
                index + 2U < input->size &&
                input->data[index + 2U] == '\n') {
                ++physical_line;
                index += 3U;
                continue;
            }
        }
        if (input->data[index] == '\n') {
            ++physical_line;
            MINIPP_PUSH_LINE_NUMBER(physical_line);
        }
        ++index;
    }

#undef MINIPP_PUSH_LINE_NUMBER
    *numbers_out = numbers;
    *count_out = count;
    return true;
}

static bool minipp_line_has_nonspace(const MiniPpString *line) {
    size_t index;

    for (index = 0U; index < line->size; ++index) {
        unsigned char ch = (unsigned char)line->data[index];
        if (ch != ' ' && ch != '\t' && ch != '\v' &&
            ch != '\f' && ch != '\r' && ch != '\n') {
            return true;
        }
    }
    return false;
}

static bool minipp_process_source(MiniPpState *state,
                                  const char *current_path,
                                  const MiniPpString *input,
                                  const size_t *line_numbers,
                                  size_t line_number_count,
                                  MiniPpString *output) {
    size_t offset = 0U;
    size_t logical_line = 0U;
    size_t pending_line = 0U;
    MiniPpString pending;

    minipp_string_init(&pending);

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

        state->current_file = current_path;
        state->current_line =
            logical_line < line_number_count ? line_numbers[logical_line] :
                                               state->current_line;
        minipp_string_init(&stripped);
        if (!minipp_strip_comments_line(state,
                                        input->data + offset,
                                        end - offset,
                                        &stripped)) {
            minipp_string_destroy(&stripped);
            minipp_string_destroy(&pending);
            fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
            return false;
        }

        if (!minipp_string_append_char(&stripped, '\0')) {
            minipp_string_destroy(&stripped);
            minipp_string_destroy(&pending);
            fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
            return false;
        }
        --stripped.size;

        if (!minipp_handle_directive(state,
                                     current_path,
                                     stripped.data,
                                     output,
                                     &handled)) {
            minipp_string_destroy(&stripped);
            minipp_string_destroy(&pending);
            return false;
        }

        if (!handled && state->active) {
            if (pending.size == 0U && !minipp_line_has_nonspace(&stripped)) {
                minipp_string_destroy(&stripped);
                offset = end;
                ++logical_line;
                continue;
            }
            if (pending.size == 0U) {
                pending_line = state->current_line;
            }
            if (!minipp_string_append_n(&pending,
                                        stripped.data == NULL ? "" : stripped.data,
                                        stripped.size)) {
                minipp_string_destroy(&stripped);
                minipp_string_destroy(&pending);
                fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                return false;
            }
            if (!minipp_try_flush_pending(state,
                                          &pending,
                                          output,
                                          pending_line,
                                          false)) {
                minipp_string_destroy(&stripped);
                minipp_string_destroy(&pending);
                return false;
            }
        }

        minipp_string_destroy(&stripped);
        offset = end;
        ++logical_line;
    }

    if (!minipp_try_flush_pending(state,
                                  &pending,
                                  output,
                                  pending_line,
                                  true)) {
        minipp_string_destroy(&pending);
        return false;
    }

    minipp_string_destroy(&pending);
    return true;
}

static bool minipp_process_file(MiniPpState *state,
                                const char *path,
                                MiniPpString *output) {
    MiniPpString input;
    MiniPpString logical;
    size_t *line_numbers = NULL;
    size_t line_number_count = 0U;
    size_t conditional_base = state->conditional_count;
    bool previous_comment_state = state->in_block_comment;
    const char *previous_file = state->current_file;
    size_t previous_line = state->current_line;
    bool ok;

    minipp_string_init(&input);
    minipp_string_init(&logical);
    state->in_block_comment = false;

    ok = minipp_read_file(path, &input, state->diagnostics) &&
         minipp_build_logical_line_numbers(&input,
                                           &line_numbers,
                                           &line_number_count) &&
         minipp_splice_backslash_newlines(&input, &logical) &&
         minipp_process_source(state,
                               path,
                               &logical,
                               line_numbers,
                               line_number_count,
                               output);

    if (!ok && logical.data == NULL && input.data != NULL) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
    }
    if (ok && state->in_block_comment) {
        fprintf(state->diagnostics,
                "minic-cpp: unterminated-comment:%s\n",
                path);
        ok = false;
    }
    if (ok && state->conditional_count != conditional_base) {
        fprintf(state->diagnostics,
                "minic-cpp: unterminated-conditional:%s\n",
                path);
        ok = false;
    }

    state->in_block_comment = previous_comment_state;
    state->current_file = previous_file;
    state->current_line = previous_line;
    free(line_numbers);
    minipp_string_destroy(&logical);
    minipp_string_destroy(&input);
    return ok;
}

int minipp_preprocess_file(const char *input_path,
                           const char *output_path,
                           const MiniPpConfig *config,
                           FILE *diagnostics) {
    MiniPpState state;
    MiniPpString output;
    MiniPpString rendered;
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
                "minic-cpp: m0-requires-nostdinc:standard-include-search-not-yet-implemented\n");
        return 2;
    }

    memset(&state, 0, sizeof(state));
    state.active = true;
    state.include_paths = config->include_paths;
    state.include_path_count = config->include_path_count;
    state.diagnostics = diagnostics;
    minipp_string_init(&output);
    minipp_string_init(&rendered);

    for (index = 0U; index < config->define_count; ++index) {
        if (!minipp_parse_command_line_define(&state, config->defines[index])) {
            minipp_state_destroy(&state);
            minipp_string_destroy(&rendered);
            minipp_string_destroy(&output);
            return 1;
        }
    }
    for (index = 0U; index < config->undefine_count; ++index) {
        minipp_undefine_macro(&state, config->undefines[index]);
    }

    ok = true;
    for (index = 0U; index < config->forced_include_count && ok; ++index) {
        MiniPpString resolved;

        if (!minipp_resolve_include(&state,
                                    "",
                                    config->forced_includes[index],
                                    false,
                                    &resolved)) {
            fprintf(diagnostics,
                    "minic-cpp: forced-include-not-found:%s\n",
                    config->forced_includes[index]);
            ok = false;
            break;
        }
        ok = minipp_process_file(&state, resolved.data, &output);
        minipp_string_destroy(&resolved);
    }

    ok = ok && minipp_process_file(&state, input_path, &output);
    if (ok && !minipp_render_gcc_p_output(&output, &rendered)) {
        fprintf(diagnostics, "minic-cpp: out-of-memory\n");
        ok = false;
    }
    if (ok) {
        ok = minipp_write_file(output_path,
                               rendered.data == NULL ? "" : rendered.data,
                               rendered.size,
                               diagnostics);
    }

    minipp_state_destroy(&state);
    minipp_string_destroy(&rendered);
    minipp_string_destroy(&output);
    return ok ? 0 : 1;
}
