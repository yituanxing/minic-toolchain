#include "minias_internal.h"

#include <elf.h>
#include <stdlib.h>
#include <string.h>

static uint64_t align_up_u64(uint64_t value, uint64_t align) {
    return align <= 1U ? value : (value + align - 1U) & ~(align - 1U);
}

static bool append_string(char **buffer,
                          size_t *size,
                          size_t *capacity,
                          const char *text,
                          uint32_t *offset) {
    size_t n = strlen(text) + 1U;
    char *next_buffer;

    if (*size + n > *capacity) {
        size_t next = *capacity == 0U ? 64U : *capacity;
        while (next < *size + n) {
            if (next > SIZE_MAX / 2U) {
                return false;
            }
            next *= 2U;
        }
        next_buffer = realloc(*buffer, next);
        if (next_buffer == NULL) {
            return false;
        }
        *buffer = next_buffer;
        *capacity = next;
    }
    if (*size > UINT32_MAX) {
        return false;
    }
    *offset = (uint32_t)*size;
    memcpy(*buffer + *size, text, n);
    *size += n;
    return true;
}

static size_t relocation_count_for_section(const MiniAs *as, size_t section) {
    size_t i;
    size_t count = 0U;

    for (i = 0U; i < as->reloc_count; ++i) {
        if (as->relocs[i].section >= 0 &&
            (size_t)as->relocs[i].section == section) {
            ++count;
        }
    }
    return count;
}

bool minias_write_elf64(MiniAs *as, const char *path) {
    size_t i;
    size_t pass;
    size_t section_count = as->section_count;
    size_t relocation_section_count = 0U;
    size_t symbol_count = 1U + section_count + as->symbol_count;
    size_t output_section_count;
    size_t symtab_index;
    size_t strtab_index;
    size_t shstrtab_index;
    size_t local_count = 0U;
    size_t symbol_index;
    size_t relocation_ordinal = 0U;
    size_t *relocation_counts = NULL;
    size_t *relocation_section_indices = NULL;
    size_t *elf_symbol_indices = NULL;
    uint32_t *symbol_names = NULL;
    uint32_t *section_names = NULL;
    uint32_t *relocation_names = NULL;
    Elf64_Sym *symbols = NULL;
    Elf64_Shdr *section_headers = NULL;
    char *strtab = NULL;
    char *shstrtab = NULL;
    size_t strtab_size = 1U;
    size_t strtab_capacity = 64U;
    size_t shstrtab_size = 1U;
    size_t shstrtab_capacity = 64U;
    uint32_t symtab_name = 0U;
    uint32_t strtab_name = 0U;
    uint32_t shstrtab_name = 0U;
    unsigned char *image = NULL;
    uint64_t cursor;
    uint64_t section_header_offset;
    uint64_t total_size;
    FILE *file = NULL;
    bool ok = false;

    relocation_counts = calloc(section_count == 0U ? 1U : section_count,
                               sizeof(*relocation_counts));
    relocation_section_indices =
        malloc((section_count == 0U ? 1U : section_count) *
               sizeof(*relocation_section_indices));
    section_names = calloc(section_count == 0U ? 1U : section_count,
                           sizeof(*section_names));
    relocation_names = calloc(section_count == 0U ? 1U : section_count,
                              sizeof(*relocation_names));
    symbol_names = calloc(as->symbol_count == 0U ? 1U : as->symbol_count,
                          sizeof(*symbol_names));
    elf_symbol_indices =
        calloc(as->symbol_count == 0U ? 1U : as->symbol_count,
               sizeof(*elf_symbol_indices));
    symbols = calloc(symbol_count, sizeof(*symbols));
    strtab = calloc(strtab_capacity, 1U);
    shstrtab = calloc(shstrtab_capacity, 1U);
    if (relocation_counts == NULL || relocation_section_indices == NULL ||
        section_names == NULL || relocation_names == NULL ||
        symbol_names == NULL || elf_symbol_indices == NULL || symbols == NULL ||
        strtab == NULL || shstrtab == NULL) {
        minias_set_error(as, "out-of-memory:elf");
        goto done;
    }
    for (i = 0U; i < section_count; ++i) {
        relocation_section_indices[i] = SIZE_MAX;
        relocation_counts[i] = relocation_count_for_section(as, i);
        if (relocation_counts[i] != 0U) {
            ++relocation_section_count;
        }
    }

    output_section_count = section_count + relocation_section_count + 3U;
    if (output_section_count + 1U > UINT16_MAX) {
        minias_set_error(as, "too-many-elf-sections");
        goto done;
    }
    section_headers =
        calloc(output_section_count + 1U, sizeof(*section_headers));
    if (section_headers == NULL) {
        minias_set_error(as, "out-of-memory:elf-sections");
        goto done;
    }

    for (i = 0U; i < section_count; ++i) {
        if (!append_string(&shstrtab,
                           &shstrtab_size,
                           &shstrtab_capacity,
                           as->sections[i].name,
                           &section_names[i])) {
            goto oom;
        }
    }

    relocation_ordinal = 0U;
    for (i = 0U; i < section_count; ++i) {
        if (relocation_counts[i] != 0U) {
            char name[512];
            int written = snprintf(name, sizeof(name), ".rela%s", as->sections[i].name);
            if (written < 0 || (size_t)written >= sizeof(name) ||
                !append_string(&shstrtab,
                               &shstrtab_size,
                               &shstrtab_capacity,
                               name,
                               &relocation_names[i])) {
                goto oom;
            }
            relocation_section_indices[i] =
                section_count + 1U + relocation_ordinal;
            ++relocation_ordinal;
        }
    }

    if (!append_string(&shstrtab,
                       &shstrtab_size,
                       &shstrtab_capacity,
                       ".symtab",
                       &symtab_name) ||
        !append_string(&shstrtab,
                       &shstrtab_size,
                       &shstrtab_capacity,
                       ".strtab",
                       &strtab_name) ||
        !append_string(&shstrtab,
                       &shstrtab_size,
                       &shstrtab_capacity,
                       ".shstrtab",
                       &shstrtab_name)) {
        goto oom;
    }

    for (i = 0U; i < as->symbol_count; ++i) {
        if (!append_string(&strtab,
                           &strtab_size,
                           &strtab_capacity,
                           as->symbols[i].name,
                           &symbol_names[i])) {
            goto oom;
        }
    }

    symbol_index = 1U;
    for (i = 0U; i < section_count; ++i) {
        symbols[symbol_index].st_info =
            (unsigned char)ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
        symbols[symbol_index].st_shndx = (Elf64_Section)(i + 1U);
        ++symbol_index;
    }
    for (pass = 0U; pass < 2U; ++pass) {
        for (i = 0U; i < as->symbol_count; ++i) {
            MiniAsSymbol *symbol = &as->symbols[i];
            bool local = symbol->bind == MINIAS_STB_LOCAL;

            if ((pass == 0U) != local) {
                continue;
            }
            if (local && !symbol->defined) {
                minias_set_error(as, "undefined-local-symbol:%s", symbol->name);
                goto done;
            }
            elf_symbol_indices[i] = symbol_index;
            symbols[symbol_index].st_name = symbol_names[i];
            symbols[symbol_index].st_info =
                (unsigned char)ELF64_ST_INFO(symbol->bind, symbol->type);
            symbols[symbol_index].st_other = symbol->visibility;
            if (!symbol->defined) {
                symbols[symbol_index].st_shndx = SHN_UNDEF;
            } else if (symbol->section == MINIAS_SECTION_ABS) {
                symbols[symbol_index].st_shndx = SHN_ABS;
            } else {
                symbols[symbol_index].st_shndx =
                    (Elf64_Section)(symbol->section + 1);
            }
            symbols[symbol_index].st_value = symbol->defined ? symbol->value : 0U;
            symbols[symbol_index].st_size = symbol->size;
            ++symbol_index;
        }
        if (pass == 0U) {
            local_count = symbol_index;
        }
    }

    symtab_index = section_count + relocation_section_count + 1U;
    strtab_index = symtab_index + 1U;
    shstrtab_index = symtab_index + 2U;

    cursor = sizeof(Elf64_Ehdr);
    for (i = 0U; i < section_count; ++i) {
        MiniAsSection *section = &as->sections[i];
        uint64_t alignment = section->align == 0U ? 1U : section->align;

        cursor = align_up_u64(cursor, alignment);
        section_headers[i + 1U].sh_name = section_names[i];
        section_headers[i + 1U].sh_type = section->type;
        section_headers[i + 1U].sh_flags = section->flags;
        section_headers[i + 1U].sh_offset = cursor;
        section_headers[i + 1U].sh_size = (Elf64_Xword)section->size;
        section_headers[i + 1U].sh_addralign = alignment;
        if (section->type != SHT_NOBITS) {
            cursor += (uint64_t)section->size;
        }
    }

    relocation_ordinal = 0U;
    for (i = 0U; i < section_count; ++i) {
        if (relocation_counts[i] != 0U) {
            size_t index = section_count + 1U + relocation_ordinal;
            uint64_t bytes =
                (uint64_t)relocation_counts[i] * (uint64_t)sizeof(Elf64_Rela);

            cursor = align_up_u64(cursor, 8U);
            section_headers[index].sh_name = relocation_names[i];
            section_headers[index].sh_type = SHT_RELA;
            section_headers[index].sh_flags = SHF_INFO_LINK;
            section_headers[index].sh_offset = cursor;
            section_headers[index].sh_size = bytes;
            section_headers[index].sh_link = (Elf64_Word)symtab_index;
            section_headers[index].sh_info = (Elf64_Word)(i + 1U);
            section_headers[index].sh_addralign = 8U;
            section_headers[index].sh_entsize = sizeof(Elf64_Rela);
            cursor += bytes;
            ++relocation_ordinal;
        }
    }

    cursor = align_up_u64(cursor, 8U);
    section_headers[symtab_index].sh_name = symtab_name;
    section_headers[symtab_index].sh_type = SHT_SYMTAB;
    section_headers[symtab_index].sh_offset = cursor;
    section_headers[symtab_index].sh_size =
        (Elf64_Xword)(symbol_count * sizeof(Elf64_Sym));
    section_headers[symtab_index].sh_link = (Elf64_Word)strtab_index;
    section_headers[symtab_index].sh_info = (Elf64_Word)local_count;
    section_headers[symtab_index].sh_addralign = 8U;
    section_headers[symtab_index].sh_entsize = sizeof(Elf64_Sym);
    cursor += (uint64_t)(symbol_count * sizeof(Elf64_Sym));

    section_headers[strtab_index].sh_name = strtab_name;
    section_headers[strtab_index].sh_type = SHT_STRTAB;
    section_headers[strtab_index].sh_offset = cursor;
    section_headers[strtab_index].sh_size = (Elf64_Xword)strtab_size;
    section_headers[strtab_index].sh_addralign = 1U;
    cursor += (uint64_t)strtab_size;

    section_headers[shstrtab_index].sh_name = shstrtab_name;
    section_headers[shstrtab_index].sh_type = SHT_STRTAB;
    section_headers[shstrtab_index].sh_offset = cursor;
    section_headers[shstrtab_index].sh_size = (Elf64_Xword)shstrtab_size;
    section_headers[shstrtab_index].sh_addralign = 1U;
    cursor += (uint64_t)shstrtab_size;

    section_header_offset = align_up_u64(cursor, 8U);
    total_size =
        section_header_offset +
        (uint64_t)(output_section_count + 1U) * (uint64_t)sizeof(Elf64_Shdr);
    if (total_size > SIZE_MAX) {
        minias_set_error(as, "elf-too-large");
        goto done;
    }

    image = calloc((size_t)total_size, 1U);
    if (image == NULL) {
        goto oom;
    }

    {
        Elf64_Ehdr *header = (Elf64_Ehdr *)image;
        memcpy(header->e_ident, ELFMAG, SELFMAG);
        header->e_ident[EI_CLASS] = ELFCLASS64;
        header->e_ident[EI_DATA] = ELFDATA2LSB;
        header->e_ident[EI_VERSION] = EV_CURRENT;
        header->e_type = ET_REL;
        header->e_machine = EM_RISCV;
        header->e_version = EV_CURRENT;
        header->e_ehsize = sizeof(Elf64_Ehdr);
        header->e_shoff = section_header_offset;
        header->e_shentsize = sizeof(Elf64_Shdr);
        header->e_shnum = (Elf64_Half)(output_section_count + 1U);
        header->e_shstrndx = (Elf64_Half)shstrtab_index;
    }

    for (i = 0U; i < section_count; ++i) {
        if (as->sections[i].type != SHT_NOBITS && as->sections[i].size != 0U) {
            memcpy(image + section_headers[i + 1U].sh_offset,
                   as->sections[i].data,
                   as->sections[i].size);
        }
    }

    for (i = 0U; i < section_count; ++i) {
        if (relocation_counts[i] != 0U) {
            Elf64_Rela *out = (Elf64_Rela *)(void *)(
                image + section_headers[relocation_section_indices[i]].sh_offset);
            size_t written = 0U;
            size_t j;

            for (j = 0U; j < as->reloc_count; ++j) {
                MiniAsReloc *reloc = &as->relocs[j];
                if (reloc->section < 0 || (size_t)reloc->section != i) {
                    continue;
                }
                if (reloc->symbol_index >= as->symbol_count) {
                    minias_set_error(as, "internal:bad-relocation-symbol");
                    goto done;
                }
                out[written].r_offset = reloc->offset;
                out[written].r_info =
                    ELF64_R_INFO((Elf64_Xword)elf_symbol_indices[reloc->symbol_index],
                                 reloc->type);
                out[written].r_addend = reloc->addend;
                ++written;
            }
            if (written != relocation_counts[i]) {
                minias_set_error(as, "internal:relocation-count");
                goto done;
            }
        }
    }

    memcpy(image + section_headers[symtab_index].sh_offset,
           symbols,
           symbol_count * sizeof(Elf64_Sym));
    memcpy(image + section_headers[strtab_index].sh_offset, strtab, strtab_size);
    memcpy(image + section_headers[shstrtab_index].sh_offset,
           shstrtab,
           shstrtab_size);
    memcpy(image + section_header_offset,
           section_headers,
           (output_section_count + 1U) * sizeof(Elf64_Shdr));

    file = fopen(path, "wb");
    if (file == NULL) {
        minias_set_error(as, "output-open:%s", path);
        goto done;
    }
    if (fwrite(image, 1U, (size_t)total_size, file) != (size_t)total_size ||
        fclose(file) != 0) {
        file = NULL;
        minias_set_error(as, "output-write:%s", path);
        goto done;
    }
    file = NULL;
    ok = true;
    goto done;

oom:
    minias_set_error(as, "out-of-memory:elf");

done:
    if (file != NULL) {
        (void)fclose(file);
    }
    free(image);
    free(section_headers);
    free(symbols);
    free(elf_symbol_indices);
    free(symbol_names);
    free(section_names);
    free(relocation_names);
    free(relocation_section_indices);
    free(relocation_counts);
    free(strtab);
    free(shstrtab);
    return ok;
}

bool minias_write_elf32(MiniAs *as, const char *path) {
    size_t i;
    size_t pass;
    size_t section_count = as->section_count;
    size_t relocation_section_count = 0U;
    size_t symbol_count = 1U + section_count + as->symbol_count;
    size_t output_section_count;
    size_t symtab_index;
    size_t strtab_index;
    size_t shstrtab_index;
    size_t local_count = 0U;
    size_t symbol_index;
    size_t relocation_ordinal = 0U;
    size_t *relocation_counts = NULL;
    size_t *relocation_section_indices = NULL;
    size_t *elf_symbol_indices = NULL;
    uint32_t *symbol_names = NULL;
    uint32_t *section_names = NULL;
    uint32_t *relocation_names = NULL;
    Elf32_Sym *symbols = NULL;
    Elf32_Shdr *section_headers = NULL;
    char *strtab = NULL;
    char *shstrtab = NULL;
    size_t strtab_size = 1U;
    size_t strtab_capacity = 64U;
    size_t shstrtab_size = 1U;
    size_t shstrtab_capacity = 64U;
    uint32_t symtab_name = 0U;
    uint32_t strtab_name = 0U;
    uint32_t shstrtab_name = 0U;
    unsigned char *image = NULL;
    uint64_t cursor;
    uint64_t section_header_offset;
    uint64_t total_size;
    FILE *file = NULL;
    bool ok = false;

    relocation_counts = calloc(section_count == 0U ? 1U : section_count,
                               sizeof(*relocation_counts));
    relocation_section_indices =
        malloc((section_count == 0U ? 1U : section_count) *
               sizeof(*relocation_section_indices));
    section_names = calloc(section_count == 0U ? 1U : section_count,
                           sizeof(*section_names));
    relocation_names = calloc(section_count == 0U ? 1U : section_count,
                              sizeof(*relocation_names));
    symbol_names = calloc(as->symbol_count == 0U ? 1U : as->symbol_count,
                          sizeof(*symbol_names));
    elf_symbol_indices =
        calloc(as->symbol_count == 0U ? 1U : as->symbol_count,
               sizeof(*elf_symbol_indices));
    symbols = calloc(symbol_count, sizeof(*symbols));
    strtab = calloc(strtab_capacity, 1U);
    shstrtab = calloc(shstrtab_capacity, 1U);
    if (relocation_counts == NULL || relocation_section_indices == NULL ||
        section_names == NULL || relocation_names == NULL ||
        symbol_names == NULL || elf_symbol_indices == NULL || symbols == NULL ||
        strtab == NULL || shstrtab == NULL) {
        minias_set_error(as, "out-of-memory:elf");
        goto done;
    }
    for (i = 0U; i < section_count; ++i) {
        relocation_section_indices[i] = SIZE_MAX;
        relocation_counts[i] = relocation_count_for_section(as, i);
        if (relocation_counts[i] != 0U) {
            ++relocation_section_count;
        }
    }

    output_section_count = section_count + relocation_section_count + 3U;
    if (output_section_count + 1U > UINT16_MAX) {
        minias_set_error(as, "too-many-elf-sections");
        goto done;
    }
    section_headers =
        calloc(output_section_count + 1U, sizeof(*section_headers));
    if (section_headers == NULL) {
        minias_set_error(as, "out-of-memory:elf-sections");
        goto done;
    }

    for (i = 0U; i < section_count; ++i) {
        if (!append_string(&shstrtab,
                           &shstrtab_size,
                           &shstrtab_capacity,
                           as->sections[i].name,
                           &section_names[i])) {
            goto oom;
        }
    }

    relocation_ordinal = 0U;
    for (i = 0U; i < section_count; ++i) {
        if (relocation_counts[i] != 0U) {
            char name[512];
            int written = snprintf(name, sizeof(name), ".rela%s", as->sections[i].name);
            if (written < 0 || (size_t)written >= sizeof(name) ||
                !append_string(&shstrtab,
                               &shstrtab_size,
                               &shstrtab_capacity,
                               name,
                               &relocation_names[i])) {
                goto oom;
            }
            relocation_section_indices[i] =
                section_count + 1U + relocation_ordinal;
            ++relocation_ordinal;
        }
    }

    if (!append_string(&shstrtab,
                       &shstrtab_size,
                       &shstrtab_capacity,
                       ".symtab",
                       &symtab_name) ||
        !append_string(&shstrtab,
                       &shstrtab_size,
                       &shstrtab_capacity,
                       ".strtab",
                       &strtab_name) ||
        !append_string(&shstrtab,
                       &shstrtab_size,
                       &shstrtab_capacity,
                       ".shstrtab",
                       &shstrtab_name)) {
        goto oom;
    }

    for (i = 0U; i < as->symbol_count; ++i) {
        if (!append_string(&strtab,
                           &strtab_size,
                           &strtab_capacity,
                           as->symbols[i].name,
                           &symbol_names[i])) {
            goto oom;
        }
    }

    symbol_index = 1U;
    for (i = 0U; i < section_count; ++i) {
        symbols[symbol_index].st_info =
            (unsigned char)ELF32_ST_INFO(STB_LOCAL, STT_SECTION);
        symbols[symbol_index].st_shndx = (Elf32_Section)(i + 1U);
        ++symbol_index;
    }
    for (pass = 0U; pass < 2U; ++pass) {
        for (i = 0U; i < as->symbol_count; ++i) {
            MiniAsSymbol *symbol = &as->symbols[i];
            bool local = symbol->bind == MINIAS_STB_LOCAL;

            if ((pass == 0U) != local) {
                continue;
            }
            if (local && !symbol->defined) {
                minias_set_error(as, "undefined-local-symbol:%s", symbol->name);
                goto done;
            }
            elf_symbol_indices[i] = symbol_index;
            symbols[symbol_index].st_name = symbol_names[i];
            symbols[symbol_index].st_info =
                (unsigned char)ELF32_ST_INFO(symbol->bind, symbol->type);
            symbols[symbol_index].st_other = symbol->visibility;
            if (!symbol->defined) {
                symbols[symbol_index].st_shndx = SHN_UNDEF;
            } else if (symbol->section == MINIAS_SECTION_ABS) {
                symbols[symbol_index].st_shndx = SHN_ABS;
            } else {
                symbols[symbol_index].st_shndx =
                    (Elf32_Section)(symbol->section + 1);
            }
            if ((symbol->defined && symbol->value > UINT32_MAX) ||
                symbol->size > UINT32_MAX) {
                minias_set_error(as, "elf32-symbol-range:%s", symbol->name);
                goto done;
            }
            symbols[symbol_index].st_value =
                symbol->defined ? (Elf32_Addr)symbol->value : 0U;
            symbols[symbol_index].st_size = (Elf32_Word)symbol->size;
            ++symbol_index;
        }
        if (pass == 0U) {
            local_count = symbol_index;
        }
    }

    symtab_index = section_count + relocation_section_count + 1U;
    strtab_index = symtab_index + 1U;
    shstrtab_index = symtab_index + 2U;

    cursor = sizeof(Elf32_Ehdr);
    for (i = 0U; i < section_count; ++i) {
        MiniAsSection *section = &as->sections[i];
        uint64_t alignment = section->align == 0U ? 1U : section->align;

        cursor = align_up_u64(cursor, alignment);
        section_headers[i + 1U].sh_name = section_names[i];
        section_headers[i + 1U].sh_type = section->type;
        section_headers[i + 1U].sh_flags = (Elf32_Word)section->flags;
        section_headers[i + 1U].sh_offset = (Elf32_Off)cursor;
        section_headers[i + 1U].sh_size = (Elf32_Word)section->size;
        section_headers[i + 1U].sh_addralign = (Elf32_Word)alignment;
        if (section->type != SHT_NOBITS) {
            cursor += (uint64_t)section->size;
        }
    }

    relocation_ordinal = 0U;
    for (i = 0U; i < section_count; ++i) {
        if (relocation_counts[i] != 0U) {
            size_t index = section_count + 1U + relocation_ordinal;
            uint64_t bytes =
                (uint64_t)relocation_counts[i] * (uint64_t)sizeof(Elf32_Rela);

            cursor = align_up_u64(cursor, 4U);
            section_headers[index].sh_name = relocation_names[i];
            section_headers[index].sh_type = SHT_RELA;
            section_headers[index].sh_flags = SHF_INFO_LINK;
            section_headers[index].sh_offset = (Elf32_Off)cursor;
            section_headers[index].sh_size = (Elf32_Word)bytes;
            section_headers[index].sh_link = (Elf32_Word)symtab_index;
            section_headers[index].sh_info = (Elf32_Word)(i + 1U);
            section_headers[index].sh_addralign = 4U;
            section_headers[index].sh_entsize = sizeof(Elf32_Rela);
            cursor += bytes;
            ++relocation_ordinal;
        }
    }

    cursor = align_up_u64(cursor, 4U);
    section_headers[symtab_index].sh_name = symtab_name;
    section_headers[symtab_index].sh_type = SHT_SYMTAB;
    section_headers[symtab_index].sh_offset = (Elf32_Off)cursor;
    section_headers[symtab_index].sh_size =
        (Elf32_Word)(symbol_count * sizeof(Elf32_Sym));
    section_headers[symtab_index].sh_link = (Elf32_Word)strtab_index;
    section_headers[symtab_index].sh_info = (Elf32_Word)local_count;
    section_headers[symtab_index].sh_addralign = 4U;
    section_headers[symtab_index].sh_entsize = sizeof(Elf32_Sym);
    cursor += (uint64_t)(symbol_count * sizeof(Elf32_Sym));

    section_headers[strtab_index].sh_name = strtab_name;
    section_headers[strtab_index].sh_type = SHT_STRTAB;
    section_headers[strtab_index].sh_offset = (Elf32_Off)cursor;
    section_headers[strtab_index].sh_size = (Elf32_Word)strtab_size;
    section_headers[strtab_index].sh_addralign = 1U;
    cursor += (uint64_t)strtab_size;

    section_headers[shstrtab_index].sh_name = shstrtab_name;
    section_headers[shstrtab_index].sh_type = SHT_STRTAB;
    section_headers[shstrtab_index].sh_offset = (Elf32_Off)cursor;
    section_headers[shstrtab_index].sh_size = (Elf32_Word)shstrtab_size;
    section_headers[shstrtab_index].sh_addralign = 1U;
    cursor += (uint64_t)shstrtab_size;

    section_header_offset = align_up_u64(cursor, 4U);
    total_size =
        section_header_offset +
        (uint64_t)(output_section_count + 1U) * (uint64_t)sizeof(Elf32_Shdr);
    if (total_size > SIZE_MAX || total_size > UINT32_MAX ||
        section_header_offset > UINT32_MAX) {
        minias_set_error(as, "elf32-too-large");
        goto done;
    }

    image = calloc((size_t)total_size, 1U);
    if (image == NULL) {
        goto oom;
    }

    {
        Elf32_Ehdr *header = (Elf32_Ehdr *)image;
        memcpy(header->e_ident, ELFMAG, SELFMAG);
        header->e_ident[EI_CLASS] = ELFCLASS32;
        header->e_ident[EI_DATA] = ELFDATA2LSB;
        header->e_ident[EI_VERSION] = EV_CURRENT;
        header->e_type = ET_REL;
        header->e_machine = EM_RISCV;
        header->e_version = EV_CURRENT;
        header->e_ehsize = sizeof(Elf32_Ehdr);
        header->e_shoff = (Elf32_Off)section_header_offset;
        header->e_shentsize = sizeof(Elf32_Shdr);
        header->e_shnum = (Elf32_Half)(output_section_count + 1U);
        header->e_shstrndx = (Elf32_Half)shstrtab_index;
    }

    for (i = 0U; i < section_count; ++i) {
        if (as->sections[i].type != SHT_NOBITS && as->sections[i].size != 0U) {
            memcpy(image + section_headers[i + 1U].sh_offset,
                   as->sections[i].data,
                   as->sections[i].size);
        }
    }

    for (i = 0U; i < section_count; ++i) {
        if (relocation_counts[i] != 0U) {
            Elf32_Rela *out = (Elf32_Rela *)(void *)(
                image + section_headers[relocation_section_indices[i]].sh_offset);
            size_t written = 0U;
            size_t j;

            for (j = 0U; j < as->reloc_count; ++j) {
                MiniAsReloc *reloc = &as->relocs[j];
                if (reloc->section < 0 || (size_t)reloc->section != i) {
                    continue;
                }
                if (reloc->symbol_index >= as->symbol_count) {
                    minias_set_error(as, "internal:bad-relocation-symbol");
                    goto done;
                }
                if (reloc->offset > UINT32_MAX ||
                    reloc->addend < INT32_MIN || reloc->addend > INT32_MAX) {
                    minias_set_error(as, "elf32-relocation-range");
                    goto done;
                }
                out[written].r_offset = (Elf32_Addr)reloc->offset;
                out[written].r_info =
                    ELF32_R_INFO((Elf32_Word)elf_symbol_indices[reloc->symbol_index],
                                 reloc->type);
                out[written].r_addend = (Elf32_Sword)reloc->addend;
                ++written;
            }
            if (written != relocation_counts[i]) {
                minias_set_error(as, "internal:relocation-count");
                goto done;
            }
        }
    }

    memcpy(image + section_headers[symtab_index].sh_offset,
           symbols,
           symbol_count * sizeof(Elf32_Sym));
    memcpy(image + section_headers[strtab_index].sh_offset, strtab, strtab_size);
    memcpy(image + section_headers[shstrtab_index].sh_offset,
           shstrtab,
           shstrtab_size);
    memcpy(image + section_header_offset,
           section_headers,
           (output_section_count + 1U) * sizeof(Elf32_Shdr));

    file = fopen(path, "wb");
    if (file == NULL) {
        minias_set_error(as, "output-open:%s", path);
        goto done;
    }
    if (fwrite(image, 1U, (size_t)total_size, file) != (size_t)total_size ||
        fclose(file) != 0) {
        file = NULL;
        minias_set_error(as, "output-write:%s", path);
        goto done;
    }
    file = NULL;
    ok = true;
    goto done;

oom:
    minias_set_error(as, "out-of-memory:elf");

done:
    if (file != NULL) {
        (void)fclose(file);
    }
    free(image);
    free(section_headers);
    free(symbols);
    free(elf_symbol_indices);
    free(symbol_names);
    free(section_names);
    free(relocation_names);
    free(relocation_section_indices);
    free(relocation_counts);
    free(strtab);
    free(shstrtab);
    return ok;
}

