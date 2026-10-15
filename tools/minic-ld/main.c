/* CLI adapter for the focused MiniLD section-GC frontier. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int minild_main_without_gc_v0(int argc, char **argv);
void minild_cli_enable_gc_sections_v0(void);

#define main minild_main_without_gc_v0
#include "main_core.inc"
#undef main

int main(int argc, char **argv) {
    char **filtered;
    int filtered_argc = 0;
    bool gc_sections = false;
    bool relocatable = false;
    bool shared = false;
    bool pie = false;
    int i;
    int result;

    filtered = malloc((size_t)(argc + 1) * sizeof(*filtered));
    if (filtered == NULL) {
        fprintf(stderr, "minic-ld: out-of-memory:argv\n");
        return 2;
    }

    for (i = 0; i < argc; ++i) {
        const char *arg = argv[i];

        if (i != 0 && strcmp(arg, "--gc-sections") == 0) {
            gc_sections = true;
            continue;
        }
        if (i != 0 && (strcmp(arg, "-r") == 0 ||
                       strcmp(arg, "--relocatable") == 0)) {
            relocatable = true;
        } else if (i != 0 && (strcmp(arg, "-shared") == 0 ||
                              strcmp(arg, "--shared") == 0)) {
            shared = true;
        } else if (i != 0 && strcmp(arg, "-pie") == 0) {
            pie = true;
        }
        filtered[filtered_argc++] = argv[i];
    }
    filtered[filtered_argc] = NULL;

    if (gc_sections && (relocatable || shared || pie)) {
        fprintf(stderr,
                "minic-ld: --gc-sections is currently supported only for final static links\n");
        free(filtered);
        return 2;
    }
    if (gc_sections) {
        minild_cli_enable_gc_sections_v0();
    }

    result = minild_main_without_gc_v0(filtered_argc, filtered);
    free(filtered);
    return result;
}
