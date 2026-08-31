#ifndef MINIAR_H
#define MINIAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct MiniArOptions {
    bool thin;
    bool deterministic;
    bool write_index;
    bool preserve_paths;
} MiniArOptions;

int miniar_create_archive(const char *output_path,
                          const char *const *member_paths,
                          size_t member_count,
                          const MiniArOptions *options,
                          FILE *diagnostics);

int miniar_list_archive(const char *archive_path, FILE *out, FILE *diagnostics);

#endif
