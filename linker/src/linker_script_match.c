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
    if (pattern == NULL || pattern->text == NULL ||
        section_name == NULL) {
        return false;
    }
    return pattern->common
               ? strcmp(section_name, "COMMON") == 0
               : wildcard_match(pattern->text, section_name);
}
