#include "minild.h"

#include <elf.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINILD_SECTION_UNDEF (-1)
#define MINILD_SECTION_ABS (-2)
#define MINILD_SECTION_COMMON (-3)

typedef struct MiniLdBuffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} MiniLdBuffer;

typedef struct MiniLdSection {
    char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    uint64_t entsize;
    unsigned char *data;
    size_t size;
    size_t capacity;
} MiniLdSection;

typedef struct MiniLdSymbol {
    char *name;
    int section;
    uint64_t value;
    uint64_t size;
    unsigned char info;
    unsigned char other;
    size_t final_index;
} MiniLdSymbol;

typedef struct MiniLdReloc {
    size_t section;
    uint64_t offset;
    uint32_t type;
    size_t symbol;
    int64_t addend;
} MiniLdReloc;

typedef struct MiniLdState {
    MiniLdSection *sections;
    size_t section_count;
    size_t section_capacity;
    MiniLdSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    MiniLdReloc *relocs;
    size_t reloc_count;
    size_t reloc_capacity;
    uint32_t elf_flags;
    bool have_input;
    FILE *diagnostics;
} MiniLdState;

typedef struct MiniLdSectionMap {
    size_t output_section;
    uint64_t base;
    bool mapped;
} MiniLdSectionMap;

static char *minild_strdup(const char *text) {
    size_t size = strlen(text) + 1U;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static bool range_ok(size_t offset, size_t amount, size_t total) {
    return offset <= total && amount <= total - offset;
}

static bool add_size(size_t a, size_t b, size_t *out) {
    if (b > SIZE_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool align_up_size(size_t value, uint64_t alignment, size_t *out) {
    size_t align;
    size_t mask;

    if (alignment <= 1U) {
        *out = value;
        return true;
    }
    if (alignment > SIZE_MAX) {
        return false;
    }
    align = (size_t)alignment;
    if ((align & (align - 1U)) != 0U) {
        return false;
    }
    mask = align - 1U;
    if (value > SIZE_MAX - mask) {
        return false;
    }
    *out = (value + mask) & ~mask;
    return true;
}

static bool buffer_reserve(MiniLdBuffer *buffer, size_t extra) {
    size_t required;
    size_t next;
    unsigned char *data;

    if (!add_size(buffer->size, extra, &required)) {
        return false;
    }
    if (required <= buffer->capacity) {
        return true;
    }
    next = buffer->capacity == 0U ? 128U : buffer->capacity;
    while (next < required) {
        if (next > SIZE_MAX / 2U) {
            next = required;
            break;
        }
        next *= 2U;
    }
    data = realloc(buffer->data, next);
    if (data == NULL) {
        return false;
    }
    buffer->data = data;
    buffer->capacity = next;
    return true;
}

static bool buffer_append(MiniLdBuffer *buffer, const void *data, size_t size) {
    if (!buffer_reserve(buffer, size)) {
        return false;
    }
    if (size != 0U) {
        memcpy(buffer->data + buffer->size, data, size);
    }
    buffer->size += size;
    return true;
}

static bool buffer_append_zero(MiniLdBuffer *buffer, size_t size) {
    if (!buffer_reserve(buffer, size)) {
        return false;
    }
    memset(buffer->data + buffer->size, 0, size);
    buffer->size += size;
    return true;
}

static bool buffer_append_string(MiniLdBuffer *buffer,
                                 const char *text,
                                 uint32_t *offset_out) {
    size_t size = strlen(text) + 1U;

    if (buffer->size > UINT32_MAX) {
        return false;
    }
    *offset_out = (uint32_t)buffer->size;
    return buffer_append(buffer, text, size);
}

static bool read_file(const char *path,
                      unsigned char **data_out,
                      size_t *size_out,
                      FILE *diagnostics) {
    FILE *file = fopen(path, "rb");
    long end;
    size_t size;
    unsigned char *data;

    if (file == NULL) {
        fprintf(diagnostics, "minic-ld: cannot-open:%s:%s\n", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (end = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fprintf(diagnostics, "minic-ld: cannot-size:%s\n", path);
        fclose(file);
        return false;
    }
    size = (size_t)end;
    data = malloc(size == 0U ? 1U : size);
    if (data == NULL) {
        fprintf(diagnostics, "minic-ld: out-of-memory:%s\n", path);
        fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        fprintf(diagnostics, "minic-ld: cannot-read:%s\n", path);
        free(data);
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        fprintf(diagnostics, "minic-ld: cannot-close:%s\n", path);
        free(data);
        return false;
    }
    *data_out = data;
    *size_out = size;
    return true;
}

static void state_destroy(MiniLdState *state) {
    size_t i;

    for (i = 0U; i < state->section_count; ++i) {
        free(state->sections[i].name);
        free(state->sections[i].data);
    }
    for (i = 0U; i < state->symbol_count; ++i) {
        free(state->symbols[i].name);
    }
    free(state->sections);
    free(state->symbols);
    free(state->relocs);
}

static bool ensure_section_capacity(MiniLdState *state) {
    MiniLdSection *next;
    size_t capacity;

    if (state->section_count < state->section_capacity) {
        return true;
    }
    capacity = state->section_capacity == 0U ? 16U : state->section_capacity * 2U;
    if (capacity < state->section_capacity ||
        capacity > SIZE_MAX / sizeof(*state->sections)) {
        return false;
    }
    next = realloc(state->sections, capacity * sizeof(*state->sections));
    if (next == NULL) {
        return false;
    }
    state->sections = next;
    state->section_capacity = capacity;
    return true;
}

static bool ensure_symbol_capacity(MiniLdState *state) {
    MiniLdSymbol *next;
    size_t capacity;

    if (state->symbol_count < state->symbol_capacity) {
        return true;
    }
    capacity = state->symbol_capacity == 0U ? 64U : state->symbol_capacity * 2U;
    if (capacity < state->symbol_capacity ||
        capacity > SIZE_MAX / sizeof(*state->symbols)) {
        return false;
    }
    next = realloc(state->symbols, capacity * sizeof(*state->symbols));
    if (next == NULL) {
        return false;
    }
    state->symbols = next;
    state->symbol_capacity = capacity;
    return true;
}

static bool ensure_reloc_capacity(MiniLdState *state) {
    MiniLdReloc *next;
    size_t capacity;

    if (state->reloc_count < state->reloc_capacity) {
        return true;
    }
    capacity = state->reloc_capacity == 0U ? 64U : state->reloc_capacity * 2U;
    if (capacity < state->reloc_capacity ||
        capacity > SIZE_MAX / sizeof(*state->relocs)) {
        return false;
    }
    next = realloc(state->relocs, capacity * sizeof(*state->relocs));
    if (next == NULL) {
        return false;
    }
    state->relocs = next;
    state->reloc_capacity = capacity;
    return true;
}

static bool section_append_zero(MiniLdSection *section, size_t size) {
    unsigned char *next;
    size_t required;
    size_t capacity;

    if (section->type == SHT_NOBITS) {
        return add_size(section->size, size, &section->size);
    }
    if (!add_size(section->size, size, &required)) {
        return false;
    }
    if (required > section->capacity) {
        capacity = section->capacity == 0U ? 128U : section->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                capacity = required;
                break;
            }
            capacity *= 2U;
        }
        next = realloc(section->data, capacity);
        if (next == NULL) {
            return false;
        }
        section->data = next;
        section->capacity = capacity;
    }
    memset(section->data + section->size, 0, size);
    section->size = required;
    return true;
}

static bool section_append_data(MiniLdSection *section,
                                const unsigned char *data,
                                size_t size) {
    size_t old_size = section->size;

    if (section->type == SHT_NOBITS) {
        return false;
    }
    if (!section_append_zero(section, size)) {
        return false;
    }
    if (size != 0U) {
        memcpy(section->data + old_size, data, size);
    }
    return true;
}

static bool find_or_add_section(MiniLdState *state,
                                const char *name,
                                uint32_t type,
                                uint64_t flags,
                                uint64_t align,
                                uint64_t entsize,
                                size_t *section_out) {
    size_t i;
    MiniLdSection *section;

    for (i = 0U; i < state->section_count; ++i) {
        section = &state->sections[i];
        if (strcmp(section->name, name) == 0) {
            if (section->type != type || section->flags != flags ||
                section->entsize != entsize) {
                fprintf(state->diagnostics,
                        "minic-ld: incompatible-section:%s\n",
                        name);
                return false;
            }
            if (align > section->align) {
                section->align = align;
            }
            *section_out = i;
            return true;
        }
    }
    if (!ensure_section_capacity(state)) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:sections\n");
        return false;
    }
    section = &state->sections[state->section_count];
    memset(section, 0, sizeof(*section));
    section->name = minild_strdup(name);
    if (section->name == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:section-name\n");
        return false;
    }
    section->type = type;
    section->flags = flags;
    section->align = align == 0U ? 1U : align;
    section->entsize = entsize;
    *section_out = state->section_count++;
    return true;
}

static bool add_local_symbol(MiniLdState *state,
                             const char *name,
                             int section,
                             uint64_t value,
                             uint64_t size,
                             unsigned char info,
                             unsigned char other,
                             size_t *symbol_out) {
    MiniLdSymbol *symbol;

    if (!ensure_symbol_capacity(state)) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:symbols\n");
        return false;
    }
    symbol = &state->symbols[state->symbol_count];
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = minild_strdup(name);
    if (symbol->name == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:symbol-name\n");
        return false;
    }
    symbol->section = section;
    symbol->value = value;
    symbol->size = size;
    symbol->info = info;
    symbol->other = other;
    symbol->final_index = 0U;
    *symbol_out = state->symbol_count++;
    return true;
}

static bool symbol_is_defined(const MiniLdSymbol *symbol) {
    return symbol->section != MINILD_SECTION_UNDEF;
}

static size_t find_global_symbol(const MiniLdState *state, const char *name) {
    size_t i;

    for (i = 0U; i < state->symbol_count; ++i) {
        if (ELF64_ST_BIND(state->symbols[i].info) != STB_LOCAL &&
            strcmp(state->symbols[i].name, name) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

static bool add_or_merge_global_symbol(MiniLdState *state,
                                       const char *name,
                                       int section,
                                       uint64_t value,
                                       uint64_t size,
                                       unsigned char info,
                                       unsigned char other,
                                       size_t *symbol_out) {
    size_t existing_index = find_global_symbol(state, name);
    unsigned new_bind = ELF64_ST_BIND(info);

    if (existing_index == SIZE_MAX) {
        return add_local_symbol(state,
                                name,
                                section,
                                value,
                                size,
                                info,
                                other,
                                symbol_out);
    }

    {
        MiniLdSymbol *existing = &state->symbols[existing_index];
        unsigned old_bind = ELF64_ST_BIND(existing->info);
        bool old_defined = symbol_is_defined(existing);
        bool new_defined = section != MINILD_SECTION_UNDEF;

        if (!old_defined && new_defined) {
            existing->section = section;
            existing->value = value;
            existing->size = size;
            existing->info = info;
            existing->other = other;
        } else if (old_defined && new_defined) {
            if (old_bind == STB_WEAK && new_bind != STB_WEAK) {
                existing->section = section;
                existing->value = value;
                existing->size = size;
                existing->info = info;
                existing->other = other;
            } else if (old_bind != STB_WEAK && new_bind != STB_WEAK) {
                fprintf(state->diagnostics,
                        "minic-ld: multiple-definition:%s\n",
                        name);
                return false;
            }
        } else if (!old_defined && !new_defined &&
                   old_bind == STB_WEAK && new_bind != STB_WEAK) {
            existing->info = info;
            existing->other = other;
        }
    }
    *symbol_out = existing_index;
    return true;
}

static bool add_relocation(MiniLdState *state,
                           size_t section,
                           uint64_t offset,
                           uint32_t type,
                           size_t symbol,
                           int64_t addend) {
    MiniLdReloc *reloc;

    if (!ensure_reloc_capacity(state)) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:relocations\n");
        return false;
    }
    reloc = &state->relocs[state->reloc_count++];
    reloc->section = section;
    reloc->offset = offset;
    reloc->type = type;
    reloc->symbol = symbol;
    reloc->addend = addend;
    return true;
}

static bool input_string(const unsigned char *data,
                         size_t size,
                         const Elf64_Shdr *table,
                         uint32_t offset,
                         const char **text_out) {
    size_t base;
    size_t available;
    const char *text;

    if (table->sh_offset > SIZE_MAX || table->sh_size > SIZE_MAX ||
        offset >= table->sh_size) {
        return false;
    }
    base = (size_t)table->sh_offset;
    if (!range_ok(base, (size_t)table->sh_size, size)) {
        return false;
    }
    text = (const char *)data + base + offset;
    available = (size_t)table->sh_size - offset;
    if (memchr(text, '\0', available) == NULL) {
        return false;
    }
    *text_out = text;
    return true;
}

static bool section_supported_for_merge(const Elf64_Shdr *section) {
    if (section->sh_type == SHT_GROUP ||
        section->sh_type == SHT_SYMTAB_SHNDX ||
        section->sh_type == SHT_REL) {
        return false;
    }
    if ((section->sh_flags & SHF_LINK_ORDER) != 0U ||
        (section->sh_flags & SHF_GROUP) != 0U) {
        return false;
    }
    return true;
}

static bool process_input(MiniLdState *state, const char *path) {
    unsigned char *data = NULL;
    size_t size = 0U;
    Elf64_Ehdr ehdr;
    const Elf64_Shdr *section_headers;
    const Elf64_Shdr *shstrtab;
    const Elf64_Shdr *symtab = NULL;
    const Elf64_Shdr *strtab = NULL;
    size_t symtab_index = SIZE_MAX;
    size_t strtab_index = SIZE_MAX;
    MiniLdSectionMap *section_map = NULL;
    size_t *symbol_map = NULL;
    size_t symbol_count = 0U;
    size_t i;
    bool ok = false;

    if (!read_file(path, &data, &size, state->diagnostics)) {
        return false;
    }
    if (!range_ok(0U, sizeof(ehdr), size)) {
        fprintf(state->diagnostics, "minic-ld: truncated-elf:%s\n", path);
        goto done;
    }
    memcpy(&ehdr, data, sizeof(ehdr));
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_type != ET_REL ||
        ehdr.e_machine != EM_RISCV ||
        ehdr.e_version != EV_CURRENT ||
        ehdr.e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr.e_shnum == 0U ||
        ehdr.e_shstrndx == SHN_UNDEF ||
        ehdr.e_shstrndx >= ehdr.e_shnum) {
        fprintf(state->diagnostics, "minic-ld: unsupported-elf:%s\n", path);
        goto done;
    }
    if (ehdr.e_shoff > SIZE_MAX ||
        !range_ok((size_t)ehdr.e_shoff,
                  (size_t)ehdr.e_shnum * (size_t)ehdr.e_shentsize,
                  size)) {
        fprintf(state->diagnostics, "minic-ld: invalid-section-table:%s\n", path);
        goto done;
    }

    section_headers = (const Elf64_Shdr *)(const void *)(data + (size_t)ehdr.e_shoff);
    shstrtab = &section_headers[ehdr.e_shstrndx];
    if (shstrtab->sh_type != SHT_STRTAB ||
        shstrtab->sh_offset > SIZE_MAX ||
        shstrtab->sh_size > SIZE_MAX ||
        !range_ok((size_t)shstrtab->sh_offset, (size_t)shstrtab->sh_size, size)) {
        fprintf(state->diagnostics, "minic-ld: invalid-shstrtab:%s\n", path);
        goto done;
    }

    for (i = 1U; i < ehdr.e_shnum; ++i) {
        if (section_headers[i].sh_type == SHT_SYMTAB) {
            if (symtab != NULL) {
                fprintf(state->diagnostics, "minic-ld: multiple-symtabs:%s\n", path);
                goto done;
            }
            symtab = &section_headers[i];
            symtab_index = i;
        }
    }
    if (symtab == NULL || symtab->sh_link >= ehdr.e_shnum ||
        symtab->sh_entsize < sizeof(Elf64_Sym)) {
        fprintf(state->diagnostics, "minic-ld: missing-symtab:%s\n", path);
        goto done;
    }
    strtab_index = symtab->sh_link;
    strtab = &section_headers[strtab_index];
    if (strtab->sh_type != SHT_STRTAB ||
        symtab->sh_offset > SIZE_MAX || symtab->sh_size > SIZE_MAX ||
        strtab->sh_offset > SIZE_MAX || strtab->sh_size > SIZE_MAX ||
        !range_ok((size_t)symtab->sh_offset, (size_t)symtab->sh_size, size) ||
        !range_ok((size_t)strtab->sh_offset, (size_t)strtab->sh_size, size)) {
        fprintf(state->diagnostics, "minic-ld: invalid-symtab:%s\n", path);
        goto done;
    }

    section_map = calloc(ehdr.e_shnum, sizeof(*section_map));
    if (section_map == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:section-map\n");
        goto done;
    }

    for (i = 1U; i < ehdr.e_shnum; ++i) {
        const Elf64_Shdr *input = &section_headers[i];
        const char *name;
        size_t output_index;
        MiniLdSection *output;
        size_t base;
        uint64_t alignment = input->sh_addralign == 0U ? 1U : input->sh_addralign;

        if (i == symtab_index || i == strtab_index ||
            i == ehdr.e_shstrndx ||
            input->sh_type == SHT_RELA) {
            continue;
        }
        if (!section_supported_for_merge(input)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-section-type:%s:index=%zu:type=%u\n",
                    path,
                    i,
                    input->sh_type);
            goto done;
        }
        if (!input_string(data, size, shstrtab, input->sh_name, &name)) {
            fprintf(state->diagnostics, "minic-ld: invalid-section-name:%s\n", path);
            goto done;
        }
        if (!find_or_add_section(state,
                                 name,
                                 input->sh_type,
                                 input->sh_flags,
                                 alignment,
                                 input->sh_entsize,
                                 &output_index)) {
            goto done;
        }
        output = &state->sections[output_index];
        if (!align_up_size(output->size, alignment, &base) ||
            !section_append_zero(output, base - output->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: cannot-align-section:%s\n",
                    name);
            goto done;
        }
        if (input->sh_size > SIZE_MAX) {
            fprintf(state->diagnostics, "minic-ld: section-too-large:%s\n", name);
            goto done;
        }
        if (input->sh_type == SHT_NOBITS) {
            if (!section_append_zero(output, (size_t)input->sh_size)) {
                fprintf(state->diagnostics, "minic-ld: section-overflow:%s\n", name);
                goto done;
            }
        } else {
            if (input->sh_offset > SIZE_MAX ||
                !range_ok((size_t)input->sh_offset, (size_t)input->sh_size, size) ||
                !section_append_data(output,
                                     data + (size_t)input->sh_offset,
                                     (size_t)input->sh_size)) {
                fprintf(state->diagnostics, "minic-ld: invalid-section-data:%s\n", name);
                goto done;
            }
        }
        section_map[i].output_section = output_index;
        section_map[i].base = (uint64_t)base;
        section_map[i].mapped = true;
    }

    symbol_count = (size_t)(symtab->sh_size / symtab->sh_entsize);
    symbol_map = malloc((symbol_count == 0U ? 1U : symbol_count) *
                        sizeof(*symbol_map));
    if (symbol_map == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:symbol-map\n");
        goto done;
    }
    for (i = 0U; i < symbol_count; ++i) {
        symbol_map[i] = SIZE_MAX;
    }

    for (i = 1U; i < symbol_count; ++i) {
        size_t offset = (size_t)symtab->sh_offset + i * (size_t)symtab->sh_entsize;
        Elf64_Sym input_symbol;
        const char *name;
        int output_section = MINILD_SECTION_UNDEF;
        uint64_t value;
        size_t output_symbol;
        unsigned bind;

        if (!range_ok(offset, sizeof(input_symbol), size)) {
            fprintf(state->diagnostics, "minic-ld: truncated-symbol:%s\n", path);
            goto done;
        }
        memcpy(&input_symbol, data + offset, sizeof(input_symbol));
        if (!input_string(data, size, strtab, input_symbol.st_name, &name)) {
            fprintf(state->diagnostics, "minic-ld: invalid-symbol-name:%s\n", path);
            goto done;
        }
        value = input_symbol.st_value;
        if (input_symbol.st_shndx == SHN_UNDEF) {
            output_section = MINILD_SECTION_UNDEF;
        } else if (input_symbol.st_shndx == SHN_ABS) {
            output_section = MINILD_SECTION_ABS;
        } else if (input_symbol.st_shndx == SHN_COMMON) {
            output_section = MINILD_SECTION_COMMON;
        } else if (input_symbol.st_shndx >= ehdr.e_shnum ||
                   !section_map[input_symbol.st_shndx].mapped) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-symbol-section:%s:%s\n",
                    path,
                    name);
            goto done;
        } else {
            output_section = (int)section_map[input_symbol.st_shndx].output_section;
            value += section_map[input_symbol.st_shndx].base;
        }

        bind = ELF64_ST_BIND(input_symbol.st_info);
        if (bind == STB_LOCAL) {
            if (!add_local_symbol(state,
                                  name,
                                  output_section,
                                  value,
                                  input_symbol.st_size,
                                  input_symbol.st_info,
                                  input_symbol.st_other,
                                  &output_symbol)) {
                goto done;
            }
        } else if (bind == STB_GLOBAL || bind == STB_WEAK) {
            if (!add_or_merge_global_symbol(state,
                                            name,
                                            output_section,
                                            value,
                                            input_symbol.st_size,
                                            input_symbol.st_info,
                                            input_symbol.st_other,
                                            &output_symbol)) {
                goto done;
            }
        } else {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-symbol-binding:%s:%s:%u\n",
                    path,
                    name,
                    bind);
            goto done;
        }
        symbol_map[i] = output_symbol;
    }

    for (i = 1U; i < ehdr.e_shnum; ++i) {
        const Elf64_Shdr *rela_section = &section_headers[i];
        size_t count;
        size_t j;
        MiniLdSectionMap target_map;

        if (rela_section->sh_type != SHT_RELA) {
            continue;
        }
        if (rela_section->sh_link != symtab_index ||
            rela_section->sh_info >= ehdr.e_shnum ||
            !section_map[rela_section->sh_info].mapped ||
            rela_section->sh_entsize < sizeof(Elf64_Rela) ||
            rela_section->sh_offset > SIZE_MAX ||
            rela_section->sh_size > SIZE_MAX ||
            !range_ok((size_t)rela_section->sh_offset,
                      (size_t)rela_section->sh_size,
                      size)) {
            fprintf(state->diagnostics, "minic-ld: invalid-rela:%s\n", path);
            goto done;
        }
        target_map = section_map[rela_section->sh_info];
        count = (size_t)(rela_section->sh_size / rela_section->sh_entsize);
        for (j = 0U; j < count; ++j) {
            size_t offset = (size_t)rela_section->sh_offset +
                            j * (size_t)rela_section->sh_entsize;
            Elf64_Rela rela;
            size_t input_symbol_index;
            size_t output_symbol = SIZE_MAX;

            if (!range_ok(offset, sizeof(rela), size)) {
                fprintf(state->diagnostics, "minic-ld: truncated-rela:%s\n", path);
                goto done;
            }
            memcpy(&rela, data + offset, sizeof(rela));
            input_symbol_index = (size_t)ELF64_R_SYM(rela.r_info);
            if (input_symbol_index >= symbol_count) {
                fprintf(state->diagnostics, "minic-ld: invalid-rela-symbol:%s\n", path);
                goto done;
            }
            if (input_symbol_index != 0U) {
                output_symbol = symbol_map[input_symbol_index];
                if (output_symbol == SIZE_MAX) {
                    fprintf(state->diagnostics,
                            "minic-ld: unmapped-rela-symbol:%s\n",
                            path);
                    goto done;
                }
            }
            if (!add_relocation(state,
                                target_map.output_section,
                                rela.r_offset + target_map.base,
                                (uint32_t)ELF64_R_TYPE(rela.r_info),
                                output_symbol,
                                rela.r_addend)) {
                goto done;
            }
        }
    }

    state->elf_flags |= ehdr.e_flags;
    state->have_input = true;
    ok = true;

done:
    free(symbol_map);
    free(section_map);
    free(data);
    return ok;
}

static size_t relocation_count_for_section(const MiniLdState *state,
                                           size_t section) {
    size_t i;
    size_t count = 0U;

    for (i = 0U; i < state->reloc_count; ++i) {
        if (state->relocs[i].section == section) {
            ++count;
        }
    }
    return count;
}

static bool assign_symbol_indices(MiniLdState *state, size_t *local_count_out) {
    size_t i;
    size_t next = 1U;

    for (i = 0U; i < state->symbol_count; ++i) {
        if (ELF64_ST_BIND(state->symbols[i].info) == STB_LOCAL) {
            state->symbols[i].final_index = next++;
        }
    }
    *local_count_out = next;
    for (i = 0U; i < state->symbol_count; ++i) {
        if (ELF64_ST_BIND(state->symbols[i].info) != STB_LOCAL) {
            state->symbols[i].final_index = next++;
        }
    }
    return true;
}

static bool write_output(MiniLdState *state, const char *path) {
    size_t i;
    size_t local_count;
    size_t relocation_section_count = 0U;
    size_t output_section_count;
    size_t symtab_index;
    size_t strtab_index;
    size_t shstrtab_index;
    size_t *relocation_section_indices = NULL;
    uint32_t *section_name_offsets = NULL;
    uint32_t *relocation_name_offsets = NULL;
    uint32_t *symbol_name_offsets = NULL;
    MiniLdBuffer strtab = {NULL, 0U, 0U};
    MiniLdBuffer shstrtab = {NULL, 0U, 0U};
    Elf64_Shdr *headers = NULL;
    Elf64_Sym *symbols = NULL;
    unsigned char *image = NULL;
    size_t cursor;
    size_t section_header_offset;
    size_t total_size;
    FILE *file = NULL;
    bool ok = false;

    (void)assign_symbol_indices(state, &local_count);
    for (i = 0U; i < state->section_count; ++i) {
        if (relocation_count_for_section(state, i) != 0U) {
            ++relocation_section_count;
        }
    }
    if (state->section_count > SIZE_MAX - relocation_section_count - 3U) {
        goto oom;
    }
    output_section_count =
        state->section_count + relocation_section_count + 3U;
    if (output_section_count + 1U > UINT16_MAX) {
        fprintf(state->diagnostics, "minic-ld: too-many-output-sections\n");
        goto done;
    }

    relocation_section_indices =
        malloc((state->section_count == 0U ? 1U : state->section_count) *
               sizeof(*relocation_section_indices));
    section_name_offsets =
        calloc(state->section_count == 0U ? 1U : state->section_count,
               sizeof(*section_name_offsets));
    relocation_name_offsets =
        calloc(state->section_count == 0U ? 1U : state->section_count,
               sizeof(*relocation_name_offsets));
    symbol_name_offsets =
        calloc(state->symbol_count == 0U ? 1U : state->symbol_count,
               sizeof(*symbol_name_offsets));
    headers = calloc(output_section_count + 1U, sizeof(*headers));
    symbols = calloc(state->symbol_count + 1U, sizeof(*symbols));
    if (relocation_section_indices == NULL || section_name_offsets == NULL ||
        relocation_name_offsets == NULL || symbol_name_offsets == NULL ||
        headers == NULL || symbols == NULL) {
        goto oom;
    }
    for (i = 0U; i < state->section_count; ++i) {
        relocation_section_indices[i] = SIZE_MAX;
    }
    if (!buffer_append_zero(&strtab, 1U) ||
        !buffer_append_zero(&shstrtab, 1U)) {
        goto oom;
    }

    for (i = 0U; i < state->section_count; ++i) {
        if (!buffer_append_string(&shstrtab,
                                  state->sections[i].name,
                                  &section_name_offsets[i])) {
            goto oom;
        }
    }
    {
        size_t ordinal = 0U;
        for (i = 0U; i < state->section_count; ++i) {
            if (relocation_count_for_section(state, i) != 0U) {
                char name[512];
                int written = snprintf(name,
                                       sizeof(name),
                                       ".rela%s",
                                       state->sections[i].name);
                if (written < 0 || (size_t)written >= sizeof(name) ||
                    !buffer_append_string(&shstrtab,
                                          name,
                                          &relocation_name_offsets[i])) {
                    goto oom;
                }
                relocation_section_indices[i] =
                    1U + state->section_count + ordinal++;
            }
        }
    }

    for (i = 0U; i < state->symbol_count; ++i) {
        if (state->symbols[i].name[0] != '\0' &&
            !buffer_append_string(&strtab,
                                  state->symbols[i].name,
                                  &symbol_name_offsets[i])) {
            goto oom;
        }
    }

    symtab_index = 1U + state->section_count + relocation_section_count;
    strtab_index = symtab_index + 1U;
    shstrtab_index = symtab_index + 2U;

    {
        uint32_t symtab_name;
        uint32_t strtab_name;
        uint32_t shstrtab_name;

        if (!buffer_append_string(&shstrtab, ".symtab", &symtab_name) ||
            !buffer_append_string(&shstrtab, ".strtab", &strtab_name) ||
            !buffer_append_string(&shstrtab, ".shstrtab", &shstrtab_name)) {
            goto oom;
        }
        headers[symtab_index].sh_name = symtab_name;
        headers[strtab_index].sh_name = strtab_name;
        headers[shstrtab_index].sh_name = shstrtab_name;
    }

    for (i = 0U; i < state->symbol_count; ++i) {
        MiniLdSymbol *input = &state->symbols[i];
        Elf64_Sym *output = &symbols[input->final_index];

        output->st_name = symbol_name_offsets[i];
        output->st_info = input->info;
        output->st_other = input->other;
        output->st_value = input->value;
        output->st_size = input->size;
        if (input->section == MINILD_SECTION_UNDEF) {
            output->st_shndx = SHN_UNDEF;
        } else if (input->section == MINILD_SECTION_ABS) {
            output->st_shndx = SHN_ABS;
        } else if (input->section == MINILD_SECTION_COMMON) {
            output->st_shndx = SHN_COMMON;
        } else {
            output->st_shndx = (Elf64_Section)((size_t)input->section + 1U);
        }
    }

    cursor = sizeof(Elf64_Ehdr);
    for (i = 0U; i < state->section_count; ++i) {
        MiniLdSection *section = &state->sections[i];
        size_t aligned;

        if (!align_up_size(cursor, section->align, &aligned)) {
            goto oom;
        }
        cursor = aligned;
        headers[i + 1U].sh_name = section_name_offsets[i];
        headers[i + 1U].sh_type = section->type;
        headers[i + 1U].sh_flags = section->flags;
        headers[i + 1U].sh_offset = cursor;
        headers[i + 1U].sh_size = section->size;
        headers[i + 1U].sh_addralign = section->align;
        headers[i + 1U].sh_entsize = section->entsize;
        if (section->type != SHT_NOBITS &&
            !add_size(cursor, section->size, &cursor)) {
            goto oom;
        }
    }

    for (i = 0U; i < state->section_count; ++i) {
        size_t count = relocation_count_for_section(state, i);
        size_t index;
        size_t aligned;

        if (count == 0U) {
            continue;
        }
        index = relocation_section_indices[i];
        if (!align_up_size(cursor, 8U, &aligned)) {
            goto oom;
        }
        cursor = aligned;
        headers[index].sh_name = relocation_name_offsets[i];
        headers[index].sh_type = SHT_RELA;
        headers[index].sh_flags = SHF_INFO_LINK;
        headers[index].sh_offset = cursor;
        headers[index].sh_size = count * sizeof(Elf64_Rela);
        headers[index].sh_link = (Elf64_Word)symtab_index;
        headers[index].sh_info = (Elf64_Word)(i + 1U);
        headers[index].sh_addralign = 8U;
        headers[index].sh_entsize = sizeof(Elf64_Rela);
        if (!add_size(cursor, headers[index].sh_size, &cursor)) {
            goto oom;
        }
    }

    {
        size_t aligned;
        if (!align_up_size(cursor, 8U, &aligned)) {
            goto oom;
        }
        cursor = aligned;
    }
    headers[symtab_index].sh_type = SHT_SYMTAB;
    headers[symtab_index].sh_offset = cursor;
    headers[symtab_index].sh_size =
        (state->symbol_count + 1U) * sizeof(Elf64_Sym);
    headers[symtab_index].sh_link = (Elf64_Word)strtab_index;
    headers[symtab_index].sh_info = (Elf64_Word)local_count;
    headers[symtab_index].sh_addralign = 8U;
    headers[symtab_index].sh_entsize = sizeof(Elf64_Sym);
    if (!add_size(cursor, headers[symtab_index].sh_size, &cursor)) {
        goto oom;
    }

    headers[strtab_index].sh_type = SHT_STRTAB;
    headers[strtab_index].sh_offset = cursor;
    headers[strtab_index].sh_size = strtab.size;
    headers[strtab_index].sh_addralign = 1U;
    if (!add_size(cursor, strtab.size, &cursor)) {
        goto oom;
    }

    headers[shstrtab_index].sh_type = SHT_STRTAB;
    headers[shstrtab_index].sh_offset = cursor;
    headers[shstrtab_index].sh_size = shstrtab.size;
    headers[shstrtab_index].sh_addralign = 1U;
    if (!add_size(cursor, shstrtab.size, &cursor)) {
        goto oom;
    }

    if (!align_up_size(cursor, 8U, &section_header_offset) ||
        output_section_count + 1U >
            (SIZE_MAX - section_header_offset) / sizeof(Elf64_Shdr)) {
        goto oom;
    }
    total_size =
        section_header_offset +
        (output_section_count + 1U) * sizeof(Elf64_Shdr);
    image = calloc(total_size == 0U ? 1U : total_size, 1U);
    if (image == NULL) {
        goto oom;
    }

    {
        Elf64_Ehdr output;
        memset(&output, 0, sizeof(output));
        memcpy(output.e_ident, ELFMAG, SELFMAG);
        output.e_ident[EI_CLASS] = ELFCLASS64;
        output.e_ident[EI_DATA] = ELFDATA2LSB;
        output.e_ident[EI_VERSION] = EV_CURRENT;
        output.e_ident[EI_OSABI] = ELFOSABI_NONE;
        output.e_type = ET_REL;
        output.e_machine = EM_RISCV;
        output.e_version = EV_CURRENT;
        output.e_flags = state->elf_flags;
        output.e_ehsize = sizeof(Elf64_Ehdr);
        output.e_shentsize = sizeof(Elf64_Shdr);
        output.e_shnum = (Elf64_Half)(output_section_count + 1U);
        output.e_shstrndx = (Elf64_Half)shstrtab_index;
        output.e_shoff = section_header_offset;
        memcpy(image, &output, sizeof(output));
    }

    for (i = 0U; i < state->section_count; ++i) {
        MiniLdSection *section = &state->sections[i];
        if (section->type != SHT_NOBITS && section->size != 0U) {
            memcpy(image + headers[i + 1U].sh_offset,
                   section->data,
                   section->size);
        }
    }

    for (i = 0U; i < state->section_count; ++i) {
        size_t index = relocation_section_indices[i];
        size_t write_index = 0U;
        size_t j;

        if (index == SIZE_MAX) {
            continue;
        }
        for (j = 0U; j < state->reloc_count; ++j) {
            MiniLdReloc *input = &state->relocs[j];
            Elf64_Rela output;
            size_t symbol_index = 0U;

            if (input->section != i) {
                continue;
            }
            if (input->symbol != SIZE_MAX) {
                symbol_index = state->symbols[input->symbol].final_index;
            }
            output.r_offset = input->offset;
            output.r_info = ELF64_R_INFO(symbol_index, input->type);
            output.r_addend = input->addend;
            memcpy(image + headers[index].sh_offset +
                       write_index * sizeof(output),
                   &output,
                   sizeof(output));
            ++write_index;
        }
    }

    memcpy(image + headers[symtab_index].sh_offset,
           symbols,
           headers[symtab_index].sh_size);
    memcpy(image + headers[strtab_index].sh_offset,
           strtab.data,
           strtab.size);
    memcpy(image + headers[shstrtab_index].sh_offset,
           shstrtab.data,
           shstrtab.size);
    memcpy(image + section_header_offset,
           headers,
           (output_section_count + 1U) * sizeof(*headers));

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: cannot-create:%s:%s\n",
                path,
                strerror(errno));
        goto done;
    }
    if (fwrite(image, 1U, total_size, file) != total_size || fflush(file) != 0) {
        fprintf(state->diagnostics, "minic-ld: write-error:%s\n", path);
        goto done;
    }
    ok = true;
    goto done;

oom:
    fprintf(state->diagnostics, "minic-ld: out-of-memory:output\n");

done:
    if (file != NULL && fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        (void)remove(path);
    }
    free(image);
    free(headers);
    free(symbols);
    free(relocation_section_indices);
    free(section_name_offsets);
    free(relocation_name_offsets);
    free(symbol_name_offsets);
    free(strtab.data);
    free(shstrtab.data);
    return ok;
}

int minild_link_relocatable_elf64_riscv(const char *output_path,
                                        const char *const *input_paths,
                                        size_t input_count,
                                        FILE *diagnostics) {
    MiniLdState state;
    size_t i;
    bool ok = false;

    if (output_path == NULL || input_paths == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    state.diagnostics = diagnostics;

    for (i = 0U; i < input_count; ++i) {
        if (!process_input(&state, input_paths[i])) {
            goto done;
        }
    }
    if (!state.have_input || !write_output(&state, output_path)) {
        goto done;
    }
    ok = true;

done:
    state_destroy(&state);
    return ok ? 0 : 1;
}
