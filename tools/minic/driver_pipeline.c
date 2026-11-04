#define _POSIX_C_SOURCE 200809L

#include "driver_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool minic_driver_plain_char_unsigned(const MinicDriverLanguageOptions *o) {
    return o->plain_char == NULL ||
           strcmp(o->plain_char, "-funsigned-char") == 0 ||
           strcmp(o->plain_char, "-fno-signed-char") == 0;
}


static void append_rv64_linux_musl_predefines(char **arguments,
                                               size_t *count,
                                               bool hosted,
                                               const char *march,
                                               const char *mabi,
                                 const MinicDriverLanguageOptions *language) {
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
    if (minic_driver_plain_char_unsigned(language))
        arguments[(*count)++] = "-D__CHAR_UNSIGNED__=1";
    arguments[(*count)++] = "-D__LP64__=1";
    arguments[(*count)++] = "-D_LP64=1";
    if (mabi != NULL && strcmp(mabi, "lp64") == 0) {
        arguments[(*count)++] = "-D__riscv_float_abi_soft=1";
    } else {
        arguments[(*count)++] = "-D__riscv_float_abi_double=1";
    }
    /* CPP macros describe available hardware, not compiler codegen choices. */
    if (march == NULL ||
        (minic_riscv_profile_find(march) != NULL &&
         (minic_riscv_profile_find(march)->features & MINIC_RISCV_FEATURE_M))) {
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

static int preprocess_c_to_file(const MinicDriverInvocation *invocation,
                                const char *input,
                                const char *output,
                                char *cpp) {
    const char *sysroot = invocation->sysroot;
    bool hosted = invocation->hosted;
    bool no_stdinc = invocation->no_stdinc;
    const char *const *cpp_forward = invocation->cpp_forward;
    size_t cpp_forward_count = invocation->cpp_forward_count;
    const char *march = invocation->march;
    const char *mabi = invocation->mabi;
    const MinicDriverLanguageOptions *language = &invocation->language;
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
    append_rv64_linux_musl_predefines(
        arguments, &count, hosted, march, mabi, language);
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

static int compile_c_to_assembly(const MinicDriverInvocation *invocation,
                                 const char *input,
                                 const char *output,
                                 char *cpp,
                                 char *cc) {
    bool function_sections = invocation->function_sections;
    const char *march = invocation->march;
    const char *mabi = invocation->mabi;
    const MinicDriverLanguageOptions *language = &invocation->language;
    char *temp_i = make_temp_path("/tmp/minic-driver-i-XXXXXX");
    int status = 1;

    if (temp_i == NULL) {
        return 1;
    }
    status = preprocess_c_to_file(invocation, input, temp_i, cpp);
    if (status == 0) {
        char *arguments[16];
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
        if (language->plain_char != NULL)
            arguments[argument_count++] = (char *)language->plain_char;
        if (language->wide_char != NULL)
            arguments[argument_count++] = (char *)language->wide_char;
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

static int compile_c_to_object(const MinicDriverInvocation *invocation,
                               const char *input,
                               const char *output,
                               char *cpp,
                               char *cc,
                               char *as) {
    const char *march = invocation->march;
    const char *mabi = invocation->mabi;
    char *temp_s = make_temp_path("/tmp/minic-driver-s-XXXXXX");
    int status = 1;

    if (temp_s == NULL) {
        return 1;
    }
    status = compile_c_to_assembly(invocation, input, temp_s, cpp, cc);
    if (status == 0) {
        char *arguments[8];
        size_t argument_count = 0U;
        /* User-facing driver uses the same ISA and ABI as minic-cc.
         * The default existing rv64gc/lp64d pipeline is untouched. */
        char isa_option[64], abi_option[32];
        int isa_size = snprintf(isa_option, sizeof(isa_option), "-march=%s",
                                march != NULL ? march : "rv64gc");
        int abi_size = snprintf(abi_option, sizeof(abi_option), "-mabi=%s",
                                mabi != NULL ? mabi : "lp64d");
        /* The profile was already validated by the shared catalog in main.
         * The assembler receives exactly that pair, without an independent
         * ternary map silently rewriting a future accepted ISA. */
        if (isa_size < 0 || (size_t)isa_size >= sizeof(isa_option) ||
            abi_size < 0 || (size_t)abi_size >= sizeof(abi_option))
            return 1;
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

int minic_driver_execute(const char *argv0,
                         const MinicDriverInvocation *invocation) {
    MinicDriverMode mode = invocation->mode;
    const char *const *inputs = invocation->inputs;
    size_t input_count = invocation->input_count;
    const char *output = invocation->output;
    const char *sysroot = invocation->sysroot;
    bool static_link = invocation->static_link;
    bool no_stdlib = invocation->no_stdlib;
    bool no_startfiles = invocation->no_startfiles;
    bool no_defaultlibs = invocation->no_defaultlibs;
    bool function_sections = invocation->function_sections;
    const char *target_name = invocation->target_name;
    const char *march = invocation->march;
    const char *mabi = invocation->mabi;
    const char *runtime_archive = invocation->runtime_archive;
    MinicDriverLanguageOptions language = invocation->language;
    const char *const *ld_forward = invocation->ld_forward;
    size_t ld_forward_count = invocation->ld_forward_count;
    const char *const *machine_codegen_options =
        invocation->machine_codegen_options;
    size_t machine_codegen_option_count =
        invocation->machine_codegen_option_count;
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
    int status = 1;

    (void)memset(temporary_objects, 0, sizeof(temporary_objects));

    cpp = sibling_tool(argv0, "minic-cpp");
    cc = sibling_tool(argv0, "minic-cc");
    as = sibling_tool(argv0, "minic-as");
    ld = sibling_tool(argv0, "minic-ld");
    if (cpp == NULL || cc == NULL || as == NULL || ld == NULL) {
        fprintf(stderr, "minic: out-of-memory:tool-paths\n");
        goto done;
    }

    if (mode == MINIC_DRIVER_ASSEMBLY) {
        char *cc_arguments[38];
        size_t argument_count = 0U;
        size_t machine_index;
        cc_arguments[argument_count++] = cc;
        cc_arguments[argument_count++] = "-S";
        cc_arguments[argument_count++] = (char *)inputs[0];
        cc_arguments[argument_count++] = "-o";
        cc_arguments[argument_count++] = (char *)output;
        cc_arguments[argument_count++] =
            function_sections ? "-ffunction-sections" : "-fno-function-sections";
        cc_arguments[argument_count++] = "--target";
        cc_arguments[argument_count++] = (char *)target_name;
        if (march != NULL) {
            cc_arguments[argument_count++] = "-march";
            cc_arguments[argument_count++] = (char *)march;
        }
        if (mabi != NULL) {
            cc_arguments[argument_count++] = "-mabi";
            cc_arguments[argument_count++] = (char *)mabi;
        }
        if (language.plain_char != NULL)
            cc_arguments[argument_count++] = (char *)language.plain_char;
        if (language.wide_char != NULL)
            cc_arguments[argument_count++] = (char *)language.wide_char;
        if (language.relocation != NULL)
            cc_arguments[argument_count++] = (char *)language.relocation;
        for (machine_index = 0U; machine_index < machine_codegen_option_count;
             ++machine_index)
            cc_arguments[argument_count++] =
                (char *)machine_codegen_options[machine_index];
        cc_arguments[argument_count] = NULL;
        status = run_tool(cc_arguments);
        goto done;
    }

    if (mode == MINIC_DRIVER_COMPILE) {
        status = compile_c_to_object(invocation, inputs[0], output, cpp, cc, as);
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
                status = compile_c_to_object(invocation, inputs[input_index], object_path,
                                             cpp, cc, as);
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
