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

typedef enum MinicDriverMode {
    MINIC_DRIVER_LINK,
    MINIC_DRIVER_COMPILE,
    MINIC_DRIVER_ASSEMBLY
} MinicDriverMode;

static void usage(FILE *out, const char *argv0) {
    fprintf(out,
            "usage: %s [-S|-c] [--sysroot DIR] [-DNAME[=VALUE]] [-IDIR] "
            "-o OUTPUT INPUT\n",
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

static bool make_temp(char *path) {
    int descriptor = mkstemp(path);

    if (descriptor < 0) {
        perror("minic: mkstemp");
        return false;
    }
    if (close(descriptor) != 0) {
        perror("minic: close");
        (void)unlink(path);
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    const char *sysroot = NULL;
    const char *cpp_forward[128];
    size_t cpp_forward_count = 0U;
    MinicDriverMode mode = MINIC_DRIVER_LINK;
    char *cpp = NULL;
    char *cc = NULL;
    char *as = NULL;
    char *ld = NULL;
    char *include_dir = NULL;
    char *lib_dir = NULL;
    char *scrt1 = NULL;
    char *crti = NULL;
    char *crtn = NULL;
    char *library_option = NULL;
    char temp_i[] = "/tmp/minic-driver-i-XXXXXX";
    char temp_s[] = "/tmp/minic-driver-s-XXXXXX";
    char temp_o[] = "/tmp/minic-driver-o-XXXXXX";
    bool have_i = false;
    bool have_s = false;
    bool have_o = false;
    int index;
    int status = 1;

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
                   strncmp(argument, "-I", 2U) == 0) {
            if (argument[2] == '\0') {
                if (index + 1 >= argc ||
                    cpp_forward_count + 2U >
                        sizeof(cpp_forward) / sizeof(cpp_forward[0])) {
                    usage(stderr, argv[0]);
                    return 2;
                }
                cpp_forward[cpp_forward_count++] = argument;
                cpp_forward[cpp_forward_count++] = argv[++index];
            } else {
                if (cpp_forward_count ==
                    sizeof(cpp_forward) / sizeof(cpp_forward[0])) {
                    fprintf(stderr, "minic: too-many-cpp-options\n");
                    return 2;
                }
                cpp_forward[cpp_forward_count++] = argument;
            }
        } else if (strcmp(argument, "-h") == 0 ||
                   strcmp(argument, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (argument[0] == '-') {
            fprintf(stderr, "minic: unsupported-option:%s\n", argument);
            return 2;
        } else if (input == NULL) {
            input = argument;
        } else {
            fprintf(stderr, "minic: multiple-inputs\n");
            return 2;
        }
    }

    if (input == NULL || output == NULL) {
        usage(stderr, argv[0]);
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
            cc, "-S", (char *)input, "-o", (char *)output, NULL
        };

        status = run_tool(cc_arguments);
        goto done;
    }

    if (!make_temp(temp_i)) {
        goto done;
    }
    have_i = true;
    if (!make_temp(temp_s)) {
        goto done;
    }
    have_s = true;

    {
        char *arguments[160];
        size_t count = 0U;
        size_t i;

        arguments[count++] = cpp;
        arguments[count++] = "-E";
        arguments[count++] = "-P";
        arguments[count++] = "-undef";
        arguments[count++] = "-nostdinc";
        if (sysroot != NULL && sysroot[0] != '\0') {
            include_dir = join_path(sysroot, "include");
            if (include_dir == NULL) {
                fprintf(stderr, "minic: out-of-memory:include-path\n");
                goto done;
            }
            arguments[count++] = "-isystem";
            arguments[count++] = include_dir;
        }
        for (i = 0U; i < cpp_forward_count; ++i) {
            arguments[count++] = (char *)cpp_forward[i];
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
        char *arguments[] = {
            cc, "-S", temp_i, "-o", temp_s, NULL
        };

        status = run_tool(arguments);
        if (status != 0) {
            goto done;
        }
    }

    if (mode == MINIC_DRIVER_COMPILE) {
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
        goto done;
    }

    if (!make_temp(temp_o)) {
        goto done;
    }
    have_o = true;
    {
        char *arguments[] = {
            as,
            "-march=rv64gc",
            "-mabi=lp64d",
            "-o",
            temp_o,
            temp_s,
            NULL
        };

        status = run_tool(arguments);
        if (status != 0) {
            goto done;
        }
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
        char *arguments[] = {
            ld,
            "-melf64lriscv",
            "-pie",
            "-e",
            "_start",
            "--dynamic-linker=/lib/ld-musl-riscv64.so.1",
            "-o",
            (char *)output,
            scrt1,
            crti,
            temp_o,
            crtn,
            library_option,
            "-lc",
            NULL
        };

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
    if (have_i) {
        (void)unlink(temp_i);
    }
    if (have_s) {
        (void)unlink(temp_s);
    }
    if (have_o) {
        (void)unlink(temp_o);
    }
    free(cpp);
    free(cc);
    free(as);
    free(ld);
    free(include_dir);
    free(lib_dir);
    free(scrt1);
    free(crti);
    free(crtn);
    free(library_option);
    return status;
}
