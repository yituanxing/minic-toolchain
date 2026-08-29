#ifndef MINIPP_INTERNAL_H
#define MINIPP_INTERNAL_H

#include "minipp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define MINIPP_MAX_EXPANSION_DEPTH 64U

typedef struct MiniPpString {
    char *data;
    size_t size;
    size_t capacity;
} MiniPpString;

typedef struct MiniPpMacro {
    char *name;
    char *replacement;
    char **params;
    size_t param_count;
    bool function_like;
    bool variadic;
} MiniPpMacro;

typedef struct MiniPpConditional {
    bool parent_active;
    bool branch_taken;
    bool current_active;
    bool else_seen;
} MiniPpConditional;

typedef struct MiniPpState {
    MiniPpMacro *macros;
    size_t macro_count;
    size_t macro_capacity;
    MiniPpConditional *conditionals;
    size_t conditional_count;
    size_t conditional_capacity;
    bool active;
    bool in_block_comment;
    const char **include_paths;
    size_t include_path_count;
    FILE *diagnostics;
} MiniPpState;

bool minipp_read_file(const char *path, MiniPpString *out, FILE *diagnostics);
bool minipp_write_file(const char *path,
                       const char *data,
                       size_t size,
                       FILE *diagnostics);
bool minipp_splice_backslash_newlines(const MiniPpString *input,
                                      MiniPpString *output);

void minipp_string_init(MiniPpString *string);
void minipp_string_destroy(MiniPpString *string);
bool minipp_string_append_n(MiniPpString *string,
                            const char *data,
                            size_t size);
bool minipp_string_append_char(MiniPpString *string, char value);

bool minipp_expand_text(MiniPpState *state,
                        const char *text,
                        MiniPpString *out);
bool minipp_eval_if_expression(MiniPpState *state,
                               const char *expression,
                               bool *value);
bool minipp_strip_comments_line(MiniPpState *state,
                                const char *line,
                                size_t line_size,
                                MiniPpString *out);

bool minipp_resolve_include(const MiniPpState *state,
                            const char *current_path,
                            const char *name,
                            bool angled,
                            MiniPpString *resolved_path);

#endif
