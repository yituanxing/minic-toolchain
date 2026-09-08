#include "minipp_internal.h"

#include <stdio.h>
#include <string.h>

static bool minipp_path_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

static bool minipp_join_path(MiniPpString *out,
                             const char *directory,
                             size_t directory_size,
                             const char *name) {
    minipp_string_init(out);
    if (directory_size != 0U) {
        if (!minipp_string_append_n(out, directory, directory_size)) {
            return false;
        }
        if (directory[directory_size - 1U] != '/' &&
            !minipp_string_append_char(out, '/')) {
            return false;
        }
    }
    if (!minipp_string_append_n(out, name, strlen(name)) ||
        !minipp_string_append_char(out, '\0')) {
        minipp_string_destroy(out);
        return false;
    }
    --out->size;
    return true;
}

static bool minipp_try_candidate(MiniPpString *resolved,
                                 const char *directory,
                                 size_t directory_size,
                                 const char *name) {
    MiniPpString candidate;

    if (!minipp_join_path(&candidate, directory, directory_size, name)) {
        return false;
    }
    if (!minipp_path_exists(candidate.data)) {
        minipp_string_destroy(&candidate);
        return false;
    }

    *resolved = candidate;
    return true;
}

bool minipp_resolve_include(const MiniPpState *state,
                            const char *current_path,
                            const char *name,
                            bool angled,
                            MiniPpString *resolved_path) {
    size_t index;

    minipp_string_init(resolved_path);

    if (!angled) {
        const char *slash = strrchr(current_path, '/');
        if (slash != NULL) {
            size_t directory_size = (size_t)(slash - current_path);
            if (minipp_try_candidate(resolved_path,
                                     current_path,
                                     directory_size,
                                     name)) {
                return true;
            }
        } else if (minipp_try_candidate(resolved_path, "", 0U, name)) {
            return true;
        }
    }

    for (index = 0U; index < state->include_path_count; ++index) {
        const char *directory = state->include_paths[index];
        if (minipp_try_candidate(resolved_path,
                                 directory,
                                 strlen(directory),
                                 name)) {
            return true;
        }
    }

    return false;
}

static bool minipp_path_is_under_include_directory(const char *path,
                                                   const char *directory) {
    size_t directory_size;

    if (path == NULL || directory == NULL) {
        return false;
    }
    directory_size = strlen(directory);
    while (directory_size != 0U && directory[directory_size - 1U] == '/') {
        --directory_size;
    }
    if (directory_size == 0U || strncmp(path, directory, directory_size) != 0) {
        return false;
    }
    return path[directory_size] == '/';
}

bool minipp_resolve_include_next(const MiniPpState *state,
                                 const char *current_path,
                                 const char *name,
                                 MiniPpString *resolved_path) {
    size_t index;
    size_t start_index;
    bool found_origin;

    if (state == NULL || current_path == NULL || name == NULL ||
        resolved_path == NULL) {
        return false;
    }
    minipp_string_init(resolved_path);
    start_index = 0U;
    found_origin = false;
    for (index = 0U; index < state->include_path_count; ++index) {
        if (minipp_path_is_under_include_directory(
                current_path, state->include_paths[index])) {
            start_index = index + 1U;
            found_origin = true;
            break;
        }
    }
    if (!found_origin) {
        return false;
    }

    for (index = start_index; index < state->include_path_count; ++index) {
        const char *directory = state->include_paths[index];
        if (minipp_try_candidate(resolved_path,
                                 directory,
                                 strlen(directory),
                                 name)) {
            return true;
        }
    }
    return false;
}
