#include "riscv_linux_predefines.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Three pinned ABI baselines; Linux C option deltas are computed below. */
#include "riscv_linux_profiles.inc"

static const char *const rv64_c_common[] = {
    "-std=gnu11",
    "-fshort-wchar",
    "-funsigned-char",
    "-fno-PIE",
    "-mabi=lp64",
    "-march=rv64imac_zicsr_zifencei_zihintpause",
    "-mcmodel=medany"
};
static const char *const rv64_asm_common[] = {
    "-fno-PIE",
    "-mabi=lp64",
    "-march=rv64imafdcv_zicsr_zifencei_zihintpause"
};
static const char *const rv32_asm_suffix[] = {
    "-march=rv32g", "-mabi=ilp32"
};

static bool equals_prefix(const char *const *values,
                          size_t value_count,
                          char *const *flags,
                          size_t flag_count) {
    size_t index;
    if (flag_count < value_count) {
        return false;
    }
    for (index = 0U; index < value_count; ++index) {
        if (strcmp(values[index], flags[index]) != 0) {
            return false;
        }
    }
    return true;
}

static bool output_literal(FILE *output, const char *data) {
    return fputs(data, output) >= 0;
}

static bool write_target_profile(FILE *output,
                                 const char *language,
                                 char *const *flags,
                                 size_t count,
                                 const char **profile_name) {
    size_t index;
    if (strcmp(language, "assembler-with-cpp") == 0) {
        if (!equals_prefix(rv64_asm_common, 3U, flags, count)) {
            return false;
        }
        if (count == 3U) {
            *profile_name = "rv64-lp64-assembler-ABI";
            return output_literal(output, minipp_rv64_asm_base);
        }
        if (count == 5U &&
            strcmp(flags[3], rv32_asm_suffix[0]) == 0 &&
            strcmp(flags[4], rv32_asm_suffix[1]) == 0) {
            *profile_name = "rv32-ilp32-assembler-ABI";
            return output_literal(output, minipp_rv32_asm_base);
        }
        return false;
    }
    if (strcmp(language, "c") == 0) {
        bool optimize = false;
        bool no_builtin_wcslen = false;
        const char *pic = NULL;
        if (!equals_prefix(rv64_c_common, 7U, flags, count)) {
            return false;
        }
        for (index = 7U; index < count; ++index) {
            const char *flag = flags[index];
            if (strcmp(flag, "-O2") == 0) {
                if (optimize) return false;
                optimize = true;
            } else if (strcmp(flag, "-fno-builtin-wcslen") == 0) {
                if (no_builtin_wcslen) return false;
                no_builtin_wcslen = true;
            } else if (strcmp(flag, "-fPIC") == 0 ||
                       strcmp(flag, "-fpic") == 0 ||
                       strcmp(flag, "-fpie") == 0) {
                if (pic != NULL) return false;
                pic = flag;
            } else if (strcmp(flag, "-mcmodel=medany") != 0) {
                return false;
            }
        }
        *profile_name = "rv64-lp64-c-common+explicit-flag-rules";
        if (!output_literal(output, minipp_rv64_c_base)) {
            return false;
        }
        if (optimize &&
            !output_literal(output, "#undef __NO_INLINE__\n"
                                     "#define __OPTIMIZE__ 1\n"
                                     "#define _FORTIFY_SOURCE 3\n")) {
            return false;
        }
        if (pic != NULL) {
            if (strcmp(pic, "-fPIC") == 0 &&
                !output_literal(output, "#define __PIC__ 2\n"
                                         "#define __pic__ 2\n")) return false;
            if (strcmp(pic, "-fpic") == 0 &&
                !output_literal(output, "#define __PIC__ 1\n"
                                         "#define __pic__ 1\n")) return false;
            if (strcmp(pic, "-fpie") == 0 &&
                !output_literal(output, "#define __PIC__ 1\n"
                                         "#define __pic__ 1\n"
                                         "#define __PIE__ 1\n"
                                         "#define __pie__ 1\n")) return false;
        }
        return true;
    }
    return false;
}

int minipp_target_predefines_cli(int argc, char **argv) {
    const char *language = NULL;
    const char *output_path = NULL;
    const char *profile_name = NULL;
    char *const *flags = NULL;
    size_t flag_count = 0U;
    bool self_check = false;
    FILE *output;
    int index;
    bool ok;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--language") == 0 &&
            index + 1 < argc) {
            language = argv[++index];
        } else if (strcmp(argv[index], "--output") == 0 &&
                   index + 1 < argc) {
            output_path = argv[++index];
        } else if (strcmp(argv[index], "--self-check") == 0) {
            self_check = true;
        } else if (strcmp(argv[index], "--") == 0) {
            flags = argv + index + 1;
            flag_count = (size_t)(argc - index - 1);
            break;
        } else {
            fprintf(stderr, "MINIPP_NATIVE_MACRO_RULES=FAIL invalid-option:%s\n",
                    argv[index]);
            return 2;
        }
    }
    if (self_check && language == NULL && output_path == NULL) {
        /* The full nine-context oracle comparison is the separate
         * permanent tests/preprocessor/run-target-predefines-native.sh. */
        ok = sizeof(minipp_rv64_c_base) == 14668U &&
             sizeof(minipp_rv64_asm_base) == 15433U &&
             sizeof(minipp_rv32_asm_base) == 14751U &&
             strstr(minipp_rv64_c_base, "#define __riscv") != NULL &&
             strstr(minipp_rv32_asm_base, "#define __riscv") != NULL;
        printf("MINIPP_NATIVE_MACRO_INTERNAL=%s baselines=3 gcc_processes=0 python_processes=0\n",
               ok ? "PASS" : "FAIL");
        return ok ? 0 : 2;
    }
    if (language == NULL || output_path == NULL || flags == NULL ||
        output_path[0] == '\0') {
        fprintf(stderr,
                "MINIPP_NATIVE_MACRO_RULES=FAIL require-language-output-and-flags\n");
        return 2;
    }
    /* Reject unsupported flag contexts without producing a partial file. */
    output = tmpfile();
    if (output == NULL) {
        perror("minic-cpp: target-macro-tmpfile");
        return 2;
    }
    ok = write_target_profile(output, language, flags, flag_count,
                              &profile_name) && fflush(output) == 0;
    if (!ok) {
        fprintf(stderr, "MINIPP_NATIVE_MACRO_RULES=FAIL unsupported-target-context-or-io\n");
        fclose(output);
        return 2;
    }
    if (fseek(output, 0L, SEEK_SET) != 0) {
        fclose(output);
        return 2;
    }
    {
        unsigned char buffer[8192];
        size_t n;
        FILE *dest = fopen(output_path, "wb");
        if (dest == NULL) {
            perror("minic-cpp: target-macro-output");
            fclose(output);
            return 2;
        }
        while ((n = fread(buffer, 1U, sizeof(buffer), output)) != 0U) {
            if (fwrite(buffer, 1U, n, dest) != n) {
                ok = false;
                break;
            }
        }
        if (ferror(output)) ok = false;
        if (fclose(dest) != 0) ok = false;
    }
    fclose(output);
    if (!ok) {
        remove(output_path);
        fprintf(stderr, "MINIPP_NATIVE_MACRO_RULES=FAIL output-io\n");
        return 2;
    }
    printf("MINIPP_NATIVE_MACRO_PROFILE=PASS language=%s rules=%s gcc_processes=0 python_processes=0\n",
           language, profile_name);
    return 0;
}
