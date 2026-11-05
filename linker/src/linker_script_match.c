#include "linker_script.h"

#include <string.h>

static bool wildcard_match_span(const char *pattern, size_t length,
                                const char *text) {
    while (length != 0U) {
        if (*pattern == '*') {
            ++pattern;
            --length;
            if (length == 0U) {
                return true;
            }
            while (*text != '\0') {
                if (wildcard_match_span(pattern, length, text)) {
                    return true;
                }
                ++text;
            }
            return wildcard_match_span(pattern, length, text);
        }
        if (*text == '\0' || *pattern != *text) {
            return false;
        }
        ++pattern;
        --length;
        ++text;
    }
    return *text == '\0';
}

static bool wildcard_match(const char *pattern, const char *text) {
    return wildcard_match_span(pattern, strlen(pattern), text);
}

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');

    return slash == NULL ? path : slash + 1;
}

bool minild_script_pattern_matches(const MiniLdScriptPattern *pattern,
                                   const char *section_name) {
    return minild_script_pattern_matches_input(pattern, section_name,
                                               NULL, NULL, NULL);
}

bool minild_script_pattern_matches_input(const MiniLdScriptPattern *pattern,
                                         const char *section_name,
                                         const char *input_file,
                                         const char *archive_path,
                                         const char *archive_member_name) {
    if (pattern == NULL || pattern->text == NULL ||
        section_name == NULL) {
        return false;
    }
    if (pattern->file_pattern != NULL) {
        const char *archive_separator = strchr(pattern->file_pattern, ':');
        if (input_file == NULL) {
            return false;
        }
        if (archive_separator != NULL) {
            /* GNU archive:file selectors must match both independent source
             * identities. Never fabricate a filename by concatenating them;
             * ordinary obj.o selectors remain valid for archive members. */
            const size_t archive_length =
                (size_t)(archive_separator - pattern->file_pattern);
            const char *member_pattern = archive_separator + 1U;

            if (archive_path == NULL || archive_member_name == NULL ||
                (!wildcard_match_span(pattern->file_pattern, archive_length,
                                      archive_path) &&
                 !wildcard_match_span(pattern->file_pattern, archive_length,
                                      path_basename(archive_path))) ||
                (!wildcard_match(member_pattern, archive_member_name) &&
                 !wildcard_match(member_pattern,
                                 path_basename(archive_member_name)))) {
                return false;
            }
        } else if (!wildcard_match(pattern->file_pattern, input_file) &&
                   !wildcard_match(pattern->file_pattern,
                                   path_basename(input_file)) &&
                   (archive_member_name == NULL ||
                    (!wildcard_match(pattern->file_pattern,
                                     archive_member_name) &&
                     !wildcard_match(pattern->file_pattern,
                                     path_basename(archive_member_name))))) {
            return false;
        }
    }
    return pattern->common
               ? strcmp(section_name, "COMMON") == 0
               : wildcard_match(pattern->text, section_name);
}
