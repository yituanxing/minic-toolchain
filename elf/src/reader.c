#include "minielf.h"

#include <elf.h>
#include <limits.h>
#include <string.h>

static bool range_ok(size_t offset, size_t amount, size_t total) {
    return offset <= total && amount <= total - offset;
}

static uint16_t load_u16(const MiniElfView *view, const unsigned char *data) {
    if (view->data_encoding == ELFDATA2MSB) {
        return (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
    }
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t load_u32(const MiniElfView *view, const unsigned char *data) {
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

static uint64_t load_u64(const MiniElfView *view, const unsigned char *data) {
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

static int32_t load_i32(const MiniElfView *view, const unsigned char *data) {
    uint32_t raw = load_u32(view, data);
    int32_t value;

    memcpy(&value, &raw, sizeof(value));
    return value;
}

static int64_t load_i64(const MiniElfView *view, const unsigned char *data) {
    uint64_t raw = load_u64(view, data);
    int64_t value;

    memcpy(&value, &raw, sizeof(value));
    return value;
}

bool minielf_open(MiniElfView *view, const void *data, size_t size) {
    const unsigned char *bytes = data;
    size_t header_size;
    size_t section_header_size;
    uint16_t section_count;
    uint16_t section_name_index;

    if (view == NULL || data == NULL || size < EI_NIDENT) {
        return false;
    }
    memset(view, 0, sizeof(*view));
    view->data = bytes;
    view->size = size;

    if (memcmp(bytes, ELFMAG, SELFMAG) != 0 ||
        (bytes[EI_CLASS] != ELFCLASS32 && bytes[EI_CLASS] != ELFCLASS64) ||
        (bytes[EI_DATA] != ELFDATA2LSB && bytes[EI_DATA] != ELFDATA2MSB) ||
        bytes[EI_VERSION] != EV_CURRENT) {
        return false;
    }

    view->elf_class = bytes[EI_CLASS];
    view->data_encoding = bytes[EI_DATA];
    header_size = view->elf_class == ELFCLASS64 ? 64U : 52U;
    section_header_size = view->elf_class == ELFCLASS64 ? 64U : 40U;
    if (size < header_size) {
        return false;
    }

    view->type = load_u16(view, bytes + 16U);
    view->machine = load_u16(view, bytes + 18U);
    view->version = load_u32(view, bytes + 20U);
    if (view->version != EV_CURRENT) {
        return false;
    }

    if (view->elf_class == ELFCLASS64) {
        view->entry = load_u64(view, bytes + 24U);
        view->program_header_offset = load_u64(view, bytes + 32U);
        view->section_header_offset = load_u64(view, bytes + 40U);
        view->flags = load_u32(view, bytes + 48U);
        if (load_u16(view, bytes + 52U) < header_size) {
            return false;
        }
        view->program_header_entry_size = load_u16(view, bytes + 54U);
        view->program_header_count = load_u16(view, bytes + 56U);
        view->section_header_entry_size = load_u16(view, bytes + 58U);
        section_count = load_u16(view, bytes + 60U);
        section_name_index = load_u16(view, bytes + 62U);
    } else {
        view->entry = load_u32(view, bytes + 24U);
        view->program_header_offset = load_u32(view, bytes + 28U);
        view->section_header_offset = load_u32(view, bytes + 32U);
        view->flags = load_u32(view, bytes + 36U);
        if (load_u16(view, bytes + 40U) < header_size) {
            return false;
        }
        view->program_header_entry_size = load_u16(view, bytes + 42U);
        view->program_header_count = load_u16(view, bytes + 44U);
        view->section_header_entry_size = load_u16(view, bytes + 46U);
        section_count = load_u16(view, bytes + 48U);
        section_name_index = load_u16(view, bytes + 50U);
    }

    if (view->program_header_count != 0U) {
        size_t program_header_size =
            view->elf_class == ELFCLASS64 ? 56U : 32U;

        if (view->program_header_entry_size < program_header_size ||
            view->program_header_offset > SIZE_MAX ||
            (size_t)view->program_header_count >
                SIZE_MAX / view->program_header_entry_size ||
            !range_ok((size_t)view->program_header_offset,
                      (size_t)view->program_header_count *
                          view->program_header_entry_size,
                      size)) {
            return false;
        }
    }

    if (section_count == 0U) {
        if (view->section_header_offset != 0U || section_name_index != SHN_UNDEF) {
            return false;
        }
        return true;
    }
    if (view->section_header_entry_size < section_header_size ||
        view->section_header_offset > SIZE_MAX ||
        section_count > SIZE_MAX / view->section_header_entry_size ||
        !range_ok((size_t)view->section_header_offset,
                  (size_t)section_count * view->section_header_entry_size,
                  size) ||
        (section_name_index != SHN_UNDEF && section_name_index >= section_count)) {
        return false;
    }

    view->section_count = section_count;
    view->section_name_table_index = section_name_index;
    return true;
}

bool minielf_program_header(const MiniElfView *view,
                            size_t index,
                            MiniElfProgramHeader *program_out) {
    const unsigned char *data;
    size_t minimum;
    size_t offset;

    if (view == NULL || program_out == NULL ||
        index >= view->program_header_count ||
        view->program_header_offset > SIZE_MAX) {
        return false;
    }
    minimum = view->elf_class == ELFCLASS64 ? 56U : 32U;
    if (view->program_header_entry_size < minimum ||
        index > (SIZE_MAX - (size_t)view->program_header_offset) /
                    view->program_header_entry_size) {
        return false;
    }
    offset = (size_t)view->program_header_offset +
             index * view->program_header_entry_size;
    if (!range_ok(offset, minimum, view->size)) {
        return false;
    }

    data = view->data + offset;
    memset(program_out, 0, sizeof(*program_out));
    program_out->type = load_u32(view, data);
    if (view->elf_class == ELFCLASS64) {
        program_out->flags = load_u32(view, data + 4U);
        program_out->offset = load_u64(view, data + 8U);
        program_out->virtual_address = load_u64(view, data + 16U);
        program_out->physical_address = load_u64(view, data + 24U);
        program_out->file_size = load_u64(view, data + 32U);
        program_out->memory_size = load_u64(view, data + 40U);
        program_out->alignment = load_u64(view, data + 48U);
    } else {
        program_out->offset = load_u32(view, data + 4U);
        program_out->virtual_address = load_u32(view, data + 8U);
        program_out->physical_address = load_u32(view, data + 12U);
        program_out->file_size = load_u32(view, data + 16U);
        program_out->memory_size = load_u32(view, data + 20U);
        program_out->flags = load_u32(view, data + 24U);
        program_out->alignment = load_u32(view, data + 28U);
    }
    return true;
}

bool minielf_section_load_address(const MiniElfView *view,
                                  const MiniElfSection *section,
                                  uint64_t *address_out) {
    size_t i;

    if (view == NULL || section == NULL || address_out == NULL) {
        return false;
    }

    for (i = 0U; i < view->program_header_count; ++i) {
        MiniElfProgramHeader program;
        uint64_t relative;

        if (!minielf_program_header(view, i, &program)) {
            return false;
        }
        if (program.type != PT_LOAD ||
            section->address < program.virtual_address) {
            continue;
        }
        relative = section->address - program.virtual_address;
        if (relative > program.memory_size ||
            section->size > program.memory_size - relative ||
            program.physical_address > UINT64_MAX - relative) {
            continue;
        }
        *address_out = program.physical_address + relative;
        return true;
    }

    *address_out = section->address;
    return true;
}

bool minielf_section(const MiniElfView *view,
                     size_t index,
                     MiniElfSection *section_out) {
    const unsigned char *data;
    size_t offset;

    if (view == NULL || section_out == NULL || index >= view->section_count ||
        view->section_header_offset > SIZE_MAX ||
        index > (SIZE_MAX - (size_t)view->section_header_offset) /
                    view->section_header_entry_size) {
        return false;
    }
    offset = (size_t)view->section_header_offset +
             index * view->section_header_entry_size;
    if (!range_ok(offset,
                  view->elf_class == ELFCLASS64 ? 64U : 40U,
                  view->size)) {
        return false;
    }
    data = view->data + offset;
    memset(section_out, 0, sizeof(*section_out));
    section_out->name = load_u32(view, data);
    section_out->type = load_u32(view, data + 4U);
    if (view->elf_class == ELFCLASS64) {
        section_out->flags = load_u64(view, data + 8U);
        section_out->address = load_u64(view, data + 16U);
        section_out->offset = load_u64(view, data + 24U);
        section_out->size = load_u64(view, data + 32U);
        section_out->link = load_u32(view, data + 40U);
        section_out->info = load_u32(view, data + 44U);
        section_out->alignment = load_u64(view, data + 48U);
        section_out->entry_size = load_u64(view, data + 56U);
    } else {
        section_out->flags = load_u32(view, data + 8U);
        section_out->address = load_u32(view, data + 12U);
        section_out->offset = load_u32(view, data + 16U);
        section_out->size = load_u32(view, data + 20U);
        section_out->link = load_u32(view, data + 24U);
        section_out->info = load_u32(view, data + 28U);
        section_out->alignment = load_u32(view, data + 32U);
        section_out->entry_size = load_u32(view, data + 36U);
    }
    return true;
}

bool minielf_string(const MiniElfView *view,
                    size_t string_table_index,
                    uint32_t offset,
                    const char **text_out) {
    MiniElfSection table;
    const char *text;
    size_t base;
    size_t available;

    if (text_out == NULL ||
        !minielf_section(view, string_table_index, &table) ||
        table.type != SHT_STRTAB ||
        table.offset > SIZE_MAX || table.size > SIZE_MAX ||
        offset >= table.size) {
        return false;
    }
    base = (size_t)table.offset;
    if (!range_ok(base, (size_t)table.size, view->size)) {
        return false;
    }
    text = (const char *)view->data + base + offset;
    available = (size_t)table.size - offset;
    if (memchr(text, '\0', available) == NULL) {
        return false;
    }
    *text_out = text;
    return true;
}

bool minielf_section_name(const MiniElfView *view,
                          size_t index,
                          const char **name_out) {
    MiniElfSection section;

    if (view == NULL || view->section_name_table_index == SHN_UNDEF ||
        !minielf_section(view, index, &section)) {
        return false;
    }
    return minielf_string(view,
                          view->section_name_table_index,
                          section.name,
                          name_out);
}

bool minielf_section_data(const MiniElfView *view,
                          size_t index,
                          const unsigned char **data_out,
                          size_t *size_out) {
    MiniElfSection section;

    if (data_out == NULL || size_out == NULL ||
        !minielf_section(view, index, &section) ||
        section.type == SHT_NOBITS ||
        section.offset > SIZE_MAX || section.size > SIZE_MAX ||
        !range_ok((size_t)section.offset, (size_t)section.size, view->size)) {
        return false;
    }
    *data_out = view->data + (size_t)section.offset;
    *size_out = (size_t)section.size;
    return true;
}

size_t minielf_symbol_count(const MiniElfView *view,
                            const MiniElfSection *symbol_table) {
    uint64_t minimum;

    if (view == NULL || symbol_table == NULL ||
        (symbol_table->type != SHT_SYMTAB &&
         symbol_table->type != SHT_DYNSYM)) {
        return 0U;
    }
    minimum = view->elf_class == ELFCLASS64 ? 24U : 16U;
    if (symbol_table->entry_size < minimum ||
        symbol_table->entry_size == 0U ||
        symbol_table->size / symbol_table->entry_size > SIZE_MAX) {
        return 0U;
    }
    return (size_t)(symbol_table->size / symbol_table->entry_size);
}

bool minielf_symbol(const MiniElfView *view,
                    size_t symbol_table_index,
                    size_t symbol_index,
                    MiniElfSymbol *symbol_out) {
    MiniElfSection table;
    uint64_t minimum;
    uint64_t relative;
    uint64_t absolute;
    const unsigned char *data;

    if (symbol_out == NULL ||
        !minielf_section(view, symbol_table_index, &table) ||
        (table.type != SHT_SYMTAB && table.type != SHT_DYNSYM)) {
        return false;
    }
    minimum = view->elf_class == ELFCLASS64 ? 24U : 16U;
    if (table.entry_size < minimum ||
        symbol_index >= minielf_symbol_count(view, &table) ||
        symbol_index > UINT64_MAX / table.entry_size) {
        return false;
    }
    relative = (uint64_t)symbol_index * table.entry_size;
    if (table.offset > UINT64_MAX - relative) {
        return false;
    }
    absolute = table.offset + relative;
    if (absolute > SIZE_MAX ||
        !range_ok((size_t)absolute, (size_t)minimum, view->size)) {
        return false;
    }

    data = view->data + (size_t)absolute;
    memset(symbol_out, 0, sizeof(*symbol_out));
    symbol_out->name = load_u32(view, data);
    if (view->elf_class == ELFCLASS64) {
        symbol_out->info = data[4U];
        symbol_out->other = data[5U];
        symbol_out->section_index = load_u16(view, data + 6U);
        symbol_out->value = load_u64(view, data + 8U);
        symbol_out->size = load_u64(view, data + 16U);
    } else {
        symbol_out->value = load_u32(view, data + 4U);
        symbol_out->size = load_u32(view, data + 8U);
        symbol_out->info = data[12U];
        symbol_out->other = data[13U];
        symbol_out->section_index = load_u16(view, data + 14U);
    }
    return true;
}

size_t minielf_rela_count(const MiniElfView *view,
                          const MiniElfSection *relocation_section) {
    uint64_t minimum;

    if (view == NULL || relocation_section == NULL ||
        relocation_section->type != SHT_RELA) {
        return 0U;
    }
    minimum = view->elf_class == ELFCLASS64 ? 24U : 12U;
    if (relocation_section->entry_size < minimum ||
        relocation_section->entry_size == 0U ||
        relocation_section->size / relocation_section->entry_size > SIZE_MAX) {
        return 0U;
    }
    return (size_t)(relocation_section->size /
                    relocation_section->entry_size);
}

bool minielf_rela(const MiniElfView *view,
                  size_t relocation_section_index,
                  size_t relocation_index,
                  MiniElfRela *rela_out) {
    MiniElfSection table;
    uint64_t minimum;
    uint64_t relative;
    uint64_t absolute;
    const unsigned char *data;

    if (rela_out == NULL ||
        !minielf_section(view, relocation_section_index, &table) ||
        table.type != SHT_RELA) {
        return false;
    }
    minimum = view->elf_class == ELFCLASS64 ? 24U : 12U;
    if (table.entry_size < minimum ||
        relocation_index >= minielf_rela_count(view, &table) ||
        relocation_index > UINT64_MAX / table.entry_size) {
        return false;
    }
    relative = (uint64_t)relocation_index * table.entry_size;
    if (table.offset > UINT64_MAX - relative) {
        return false;
    }
    absolute = table.offset + relative;
    if (absolute > SIZE_MAX ||
        !range_ok((size_t)absolute, (size_t)minimum, view->size)) {
        return false;
    }

    data = view->data + (size_t)absolute;
    if (view->elf_class == ELFCLASS64) {
        rela_out->offset = load_u64(view, data);
        rela_out->info = load_u64(view, data + 8U);
        rela_out->addend = load_i64(view, data + 16U);
    } else {
        rela_out->offset = load_u32(view, data);
        rela_out->info = load_u32(view, data + 4U);
        rela_out->addend = load_i32(view, data + 8U);
    }
    return true;
}

size_t minielf_rela_symbol(const MiniElfView *view, uint64_t info) {
    if (view == NULL) {
        return SIZE_MAX;
    }
    if (view->elf_class == ELFCLASS64) {
        return (size_t)(info >> 32U);
    }
    return (size_t)(info >> 8U);
}

uint32_t minielf_rela_type(const MiniElfView *view, uint64_t info) {
    if (view == NULL) {
        return UINT32_MAX;
    }
    if (view->elf_class == ELFCLASS64) {
        return (uint32_t)info;
    }
    return (uint32_t)(info & UINT64_C(0xff));
}

unsigned minielf_symbol_bind(unsigned char info) {
    return (unsigned)(info >> 4U);
}

unsigned minielf_symbol_type(unsigned char info) {
    return (unsigned)(info & 0x0fU);
}
