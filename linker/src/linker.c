#include "minild.h"

#include <elf.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
    size_t relocation_count;
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

typedef struct MiniLdIndexSlot {
    uint64_t hash;
    size_t index_plus_one;
} MiniLdIndexSlot;

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
    MiniLdIndexSlot *section_index;
    size_t section_index_capacity;
    MiniLdIndexSlot *global_index;
    size_t global_index_capacity;
    size_t global_index_count;
    uint32_t elf_flags;
    bool have_input;
    FILE *diagnostics;
} MiniLdState;

typedef struct MiniLdSectionMap {
    size_t output_section;
    uint64_t base;
    bool mapped;
} MiniLdSectionMap;

typedef struct MiniLdArchiveMembers {
    char **paths;
    size_t count;
    size_t capacity;
} MiniLdArchiveMembers;

typedef struct MiniLdEmbeddedMember {
    char *name;
    size_t data_offset;
    size_t data_size;
} MiniLdEmbeddedMember;

typedef struct MiniLdEmbeddedArchive {
    unsigned char *data;
    size_t size;
    MiniLdEmbeddedMember *members;
    size_t count;
    size_t capacity;
} MiniLdEmbeddedArchive;

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
    free(state->section_index);
    free(state->global_index);
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


static uint64_t hash_text(const char *text) {
    uint64_t hash = UINT64_C(14695981039346656037);

    while (*text != '\0') {
        hash ^= (unsigned char)*text++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void insert_index_slot(MiniLdIndexSlot *slots,
                              size_t capacity,
                              uint64_t hash,
                              size_t index) {
    size_t position = (size_t)hash & (capacity - 1U);

    while (slots[position].index_plus_one != 0U) {
        position = (position + 1U) & (capacity - 1U);
    }
    slots[position].hash = hash;
    slots[position].index_plus_one = index + 1U;
}

static bool rebuild_section_index(MiniLdState *state, size_t capacity) {
    MiniLdIndexSlot *slots;
    size_t i;

    slots = calloc(capacity, sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (i = 0U; i < state->section_count; ++i) {
        uint64_t hash = hash_text(state->sections[i].name);
        insert_index_slot(slots, capacity, hash, i);
    }
    free(state->section_index);
    state->section_index = slots;
    state->section_index_capacity = capacity;
    return true;
}

static bool ensure_section_index_insert(MiniLdState *state) {
    size_t capacity = state->section_index_capacity;

    if (capacity != 0U &&
        (state->section_count + 1U) * 10U < capacity * 7U) {
        return true;
    }
    if (capacity == 0U) {
        capacity = 128U;
    } else {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    return rebuild_section_index(state, capacity);
}

static size_t find_section_index(const MiniLdState *state,
                                 const char *name,
                                 uint64_t hash) {
    size_t position;

    if (state->section_index_capacity == 0U) {
        return SIZE_MAX;
    }
    position = (size_t)hash & (state->section_index_capacity - 1U);
    while (state->section_index[position].index_plus_one != 0U) {
        const MiniLdIndexSlot *slot = &state->section_index[position];
        size_t index = slot->index_plus_one - 1U;

        if (slot->hash == hash &&
            strcmp(state->sections[index].name, name) == 0) {
            return index;
        }
        position = (position + 1U) & (state->section_index_capacity - 1U);
    }
    return SIZE_MAX;
}

static bool rebuild_global_index(MiniLdState *state, size_t capacity) {
    MiniLdIndexSlot *slots;
    size_t i;

    slots = calloc(capacity, sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (i = 0U; i < state->symbol_count; ++i) {
        if (ELF64_ST_BIND(state->symbols[i].info) != STB_LOCAL) {
            uint64_t hash = hash_text(state->symbols[i].name);
            insert_index_slot(slots, capacity, hash, i);
        }
    }
    free(state->global_index);
    state->global_index = slots;
    state->global_index_capacity = capacity;
    return true;
}

static bool ensure_global_index_insert(MiniLdState *state) {
    size_t capacity = state->global_index_capacity;

    if (capacity != 0U &&
        (state->global_index_count + 1U) * 10U < capacity * 7U) {
        return true;
    }
    if (capacity == 0U) {
        capacity = 256U;
    } else {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    return rebuild_global_index(state, capacity);
}

static bool find_or_add_section(MiniLdState *state,
                                const char *name,
                                uint32_t type,
                                uint64_t flags,
                                uint64_t align,
                                uint64_t entsize,
                                size_t *section_out) {
    uint64_t hash = hash_text(name);
    size_t index = find_section_index(state, name, hash);
    MiniLdSection *section;

    if (index != SIZE_MAX) {
        section = &state->sections[index];
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
        *section_out = index;
        return true;
    }
    if (!ensure_section_capacity(state) ||
        !ensure_section_index_insert(state)) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:sections\n");
        return false;
    }
    index = state->section_count;
    section = &state->sections[index];
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
    ++state->section_count;
    insert_index_slot(state->section_index,
                      state->section_index_capacity,
                      hash,
                      index);
    *section_out = index;
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
    uint64_t hash = hash_text(name);
    size_t position;

    if (state->global_index_capacity == 0U) {
        return SIZE_MAX;
    }
    position = (size_t)hash & (state->global_index_capacity - 1U);
    while (state->global_index[position].index_plus_one != 0U) {
        const MiniLdIndexSlot *slot = &state->global_index[position];
        size_t index = slot->index_plus_one - 1U;

        if (slot->hash == hash &&
            strcmp(state->symbols[index].name, name) == 0) {
            return index;
        }
        position = (position + 1U) & (state->global_index_capacity - 1U);
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
        size_t index;
        uint64_t hash = hash_text(name);

        if (!ensure_global_index_insert(state) ||
            !add_local_symbol(state,
                              name,
                              section,
                              value,
                              size,
                              info,
                              other,
                              &index)) {
            return false;
        }
        insert_index_slot(state->global_index,
                          state->global_index_capacity,
                          hash,
                          index);
        ++state->global_index_count;
        *symbol_out = index;
        return true;
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
    ++state->sections[section].relocation_count;
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

static bool process_input_data(MiniLdState *state,
                               const unsigned char *data,
                               size_t size,
                               const char *path) {
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
    return ok;
}

static bool process_input(MiniLdState *state, const char *path) {
    unsigned char *data = NULL;
    size_t size = 0U;
    bool ok;

    if (!read_file(path, &data, &size, state->diagnostics)) {
        return false;
    }
    ok = process_input_data(state, data, size, path);
    free(data);
    return ok;
}

static char *archive_directory(const char *path) {
    const char *slash = strrchr(path, '/');
    size_t length;
    char *result;

    if (slash == NULL) {
        return minild_strdup(".");
    }
    if (slash == path) {
        return minild_strdup("/");
    }
    length = (size_t)(slash - path);
    result = malloc(length + 1U);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, path, length);
    result[length] = '\0';
    return result;
}

static char *join_path(const char *directory, const char *name) {
    size_t directory_size;
    size_t name_size;
    bool needs_slash;
    char *result;

    if (name[0] == '/') {
        return minild_strdup(name);
    }
    directory_size = strlen(directory);
    name_size = strlen(name);
    needs_slash = directory_size != 0U && directory[directory_size - 1U] != '/';
    if (directory_size > SIZE_MAX - name_size - (needs_slash ? 2U : 1U)) {
        return NULL;
    }
    result = malloc(directory_size + (needs_slash ? 1U : 0U) + name_size + 1U);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, directory, directory_size);
    if (needs_slash) {
        result[directory_size++] = '/';
    }
    memcpy(result + directory_size, name, name_size + 1U);
    return result;
}

static void archive_members_destroy(MiniLdArchiveMembers *members) {
    size_t i;

    for (i = 0U; i < members->count; ++i) {
        free(members->paths[i]);
    }
    free(members->paths);
    members->paths = NULL;
    members->count = 0U;
    members->capacity = 0U;
}

static bool archive_members_append(MiniLdArchiveMembers *members, char *path) {
    char **next;
    size_t capacity;

    if (members->count == members->capacity) {
        capacity = members->capacity == 0U ? 64U : members->capacity * 2U;
        if (capacity < members->capacity ||
            capacity > SIZE_MAX / sizeof(*members->paths)) {
            return false;
        }
        next = realloc(members->paths, capacity * sizeof(*members->paths));
        if (next == NULL) {
            return false;
        }
        members->paths = next;
        members->capacity = capacity;
    }
    members->paths[members->count++] = path;
    return true;
}

static bool parse_archive_decimal(const unsigned char *field,
                                  size_t width,
                                  uint64_t *value_out) {
    uint64_t value = 0U;
    size_t i = 0U;
    bool saw_digit = false;

    while (i < width && field[i] == ' ') {
        ++i;
    }
    for (; i < width && field[i] != ' '; ++i) {
        unsigned digit;

        if (field[i] < '0' || field[i] > '9') {
            return false;
        }
        digit = (unsigned)(field[i] - '0');
        if (value > (UINT64_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
        saw_digit = true;
    }
    while (i < width) {
        if (field[i] != ' ') {
            return false;
        }
        ++i;
    }
    if (!saw_digit) {
        value = 0U;
    }
    *value_out = value;
    return true;
}

static bool parse_archive_name_field(const unsigned char *header,
                                     char field[17]) {
    size_t length = 16U;

    memcpy(field, header, 16U);
    field[16] = '\0';
    while (length != 0U && field[length - 1U] == ' ') {
        field[--length] = '\0';
    }
    return length != 0U;
}

static bool decode_archive_member_name(const unsigned char *long_names,
                                       size_t long_names_size,
                                       const char field[17],
                                       char **name_out) {
    char *name;
    size_t length;

    if (field[0] == '/' && field[1] >= '0' && field[1] <= '9') {
        uint64_t offset = 0U;
        const char *p = field + 1;
        size_t end;

        while (*p >= '0' && *p <= '9') {
            unsigned digit = (unsigned)(*p - '0');
            if (offset > (UINT64_MAX - digit) / 10U) {
                return false;
            }
            offset = offset * 10U + digit;
            ++p;
        }
        while (*p == ' ') {
            ++p;
        }
        if (*p == '/') {
            ++p;
        }
        while (*p == ' ') {
            ++p;
        }
        if (*p != '\0' || offset > SIZE_MAX ||
            (size_t)offset >= long_names_size) {
            return false;
        }
        end = (size_t)offset;
        while (end < long_names_size && long_names[end] != '\n') {
            ++end;
        }
        if (end == long_names_size || end == (size_t)offset ||
            long_names[end - 1U] != '/') {
            return false;
        }
        --end;
        length = end - (size_t)offset;
        name = malloc(length + 1U);
        if (name == NULL) {
            return false;
        }
        memcpy(name, long_names + (size_t)offset, length);
        name[length] = '\0';
        *name_out = name;
        return true;
    }

    if (strncmp(field, "#1/", 3U) == 0) {
        return false;
    }
    {
        const char *slash = strchr(field, '/');
        length = slash == NULL ? strlen(field) : (size_t)(slash - field);
    }
    name = malloc(length + 1U);
    if (name == NULL) {
        return false;
    }
    memcpy(name, field, length);
    name[length] = '\0';
    *name_out = name;
    return true;
}

static bool read_thin_archive_members(const char *archive_path,
                                      MiniLdArchiveMembers *members,
                                      FILE *diagnostics) {
    unsigned char *data = NULL;
    size_t size = 0U;
    size_t cursor = 8U;
    const unsigned char *long_names = NULL;
    size_t long_names_size = 0U;
    char *directory = NULL;
    bool ok = false;

    if (!read_file(archive_path, &data, &size, diagnostics)) {
        return false;
    }
    if (size < 8U || memcmp(data, "!<thin>\n", 8U) != 0) {
        fprintf(diagnostics,
                "minic-ld: A1 archive input must be GNU thin archive:%s\n",
                archive_path);
        goto done;
    }
    directory = archive_directory(archive_path);
    if (directory == NULL) {
        fprintf(diagnostics, "minic-ld: out-of-memory:archive-directory\n");
        goto done;
    }

    while (cursor < size) {
        const unsigned char *header;
        char field[17];
        uint64_t member_size;
        bool embedded;

        if (!range_ok(cursor, 60U, size)) {
            fprintf(diagnostics, "minic-ld: truncated-archive-header:%s\n", archive_path);
            goto done;
        }
        header = data + cursor;
        if (header[58] != (unsigned char)0x60 || header[59] != '\n' ||
            !parse_archive_name_field(header, field) ||
            !parse_archive_decimal(header + 48U, 10U, &member_size) ||
            member_size > SIZE_MAX) {
            fprintf(diagnostics, "minic-ld: invalid-archive-header:%s\n", archive_path);
            goto done;
        }
        cursor += 60U;
        embedded = strcmp(field, "/") == 0 || strcmp(field, "//") == 0 ||
                   strcmp(field, "/SYM64/") == 0;

        if (embedded && !range_ok(cursor, (size_t)member_size, size)) {
            fprintf(diagnostics, "minic-ld: truncated-archive-special:%s\n", archive_path);
            goto done;
        }

        if (strcmp(field, "//") == 0) {
            long_names = data + cursor;
            long_names_size = (size_t)member_size;
        } else if (strcmp(field, "/") != 0 && strcmp(field, "/SYM64/") != 0) {
            char *name = NULL;
            char *path = NULL;

            if (!decode_archive_member_name(long_names,
                                            long_names_size,
                                            field,
                                            &name)) {
                fprintf(diagnostics,
                        "minic-ld: unsupported-archive-member-name:%s:%s\n",
                        archive_path,
                        field);
                goto done;
            }
            path = join_path(directory, name);
            free(name);
            if (path == NULL || !archive_members_append(members, path)) {
                free(path);
                fprintf(diagnostics, "minic-ld: out-of-memory:archive-members\n");
                goto done;
            }
        }

        if (embedded) {
            cursor += (size_t)member_size;
            if ((member_size & 1U) != 0U) {
                if (!range_ok(cursor, 1U, size)) {
                    fprintf(diagnostics,
                            "minic-ld: truncated-archive-padding:%s\n",
                            archive_path);
                    goto done;
                }
                ++cursor;
            }
        }
    }

    ok = true;

done:
    free(directory);
    free(data);
    return ok;
}

static bool state_has_unresolved_nonweak(const MiniLdState *state,
                                         const char *name) {
    size_t index = find_global_symbol(state, name);

    if (index == SIZE_MAX) {
        return false;
    }
    return !symbol_is_defined(&state->symbols[index]) &&
           ELF64_ST_BIND(state->symbols[index].info) != STB_WEAK;
}

static bool object_data_defines_needed_symbol(MiniLdState *state,
                                              const unsigned char *data,
                                              size_t size,
                                              const char *path,
                                              bool *needed_out) {
    Elf64_Ehdr ehdr;
    const Elf64_Shdr *sections;
    const Elf64_Shdr *symtab = NULL;
    const Elf64_Shdr *strtab = NULL;
    size_t i;
    bool ok = false;

    *needed_out = false;
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
        ehdr.e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr.e_shoff > SIZE_MAX ||
        !range_ok((size_t)ehdr.e_shoff,
                  (size_t)ehdr.e_shnum * sizeof(Elf64_Shdr),
                  size)) {
        fprintf(state->diagnostics, "minic-ld: unsupported-archive-object:%s\n", path);
        goto done;
    }
    sections = (const Elf64_Shdr *)(const void *)(data + (size_t)ehdr.e_shoff);
    for (i = 1U; i < ehdr.e_shnum; ++i) {
        if (sections[i].sh_type == SHT_SYMTAB) {
            symtab = &sections[i];
            break;
        }
    }
    if (symtab == NULL) {
        /*
         * A regular archive may contain ET_REL members with no symbol table.
         * Such a member cannot satisfy a named unresolved global, so lazy
         * archive selection must simply leave it unselected.
         */
        ok = true;
        goto done;
    }
    if (symtab->sh_link >= ehdr.e_shnum ||
        symtab->sh_entsize != sizeof(Elf64_Sym)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-archive-object-symtab:%s\n",
                path);
        goto done;
    }
    strtab = &sections[symtab->sh_link];
    if (strtab->sh_type != SHT_STRTAB ||
        symtab->sh_offset > SIZE_MAX || symtab->sh_size > SIZE_MAX ||
        strtab->sh_offset > SIZE_MAX || strtab->sh_size > SIZE_MAX ||
        !range_ok((size_t)symtab->sh_offset, (size_t)symtab->sh_size, size) ||
        !range_ok((size_t)strtab->sh_offset, (size_t)strtab->sh_size, size)) {
        fprintf(state->diagnostics, "minic-ld: invalid-archive-object-symtab:%s\n", path);
        goto done;
    }

    {
        size_t count = (size_t)(symtab->sh_size / symtab->sh_entsize);
        for (i = 1U; i < count; ++i) {
            Elf64_Sym symbol;
            size_t offset = (size_t)symtab->sh_offset +
                            i * (size_t)symtab->sh_entsize;
            const char *name;
            unsigned bind;

            if (!range_ok(offset, sizeof(symbol), size)) {
                fprintf(state->diagnostics,
                        "minic-ld: truncated-archive-object-symbol:%s\n",
                        path);
                goto done;
            }
            memcpy(&symbol, data + offset, sizeof(symbol));
            bind = ELF64_ST_BIND(symbol.st_info);
            if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
                symbol.st_shndx == SHN_UNDEF || symbol.st_name == 0U ||
                symbol.st_name >= strtab->sh_size) {
                continue;
            }
            name = (const char *)data + (size_t)strtab->sh_offset + symbol.st_name;
            if (memchr(name,
                       '\0',
                       (size_t)strtab->sh_size - symbol.st_name) == NULL) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-archive-object-symbol-name:%s\n",
                        path);
                goto done;
            }
            if (state_has_unresolved_nonweak(state, name)) {
                *needed_out = true;
                break;
            }
        }
    }
    ok = true;

done:
    return ok;
}

static bool object_defines_needed_symbol(MiniLdState *state,
                                         const char *path,
                                         bool *needed_out) {
    unsigned char *data = NULL;
    size_t size = 0U;
    bool ok;

    if (!read_file(path, &data, &size, state->diagnostics)) {
        return false;
    }
    ok = object_data_defines_needed_symbol(state,
                                           data,
                                           size,
                                           path,
                                           needed_out);
    free(data);
    return ok;
}


static void embedded_archive_destroy(MiniLdEmbeddedArchive *archive) {
    size_t i;

    for (i = 0U; i < archive->count; ++i) {
        free(archive->members[i].name);
    }
    free(archive->members);
    free(archive->data);
    archive->members = NULL;
    archive->data = NULL;
    archive->count = 0U;
    archive->capacity = 0U;
    archive->size = 0U;
}

static bool embedded_archive_append(MiniLdEmbeddedArchive *archive,
                                    char *name,
                                    size_t data_offset,
                                    size_t data_size) {
    MiniLdEmbeddedMember *next;
    size_t capacity;

    if (archive->count == archive->capacity) {
        capacity = archive->capacity == 0U ? 64U : archive->capacity * 2U;
        if (capacity < archive->capacity ||
            capacity > SIZE_MAX / sizeof(*archive->members)) {
            return false;
        }
        next = realloc(archive->members, capacity * sizeof(*archive->members));
        if (next == NULL) {
            return false;
        }
        archive->members = next;
        archive->capacity = capacity;
    }
    archive->members[archive->count].name = name;
    archive->members[archive->count].data_offset = data_offset;
    archive->members[archive->count].data_size = data_size;
    ++archive->count;
    return true;
}

static bool read_regular_archive(const char *archive_path,
                                 MiniLdEmbeddedArchive *archive,
                                 FILE *diagnostics) {
    size_t cursor = 8U;
    const unsigned char *long_names = NULL;
    size_t long_names_size = 0U;
    bool ok = false;

    if (!read_file(archive_path, &archive->data, &archive->size, diagnostics)) {
        return false;
    }
    if (archive->size < 8U ||
        memcmp(archive->data, "!<arch>\n", 8U) != 0) {
        fprintf(diagnostics,
                "minic-ld: A3 archive input must be GNU regular archive:%s\n",
                archive_path);
        goto done;
    }

    while (cursor < archive->size) {
        const unsigned char *header;
        char field[17];
        uint64_t member_size;
        size_t payload_offset;

        if (!range_ok(cursor, 60U, archive->size)) {
            fprintf(diagnostics,
                    "minic-ld: truncated-regular-archive-header:%s\n",
                    archive_path);
            goto done;
        }
        header = archive->data + cursor;
        if (header[58] != (unsigned char)0x60 || header[59] != '\n' ||
            !parse_archive_name_field(header, field) ||
            !parse_archive_decimal(header + 48U, 10U, &member_size) ||
            member_size > SIZE_MAX) {
            fprintf(diagnostics,
                    "minic-ld: invalid-regular-archive-header:%s\n",
                    archive_path);
            goto done;
        }
        cursor += 60U;
        payload_offset = cursor;
        if (!range_ok(payload_offset, (size_t)member_size, archive->size)) {
            fprintf(diagnostics,
                    "minic-ld: truncated-regular-archive-member:%s:%s\n",
                    archive_path,
                    field);
            goto done;
        }

        if (strcmp(field, "//") == 0) {
            long_names = archive->data + payload_offset;
            long_names_size = (size_t)member_size;
        } else if (strcmp(field, "/") != 0 &&
                   strcmp(field, "/SYM64/") != 0) {
            char *name = NULL;

            if (!decode_archive_member_name(long_names,
                                            long_names_size,
                                            field,
                                            &name)) {
                fprintf(diagnostics,
                        "minic-ld: unsupported-regular-member-name:%s:%s\n",
                        archive_path,
                        field);
                goto done;
            }
            if (!embedded_archive_append(archive,
                                         name,
                                         payload_offset,
                                         (size_t)member_size)) {
                free(name);
                fprintf(diagnostics,
                        "minic-ld: out-of-memory:regular-archive-members\n");
                goto done;
            }
        }

        cursor += (size_t)member_size;
        if ((member_size & 1U) != 0U) {
            if (!range_ok(cursor, 1U, archive->size)) {
                fprintf(diagnostics,
                        "minic-ld: truncated-regular-archive-padding:%s\n",
                        archive_path);
                goto done;
            }
            ++cursor;
        }
    }

    ok = true;

done:
    if (!ok) {
        embedded_archive_destroy(archive);
    }
    return ok;
}

static int archive_file_kind(const char *path, FILE *diagnostics) {
    FILE *file;
    unsigned char magic[8];
    size_t got;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(diagnostics,
                "minic-ld: cannot-open:%s:%s\n",
                path,
                strerror(errno));
        return -1;
    }
    got = fread(magic, 1U, sizeof(magic), file);
    if (fclose(file) != 0) {
        fprintf(diagnostics, "minic-ld: cannot-close:%s\n", path);
        return -1;
    }
    if (got != sizeof(magic)) {
        return 0;
    }
    if (memcmp(magic, "!<thin>\n", 8U) == 0) {
        return 1;
    }
    if (memcmp(magic, "!<arch>\n", 8U) == 0) {
        return 2;
    }
    return 0;
}

static bool process_regular_whole_archive(MiniLdState *state,
                                          const char *path) {
    MiniLdEmbeddedArchive archive = {NULL, 0U, NULL, 0U, 0U};
    size_t i;
    bool ok = false;

    if (!read_regular_archive(path, &archive, state->diagnostics)) {
        goto done;
    }
    for (i = 0U; i < archive.count; ++i) {
        MiniLdEmbeddedMember *member = &archive.members[i];

        if (!process_input_data(state,
                                archive.data + member->data_offset,
                                member->data_size,
                                member->name)) {
            goto done;
        }
    }
    ok = true;

done:
    embedded_archive_destroy(&archive);
    return ok;
}

static bool process_regular_group_archive(MiniLdState *state,
                                          const char *path) {
    MiniLdEmbeddedArchive archive = {NULL, 0U, NULL, 0U, 0U};
    bool *selected = NULL;
    bool changed;
    size_t i;
    bool ok = false;

    if (!read_regular_archive(path, &archive, state->diagnostics)) {
        goto done;
    }
    selected = calloc(archive.count == 0U ? 1U : archive.count,
                      sizeof(*selected));
    if (selected == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:regular-archive-selection\n");
        goto done;
    }

    do {
        changed = false;
        for (i = 0U; i < archive.count; ++i) {
            MiniLdEmbeddedMember *member = &archive.members[i];
            bool needed = false;

            if (selected[i]) {
                continue;
            }
            if (!object_data_defines_needed_symbol(
                    state,
                    archive.data + member->data_offset,
                    member->data_size,
                    member->name,
                    &needed)) {
                goto done;
            }
            if (!needed) {
                continue;
            }
            if (!process_input_data(state,
                                    archive.data + member->data_offset,
                                    member->data_size,
                                    member->name)) {
                goto done;
            }
            selected[i] = true;
            changed = true;
        }
    } while (changed);

    ok = true;

done:
    free(selected);
    embedded_archive_destroy(&archive);
    return ok;
}

static bool process_whole_archive(MiniLdState *state, const char *path) {
    MiniLdArchiveMembers members = {NULL, 0U, 0U};
    int kind = archive_file_kind(path, state->diagnostics);
    size_t i;
    bool ok = false;

    if (kind < 0) {
        return false;
    }
    if (kind == 2) {
        return process_regular_whole_archive(state, path);
    }
    if (kind != 1) {
        fprintf(state->diagnostics, "minic-ld: expected-archive:%s\n", path);
        return false;
    }
    if (!read_thin_archive_members(path, &members, state->diagnostics)) {
        goto done;
    }
    for (i = 0U; i < members.count; ++i) {
        if (!process_input(state, members.paths[i])) {
            goto done;
        }
    }
    ok = true;

done:
    archive_members_destroy(&members);
    return ok;
}

static bool process_group_archive(MiniLdState *state, const char *path) {
    MiniLdArchiveMembers members = {NULL, 0U, 0U};
    int kind = archive_file_kind(path, state->diagnostics);
    bool *selected = NULL;
    bool changed;
    size_t i;
    bool ok = false;

    if (kind < 0) {
        return false;
    }
    if (kind == 2) {
        return process_regular_group_archive(state, path);
    }
    if (kind != 1) {
        fprintf(state->diagnostics, "minic-ld: expected-archive:%s\n", path);
        return false;
    }
    if (!read_thin_archive_members(path, &members, state->diagnostics)) {
        goto done;
    }
    selected = calloc(members.count == 0U ? 1U : members.count, sizeof(*selected));
    if (selected == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:archive-selection\n");
        goto done;
    }

    do {
        changed = false;
        for (i = 0U; i < members.count; ++i) {
            bool needed = false;

            if (selected[i]) {
                continue;
            }
            if (!object_defines_needed_symbol(state, members.paths[i], &needed)) {
                goto done;
            }
            if (!needed) {
                continue;
            }
            if (!process_input(state, members.paths[i])) {
                goto done;
            }
            selected[i] = true;
            changed = true;
        }
    } while (changed);

    ok = true;

done:
    free(selected);
    archive_members_destroy(&members);
    return ok;
}

static size_t relocation_count_for_section(const MiniLdState *state,
                                           size_t section) {
    return state->sections[section].relocation_count;
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
    size_t *relocation_write_counts = NULL;
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
    relocation_write_counts =
        calloc(state->section_count == 0U ? 1U : state->section_count,
               sizeof(*relocation_write_counts));
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
    if (relocation_section_indices == NULL || relocation_write_counts == NULL ||
        section_name_offsets == NULL || relocation_name_offsets == NULL ||
        symbol_name_offsets == NULL ||
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

    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *input = &state->relocs[i];
        size_t section = input->section;
        size_t index = relocation_section_indices[section];
        size_t write_index = relocation_write_counts[section]++;
        Elf64_Rela output;
        size_t symbol_index = 0U;

        if (index == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: internal-missing-rela-section:%zu\n",
                    section);
            goto done;
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
    free(relocation_write_counts);
    free(section_name_offsets);
    free(relocation_name_offsets);
    free(symbol_name_offsets);
    free(strtab.data);
    free(shstrtab.data);
    return ok;
}


typedef struct MiniLdPcrelSlot {
    size_t section_plus_one;
    uint64_t offset;
    int64_t delta;
} MiniLdPcrelSlot;

typedef struct MiniLdStaticLayout {
    uint64_t *section_vaddr;
    size_t *section_file_offset;
    size_t rx_file_offset;
    uint64_t rx_vaddr;
    size_t rx_file_size;
    size_t rw_file_offset;
    uint64_t rw_vaddr;
    size_t rw_file_size;
    size_t rw_mem_size;
    bool have_rx;
    bool have_rw;
} MiniLdStaticLayout;

static uint16_t load_u16le(const unsigned char *data) {
    return (uint16_t)((uint16_t)data[0] |
                      ((uint16_t)data[1] << 8U));
}

static void store_u16le(unsigned char *data, uint16_t value) {
    data[0] = (unsigned char)(value & UINT16_C(0xff));
    data[1] = (unsigned char)((value >> 8U) & UINT16_C(0xff));
}

static uint32_t load_u32le(const unsigned char *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void store_u32le(unsigned char *data, uint32_t value) {
    data[0] = (unsigned char)(value & 0xffU);
    data[1] = (unsigned char)((value >> 8U) & 0xffU);
    data[2] = (unsigned char)((value >> 16U) & 0xffU);
    data[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static void store_u64le(unsigned char *data, uint64_t value) {
    size_t i;

    for (i = 0U; i < 8U; ++i) {
        data[i] = (unsigned char)((value >> (i * 8U)) & 0xffU);
    }
}

static int64_t riscv_hi20(int64_t value) {
    return (value + INT64_C(0x800)) >> 12;
}

static int64_t riscv_lo12(int64_t value) {
    int64_t high = riscv_hi20(value);
    return value - (high << 12);
}

static bool static_patch_utype(MiniLdSection *section,
                               uint64_t offset,
                               int64_t imm20,
                               FILE *diagnostics) {
    uint32_t instruction;

    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= UINT32_C(0x00000fff);
    instruction |= ((uint32_t)imm20 & UINT32_C(0x000fffff)) << 12U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_itype(MiniLdSection *section,
                               uint64_t offset,
                               int64_t imm12,
                               FILE *diagnostics) {
    uint32_t instruction;

    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= UINT32_C(0x000fffff);
    instruction |= ((uint32_t)imm12 & UINT32_C(0x00000fff)) << 20U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_stype(MiniLdSection *section,
                               uint64_t offset,
                               int64_t imm12,
                               FILE *diagnostics) {
    uint32_t instruction;
    uint32_t encoded = (uint32_t)imm12 & UINT32_C(0x00000fff);

    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= ~UINT32_C(0xfe000f80);
    instruction |= ((encoded >> 5U) & UINT32_C(0x7f)) << 25U;
    instruction |= (encoded & UINT32_C(0x1f)) << 7U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_branch(MiniLdSection *section,
                                uint64_t offset,
                                int64_t delta,
                                FILE *diagnostics) {
    uint32_t instruction;
    uint32_t encoded;

    if ((delta & 1) != 0 || delta < -4096 || delta > 4094) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_BRANCH-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint32_t)delta & UINT32_C(0x1fff);
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= ~UINT32_C(0xfe000f80);
    instruction |= ((encoded >> 12U) & 1U) << 31U;
    instruction |= ((encoded >> 5U) & UINT32_C(0x3f)) << 25U;
    instruction |= ((encoded >> 1U) & UINT32_C(0x0f)) << 8U;
    instruction |= ((encoded >> 11U) & 1U) << 7U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}


static bool static_patch_rvc_branch(MiniLdSection *section,
                                    uint64_t offset,
                                    int64_t delta,
                                    FILE *diagnostics) {
    uint16_t instruction;
    uint16_t encoded;

    if ((delta & 1) != 0 || delta < -256 || delta > 254) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_RVC_BRANCH-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 2U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint16_t)((uint64_t)delta & UINT64_C(0x1ff));
    instruction = load_u16le(section->data + (size_t)offset);
    instruction &= (uint16_t)~UINT16_C(0x1c7c);
    instruction |= (uint16_t)(((encoded >> 8U) & 1U) << 12U);
    instruction |= (uint16_t)(((encoded >> 3U) & 3U) << 10U);
    instruction |= (uint16_t)(((encoded >> 6U) & 3U) << 5U);
    instruction |= (uint16_t)(((encoded >> 1U) & 3U) << 3U);
    instruction |= (uint16_t)(((encoded >> 5U) & 1U) << 2U);
    store_u16le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_rvc_jump(MiniLdSection *section,
                                  uint64_t offset,
                                  int64_t delta,
                                  FILE *diagnostics) {
    uint16_t instruction;
    uint16_t encoded;

    if ((delta & 1) != 0 || delta < -2048 || delta > 2046) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_RVC_JUMP-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 2U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint16_t)((uint64_t)delta & UINT64_C(0xfff));
    instruction = load_u16le(section->data + (size_t)offset);
    instruction &= (uint16_t)~UINT16_C(0x1ffc);
    instruction |= (uint16_t)(((encoded >> 11U) & 1U) << 12U);
    instruction |= (uint16_t)(((encoded >> 4U) & 1U) << 11U);
    instruction |= (uint16_t)(((encoded >> 8U) & 3U) << 9U);
    instruction |= (uint16_t)(((encoded >> 10U) & 1U) << 8U);
    instruction |= (uint16_t)(((encoded >> 6U) & 1U) << 7U);
    instruction |= (uint16_t)(((encoded >> 7U) & 1U) << 6U);
    instruction |= (uint16_t)(((encoded >> 1U) & 7U) << 3U);
    instruction |= (uint16_t)(((encoded >> 5U) & 1U) << 2U);
    store_u16le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_jal(MiniLdSection *section,
                             uint64_t offset,
                             int64_t delta,
                             FILE *diagnostics) {
    uint32_t instruction;
    uint32_t encoded;

    if ((delta & 1) != 0 || delta < -1048576 || delta > 1048574) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_JAL-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint32_t)delta & UINT32_C(0x1fffff);
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= UINT32_C(0x00000fff);
    instruction |= ((encoded >> 20U) & 1U) << 31U;
    instruction |= ((encoded >> 1U) & UINT32_C(0x03ff)) << 21U;
    instruction |= ((encoded >> 11U) & 1U) << 20U;
    instruction |= ((encoded >> 12U) & UINT32_C(0x00ff)) << 12U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_allocate_common(MiniLdState *state) {
    size_t bss_index = SIZE_MAX;
    size_t i;

    for (i = 0U; i < state->symbol_count; ++i) {
        MiniLdSymbol *symbol = &state->symbols[i];
        size_t aligned;
        uint64_t alignment;

        if (symbol->section != MINILD_SECTION_COMMON) {
            continue;
        }
        if (bss_index == SIZE_MAX &&
            !find_or_add_section(state,
                                 ".bss",
                                 SHT_NOBITS,
                                 SHF_ALLOC | SHF_WRITE,
                                 16U,
                                 0U,
                                 &bss_index)) {
            return false;
        }
        alignment = symbol->value == 0U ? 1U : symbol->value;
        if (!align_up_size(state->sections[bss_index].size,
                           alignment,
                           &aligned) ||
            !section_append_zero(&state->sections[bss_index],
                                 aligned - state->sections[bss_index].size) ||
            !section_append_zero(&state->sections[bss_index],
                                 (size_t)symbol->size)) {
            fprintf(state->diagnostics, "minic-ld: common-allocation-overflow\n");
            return false;
        }
        symbol->section = (int)bss_index;
        symbol->value = aligned;
    }
    return true;
}

static bool static_build_layout(MiniLdState *state,
                                MiniLdStaticLayout *layout) {
    const size_t page = 4096U;
    const size_t header_page = 4096U;
    const uint64_t base_vaddr = UINT64_C(0x10000);
    size_t rx_cursor = 0U;
    size_t rw_file_cursor = 0U;
    size_t rw_mem_cursor = 0U;
    size_t i;

    memset(layout, 0, sizeof(*layout));
    layout->section_vaddr =
        calloc(state->section_count == 0U ? 1U : state->section_count,
               sizeof(*layout->section_vaddr));
    layout->section_file_offset =
        malloc((state->section_count == 0U ? 1U : state->section_count) *
               sizeof(*layout->section_file_offset));
    if (layout->section_vaddr == NULL || layout->section_file_offset == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-layout\n");
        return false;
    }
    for (i = 0U; i < state->section_count; ++i) {
        layout->section_file_offset[i] = SIZE_MAX;
    }

    layout->rx_file_offset = header_page;
    layout->rx_vaddr = base_vaddr;

    for (i = 0U; i < state->section_count; ++i) {
        MiniLdSection *section = &state->sections[i];
        size_t aligned;

        if ((section->flags & SHF_ALLOC) == 0U ||
            (section->flags & SHF_WRITE) != 0U) {
            continue;
        }
        if (section->type == SHT_NOBITS) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-rx-nobits:%s\n",
                    section->name);
            return false;
        }
        if (!align_up_size(rx_cursor, section->align, &aligned)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        rx_cursor = aligned;
        layout->section_vaddr[i] = layout->rx_vaddr + rx_cursor;
        layout->section_file_offset[i] = layout->rx_file_offset + rx_cursor;
        if (!add_size(rx_cursor, section->size, &rx_cursor)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        layout->have_rx = true;
    }
    layout->rx_file_size = rx_cursor;

    if (!align_up_size(layout->rx_file_offset + layout->rx_file_size,
                       page,
                       &layout->rw_file_offset)) {
        fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
        return false;
    }
    {
        size_t next_vaddr;
        if (!align_up_size((size_t)(layout->rx_vaddr + layout->rx_file_size),
                           page,
                           &next_vaddr)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        layout->rw_vaddr = (uint64_t)next_vaddr;
    }

    for (i = 0U; i < state->section_count; ++i) {
        MiniLdSection *section = &state->sections[i];
        size_t aligned;

        if ((section->flags & (SHF_ALLOC | SHF_WRITE)) !=
                (SHF_ALLOC | SHF_WRITE) ||
            section->type == SHT_NOBITS) {
            continue;
        }
        if (!align_up_size(rw_file_cursor, section->align, &aligned)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        rw_file_cursor = aligned;
        rw_mem_cursor = aligned;
        layout->section_vaddr[i] = layout->rw_vaddr + rw_mem_cursor;
        layout->section_file_offset[i] =
            layout->rw_file_offset + rw_file_cursor;
        if (!add_size(rw_file_cursor, section->size, &rw_file_cursor) ||
            !add_size(rw_mem_cursor, section->size, &rw_mem_cursor)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        layout->have_rw = true;
    }

    for (i = 0U; i < state->section_count; ++i) {
        MiniLdSection *section = &state->sections[i];
        size_t aligned;

        if ((section->flags & (SHF_ALLOC | SHF_WRITE)) !=
                (SHF_ALLOC | SHF_WRITE) ||
            section->type != SHT_NOBITS) {
            continue;
        }
        if (!align_up_size(rw_mem_cursor, section->align, &aligned)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        rw_mem_cursor = aligned;
        layout->section_vaddr[i] = layout->rw_vaddr + rw_mem_cursor;
        if (!add_size(rw_mem_cursor, section->size, &rw_mem_cursor)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        layout->have_rw = true;
    }

    layout->rw_file_size = rw_file_cursor;
    layout->rw_mem_size = rw_mem_cursor;
    return layout->have_rx;
}

static void static_layout_destroy(MiniLdStaticLayout *layout) {
    free(layout->section_vaddr);
    free(layout->section_file_offset);
    layout->section_vaddr = NULL;
    layout->section_file_offset = NULL;
}

static bool static_resolve_symbol(MiniLdState *state,
                                  const MiniLdStaticLayout *layout,
                                  size_t symbol_index,
                                  uint64_t *value_out) {
    MiniLdSymbol *symbol;

    if (symbol_index >= state->symbol_count) {
        fprintf(state->diagnostics, "minic-ld: relocation-symbol-out-of-range\n");
        return false;
    }
    symbol = &state->symbols[symbol_index];
    if (symbol->section == MINILD_SECTION_UNDEF) {
        if (ELF64_ST_BIND(symbol->info) == STB_WEAK) {
            *value_out = 0U;
            return true;
        }
        fprintf(state->diagnostics,
                "minic-ld: undefined-reference:%s\n",
                symbol->name);
        return false;
    }
    if (symbol->section == MINILD_SECTION_ABS) {
        *value_out = symbol->value;
        return true;
    }
    if (symbol->section < 0 ||
        (size_t)symbol->section >= state->section_count ||
        (state->sections[symbol->section].flags & SHF_ALLOC) == 0U) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-static-symbol:%s\n",
                symbol->name);
        return false;
    }
    *value_out = layout->section_vaddr[symbol->section] + symbol->value;
    return true;
}

static size_t pcrel_capacity_for(size_t relocation_count) {
    size_t capacity = 64U;
    size_t target;

    if (relocation_count > (SIZE_MAX - 1U) / 2U) {
        return 0U;
    }
    target = relocation_count * 2U + 1U;
    while (capacity < target) {
        if (capacity > SIZE_MAX / 2U) {
            return 0U;
        }
        capacity *= 2U;
    }
    return capacity;
}

static size_t pcrel_hash(size_t section, uint64_t offset, size_t capacity) {
    uint64_t hash = offset ^ (UINT64_C(0x9e3779b97f4a7c15) *
                              (uint64_t)(section + 1U));
    hash ^= hash >> 33U;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33U;
    return (size_t)hash & (capacity - 1U);
}

static void pcrel_insert(MiniLdPcrelSlot *slots,
                         size_t capacity,
                         size_t section,
                         uint64_t offset,
                         int64_t delta) {
    size_t position = pcrel_hash(section, offset, capacity);

    while (slots[position].section_plus_one != 0U) {
        position = (position + 1U) & (capacity - 1U);
    }
    slots[position].section_plus_one = section + 1U;
    slots[position].offset = offset;
    slots[position].delta = delta;
}

static bool pcrel_find(const MiniLdPcrelSlot *slots,
                       size_t capacity,
                       size_t section,
                       uint64_t offset,
                       int64_t *delta_out) {
    size_t position = pcrel_hash(section, offset, capacity);

    while (slots[position].section_plus_one != 0U) {
        if (slots[position].section_plus_one == section + 1U &&
            slots[position].offset == offset) {
            *delta_out = slots[position].delta;
            return true;
        }
        position = (position + 1U) & (capacity - 1U);
    }
    return false;
}

static bool static_apply_relocations(MiniLdState *state,
                                     const MiniLdStaticLayout *layout) {
    size_t capacity = pcrel_capacity_for(state->reloc_count);
    MiniLdPcrelSlot *pcrel;
    size_t i;

    if (capacity == 0U) {
        fprintf(state->diagnostics, "minic-ld: relocation-index-overflow\n");
        return false;
    }
    pcrel = calloc(capacity, sizeof(*pcrel));
    if (pcrel == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:pcrel-index\n");
        return false;
    }

    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *reloc = &state->relocs[i];
        MiniLdSection *section = &state->sections[reloc->section];
        uint64_t symbol_value = 0U;
        uint64_t place = layout->section_vaddr[reloc->section] + reloc->offset;
        int64_t target;
        int64_t delta;

        if (reloc->type == R_RISCV_NONE || reloc->type == R_RISCV_RELAX ||
            reloc->type == R_RISCV_PCREL_LO12_I ||
            reloc->type == R_RISCV_PCREL_LO12_S) {
            continue;
        }
        if (reloc->symbol != SIZE_MAX &&
            !static_resolve_symbol(state, layout, reloc->symbol, &symbol_value)) {
            free(pcrel);
            return false;
        }
        target = (int64_t)symbol_value + reloc->addend;

        switch (reloc->type) {
        case R_RISCV_64:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 8U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_64-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u64le(section->data + (size_t)reloc->offset,
                        (uint64_t)target);
            break;
        case R_RISCV_32:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_32-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u32le(section->data + (size_t)reloc->offset,
                        (uint32_t)target);
            break;
        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT:
            delta = target - (int64_t)place;
            if (!static_patch_utype(section,
                                    reloc->offset,
                                    riscv_hi20(delta),
                                    state->diagnostics) ||
                !static_patch_itype(section,
                                    reloc->offset + 4U,
                                    riscv_lo12(delta),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_PCREL_HI20:
            delta = target - (int64_t)place;
            if (!static_patch_utype(section,
                                    reloc->offset,
                                    riscv_hi20(delta),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            pcrel_insert(pcrel,
                         capacity,
                         reloc->section,
                         reloc->offset,
                         delta);
            break;
        case R_RISCV_HI20:
            if (!static_patch_utype(section,
                                    reloc->offset,
                                    riscv_hi20(target),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_LO12_I:
            if (!static_patch_itype(section,
                                    reloc->offset,
                                    riscv_lo12(target),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_LO12_S:
            if (!static_patch_stype(section,
                                    reloc->offset,
                                    riscv_lo12(target),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_BRANCH:
            delta = target - (int64_t)place;
            if (!static_patch_branch(section,
                                     reloc->offset,
                                     delta,
                                     state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_JAL:
            delta = target - (int64_t)place;
            if (!static_patch_jal(section,
                                  reloc->offset,
                                  delta,
                                  state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_RVC_BRANCH:
            delta = target - (int64_t)place;
            if (!static_patch_rvc_branch(section,
                                         reloc->offset,
                                         delta,
                                         state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_RVC_JUMP:
            delta = target - (int64_t)place;
            if (!static_patch_rvc_jump(section,
                                       reloc->offset,
                                       delta,
                                       state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_32_PCREL:
            delta = target - (int64_t)place;
            if (delta < INT32_MIN || delta > INT32_MAX ||
                section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_32_PCREL-overflow-or-range:delta=%lld\n",
                        (long long)delta);
                free(pcrel);
                return false;
            }
            store_u32le(section->data + (size_t)reloc->offset,
                        (uint32_t)(int32_t)delta);
            break;
        default:
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-static-relocation:%u\n",
                    reloc->type);
            free(pcrel);
            return false;
        }
    }

    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *reloc = &state->relocs[i];
        MiniLdSection *section;
        MiniLdSymbol *label;
        int64_t delta;

        if (reloc->type != R_RISCV_PCREL_LO12_I &&
            reloc->type != R_RISCV_PCREL_LO12_S) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= state->symbol_count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-pcrel-lo-symbol\n");
            free(pcrel);
            return false;
        }
        label = &state->symbols[reloc->symbol];
        if (label->section < 0 ||
            (size_t)label->section >= state->section_count ||
            !pcrel_find(pcrel,
                        capacity,
                        (size_t)label->section,
                        label->value + (uint64_t)reloc->addend,
                        &delta)) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-matching-pcrel-hi20\n");
            free(pcrel);
            return false;
        }
        section = &state->sections[reloc->section];
        if (reloc->type == R_RISCV_PCREL_LO12_I) {
            if (!static_patch_itype(section,
                                    reloc->offset,
                                    riscv_lo12(delta),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
        } else if (!static_patch_stype(section,
                                       reloc->offset,
                                       riscv_lo12(delta),
                                       state->diagnostics)) {
            free(pcrel);
            return false;
        }
    }

    free(pcrel);
    return true;
}

static bool static_entry_address(MiniLdState *state,
                                 const MiniLdStaticLayout *layout,
                                 const char *entry_symbol,
                                 uint64_t *entry_out) {
    size_t index = find_global_symbol(state, entry_symbol);

    if (index == SIZE_MAX) {
        fprintf(state->diagnostics,
                "minic-ld: undefined-entry:%s\n",
                entry_symbol);
        return false;
    }
    return static_resolve_symbol(state, layout, index, entry_out);
}

static bool static_write_executable(MiniLdState *state,
                                    const MiniLdStaticLayout *layout,
                                    const char *path,
                                    uint64_t entry) {
    size_t phnum = (layout->have_rx ? 1U : 0U) +
                   (layout->have_rw ? 1U : 0U);
    size_t image_size = sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr);
    unsigned char *image;
    Elf64_Ehdr header;
    Elf64_Phdr *programs;
    size_t i;
    size_t program_index = 0U;
    FILE *file = NULL;
    bool ok = false;

    if (layout->have_rx &&
        layout->rx_file_offset + layout->rx_file_size > image_size) {
        image_size = layout->rx_file_offset + layout->rx_file_size;
    }
    if (layout->have_rw &&
        layout->rw_file_offset + layout->rw_file_size > image_size) {
        image_size = layout->rw_file_offset + layout->rw_file_size;
    }
    image = calloc(image_size == 0U ? 1U : image_size, 1U);
    if (image == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-image\n");
        return false;
    }

    memset(&header, 0, sizeof(header));
    memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_ident[EI_OSABI] = ELFOSABI_NONE;
    header.e_type = ET_EXEC;
    header.e_machine = EM_RISCV;
    header.e_version = EV_CURRENT;
    header.e_entry = entry;
    header.e_phoff = sizeof(Elf64_Ehdr);
    header.e_flags = state->elf_flags;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = (Elf64_Half)phnum;
    memcpy(image, &header, sizeof(header));

    programs = (Elf64_Phdr *)(void *)(image + sizeof(Elf64_Ehdr));
    if (layout->have_rx) {
        Elf64_Phdr *ph = &programs[program_index++];
        memset(ph, 0, sizeof(*ph));
        ph->p_type = PT_LOAD;
        ph->p_flags = PF_R | PF_X;
        ph->p_offset = layout->rx_file_offset;
        ph->p_vaddr = layout->rx_vaddr;
        ph->p_paddr = layout->rx_vaddr;
        ph->p_filesz = layout->rx_file_size;
        ph->p_memsz = layout->rx_file_size;
        ph->p_align = 4096U;
    }
    if (layout->have_rw) {
        Elf64_Phdr *ph = &programs[program_index++];
        memset(ph, 0, sizeof(*ph));
        ph->p_type = PT_LOAD;
        ph->p_flags = PF_R | PF_W;
        ph->p_offset = layout->rw_file_offset;
        ph->p_vaddr = layout->rw_vaddr;
        ph->p_paddr = layout->rw_vaddr;
        ph->p_filesz = layout->rw_file_size;
        ph->p_memsz = layout->rw_mem_size;
        ph->p_align = 4096U;
    }

    for (i = 0U; i < state->section_count; ++i) {
        MiniLdSection *section = &state->sections[i];
        size_t offset = layout->section_file_offset[i];

        if ((section->flags & SHF_ALLOC) == 0U ||
            section->type == SHT_NOBITS ||
            section->size == 0U) {
            continue;
        }
        if (offset == SIZE_MAX ||
            !range_ok(offset, section->size, image_size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-section-layout:%s\n",
                    section->name);
            goto done;
        }
        memcpy(image + offset, section->data, section->size);
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: cannot-create:%s:%s\n",
                path,
                strerror(errno));
        goto done;
    }
    if (fwrite(image, 1U, image_size, file) != image_size ||
        fflush(file) != 0) {
        fprintf(state->diagnostics, "minic-ld: write-error:%s\n", path);
        goto done;
    }
    ok = true;

done:
    if (file != NULL && fclose(file) != 0) {
        ok = false;
    }
    if (ok) {
        if (chmod(path, 0755) != 0) {
            fprintf(state->diagnostics,
                    "minic-ld: chmod-error:%s:%s\n",
                    path,
                    strerror(errno));
            ok = false;
        }
    }
    if (!ok) {
        (void)remove(path);
    }
    free(image);
    return ok;
}

int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics) {
    MiniLdState state;
    MiniLdStaticLayout layout;
    size_t i;
    uint64_t entry = 0U;
    bool layout_ready = false;
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        entry_symbol == NULL || diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    memset(&layout, 0, sizeof(layout));
    state.diagnostics = diagnostics;

    for (i = 0U; i < input_count; ++i) {
        bool input_ok;

        switch (inputs[i].kind) {
        case MINILD_INPUT_OBJECT:
            input_ok = process_input(&state, inputs[i].path);
            break;
        case MINILD_INPUT_WHOLE_ARCHIVE:
            input_ok = process_whole_archive(&state, inputs[i].path);
            break;
        case MINILD_INPUT_GROUP_ARCHIVE:
            input_ok = process_group_archive(&state, inputs[i].path);
            break;
        default:
            fprintf(diagnostics,
                    "minic-ld: invalid-input-kind:%s\n",
                    inputs[i].path);
            input_ok = false;
            break;
        }
        if (!input_ok) {
            goto done;
        }
    }

    if (!state.have_input ||
        !static_allocate_common(&state) ||
        !static_build_layout(&state, &layout)) {
        goto done;
    }
    layout_ready = true;
    if (!static_apply_relocations(&state, &layout) ||
        !static_entry_address(&state, &layout, entry_symbol, &entry) ||
        !static_write_executable(&state, &layout, output_path, entry)) {
        goto done;
    }
    ok = true;

done:
    if (layout_ready) {
        static_layout_destroy(&layout);
    }
    state_destroy(&state);
    return ok ? 0 : 1;
}

int minild_link_relocatable_elf64_riscv_inputs(const char *output_path,
                                               const MiniLdInput *inputs,
                                               size_t input_count,
                                               FILE *diagnostics) {
    MiniLdState state;
    size_t i;
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    state.diagnostics = diagnostics;

    for (i = 0U; i < input_count; ++i) {
        bool input_ok;

        switch (inputs[i].kind) {
        case MINILD_INPUT_OBJECT:
            input_ok = process_input(&state, inputs[i].path);
            break;
        case MINILD_INPUT_WHOLE_ARCHIVE:
            input_ok = process_whole_archive(&state, inputs[i].path);
            break;
        case MINILD_INPUT_GROUP_ARCHIVE:
            input_ok = process_group_archive(&state, inputs[i].path);
            break;
        default:
            fprintf(diagnostics,
                    "minic-ld: invalid-input-kind:%s\n",
                    inputs[i].path);
            input_ok = false;
            break;
        }
        if (!input_ok) {
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

int minild_link_relocatable_elf64_riscv(const char *output_path,
                                        const char *const *input_paths,
                                        size_t input_count,
                                        FILE *diagnostics) {
    MiniLdInput *inputs;
    size_t i;
    int result;

    if (output_path == NULL || input_paths == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    inputs = malloc(input_count * sizeof(*inputs));
    if (inputs == NULL) {
        fprintf(diagnostics, "minic-ld: out-of-memory:inputs\n");
        return 1;
    }
    for (i = 0U; i < input_count; ++i) {
        inputs[i].path = input_paths[i];
        inputs[i].kind = MINILD_INPUT_OBJECT;
    }
    result = minild_link_relocatable_elf64_riscv_inputs(output_path,
                                                         inputs,
                                                         input_count,
                                                         diagnostics);
    free(inputs);
    return result;
}
