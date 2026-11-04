#include "driver_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool minic_driver_is_machine_codegen_option(const char *option) {
    return strcmp(option, "-mno-sse") == 0 ||
           strcmp(option, "-msse") == 0 ||
           strncmp(option, "-fcf-protection=", 16U) == 0 ||
           strncmp(option, "-mindirect-branch=", 18U) == 0 ||
           strcmp(option, "-mindirect-branch-register") == 0 ||
           strncmp(option, "-mfunction-return=", 18U) == 0 ||
           strncmp(option, "-fpatchable-function-entry=", 27U) == 0;
}

static void usage(FILE *out, const char *argv0) {
    const MinicTargetCatalogEntry *targets;
    size_t count, i;
    targets = minic_target_catalog_all(&count);
    (void)fprintf(out, "usage: %s [-S|-c] [--target=", argv0);
    for (i = 0U; i < count; ++i)
        (void)fprintf(out, "%s%s", i == 0U ? "" : "|", targets[i].name);
    (void)fprintf(out,
            "] [-march=rv32i|rv64i|rv64im|rv64imafd|rv64ima_zicsr_zifencei|rv64imac_zicsr_zifencei] [-mabi=ilp32|lp64|lp64d] [-fsigned-char|-funsigned-char] [-fshort-wchar|-fno-short-wchar] [-fPIC|-fPIE (only -S)] [-fcf-protection=branch] [-mindirect-branch=thunk-extern|thunk-inline|keep] [-mindirect-branch-register] [-mfunction-return=thunk-extern] [-fpatchable-function-entry=16,16] [--rtlib=PATH (link only)] [-ffreestanding|-fhosted] [-ffunction-sections|-fno-function-sections] [-static] "
            "[-nostdlib] [-nostartfiles] [-nodefaultlibs] [-nostdinc] "
            "[--sysroot DIR] [-DNAME[=VALUE]] [-UNAME] [-IDIR] "
            "[-isystem DIR] [-include FILE] [-LDIR] [-lNAME] "
            "-o OUTPUT INPUT...\n"
            "  -S: all registered MiniC backends (preprocessed C input)\n"
            "  -c/link: RV64 staged CPP/AS/LD pipeline only\n");
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

int minic_driver_cli_parse(int argc, char **argv,
                           MinicDriverInvocation *invocation) {
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
    const MinicTargetCatalogEntry *default_target = minic_target_catalog_default();
    const char *target_name = default_target != NULL ? default_target->name : "";
    const MinicTargetCatalogEntry *target_entry;
    bool target_given = false;
    const char *march = NULL;
    const char *mabi = NULL;
    const char *machine_codegen_options[MINIC_DRIVER_MAX_MACHINE_OPTIONS];
    size_t machine_codegen_option_count = 0U;
    const char *runtime_archive = NULL;
    MinicDriverLanguageOptions language = {0};
    int index;

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
        } else if (strcmp(argument, "-fsigned-char") == 0 ||
                   strcmp(argument, "-funsigned-char") == 0 ||
                   strcmp(argument, "-fno-signed-char") == 0 ||
                   strcmp(argument, "-fno-unsigned-char") == 0) {
            language.plain_char = argument;
        } else if (strcmp(argument, "-fshort-wchar") == 0 ||
                   strcmp(argument, "-fno-short-wchar") == 0) {
            language.wide_char = argument;
        } else if (strcmp(argument, "-fPIC") == 0 ||
                   strcmp(argument, "-fpic") == 0 ||
                   strcmp(argument, "-fPIE") == 0 ||
                   strcmp(argument, "-fpie") == 0 ||
                   strcmp(argument, "-fno-pie") == 0 ||
                   strcmp(argument, "-fno-PIE") == 0 ||
                   strcmp(argument, "-fno-pic") == 0 ||
                   strcmp(argument, "-fno-PIC") == 0) {
            language.relocation = argument;
        } else if (minic_driver_is_machine_codegen_option(argument)) {
            if (machine_codegen_option_count ==
                sizeof(machine_codegen_options) / sizeof(machine_codegen_options[0])) {
                fprintf(stderr, "minic: too many machine codegen options\n");
                return 2;
            }
            machine_codegen_options[machine_codegen_option_count++] = argument;
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
            return 1;
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

    target_entry = minic_target_catalog_find(target_name);
    if (target_entry == NULL) {
        fprintf(stderr, "minic: unsupported-target:%s\n", target_name);
        return 2;
    }
    /* C -> GNU assembly may use all registered backends. The staged PP,
     * assembler and linker remain exclusively RV64; reject unqualified
     * -c/link *before* invoking any RV64 tool for a foreign target. */
    if (mode != MINIC_DRIVER_ASSEMBLY &&
        (target_entry->product_driver_stages &
         MINIC_TARGET_STAGE_STAGED_ASSEMBLER_AND_LINKER) == 0U) {
        fprintf(stderr, "minic: target-requires-minic-cc-or-external-assembler:%s\n",
                target_name);
        return 2;
    }
    if (target_entry->isa_family == MINIC_TARGET_ISA_FAMILY_RISCV &&
        (target_entry->product_driver_stages &
         MINIC_TARGET_STAGE_STAGED_ASSEMBLER_AND_LINKER) != 0U) {
    /* Be strict about the whole pipeline: a compiler accepting RV64I but
     * the assembler silently using RV64GC would violate the target contract. */
    if (march != NULL) {
        const MinicRiscvProfile *profile = minic_riscv_profile_find(march);
        if (profile == NULL || profile->xlen != 64U) {
            fprintf(stderr, "minic: unsupported-riscv-isa:%s\n", march);
            return 2;
        }
        if (mabi == NULL) mabi = profile->mabi;
        if (strcmp(mabi, "lp64") != 0 &&
            !(strcmp(mabi, "lp64d") == 0 &&
              (profile->features & (MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D)) ==
                  (MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D))) {
            fprintf(stderr, "minic: incompatible-riscv-isa-abi:%s/%s\n", march, mabi);
            return 2;
        }
        /* Unqualified built-in MiniAS ISA combinations are -S only. */
        if (mode != MINIC_DRIVER_ASSEMBLY &&
            !profile->staged_assembler_qualified) {
            fprintf(stderr, "minic: extended-riscv-isa-requires--S-external-assembler:%s\n", march);
            return 2;
        }
    } else if (mabi != NULL && strcmp(mabi, "lp64d") != 0) {
        fprintf(stderr, "minic: explicit-nondefault-abi-requires--march\n");
        return 2;
    }
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
    if (language.relocation != NULL && mode != MINIC_DRIVER_ASSEMBLY) {
        fprintf(stderr, "minic: PIC/PIE requires -S (qualified backend-only)\n");
        return 2;
    }
    if (machine_codegen_option_count != 0U && mode != MINIC_DRIVER_ASSEMBLY) {
        fprintf(stderr, "minic: machine-codegen-options-require--S\n");
        return 2;
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

    *invocation = (MinicDriverInvocation){
        .mode = mode,
        .target_entry = target_entry,
        .target_name = target_name,
        .input_count = input_count,
        .output = output,
        .sysroot = sysroot,
        .hosted = hosted,
        .static_link = static_link,
        .no_stdlib = no_stdlib,
        .no_startfiles = no_startfiles,
        .no_defaultlibs = no_defaultlibs,
        .no_stdinc = no_stdinc,
        .function_sections = function_sections,
        .march = march,
        .mabi = mabi,
        .runtime_archive = runtime_archive,
        .language = language,
        .cpp_forward_count = cpp_forward_count,
        .ld_forward_count = ld_forward_count,
        .machine_codegen_option_count = machine_codegen_option_count,
    };
    (void)memcpy(invocation->inputs, inputs, input_count * sizeof(inputs[0]));
    (void)memcpy(invocation->cpp_forward, cpp_forward,
                 cpp_forward_count * sizeof(cpp_forward[0]));
    (void)memcpy(invocation->ld_forward, ld_forward,
                 ld_forward_count * sizeof(ld_forward[0]));
    (void)memcpy(invocation->machine_codegen_options, machine_codegen_options,
                 machine_codegen_option_count * sizeof(machine_codegen_options[0]));
    return 0;
}
