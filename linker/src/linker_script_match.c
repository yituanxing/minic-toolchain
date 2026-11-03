#include "linker_script.h"

#include <string.h>

static bool wildcard_match(const char *pattern, const char *text) {
    while (*pattern != '\0') {
        if (*pattern == '*') {
            ++pattern;
            if (*pattern == '\0') {
                return true;
            }
            while (*text != '\0') {
                if (wildcard_match(pattern, text)) {
                    return true;
                }
                ++text;
            }
            return wildcard_match(pattern, text);
        }
        if (*text == '\0' || *pattern != *text) {
            return false;
        }
        ++pattern;
        ++text;
    }
    return *text == '\0';
}

bool minild_script_pattern_matches(const MiniLdScriptPattern *pattern,
                                   const char *section_name) {
    return minild_script_pattern_matches_input(pattern, section_name, NULL);
}

bool minild_script_pattern_matches_input(const MiniLdScriptPattern *pattern,
                                         const char *section_name,
                                         const char *input_file) {
    if (pattern == NULL || pattern->text == NULL ||
        section_name == NULL) {
        return false;
    }
    if (pattern->file_pattern != NULL) {
        const char *basename;
        if (input_file == NULL) {
            return false;
        }
        /* An object may have been passed as /some/path/name.o, whereas GNU
         * linker script selectors often refer to just name.o. Accept either
         * the supplied path or the file basename for such selectors. */
        basename = strrchr(input_file, '/');
        basename = basename == NULL ? input_file : basename + 1;
        if (!wildcard_match(pattern->file_pattern, input_file) &&
            !wildcard_match(pattern->file_pattern, basename)) {
            return false;
        }
    }
    return pattern->common
               ? strcmp(section_name, "COMMON") == 0
               : wildcard_match(pattern->text, section_name);
}
