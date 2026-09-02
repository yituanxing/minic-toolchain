#include "minielf.h"

#include <elf.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct MiniElfBuffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} MiniElfBuffer;

typedef struct MiniElfOutputSection {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_size;
} MiniElfOutputSection;

static bool add_size(size_t a, size_t b, size_t *out) {
    if (b > SIZE_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool align_up(size_t value, uint64_t alignment, size_t *out) {
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

static bool buffer_reserve(MiniElfBuffer *buffer, size_t extra) {
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

static bool buffer_append(MiniElfBuffer *buffer,
                          const void *data,
                          size_t size) {
    if (!buffer_reserve(buffer, size)) {
        return false;
    }
    if (size != 0U) {
        memcpy(buffer->data + buffer->size, data, size);
    }
    buffer->size += size;
    return true;
}

static bool buffer_append_zero(MiniElfBuffer *buffer, size_t size) {
    if (!buffer_reserve(buffer, size)) {
        return false;
    }
    if (size != 0U) {
        memset(buffer->data + buffer->size, 0, size);
    }
    buffer->size += size;
    return true;
}

static bool buffer_append_string(MiniElfBuffer *buffer,
                                 const char *text,
                                 uint32_t *offset_out) {
    size_t size;

    if (text == NULL || buffer->size > UINT32_MAX) {
        return false;
    }
    size = strlen(text) + 1U;
    *offset_out = (uint32_t)buffer->size;
    return buffer_append(buffer, text, size);
}

static void store_u16(unsigned char *data,
                      unsigned char encoding,
                      uint16_t value) {
    if (encoding == ELFDATA2MSB) {
        data[0] = (unsigned char)((value >> 8U) & UINT16_C(0xff));
        data[1] = (unsigned char)(value & UINT16_C(0xff));
    } else {
        data[0] = (unsigned char)(value & UINT16_C(0xff));
        data[1] = (unsigned char)((value >> 8U) & UINT16_C(0xff));
    }
}

static void store_u32(unsigned char *data,
                      unsigned char encoding,
                      uint32_t value) {
    size_t i;

    if (encoding == ELFDATA2MSB) {
        for (i = 0U; i < 4U; ++i) {
            data[i] = (unsigned char)((value >> ((3U - i) * 8U)) & 0xffU);
        }
    } else {
        for (i = 0U; i < 4U; ++i) {
            data[i] = (unsigned char)((value >> (i * 8U)) & 0xffU);
        }
    }
}

static void store_u64(unsigned char *data,
                      unsigned char encoding,
                      uint64_t value) {
    size_t i;

    if (encoding == ELFDATA2MSB) {
        for (i = 0U; i < 8U; ++i) {
            data[i] = (unsigned char)((value >> ((7U - i) * 8U)) & 0xffU);
        }
    } else {
        for (i = 0U; i < 8U; ++i) {
            data[i] = (unsigned char)((value >> (i * 8U)) & 0xffU);
        }
    }
}

static bool store_offset(unsigned char *data,
                         const MiniElfRelocatableSpec *spec,
                         uint64_t value) {
    if (spec->elf_class == ELFCLASS64) {
        store_u64(data, spec->data_encoding, value);
        return true;
    }
    if (value > UINT32_MAX) {
        return false;
    }
    store_u32(data, spec->data_encoding, (uint32_t)value);
    return true;
}

static void set_error(MiniElfWriteError *error_out, MiniElfWriteError error) {
    if (error_out != NULL) {
        *error_out = error;
    }
}

static bool write_section_header(unsigned char *data,
                                 const MiniElfRelocatableSpec *spec,
                                 const MiniElfOutputSection *section) {
    memset(data, 0, spec->elf_class == ELFCLASS64 ? 64U : 40U);
    store_u32(data, spec->data_encoding, section->name);
    store_u32(data + 4U, spec->data_encoding, section->type);
    if (spec->elf_class == ELFCLASS64) {
        store_u64(data + 8U, spec->data_encoding, section->flags);
        store_u64(data + 16U, spec->data_encoding, section->address);
        store_u64(data + 24U, spec->data_encoding, section->offset);
        store_u64(data + 32U, spec->data_encoding, section->size);
        store_u32(data + 40U, spec->data_encoding, section->link);
        store_u32(data + 44U, spec->data_encoding, section->info);
        store_u64(data + 48U, spec->data_encoding, section->alignment);
        store_u64(data + 56U, spec->data_encoding, section->entry_size);
        return true;
    }
    if (section->flags > UINT32_MAX ||
        section->address > UINT32_MAX ||
        section->offset > UINT32_MAX ||
        section->size > UINT32_MAX ||
        section->alignment > UINT32_MAX ||
        section->entry_size > UINT32_MAX) {
        return false;
    }
    store_u32(data + 8U, spec->data_encoding, (uint32_t)section->flags);
    store_u32(data + 12U, spec->data_encoding, (uint32_t)section->address);
    store_u32(data + 16U, spec->data_encoding, (uint32_t)section->offset);
    store_u32(data + 20U, spec->data_encoding, (uint32_t)section->size);
    store_u32(data + 24U, spec->data_encoding, section->link);
    store_u32(data + 28U, spec->data_encoding, section->info);
    store_u32(data + 32U, spec->data_encoding, (uint32_t)section->alignment);
    store_u32(data + 36U, spec->data_encoding, (uint32_t)section->entry_size);
    return true;
}

static bool write_symbol(unsigned char *data,
                         const MiniElfRelocatableSpec *spec,
                         uint32_t name,
                         unsigned char info,
                         unsigned char other,
                         uint16_t section_index,
                         uint64_t value,
                         uint64_t size) {
    if (spec->elf_class == ELFCLASS64) {
        memset(data, 0, 24U);
        store_u32(data, spec->data_encoding, name);
        data[4U] = info;
        data[5U] = other;
        store_u16(data + 6U, spec->data_encoding, section_index);
        store_u64(data + 8U, spec->data_encoding, value);
        store_u64(data + 16U, spec->data_encoding, size);
        return true;
    }
    if (value > UINT32_MAX || size > UINT32_MAX) {
        return false;
    }
    memset(data, 0, 16U);
    store_u32(data, spec->data_encoding, name);
    store_u32(data + 4U, spec->data_encoding, (uint32_t)value);
    store_u32(data + 8U, spec->data_encoding, (uint32_t)size);
    data[12U] = info;
    data[13U] = other;
    store_u16(data + 14U, spec->data_encoding, section_index);
    return true;
}

static bool write_rela(unsigned char *data,
                       const MiniElfRelocatableSpec *spec,
                       uint64_t offset,
                       size_t symbol_index,
                       uint32_t type,
                       int64_t addend) {
    if (spec->elf_class == ELFCLASS64) {
        uint64_t info;

        if (symbol_index > UINT32_MAX) {
            return false;
        }
        info = ((uint64_t)symbol_index << 32U) | (uint64_t)type;
        store_u64(data, spec->data_encoding, offset);
        store_u64(data + 8U, spec->data_encoding, info);
        store_u64(data + 16U, spec->data_encoding, (uint64_t)addend);
        return true;
    }
    if (offset > UINT32_MAX ||
        symbol_index > UINT32_C(0x00ffffff) ||
        type > UINT32_C(0xff) ||
        addend < INT32_MIN || addend > INT32_MAX) {
        return false;
    }
    store_u32(data, spec->data_encoding, (uint32_t)offset);
    store_u32(data + 4U,
              spec->data_encoding,
              ((uint32_t)symbol_index << 8U) | type);
    store_u32(data + 8U, spec->data_encoding, (uint32_t)addend);
    return true;
}

const char *minielf_write_error_string(MiniElfWriteError error) {
    switch (error) {
    case MINIELF_WRITE_OK:
        return "ok";
    case MINIELF_WRITE_INVALID_ARGUMENT:
        return "invalid-argument";
    case MINIELF_WRITE_OUT_OF_MEMORY:
        return "out-of-memory";
    case MINIELF_WRITE_LIMIT:
        return "format-limit";
    case MINIELF_WRITE_INVALID_SECTION:
        return "invalid-section";
    case MINIELF_WRITE_INVALID_SYMBOL:
        return "invalid-symbol";
    case MINIELF_WRITE_INVALID_RELOCATION:
        return "invalid-relocation";
    }
    return "unknown";
}

bool minielf_build_relocatable(const MiniElfRelocatableSpec *spec,
                               unsigned char **image_out,
                               size_t *size_out,
                               MiniElfWriteError *error_out) {
    size_t *relocation_counts = NULL;
    size_t *relocation_section_indices = NULL;
    size_t *relocation_write_counts = NULL;
    size_t *symbol_output_indices = NULL;
    uint32_t *section_name_offsets = NULL;
    uint32_t *relocation_name_offsets = NULL;
    uint32_t *symbol_name_offsets = NULL;
    MiniElfOutputSection *headers = NULL;
    MiniElfBuffer strtab = {NULL, 0U, 0U};
    MiniElfBuffer shstrtab = {NULL, 0U, 0U};
    unsigned char *image = NULL;
    size_t relocation_section_count = 0U;
    size_t symbol_count;
    size_t output_section_count;
    size_t symtab_index;
    size_t strtab_index;
    size_t shstrtab_index;
    size_t first_user_symbol;
    size_t local_count;
    size_t cursor;
    size_t section_header_offset;
    size_t total_size;
    size_t ehdr_size;
    size_t shdr_size;
    size_t sym_size;
    size_t rela_size;
    size_t word_align;
    size_t i;
    bool ok = false;

    if (error_out != NULL) {
        *error_out = MINIELF_WRITE_OK;
    }
    if (spec == NULL || image_out == NULL || size_out == NULL ||
        (spec->elf_class != ELFCLASS32 && spec->elf_class != ELFCLASS64) ||
        (spec->data_encoding != ELFDATA2LSB &&
         spec->data_encoding != ELFDATA2MSB) ||
        (spec->section_count != 0U && spec->sections == NULL) ||
        (spec->symbol_count != 0U && spec->symbols == NULL) ||
        (spec->relocation_count != 0U && spec->relocations == NULL)) {
        set_error(error_out, MINIELF_WRITE_INVALID_ARGUMENT);
        return false;
    }
    *image_out = NULL;
    *size_out = 0U;

    ehdr_size = spec->elf_class == ELFCLASS64 ? 64U : 52U;
    shdr_size = spec->elf_class == ELFCLASS64 ? 64U : 40U;
    sym_size = spec->elf_class == ELFCLASS64 ? 24U : 16U;
    rela_size = spec->elf_class == ELFCLASS64 ? 24U : 12U;
    word_align = spec->elf_class == ELFCLASS64 ? 8U : 4U;

    if (spec->section_count > UINT16_MAX - 4U ||
        spec->section_count > SIZE_MAX - 3U) {
        set_error(error_out, MINIELF_WRITE_LIMIT);
        goto done;
    }

    relocation_counts =
        calloc(spec->section_count == 0U ? 1U : spec->section_count,
               sizeof(*relocation_counts));
    relocation_section_indices =
        malloc((spec->section_count == 0U ? 1U : spec->section_count) *
               sizeof(*relocation_section_indices));
    relocation_write_counts =
        calloc(spec->section_count == 0U ? 1U : spec->section_count,
               sizeof(*relocation_write_counts));
    section_name_offsets =
        calloc(spec->section_count == 0U ? 1U : spec->section_count,
               sizeof(*section_name_offsets));
    relocation_name_offsets =
        calloc(spec->section_count == 0U ? 1U : spec->section_count,
               sizeof(*relocation_name_offsets));
    symbol_output_indices =
        calloc(spec->symbol_count == 0U ? 1U : spec->symbol_count,
               sizeof(*symbol_output_indices));
    symbol_name_offsets =
        calloc(spec->symbol_count == 0U ? 1U : spec->symbol_count,
               sizeof(*symbol_name_offsets));
    if (relocation_counts == NULL || relocation_section_indices == NULL ||
        relocation_write_counts == NULL || section_name_offsets == NULL ||
        relocation_name_offsets == NULL || symbol_output_indices == NULL ||
        symbol_name_offsets == NULL) {
        set_error(error_out, MINIELF_WRITE_OUT_OF_MEMORY);
        goto done;
    }
    for (i = 0U; i < spec->section_count; ++i) {
        relocation_section_indices[i] = SIZE_MAX;
    }

    for (i = 0U; i < spec->relocation_count; ++i) {
        const MiniElfWriteRela *rela = &spec->relocations[i];

        if (rela->target_section >= spec->section_count ||
            (rela->symbol_index != SIZE_MAX &&
             rela->symbol_index >= spec->symbol_count)) {
            set_error(error_out, MINIELF_WRITE_INVALID_RELOCATION);
            goto done;
        }
        if (relocation_counts[rela->target_section] == SIZE_MAX) {
            set_error(error_out, MINIELF_WRITE_LIMIT);
            goto done;
        }
        ++relocation_counts[rela->target_section];
    }
    for (i = 0U; i < spec->section_count; ++i) {
        if (relocation_counts[i] != 0U) {
            ++relocation_section_count;
        }
    }

    if (spec->section_count >
            SIZE_MAX - relocation_section_count - 3U) {
        set_error(error_out, MINIELF_WRITE_LIMIT);
        goto done;
    }
    output_section_count =
        spec->section_count + relocation_section_count + 3U;
    if (output_section_count + 1U > UINT16_MAX) {
        set_error(error_out, MINIELF_WRITE_LIMIT);
        goto done;
    }

    headers = calloc(output_section_count + 1U, sizeof(*headers));
    if (headers == NULL ||
        !buffer_append_zero(&strtab, 1U) ||
        !buffer_append_zero(&shstrtab, 1U)) {
        set_error(error_out, MINIELF_WRITE_OUT_OF_MEMORY);
        goto done;
    }

    for (i = 0U; i < spec->section_count; ++i) {
        const MiniElfWriteSection *section = &spec->sections[i];

        if (section->name == NULL ||
            (section->type != SHT_NOBITS &&
             section->size != 0U && section->data == NULL) ||
            !buffer_append_string(&shstrtab,
                                  section->name,
                                  &section_name_offsets[i])) {
            set_error(error_out, MINIELF_WRITE_INVALID_SECTION);
            goto done;
        }
    }

    {
        size_t ordinal = 0U;

        for (i = 0U; i < spec->section_count; ++i) {
            if (relocation_counts[i] != 0U) {
                const char *section_name = spec->sections[i].name;
                size_t section_name_size = strlen(section_name);
                char *name;

                if (section_name_size > SIZE_MAX - 6U) {
                    set_error(error_out, MINIELF_WRITE_LIMIT);
                    goto done;
                }
                name = malloc(section_name_size + 6U);
                if (name == NULL) {
                    set_error(error_out, MINIELF_WRITE_OUT_OF_MEMORY);
                    goto done;
                }
                memcpy(name, ".rela", 5U);
                memcpy(name + 5U, section_name, section_name_size + 1U);
                if (!buffer_append_string(&shstrtab,
                                          name,
                                          &relocation_name_offsets[i])) {
                    free(name);
                    set_error(error_out, MINIELF_WRITE_OUT_OF_MEMORY);
                    goto done;
                }
                free(name);
                relocation_section_indices[i] =
                    1U + spec->section_count + ordinal++;
            }
        }
    }

    for (i = 0U; i < spec->symbol_count; ++i) {
        const MiniElfWriteSymbol *symbol = &spec->symbols[i];

        if (symbol->name == NULL) {
            set_error(error_out, MINIELF_WRITE_INVALID_SYMBOL);
            goto done;
        }
        if (symbol->name[0] != '\0' &&
            !buffer_append_string(&strtab,
                                  symbol->name,
                                  &symbol_name_offsets[i])) {
            set_error(error_out, MINIELF_WRITE_OUT_OF_MEMORY);
            goto done;
        }
    }

    symtab_index = 1U + spec->section_count + relocation_section_count;
    strtab_index = symtab_index + 1U;
    shstrtab_index = symtab_index + 2U;
    {
        uint32_t symtab_name;
        uint32_t strtab_name;
        uint32_t shstrtab_name;

        if (!buffer_append_string(&shstrtab, ".symtab", &symtab_name) ||
            !buffer_append_string(&shstrtab, ".strtab", &strtab_name) ||
            !buffer_append_string(&shstrtab, ".shstrtab", &shstrtab_name)) {
            set_error(error_out, MINIELF_WRITE_OUT_OF_MEMORY);
            goto done;
        }
        headers[symtab_index].name = symtab_name;
        headers[strtab_index].name = strtab_name;
        headers[shstrtab_index].name = shstrtab_name;
    }

    if (spec->emit_section_symbols &&
        spec->section_count > SIZE_MAX - 1U) {
        set_error(error_out, MINIELF_WRITE_LIMIT);
        goto done;
    }
    first_user_symbol = 1U + (spec->emit_section_symbols
                                 ? spec->section_count
                                 : 0U);
    if (spec->symbol_count > SIZE_MAX - first_user_symbol) {
        set_error(error_out, MINIELF_WRITE_LIMIT);
        goto done;
    }
    symbol_count = first_user_symbol + spec->symbol_count;

    {
        size_t next = first_user_symbol;

        for (i = 0U; i < spec->symbol_count; ++i) {
            if (minielf_symbol_bind(spec->symbols[i].info) == STB_LOCAL) {
                symbol_output_indices[i] = next++;
            }
        }
        local_count = next;
        for (i = 0U; i < spec->symbol_count; ++i) {
            if (minielf_symbol_bind(spec->symbols[i].info) != STB_LOCAL) {
                symbol_output_indices[i] = next++;
            }
        }
        if (next != symbol_count) {
            set_error(error_out, MINIELF_WRITE_INVALID_SYMBOL);
            goto done;
        }
    }

    cursor = ehdr_size;
    for (i = 0U; i < spec->section_count; ++i) {
        const MiniElfWriteSection *section = &spec->sections[i];
        uint64_t alignment = section->alignment == 0U
                                 ? 1U
                                 : section->alignment;
        size_t aligned;

        if (!align_up(cursor, alignment, &aligned)) {
            set_error(error_out, MINIELF_WRITE_LIMIT);
            goto done;
        }
        cursor = aligned;
        headers[i + 1U].name = section_name_offsets[i];
        headers[i + 1U].type = section->type;
        headers[i + 1U].flags = section->flags;
        headers[i + 1U].offset = cursor;
        headers[i + 1U].size = section->size;
        headers[i + 1U].alignment = alignment;
        headers[i + 1U].entry_size = section->entry_size;
        if (section->type != SHT_NOBITS &&
            !add_size(cursor, section->size, &cursor)) {
            set_error(error_out, MINIELF_WRITE_LIMIT);
            goto done;
        }
    }

    for (i = 0U; i < spec->section_count; ++i) {
        size_t count = relocation_counts[i];
        size_t index;
        size_t aligned;

        if (count == 0U) {
            continue;
        }
        index = relocation_section_indices[i];
        if (!align_up(cursor, word_align, &aligned) ||
            count > SIZE_MAX / rela_size) {
            set_error(error_out, MINIELF_WRITE_LIMIT);
            goto done;
        }
        cursor = aligned;
        headers[index].name = relocation_name_offsets[i];
        headers[index].type = SHT_RELA;
        headers[index].flags = SHF_INFO_LINK;
        headers[index].offset = cursor;
        headers[index].size = count * rela_size;
        headers[index].link = (uint32_t)symtab_index;
        headers[index].info = (uint32_t)(i + 1U);
        headers[index].alignment = word_align;
        headers[index].entry_size = rela_size;
        if (!add_size(cursor, count * rela_size, &cursor)) {
            set_error(error_out, MINIELF_WRITE_LIMIT);
            goto done;
        }
    }

    {
        size_t aligned;

        if (!align_up(cursor, word_align, &aligned) ||
            symbol_count > SIZE_MAX / sym_size) {
            set_error(error_out, MINIELF_WRITE_LIMIT);
            goto done;
        }
        cursor = aligned;
    }
    headers[symtab_index].type = SHT_SYMTAB;
    headers[symtab_index].offset = cursor;
    headers[symtab_index].size = symbol_count * sym_size;
    headers[symtab_index].link = (uint32_t)strtab_index;
    headers[symtab_index].info = (uint32_t)local_count;
    headers[symtab_index].alignment = word_align;
    headers[symtab_index].entry_size = sym_size;
    if (!add_size(cursor, symbol_count * sym_size, &cursor)) {
        set_error(error_out, MINIELF_WRITE_LIMIT);
        goto done;
    }

    headers[strtab_index].type = SHT_STRTAB;
    headers[strtab_index].offset = cursor;
    headers[strtab_index].size = strtab.size;
    headers[strtab_index].alignment = 1U;
    if (!add_size(cursor, strtab.size, &cursor)) {
        set_error(error_out, MINIELF_WRITE_LIMIT);
        goto done;
    }

    headers[shstrtab_index].type = SHT_STRTAB;
    headers[shstrtab_index].offset = cursor;
    headers[shstrtab_index].size = shstrtab.size;
    headers[shstrtab_index].alignment = 1U;
    if (!add_size(cursor, shstrtab.size, &cursor) ||
        !align_up(cursor, word_align, &section_header_offset) ||
        output_section_count + 1U >
            (SIZE_MAX - section_header_offset) / shdr_size) {
        set_error(error_out, MINIELF_WRITE_LIMIT);
        goto done;
    }
    total_size =
        section_header_offset + (output_section_count + 1U) * shdr_size;
    if (spec->elf_class == ELFCLASS32 &&
        (section_header_offset > UINT32_MAX || total_size > UINT32_MAX)) {
        set_error(error_out, MINIELF_WRITE_LIMIT);
        goto done;
    }

    image = calloc(total_size == 0U ? 1U : total_size, 1U);
    if (image == NULL) {
        set_error(error_out, MINIELF_WRITE_OUT_OF_MEMORY);
        goto done;
    }

    memcpy(image, ELFMAG, SELFMAG);
    image[EI_CLASS] = spec->elf_class;
    image[EI_DATA] = spec->data_encoding;
    image[EI_VERSION] = EV_CURRENT;
    image[EI_OSABI] = ELFOSABI_NONE;
    store_u16(image + 16U, spec->data_encoding, ET_REL);
    store_u16(image + 18U, spec->data_encoding, spec->machine);
    store_u32(image + 20U, spec->data_encoding, EV_CURRENT);
    if (spec->elf_class == ELFCLASS64) {
        store_u64(image + 24U, spec->data_encoding, 0U);
        store_u64(image + 32U, spec->data_encoding, 0U);
        store_u64(image + 40U,
                  spec->data_encoding,
                  section_header_offset);
        store_u32(image + 48U, spec->data_encoding, spec->flags);
        store_u16(image + 52U, spec->data_encoding, (uint16_t)ehdr_size);
        store_u16(image + 54U, spec->data_encoding, 0U);
        store_u16(image + 56U, spec->data_encoding, 0U);
        store_u16(image + 58U, spec->data_encoding, (uint16_t)shdr_size);
        store_u16(image + 60U,
                  spec->data_encoding,
                  (uint16_t)(output_section_count + 1U));
        store_u16(image + 62U,
                  spec->data_encoding,
                  (uint16_t)shstrtab_index);
    } else {
        store_u32(image + 24U, spec->data_encoding, 0U);
        store_u32(image + 28U, spec->data_encoding, 0U);
        store_u32(image + 32U,
                  spec->data_encoding,
                  (uint32_t)section_header_offset);
        store_u32(image + 36U, spec->data_encoding, spec->flags);
        store_u16(image + 40U, spec->data_encoding, (uint16_t)ehdr_size);
        store_u16(image + 42U, spec->data_encoding, 0U);
        store_u16(image + 44U, spec->data_encoding, 0U);
        store_u16(image + 46U, spec->data_encoding, (uint16_t)shdr_size);
        store_u16(image + 48U,
                  spec->data_encoding,
                  (uint16_t)(output_section_count + 1U));
        store_u16(image + 50U,
                  spec->data_encoding,
                  (uint16_t)shstrtab_index);
    }

    for (i = 0U; i < spec->section_count; ++i) {
        const MiniElfWriteSection *section = &spec->sections[i];

        if (section->type != SHT_NOBITS && section->size != 0U) {
            memcpy(image + (size_t)headers[i + 1U].offset,
                   section->data,
                   section->size);
        }
    }

    if (spec->emit_section_symbols) {
        for (i = 0U; i < spec->section_count; ++i) {
            size_t offset = (size_t)headers[symtab_index].offset +
                            (1U + i) * sym_size;

            if (!write_symbol(image + offset,
                              spec,
                              0U,
                              (unsigned char)((STB_LOCAL << 4U) | STT_SECTION),
                              STV_DEFAULT,
                              (uint16_t)(i + 1U),
                              0U,
                              0U)) {
                set_error(error_out, MINIELF_WRITE_LIMIT);
                goto done;
            }
        }
    }

    for (i = 0U; i < spec->symbol_count; ++i) {
        const MiniElfWriteSymbol *symbol = &spec->symbols[i];
        size_t offset;

        if (symbol_output_indices[i] == 0U ||
            symbol_output_indices[i] >= symbol_count) {
            set_error(error_out, MINIELF_WRITE_INVALID_SYMBOL);
            goto done;
        }
        offset = (size_t)headers[symtab_index].offset +
                 symbol_output_indices[i] * sym_size;
        if (!write_symbol(image + offset,
                          spec,
                          symbol_name_offsets[i],
                          symbol->info,
                          symbol->other,
                          symbol->section_index,
                          symbol->value,
                          symbol->size)) {
            set_error(error_out, MINIELF_WRITE_INVALID_SYMBOL);
            goto done;
        }
    }

    for (i = 0U; i < spec->relocation_count; ++i) {
        const MiniElfWriteRela *rela = &spec->relocations[i];
        size_t section_index = relocation_section_indices[rela->target_section];
        size_t write_index = relocation_write_counts[rela->target_section]++;
        size_t symbol_index = 0U;
        size_t offset;

        if (section_index == SIZE_MAX) {
            set_error(error_out, MINIELF_WRITE_INVALID_RELOCATION);
            goto done;
        }
        if (rela->symbol_index != SIZE_MAX) {
            symbol_index = symbol_output_indices[rela->symbol_index];
            if (symbol_index == 0U) {
                set_error(error_out, MINIELF_WRITE_INVALID_RELOCATION);
                goto done;
            }
        }
        offset = (size_t)headers[section_index].offset +
                 write_index * rela_size;
        if (!write_rela(image + offset,
                        spec,
                        rela->offset,
                        symbol_index,
                        rela->type,
                        rela->addend)) {
            set_error(error_out, MINIELF_WRITE_INVALID_RELOCATION);
            goto done;
        }
    }

    memcpy(image + (size_t)headers[strtab_index].offset,
           strtab.data,
           strtab.size);
    memcpy(image + (size_t)headers[shstrtab_index].offset,
           shstrtab.data,
           shstrtab.size);

    for (i = 0U; i <= output_section_count; ++i) {
        size_t offset = section_header_offset + i * shdr_size;

        if (!write_section_header(image + offset, spec, &headers[i])) {
            set_error(error_out, MINIELF_WRITE_LIMIT);
            goto done;
        }
    }

    *image_out = image;
    *size_out = total_size;
    image = NULL;
    ok = true;

done:
    free(image);
    free(headers);
    free(symbol_name_offsets);
    free(symbol_output_indices);
    free(relocation_name_offsets);
    free(section_name_offsets);
    free(relocation_write_counts);
    free(relocation_section_indices);
    free(relocation_counts);
    free(strtab.data);
    free(shstrtab.data);
    return ok;
}
