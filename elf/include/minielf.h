#ifndef MINIELF_H
#define MINIELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MiniElfView {
    const unsigned char *data;
    size_t size;
    unsigned char elf_class;
    unsigned char data_encoding;
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    size_t section_count;
    size_t section_name_table_index;
} MiniElfView;

typedef struct MiniElfSection {
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
} MiniElfSection;

typedef struct MiniElfSymbol {
    uint32_t name;
    unsigned char info;
    unsigned char other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
} MiniElfSymbol;

typedef struct MiniElfRela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} MiniElfRela;

bool minielf_open(MiniElfView *view, const void *data, size_t size);
bool minielf_section(const MiniElfView *view,
                     size_t index,
                     MiniElfSection *section_out);
bool minielf_section_name(const MiniElfView *view,
                          size_t index,
                          const char **name_out);
bool minielf_string(const MiniElfView *view,
                    size_t string_table_index,
                    uint32_t offset,
                    const char **text_out);
bool minielf_section_data(const MiniElfView *view,
                          size_t index,
                          const unsigned char **data_out,
                          size_t *size_out);
bool minielf_symbol(const MiniElfView *view,
                    size_t symbol_table_index,
                    size_t symbol_index,
                    MiniElfSymbol *symbol_out);
bool minielf_rela(const MiniElfView *view,
                  size_t relocation_section_index,
                  size_t relocation_index,
                  MiniElfRela *rela_out);
size_t minielf_symbol_count(const MiniElfView *view,
                            const MiniElfSection *symbol_table);
size_t minielf_rela_count(const MiniElfView *view,
                          const MiniElfSection *relocation_section);
size_t minielf_rela_symbol(const MiniElfView *view, uint64_t info);
uint32_t minielf_rela_type(const MiniElfView *view, uint64_t info);
unsigned minielf_symbol_bind(unsigned char info);
unsigned minielf_symbol_type(unsigned char info);

typedef enum MiniElfWriteError {
    MINIELF_WRITE_OK = 0,
    MINIELF_WRITE_INVALID_ARGUMENT,
    MINIELF_WRITE_OUT_OF_MEMORY,
    MINIELF_WRITE_LIMIT,
    MINIELF_WRITE_INVALID_SECTION,
    MINIELF_WRITE_INVALID_SYMBOL,
    MINIELF_WRITE_INVALID_RELOCATION
} MiniElfWriteError;

typedef struct MiniElfWriteSection {
    const char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t alignment;
    uint64_t entry_size;
    const unsigned char *data;
    size_t size;
} MiniElfWriteSection;

typedef struct MiniElfWriteSymbol {
    const char *name;
    unsigned char info;
    unsigned char other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
} MiniElfWriteSymbol;

typedef struct MiniElfWriteRela {
    size_t target_section;
    uint64_t offset;
    size_t symbol_index;
    uint32_t type;
    int64_t addend;
} MiniElfWriteRela;

typedef struct MiniElfRelocatableSpec {
    unsigned char elf_class;
    unsigned char data_encoding;
    uint16_t machine;
    uint32_t flags;
    const MiniElfWriteSection *sections;
    size_t section_count;
    const MiniElfWriteSymbol *symbols;
    size_t symbol_count;
    const MiniElfWriteRela *relocations;
    size_t relocation_count;
    bool emit_section_symbols;
} MiniElfRelocatableSpec;

bool minielf_build_relocatable(const MiniElfRelocatableSpec *spec,
                               unsigned char **image_out,
                               size_t *size_out,
                               MiniElfWriteError *error_out);
const char *minielf_write_error_string(MiniElfWriteError error);

#endif
