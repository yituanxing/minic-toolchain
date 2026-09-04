#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MINIC_DRIVER_MAX_INPUTS 128U
#define MINIC_DRIVER_MAX_FORWARD_OPTIONS 128U
#define MINIC_DRIVER_MAX_LINK_ARGUMENTS 512U

typedef enum MinicDriverMode {
    MINIC_DRIVER_LINK,
    MINIC_DRIVER_COMPILE,
    MINIC_DRIVER_ASSEMBLY
} MinicDriverMode;

static void append_rv64_linux_musl_predefines(char **arguments,
                                               size_t *count) {
    arguments[(*count)++] = "-D__STDC__=1";
    arguments[(*count)++] = "-D__STDC_VERSION__=201112L";
    arguments[(*count)++] = "-D__STDC_HOSTED__=1";
    arguments[(*count)++] = "-D__linux__=1";
    arguments[(*count)++] = "-D__linux=1";
    arguments[(*count)++] = "-Dlinux=1";
    arguments[(*count)++] = "-D__unix__=1";
    arguments[(*count)++] = "-D__unix=1";
    arguments[(*count)++] = "-D__riscv=1";
    arguments[(*count)++] = "-D__riscv_xlen=64";
    arguments[(*count)++] = "-D__LP64__=1";
    arguments[(*count)++] = "-D_LP64=1";
    arguments[(*count)++] = "-D__riscv_float_abi_double=1";
    arguments[(*count)++] = "-D__ORDER_LITTLE_ENDIAN__=1234";
    arguments[(*count)++] = "-D__ORDER_BIG_ENDIAN__=4321";
    arguments[(*count)++] =
        "-D__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__";
    arguments[(*count)++] = "-D__CHAR_BIT__=8";
    arguments[(*count)++] = "-D__SIZEOF_SHORT__=2";
    arguments[(*count)++] = "-D__SIZEOF_INT__=4";
    arguments[(*count)++] = "-D__SIZEOF_LONG__=8";
    arguments[(*count)++] = "-D__SIZEOF_LONG_LONG__=8";
    arguments[(*count)++] = "-D__SIZEOF_POINTER__=8";
    arguments[(*count)++] = "-D__SIZEOF_SIZE_T__=8";
}

static void usage(FILE *out, const char *argv0) {
    fprintf(out,
            "usage: %s [-S|-c] [--sysroot DIR] [-DNAME[=VALUE]] [-UNAME] "
            "[-IDIR] [-isystem DIR] [-include FILE] [-LDIR] [-lNAME] "
            "-o OUTPUT INPUT...\n",
            argv0);
}

static char *duplicate_text(const char *text) {
    size_t size = strlen(text) + 1U;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static char *join_path(const char *directory, const char *name) {
    size_t directory_size = strlen(directory);
    size_t name_size = strlen(name);
    bool slash = directory_size != 0U &&
                 directory[directory_size - 1U] != '/';
    size_t total;
    char *path;

    if (directory_size > (size_t)-1 - name_size - 2U) {
        return NULL;
    }
    total = directory_size + (slash ? 1U : 0U) + name_size + 1U;
    path = malloc(total);
    if (path == NULL) {
        return NULL;
    }
    (void)snprintf(path,
                   total,
                   "%s%s%s",
                   directory,
                   slash ? "/" : "",
                   name);
    return path;
}

static char *sibling_tool(const char *argv0, const char *tool) {
    const char *slash = strrchr(argv0, '/');
    size_t directory_size;
    size_t tool_size;
    char *path;

    if (slash == NULL) {
        return duplicate_text(tool);
    }
    directory_size = (size_t)(slash - argv0) + 1U;
    tool_size = strlen(tool);
    if (directory_size > (size_t)-1 - tool_size - 1U) {
        return NULL;
    }
    path = malloc(directory_size + tool_size + 1U);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, argv0, directory_size);
    memcpy(path + directory_size, tool, tool_size + 1U);
    return path;
}

static int run_tool(char *const arguments[]) {
    pid_t child = fork();
    int status;

    if (child < 0) {
        perror("minic: fork");
        return 1;
    }
    if (child == 0) {
        execvp(arguments[0], arguments);
        perror(arguments[0]);
        _exit(127);
    }

    do {
        if (waitpid(child, &status, 0) >= 0) {
            break;
        }
        if (errno != EINTR) {
            perror("minic: waitpid");
            return 1;
        }
    } while (true);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static char *make_temp_path(const char *pattern) {
    char *path = duplicate_text(pattern);
    int descriptor;

    if (path == NULL) {
        fprintf(stderr, "minic: out-of-memory:temp-path\n");
        return NULL;
    }
    descriptor = mkstemp(path);
    if (descriptor < 0) {
        perror("minic: mkstemp");
        free(path);
        return NULL;
    }
    if (close(descriptor) != 0) {
        perror("minic: close");
        (void)unlink(path);
        free(path);
        return NULL;
    }
    return path;
}

static bool has_suffix(const char *path, const char *suffix) {
    size_t path_size;
    size_t suffix_size;

    if (path == NULL || suffix == NULL) {
        return false;
    }
    path_size = strlen(path);
    suffix_size = strlen(suffix);
    return path_size >= suffix_size &&
           memcmp(path + path_size - suffix_size, suffix, suffix_size) == 0;
}

static int compile_c_to_object(const char *input,
                               const char *output,
                               const char *sysroot,
                               const char *const *cpp_forward,
                               size_t cpp_forward_count,
                               char *cpp,
                               char *cc,
                               char *as) {
    char *include_dir = NULL;
    char *temp_i = NULL;
    char *temp_s = NULL;
    int status = 1;

    temp_i = make_temp_path("/tmp/minic-driver-i-XXXXXX");
    temp_s = make_temp_path("/tmp/minic-driver-s-XXXXXX");
    if (temp_i == NULL || temp_s == NULL) {
        goto done;
    }

    {
        char *arguments[256];
        size_t count = 0U;
        size_t index;

        arguments[count++] = cpp;
        arguments[count++] = "-E";
        arguments[count++] = "-P";
        arguments[count++] = "-undef";
        arguments[count++] = "-nostdinc";
        append_rv64_linux_musl_predefines(arguments, &count);
        if (sysroot != NULL && sysroot[0] != '\0') {
            include_dir = join_path(sysroot, "include");
            if (include_dir == NULL) {
                fprintf(stderr, "minic: out-of-memory:include-path\n");
                goto done;
            }
            arguments[count++] = "-isystem";
            arguments[count++] = include_dir;
        }
        for (index = 0U; index < cpp_forward_count; ++index) {
            arguments[count++] = (char *)cpp_forward[index];
        }
        arguments[count++] = "-o";
        arguments[count++] = temp_i;
        arguments[count++] = (char *)input;
        arguments[count] = NULL;

        status = run_tool(arguments);
        if (status != 0) {
            goto done;
        }
    }

    {
        char *arguments[] = {cc, "-S", temp_i, "-o", temp_s, NULL};

        status = run_tool(arguments);
        if (status != 0) {
            goto done;
        }
    }

    {
        char *arguments[] = {
            as,
            "-march=rv64gc",
            "-mabi=lp64d",
            "-o",
            (char *)output,
            temp_s,
            NULL
        };

        status = run_tool(arguments);
    }

done:
    if (temp_i != NULL) {
        (void)unlink(temp_i);
    }
    if (temp_s != NULL) {
        (void)unlink(temp_s);
    }
    free(temp_i);
    free(temp_s);
    free(include_dir);
    return status;
}

static bool append_forward_option(const char **options,
                                  size_t *count,
                                  const char *value) {
    if (*count >= MINIC_DRIVER_MAX_FORWARD_OPTIONS) {
        fprintf(stderr, "minic: too-many-forward-options\n");
        return false;
    }
    options[(*count)++] = value;
    return true;
}

int main(int argc, char **argv) {
    const char *inputs[MINIC_DRIVER_MAX_INPUTS];
    size_t input_count = 0U;
    const char *output = NULL;
    const char *sysroot = NULL;
    const char *cpp_forward[MINIC_DRIVER_MAX_FORWARD_OPTIONS];
    size_t cpp_forward_count = 0U;
    const char *ld_forward[MINIC_DRIVER_MAX_FORWARD_OPTIONS];
    size_t ld_forward_count = 0U;
    MinicDriverMode mode = MINIC_DRIVER_LINK;
    char *cpp = NULL;
    char *cc = NULL;
    char *as = NULL;
    char *ld = NULL;
    char *lib_dir = NULL;
    char *scrt1 = NULL;
    char *crti = NULL;
    char *crtn = NULL;
    char *library_option = NULL;
    char *temporary_objects[MINIC_DRIVER_MAX_INPUTS];
    size_t temporary_object_count = 0U;
    int index;
    int status = 1;

    (void)memset(temporary_objects, 0, sizeof(temporary_objects));

    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if (strcmp(argument, "-S") == 0) {
            mode = MINIC_DRIVER_ASSEMBLY;
        } else if (strcmp(argument, "-c") == 0) {
            mode = MINIC_DRIVER_COMPILE;
        } else if (strcmp(argument, "-o") == 0) {
            if (++index >= argc || output != NULL) {
                usage(stderr, argv[0]);
                return 2;
            }
            output = argv[index];
        } else if (strcmp(argument, "--sysroot") == 0) {
            if (++index >= argc || sysroot != NULL) {
                usage(stderr, argv[0]);
                return 2;
            }
            sysroot = argv[index];
        } else if (strncmp(argument, "--sysroot=", 10U) == 0) {
            if (sysroot != NULL || argument[10] == '\0') {
                usage(stderr, argv[0]);
                return 2;
            }
            sysroot = argument + 10U;
        } else if (strncmp(argument, "-D", 2U) == 0 ||
                   strncmp(argument, "-U", 2U) == 0 ||
                   strncmp(argument, "-I", 2U) == 0) {
            if (argument[2] == '\0') {
                if (index + 1 >= argc ||
                    !append_forward_option(cpp_forward,
                                           &cpp_forward_count,
                                           argument) ||
                    !append_forward_option(cpp_forward,
                                           &cpp_forward_count,
                                           argv[++index])) {
                    usage(stderr, argv[0]);
                    return 2;
                }
            } else if (!append_forward_option(
                           cpp_forward, &cpp_forward_count, argument)) {
                return 2;
            }
        } else if (strcmp(argument, "-isystem") == 0 ||
                   strcmp(argument, "-include") == 0) {
            if (index + 1 >= argc ||
                !append_forward_option(
                    cpp_forward, &cpp_forward_count, argument) ||
                !append_forward_option(
                    cpp_forward, &cpp_forward_count, argv[++index])) {
                usage(stderr, argv[0]);
                return 2;
            }
        } else if (strncmp(argument, "-isystem", 8U) == 0 &&
                   argument[8] != '\0') {
            if (!append_forward_option(
                    cpp_forward, &cpp_forward_count, argument)) {
                return 2;
            }
        } else if (strcmp(argument, "-L") == 0 ||
                   strcmp(argument, "-l") == 0) {
            if (index + 1 >= argc ||
                !append_forward_option(ld_forward,
                                       &ld_forward_count,
                                       argument) ||
                !append_forward_option(ld_forward,
                                       &ld_forward_count,
                                       argv[++index])) {
                usage(stderr, argv[0]);
                return 2;
            }
        } else if ((strncmp(argument, "-L", 2U) == 0 ||
                    strncmp(argument, "-l", 2U) == 0) &&
                   argument[2] != '\0') {
            if (!append_forward_option(ld_forward,
                                       &ld_forward_count,
                                       argument)) {
                return 2;
            }
        } else if (strcmp(argument, "-h") == 0 ||
                   strcmp(argument, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (argument[0] == '-') {
            fprintf(stderr, "minic: unsupported-option:%s\n", argument);
            return 2;
        } else {
            if (input_count >= MINIC_DRIVER_MAX_INPUTS) {
                fprintf(stderr, "minic: too-many-inputs\n");
                return 2;
            }
            inputs[input_count++] = argument;
        }
    }

    if (input_count == 0U || output == NULL) {
        usage(stderr, argv[0]);
        return 2;
    }
    if (mode != MINIC_DRIVER_LINK && input_count != 1U) {
        fprintf(stderr, "minic: -S-and--c-require-one-input\n");
        return 2;
    }
    if (mode != MINIC_DRIVER_LINK && ld_forward_count != 0U) {
        fprintf(stderr, "minic: linker-options-require-link-mode\n");
        return 2;
    }
    if (sysroot == NULL) {
        sysroot = getenv("MINIC_SYSROOT");
    }
    if (mode == MINIC_DRIVER_LINK &&
        (sysroot == NULL || sysroot[0] == '\0')) {
        fprintf(stderr,
                "minic: dynamic-link-requires---sysroot-or-MINIC_SYSROOT\n");
        return 2;
    }

    cpp = sibling_tool(argv[0], "minic-cpp");
    cc = sibling_tool(argv[0], "minic-cc");
    as = sibling_tool(argv[0], "minic-as");
    ld = sibling_tool(argv[0], "minic-ld");
    if (cpp == NULL || cc == NULL || as == NULL || ld == NULL) {
        fprintf(stderr, "minic: out-of-memory:tool-paths\n");
        goto done;
    }

    if (mode == MINIC_DRIVER_ASSEMBLY) {
        char *cc_arguments[] = {
            cc, "-S", (char *)inputs[0], "-o", (char *)output, NULL
        };

        status = run_tool(cc_arguments);
        goto done;
    }

    if (mode == MINIC_DRIVER_COMPILE) {
        status = compile_c_to_object(inputs[0],
                                     output,
                                     sysroot,
                                     cpp_forward,
                                     cpp_forward_count,
                                     cpp,
                                     cc,
                                     as);
        goto done;
    }

    lib_dir = join_path(sysroot, "lib");
    scrt1 = join_path(lib_dir != NULL ? lib_dir : "", "Scrt1.o");
    crti = join_path(lib_dir != NULL ? lib_dir : "", "crti.o");
    crtn = join_path(lib_dir != NULL ? lib_dir : "", "crtn.o");
    if (lib_dir == NULL || scrt1 == NULL || crti == NULL || crtn == NULL) {
        fprintf(stderr, "minic: out-of-memory:sysroot-paths\n");
        goto done;
    }
    {
        size_t lib_size = strlen(lib_dir);

        if (lib_size > (size_t)-1 - 3U) {
            fprintf(stderr, "minic: library-path-too-long\n");
            goto done;
        }
        library_option = malloc(lib_size + 3U);
        if (library_option == NULL) {
            fprintf(stderr, "minic: out-of-memory:library-option\n");
            goto done;
        }
        (void)snprintf(library_option, lib_size + 3U, "-L%s", lib_dir);
    }

    {
        char *link_inputs[MINIC_DRIVER_MAX_INPUTS];
        char *arguments[MINIC_DRIVER_MAX_LINK_ARGUMENTS];
        size_t link_input_count = 0U;
        size_t count = 0U;
        size_t input_index;
        size_t option_index;

        for (input_index = 0U; input_index < input_count; ++input_index) {
            if (has_suffix(inputs[input_index], ".c")) {
                char *object_path =
                    make_temp_path("/tmp/minic-driver-o-XXXXXX");

                if (object_path == NULL) {
                    goto done;
                }
                temporary_objects[temporary_object_count++] = object_path;
                status = compile_c_to_object(inputs[input_index],
                                             object_path,
                                             sysroot,
                                             cpp_forward,
                                             cpp_forward_count,
                                             cpp,
                                             cc,
                                             as);
                if (status != 0) {
                    goto done;
                }
                link_inputs[link_input_count++] = object_path;
            } else {
                link_inputs[link_input_count++] = (char *)inputs[input_index];
            }
        }

        arguments[count++] = ld;
        arguments[count++] = "-melf64lriscv";
        arguments[count++] = "-pie";
        arguments[count++] = "-e";
        arguments[count++] = "_start";
        arguments[count++] =
            "--dynamic-linker=/lib/ld-musl-riscv64.so.1";
        arguments[count++] = "-o";
        arguments[count++] = (char *)output;
        arguments[count++] = scrt1;
        arguments[count++] = crti;
        for (input_index = 0U; input_index < link_input_count; ++input_index) {
            arguments[count++] = link_inputs[input_index];
        }
        arguments[count++] = crtn;
        arguments[count++] = library_option;
        for (option_index = 0U; option_index < ld_forward_count; ++option_index) {
            arguments[count++] = (char *)ld_forward[option_index];
        }
        arguments[count++] = "-lc";
        arguments[count] = NULL;

        status = run_tool(arguments);
        if (status != 0) {
            goto done;
        }
    }

    if (chmod(output, 0755) != 0) {
        perror("minic: chmod");
        status = 1;
        goto done;
    }
    status = 0;

done:
    while (temporary_object_count != 0U) {
        char *path = temporary_objects[--temporary_object_count];

        if (path != NULL) {
            (void)unlink(path);
            free(path);
        }
    }
    free(cpp);
    free(cc);
    free(as);
    free(ld);
    free(lib_dir);
    free(scrt1);
    free(crti);
    free(crtn);
    free(library_option);
    return status;
}
