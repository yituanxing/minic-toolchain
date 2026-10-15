#include "minias.h"

#include <elf.h>
#include <stdio.h>
#include <string.h>

static bool arch_has_standard_extension(const char *arch, char extension) {
    const char *p = arch + 4;

    while (*p != '\0' && *p != '_') {
        if (*p == extension) {
            return true;
        }
        ++p;
    }
    return false;
}

static void usage(FILE *out, const char *argv0) {
    fprintf(out,
            "usage: %s [-march=rv32...|-march=rv64...] "
            "[-mabi=ilp32...|-mabi=lp64...] -o OUTPUT INPUT.s\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    bool elf32 = false;
    uint32_t elf_flags = 0U;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                usage(stderr, argv[0]);
                return 2;
            }
            output = argv[i];
        } else if (strncmp(argv[i], "-march=", 7U) == 0) {
            const char *arch = argv[i] + 7;
            if (strncmp(arch, "rv32", 4U) == 0) {
                elf32 = true;
            } else if (strncmp(arch, "rv64", 4U) == 0) {
                elf32 = false;
            } else {
                fprintf(stderr, "minic-as: unsupported-arch:%s\n", arch);
                return 2;
            }
            if (arch_has_standard_extension(arch, 'c')) {
                elf_flags |= 1U; /* EF_RISCV_RVC */
            } else {
                elf_flags &= ~1U;
            }
        } else if (strncmp(argv[i], "-mabi=", 6U) == 0) {
            const char *abi = argv[i] + 6;
            uint32_t abi_flags = 0U;

            if (strcmp(abi, "ilp32") == 0) {
                elf32 = true;
                abi_flags = EF_RISCV_FLOAT_ABI_SOFT;
            } else if (strcmp(abi, "ilp32f") == 0) {
                elf32 = true;
                abi_flags = EF_RISCV_FLOAT_ABI_SINGLE;
            } else if (strcmp(abi, "ilp32d") == 0) {
                elf32 = true;
                abi_flags = EF_RISCV_FLOAT_ABI_DOUBLE;
            } else if (strcmp(abi, "ilp32e") == 0) {
                elf32 = true;
                abi_flags = EF_RISCV_FLOAT_ABI_SOFT | EF_RISCV_RVE;
            } else if (strcmp(abi, "lp64") == 0) {
                elf32 = false;
                abi_flags = EF_RISCV_FLOAT_ABI_SOFT;
            } else if (strcmp(abi, "lp64f") == 0) {
                elf32 = false;
                abi_flags = EF_RISCV_FLOAT_ABI_SINGLE;
            } else if (strcmp(abi, "lp64d") == 0) {
                elf32 = false;
                abi_flags = EF_RISCV_FLOAT_ABI_DOUBLE;
            } else if (strcmp(abi, "lp64q") == 0) {
                elf32 = false;
                abi_flags = EF_RISCV_FLOAT_ABI_QUAD;
            } else {
                fprintf(stderr, "minic-as: unsupported-abi:%s\n", abi);
                return 2;
            }
            elf_flags &= ~(uint32_t)(EF_RISCV_FLOAT_ABI | EF_RISCV_RVE);
            elf_flags |= abi_flags;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "minic-as: unsupported-option:%s\n", argv[i]);
            return 2;
        } else if (input == NULL) {
            input = argv[i];
        } else {
            fprintf(stderr, "minic-as: multiple-inputs\n");
            return 2;
        }
    }

    if (input == NULL || output == NULL) {
        usage(stderr, argv[0]);
        return 2;
    }
    return minias_assemble_file_target(input,
                                       output,
                                       elf32,
                                       elf_flags,
                                       stderr);
}
