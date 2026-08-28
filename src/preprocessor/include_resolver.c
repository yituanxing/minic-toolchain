#define _GNU_SOURCE

#include "minic/preprocessor.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int path_is_regular_file(const char *path) {
    struct stat status;

    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static char *join_path(const char *directory, const char *name) {
    size_t directory_length;
    size_t name_length;
    size_t total_length;
    int needs_separator;
    char *result;

    if (directory == NULL || name == NULL) {
        errno = EINVAL;
        return NULL;
    }

    directory_length = strlen(directory);
    name_length = strlen(name);
    needs_separator = directory_length != 0U && directory[directory_length - 1U] != '/';

    if (directory_length > SIZE_MAX - name_length - 2U) {
        errno = EOVERFLOW;
        return NULL;
    }
    total_length = directory_length + name_length + (size_t)needs_separator + 1U;
    result = malloc(total_length);
    if (result == NULL) {
        return NULL;
    }

    (void)memcpy(result, directory, directory_length);
    if (needs_separator != 0) {
        result[directory_length] = '/';
        ++directory_length;
    }
    (void)memcpy(result + directory_length, name, name_length);
    result[directory_length + name_length] = '\0';
    return result;
}

static char *current_directory(const char *current_file) {
    const char *separator;
    size_t length;
    char *directory;

    if (current_file == NULL || current_file[0] == '\0') {
        return NULL;
    }

    separator = strrchr(current_file, '/');
    if (separator == NULL) {
        directory = malloc(2U);
        if (directory != NULL) {
            directory[0] = '.';
            directory[1] = '\0';
        }
        return directory;
    }

    length = (size_t)(separator - current_file);
    if (length == 0U) {
        length = 1U;
    }
    directory = malloc(length + 1U);
    if (directory == NULL) {
        return NULL;
    }
    (void)memcpy(directory, current_file, length);
    directory[length] = '\0';
    return directory;
}

static int resolve_candidate(const char *candidate, char **out_path) {
    char *canonical;

    canonical = realpath(candidate, NULL);
    if (canonical == NULL) {
        return 0;
    }
    if (!path_is_regular_file(canonical)) {
        free(canonical);
        return 0;
    }
    *out_path = canonical;
    return 1;
}

static int resolve_in_directory(const char *directory,
                                const char *header_name,
                                char **out_path) {
    char *candidate;
    int result;

    candidate = join_path(directory, header_name);
    if (candidate == NULL) {
        return -1;
    }
    result = resolve_candidate(candidate, out_path);
    free(candidate);
    return result;
}

int minic_pp_resolve_include(const char *header_name,
                             bool is_angle,
                             const char *current_file,
                             const MinicPpIncludeSearch *search,
                             char **out_path) {
    char *directory;
    size_t index;
    int result;

    if (header_name == NULL || header_name[0] == '\0' || out_path == NULL) {
        errno = EINVAL;
        return -1;
    }
    *out_path = NULL;

    if (header_name[0] == '/') {
        result = resolve_candidate(header_name, out_path);
        if (result == 1) {
            return 0;
        }
        if (result < 0) {
            return -1;
        }
        errno = ENOENT;
        return -1;
    }

    if (!is_angle && current_file != NULL) {
        directory = current_directory(current_file);
        if (directory == NULL) {
            return -1;
        }
        result = resolve_in_directory(directory, header_name, out_path);
        free(directory);
        if (result == 1) {
            return 0;
        }
        if (result < 0) {
            return -1;
        }
    }

    if (search != NULL) {
        if (search->directory_count != 0U && search->directories == NULL) {
            errno = EINVAL;
            return -1;
        }
        for (index = 0U; index < search->directory_count; ++index) {
            if (search->directories[index] == NULL) {
                errno = EINVAL;
                return -1;
            }
            result = resolve_in_directory(search->directories[index],
                                          header_name,
                                          out_path);
            if (result == 1) {
                return 0;
            }
            if (result < 0) {
                return -1;
            }
        }
    }

    errno = ENOENT;
    return -1;
}

void minic_pp_path_destroy(char *path) {
    free(path);
}
