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

/* Standard SHT_RISCV_ATTRIBUTES / Tag_RISCV_arch encoding for
 * explicitly qualified RV64I/LP64. A + vendor-length + "riscv" + Tag_File
 * + file-length + ULEB(Tag_arch=5) + NUL-terminated ISA string.
 * The profile is exact rv64i2p1, and thus never advertises extensions the
 * strict-I encoder refuses. Emit nothing for the historical default. */
static const unsigned char rv64i_lp64_attributes[] = {
    0x41, 0x19, 0x00, 0x00, 0x00,
    'r', 'i', 's', 'c', 'v', 0x00,
    0x01, 0x0f, 0x00, 0x00, 0x00,
    0x05, 'r', 'v', '6', '4', 'i', '2', 'p', '1', 0x00
};
#define MINIAS_SHT_RISCV_ATTRIBUTES UINT32_C(0x70000003)

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
    size_t section_count;
    bool strict_arch_attribute;
    size_t i;
    FILE *file = NULL;
    bool ok = false;

    expected_symbol_count = emitted_symbol_count(as);
    strict_arch_attribute = as->strict_rv64i && elf_class == ELFCLASS64;
    if (strict_arch_attribute &&
        minias_find_section(as, ".riscv.attributes") >= 0) {
        minias_set_error(as, "rv64i-duplicate-arch-attribute-section");
        return false;
    }
    if (as->section_count == SIZE_MAX) {
        minias_set_error(as, "too-many-elf-sections");
        return false;
    }
    section_count = as->section_count + (strict_arch_attribute ? 1U : 0U);
    sections = calloc(section_count == 0U ? 1U : section_count,
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

    if (strict_arch_attribute) {
        MiniElfWriteSection *attr = &sections[as->section_count];
        attr->name = ".riscv.attributes";
        attr->type = MINIAS_SHT_RISCV_ATTRIBUTES;
        attr->flags = 0U;
        attr->alignment = 1U;
        attr->entry_size = 0U;
        attr->data = rv64i_lp64_attributes;
        attr->size = sizeof(rv64i_lp64_attributes);
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
    spec.section_count = section_count;
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
