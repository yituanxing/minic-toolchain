#include "minielf.h"

#include <elf.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct MiniElfRewriteSectionState {
    MiniElfSection section;
    bool remove;
    uint64_t new_offset;
    uint32_t new_info;
    unsigned char *modified_data;
    size_t modified_size;
} MiniElfRewriteSectionState;

static void set_error(MiniElfRewriteError *error_out,
                      MiniElfRewriteError error) {
    if (error_out != NULL) {
        *error_out = error;
    }
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

static uint32_t load_u32(const MiniElfView *view,
                         const unsigned char *data) {
    if (view->data_encoding == ELFDATA2MSB) {
        return ((uint32_t)data[0] << 24U) |
               ((uint32_t)data[1] << 16U) |
               ((uint32_t)data[2] << 8U) |
               (uint32_t)data[3];
    }
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static uint64_t load_u64(const MiniElfView *view,
                         const unsigned char *data) {
    uint64_t value = 0U;
    size_t i;

    if (view->data_encoding == ELFDATA2MSB) {
        for (i = 0U; i < 8U; ++i) {
            value = (value << 8U) | (uint64_t)data[i];
        }
        return value;
    }
    for (i = 0U; i < 8U; ++i) {
        value |= (uint64_t)data[i] << (i * 8U);
    }
    return value;
}

static void store_u32(unsigned char *data,
                      unsigned char encoding,
                      uint32_t value) {
    size_t i;

    if (encoding == ELFDATA2MSB) {
        for (i = 0U; i < 4U; ++i) {
            data[i] =
                (unsigned char)((value >> ((3U - i) * 8U)) & UINT32_C(0xff));
        }
    } else {
        for (i = 0U; i < 4U; ++i) {
            data[i] =
                (unsigned char)((value >> (i * 8U)) & UINT32_C(0xff));
        }
    }
}

static void store_u64(unsigned char *data,
                      unsigned char encoding,
                      uint64_t value) {
    size_t i;

    if (encoding == ELFDATA2MSB) {
        for (i = 0U; i < 8U; ++i) {
            data[i] =
                (unsigned char)((value >> ((7U - i) * 8U)) & UINT64_C(0xff));
        }
    } else {
        for (i = 0U; i < 8U; ++i) {
            data[i] =
                (unsigned char)((value >> (i * 8U)) & UINT64_C(0xff));
        }
    }
}

static bool starts_with(const char *text, const char *prefix) {
    size_t length = strlen(prefix);

    return strncmp(text, prefix, length) == 0;
}

static bool debug_section_name(const char *name) {
    return starts_with(name, ".debug") ||
           starts_with(name, ".zdebug") ||
           starts_with(name, ".stab") ||
           strcmp(name, ".gdb_index") == 0 ||
           strcmp(name, ".gnu_debuglink") == 0 ||
           strcmp(name, ".gnu_debugaltlink") == 0;
}

static bool keep_global_name(const MiniElfRewriteOptions *options,
                             const char *name) {
    size_t i;

    for (i = 0U; i < options->keep_global_count; ++i) {
        if (strcmp(options->keep_global_symbols[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool effective_local_symbol(const MiniElfRewriteOptions *options,
                                   const MiniElfSymbol *symbol,
                                   const char *name) {
    unsigned bind = minielf_symbol_bind(symbol->info);

    if (bind == STB_LOCAL) {
        return true;
    }
    if ((bind == STB_GLOBAL || bind == STB_WEAK) &&
        symbol->section_index != SHN_UNDEF &&
        !keep_global_name(options, name)) {
        return true;
    }
    return false;
}

static bool write_section_header(unsigned char *data,
                                 const MiniElfView *view,
                                 const MiniElfSection *section,
                                 uint64_t offset,
                                 uint32_t info) {
    size_t minimum = view->elf_class == ELFCLASS64 ? 64U : 40U;

    memset(data, 0, view->section_header_entry_size);
    store_u32(data, view->data_encoding, section->name);
    store_u32(data + 4U, view->data_encoding, section->type);
    if (view->elf_class == ELFCLASS64) {
        store_u64(data + 8U, view->data_encoding, section->flags);
        store_u64(data + 16U, view->data_encoding, section->address);
        store_u64(data + 24U, view->data_encoding, offset);
        store_u64(data + 32U, view->data_encoding, section->size);
        store_u32(data + 40U, view->data_encoding, section->link);
        store_u32(data + 44U, view->data_encoding, info);
        store_u64(data + 48U, view->data_encoding, section->alignment);
        store_u64(data + 56U, view->data_encoding, section->entry_size);
    } else {
        if (section->flags > UINT32_MAX ||
            section->address > UINT32_MAX ||
            offset > UINT32_MAX ||
            section->size > UINT32_MAX ||
            section->alignment > UINT32_MAX ||
            section->entry_size > UINT32_MAX) {
            return false;
        }
        store_u32(data + 8U,
                  view->data_encoding,
                  (uint32_t)section->flags);
        store_u32(data + 12U,
                  view->data_encoding,
                  (uint32_t)section->address);
        store_u32(data + 16U,
                  view->data_encoding,
                  (uint32_t)offset);
        store_u32(data + 20U,
                  view->data_encoding,
                  (uint32_t)section->size);
        store_u32(data + 24U, view->data_encoding, section->link);
        store_u32(data + 28U, view->data_encoding, info);
        store_u32(data + 32U,
                  view->data_encoding,
                  (uint32_t)section->alignment);
        store_u32(data + 36U,
                  view->data_encoding,
                  (uint32_t)section->entry_size);
    }
    (void)minimum;
    return true;
}

static bool patch_symbol_bind(unsigned char *entry,
                              const MiniElfView *view,
                              unsigned bind) {
    size_t info_offset = view->elf_class == ELFCLASS64 ? 4U : 12U;

    if (bind > UINT8_MAX >> 4U) {
        return false;
    }
    entry[info_offset] =
        (unsigned char)((bind << 4U) | (entry[info_offset] & 0x0fU));
    return true;
}

static bool build_localized_symtab(const MiniElfView *view,
                                   size_t symtab_index,
                                   MiniElfRewriteSectionState *states,
                                   const MiniElfRewriteOptions *options,
                                   size_t **symbol_map_out,
                                   size_t *symbol_count_out) {
    MiniElfSection *symtab = &states[symtab_index].section;
    const unsigned char *input;
    size_t input_size;
    size_t count;
    size_t *symbol_map = NULL;
    bool *local = NULL;
    unsigned char *output = NULL;
    size_t entry_size;
    size_t local_next = 1U;
    size_t global_next;
    size_t local_count = 0U;
    size_t i;
    bool ok = false;

    if (symtab->link >= view->section_count ||
        !minielf_section_data(view, symtab_index, &input, &input_size)) {
        return false;
    }
    entry_size = (size_t)symtab->entry_size;
    if (entry_size < (view->elf_class == ELFCLASS64 ? 24U : 16U) ||
        entry_size == 0U || input_size % entry_size != 0U) {
        return false;
    }
    count = input_size / entry_size;
    if (count == 0U) {
        return false;
    }

    symbol_map = malloc(count * sizeof(*symbol_map));
    local = calloc(count, sizeof(*local));
    output = malloc(input_size == 0U ? 1U : input_size);
    if (symbol_map == NULL || local == NULL || output == NULL) {
        goto done;
    }
    symbol_map[0] = 0U;
    local[0] = true;

    for (i = 1U; i < count; ++i) {
        MiniElfSymbol symbol;
        const char *name;

        if (!minielf_symbol(view, symtab_index, i, &symbol) ||
            !minielf_string(view, symtab->link, symbol.name, &name)) {
            goto done;
        }
        local[i] = effective_local_symbol(options, &symbol, name);
        if (local[i]) {
            ++local_count;
        }
    }
    global_next = 1U + local_count;

    for (i = 1U; i < count; ++i) {
        if (local[i]) {
            symbol_map[i] = local_next++;
        } else {
            symbol_map[i] = global_next++;
        }
    }
    if (local_next != 1U + local_count || global_next != count) {
        goto done;
    }

    memcpy(output, input, entry_size);
    for (i = 1U; i < count; ++i) {
        MiniElfSymbol symbol;
        const char *name;
        unsigned bind;
        unsigned char *entry =
            output + symbol_map[i] * entry_size;

        memcpy(entry, input + i * entry_size, entry_size);
        if (!minielf_symbol(view, symtab_index, i, &symbol) ||
            !minielf_string(view, symtab->link, symbol.name, &name)) {
            goto done;
        }
        bind = minielf_symbol_bind(symbol.info);
        if (effective_local_symbol(options, &symbol, name) &&
            bind != STB_LOCAL &&
            !patch_symbol_bind(entry, view, STB_LOCAL)) {
            goto done;
        }
    }

    states[symtab_index].modified_data = output;
    states[symtab_index].modified_size = input_size;
    states[symtab_index].new_info = (uint32_t)(1U + local_count);
    output = NULL;
    *symbol_map_out = symbol_map;
    *symbol_count_out = count;
    symbol_map = NULL;
    ok = true;

done:
    free(output);
    free(local);
    free(symbol_map);
    return ok;
}

static bool rewrite_linked_symbol_indices(
    const MiniElfView *view,
    size_t section_index,
    MiniElfRewriteSectionState *states,
    const size_t *symbol_map,
    size_t symbol_count) {
    MiniElfSection *section = &states[section_index].section;
    const unsigned char *input;
    size_t input_size;
    unsigned char *output;
    size_t count;
    size_t entry_size;
    size_t i;

    if (section->type == SHT_GROUP) {
        if (section->info >= symbol_count) {
            return false;
        }
        states[section_index].new_info =
            (uint32_t)symbol_map[section->info];
        return true;
    }
    if (section->type == SHT_SYMTAB_SHNDX) {
        if (!minielf_section_data(view,
                                  section_index,
                                  &input,
                                  &input_size) ||
            input_size % sizeof(uint32_t) != 0U ||
            input_size / sizeof(uint32_t) != symbol_count) {
            return false;
        }
        output = malloc(input_size == 0U ? 1U : input_size);
        if (output == NULL) {
            return false;
        }
        for (i = 0U; i < symbol_count; ++i) {
            uint32_t value = load_u32(view,
                                      input + i * sizeof(uint32_t));
            store_u32(output + symbol_map[i] * sizeof(uint32_t),
                      view->data_encoding,
                      value);
        }
        states[section_index].modified_data = output;
        states[section_index].modified_size = input_size;
        return true;
    }
    if (section->type != SHT_RELA && section->type != SHT_REL) {
        return false;
    }
    if (!minielf_section_data(view,
                              section_index,
                              &input,
                              &input_size)) {
        return false;
    }

    if (view->elf_class == ELFCLASS64) {
        entry_size = section->type == SHT_RELA ? 24U : 16U;
    } else {
        entry_size = section->type == SHT_RELA ? 12U : 8U;
    }
    if (section->entry_size < entry_size ||
        section->entry_size == 0U ||
        input_size % (size_t)section->entry_size != 0U) {
        return false;
    }
    count = input_size / (size_t)section->entry_size;
    output = malloc(input_size == 0U ? 1U : input_size);
    if (output == NULL) {
        return false;
    }
    memcpy(output, input, input_size);

    for (i = 0U; i < count; ++i) {
        unsigned char *entry =
            output + i * (size_t)section->entry_size;
        uint64_t info;
        size_t old_symbol;
        uint32_t type;

        if (view->elf_class == ELFCLASS64) {
            info = load_u64(view, entry + 8U);
            old_symbol = (size_t)(info >> 32U);
            type = (uint32_t)info;
            if (old_symbol >= symbol_count) {
                free(output);
                return false;
            }
            info = ((uint64_t)symbol_map[old_symbol] << 32U) |
                   (uint64_t)type;
            store_u64(entry + 8U, view->data_encoding, info);
        } else {
            info = load_u32(view, entry + 4U);
            old_symbol = (size_t)(info >> 8U);
            type = (uint32_t)(info & UINT64_C(0xff));
            if (old_symbol >= symbol_count ||
                symbol_map[old_symbol] > UINT32_C(0x00ffffff)) {
                free(output);
                return false;
            }
            store_u32(entry + 4U,
                      view->data_encoding,
                      ((uint32_t)symbol_map[old_symbol] << 8U) | type);
        }
    }

    states[section_index].modified_data = output;
    states[section_index].modified_size = input_size;
    return true;
}

static bool write_elf_shoff(unsigned char *image,
                            const MiniElfView *view,
                            size_t section_header_offset) {
    if (view->elf_class == ELFCLASS64) {
        store_u64(image + 40U,
                  view->data_encoding,
                  section_header_offset);
        return true;
    }
    if (section_header_offset > UINT32_MAX) {
        return false;
    }
    store_u32(image + 32U,
              view->data_encoding,
              (uint32_t)section_header_offset);
    return true;
}

const char *minielf_rewrite_error_string(MiniElfRewriteError error) {
    switch (error) {
    case MINIELF_REWRITE_OK:
        return "ok";
    case MINIELF_REWRITE_INVALID_ARGUMENT:
        return "invalid-argument";
    case MINIELF_REWRITE_UNSUPPORTED_FILE:
        return "unsupported-file";
    case MINIELF_REWRITE_INVALID_SECTION:
        return "invalid-section";
    case MINIELF_REWRITE_INVALID_SYMBOL_TABLE:
        return "invalid-symbol-table";
    case MINIELF_REWRITE_UNSUPPORTED_SYMBOL_REFERENCE:
        return "unsupported-symbol-reference";
    case MINIELF_REWRITE_LIMIT:
        return "format-limit";
    case MINIELF_REWRITE_OUT_OF_MEMORY:
        return "out-of-memory";
    }
    return "unknown";
}

bool minielf_rewrite(const MiniElfView *view,
                     const MiniElfRewriteOptions *options,
                     unsigned char **image_out,
                     size_t *size_out,
                     MiniElfRewriteError *error_out) {
    MiniElfRewriteSectionState *states = NULL;
    size_t *symbol_map = NULL;
    size_t symbol_count = 0U;
    size_t symtab_index = SIZE_MAX;
    unsigned char *image = NULL;
    size_t header_size;
    size_t section_header_minimum;
    size_t prefix_end;
    size_t cursor;
    size_t section_header_offset;
    size_t total_size;
    size_t i;
    bool ok = false;

    if (error_out != NULL) {
        *error_out = MINIELF_REWRITE_OK;
    }
    if (view == NULL || options == NULL ||
        image_out == NULL || size_out == NULL ||
        (view->type != ET_EXEC && view->type != ET_DYN) ||
        view->section_count == 0U ||
        view->section_name_table_index == SHN_UNDEF) {
        set_error(error_out,
                  view == NULL || options == NULL ||
                          image_out == NULL || size_out == NULL
                      ? MINIELF_REWRITE_INVALID_ARGUMENT
                      : MINIELF_REWRITE_UNSUPPORTED_FILE);
        return false;
    }
    *image_out = NULL;
    *size_out = 0U;

    header_size = view->elf_class == ELFCLASS64 ? 64U : 52U;
    section_header_minimum =
        view->elf_class == ELFCLASS64 ? 64U : 40U;
    if (view->section_header_entry_size < section_header_minimum) {
        set_error(error_out, MINIELF_REWRITE_UNSUPPORTED_FILE);
        return false;
    }

    states = calloc(view->section_count, sizeof(*states));
    if (states == NULL) {
        set_error(error_out, MINIELF_REWRITE_OUT_OF_MEMORY);
        goto done;
    }

    for (i = 0U; i < view->section_count; ++i) {
        const char *name = "";
        MiniElfSection section;

        if (!minielf_section(view, i, &section)) {
            set_error(error_out, MINIELF_REWRITE_INVALID_SECTION);
            goto done;
        }
        states[i].section = section;
        states[i].new_info = section.info;
        if (i != 0U && !minielf_section_name(view, i, &name)) {
            set_error(error_out, MINIELF_REWRITE_INVALID_SECTION);
            goto done;
        }
        if (i != 0U && options->remove_sections != NULL &&
            options->remove_sections[i]) {
            states[i].remove = true;
        }
        if (i != 0U &&
            (options->strip_debug || options->strip_all) &&
            debug_section_name(name)) {
            states[i].remove = true;
        }
        if (section.type == SHT_SYMTAB) {
            if (symtab_index != SIZE_MAX) {
                set_error(error_out,
                          MINIELF_REWRITE_INVALID_SYMBOL_TABLE);
                goto done;
            }
            symtab_index = i;
            if (options->strip_all) {
                states[i].remove = true;
                if (section.link < view->section_count) {
                    states[section.link].remove = true;
                }
            }
        }
    }

    if (states[view->section_name_table_index].remove) {
        set_error(error_out, MINIELF_REWRITE_INVALID_SECTION);
        goto done;
    }

    for (i = 1U; i < view->section_count; ++i) {
        MiniElfSection *section = &states[i].section;

        if ((section->type == SHT_RELA ||
             section->type == SHT_REL) &&
            section->info < view->section_count &&
            states[section->info].remove) {
            states[i].remove = true;
        }
        if (options->strip_all &&
            symtab_index != SIZE_MAX &&
            section->link == symtab_index) {
            states[i].remove = true;
        }
    }

    if (options->keep_global_count != 0U) {
        if (symtab_index == SIZE_MAX || states[symtab_index].remove) {
            set_error(error_out,
                      MINIELF_REWRITE_INVALID_SYMBOL_TABLE);
            goto done;
        }
        if (!build_localized_symtab(view,
                                    symtab_index,
                                    states,
                                    options,
                                    &symbol_map,
                                    &symbol_count)) {
            set_error(error_out,
                      MINIELF_REWRITE_INVALID_SYMBOL_TABLE);
            goto done;
        }
        for (i = 1U; i < view->section_count; ++i) {
            MiniElfSection *section = &states[i].section;

            if (i == symtab_index || states[i].remove ||
                section->link != symtab_index) {
                continue;
            }
            if (!rewrite_linked_symbol_indices(view,
                                               i,
                                               states,
                                               symbol_map,
                                               symbol_count)) {
                set_error(error_out,
                          MINIELF_REWRITE_UNSUPPORTED_SYMBOL_REFERENCE);
                goto done;
            }
        }
    }

    prefix_end = header_size;
    if (view->program_header_count != 0U) {
        size_t ph_end;

        if (view->program_header_offset > SIZE_MAX ||
            view->program_header_count >
                (SIZE_MAX - (size_t)view->program_header_offset) /
                    view->program_header_entry_size) {
            set_error(error_out, MINIELF_REWRITE_LIMIT);
            goto done;
        }
        ph_end = (size_t)view->program_header_offset +
                 (size_t)view->program_header_count *
                     view->program_header_entry_size;
        if (ph_end > prefix_end) {
            prefix_end = ph_end;
        }
    }
    for (i = 0U; i < view->program_header_count; ++i) {
        MiniElfProgramHeader program;
        size_t end;

        if (!minielf_program_header(view, i, &program) ||
            program.offset > SIZE_MAX ||
            program.file_size > SIZE_MAX ||
            !add_size((size_t)program.offset,
                      (size_t)program.file_size,
                      &end) ||
            end > view->size) {
            set_error(error_out, MINIELF_REWRITE_UNSUPPORTED_FILE);
            goto done;
        }
        if (end > prefix_end) {
            prefix_end = end;
        }
    }
    if (prefix_end > view->size) {
        set_error(error_out, MINIELF_REWRITE_UNSUPPORTED_FILE);
        goto done;
    }

    cursor = prefix_end;
    for (i = 1U; i < view->section_count; ++i) {
        MiniElfSection *section = &states[i].section;
        size_t end;
        size_t aligned;

        if (states[i].remove || section->type == SHT_NOBITS ||
            section->size == 0U) {
            states[i].new_offset = section->offset;
            continue;
        }
        if (section->offset > SIZE_MAX ||
            section->size > SIZE_MAX ||
            !add_size((size_t)section->offset,
                      (size_t)section->size,
                      &end) ||
            end > view->size) {
            set_error(error_out, MINIELF_REWRITE_INVALID_SECTION);
            goto done;
        }

        if (end <= prefix_end) {
            states[i].new_offset = section->offset;
            continue;
        }
        if ((section->flags & SHF_ALLOC) != 0U) {
            set_error(error_out, MINIELF_REWRITE_UNSUPPORTED_FILE);
            goto done;
        }
        if (!align_up(cursor,
                      section->alignment == 0U
                          ? 1U
                          : section->alignment,
                      &aligned)) {
            set_error(error_out, MINIELF_REWRITE_LIMIT);
            goto done;
        }
        cursor = aligned;
        states[i].new_offset = cursor;
        if (!add_size(cursor, (size_t)section->size, &cursor)) {
            set_error(error_out, MINIELF_REWRITE_LIMIT);
            goto done;
        }
    }

    if (!align_up(cursor,
                  view->elf_class == ELFCLASS64 ? 8U : 4U,
                  &section_header_offset) ||
        view->section_count >
            (SIZE_MAX - section_header_offset) /
                view->section_header_entry_size) {
        set_error(error_out, MINIELF_REWRITE_LIMIT);
        goto done;
    }
    total_size =
        section_header_offset +
        view->section_count * view->section_header_entry_size;
    if (view->elf_class == ELFCLASS32 &&
        section_header_offset > UINT32_MAX) {
        set_error(error_out, MINIELF_REWRITE_LIMIT);
        goto done;
    }

    image = calloc(total_size == 0U ? 1U : total_size, 1U);
    if (image == NULL) {
        set_error(error_out, MINIELF_REWRITE_OUT_OF_MEMORY);
        goto done;
    }
    memcpy(image, view->data, prefix_end);
    if (!write_elf_shoff(image, view, section_header_offset)) {
        set_error(error_out, MINIELF_REWRITE_LIMIT);
        goto done;
    }

    for (i = 1U; i < view->section_count; ++i) {
        MiniElfSection *section = &states[i].section;
        const unsigned char *data;
        size_t data_size;

        if (states[i].remove || section->type == SHT_NOBITS ||
            section->size == 0U) {
            continue;
        }
        if (states[i].modified_data != NULL) {
            data = states[i].modified_data;
            data_size = states[i].modified_size;
        } else if (!minielf_section_data(view,
                                         i,
                                         &data,
                                         &data_size)) {
            set_error(error_out, MINIELF_REWRITE_INVALID_SECTION);
            goto done;
        }
        if (data_size != (size_t)section->size ||
            states[i].new_offset > SIZE_MAX ||
            !range_ok((size_t)states[i].new_offset,
                      data_size,
                      total_size)) {
            set_error(error_out, MINIELF_REWRITE_INVALID_SECTION);
            goto done;
        }
        memcpy(image + (size_t)states[i].new_offset,
               data,
               data_size);
    }

    for (i = 1U; i < view->section_count; ++i) {
        size_t offset =
            section_header_offset +
            i * view->section_header_entry_size;

        if (states[i].remove) {
            continue;
        }
        if (!write_section_header(image + offset,
                                  view,
                                  &states[i].section,
                                  states[i].new_offset,
                                  states[i].new_info)) {
            set_error(error_out, MINIELF_REWRITE_LIMIT);
            goto done;
        }
    }

    *image_out = image;
    *size_out = total_size;
    image = NULL;
    ok = true;

done:
    if (states != NULL) {
        for (i = 0U; i < view->section_count; ++i) {
            free(states[i].modified_data);
        }
    }
    free(image);
    free(symbol_map);
    free(states);
    return ok;
}
