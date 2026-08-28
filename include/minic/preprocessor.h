#ifndef MINIC_PREPROCESSOR_H
#define MINIC_PREPROCESSOR_H

#include <stdbool.h>
#include <stddef.h>

typedef enum MinicPpTokenKind {
    MINIC_PP_TOKEN_IDENTIFIER = 0,
    MINIC_PP_TOKEN_NUMBER,
    MINIC_PP_TOKEN_STRING,
    MINIC_PP_TOKEN_CHARACTER,
    MINIC_PP_TOKEN_PUNCTUATOR,
    MINIC_PP_TOKEN_NEWLINE,
    MINIC_PP_TOKEN_END
} MinicPpTokenKind;

typedef struct MinicPpToken {
    MinicPpTokenKind kind;
    size_t offset;
    size_t length;
    size_t line;
    size_t column;
} MinicPpToken;

typedef struct MinicPpFileBuffer {
    char *path;
    unsigned char *bytes;
    size_t length;
} MinicPpFileBuffer;

typedef struct MinicPpIncludeSearch {
    const char *const *directories;
    size_t directory_count;
} MinicPpIncludeSearch;

const char *minic_pp_token_kind_name(MinicPpTokenKind kind);

int minic_pp_file_buffer_load(const char *path, MinicPpFileBuffer *out_buffer);
void minic_pp_file_buffer_destroy(MinicPpFileBuffer *buffer);

/*
 * Resolve an include using C preprocessing search order.
 *
 * Quoted includes search the directory containing current_file first, then the
 * configured include directories. Angle includes search only the configured
 * include directories. Existing paths are canonicalized with realpath().
 *
 * On success, *out_path owns a heap allocation which must be released with
 * minic_pp_path_destroy().
 */
int minic_pp_resolve_include(const char *header_name,
                             bool is_angle,
                             const char *current_file,
                             const MinicPpIncludeSearch *search,
                             char **out_path);
void minic_pp_path_destroy(char *path);

#endif
