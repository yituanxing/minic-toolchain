#include "minic/compiler.h"
#include "target/target_info.h"
#include "target/riscv/isa_resolve.h" /* shared 32/64-bit target ISA policy */
#include "target/target_catalog.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void minic_print_usage(FILE *stream, const char *program) {
    const MinicTargetCatalogEntry *targets;
    size_t count, i;
    targets = minic_target_catalog_all(&count);
    (void)fprintf(stream, "usage: %s -S <preprocessed-input.i> [--target=", program);
    for (i = 0U; i < count; ++i)
        (void)fprintf(stream, "%s%s", i == 0U ? "" : "|", targets[i].name);
    (void)fprintf(stream, "] [-march=rv32i|rv64i|rv64im|rv64imafd] [-mabi=ilp32|lp64|lp64d] [-ffunction-sections|-fno-function-sections] [-fPIE|-fPIC|-fno-pie] [-fsigned-char|-funsigned-char] [-fshort-wchar|-fno-short-wchar] -o <output.s>\n");
}

int main(int argc, char **argv) {
    const char *input_path;
    const char *output_path;
    MinicDiagnostic diagnostic;
    int index;
    int result;
    bool function_sections = false;
    bool position_independent = false;
    /* GCC's -fshort-wchar changes the type of L"..." and wchar_t ABI.
     * Keep the standard per-target default unless explicitly overridden. */
    bool short_wchar = false;
    bool wide_char_override = false;
    bool plain_char_override = false;
    MinicIntegerSign plain_char_sign = MINIC_INTEGER_SIGN_NONE;
    MinicTargetInvocation invocation;
    MinicTargetInfo selected_target;
    const char *target_name = "riscv64-linux";
    const MinicTargetInfo *target_info;
    bool target_given = false;
    const char *march = NULL;
    const char *mabi = NULL;

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
        } else if (strcmp(argv[index], "--target") == 0) {
            if (target_given || index + 1 >= argc) {
                minic_print_usage(stderr, argv[0]);
                return 2;
            }
            target_name = argv[++index];
            target_given = true;
        } else if (strncmp(argv[index], "--target=", 9U) == 0) {
            if (target_given) {
                minic_print_usage(stderr, argv[0]);
                return 2;
            }
            target_name = argv[index] + 9U;
            target_given = true;
        } else if (strncmp(argv[index], "-march=", 7U) == 0) {
            if (march != NULL) { (void)fprintf(stderr, "%s: duplicate -march\n", argv[0]); return 2; }
            march = argv[index] + 7U;
        } else if (strcmp(argv[index], "-march") == 0) {
            if (march != NULL || index + 1 >= argc) return 2;
            march = argv[++index];
        } else if (strncmp(argv[index], "-mabi=", 6U) == 0) {
            if (mabi != NULL) { (void)fprintf(stderr, "%s: duplicate -mabi\n", argv[0]); return 2; }
            mabi = argv[index] + 6U;
        } else if (strcmp(argv[index], "-mabi") == 0) {
            if (mabi != NULL || index + 1 >= argc) return 2;
            mabi = argv[++index];
        } else if (strcmp(argv[index], "-fsigned-char") == 0) {
            plain_char_override = true;
            plain_char_sign = MINIC_INTEGER_SIGN_SIGNED;
        } else if (strcmp(argv[index], "-funsigned-char") == 0 ||
                   strcmp(argv[index], "-fno-signed-char") == 0) {
            plain_char_override = true;
            plain_char_sign = MINIC_INTEGER_SIGN_UNSIGNED;
        } else if (strcmp(argv[index], "-fno-unsigned-char") == 0) {
            plain_char_override = true;
            plain_char_sign = MINIC_INTEGER_SIGN_SIGNED;
        } else if (strcmp(argv[index], "-fshort-wchar") == 0) {
            short_wchar = true;
            wide_char_override = true;
        } else if (strcmp(argv[index], "-fno-short-wchar") == 0) {
            short_wchar = false;
            wide_char_override = true;
        } else if (strcmp(argv[index], "-ffunction-sections") == 0) {
            function_sections = true;
        } else if (strcmp(argv[index], "-fno-function-sections") == 0) {
            function_sections = false;
        } else if (strcmp(argv[index], "-fPIE") == 0 ||
                   strcmp(argv[index], "-fpie") == 0 ||
                   strcmp(argv[index], "-fPIC") == 0 ||
                   strcmp(argv[index], "-fpic") == 0) {
            position_independent = true;
        } else if (strcmp(argv[index], "-fno-pie") == 0 ||
                   strcmp(argv[index], "-fno-PIE") == 0 ||
                   strcmp(argv[index], "-fno-pic") == 0 ||
                   strcmp(argv[index], "-fno-PIC") == 0) {
            position_independent = false;
        } else if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
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

    target_info = minic_target_info_from_name(target_name);
    if (target_info == NULL) {
        (void)fprintf(stderr, "%s: unsupported target: %s\n", argv[0], target_name);
        return 2;
    }
    if (position_independent && !target_info->pic_lowering_supported) {
        (void)fprintf(stderr, "%s: PIC lowering is not yet qualified for this target\n", argv[0]);
        return 2;
    }
    /* Both C language ABI and machine code generation settings are
     * invocation-scoped. The canonical TargetInfo remains read-only. */
    if (!minic_target_invocation_begin(&invocation, target_info) ||
        (plain_char_override &&
         !minic_target_invocation_plain_char(&invocation, plain_char_sign)) ||
        (wide_char_override &&
         !minic_target_invocation_short_wchar(&invocation, short_wchar))) {
        (void)fprintf(stderr, "%s: invalid language ABI options\n", argv[0]);
        return 2;
    }
    selected_target = invocation.target;
    selected_target.position_independent = position_independent;
    if (march != NULL || mabi != NULL) {
        char reason[240];
        if (selected_target.architecture != MINIC_TARGET_ARCH_RISCV64 &&
            selected_target.architecture != MINIC_TARGET_ARCH_RISCV32) {
            (void)fprintf(stderr, "%s: -march/-mabi currently require RISC-V\n", argv[0]);
            return 2;
        }
        if (!minic_riscv_isa_resolve(march, mabi, &selected_target.riscv_isa,
                                     reason, sizeof(reason))) {
            (void)fprintf(stderr, "%s: invalid RISC-V configuration: %s\n", argv[0], reason);
            return 2;
        }
    }
    result = minic_compile_preprocessed_file_for_target(
        input_path, output_path, &selected_target, function_sections, &diagnostic);
    if (result != 0) {
        (void)fprintf(stderr,
                      "%s:%zu:%zu: error: %s\n",
                      diagnostic.path != NULL ? diagnostic.path : input_path,
                      diagnostic.line,
                      diagnostic.column,
                      diagnostic.message[0] != '\0' ? diagnostic.message : "compilation failed");
    }

    return result;
}
