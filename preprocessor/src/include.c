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

static bool minipp_resolve_include_from(const MiniPpState *state,
                                        const char *current_path,
                                        const char *name,
                                        bool search_current_directory,
                                        size_t include_path_begin,
                                        MiniPpString *resolved_path,
                                        size_t *resolved_include_path_index) {
    size_t index;

    minipp_string_init(resolved_path);
    if (resolved_include_path_index != NULL) {
        *resolved_include_path_index = SIZE_MAX;
    }

    if (search_current_directory) {
        const char *slash = strrchr(current_path, '/');
        if (slash != NULL) {
            size_t directory_size = (size_t)(slash - current_path);
            if (minipp_try_candidate(resolved_path,
                                     current_path,
                                     directory_size,
                                     name)) {
                /*
                 * A quoted include found beside a header that itself came from
                 * an include-path directory remains in that same search slot.
                 * Preserve the provenance so a later #include_next starts
                 * after the correct directory instead of restarting at -I[0].
                 */
                if (resolved_include_path_index != NULL) {
                    *resolved_include_path_index =
                        state->current_include_path_index;
                }
                return true;
            }
        } else if (minipp_try_candidate(resolved_path, "", 0U, name)) {
            if (resolved_include_path_index != NULL) {
                *resolved_include_path_index =
                    state->current_include_path_index;
            }
            return true;
        }
    }

    for (index = include_path_begin; index < state->include_path_count; ++index) {
        const char *directory = state->include_paths[index];
        if (minipp_try_candidate(resolved_path,
                                 directory,
                                 strlen(directory),
                                 name)) {
            if (resolved_include_path_index != NULL) {
                *resolved_include_path_index = index;
            }
            return true;
        }
    }

    return false;
}

bool minipp_resolve_include(const MiniPpState *state,
                            const char *current_path,
                            const char *name,
                            bool angled,
                            MiniPpString *resolved_path,
                            size_t *resolved_include_path_index) {
    return minipp_resolve_include_from(state,
                                       current_path,
                                       name,
                                       !angled,
                                       0U,
                                       resolved_path,
                                       resolved_include_path_index);
}

bool minipp_resolve_include_next(const MiniPpState *state,
                                 const char *name,
                                 MiniPpString *resolved_path,
                                 size_t *resolved_include_path_index) {
    size_t begin = 0U;

    if (state->current_include_path_index != SIZE_MAX) {
        if (state->current_include_path_index + 1U < state->include_path_count) {
            begin = state->current_include_path_index + 1U;
        } else {
            minipp_string_init(resolved_path);
            if (resolved_include_path_index != NULL) {
                *resolved_include_path_index = SIZE_MAX;
            }
            return false;
        }
    }

    return minipp_resolve_include_from(state,
                                       "",
                                       name,
                                       false,
                                       begin,
                                       resolved_path,
                                       resolved_include_path_index);
}
