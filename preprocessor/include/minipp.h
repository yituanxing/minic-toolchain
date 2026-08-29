#ifndef MINIPP_H
#define MINIPP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct MiniPpConfig {
    bool suppress_line_markers;
    bool inhibit_predefined_macros;
    bool no_standard_includes;
    const char **defines;
    size_t define_count;
    const char **undefines;
    size_t undefine_count;
    const char **include_paths;
    size_t include_path_count;
} MiniPpConfig;

int minipp_preprocess_file(const char *input_path,
                           const char *output_path,
                           const MiniPpConfig *config,
                           FILE *diagnostics);

#endif
