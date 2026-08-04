#include "minic/compiler.h"

#include <stdio.h>
#include <string.h>

static void minic_print_usage(FILE *stream, const char *program)
{
    (void)fprintf(
        stream,
        "usage: %s -S <preprocessed-input.i> -o <output.s>\n",
        program);
}

int main(int argc, char **argv)
{
    const char *input_path;
    const char *output_path;
    MinicDiagnostic diagnostic;
    int index;
    int result;

    input_path = NULL;
    output_path = NULL;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "-S") == 0) {
            if (index + 1 >= argc || input_path != NULL) {
                minic_print_usage(stderr, argv[0]);
                return 2;
            }
            input_path = argv[++index];
        } else if (strcmp(argv[index], "-o") == 0) {
            if (index + 1 >= argc || output_path != NULL) {
                minic_print_usage(stderr, argv[0]);
                return 2;
            }
            output_path = argv[++index];
        } else if (strcmp(argv[index], "--help") == 0 ||
                   strcmp(argv[index], "-h") == 0) {
            minic_print_usage(stdout, argv[0]);
            return 0;
        } else {
            (void)fprintf(stderr, "%s: unknown argument: %s\n", argv[0], argv[index]);
            minic_print_usage(stderr, argv[0]);
            return 2;
        }
    }

    if (input_path == NULL || output_path == NULL) {
        minic_print_usage(stderr, argv[0]);
        return 2;
    }

    result = minic_compile_preprocessed_file(
        input_path,
        output_path,
        &diagnostic);
    if (result != 0) {
        (void)fprintf(
            stderr,
            "%s:%zu:%zu: error: %s\n",
            diagnostic.path != NULL ? diagnostic.path : input_path,
            diagnostic.line,
            diagnostic.column,
            diagnostic.message[0] != '\0' ? diagnostic.message : "compilation failed");
    }

    return result;
}
