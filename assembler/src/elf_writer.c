#include "minias_internal.h"
#include "minielf.h"

#include <elf.h>
#include <stdlib.h>
#include <string.h>

static bool symbol_referenced_by_relocation(const MiniAs *as,
                                             size_t symbol_index) {
    size_t i;

    for (i = 0U; i < as->reloc_count; ++i) {
        if (as->relocs[i].symbol_index == symbol_index) {
            return true;
        }
    }
    return false;
}

static bool should_emit_symbol(const MiniAs *as, size_t symbol_index) {
    const MiniAsSymbol *symbol = &as->symbols[symbol_index];

    /*
     * GNU as treats .L-prefixed names as temporary local labels: once their
     * assembly-time role is finished, they are omitted unless a relocation
     * still needs them. Keep undefined locals visible so invalid input is not
     * silently accepted.
     */
    if (symbol->bind == MINIAS_STB_LOCAL &&
        symbol->defined &&
        strncmp(symbol->name, ".L", 2U) == 0 &&
        !symbol_referenced_by_relocation(as, symbol_index)) {
        return false;
    }
    return true;
}

static size_t emitted_symbol_count(const MiniAs *as) {
    size_t i;
    size_t count = 0U;

    for (i = 0U; i < as->symbol_count; ++i) {
        if (should_emit_symbol(as, i)) {
            ++count;
        }
    }
    return count;
}

static bool minias_write_elf(MiniAs *as,
                             const char *path,
                             unsigned char elf_class) {
    MiniElfWriteSection *sections = NULL;
    MiniElfWriteSymbol *symbols = NULL;
    MiniElfWriteRela *relocations = NULL;
    size_t *symbol_map = NULL;
    unsigned char *image = NULL;
    size_t image_size = 0U;
    MiniElfWriteError write_error = MINIELF_WRITE_OK;
    MiniElfRelocatableSpec spec;
    size_t output_symbol_count = 0U;
    size_t expected_symbol_count;
    size_t i;
    FILE *file = NULL;
    bool ok = false;

    expected_symbol_count = emitted_symbol_count(as);
    sections = calloc(as->section_count == 0U ? 1U : as->section_count,
                      sizeof(*sections));
    symbols = calloc(expected_symbol_count == 0U ? 1U : expected_symbol_count,
                     sizeof(*symbols));
    relocations = calloc(as->reloc_count == 0U ? 1U : as->reloc_count,
                         sizeof(*relocations));
    symbol_map = malloc((as->symbol_count == 0U ? 1U : as->symbol_count) *
                        sizeof(*symbol_map));
    if (sections == NULL || symbols == NULL || relocations == NULL ||
        symbol_map == NULL) {
        minias_set_error(as, "out-of-memory:elf");
        goto done;
    }
    for (i = 0U; i < as->symbol_count; ++i) {
        symbol_map[i] = SIZE_MAX;
    }

    for (i = 0U; i < as->section_count; ++i) {
        MiniAsSection *input = &as->sections[i];
        MiniElfWriteSection *output = &sections[i];

        output->name = input->name;
        output->type = input->type;
        output->flags = input->flags;
        output->alignment = input->align == 0U ? 1U : input->align;
        output->entry_size = 0U;
        output->data = input->data;
        output->size = input->size;
    }

    for (i = 0U; i < as->symbol_count; ++i) {
        MiniAsSymbol *input = &as->symbols[i];
        MiniElfWriteSymbol *output;

        if (!should_emit_symbol(as, i)) {
            continue;
        }
        if (input->bind == MINIAS_STB_LOCAL && !input->defined) {
            minias_set_error(as,
                             "undefined-local-symbol:%s",
                             input->name);
            goto done;
        }
        if (output_symbol_count >= expected_symbol_count) {
            minias_set_error(as, "internal:symbol-count");
            goto done;
        }
        symbol_map[i] = output_symbol_count;
        output = &symbols[output_symbol_count++];
        output->name = input->name;
        output->info =
            (unsigned char)((input->bind << 4U) | (input->type & 0x0fU));
        output->other = input->visibility;
        if (!input->defined) {
            output->section_index = SHN_UNDEF;
        } else if (input->section == MINIAS_SECTION_ABS) {
            output->section_index = SHN_ABS;
        } else if (input->section < 0 ||
                   (size_t)input->section >= as->section_count ||
                   (size_t)input->section + 1U > UINT16_MAX) {
            minias_set_error(as,
                             "internal:bad-symbol-section:%s",
                             input->name);
            goto done;
        } else {
            output->section_index =
                (uint16_t)((size_t)input->section + 1U);
        }
        output->value = input->defined ? input->value : 0U;
        output->size = input->size;
    }
    if (output_symbol_count != expected_symbol_count) {
        minias_set_error(as, "internal:symbol-count");
        goto done;
    }

    for (i = 0U; i < as->reloc_count; ++i) {
        MiniAsReloc *input = &as->relocs[i];
        MiniElfWriteRela *output = &relocations[i];

        if (input->section < 0 ||
            (size_t)input->section >= as->section_count ||
            input->symbol_index >= as->symbol_count ||
            symbol_map[input->symbol_index] == SIZE_MAX) {
            minias_set_error(as, "internal:bad-relocation-symbol");
            goto done;
        }
        output->target_section = (size_t)input->section;
        output->offset = input->offset;
        output->symbol_index = symbol_map[input->symbol_index];
        output->type = input->type;
        output->addend = input->addend;
    }

    memset(&spec, 0, sizeof(spec));
    spec.elf_class = elf_class;
    spec.data_encoding = ELFDATA2LSB;
    spec.machine = EM_RISCV;
    spec.flags = as->elf_flags;
    spec.sections = sections;
    spec.section_count = as->section_count;
    spec.symbols = symbols;
    spec.symbol_count = output_symbol_count;
    spec.relocations = relocations;
    spec.relocation_count = as->reloc_count;
    spec.emit_section_symbols = true;

    if (!minielf_build_relocatable(&spec,
                                   &image,
                                   &image_size,
                                   &write_error)) {
        minias_set_error(as,
                         "elf-write:%s",
                         minielf_write_error_string(write_error));
        goto done;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        minias_set_error(as, "output-open:%s", path);
        goto done;
    }
    if (fwrite(image, 1U, image_size, file) != image_size ||
        fclose(file) != 0) {
        file = NULL;
        minias_set_error(as, "output-write:%s", path);
        goto done;
    }
    file = NULL;
    ok = true;

done:
    if (file != NULL) {
        (void)fclose(file);
    }
    free(image);
    free(symbol_map);
    free(relocations);
    free(symbols);
    free(sections);
    return ok;
}

bool minias_write_elf64(MiniAs *as, const char *path) {
    return minias_write_elf(as, path, ELFCLASS64);
}

bool minias_write_elf32(MiniAs *as, const char *path) {
    return minias_write_elf(as, path, ELFCLASS32);
}
