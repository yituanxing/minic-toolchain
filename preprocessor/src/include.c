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
