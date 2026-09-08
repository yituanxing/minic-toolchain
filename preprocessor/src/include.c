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

static bool minipp_search_path_range(const char *const *paths,
                                     size_t begin,
                                     size_t count,
                                     const char *name,
                                     MiniPpString *resolved_path) {
    size_t index;

    if (paths == NULL && count != 0U) {
        return false;
    }
    for (index = begin; index < count; ++index) {
        const char *directory = paths[index];

        if (directory != NULL &&
            minipp_try_candidate(resolved_path, directory, strlen(directory), name)) {
            return true;
        }
    }
    return false;
}

static bool minipp_path_belongs_to_directory(const char *path, const char *directory) {
    size_t directory_size;

    if (path == NULL || directory == NULL) {
        return false;
    }
    directory_size = strlen(directory);
    if (directory_size == 0U) {
        return strchr(path, '/') == NULL;
    }
    if (strncmp(path, directory, directory_size) != 0) {
        return false;
    }
    if (directory[directory_size - 1U] == '/') {
        return path[directory_size] != '\0';
    }
    return path[directory_size] == '/' && path[directory_size + 1U] != '\0';
}

bool minipp_resolve_include(const MiniPpState *state,
                            const char *current_path,
                            const char *name,
                            bool angled,
                            MiniPpString *resolved_path) {
    minipp_string_init(resolved_path);

    if (state == NULL || current_path == NULL || name == NULL) {
        return false;
    }

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

    /* GCC's search classes are ordered independently of command-line
       interleaving: ordinary -I directories precede -isystem directories. */
    if (minipp_search_path_range(state->include_paths,
                                 0U,
                                 state->include_path_count,
                                 name,
                                 resolved_path)) {
        return true;
    }
    return minipp_search_path_range(state->system_include_paths,
                                    0U,
                                    state->system_include_path_count,
                                    name,
                                    resolved_path);
}

bool minipp_resolve_include_next(const MiniPpState *state,
                                 const char *current_path,
                                 const char *name,
                                 MiniPpString *resolved_path) {
    size_t index;

    minipp_string_init(resolved_path);
    if (state == NULL || current_path == NULL || name == NULL) {
        return false;
    }

    /* include_next resumes after the include root that supplied the current
       header. The effective chain is all -I roots followed by all -isystem
       roots, matching normal lookup above. */
    for (index = 0U; index < state->include_path_count; ++index) {
        if (!minipp_path_belongs_to_directory(current_path, state->include_paths[index])) {
            continue;
        }
        if (minipp_search_path_range(state->include_paths,
                                     index + 1U,
                                     state->include_path_count,
                                     name,
                                     resolved_path)) {
            return true;
        }
        return minipp_search_path_range(state->system_include_paths,
                                        0U,
                                        state->system_include_path_count,
                                        name,
                                        resolved_path);
    }

    for (index = 0U; index < state->system_include_path_count; ++index) {
        if (!minipp_path_belongs_to_directory(
                current_path, state->system_include_paths[index])) {
            continue;
        }
        return minipp_search_path_range(state->system_include_paths,
                                        index + 1U,
                                        state->system_include_path_count,
                                        name,
                                        resolved_path);
    }

    return false;
}
