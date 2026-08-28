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
            next *= 2U;
        }
        next_buffer = realloc(*buffer, next);
        if (next_buffer == NULL) {
            return false;
        }
        *buffer = next_buffer;
        *capacity = next;
    }
    *offset = (uint32_t)*size;
    memcpy(*buffer + *size, text, n);
    *size += n;
    return true;
}

bool minias_write_elf64(MiniAs *as, const char *path) {
    size_t i;
    size_t pass;
    size_t local_count;
    size_t symbol_count = 1U + as->section_count + as->symbol_count;
    Elf64_Sym *symbols = NULL;
    char *strtab = NULL;
    char *shstrtab = NULL;
    size_t strtab_size = 1U;
    size_t strtab_capacity = 64U;
    size_t shstrtab_size = 1U;
    size_t shstrtab_capacity = 64U;
    uint32_t *symbol_names = NULL;
    uint32_t *section_names = NULL;
    size_t output_section_count = as->section_count + 3U;
    Elf64_Shdr *section_headers = NULL;
    unsigned char *image = NULL;
    uint64_t cursor;
    uint64_t section_header_offset;
    uint64_t total_size;
    FILE *file = NULL;
    bool ok = false;
    uint32_t symtab_name;
    uint32_t strtab_name;
    uint32_t shstrtab_name;
    size_t symbol_index;

    strtab = calloc(strtab_capacity, 1U);
    shstrtab = calloc(shstrtab_capacity, 1U);
    symbols = calloc(symbol_count, sizeof(*symbols));
    symbol_names = calloc(as->symbol_count, sizeof(*symbol_names));
    section_names = calloc(as->section_count, sizeof(*section_names));
    section_headers = calloc(output_section_count + 1U, sizeof(*section_headers));
    if (strtab == NULL || shstrtab == NULL || symbols == NULL || symbol_names == NULL ||
        section_names == NULL || section_headers == NULL) {
        minias_set_error(as, "out-of-memory:elf");
        goto done;
    }

    for (i = 0U; i < as->section_count; ++i) {
        if (!append_string(&shstrtab,
                           &shstrtab_size,
                           &shstrtab_capacity,
                           as->sections[i].name,
                           &section_names[i])) {
            goto oom;
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
    for (i = 0U; i < as->section_count; ++i) {
        symbols[symbol_index].st_info = (unsigned char)ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
        symbols[symbol_index].st_shndx = (Elf64_Section)(i + 1U);
        ++symbol_index;
    }
    local_count = symbol_index;

    for (pass = 0U; pass < 2U; ++pass) {
        for (i = 0U; i < as->symbol_count; ++i) {
            MiniAsSymbol *symbol = &as->symbols[i];
            bool local = symbol->bind == MINIAS_STB_LOCAL;

            if ((pass == 0U) != local) {
                continue;
            }
            symbols[symbol_index].st_name = symbol_names[i];
            symbols[symbol_index].st_info =
                (unsigned char)ELF64_ST_INFO(symbol->bind, symbol->type);
            symbols[symbol_index].st_other = symbol->visibility;
            symbols[symbol_index].st_shndx =
                symbol->defined ? (Elf64_Section)(symbol->section + 1) : SHN_UNDEF;
            symbols[symbol_index].st_value = symbol->defined ? symbol->value : 0U;
            symbols[symbol_index].st_size = symbol->size;
            ++symbol_index;
            if (local) {
                ++local_count;
            }
        }
    }

    cursor = sizeof(Elf64_Ehdr);
    for (i = 0U; i < as->section_count; ++i) {
        MiniAsSection *section = &as->sections[i];

        cursor = align_up_u64(cursor, section->align);
        section_headers[i + 1U].sh_name = section_names[i];
        section_headers[i + 1U].sh_type = section->type;
        section_headers[i + 1U].sh_flags = section->flags;
        section_headers[i + 1U].sh_offset = cursor;
        section_headers[i + 1U].sh_size = section->size;
        section_headers[i + 1U].sh_addralign = section->align;
        if (section->type != SHT_NOBITS) {
            cursor += section->size;
        }
    }

    cursor = align_up_u64(cursor, 8U);
    section_headers[as->section_count + 1U].sh_name = symtab_name;
    section_headers[as->section_count + 1U].sh_type = SHT_SYMTAB;
    section_headers[as->section_count + 1U].sh_offset = cursor;
    section_headers[as->section_count + 1U].sh_size = symbol_count * sizeof(Elf64_Sym);
    section_headers[as->section_count + 1U].sh_link =
        (Elf64_Word)(as->section_count + 2U);
    section_headers[as->section_count + 1U].sh_info = (Elf64_Word)local_count;
    section_headers[as->section_count + 1U].sh_addralign = 8U;
    section_headers[as->section_count + 1U].sh_entsize = sizeof(Elf64_Sym);
    cursor += symbol_count * sizeof(Elf64_Sym);

    section_headers[as->section_count + 2U].sh_name = strtab_name;
    section_headers[as->section_count + 2U].sh_type = SHT_STRTAB;
    section_headers[as->section_count + 2U].sh_offset = cursor;
    section_headers[as->section_count + 2U].sh_size = strtab_size;
    section_headers[as->section_count + 2U].sh_addralign = 1U;
    cursor += strtab_size;

    section_headers[as->section_count + 3U].sh_name = shstrtab_name;
    section_headers[as->section_count + 3U].sh_type = SHT_STRTAB;
    section_headers[as->section_count + 3U].sh_offset = cursor;
    section_headers[as->section_count + 3U].sh_size = shstrtab_size;
    section_headers[as->section_count + 3U].sh_addralign = 1U;
    cursor += shstrtab_size;

    section_header_offset = align_up_u64(cursor, 8U);
    total_size =
        section_header_offset + (output_section_count + 1U) * sizeof(Elf64_Shdr);
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
        header->e_shstrndx = (Elf64_Half)(as->section_count + 3U);
    }

    for (i = 0U; i < as->section_count; ++i) {
        if (as->sections[i].type != SHT_NOBITS && as->sections[i].size != 0U) {
            memcpy(image + section_headers[i + 1U].sh_offset,
                   as->sections[i].data,
                   as->sections[i].size);
        }
    }
    memcpy(image + section_headers[as->section_count + 1U].sh_offset,
           symbols,
           symbol_count * sizeof(Elf64_Sym));
    memcpy(image + section_headers[as->section_count + 2U].sh_offset,
           strtab,
           strtab_size);
    memcpy(image + section_headers[as->section_count + 3U].sh_offset,
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
    minias_set_error(as, "out-of-memory:elf-string");

done:
    if (file != NULL) {
        fclose(file);
    }
    free(image);
    free(section_headers);
    free(section_names);
    free(symbol_names);
    free(symbols);
    free(strtab);
    free(shstrtab);
    return ok;
}
