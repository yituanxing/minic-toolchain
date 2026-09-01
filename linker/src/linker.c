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
    size_t processed_object_count;
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
    ++state->processed_object_count;
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

static bool process_object_or_archive(MiniLdState *state,
                                      const char *path) {
    int kind = archive_file_kind(path, state->diagnostics);

    if (kind < 0) {
        return false;
    }
    if (kind == 1 || kind == 2) {
        return process_group_archive(state, path);
    }
    return process_input(state, path);
}


static bool process_group_sequence(MiniLdState *state,
                                   const MiniLdInput *inputs,
                                   size_t input_count) {
    bool first_pass = true;
    bool changed;
    size_t i;

    do {
        size_t before = state->processed_object_count;

        for (i = 0U; i < input_count; ++i) {
            int kind = archive_file_kind(inputs[i].path, state->diagnostics);

            if (kind < 0) {
                return false;
            }
            if (kind == 1 || kind == 2) {
                if (!process_group_archive(state, inputs[i].path)) {
                    return false;
                }
            } else if (first_pass) {
                if (!process_input(state, inputs[i].path)) {
                    return false;
                }
            }
        }
        changed = state->processed_object_count != before;
        first_pass = false;
    } while (changed);

    return true;
}

static bool process_input_sequence(MiniLdState *state,
                                   const MiniLdInput *inputs,
                                   size_t input_count) {
    size_t i = 0U;

    while (i < input_count) {
        bool input_ok;

        if (inputs[i].kind == MINILD_INPUT_GROUP_ARCHIVE) {
            size_t end = i + 1U;

            while (end < input_count &&
                   inputs[end].kind == MINILD_INPUT_GROUP_ARCHIVE) {
                ++end;
            }
            if (!process_group_sequence(state, inputs + i, end - i)) {
                return false;
            }
            i = end;
            continue;
        }

        switch (inputs[i].kind) {
        case MINILD_INPUT_OBJECT:
            input_ok = process_object_or_archive(state, inputs[i].path);
            break;
        case MINILD_INPUT_WHOLE_ARCHIVE:
            input_ok = process_whole_archive(state, inputs[i].path);
            break;
        case MINILD_INPUT_ARCHIVE:
            input_ok = process_group_archive(state, inputs[i].path);
            break;
        default:
            fprintf(state->diagnostics,
                    "minic-ld: invalid-input-kind:%s\n",
                    inputs[i].path);
            input_ok = false;
            break;
        }
        if (!input_ok) {
            return false;
        }
        ++i;
    }
    return true;
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

typedef struct MiniLdAlignSite {
    size_t offset;
    size_t max_padding;
} MiniLdAlignSite;

typedef struct MiniLdRelaxEvent {
    size_t raw_end;
    size_t cumulative_deleted;
} MiniLdRelaxEvent;

typedef struct MiniLdStaticGot {
    size_t section;
    size_t *slot_offsets;
    size_t symbol_count;
    bool present;
} MiniLdStaticGot;

static uint8_t load_u8(const unsigned char *data) {
    return data[0];
}

static void store_u8(unsigned char *data, uint8_t value) {
    data[0] = value;
}

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

static uint64_t load_u64le(const unsigned char *data) {
    uint64_t value = 0U;
    size_t i;

    for (i = 0U; i < 8U; ++i) {
        value |= (uint64_t)data[i] << (i * 8U);
    }
    return value;
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


static void static_got_destroy(MiniLdStaticGot *got) {
    free(got->slot_offsets);
    got->slot_offsets = NULL;
    got->symbol_count = 0U;
    got->section = SIZE_MAX;
    got->present = false;
}

static bool static_build_got(MiniLdState *state, MiniLdStaticGot *got) {
    size_t i;

    memset(got, 0, sizeof(*got));
    got->section = SIZE_MAX;
    got->symbol_count = state->symbol_count;
    got->slot_offsets =
        malloc((got->symbol_count == 0U ? 1U : got->symbol_count) *
               sizeof(*got->slot_offsets));
    if (got->slot_offsets == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->slot_offsets[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *reloc = &state->relocs[i];
        MiniLdSection *section;
        size_t aligned;

        if (reloc->type != R_RISCV_GOT_HI20) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count ||
            reloc->addend != 0) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_RISCV_GOT_HI20\n");
            static_got_destroy(got);
            return false;
        }
        if (got->slot_offsets[reloc->symbol] != SIZE_MAX) {
            continue;
        }
        if (!got->present) {
            if (!find_or_add_section(state,
                                     ".got",
                                     SHT_PROGBITS,
                                     SHF_ALLOC | SHF_WRITE,
                                     8U,
                                     0U,
                                     &got->section)) {
                static_got_destroy(got);
                return false;
            }
            got->present = true;
        }

        section = &state->sections[got->section];
        if (!align_up_size(section->size, 8U, &aligned) ||
            !section_append_zero(section, aligned - section->size) ||
            !section_append_zero(section, 8U)) {
            fprintf(state->diagnostics,
                    "minic-ld: static-got-allocation-overflow\n");
            static_got_destroy(got);
            return false;
        }
        got->slot_offsets[reloc->symbol] = aligned;
    }
    return true;
}


static int static_align_site_compare(const void *left, const void *right) {
    const MiniLdAlignSite *a = left;
    const MiniLdAlignSite *b = right;

    if (a->offset < b->offset) {
        return -1;
    }
    if (a->offset > b->offset) {
        return 1;
    }
    return 0;
}

static bool static_append_riscv_nops(unsigned char *output,
                                     size_t output_capacity,
                                     size_t *output_size,
                                     size_t length) {
    while (length >= 4U) {
        if (!range_ok(*output_size, 4U, output_capacity)) {
            return false;
        }
        output[*output_size + 0U] = 0x13U;
        output[*output_size + 1U] = 0x00U;
        output[*output_size + 2U] = 0x00U;
        output[*output_size + 3U] = 0x00U;
        *output_size += 4U;
        length -= 4U;
    }
    if (length == 2U) {
        if (!range_ok(*output_size, 2U, output_capacity)) {
            return false;
        }
        output[*output_size + 0U] = 0x01U;
        output[*output_size + 1U] = 0x00U;
        *output_size += 2U;
        length = 0U;
    }
    return length == 0U;
}

static size_t static_map_relaxed_offset(
    size_t raw_offset,
    const MiniLdRelaxEvent *events,
    size_t event_count) {
    size_t deleted = 0U;
    size_t i;

    for (i = 0U; i < event_count; ++i) {
        if (raw_offset < events[i].raw_end) {
            break;
        }
        deleted = events[i].cumulative_deleted;
    }
    return raw_offset - deleted;
}

static bool static_relax_align_section(MiniLdState *state,
                                       size_t section_index,
                                       uint64_t base_addr) {
    MiniLdSection *section = &state->sections[section_index];
    MiniLdAlignSite *sites = NULL;
    MiniLdRelaxEvent *events = NULL;
    unsigned char *output = NULL;
    size_t site_count = 0U;
    size_t event_count = 0U;
    size_t output_size = 0U;
    size_t cursor = 0U;
    size_t i;
    bool ok = false;

    if (section->type == SHT_NOBITS || section->size == 0U) {
        return true;
    }

    for (i = 0U; i < state->reloc_count; ++i) {
        if (state->relocs[i].section == section_index &&
            state->relocs[i].type == R_RISCV_ALIGN) {
            ++site_count;
        }
    }
    if (site_count == 0U) {
        return true;
    }

    sites = malloc(site_count * sizeof(*sites));
    events = malloc(site_count * sizeof(*events));
    output = malloc(section->size == 0U ? 1U : section->size);
    if (sites == NULL || events == NULL || output == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:align-relax:%s\n",
                section->name);
        goto done;
    }

    site_count = 0U;
    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *reloc = &state->relocs[i];

        if (reloc->section != section_index ||
            reloc->type != R_RISCV_ALIGN) {
            continue;
        }
        if (reloc->offset > SIZE_MAX || reloc->addend < 0 ||
            (uint64_t)reloc->addend > SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_RISCV_ALIGN:%s\n",
                    section->name);
            goto done;
        }
        sites[site_count].offset = (size_t)reloc->offset;
        sites[site_count].max_padding = (size_t)reloc->addend;
        ++site_count;
    }
    qsort(sites,
          site_count,
          sizeof(*sites),
          static_align_site_compare);

    for (i = 0U; i < site_count; ++i) {
        size_t raw_offset = sites[i].offset;
        size_t max_padding = sites[i].max_padding;
        size_t alignment = 1U;
        size_t required;
        uint64_t place;
        size_t bits_value;

        if ((max_padding & 1U) != 0U ||
            raw_offset < cursor ||
            !range_ok(raw_offset, max_padding, section->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_RISCV_ALIGN-range:%s:%zu+%zu\n",
                    section->name,
                    raw_offset,
                    max_padding);
            goto done;
        }

        if (!range_ok(cursor, raw_offset - cursor, section->size) ||
            !range_ok(output_size, raw_offset - cursor, section->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_RISCV_ALIGN-copy:%s\n",
                    section->name);
            goto done;
        }
        memcpy(output + output_size,
               section->data + cursor,
               raw_offset - cursor);
        output_size += raw_offset - cursor;

        bits_value = max_padding;
        while (bits_value != 0U) {
            if (alignment > SIZE_MAX / 2U) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ALIGN-overflow:%s\n",
                        section->name);
                goto done;
            }
            alignment *= 2U;
            bits_value >>= 1U;
        }

        if (base_addr > UINT64_MAX - (uint64_t)output_size) {
            fprintf(state->diagnostics,
                    "minic-ld: R_RISCV_ALIGN-address-overflow:%s\n",
                    section->name);
            goto done;
        }
        place = base_addr + (uint64_t)output_size;
        required = (size_t)((-(uint64_t)place) &
                            (uint64_t)(alignment - 1U));
        if (required > max_padding || (required & 1U) != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: R_RISCV_ALIGN-cannot-satisfy:%s:"
                    "align=%zu:required=%zu:max=%zu\n",
                    section->name,
                    alignment,
                    required,
                    max_padding);
            goto done;
        }
        if (!static_append_riscv_nops(output,
                                      section->size,
                                      &output_size,
                                      required)) {
            fprintf(state->diagnostics,
                    "minic-ld: R_RISCV_ALIGN-nop-overflow:%s\n",
                    section->name);
            goto done;
        }

        events[event_count].raw_end = raw_offset + max_padding;
        events[event_count].cumulative_deleted =
            (event_count == 0U
                 ? 0U
                 : events[event_count - 1U].cumulative_deleted) +
            (max_padding - required);
        ++event_count;
        cursor = raw_offset + max_padding;
    }

    if (!range_ok(cursor, section->size - cursor, section->size) ||
        !range_ok(output_size, section->size - cursor, section->size)) {
        fprintf(state->diagnostics,
                "minic-ld: R_RISCV_ALIGN-tail-overflow:%s\n",
                section->name);
        goto done;
    }
    memcpy(output + output_size,
           section->data + cursor,
           section->size - cursor);
    output_size += section->size - cursor;

    for (i = 0U; i < state->symbol_count; ++i) {
        MiniLdSymbol *symbol = &state->symbols[i];

        if (symbol->section == (int)section_index &&
            symbol->value <= SIZE_MAX) {
            symbol->value = (uint64_t)static_map_relaxed_offset(
                (size_t)symbol->value,
                events,
                event_count);
        }
    }

    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *reloc = &state->relocs[i];

        if (reloc->section == section_index &&
            reloc->offset <= SIZE_MAX) {
            reloc->offset = (uint64_t)static_map_relaxed_offset(
                (size_t)reloc->offset,
                events,
                event_count);
        }
    }

    memcpy(section->data, output, output_size);
    section->size = output_size;
    ok = true;

done:
    free(output);
    free(events);
    free(sites);
    return ok;
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
        if (!static_relax_align_section(state,
                                        i,
                                        layout->section_vaddr[i])) {
            return false;
        }
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
        if (!static_relax_align_section(state,
                                        i,
                                        layout->section_vaddr[i])) {
            return false;
        }
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


static bool static_section_has_prefix(const char *name, const char *prefix) {
    size_t length = strlen(prefix);

    return strcmp(name, prefix) == 0 ||
           (strncmp(name, prefix, length) == 0 && name[length] == '.');
}

static bool static_define_absolute_if_undefined(MiniLdState *state,
                                                const char *name,
                                                uint64_t value) {
    size_t existing = find_global_symbol(state, name);
    size_t symbol_index;

    if (existing != SIZE_MAX &&
        symbol_is_defined(&state->symbols[existing])) {
        return true;
    }
    return add_or_merge_global_symbol(state,
                                      name,
                                      MINILD_SECTION_ABS,
                                      value,
                                      0U,
                                      ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE),
                                      STV_DEFAULT,
                                      &symbol_index);
}

static bool static_synthesize_array_bound_pair(
    MiniLdState *state,
    const MiniLdStaticLayout *layout,
    const char *section_prefix,
    const char *start_name,
    const char *end_name) {
    uint64_t start = UINT64_MAX;
    uint64_t end = 0U;
    bool found = false;
    size_t i;

    for (i = 0U; i < state->section_count; ++i) {
        MiniLdSection *section = &state->sections[i];
        uint64_t section_start;
        uint64_t section_end;

        if ((section->flags & SHF_ALLOC) == 0U ||
            !static_section_has_prefix(section->name, section_prefix)) {
            continue;
        }
        section_start = layout->section_vaddr[i];
        section_end = section_start + (uint64_t)section->size;
        if (!found || section_start < start) {
            start = section_start;
        }
        if (!found || section_end > end) {
            end = section_end;
        }
        found = true;
    }

    if (!found) {
        /*
         * Match the old Python linker: absent constructor/destructor arrays
         * are represented by a zero-length range at the end of writable data.
         */
        start = layout->rw_vaddr + (uint64_t)layout->rw_mem_size;
        end = start;
    }
    return static_define_absolute_if_undefined(state, start_name, start) &&
           static_define_absolute_if_undefined(state, end_name, end);
}


static bool static_resolve_symbol(MiniLdState *state,
                                  const MiniLdStaticLayout *layout,
                                  size_t symbol_index,
                                  uint64_t *value_out);

static bool static_fill_got(MiniLdState *state,
                            const MiniLdStaticLayout *layout,
                            const MiniLdStaticGot *got) {
    MiniLdSection *section;
    size_t i;

    if (!got->present) {
        return true;
    }
    if (got->section >= state->section_count) {
        fprintf(state->diagnostics, "minic-ld: invalid-static-got-section\n");
        return false;
    }
    section = &state->sections[got->section];

    for (i = 0U; i < got->symbol_count; ++i) {
        uint64_t value;

        if (got->slot_offsets[i] == SIZE_MAX) {
            continue;
        }
        if (!static_resolve_symbol(state, layout, i, &value) ||
            !range_ok(got->slot_offsets[i], 8U, section->size)) {
            return false;
        }
        store_u64le(section->data + got->slot_offsets[i], value);
    }
    return true;
}

static bool static_synthesize_runtime_boundaries(
    MiniLdState *state,
    const MiniLdStaticLayout *layout) {
    return static_synthesize_array_bound_pair(state,
                                               layout,
                                               ".preinit_array",
                                               "__preinit_array_start",
                                               "__preinit_array_end") &&
           static_synthesize_array_bound_pair(state,
                                               layout,
                                               ".init_array",
                                               "__init_array_start",
                                               "__init_array_end") &&
           static_synthesize_array_bound_pair(state,
                                               layout,
                                               ".fini_array",
                                               "__fini_array_start",
                                               "__fini_array_end");
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
                                     const MiniLdStaticLayout *layout,
                                     const MiniLdStaticGot *got) {
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

        if (reloc->type == R_RISCV_NONE ||
            reloc->type == R_RISCV_RELAX ||
            reloc->type == R_RISCV_ALIGN ||
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

        case R_RISCV_GOT_HI20: {
            uint64_t got_address;

            if (!got->present || reloc->symbol == SIZE_MAX ||
                reloc->symbol >= got->symbol_count ||
                got->slot_offsets[reloc->symbol] == SIZE_MAX ||
                reloc->addend != 0) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-R_RISCV_GOT_HI20-slot\n");
                free(pcrel);
                return false;
            }
            got_address = layout->section_vaddr[got->section] +
                          (uint64_t)got->slot_offsets[reloc->symbol];
            delta = (int64_t)got_address - (int64_t)place;
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
        }
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
            if ((delta & 1) != 0 ||
                delta < -1048576 ||
                delta > 1048574) {
                const char *symbol_name =
                    (reloc->symbol != SIZE_MAX &&
                     reloc->symbol < state->symbol_count)
                        ? state->symbols[reloc->symbol].name
                        : "<none>";
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_JAL-overflow:"
                        "section=%s:offset=%llu:symbol=%s:"
                        "place=0x%llx:target=0x%llx:delta=%lld\n",
                        section->name,
                        (unsigned long long)reloc->offset,
                        symbol_name,
                        (unsigned long long)place,
                        (unsigned long long)(uint64_t)target,
                        (long long)delta);
                free(pcrel);
                return false;
            }
            if (!static_patch_jal(section,
                                  reloc->offset,
                                  delta,
                                  state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;

        case R_RISCV_ADD8:
        case R_RISCV_SUB8: {
            uint8_t current;
            uint8_t operand;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 1U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ADD_SUB8-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u8(section->data + (size_t)reloc->offset);
            operand = (uint8_t)(uint64_t)target;
            if (reloc->type == R_RISCV_ADD8) {
                current = (uint8_t)(current + operand);
            } else {
                current = (uint8_t)(current - operand);
            }
            store_u8(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_SUB6: {
            uint8_t current;
            uint8_t low6;
            uint8_t operand;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 1U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SUB6-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u8(section->data + (size_t)reloc->offset);
            low6 = (uint8_t)(current & UINT8_C(0x3f));
            operand = (uint8_t)((uint64_t)target & UINT64_C(0x3f));
            low6 = (uint8_t)((low6 - operand) & UINT8_C(0x3f));
            current = (uint8_t)((current & UINT8_C(0xc0)) | low6);
            store_u8(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_SET6: {
            uint8_t current;
            uint8_t encoded;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 1U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SET6-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u8(section->data + (size_t)reloc->offset);
            encoded = (uint8_t)((uint64_t)target & UINT64_C(0x3f));
            current = (uint8_t)((current & UINT8_C(0xc0)) | encoded);
            store_u8(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_SET8:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 1U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SET8-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u8(section->data + (size_t)reloc->offset,
                     (uint8_t)(uint64_t)target);
            break;
        case R_RISCV_SET16:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 2U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SET16-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u16le(section->data + (size_t)reloc->offset,
                        (uint16_t)(uint64_t)target);
            break;
        case R_RISCV_SET32:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SET32-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u32le(section->data + (size_t)reloc->offset,
                        (uint32_t)(uint64_t)target);
            break;
        case R_RISCV_ADD16:
        case R_RISCV_SUB16: {
            uint16_t current;
            uint16_t operand;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 2U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ADD_SUB16-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u16le(section->data + (size_t)reloc->offset);
            operand = (uint16_t)(uint64_t)target;
            if (reloc->type == R_RISCV_ADD16) {
                current = (uint16_t)(current + operand);
            } else {
                current = (uint16_t)(current - operand);
            }
            store_u16le(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_ADD32:
        case R_RISCV_SUB32: {
            uint32_t current;
            uint32_t operand;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ADD_SUB32-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u32le(section->data + (size_t)reloc->offset);
            operand = (uint32_t)(uint64_t)target;
            if (reloc->type == R_RISCV_ADD32) {
                current += operand;
            } else {
                current -= operand;
            }
            store_u32le(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_ADD64:
        case R_RISCV_SUB64: {
            uint64_t current = 0U;
            uint64_t operand = (uint64_t)target;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 8U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ADD_SUB64-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u64le(section->data + (size_t)reloc->offset);
            if (reloc->type == R_RISCV_ADD64) {
                current += operand;
            } else {
                current -= operand;
            }
            store_u64le(section->data + (size_t)reloc->offset, current);
            break;
        }
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


typedef struct MiniLdSharedImage {
    size_t dynstr_section;
    size_t dynsym_section;
    size_t hash_section;
    size_t rela_section;
    size_t plt_section;
    size_t gotplt_section;
    size_t rela_plt_section;
    size_t dynamic_section;
    uint32_t soname_offset;
    size_t *dynsym_index;
    uint32_t *dynstr_name_offset;
    size_t *plt_index;
    size_t dynsym_count;
    size_t rela_count;
    size_t plt_count;
    bool have_soname;
} MiniLdSharedImage;

static void shared_image_destroy(MiniLdSharedImage *shared) {
    free(shared->dynsym_index);
    free(shared->dynstr_name_offset);
    free(shared->plt_index);
    shared->dynsym_index = NULL;
    shared->dynstr_name_offset = NULL;
    shared->plt_index = NULL;
    shared->dynsym_count = 0U;
    shared->rela_count = 0U;
    shared->plt_count = 0U;
}

static uint32_t shared_elf_hash(const char *name) {
    uint32_t hash = 0U;

    while (*name != '\0') {
        uint32_t high;

        hash = (hash << 4U) + (unsigned char)*name++;
        high = hash & UINT32_C(0xf0000000);
        if (high != 0U) {
            hash ^= high >> 24U;
        }
        hash &= ~high;
    }
    return hash;
}

static bool shared_symbol_eligible(const MiniLdState *state, size_t index) {
    const MiniLdSymbol *symbol = &state->symbols[index];
    unsigned bind = ELF64_ST_BIND(symbol->info);
    unsigned visibility = ELF64_ST_VISIBILITY(symbol->other);

    if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
        symbol->name[0] == '\0' ||
        (visibility != STV_DEFAULT && visibility != STV_PROTECTED)) {
        return false;
    }
    if (symbol->section >= 0) {
        size_t section = (size_t)symbol->section;
        if (section >= state->section_count ||
            (state->sections[section].flags & SHF_ALLOC) == 0U) {
            return false;
        }
    }
    return true;
}

static bool shared_symbol_needs_plt(const MiniLdSymbol *symbol) {
    unsigned bind = ELF64_ST_BIND(symbol->info);
    unsigned visibility = ELF64_ST_VISIBILITY(symbol->other);

    if (symbol->section == MINILD_SECTION_UNDEF) {
        return true;
    }
    return (bind == STB_GLOBAL || bind == STB_WEAK) &&
           visibility == STV_DEFAULT;
}

static bool shared_prepare_metadata(MiniLdState *state,
                                    const char *soname,
                                    MiniLdSharedImage *shared) {
    MiniLdSection *dynstr;
    MiniLdSection *dynsym;
    MiniLdSection *hash;
    MiniLdSection *rela;
    MiniLdSection *plt;
    MiniLdSection *gotplt;
    MiniLdSection *rela_plt;
    MiniLdSection *dynamic;
    size_t dynsym_size;
    size_t hash_words;
    size_t dynamic_entries;
    size_t i;

    memset(shared, 0, sizeof(*shared));
    shared->dynstr_section = SIZE_MAX;
    shared->dynsym_section = SIZE_MAX;
    shared->hash_section = SIZE_MAX;
    shared->rela_section = SIZE_MAX;
    shared->plt_section = SIZE_MAX;
    shared->gotplt_section = SIZE_MAX;
    shared->rela_plt_section = SIZE_MAX;
    shared->dynamic_section = SIZE_MAX;

    shared->dynsym_index =
        malloc((state->symbol_count == 0U ? 1U : state->symbol_count) *
               sizeof(*shared->dynsym_index));
    shared->dynstr_name_offset =
        calloc(state->symbol_count == 0U ? 1U : state->symbol_count,
               sizeof(*shared->dynstr_name_offset));
    shared->plt_index =
        malloc((state->symbol_count == 0U ? 1U : state->symbol_count) *
               sizeof(*shared->plt_index));
    if (shared->dynsym_index == NULL ||
        shared->dynstr_name_offset == NULL ||
        shared->plt_index == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:shared-symbol-map\n");
        goto fail;
    }
    for (i = 0U; i < state->symbol_count; ++i) {
        shared->dynsym_index[i] = SIZE_MAX;
        shared->plt_index[i] = SIZE_MAX;
    }

    if (!find_or_add_section(state,
                             ".dynstr",
                             SHT_STRTAB,
                             SHF_ALLOC,
                             1U,
                             0U,
                             &shared->dynstr_section) ||
        !find_or_add_section(state,
                             ".dynsym",
                             SHT_DYNSYM,
                             SHF_ALLOC,
                             8U,
                             sizeof(Elf64_Sym),
                             &shared->dynsym_section) ||
        !find_or_add_section(state,
                             ".hash",
                             SHT_HASH,
                             SHF_ALLOC,
                             4U,
                             sizeof(uint32_t),
                             &shared->hash_section) ||
        !find_or_add_section(state,
                             ".rela.dyn",
                             SHT_RELA,
                             SHF_ALLOC,
                             8U,
                             sizeof(Elf64_Rela),
                             &shared->rela_section) ||
        !find_or_add_section(state,
                             ".plt",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_EXECINSTR,
                             16U,
                             16U,
                             &shared->plt_section) ||
        !find_or_add_section(state,
                             ".got.plt",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_WRITE,
                             8U,
                             8U,
                             &shared->gotplt_section) ||
        !find_or_add_section(state,
                             ".rela.plt",
                             SHT_RELA,
                             SHF_ALLOC,
                             8U,
                             sizeof(Elf64_Rela),
                             &shared->rela_plt_section) ||
        !find_or_add_section(state,
                             ".dynamic",
                             SHT_DYNAMIC,
                             SHF_ALLOC | SHF_WRITE,
                             8U,
                             sizeof(Elf64_Dyn),
                             &shared->dynamic_section)) {
        goto fail;
    }

    dynstr = &state->sections[shared->dynstr_section];
    dynsym = &state->sections[shared->dynsym_section];
    hash = &state->sections[shared->hash_section];
    rela = &state->sections[shared->rela_section];
    plt = &state->sections[shared->plt_section];
    gotplt = &state->sections[shared->gotplt_section];
    rela_plt = &state->sections[shared->rela_plt_section];
    dynamic = &state->sections[shared->dynamic_section];

    if (!section_append_zero(dynstr, 1U)) {
        goto oom;
    }
    if (soname != NULL && soname[0] != '\0') {
        if (dynstr->size > UINT32_MAX) {
            goto oom;
        }
        shared->soname_offset = (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)soname,
                                 strlen(soname) + 1U)) {
            goto oom;
        }
        shared->have_soname = true;
    }

    shared->dynsym_count = 1U;
    for (i = 0U; i < state->symbol_count; ++i) {
        if (!shared_symbol_eligible(state, i)) {
            continue;
        }
        if (dynstr->size > UINT32_MAX) {
            goto oom;
        }
        shared->dynstr_name_offset[i] = (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)state->symbols[i].name,
                                 strlen(state->symbols[i].name) + 1U)) {
            goto oom;
        }
        shared->dynsym_index[i] = shared->dynsym_count++;
    }

    if (shared->dynsym_count > SIZE_MAX / sizeof(Elf64_Sym)) {
        goto oom;
    }
    dynsym_size = shared->dynsym_count * sizeof(Elf64_Sym);
    if (!section_append_zero(dynsym, dynsym_size)) {
        goto oom;
    }

    {
        size_t nbucket = shared->dynsym_count < 4U
                             ? 1U
                             : shared->dynsym_count / 4U;
        if (nbucket == 0U) {
            nbucket = 1U;
        }
        if (nbucket > SIZE_MAX - shared->dynsym_count - 2U) {
            goto oom;
        }
        hash_words = 2U + nbucket + shared->dynsym_count;
    }
    if (hash_words > SIZE_MAX / sizeof(uint32_t) ||
        !section_append_zero(hash, hash_words * sizeof(uint32_t))) {
        goto oom;
    }

    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *reloc = &state->relocs[i];
        MiniLdSymbol *target;

        if (reloc->section >= state->section_count ||
            (state->sections[reloc->section].flags & SHF_ALLOC) == 0U) {
            continue;
        }
        if (reloc->type == R_RISCV_NONE ||
            reloc->type == R_RISCV_RELAX ||
            reloc->type == R_RISCV_ALIGN) {
            continue;
        }
        if (reloc->type == R_RISCV_CALL ||
            reloc->type == R_RISCV_CALL_PLT) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->symbol_count) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-shared-call-relocation\n");
                goto fail;
            }
            target = &state->symbols[reloc->symbol];
            if (shared_symbol_needs_plt(target) &&
                shared->plt_index[reloc->symbol] == SIZE_MAX) {
                if (shared->dynsym_index[reloc->symbol] == SIZE_MAX) {
                    fprintf(state->diagnostics,
                            "minic-ld: missing-dynsym-for-plt:%s\n",
                            target->name);
                    goto fail;
                }
                shared->plt_index[reloc->symbol] = shared->plt_count++;
            }
            continue;
        }
        if (reloc->type != R_RISCV_64) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-relocation:%u\n",
                    reloc->type);
            goto fail;
        }
        ++shared->rela_count;
    }
    if (shared->rela_count > SIZE_MAX / sizeof(Elf64_Rela) ||
        !section_append_zero(rela,
                             shared->rela_count * sizeof(Elf64_Rela))) {
        goto oom;
    }

    if (shared->plt_count != 0U) {
        size_t plt_size;
        size_t gotplt_size;
        size_t rela_plt_size;

        if (shared->plt_count > (SIZE_MAX - 32U) / 16U ||
            shared->plt_count > (SIZE_MAX - 16U) / 8U ||
            shared->plt_count > SIZE_MAX / sizeof(Elf64_Rela)) {
            goto oom;
        }
        plt_size = shared->plt_count * 16U;
        gotplt_size = (3U + shared->plt_count) * 8U;
        rela_plt_size = shared->plt_count * sizeof(Elf64_Rela);
        if (!section_append_zero(plt, plt_size) ||
            !section_append_zero(gotplt, gotplt_size) ||
            !section_append_zero(rela_plt, rela_plt_size)) {
            goto oom;
        }
    }

    dynamic_entries = 6U; /* HASH/STRTAB/SYMTAB/STRSZ/SYMENT/NULL */
    if (shared->rela_count != 0U) {
        dynamic_entries += 3U; /* RELA/RELASZ/RELAENT */
    }
    if (shared->plt_count != 0U) {
        dynamic_entries += 5U; /* PLTGOT/PLTRELSZ/PLTREL/JMPREL/BIND_NOW */
    }
    if (shared->have_soname) {
        ++dynamic_entries;
    }
    if (dynamic_entries > SIZE_MAX / sizeof(Elf64_Dyn) ||
        !section_append_zero(dynamic,
                             dynamic_entries * sizeof(Elf64_Dyn))) {
        goto oom;
    }

    return true;

oom:
    fprintf(state->diagnostics, "minic-ld: out-of-memory:shared-metadata\n");
fail:
    shared_image_destroy(shared);
    return false;
}

static bool shared_fill_dynsym(MiniLdState *state,
                               const MiniLdStaticLayout *layout,
                               const MiniLdSharedImage *shared) {
    MiniLdSection *dynsym = &state->sections[shared->dynsym_section];
    size_t i;

    memset(dynsym->data, 0, dynsym->size);
    for (i = 0U; i < state->symbol_count; ++i) {
        MiniLdSymbol *input = &state->symbols[i];
        size_t dynamic_index = shared->dynsym_index[i];
        Elf64_Sym output;

        if (dynamic_index == SIZE_MAX) {
            continue;
        }
        memset(&output, 0, sizeof(output));
        output.st_name = shared->dynstr_name_offset[i];
        output.st_info = input->info;
        output.st_other = input->other;
        output.st_size = input->size;
        if (input->section == MINILD_SECTION_UNDEF) {
            output.st_shndx = SHN_UNDEF;
            output.st_value = 0U;
        } else if (input->section == MINILD_SECTION_ABS) {
            output.st_shndx = SHN_ABS;
            output.st_value = input->value;
        } else if (input->section >= 0 &&
                   (size_t)input->section < state->section_count) {
            output.st_shndx =
                (Elf64_Section)((size_t)input->section + 1U);
            output.st_value =
                layout->section_vaddr[input->section] + input->value;
        } else {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-symbol:%s\n",
                    input->name);
            return false;
        }
        memcpy(dynsym->data + dynamic_index * sizeof(output),
               &output,
               sizeof(output));
    }
    return true;
}

static bool shared_fill_hash(MiniLdState *state,
                             const MiniLdSharedImage *shared) {
    MiniLdSection *hash = &state->sections[shared->hash_section];
    size_t nbucket = shared->dynsym_count < 4U
                         ? 1U
                         : shared->dynsym_count / 4U;
    uint32_t *words = (uint32_t *)(void *)hash->data;
    uint32_t *buckets;
    uint32_t *chains;
    size_t i;

    if (nbucket == 0U) {
        nbucket = 1U;
    }
    memset(hash->data, 0, hash->size);
    words[0] = (uint32_t)nbucket;
    words[1] = (uint32_t)shared->dynsym_count;
    buckets = words + 2U;
    chains = buckets + nbucket;

    for (i = 0U; i < state->symbol_count; ++i) {
        size_t dynamic_index = shared->dynsym_index[i];
        uint32_t bucket;
        uint32_t *slot;

        if (dynamic_index == SIZE_MAX) {
            continue;
        }
        bucket = shared_elf_hash(state->symbols[i].name) % (uint32_t)nbucket;
        slot = &buckets[bucket];
        if (*slot == 0U) {
            *slot = (uint32_t)dynamic_index;
        } else {
            uint32_t cursor = *slot;
            while (chains[cursor] != 0U) {
                cursor = chains[cursor];
            }
            chains[cursor] = (uint32_t)dynamic_index;
        }
    }
    return true;
}

static bool shared_symbol_is_preemptible(const MiniLdSymbol *symbol) {
    unsigned bind = ELF64_ST_BIND(symbol->info);
    unsigned visibility = ELF64_ST_VISIBILITY(symbol->other);

    return (bind == STB_GLOBAL || bind == STB_WEAK) &&
           visibility == STV_DEFAULT;
}

static bool shared_fill_relocations(MiniLdState *state,
                                    const MiniLdStaticLayout *layout,
                                    const MiniLdSharedImage *shared) {
    MiniLdSection *rela = &state->sections[shared->rela_section];
    size_t write_index = 0U;
    size_t i;

    memset(rela->data, 0, rela->size);
    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *input = &state->relocs[i];
        MiniLdSection *source;
        MiniLdSymbol *target;
        Elf64_Rela output;
        size_t dynamic_index = 0U;

        if (input->section >= state->section_count ||
            (state->sections[input->section].flags & SHF_ALLOC) == 0U ||
            input->type == R_RISCV_NONE ||
            input->type == R_RISCV_RELAX ||
            input->type == R_RISCV_ALIGN ||
            input->type == R_RISCV_CALL ||
            input->type == R_RISCV_CALL_PLT) {
            continue;
        }
        if (input->type != R_RISCV_64 ||
            input->symbol == SIZE_MAX ||
            input->symbol >= state->symbol_count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-relocation\n");
            return false;
        }
        source = &state->sections[input->section];
        if (source->type == SHT_NOBITS ||
            input->offset > SIZE_MAX ||
            !range_ok((size_t)input->offset, 8U, source->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: shared-relocation-offset-out-of-range\n");
            return false;
        }
        target = &state->symbols[input->symbol];

        memset(&output, 0, sizeof(output));
        output.r_offset =
            layout->section_vaddr[input->section] + input->offset;
        if (target->section != MINILD_SECTION_UNDEF &&
            !shared_symbol_is_preemptible(target)) {
            uint64_t target_value;

            if (!static_resolve_symbol(state,
                                       layout,
                                       input->symbol,
                                       &target_value)) {
                return false;
            }
            output.r_info = ELF64_R_INFO(0U, R_RISCV_RELATIVE);
            output.r_addend = (Elf64_Sxword)((int64_t)target_value +
                                             input->addend);
        } else {
            dynamic_index = shared->dynsym_index[input->symbol];
            if (dynamic_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-dynsym-for-relocation:%s\n",
                        target->name);
                return false;
            }
            output.r_info =
                ELF64_R_INFO(dynamic_index, R_RISCV_64);
            output.r_addend = input->addend;
        }
        store_u64le(source->data + (size_t)input->offset, 0U);
        memcpy(rela->data + write_index * sizeof(output),
               &output,
               sizeof(output));
        ++write_index;
    }
    if (write_index != shared->rela_count) {
        fprintf(state->diagnostics,
                "minic-ld: shared-relocation-count-mismatch\n");
        return false;
    }
    return true;
}


static uint32_t shared_riscv_utype(unsigned opcode,
                                   unsigned rd,
                                   int64_t imm20) {
    return (((uint32_t)imm20 & UINT32_C(0xfffff)) << 12U) |
           ((uint32_t)rd << 7U) |
           (uint32_t)opcode;
}

static uint32_t shared_riscv_itype(unsigned opcode,
                                   unsigned rd,
                                   unsigned funct3,
                                   unsigned rs1,
                                   int64_t imm12) {
    return (((uint32_t)imm12 & UINT32_C(0xfff)) << 20U) |
           ((uint32_t)rs1 << 15U) |
           ((uint32_t)funct3 << 12U) |
           ((uint32_t)rd << 7U) |
           (uint32_t)opcode;
}

static uint32_t shared_riscv_rtype(unsigned opcode,
                                   unsigned rd,
                                   unsigned funct3,
                                   unsigned rs1,
                                   unsigned rs2,
                                   unsigned funct7) {
    return ((uint32_t)funct7 << 25U) |
           ((uint32_t)rs2 << 20U) |
           ((uint32_t)rs1 << 15U) |
           ((uint32_t)funct3 << 12U) |
           ((uint32_t)rd << 7U) |
           (uint32_t)opcode;
}

static bool shared_fill_plt(MiniLdState *state,
                            const MiniLdStaticLayout *layout,
                            const MiniLdSharedImage *shared) {
    MiniLdSection *plt;
    MiniLdSection *gotplt;
    MiniLdSection *rela_plt;
    uint64_t plt_address;
    uint64_t gotplt_address;
    int64_t delta;
    int64_t hi;
    int64_t lo;
    size_t i;

    if (shared->plt_count == 0U) {
        return true;
    }

    plt = &state->sections[shared->plt_section];
    gotplt = &state->sections[shared->gotplt_section];
    rela_plt = &state->sections[shared->rela_plt_section];
    plt_address = layout->section_vaddr[shared->plt_section];
    gotplt_address = layout->section_vaddr[shared->gotplt_section];

    if (plt->size != shared->plt_count * 16U ||
        gotplt->size != (3U + shared->plt_count) * 8U ||
        rela_plt->size != shared->plt_count * sizeof(Elf64_Rela)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-plt-size\n");
        return false;
    }

    /*
     * D019-proven eager RV64 PLT.  There is deliberately no lazy PLT0:
     *
     *   auipc t3, %pcrel_hi(slot)
     *   ld    t3, %pcrel_lo(slot)(t3)
     *   jalr  x0, t3, 0
     *   nop
     *
     * R_RISCV_JUMP_SLOT plus DT_BIND_NOW resolves each slot before first use.
     */
    memset(plt->data, 0, plt->size);
    memset(gotplt->data, 0, gotplt->size);
    memset(rela_plt->data, 0, rela_plt->size);

    for (i = 0U; i < state->symbol_count; ++i) {
        size_t plt_index = shared->plt_index[i];
        size_t plt_offset;
        size_t got_offset;
        uint64_t entry_address;
        uint64_t slot_address;
        Elf64_Rela relocation;
        size_t dynamic_index;

        if (plt_index == SIZE_MAX) {
            continue;
        }
        if (plt_index >= shared->plt_count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-plt-index\n");
            return false;
        }
        dynamic_index = shared->dynsym_index[i];
        if (dynamic_index == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-dynsym-for-jump-slot:%s\n",
                    state->symbols[i].name);
            return false;
        }

        plt_offset = plt_index * 16U;
        got_offset = (3U + plt_index) * 8U;
        entry_address = plt_address + plt_offset;
        slot_address = gotplt_address + got_offset;
        delta = (int64_t)slot_address - (int64_t)entry_address;
        hi = riscv_hi20(delta);
        lo = riscv_lo12(delta);

        store_u32le(plt->data + plt_offset + 0U,
                    shared_riscv_utype(0x17U, 28U, hi));
        store_u32le(plt->data + plt_offset + 4U,
                    shared_riscv_itype(0x03U, 28U, 3U, 28U, lo));
        store_u32le(plt->data + plt_offset + 8U,
                    shared_riscv_itype(0x67U, 0U, 0U, 28U, 0));
        store_u32le(plt->data + plt_offset + 12U, UINT32_C(0x00000013));

        memset(&relocation, 0, sizeof(relocation));
        relocation.r_offset = slot_address;
        relocation.r_info =
            ELF64_R_INFO(dynamic_index, R_RISCV_JUMP_SLOT);
        relocation.r_addend = 0;
        memcpy(rela_plt->data + plt_index * sizeof(relocation),
               &relocation,
               sizeof(relocation));
    }

    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *reloc = &state->relocs[i];
        MiniLdSection *source;
        MiniLdSymbol *target;
        uint64_t destination;

        if (reloc->type != R_RISCV_CALL &&
            reloc->type != R_RISCV_CALL_PLT) {
            continue;
        }
        if (reloc->section >= state->section_count ||
            (state->sections[reloc->section].flags & SHF_ALLOC) == 0U) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= state->symbol_count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-call-symbol\n");
            return false;
        }

        source = &state->sections[reloc->section];
        target = &state->symbols[reloc->symbol];
        if (shared_symbol_needs_plt(target)) {
            size_t plt_index = shared->plt_index[reloc->symbol];

            if (plt_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-plt-for-call:%s\n",
                        target->name);
                return false;
            }
            destination = plt_address + plt_index * 16U;
        } else {
            if (!static_resolve_symbol(state,
                                       layout,
                                       reloc->symbol,
                                       &destination)) {
                return false;
            }
            destination = (uint64_t)((int64_t)destination + reloc->addend);
        }

        delta = (int64_t)destination -
                (int64_t)(layout->section_vaddr[reloc->section] +
                          reloc->offset);
        if (!static_patch_utype(source,
                                reloc->offset,
                                riscv_hi20(delta),
                                state->diagnostics) ||
            !static_patch_itype(source,
                                reloc->offset + 4U,
                                riscv_lo12(delta),
                                state->diagnostics)) {
            return false;
        }
    }

    return true;
}

static bool shared_fill_dynamic(MiniLdState *state,
                                const MiniLdStaticLayout *layout,
                                const MiniLdSharedImage *shared) {
    MiniLdSection *dynamic = &state->sections[shared->dynamic_section];
    Elf64_Dyn *entries = (Elf64_Dyn *)(void *)dynamic->data;
    size_t index = 0U;

#define MINILD_DYN(TAG, VALUE)          \
    do {                                \
        entries[index].d_tag = (TAG);   \
        entries[index].d_un.d_val = (VALUE); \
        ++index;                        \
    } while (0)

    memset(dynamic->data, 0, dynamic->size);
    MINILD_DYN(DT_HASH, layout->section_vaddr[shared->hash_section]);
    MINILD_DYN(DT_STRTAB, layout->section_vaddr[shared->dynstr_section]);
    MINILD_DYN(DT_SYMTAB, layout->section_vaddr[shared->dynsym_section]);
    MINILD_DYN(DT_STRSZ, state->sections[shared->dynstr_section].size);
    MINILD_DYN(DT_SYMENT, sizeof(Elf64_Sym));
    if (shared->rela_count != 0U) {
        MINILD_DYN(DT_RELA, layout->section_vaddr[shared->rela_section]);
        MINILD_DYN(DT_RELASZ, state->sections[shared->rela_section].size);
        MINILD_DYN(DT_RELAENT, sizeof(Elf64_Rela));
    }
    if (shared->plt_count != 0U) {
        MINILD_DYN(DT_PLTGOT, layout->section_vaddr[shared->gotplt_section]);
        MINILD_DYN(DT_PLTRELSZ, state->sections[shared->rela_plt_section].size);
        MINILD_DYN(DT_PLTREL, DT_RELA);
        MINILD_DYN(DT_JMPREL, layout->section_vaddr[shared->rela_plt_section]);
        MINILD_DYN(DT_BIND_NOW, 0U);
    }
    if (shared->have_soname) {
        MINILD_DYN(DT_SONAME, shared->soname_offset);
    }
    MINILD_DYN(DT_NULL, 0U);

#undef MINILD_DYN

    if (index * sizeof(Elf64_Dyn) != dynamic->size) {
        fprintf(state->diagnostics,
                "minic-ld: shared-dynamic-size-mismatch\n");
        return false;
    }
    return true;
}

static bool shared_write_object(MiniLdState *state,
                                const MiniLdStaticLayout *layout,
                                const MiniLdSharedImage *shared,
                                const char *path) {
    size_t phnum = (layout->have_rx ? 1U : 0U) +
                   (layout->have_rw ? 1U : 0U) + 1U;
    size_t image_size = sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr);
    MiniLdBuffer shstrtab = {NULL, 0U, 0U};
    uint32_t *name_offsets = NULL;
    Elf64_Shdr *section_headers = NULL;
    unsigned char *image = NULL;
    size_t section_count = state->section_count + 2U; /* null + shstrtab */
    size_t shstrtab_index = state->section_count + 1U;
    size_t shstrtab_offset;
    size_t section_header_offset;
    size_t total_size;
    size_t i;
    size_t program_index = 0U;
    Elf64_Ehdr header;
    Elf64_Phdr *programs;
    FILE *file = NULL;
    bool ok = false;

    if (section_count > UINT16_MAX) {
        fprintf(state->diagnostics,
                "minic-ld: too-many-shared-sections\n");
        return false;
    }
    if (layout->have_rx &&
        layout->rx_file_offset + layout->rx_file_size > image_size) {
        image_size = layout->rx_file_offset + layout->rx_file_size;
    }
    if (layout->have_rw &&
        layout->rw_file_offset + layout->rw_file_size > image_size) {
        image_size = layout->rw_file_offset + layout->rw_file_size;
    }
    shstrtab_offset = image_size;

    name_offsets =
        calloc(state->section_count == 0U ? 1U : state->section_count,
               sizeof(*name_offsets));
    if (name_offsets == NULL ||
        !buffer_append_zero(&shstrtab, 1U)) {
        goto oom;
    }
    for (i = 0U; i < state->section_count; ++i) {
        if (!buffer_append_string(&shstrtab,
                                  state->sections[i].name,
                                  &name_offsets[i])) {
            goto oom;
        }
    }
    {
        uint32_t ignored;
        if (!buffer_append_string(&shstrtab, ".shstrtab", &ignored)) {
            goto oom;
        }
    }
    if (!add_size(shstrtab_offset, shstrtab.size, &section_header_offset) ||
        !align_up_size(section_header_offset, 8U, &section_header_offset) ||
        section_count > SIZE_MAX / sizeof(Elf64_Shdr) ||
        !add_size(section_header_offset,
                  section_count * sizeof(Elf64_Shdr),
                  &total_size)) {
        goto oom;
    }

    image = calloc(total_size == 0U ? 1U : total_size, 1U);
    section_headers = calloc(section_count, sizeof(*section_headers));
    if (image == NULL || section_headers == NULL) {
        goto oom;
    }

    memset(&header, 0, sizeof(header));
    memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_type = ET_DYN;
    header.e_machine = EM_RISCV;
    header.e_version = EV_CURRENT;
    header.e_entry = 0U;
    header.e_phoff = sizeof(Elf64_Ehdr);
    header.e_shoff = section_header_offset;
    header.e_flags = state->elf_flags;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = (Elf64_Half)phnum;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shnum = (Elf64_Half)section_count;
    header.e_shstrndx = (Elf64_Half)shstrtab_index;
    memcpy(image, &header, sizeof(header));

    programs = (Elf64_Phdr *)(void *)(image + sizeof(Elf64_Ehdr));
    if (layout->have_rx) {
        Elf64_Phdr *ph = &programs[program_index++];
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
        ph->p_type = PT_LOAD;
        ph->p_flags = PF_R | PF_W;
        ph->p_offset = layout->rw_file_offset;
        ph->p_vaddr = layout->rw_vaddr;
        ph->p_paddr = layout->rw_vaddr;
        ph->p_filesz = layout->rw_file_size;
        ph->p_memsz = layout->rw_mem_size;
        ph->p_align = 4096U;
    }
    {
        Elf64_Phdr *ph = &programs[program_index++];
        MiniLdSection *dynamic = &state->sections[shared->dynamic_section];
        size_t offset = layout->section_file_offset[shared->dynamic_section];

        if (offset == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-shared-dynamic-file-offset\n");
            goto done;
        }
        ph->p_type = PT_DYNAMIC;
        ph->p_flags = PF_R | PF_W;
        ph->p_offset = offset;
        ph->p_vaddr = layout->section_vaddr[shared->dynamic_section];
        ph->p_paddr = ph->p_vaddr;
        ph->p_filesz = dynamic->size;
        ph->p_memsz = dynamic->size;
        ph->p_align = 8U;
    }

    for (i = 0U; i < state->section_count; ++i) {
        MiniLdSection *section = &state->sections[i];
        Elf64_Shdr *sh = &section_headers[i + 1U];
        size_t offset = layout->section_file_offset[i];

        sh->sh_name = name_offsets[i];
        sh->sh_type = section->type;
        sh->sh_flags = section->flags;
        sh->sh_addr =
            (section->flags & SHF_ALLOC) != 0U
                ? layout->section_vaddr[i]
                : 0U;
        sh->sh_offset =
            section->type == SHT_NOBITS || offset == SIZE_MAX
                ? 0U
                : offset;
        sh->sh_size = section->size;
        sh->sh_addralign = section->align;
        sh->sh_entsize = section->entsize;
        if (i == shared->dynsym_section) {
            sh->sh_link = (Elf64_Word)(shared->dynstr_section + 1U);
            sh->sh_info = 1U;
        } else if (i == shared->hash_section) {
            sh->sh_link = (Elf64_Word)(shared->dynsym_section + 1U);
        } else if (i == shared->rela_section ||
                   i == shared->rela_plt_section) {
            sh->sh_link = (Elf64_Word)(shared->dynsym_section + 1U);
        } else if (i == shared->dynamic_section) {
            sh->sh_link = (Elf64_Word)(shared->dynstr_section + 1U);
        }

        if ((section->flags & SHF_ALLOC) != 0U &&
            section->type != SHT_NOBITS &&
            section->size != 0U) {
            if (offset == SIZE_MAX ||
                !range_ok(offset, section->size, total_size)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-shared-section-layout:%s\n",
                        section->name);
                goto done;
            }
            memcpy(image + offset, section->data, section->size);
        }
    }

    {
        Elf64_Shdr *sh = &section_headers[shstrtab_index];
        uint32_t shstrtab_name = 0U;
        size_t cursor = 1U;

        for (i = 0U; i < state->section_count; ++i) {
            cursor += strlen(state->sections[i].name) + 1U;
        }
        if (cursor > UINT32_MAX) {
            goto oom;
        }
        shstrtab_name = (uint32_t)cursor;
        sh->sh_name = shstrtab_name;
        sh->sh_type = SHT_STRTAB;
        sh->sh_offset = shstrtab_offset;
        sh->sh_size = shstrtab.size;
        sh->sh_addralign = 1U;
    }

    memcpy(image + shstrtab_offset, shstrtab.data, shstrtab.size);
    memcpy(image + section_header_offset,
           section_headers,
           section_count * sizeof(*section_headers));

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: cannot-create:%s:%s\n",
                path,
                strerror(errno));
        goto done;
    }
    if (fwrite(image, 1U, total_size, file) != total_size ||
        fflush(file) != 0) {
        fprintf(state->diagnostics,
                "minic-ld: write-error:%s\n",
                path);
        goto done;
    }
    ok = true;

done:
    if (file != NULL && fclose(file) != 0) {
        ok = false;
    }
    if (ok && chmod(path, 0755) != 0) {
        fprintf(state->diagnostics,
                "minic-ld: chmod-error:%s:%s\n",
                path,
                strerror(errno));
        ok = false;
    }
    if (!ok) {
        (void)remove(path);
    }
    free(name_offsets);
    free(section_headers);
    free(image);
    free(shstrtab.data);
    return ok;

oom:
    fprintf(state->diagnostics,
            "minic-ld: out-of-memory:shared-writer\n");
    goto done;
}

int minild_link_shared_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *soname,
                                          FILE *diagnostics) {
    MiniLdState state;
    MiniLdStaticLayout layout;
    MiniLdSharedImage shared;
    bool layout_ready = false;
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    memset(&layout, 0, sizeof(layout));
    memset(&shared, 0, sizeof(shared));
    state.diagnostics = diagnostics;

    if (!process_input_sequence(&state, inputs, input_count) ||
        !state.have_input ||
        !static_allocate_common(&state) ||
        !shared_prepare_metadata(&state, soname, &shared) ||
        !static_build_layout(&state, &layout)) {
        goto done;
    }
    layout_ready = true;

    if (!shared_fill_dynsym(&state, &layout, &shared) ||
        !shared_fill_hash(&state, &shared) ||
        !shared_fill_relocations(&state, &layout, &shared) ||
        !shared_fill_plt(&state, &layout, &shared) ||
        !shared_fill_dynamic(&state, &layout, &shared) ||
        !shared_write_object(&state, &layout, &shared, output_path)) {
        goto done;
    }
    ok = true;

done:
    if (layout_ready) {
        static_layout_destroy(&layout);
    }
    shared_image_destroy(&shared);
    state_destroy(&state);
    return ok ? 0 : 1;
}

int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics) {
    MiniLdState state;
    MiniLdStaticLayout layout;
    MiniLdStaticGot got;
    uint64_t entry = 0U;
    bool layout_ready = false;
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        entry_symbol == NULL || diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    memset(&layout, 0, sizeof(layout));
    memset(&got, 0, sizeof(got));
    got.section = SIZE_MAX;
    state.diagnostics = diagnostics;

    if (!process_input_sequence(&state, inputs, input_count)) {
        goto done;
    }

    if (!state.have_input ||
        !static_allocate_common(&state) ||
        !static_build_got(&state, &got) ||
        !static_build_layout(&state, &layout)) {
        goto done;
    }
    layout_ready = true;
    if (!static_synthesize_runtime_boundaries(&state, &layout) ||
        !static_fill_got(&state, &layout, &got) ||
        !static_apply_relocations(&state, &layout, &got) ||
        !static_entry_address(&state, &layout, entry_symbol, &entry) ||
        !static_write_executable(&state, &layout, output_path, entry)) {
        goto done;
    }
    ok = true;

done:
    if (layout_ready) {
        static_layout_destroy(&layout);
    }
    static_got_destroy(&got);
    state_destroy(&state);
    return ok ? 0 : 1;
}

int minild_link_relocatable_elf64_riscv_inputs(const char *output_path,
                                               const MiniLdInput *inputs,
                                               size_t input_count,
                                               FILE *diagnostics) {
    MiniLdState state;
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    state.diagnostics = diagnostics;

    if (!process_input_sequence(&state, inputs, input_count)) {
        goto done;
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
