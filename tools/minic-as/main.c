#include "minias.h"

/*
 * RISC-V ELF e_flags values are processor-specific ABI constants.  GNU
 * libc's <elf.h> exports them, but musl's generic <elf.h> does not.
 * Keep the native MiniAS CLI portable across both hosted C runtimes.
 * These are the ELF psABI-defined bit values, not compiler-specific knobs.
 */
#ifndef MINIAS_EF_RISCV_FLOAT_ABI
#define MINIAS_EF_RISCV_FLOAT_ABI 0x0006
#endif
#ifndef MINIAS_EF_RISCV_FLOAT_ABI_SOFT
#define MINIAS_EF_RISCV_FLOAT_ABI_SOFT 0x0000
#endif
#ifndef MINIAS_EF_RISCV_FLOAT_ABI_SINGLE
#define MINIAS_EF_RISCV_FLOAT_ABI_SINGLE 0x0002
#endif
#ifndef MINIAS_EF_RISCV_FLOAT_ABI_DOUBLE
#define MINIAS_EF_RISCV_FLOAT_ABI_DOUBLE 0x0004
#endif
#ifndef MINIAS_EF_RISCV_FLOAT_ABI_QUAD
#define MINIAS_EF_RISCV_FLOAT_ABI_QUAD 0x0006
#endif
#ifndef MINIAS_EF_RISCV_RVE
#define MINIAS_EF_RISCV_RVE 0x0008
#endif
#include <stdio.h>
#include <string.h>

/*
 * RISC-V ELF psABI e_flags layout, independent of libc <elf.h>.
 * musl's public elf.h need not export the GNU EF_RISCV_* extensions.
 * Retain the exact GNU/glibc values; this only makes the assembler CLI
 * portable to the matched musl headers used by native self-hosting.
 */
enum {
    MINIAS_EF_RISCV_FLOAT_ABI_SOFT = 0x0000U,
    MINIAS_EF_RISCV_FLOAT_ABI_SINGLE = 0x0002U,
    MINIAS_EF_RISCV_FLOAT_ABI_DOUBLE = 0x0004U,
    MINIAS_EF_RISCV_FLOAT_ABI_QUAD = 0x0006U,
    MINIAS_EF_RISCV_FLOAT_ABI = 0x0006U,
    MINIAS_EF_RISCV_RVE = 0x0008U
};

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
                abi_flags = MINIAS_EF_RISCV_FLOAT_ABI_SOFT;
            } else if (strcmp(abi, "ilp32f") == 0) {
                elf32 = true;
                abi_flags = MINIAS_EF_RISCV_FLOAT_ABI_SINGLE;
            } else if (strcmp(abi, "ilp32d") == 0) {
                elf32 = true;
                abi_flags = MINIAS_EF_RISCV_FLOAT_ABI_DOUBLE;
            } else if (strcmp(abi, "ilp32e") == 0) {
                elf32 = true;
                abi_flags = MINIAS_EF_RISCV_FLOAT_ABI_SOFT | MINIAS_EF_RISCV_RVE;
            } else if (strcmp(abi, "lp64") == 0) {
                elf32 = false;
                abi_flags = MINIAS_EF_RISCV_FLOAT_ABI_SOFT;
            } else if (strcmp(abi, "lp64f") == 0) {
                elf32 = false;
                abi_flags = MINIAS_EF_RISCV_FLOAT_ABI_SINGLE;
            } else if (strcmp(abi, "lp64d") == 0) {
                elf32 = false;
                abi_flags = MINIAS_EF_RISCV_FLOAT_ABI_DOUBLE;
            } else if (strcmp(abi, "lp64q") == 0) {
                elf32 = false;
                abi_flags = MINIAS_EF_RISCV_FLOAT_ABI_QUAD;
            } else {
                fprintf(stderr, "minic-as: unsupported-abi:%s\n", abi);
                return 2;
            }
            elf_flags &= ~(uint32_t)(MINIAS_EF_RISCV_FLOAT_ABI | MINIAS_EF_RISCV_RVE);
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
