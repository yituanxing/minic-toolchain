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
                                               size_t *count,
                                               bool hosted,
                                               const char *march,
                                               const char *mabi) {
    arguments[(*count)++] = "-D__STDC__=1";
    arguments[(*count)++] = "-D__STDC_VERSION__=201112L";
    /*
     * Product-driver GNU surface level.  Keep this deliberately conservative:
     * real-world headers use __GNUC_PREREQ(2,7) to decide whether GNU
     * __attribute__ syntax is available.  MiniC supports that syntax, but does
     * not claim the later GCC builtin/optimization surface.  Advertising 2.7
     * preserves attributes such as packed without opening newer feature paths.
     */
    arguments[(*count)++] = "-D__GNUC__=2";
    arguments[(*count)++] = "-D__GNUC_MINOR__=7";
    arguments[(*count)++] = "-D__GNUC_PATCHLEVEL__=0";
    arguments[(*count)++] =
        hosted ? "-D__STDC_HOSTED__=1" : "-D__STDC_HOSTED__=0";
    arguments[(*count)++] = "-D__linux__=1";
    arguments[(*count)++] = "-D__linux=1";
    arguments[(*count)++] = "-Dlinux=1";
    arguments[(*count)++] = "-D__unix__=1";
    arguments[(*count)++] = "-D__unix=1";
    arguments[(*count)++] = "-D__riscv=1";
    arguments[(*count)++] = "-D__riscv_xlen=64";
    arguments[(*count)++] = "-D__LP64__=1";
    arguments[(*count)++] = "-D_LP64=1";
    if (mabi != NULL && strcmp(mabi, "lp64") == 0) {
        arguments[(*count)++] = "-D__riscv_float_abi_soft=1";
    } else {
        arguments[(*count)++] = "-D__riscv_float_abi_double=1";
    }
    if (march != NULL && strcmp(march, "rv64i") != 0) {
        arguments[(*count)++] = "-D__riscv_mul=1";
        arguments[(*count)++] = "-D__riscv_div=1";
    }
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
            "usage: %s [-S|-c] [--target=riscv64-linux|x86_64-linux] [-march=rv64i|rv64im|rv64imafd] [-mabi=lp64|lp64d] [--rtlib=PATH (link only)] [-ffreestanding|-fhosted] [-ffunction-sections|-fno-function-sections] [-static] "
            "[-nostdlib] [-nostartfiles] [-nodefaultlibs] [-nostdinc] "
            "[--sysroot DIR] [-DNAME[=VALUE]] [-UNAME] [-IDIR] "
            "[-isystem DIR] [-include FILE] [-LDIR] [-lNAME] "
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

static bool minic_keep_failed_temps(void) {
    const char *value = getenv("MINIC_KEEP_FAILED_TEMPS");
    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
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

static int preprocess_c_to_file(const char *input,
                                const char *output,
                                const char *sysroot,
                                bool hosted,
                                bool no_stdinc,
                                const char *const *cpp_forward,
                                size_t cpp_forward_count,
                                char *cpp,
                                const char *march,
                                const char *mabi) {
    char *include_dir = NULL;
    char *arguments[256];
    size_t count = 0U;
    size_t index;
    int status;

    arguments[count++] = cpp;
    arguments[count++] = "-E";
    arguments[count++] = "-P";
    arguments[count++] = "-undef";
    arguments[count++] = "-nostdinc";
    append_rv64_linux_musl_predefines(arguments, &count, hosted, march, mabi);
    /*
     * User include directories must shadow the driver's default sysroot
     * headers, matching GCC's -I-before-system search semantics.  MiniPP
     * currently stores -I and -isystem in one ordered search list, so forward
     * explicit user paths before appending the implicit sysroot path.
     */
    for (index = 0U; index < cpp_forward_count; ++index) {
        arguments[count++] = (char *)cpp_forward[index];
    }
    if (!no_stdinc && sysroot != NULL && sysroot[0] != '\0') {
        include_dir = join_path(sysroot, "include");
        if (include_dir == NULL) {
            fprintf(stderr, "minic: out-of-memory:include-path\n");
            return 1;
        }
        arguments[count++] = "-isystem";
        arguments[count++] = include_dir;
    }
    arguments[count++] = "-o";
    arguments[count++] = (char *)output;
    arguments[count++] = (char *)input;
    arguments[count] = NULL;

    status = run_tool(arguments);
    free(include_dir);
    return status;
}

static int compile_c_to_assembly(const char *input,
                                 const char *output,
                                 const char *sysroot,
                                 bool hosted,
                                 bool no_stdinc,
                                 bool function_sections,
                                 const char *const *cpp_forward,
                                 size_t cpp_forward_count,
                                 char *cpp,
                                 char *cc,
                                 const char *march,
                                 const char *mabi) {
    char *temp_i = make_temp_path("/tmp/minic-driver-i-XXXXXX");
    int status = 1;

    if (temp_i == NULL) {
        return 1;
    }
    status = preprocess_c_to_file(input,
                                  temp_i,
                                  sysroot,
                                  hosted,
                                  no_stdinc,
                                  cpp_forward,
                                  cpp_forward_count,
                                  cpp, march, mabi);
    if (status == 0) {
        char *arguments[12];
        size_t argument_count = 0U;
        arguments[argument_count++] = cc;
        arguments[argument_count++] = "-S";
        arguments[argument_count++] = temp_i;
        arguments[argument_count++] = "-o";
        arguments[argument_count++] = (char *)output;
        arguments[argument_count++] =
            function_sections ? "-ffunction-sections" : "-fno-function-sections";
        if (march != NULL) {
            arguments[argument_count++] = "-march";
            arguments[argument_count++] = (char *)march;
        }
        if (mabi != NULL) {
            arguments[argument_count++] = "-mabi";
            arguments[argument_count++] = (char *)mabi;
        }
        arguments[argument_count] = NULL;
        status = run_tool(arguments);
    }

    if (status != 0 && minic_keep_failed_temps()) {
        (void)fprintf(stderr,
                      "MINIC_FAILED_PREPROCESSED=%s source=%s\n",
                      temp_i,
                      input);
    } else {
        (void)unlink(temp_i);
    }
    free(temp_i);
    return status;
}

static int compile_c_to_object(const char *input,
                               const char *output,
                               const char *sysroot,
                               bool hosted,
                               bool no_stdinc,
                               bool function_sections,
                               const char *const *cpp_forward,
                               size_t cpp_forward_count,
                               char *cpp,
                               char *cc,
                               char *as,
                               const char *march,
                               const char *mabi) {
    char *temp_s = make_temp_path("/tmp/minic-driver-s-XXXXXX");
    int status = 1;

    if (temp_s == NULL) {
        return 1;
    }
    status = compile_c_to_assembly(input,
                                   temp_s,
                                   sysroot,
                                   hosted,
                                   no_stdinc,
                                   function_sections,
                                   cpp_forward,
                                   cpp_forward_count,
                                   cpp,
                                   cc, march, mabi);
    if (status == 0) {
        char *arguments[8];
        size_t argument_count = 0U;
        /* User-facing driver uses the same ISA and ABI as minic-cc.
         * The default existing rv64gc/lp64d pipeline is untouched. */
        char *isa_option = march != NULL ?
            (strcmp(march, "rv64i") == 0 ? "-march=rv64i" :
             strcmp(march, "rv64im") == 0 ? "-march=rv64im" :
                                            "-march=rv64imafd") : "-march=rv64gc";
        char *abi_option = mabi != NULL && strcmp(mabi, "lp64") == 0 ?
            "-mabi=lp64" : "-mabi=lp64d";
        arguments[argument_count++] = as;
        arguments[argument_count++] = isa_option;
        arguments[argument_count++] = abi_option;
        arguments[argument_count++] = "-o";
        arguments[argument_count++] = (char *)output;
        arguments[argument_count++] = temp_s;
        arguments[argument_count] = NULL;
        status = run_tool(arguments);
    }

    if (status != 0 && minic_keep_failed_temps()) {
        (void)fprintf(stderr,
                      "MINIC_FAILED_ASSEMBLY=%s source=%s\n",
                      temp_s,
                      input);
    } else {
        (void)unlink(temp_s);
    }
    free(temp_s);
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
    bool hosted = true;
    bool static_link = false;
    bool no_stdlib = false;
    bool no_startfiles = false;
    bool no_defaultlibs = false;
    bool no_stdinc = false;
    bool function_sections = false;
    const char *target_name = "riscv64-linux";
    bool target_given = false;
    const char *march = NULL;
    const char *mabi = NULL;
    const char *runtime_archive = NULL;
    char *cpp = NULL;
    char *cc = NULL;
    char *as = NULL;
    char *ld = NULL;
    char *lib_dir = NULL;
    char *crt1 = NULL;
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
        } else if (strcmp(argument, "--target") == 0) {
            if (target_given || ++index >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            target_name = argv[index];
            target_given = true;
        } else if (strncmp(argument, "--target=", 9U) == 0) {
            if (target_given) {
                usage(stderr, argv[0]);
                return 2;
            }
            target_name = argument + 9U;
            target_given = true;
        } else if (strncmp(argument, "-march=", 7U) == 0) {
            if (march != NULL || argument[7] == '\0') return 2;
            march = argument + 7U;
        } else if (strncmp(argument, "-mabi=", 6U) == 0) {
            if (mabi != NULL || argument[6] == '\0') return 2;
            mabi = argument + 6U;
        } else if (strncmp(argument, "--rtlib=", 8U) == 0) {
            if (runtime_archive != NULL || argument[8] == '\0') return 2;
            runtime_archive = argument + 8U;
        } else if (strcmp(argument, "-ffreestanding") == 0) {
            hosted = false;
        } else if (strcmp(argument, "-fhosted") == 0) {
            hosted = true;
        } else if (strcmp(argument, "-fno-builtin") == 0) {
            /* MiniC has no implicit builtin substitution yet. */
        } else if (strcmp(argument, "-ffunction-sections") == 0) {
            function_sections = true;
        } else if (strcmp(argument, "-fno-function-sections") == 0) {
            function_sections = false;
        } else if (strcmp(argument, "-static") == 0) {
            static_link = true;
        } else if (strcmp(argument, "-nostdlib") == 0) {
            no_stdlib = true;
        } else if (strcmp(argument, "-nostartfiles") == 0) {
            no_startfiles = true;
        } else if (strcmp(argument, "-nodefaultlibs") == 0) {
            no_defaultlibs = true;
        } else if (strcmp(argument, "-nostdinc") == 0) {
            no_stdinc = true;
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

    /* The staged driver still owns RV64 preprocessing, assembly and linking.
     * Never run its RV64 pipeline for x86-64 while the native x86 backend and
     * target-specific surrounding tools are not yet wired. */
    if (strcmp(target_name, "riscv64-linux") != 0) {
        if (strcmp(target_name, "x86_64-linux") == 0) {
            fprintf(stderr, "minic: x86_64-linux driver pipeline is not implemented; "
                            "minic-cc recognizes the target for frontend/Core validation\n");
        } else {
            fprintf(stderr, "minic: unsupported-target:%s\n", target_name);
        }
        return 2;
    }
    /* Be strict about the whole pipeline: a compiler accepting RV64I but
     * the assembler silently using RV64GC would violate the target contract. */
    if (march != NULL) {
        const char *expected_abi;
        if (strcmp(march, "rv64i") == 0 || strcmp(march, "rv64im") == 0) {
            expected_abi = "lp64";
        } else if (strcmp(march, "rv64imafd") == 0) {
            expected_abi = "lp64d";
        } else {
            fprintf(stderr, "minic: unsupported-riscv-isa:%s\n", march);
            return 2;
        }
        if (mabi != NULL && strcmp(mabi, expected_abi) != 0) {
            fprintf(stderr, "minic: incompatible-riscv-isa-abi:%s/%s\n", march, mabi);
            return 2;
        }
        mabi = expected_abi;
    } else if (mabi != NULL && strcmp(mabi, "lp64d") != 0) {
        fprintf(stderr, "minic: explicit-nondefault-abi-requires--march\n");
        return 2;
    }
    if (runtime_archive != NULL) {
        if (mode != MINIC_DRIVER_LINK || march == NULL ||
            strcmp(march, "rv64i") != 0) {
            fprintf(stderr, "minic: --rtlib-requires-rv64i-link-mode\n");
            return 2;
        }
        if (access(runtime_archive, R_OK) != 0) {
            fprintf(stderr, "minic: --rtlib-unreadable:%s\n", runtime_archive);
            return 2;
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
    if (mode == MINIC_DRIVER_LINK && !no_stdlib &&
        (!no_startfiles || !no_defaultlibs) &&
        (sysroot == NULL || sysroot[0] == '\0')) {
        fprintf(stderr,
                "minic: default-runtime-link-requires---sysroot-or-MINIC_SYSROOT\n");
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
        char *cc_arguments[12];
        size_t argument_count = 0U;
        cc_arguments[argument_count++] = cc;
        cc_arguments[argument_count++] = "-S";
        cc_arguments[argument_count++] = (char *)inputs[0];
        cc_arguments[argument_count++] = "-o";
        cc_arguments[argument_count++] = (char *)output;
        cc_arguments[argument_count++] =
            function_sections ? "-ffunction-sections" : "-fno-function-sections";
        if (march != NULL) {
            cc_arguments[argument_count++] = "-march";
            cc_arguments[argument_count++] = (char *)march;
        }
        if (mabi != NULL) {
            cc_arguments[argument_count++] = "-mabi";
            cc_arguments[argument_count++] = (char *)mabi;
        }
        cc_arguments[argument_count] = NULL;
        status = run_tool(cc_arguments);
        goto done;
    }

    if (mode == MINIC_DRIVER_COMPILE) {
        status = compile_c_to_object(inputs[0],
                                     output,
                                     sysroot,
                                     hosted,
                                     no_stdinc,
                                     function_sections,
                                     cpp_forward,
                                     cpp_forward_count,
                                     cpp,
                                     cc,
                                     as, march, mabi);
        goto done;
    }

    if (sysroot != NULL && sysroot[0] != '\0') {
        lib_dir = join_path(sysroot, "lib");
        if (lib_dir == NULL) {
            fprintf(stderr, "minic: out-of-memory:sysroot-lib-path\n");
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
        if (!no_stdlib && !no_startfiles) {
            crt1 = join_path(lib_dir, static_link ? "crt1.o" : "Scrt1.o");
            crti = join_path(lib_dir, "crti.o");
            crtn = join_path(lib_dir, "crtn.o");
            if (crt1 == NULL || crti == NULL || crtn == NULL) {
                fprintf(stderr, "minic: out-of-memory:startup-paths\n");
                goto done;
            }
        }
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
                                             hosted,
                                             no_stdinc,
                                             function_sections,
                                             cpp_forward,
                                             cpp_forward_count,
                                             cpp,
                                             cc,
                                             as, march, mabi);
                if (status != 0) {
                    fprintf(stderr,
                            "minic: input-failed:%s status=%d\n",
                            inputs[input_index],
                            status);
                    goto done;
                }
                link_inputs[link_input_count++] = object_path;
            } else {
                link_inputs[link_input_count++] = (char *)inputs[input_index];
            }
        }

        arguments[count++] = ld;
        arguments[count++] = "-melf64lriscv";
        if (static_link) {
            arguments[count++] = "-static";
        } else {
            arguments[count++] = "-pie";
        }
        arguments[count++] = "-e";
        arguments[count++] = "_start";
        if (!static_link) {
            arguments[count++] =
                "--dynamic-linker=/lib/ld-musl-riscv64.so.1";
        }
        arguments[count++] = "-o";
        arguments[count++] = (char *)output;
        if (!no_stdlib && !no_startfiles) {
            arguments[count++] = crt1;
            arguments[count++] = crti;
        }
        for (input_index = 0U; input_index < link_input_count; ++input_index) {
            arguments[count++] = link_inputs[input_index];
        }
        if (library_option != NULL) {
            arguments[count++] = library_option;
        }
        /* Static archives follow their referencing objects, so standard
         * linker extraction pulls only needed third-party builtin members. */
        if (runtime_archive != NULL)
            arguments[count++] = (char *)runtime_archive;
        for (option_index = 0U; option_index < ld_forward_count; ++option_index) {
            arguments[count++] = (char *)ld_forward[option_index];
        }
        if (!no_stdlib && !no_defaultlibs) {
            if (static_link) {
                arguments[count++] = "--start-group";
                arguments[count++] = "-lc";
                /* Never silently link a default LP64D multilib runtime
                 * into an explicitly requested RV64I/LP64 binary. */
                if (march == NULL || strcmp(march, "rv64i") != 0)
                    arguments[count++] = "-lgcc";
                arguments[count++] = "--end-group";
            } else {
                arguments[count++] = "-lc";
            }
        }
        if (!no_stdlib && !no_startfiles) {
            arguments[count++] = crtn;
        }
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
    free(crt1);
    free(crti);
    free(crtn);
    free(library_option);
    return status;
}
