/*
 * Host-native producer for a RISC-V ELF64 GC input-section ownership probe.
 *
 * Three SHT_PROGBITS sections intentionally have the SAME ".text" name but
 * different alignments.  No STT_SECTION symbols are emitted: a linker must
 * retain section-header identities instead of inferring them from symbols.
 * The unreachable section references an intentionally undefined symbol.
 */
#include "minielf.h"
#include "minielf_elf_abi.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static const unsigned char start_code[] = {
    0xefU, 0x00U, 0x00U, 0x00U, /* jal ra, live (R_RISCV_JAL) */
    0x93U, 0x08U, 0xd0U, 0x05U, /* addi a7,zero,93 */
    0x73U, 0x00U, 0x00U, 0x00U  /* ecall */
};

static const unsigned char live_code[] = {
    0x13U, 0x05U, 0x70U, 0x00U, /* addi a0,zero,7 */
    0x67U, 0x80U, 0x00U, 0x00U  /* ret */
};

static const unsigned char dead_code[] = {
    0xefU, 0x00U, 0x00U, 0x00U, /* jal ra, missing_dead (undefined) */
    0x13U, 0x05U, 0xc0U, 0x02U  /* addi a0,zero,44, unreachable */
};

static const unsigned char weak_code[] = {
    0x13U, 0x05U, 0x60U, 0x01U, /* addi a0,zero,22, must lose */
    0x67U, 0x80U, 0x00U, 0x00U
};

static bool write_fixture(const char *path, bool weak) {
    const MiniElfWriteSection strong_sections[] = {
        {.name = ".text", .type = SHT_PROGBITS,
         .flags = SHF_ALLOC | SHF_EXECINSTR, .alignment = 4U,
         .data = start_code, .size = sizeof(start_code)},
        {.name = ".text", .type = SHT_PROGBITS,
         .flags = SHF_ALLOC | SHF_EXECINSTR, .alignment = 16U,
         .data = live_code, .size = sizeof(live_code)},
        {.name = ".text", .type = SHT_PROGBITS,
         .flags = SHF_ALLOC | SHF_EXECINSTR, .alignment = 64U,
         .data = dead_code, .size = sizeof(dead_code)}
    };
    const MiniElfWriteSection weak_sections[] = {
        {.name = ".text", .type = SHT_PROGBITS,
         .flags = SHF_ALLOC | SHF_EXECINSTR, .alignment = 8U,
         .data = weak_code, .size = sizeof(weak_code)}
    };
    const MiniElfWriteSymbol strong_symbols[] = {
        {.name = "_start", .info = (STB_GLOBAL << 4U) | STT_FUNC,
         .section_index = 1U, .size = sizeof(start_code)},
        {.name = "live", .info = (STB_GLOBAL << 4U) | STT_FUNC,
         .section_index = 2U, .size = sizeof(live_code)},
        {.name = "dead", .info = (STB_GLOBAL << 4U) | STT_FUNC,
         .section_index = 3U, .size = sizeof(dead_code)},
        {.name = "missing_dead", .info = (STB_GLOBAL << 4U) | STT_NOTYPE,
         .section_index = SHN_UNDEF}
    };
    const MiniElfWriteSymbol weak_symbols[] = {
        {.name = "live", .info = (STB_WEAK << 4U) | STT_FUNC,
         .section_index = 1U, .size = sizeof(weak_code)}
    };
    const MiniElfWriteRela strong_relocs[] = {
        {.target_section = 0U, .offset = 0U, .symbol_index = 1U,
         .type = R_RISCV_JAL, .addend = 0},
        {.target_section = 2U, .offset = 0U, .symbol_index = 3U,
         .type = R_RISCV_JAL, .addend = 0}
    };
    MiniElfRelocatableSpec spec = {0};
    MiniElfWriteError error = MINIELF_WRITE_OK;
    unsigned char *image = NULL;
    size_t size = 0U;
    FILE *output;
    bool ok;

    spec.elf_class = ELFCLASS64;
    spec.data_encoding = ELFDATA2LSB;
    spec.machine = EM_RISCV;
    spec.sections = weak ? weak_sections : strong_sections;
    spec.section_count = weak ? 1U : 3U;
    spec.symbols = weak ? weak_symbols : strong_symbols;
    spec.symbol_count = weak ? 1U : 4U;
    spec.relocations = weak ? NULL : strong_relocs;
    spec.relocation_count = weak ? 0U : 2U;
    spec.emit_section_symbols = false;

    if (!minielf_build_relocatable(&spec, &image, &size, &error)) {
        fprintf(stderr, "input-section-gc-fixture: %s\n",
                minielf_write_error_string(error));
        free(image);
        return false;
    }
    output = fopen(path, "wb");
    if (output == NULL) {
        perror(path);
        free(image);
        return false;
    }
    ok = fwrite(image, 1U, size, output) == size;
    if (fclose(output) != 0) {
        ok = false;
    }
    free(image);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fputs("usage: input_section_gc_fixture STRONG.o WEAK.o\n", stderr);
        return 2;
    }
    if (!write_fixture(argv[1], false) || !write_fixture(argv[2], true)) {
        return 1;
    }
    puts("MINILD_GC_INPUT_FIXTURE=PASS same_name_sections=3 section_symbols=0");
    return 0;
}
