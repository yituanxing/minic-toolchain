#include "minipp_internal.h"

#include <ctype.h>
#include <stdint.h>
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

typedef struct MiniPpArgList {
    MiniPpString *items;
    size_t count;
    size_t capacity;
} MiniPpArgList;

static void minipp_arg_list_destroy(MiniPpArgList *list) {
    size_t index;

    for (index = 0U; index < list->count; ++index) {
        minipp_string_destroy(&list->items[index]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static bool minipp_arg_list_append(MiniPpArgList *list,
                                   const char *text,
                                   size_t size) {
    size_t start = 0U;
    size_t end = size;
    MiniPpString *item;

    while (start < end &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\v' || text[start] == '\f')) {
        ++start;
    }
    while (end > start &&
           (text[end - 1U] == ' ' || text[end - 1U] == '\t' ||
            text[end - 1U] == '\v' || text[end - 1U] == '\f')) {
        --end;
    }

    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0U ? 4U : list->capacity * 2U;
        MiniPpString *next;

        if (capacity < list->capacity ||
            capacity > SIZE_MAX / sizeof(*next)) {
            return false;
        }
        next = realloc(list->items, capacity * sizeof(*next));
        if (next == NULL) {
            return false;
        }
        list->items = next;
        list->capacity = capacity;
    }

    item = &list->items[list->count];
    minipp_string_init(item);
    if (!minipp_string_append_n(item, text + start, end - start) ||
        !minipp_string_append_char(item, '\0')) {
        minipp_string_destroy(item);
        return false;
    }
    --item->size;
    ++list->count;
    return true;
}

static bool minipp_parse_invocation_args(MiniPpState *state,
                                         const char *text,
                                         size_t open_index,
                                         size_t expected_count,
                                         MiniPpArgList *args,
                                         size_t *after_index) {
    size_t index = open_index + 1U;
    size_t segment_start = index;
    size_t paren_depth = 1U;

    memset(args, 0, sizeof(*args));

    if (text[index] == ')' && expected_count == 0U) {
        *after_index = index + 1U;
        return true;
    }

    while (text[index] != '\0') {
        if (text[index] == '"' || text[index] == '\'') {
            char quote = text[index++];
            while (text[index] != '\0') {
                char value = text[index++];
                if (value == '\\' && text[index] != '\0') {
                    ++index;
                    continue;
                }
                if (value == quote) {
                    break;
                }
            }
            continue;
        }

        if (text[index] == '(') {
            ++paren_depth;
            ++index;
            continue;
        }
        if (text[index] == ')') {
            --paren_depth;
            if (paren_depth == 0U) {
                if (!minipp_arg_list_append(args,
                                            text + segment_start,
                                            index - segment_start)) {
                    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                    minipp_arg_list_destroy(args);
                    return false;
                }
                *after_index = index + 1U;
                return true;
            }
            ++index;
            continue;
        }
        if (text[index] == ',' && paren_depth == 1U) {
            if (!minipp_arg_list_append(args,
                                        text + segment_start,
                                        index - segment_start)) {
                fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                minipp_arg_list_destroy(args);
                return false;
            }
            ++index;
            segment_start = index;
            continue;
        }

        ++index;
    }

    state->expansion_incomplete = true;
    minipp_arg_list_destroy(args);
    return false;
}

static bool minipp_macro_param_index(const MiniPpMacro *macro,
                                     const char *name,
                                     size_t name_size,
                                     size_t *param_index) {
    size_t index;

    for (index = 0U; index < macro->param_count; ++index) {
        if (strlen(macro->params[index]) == name_size &&
            memcmp(macro->params[index], name, name_size) == 0) {
            *param_index = index;
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
                                         size_t depth);

static bool minipp_build_logical_args(MiniPpState *state,
                                      const MiniPpMacro *macro,
                                      const MiniPpArgList *raw_args,
                                      MiniPpArgList *logical_args) {
    size_t fixed_count;
    size_t index;
    MiniPpString variadic;

    memset(logical_args, 0, sizeof(*logical_args));

    if (!macro->variadic) {
        if (raw_args->count != macro->param_count) {
            fprintf(state->diagnostics,
                    "minic-cpp: macro-argument-count:%s:expected=%zu:actual=%zu\n",
                    macro->name,
                    macro->param_count,
                    raw_args->count);
            return false;
        }
        for (index = 0U; index < raw_args->count; ++index) {
            if (!minipp_arg_list_append(logical_args,
                                        raw_args->items[index].data,
                                        raw_args->items[index].size)) {
                fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                minipp_arg_list_destroy(logical_args);
                return false;
            }
        }
        return true;
    }

    if (macro->param_count == 0U) {
        fprintf(state->diagnostics,
                "minic-cpp: internal-variadic-without-parameter:%s\n",
                macro->name);
        return false;
    }

    fixed_count = macro->param_count - 1U;
    if (raw_args->count < fixed_count) {
        fprintf(state->diagnostics,
                "minic-cpp: macro-argument-count:%s:minimum=%zu:actual=%zu\n",
                macro->name,
                fixed_count,
                raw_args->count);
        return false;
    }

    for (index = 0U; index < fixed_count; ++index) {
        if (!minipp_arg_list_append(logical_args,
                                    raw_args->items[index].data,
                                    raw_args->items[index].size)) {
            fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
            minipp_arg_list_destroy(logical_args);
            return false;
        }
    }

    minipp_string_init(&variadic);
    for (index = fixed_count; index < raw_args->count; ++index) {
        if (index != fixed_count &&
            !minipp_string_append_char(&variadic, ',')) {
            goto oom;
        }
        if (!minipp_string_append_n(&variadic,
                                    raw_args->items[index].data,
                                    raw_args->items[index].size)) {
            goto oom;
        }
    }
    if (!minipp_string_append_char(&variadic, '\0')) {
        goto oom;
    }
    --variadic.size;
    if (!minipp_arg_list_append(logical_args,
                                variadic.data,
                                variadic.size)) {
        goto oom;
    }
    minipp_string_destroy(&variadic);
    return true;

oom:
    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
    minipp_string_destroy(&variadic);
    minipp_arg_list_destroy(logical_args);
    return false;
}

static bool minipp_param_is_paste_operand(const MiniPpMacro *macro,
                                          size_t start,
                                          size_t end) {
    size_t left = start;
    size_t right = end;

    while (left != 0U &&
           (macro->replacement[left - 1U] == ' ' ||
            macro->replacement[left - 1U] == '\t' ||
            macro->replacement[left - 1U] == '\v' ||
            macro->replacement[left - 1U] == '\f')) {
        --left;
    }
    if (left >= 2U &&
        macro->replacement[left - 1U] == '#' &&
        macro->replacement[left - 2U] == '#') {
        return true;
    }

    while (macro->replacement[right] == ' ' ||
           macro->replacement[right] == '\t' ||
           macro->replacement[right] == '\v' ||
           macro->replacement[right] == '\f') {
        ++right;
    }
    return macro->replacement[right] == '#' &&
           macro->replacement[right + 1U] == '#';
}

static bool minipp_append_stringized_arg(MiniPpString *out,
                                         const MiniPpString *arg) {
    size_t index;
    bool pending_space = false;
    bool emitted = false;

    if (!minipp_string_append_char(out, '"')) {
        return false;
    }

    for (index = 0U; index < arg->size; ++index) {
        unsigned char ch = (unsigned char)arg->data[index];

        if (isspace(ch) != 0) {
            if (emitted) {
                pending_space = true;
            }
            continue;
        }

        if (pending_space) {
            if (!minipp_string_append_char(out, ' ')) {
                return false;
            }
            pending_space = false;
        }

        if (arg->data[index] == '\\' || arg->data[index] == '"') {
            if (!minipp_string_append_char(out, '\\')) {
                return false;
            }
        }
        if (!minipp_string_append_char(out, arg->data[index])) {
            return false;
        }
        emitted = true;
    }

    return minipp_string_append_char(out, '"');
}

static bool minipp_substitute_function_macro(MiniPpState *state,
                                             const MiniPpMacro *macro,
                                             const MiniPpArgList *raw_args,
                                             const MiniPpArgList *expanded_args,
                                             MiniPpString *substituted) {
    size_t index = 0U;

    minipp_string_init(substituted);

    while (macro->replacement[index] != '\0') {
        if (macro->replacement[index] == '"' ||
            macro->replacement[index] == '\'') {
            char quote = macro->replacement[index];
            if (!minipp_string_append_char(substituted, quote)) {
                goto oom;
            }
            ++index;
            while (macro->replacement[index] != '\0') {
                char value = macro->replacement[index];
                if (!minipp_string_append_char(substituted, value)) {
                    goto oom;
                }
                ++index;
                if (value == '\\' && macro->replacement[index] != '\0') {
                    if (!minipp_string_append_char(substituted,
                                                   macro->replacement[index])) {
                        goto oom;
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

        if (macro->replacement[index] == '#') {
            if (macro->replacement[index + 1U] == '#') {
                if (!minipp_string_append_n(substituted, "##", 2U)) {
                    goto oom;
                }
                index += 2U;
                continue;
            } else {
                size_t cursor = index + 1U;
                size_t length;
                size_t param_index;

                while (macro->replacement[cursor] == ' ' ||
                       macro->replacement[cursor] == '\t' ||
                       macro->replacement[cursor] == '\v' ||
                       macro->replacement[cursor] == '\f') {
                    ++cursor;
                }
                length = 0U;
                if (minipp_is_identifier_start(macro->replacement[cursor])) {
                    length = 1U;
                    while (minipp_is_identifier_continue(
                               macro->replacement[cursor + length])) {
                        ++length;
                    }
                }
                if (length == 0U ||
                    !minipp_macro_param_index(macro,
                                              macro->replacement + cursor,
                                              length,
                                              &param_index)) {
                    fprintf(state->diagnostics,
                            "minic-cpp: invalid-stringize:%s\n",
                            macro->name);
                    minipp_string_destroy(substituted);
                    return false;
                }
                if (!minipp_append_stringized_arg(substituted,
                                                  &raw_args->items[param_index])) {
                    goto oom;
                }
                index = cursor + length;
                continue;
            }
        }

        if (minipp_is_identifier_start(macro->replacement[index])) {
            size_t start = index;
            size_t length;
            size_t param_index;

            ++index;
            while (minipp_is_identifier_continue(macro->replacement[index])) {
                ++index;
            }
            length = index - start;
            if (minipp_macro_param_index(macro,
                                         macro->replacement + start,
                                         length,
                                         &param_index)) {
                const MiniPpArgList *source_args =
                    minipp_param_is_paste_operand(macro, start, index)
                        ? raw_args
                        : expanded_args;
                const MiniPpString *arg = &source_args->items[param_index];
                if (!minipp_string_append_n(substituted,
                                            arg->data == NULL ? "" : arg->data,
                                            arg->size)) {
                    goto oom;
                }
            } else if (!minipp_string_append_n(substituted,
                                               macro->replacement + start,
                                               length)) {
                goto oom;
            }
            continue;
        }

        if (!minipp_string_append_char(substituted,
                                       macro->replacement[index])) {
            goto oom;
        }
        ++index;
    }

    if (!minipp_string_append_char(substituted, '\0')) {
        goto oom;
    }
    --substituted->size;
    return true;

oom:
    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
    minipp_string_destroy(substituted);
    return false;
}

static bool minipp_apply_token_paste(MiniPpState *state,
                                     const MiniPpString *substituted,
                                     MiniPpString *pasted) {
    size_t index = 0U;

    minipp_string_init(pasted);
    while (index < substituted->size) {
        if (substituted->data[index] == '"' ||
            substituted->data[index] == '\'') {
            char quote = substituted->data[index];
            if (!minipp_string_append_char(pasted, quote)) {
                goto oom;
            }
            ++index;
            while (index < substituted->size) {
                char value = substituted->data[index];
                if (!minipp_string_append_char(pasted, value)) {
                    goto oom;
                }
                ++index;
                if (value == '\\' && index < substituted->size) {
                    if (!minipp_string_append_char(pasted,
                                                   substituted->data[index])) {
                        goto oom;
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

        if (index + 1U < substituted->size &&
            substituted->data[index] == '#' &&
            substituted->data[index + 1U] == '#') {
            while (pasted->size != 0U) {
                char previous = pasted->data[pasted->size - 1U];
                if (previous != ' ' && previous != '\t' &&
                    previous != '\v' && previous != '\f') {
                    break;
                }
                --pasted->size;
                if (pasted->data != NULL) {
                    pasted->data[pasted->size] = '\0';
                }
            }
            index += 2U;
            while (index < substituted->size &&
                   (substituted->data[index] == ' ' ||
                    substituted->data[index] == '\t' ||
                    substituted->data[index] == '\v' ||
                    substituted->data[index] == '\f')) {
                ++index;
            }
            continue;
        }

        if (!minipp_string_append_char(pasted, substituted->data[index])) {
            goto oom;
        }
        ++index;
    }

    if (!minipp_string_append_char(pasted, '\0')) {
        goto oom;
    }
    --pasted->size;
    return true;

oom:
    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
    minipp_string_destroy(pasted);
    return false;
}

static bool minipp_expand_function_macro(MiniPpState *state,
                                         const MiniPpMacro *macro,
                                         const char *text,
                                         size_t *index,
                                         MiniPpString *out,
                                         const char *const *disabled,
                                         size_t disabled_count,
                                         size_t depth,
                                         bool *invoked) {
    size_t cursor = *index;
    MiniPpArgList parsed_args;
    MiniPpArgList raw_args;
    MiniPpArgList expanded_args;
    MiniPpString substituted;
    MiniPpString pasted;
    const char **next_disabled = NULL;
    size_t next_count = disabled_count + 1U;
    size_t arg_index;
    bool ok = false;

    *invoked = false;
    while (text[cursor] == ' ' || text[cursor] == '\t' ||
           text[cursor] == '\v' || text[cursor] == '\f') {
        ++cursor;
    }
    if (text[cursor] != '(') {
        return true;
    }
    *invoked = true;

    if (!minipp_parse_invocation_args(state,
                                      text,
                                      cursor,
                                      macro->param_count,
                                      &parsed_args,
                                      index)) {
        return false;
    }
    if (!minipp_build_logical_args(state, macro, &parsed_args, &raw_args)) {
        minipp_arg_list_destroy(&parsed_args);
        return false;
    }
    minipp_arg_list_destroy(&parsed_args);

    memset(&expanded_args, 0, sizeof(expanded_args));
    if (macro->param_count != 0U) {
        expanded_args.items = calloc(macro->param_count,
                                     sizeof(*expanded_args.items));
        if (expanded_args.items == NULL) {
            fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
            minipp_arg_list_destroy(&raw_args);
            return false;
        }
        expanded_args.capacity = macro->param_count;
    }

    for (arg_index = 0U; arg_index < macro->param_count; ++arg_index) {
        MiniPpString *expanded = &expanded_args.items[arg_index];

        minipp_string_init(expanded);
        if (!minipp_expand_text_recursive(state,
                                          raw_args.items[arg_index].data,
                                          expanded,
                                          disabled,
                                          disabled_count,
                                          depth + 1U) ||
            !minipp_string_append_char(expanded, '\0')) {
            minipp_arg_list_destroy(&raw_args);
            expanded_args.count = arg_index + 1U;
            minipp_arg_list_destroy(&expanded_args);
            return false;
        }
        --expanded->size;
        ++expanded_args.count;
    }

    if (!minipp_substitute_function_macro(state,
                                          macro,
                                          &raw_args,
                                          &expanded_args,
                                          &substituted)) {
        minipp_arg_list_destroy(&raw_args);
        minipp_arg_list_destroy(&expanded_args);
        return false;
    }
    if (!minipp_apply_token_paste(state, &substituted, &pasted)) {
        minipp_string_destroy(&substituted);
        minipp_arg_list_destroy(&raw_args);
        minipp_arg_list_destroy(&expanded_args);
        return false;
    }

    next_disabled = malloc(next_count * sizeof(*next_disabled));
    if (next_disabled == NULL) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        goto done;
    }
    if (disabled_count != 0U) {
        memcpy(next_disabled,
               disabled,
               disabled_count * sizeof(*next_disabled));
    }
    next_disabled[disabled_count] = macro->name;

    ok = minipp_expand_text_recursive(state,
                                      pasted.data,
                                      out,
                                      next_disabled,
                                      next_count,
                                      depth + 1U);

done:
    free(next_disabled);
    minipp_string_destroy(&pasted);
    minipp_string_destroy(&substituted);
    minipp_arg_list_destroy(&raw_args);
    minipp_arg_list_destroy(&expanded_args);
    return ok;
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
                if (macro->function_like) {
                    bool invoked = false;

                    if (!minipp_expand_function_macro(state,
                                                      macro,
                                                      text,
                                                      &index,
                                                      out,
                                                      disabled,
                                                      disabled_count,
                                                      depth,
                                                      &invoked)) {
                        return false;
                    }
                    if (invoked) {
                        continue;
                    }
                } else {
                    const char **next_disabled;
                    size_t next_count = disabled_count + 1U;
                    bool ok;

                    next_disabled = malloc(next_count * sizeof(*next_disabled));
                    if (next_disabled == NULL) {
                        fprintf(state->diagnostics,
                                "minic-cpp: out-of-memory\n");
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
    state->expansion_incomplete = false;
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
