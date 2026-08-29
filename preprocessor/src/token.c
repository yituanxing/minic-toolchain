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
    bool *leading_space;
    size_t *source_line;
    size_t count;
    size_t capacity;
} MiniPpArgList;

static void minipp_arg_list_destroy(MiniPpArgList *list) {
    size_t index;

    for (index = 0U; index < list->count; ++index) {
        minipp_string_destroy(&list->items[index]);
    }
    free(list->items);
    free(list->leading_space);
    free(list->source_line);
    memset(list, 0, sizeof(*list));
}

static bool minipp_append_normalized_argument(MiniPpString *item,
                                              const char *text,
                                              size_t size) {
    size_t index = 0U;
    bool pending_space = false;
    bool have_token = false;

    while (index < size) {
        unsigned char ch = (unsigned char)text[index];

        if (isspace(ch) != 0) {
            if (have_token) {
                pending_space = true;
            }
            ++index;
            continue;
        }

        if (pending_space) {
            if (!minipp_string_append_char(item, ' ')) {
                return false;
            }
            pending_space = false;
        }

        if (text[index] == '"' || text[index] == '\'') {
            char quote = text[index];

            if (!minipp_string_append_char(item, text[index])) {
                return false;
            }
            ++index;
            while (index < size) {
                char value = text[index];
                if (!minipp_string_append_char(item, value)) {
                    return false;
                }
                ++index;
                if (value == '\\' && index < size) {
                    if (!minipp_string_append_char(item, text[index])) {
                        return false;
                    }
                    ++index;
                    continue;
                }
                if (value == quote) {
                    break;
                }
            }
            have_token = true;
            continue;
        }

        if (!minipp_string_append_char(item, text[index])) {
            return false;
        }
        have_token = true;
        ++index;
    }

    return true;
}

static bool minipp_arg_list_append(MiniPpArgList *list,
                                   const char *text,
                                   size_t size,
                                   size_t source_line) {
    MiniPpString *item;

    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0U ? 4U : list->capacity * 2U;
        MiniPpString *next;

        if (capacity < list->capacity ||
            capacity > SIZE_MAX / sizeof(*next)) {
            return false;
        }
        bool *next_leading;
        size_t *next_source_line;

        next = realloc(list->items, capacity * sizeof(*next));
        if (next == NULL) {
            return false;
        }
        list->items = next;
        next_leading = realloc(list->leading_space,
                               capacity * sizeof(*next_leading));
        if (next_leading == NULL) {
            return false;
        }
        list->leading_space = next_leading;
        next_source_line = realloc(list->source_line,
                                   capacity * sizeof(*next_source_line));
        if (next_source_line == NULL) {
            return false;
        }
        list->source_line = next_source_line;
        list->capacity = capacity;
    }

    item = &list->items[list->count];
    {
        size_t leading = 0U;
        size_t token_line = source_line;
        while (leading < size &&
               isspace((unsigned char)text[leading]) != 0) {
            if (text[leading] == '\n') {
                ++token_line;
            }
            ++leading;
        }
        list->leading_space[list->count] = leading != 0U;
        list->source_line[list->count] = token_line;
    }
    minipp_string_init(item);
    if (!minipp_append_normalized_argument(item, text, size) ||
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
                                         size_t open_line,
                                         size_t expected_count,
                                         MiniPpArgList *args,
                                         size_t *after_index) {
    size_t index = open_index + 1U;
    size_t segment_start = index;
    size_t segment_line = open_line;
    size_t current_line = open_line;
    size_t paren_depth = 1U;

    memset(args, 0, sizeof(*args));

    if (text[index] == ')' && expected_count == 0U) {
        *after_index = index + 1U;
        return true;
    }

    while (text[index] != '\0') {
        if (text[index] == '\n') {
            ++current_line;
        }
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
                                            index - segment_start,
                                            segment_line)) {
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
                                        index - segment_start,
                                        segment_line)) {
                fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                minipp_arg_list_destroy(args);
                return false;
            }
            ++index;
            segment_start = index;
            segment_line = current_line;
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

static const MiniPpMacro *minipp_find_trailing_function_macro(
    const MiniPpState *state,
    const MiniPpString *replacement,
    size_t *token_start);

static bool minipp_expand_text_recursive(MiniPpState *state,
                                         const char *text,
                                         MiniPpString *out,
                                         const char *const *disabled,
                                         size_t disabled_count,
                                         size_t depth,
                                         size_t source_line,
                                         bool preserve_argument_spacing);

static bool minipp_build_logical_args(MiniPpState *state,
                                      const MiniPpMacro *macro,
                                      const MiniPpArgList *raw_args,
                                      MiniPpArgList *logical_args,
                                      bool preserve_argument_spacing) {
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
                                        raw_args->items[index].size,
                                        raw_args->source_line[index])) {
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
                                    raw_args->items[index].size,
                                    raw_args->source_line[index])) {
            fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
            minipp_arg_list_destroy(logical_args);
            return false;
        }
    }

    minipp_string_init(&variadic);
    for (index = fixed_count; index < raw_args->count; ++index) {
        if (index != fixed_count) {
            if (!minipp_string_append_char(&variadic, ',')) {
                goto oom;
            }
            if (preserve_argument_spacing &&
                raw_args->leading_space[index] &&
                !minipp_string_append_char(&variadic, ' ')) {
                goto oom;
            }
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
    if (!minipp_arg_list_append(
            logical_args,
            variadic.data,
            variadic.size,
            fixed_count < raw_args->count
                ? raw_args->source_line[fixed_count]
                : (fixed_count != 0U
                       ? raw_args->source_line[fixed_count - 1U]
                       : 1U))) {
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
        size_t param_index = 0U;
        size_t before_paste = left - 2U;

        while (before_paste != 0U &&
               isspace((unsigned char)
                           macro->replacement[before_paste - 1U]) != 0) {
            --before_paste;
        }
        if (macro->variadic &&
            minipp_macro_param_index(macro,
                                     macro->replacement + start,
                                     end - start,
                                     &param_index) &&
            param_index + 1U == macro->param_count &&
            before_paste != 0U &&
            macro->replacement[before_paste - 1U] == ',') {
            return false;
        }
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

static bool minipp_param_needs_prescan(const MiniPpMacro *macro,
                                      size_t target_param) {
    size_t index = 0U;

    while (macro->replacement[index] != '\0') {
        if (macro->replacement[index] == '"' ||
            macro->replacement[index] == '\'') {
            char quote = macro->replacement[index++];
            while (macro->replacement[index] != '\0') {
                char value = macro->replacement[index++];
                if (value == '\\' && macro->replacement[index] != '\0') {
                    ++index;
                    continue;
                }
                if (value == quote) {
                    break;
                }
            }
            continue;
        }

        if (minipp_is_identifier_start(macro->replacement[index])) {
            size_t start = index;
            size_t length;
            size_t param_index;
            size_t left;

            ++index;
            while (minipp_is_identifier_continue(macro->replacement[index])) {
                ++index;
            }
            length = index - start;
            if (!minipp_macro_param_index(macro,
                                          macro->replacement + start,
                                          length,
                                          &param_index) ||
                param_index != target_param) {
                continue;
            }

            if (minipp_param_is_paste_operand(macro, start, index)) {
                continue;
            }

            left = start;
            while (left != 0U &&
                   (macro->replacement[left - 1U] == ' ' ||
                    macro->replacement[left - 1U] == '\t' ||
                    macro->replacement[left - 1U] == '\v' ||
                    macro->replacement[left - 1U] == '\f')) {
                --left;
            }
            if (left != 0U &&
                macro->replacement[left - 1U] == '#' &&
                !(left >= 2U &&
                  macro->replacement[left - 2U] == '#')) {
                continue;
            }

            return true;
        }

        ++index;
    }

    return false;
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

static bool minipp_arg_ends_pp_number(const MiniPpString *arg) {
    size_t index = 0U;
    bool last_pp_number = false;

    while (index < arg->size) {
        unsigned char ch = (unsigned char)arg->data[index];

        if (isspace(ch) != 0) {
            ++index;
            continue;
        }

        if (isdigit(ch) != 0 ||
            (arg->data[index] == '.' &&
             index + 1U < arg->size &&
             isdigit((unsigned char)arg->data[index + 1U]) != 0)) {
            char previous = '\0';

            last_pp_number = true;
            ++index;
            while (index < arg->size) {
                char value = arg->data[index];
                unsigned char uch = (unsigned char)value;

                if (isalnum(uch) != 0 || value == '_' ||
                    value == '.' || value == '\'') {
                    previous = value;
                    ++index;
                    continue;
                }
                if ((value == '+' || value == '-') &&
                    (previous == 'e' || previous == 'E' ||
                     previous == 'p' || previous == 'P')) {
                    previous = value;
                    ++index;
                    continue;
                }
                break;
            }
            continue;
        }

        last_pp_number = false;
        if (minipp_is_identifier_start(arg->data[index])) {
            ++index;
            while (index < arg->size &&
                   minipp_is_identifier_continue(arg->data[index])) {
                ++index;
            }
            continue;
        }

        if (arg->data[index] == '"' || arg->data[index] == '\'') {
            char quote = arg->data[index++];
            while (index < arg->size) {
                char value = arg->data[index++];
                if (value == '\\' && index < arg->size) {
                    ++index;
                    continue;
                }
                if (value == quote) {
                    break;
                }
            }
            continue;
        }

        ++index;
    }

    return last_pp_number;
}

static bool minipp_needs_post_expansion_separator(
    const MiniPpString *expanded,
    char next) {
    if (!minipp_arg_ends_pp_number(expanded)) {
        return false;
    }
    return next == '+' || next == '-' || next == '.';
}

static bool minipp_needs_post_arg_separator(const MiniPpString *arg,
                                            const char *replacement,
                                            size_t next_index) {
    char next = replacement[next_index];

    if (!minipp_arg_ends_pp_number(arg)) {
        return false;
    }
    return next == '+' || next == '-' || next == '.';
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
                size_t cursor = index + 2U;
                size_t length = 0U;
                size_t param_index = 0U;

                while (macro->replacement[cursor] == ' ' ||
                       macro->replacement[cursor] == '\t' ||
                       macro->replacement[cursor] == '\v' ||
                       macro->replacement[cursor] == '\f') {
                    ++cursor;
                }
                if (minipp_is_identifier_start(macro->replacement[cursor])) {
                    length = 1U;
                    while (minipp_is_identifier_continue(
                               macro->replacement[cursor + length])) {
                        ++length;
                    }
                }

                if (length != 0U &&
                    minipp_macro_param_index(macro,
                                             macro->replacement + cursor,
                                             length,
                                             &param_index) &&
                    macro->variadic &&
                    param_index + 1U == macro->param_count) {
                    size_t before_paste = index;
                    while (before_paste != 0U &&
                           isspace((unsigned char)
                                       macro->replacement[before_paste - 1U]) != 0) {
                        --before_paste;
                    }
                    if (before_paste != 0U &&
                        macro->replacement[before_paste - 1U] == ',') {
                        if (raw_args->items[param_index].size == 0U) {
                            while (substituted->size != 0U) {
                                char previous =
                                    substituted->data[substituted->size - 1U];
                                if (previous != ' ' && previous != '\t' &&
                                    previous != '\v' && previous != '\f') {
                                    break;
                                }
                                --substituted->size;
                                substituted->data[substituted->size] = '\0';
                            }
                            if (substituted->size != 0U &&
                                substituted->data[substituted->size - 1U] == ',') {
                                --substituted->size;
                                substituted->data[substituted->size] = '\0';
                            }
                            index = cursor + length;
                            continue;
                        }
                        index = cursor;
                        continue;
                    }
                }

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
                bool paste_operand =
                    minipp_param_is_paste_operand(macro, start, index);
                const MiniPpArgList *source_args =
                    paste_operand ? raw_args : expanded_args;
                const MiniPpString *arg = &source_args->items[param_index];
                if (!minipp_string_append_n(substituted,
                                            arg->data == NULL ? "" : arg->data,
                                            arg->size)) {
                    goto oom;
                }
                if (!paste_operand &&
                    minipp_needs_post_arg_separator(arg,
                                                    macro->replacement,
                                                    index) &&
                    !minipp_string_append_char(substituted, ' ')) {
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

static void minipp_normalize_leading_expansion_space(MiniPpString *text,
                                                            bool keep_one) {
    size_t start = 0U;
    size_t keep;

    while (start < text->size &&
           (text->data[start] == ' ' || text->data[start] == '\t' ||
            text->data[start] == '\v' || text->data[start] == '\f')) {
        ++start;
    }
    if (start == 0U) {
        return;
    }

    keep = keep_one && start < text->size ? 1U : 0U;
    if (start < text->size) {
        memmove(text->data + keep,
                text->data + start,
                text->size - start);
    }
    if (keep != 0U) {
        text->data[0] = ' ';
    }
    text->size = text->size - start + keep;
    if (text->data != NULL) {
        text->data[text->size] = '\0';
    }
}

static bool minipp_output_has_line_indentation(const MiniPpString *out) {
    size_t start = out->size;
    size_t index;

    while (start != 0U && out->data[start - 1U] != '\n') {
        --start;
    }
    if (start == out->size) {
        return false;
    }

    for (index = start; index < out->size; ++index) {
        char value = out->data[index];
        if (value != ' ' && value != '\t' &&
            value != '\v' && value != '\f') {
            return false;
        }
    }
    return true;
}

static bool minipp_expand_function_macro(MiniPpState *state,
                                         const MiniPpMacro *macro,
                                         const char *text,
                                         size_t *index,
                                         MiniPpString *out,
                                         const char *const *disabled,
                                         size_t disabled_count,
                                         size_t depth,
                                         size_t source_line,
                                         bool preserve_argument_spacing,
                                         bool *invoked) {
    size_t cursor = *index;
    size_t open_line = source_line;
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
    while (isspace((unsigned char)text[cursor]) != 0) {
        if (text[cursor] == '\n') {
            ++open_line;
        }
        ++cursor;
    }
    if (text[cursor] != '(') {
        return true;
    }
    *invoked = true;

    if (!minipp_parse_invocation_args(state,
                                      text,
                                      cursor,
                                      open_line,
                                      macro->param_count,
                                      &parsed_args,
                                      index)) {
        return false;
    }
    if (!minipp_build_logical_args(state,
                                   macro,
                                   &parsed_args,
                                   &raw_args,
                                   preserve_argument_spacing)) {
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
        if (minipp_param_needs_prescan(macro, arg_index)) {
            if (!minipp_expand_text_recursive(state,
                                              raw_args.items[arg_index].data,
                                              expanded,
                                              disabled,
                                              disabled_count,
                                              depth + 1U,
                                              raw_args.source_line[arg_index],
                                              true)) {
                minipp_arg_list_destroy(&raw_args);
                expanded_args.count = arg_index + 1U;
                minipp_arg_list_destroy(&expanded_args);
                return false;
            }
            /*
             * Argument prescan is an internal token stream.  GCC's output
             * padding at a top-level macro boundary must not become real
             * leading whitespace when that expansion is substituted as an
             * argument of another macro.
             */
            minipp_normalize_leading_expansion_space(expanded, false);
        }
        if (!minipp_string_append_char(expanded, '\0')) {
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

    {
        MiniPpString replacement;

        minipp_string_init(&replacement);
        ok = minipp_expand_text_recursive(state,
                                          pasted.data,
                                          &replacement,
                                          next_disabled,
                                          next_count,
                                          depth + 1U,
                                          source_line,
                                          false);
        if (ok) {
            minipp_normalize_leading_expansion_space(
                &replacement,
                !minipp_output_has_line_indentation(out));
            size_t tail_start = 0U;
            const MiniPpMacro *tail_macro =
                minipp_find_trailing_function_macro(state,
                                                    &replacement,
                                                    &tail_start);
            bool tail_invoked = false;

            if (tail_macro != NULL &&
                !minipp_macro_is_disabled(tail_macro->name,
                                          next_disabled,
                                          next_count)) {
                size_t invocation_index = *index;

                if (!minipp_string_append_n(out,
                                            replacement.data,
                                            tail_start)) {
                    fprintf(state->diagnostics,
                            "minic-cpp: out-of-memory\n");
                    ok = false;
                } else if (!minipp_expand_function_macro(state,
                                                         tail_macro,
                                                         text,
                                                         &invocation_index,
                                                         out,
                                                         next_disabled,
                                                         next_count,
                                                         depth + 1U,
                                                         source_line,
                                                         preserve_argument_spacing,
                                                         &tail_invoked)) {
                    ok = false;
                } else if (tail_invoked) {
                    *index = invocation_index;
                } else if (!minipp_string_append_n(
                               out,
                               replacement.data + tail_start,
                               replacement.size - tail_start)) {
                    fprintf(state->diagnostics,
                            "minic-cpp: out-of-memory\n");
                    ok = false;
                }
            } else if (!minipp_string_append_n(
                           out,
                           replacement.data == NULL ? "" : replacement.data,
                           replacement.size)) {
                fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                ok = false;
            }
        }
        minipp_string_destroy(&replacement);
    }

done:
    free(next_disabled);
    minipp_string_destroy(&pasted);
    minipp_string_destroy(&substituted);
    minipp_arg_list_destroy(&raw_args);
    minipp_arg_list_destroy(&expanded_args);
    return ok;
}

static const MiniPpMacro *minipp_find_trailing_function_macro(
    const MiniPpState *state,
    const MiniPpString *replacement,
    size_t *token_start) {
    size_t end = replacement->size;
    size_t start;

    while (end != 0U &&
           isspace((unsigned char)replacement->data[end - 1U]) != 0) {
        --end;
    }
    if (end == 0U ||
        !minipp_is_identifier_continue(replacement->data[end - 1U])) {
        return NULL;
    }

    start = end - 1U;
    while (start != 0U &&
           minipp_is_identifier_continue(replacement->data[start - 1U])) {
        --start;
    }
    if (!minipp_is_identifier_start(replacement->data[start])) {
        return NULL;
    }

    {
        const MiniPpMacro *macro =
            minipp_find_macro(state, replacement->data + start, end - start);
        if (macro == NULL || !macro->function_like) {
            return NULL;
        }
        *token_start = start;
        return macro;
    }
}

static bool minipp_append_file_builtin(MiniPpState *state,
                                       MiniPpString *out) {
    const char *path = state->current_file == NULL ? "" : state->current_file;
    const unsigned char *cursor = (const unsigned char *)path;

    if (!minipp_string_append_char(out, '"')) {
        return false;
    }
    while (*cursor != '\0') {
        if (*cursor == '\\' || *cursor == '"') {
            if (!minipp_string_append_char(out, '\\')) {
                return false;
            }
        }
        if (!minipp_string_append_char(out, (char)*cursor)) {
            return false;
        }
        ++cursor;
    }
    return minipp_string_append_char(out, '"');
}

static bool minipp_append_line_builtin(MiniPpString *out, size_t line) {
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%zu", line);

    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        return false;
    }
    return minipp_string_append_n(out, buffer, (size_t)length);
}

static bool minipp_append_counter_builtin(MiniPpState *state,
                                          MiniPpString *out) {
    size_t value = state->counter_value++;
    return minipp_append_line_builtin(out, value);
}

static bool minipp_expand_text_recursive(MiniPpState *state,
                                         const char *text,
                                         MiniPpString *out,
                                         const char *const *disabled,
                                         size_t disabled_count,
                                         size_t depth,
                                         size_t source_line,
                                         bool preserve_argument_spacing) {
    size_t index = 0U;
    size_t current_line = source_line;

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
            if (length == 8U &&
                memcmp(text + start, "__FILE__", 8U) == 0) {
                if (!minipp_append_file_builtin(state, out)) {
                    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                    return false;
                }
                continue;
            }
            if (length == 8U &&
                memcmp(text + start, "__LINE__", 8U) == 0) {
                if (!minipp_append_line_builtin(out, current_line)) {
                    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                    return false;
                }
                continue;
            }
            if (length == 11U &&
                memcmp(text + start, "__COUNTER__", 11U) == 0) {
                if (!minipp_append_counter_builtin(state, out)) {
                    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                    return false;
                }
                continue;
            }
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
                                                      current_line,
                                                      preserve_argument_spacing,
                                                      &invoked)) {
                        return false;
                    }
                    if (invoked) {
                        continue;
                    }
                } else {
                    const char **next_disabled;
                    size_t next_count = disabled_count + 1U;
                    MiniPpString replacement;
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

                    minipp_string_init(&replacement);
                    ok = minipp_expand_text_recursive(state,
                                                      macro->replacement,
                                                      &replacement,
                                                      next_disabled,
                                                      next_count,
                                                      depth + 1U,
                                                      current_line,
                                                      false);
                    if (ok) {
                        size_t tail_start = 0U;
                        const MiniPpMacro *tail_macro =
                            minipp_find_trailing_function_macro(state,
                                                                &replacement,
                                                                &tail_start);
                        bool invoked = false;

                        if (tail_macro != NULL &&
                            !minipp_macro_is_disabled(tail_macro->name,
                                                      next_disabled,
                                                      next_count)) {
                            size_t invocation_index = index;

                            if (!minipp_string_append_n(out,
                                                        replacement.data,
                                                        tail_start)) {
                                fprintf(state->diagnostics,
                                        "minic-cpp: out-of-memory\n");
                                ok = false;
                            } else if (!minipp_expand_function_macro(
                                           state,
                                           tail_macro,
                                           text,
                                           &invocation_index,
                                           out,
                                           next_disabled,
                                           next_count,
                                           depth + 1U,
                                           current_line,
                                           preserve_argument_spacing,
                                           &invoked)) {
                                ok = false;
                            } else if (invoked) {
                                index = invocation_index;
                            } else if (!minipp_string_append_n(
                                           out,
                                           replacement.data + tail_start,
                                           replacement.size - tail_start)) {
                                fprintf(state->diagnostics,
                                        "minic-cpp: out-of-memory\n");
                                ok = false;
                            }
                        } else {
                            if (!minipp_string_append_n(
                                    out,
                                    replacement.data == NULL ? "" :
                                                               replacement.data,
                                    replacement.size)) {
                                fprintf(state->diagnostics,
                                        "minic-cpp: out-of-memory\n");
                                ok = false;
                            } else if (
                                minipp_needs_post_expansion_separator(
                                    &replacement,
                                    text[index]) &&
                                !minipp_string_append_char(out, ' ')) {
                                fprintf(state->diagnostics,
                                        "minic-cpp: out-of-memory\n");
                                ok = false;
                            }
                        }
                    }

                    minipp_string_destroy(&replacement);
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
        if (text[index] == '\n') {
            ++current_line;
        }
        ++index;
    }

    return true;
}

bool minipp_expand_text(MiniPpState *state,
                        const char *text,
                        MiniPpString *out) {
    state->expansion_incomplete = false;
    return minipp_expand_text_recursive(state,
                                        text,
                                        out,
                                        NULL,
                                        0U,
                                        0U,
                                        state->current_line == 0U ? 1U :
                                                                    state->current_line,
                                        true);
}

bool minipp_strip_comments_line(MiniPpState *state,
                                const char *line,
                                size_t line_size,
                                MiniPpString *out) {
    size_t index = 0U;
    bool leading_only = true;

    while (index < line_size) {
        if (state->in_block_comment) {
            if (index + 1U < line_size &&
                line[index] == '*' &&
                line[index + 1U] == '/') {
                if (leading_only &&
                    (!minipp_string_append_char(out, ' ') ||
                     !minipp_string_append_char(out, ' '))) {
                    return false;
                }
                state->in_block_comment = false;
                index += 2U;
                continue;
            }
            if (line[index] == '\n') {
                if (!minipp_string_append_char(out, '\n')) {
                    return false;
                }
                ++index;
                continue;
            }
            if (leading_only &&
                !minipp_string_append_char(out, ' ')) {
                return false;
            }
            ++index;
            continue;
        }

        if (line[index] == '"' || line[index] == '\'') {
            char quote = line[index];
            leading_only = false;
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
            if (leading_only) {
                if (!minipp_string_append_char(out, ' ') ||
                    !minipp_string_append_char(out, ' ')) {
                    return false;
                }
            } else if (!minipp_string_append_char(out, ' ')) {
                return false;
            }
            state->in_block_comment = true;
            index += 2U;
            continue;
        }

        if (!minipp_string_append_char(out, line[index])) {
            return false;
        }
        if (line[index] != ' ' && line[index] != '\t' &&
            line[index] != '\v' && line[index] != '\f' &&
            line[index] != '\r' && line[index] != '\n') {
            leading_only = false;
        }
        ++index;
    }

    return true;
}
