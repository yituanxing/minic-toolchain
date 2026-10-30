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

typedef struct MiniElfProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} MiniElfProgramHeader;

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
bool minielf_program_header(const MiniElfView *view,
                            size_t index,
                            MiniElfProgramHeader *program_out);
bool minielf_section_load_address(const MiniElfView *view,
                                  const MiniElfSection *section,
                                  uint64_t *address_out);
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
    /* ObjectModel grouping: logical signature symbol index in spec.symbols.
     * The writer resolves final symbol-table order and SHT_GROUP members. */
    bool has_group;
    size_t group_signature_symbol;
    /* SHF_LINK_ORDER sh_link is a final ELF section-header index. Keep the
     * target as an input SectionID until layout; an absent association emits
     * SHN_UNDEF (0). The explicit presence bit makes zero-initialized specs
     * safely represent an absent link instead of silently targeting section 0. */
    bool has_link_order_target;
    size_t linked_target_section;
} MiniElfWriteSection;

typedef struct MiniElfWriteSymbol {
    const char *name;
    unsigned char info;
    unsigned char other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
    /* A default-local COMDAT signature may refer to the SHT_GROUP header
     * synthesized by this writer, not an input Section.  The final header
     * index is assigned only after output layout/group collection. */
    bool group_signature_section;
} MiniElfWriteSymbol;

typedef struct MiniElfWriteRela {
    size_t target_section;
    uint64_t offset;
    size_t symbol_index;
    uint32_t type;
    int64_t addend;
    bool use_section_symbol; /* generated STT_SECTION entry, not user symbol */
    size_t section_symbol_index;
} MiniElfWriteRela;

typedef struct MiniElfRelocatableSpec {
    unsigned char elf_class;
    unsigned char data_encoding;
    unsigned char osabi; /* GNU IFUNC/UNIQUE require ELFOSABI_GNU */
    uint16_t machine;
    uint32_t flags;
    const MiniElfWriteSection *sections;
    size_t section_count;
    const MiniElfWriteSymbol *symbols;
    size_t symbol_count;
    const MiniElfWriteRela *relocations;
    size_t relocation_count;
    /* Permit relocations against generated STT_SECTION symbols. The writer
     * emits only sections actually referenced by use_section_symbol fixups,
     * not an artificial symbol for every input section. */
    bool emit_section_symbols;
    bool emit_all_section_symbols; /* opt-in GNU AArch64 STT_SECTION policy */
} MiniElfRelocatableSpec;

bool minielf_build_relocatable(const MiniElfRelocatableSpec *spec,
                               unsigned char **image_out,
                               size_t *size_out,
                               MiniElfWriteError *error_out);
const char *minielf_write_error_string(MiniElfWriteError error);

typedef enum MiniElfBinaryError {
    MINIELF_BINARY_OK = 0,
    MINIELF_BINARY_INVALID_ARGUMENT,
    MINIELF_BINARY_INVALID_SECTION,
    MINIELF_BINARY_NO_LOADABLE_SECTIONS,
    MINIELF_BINARY_LIMIT,
    MINIELF_BINARY_OUT_OF_MEMORY
} MiniElfBinaryError;

bool minielf_build_binary(const MiniElfView *view,
                          const bool *include_sections,
                          unsigned char **image_out,
                          size_t *size_out,
                          uint64_t *base_address_out,
                          MiniElfBinaryError *error_out);
const char *minielf_binary_error_string(MiniElfBinaryError error);

typedef enum MiniElfRewriteError {
    MINIELF_REWRITE_OK = 0,
    MINIELF_REWRITE_INVALID_ARGUMENT,
    MINIELF_REWRITE_UNSUPPORTED_FILE,
    MINIELF_REWRITE_INVALID_SECTION,
    MINIELF_REWRITE_INVALID_SYMBOL_TABLE,
    MINIELF_REWRITE_UNSUPPORTED_SYMBOL_REFERENCE,
    MINIELF_REWRITE_LIMIT,
    MINIELF_REWRITE_OUT_OF_MEMORY
} MiniElfRewriteError;

typedef struct MiniElfRewriteOptions {
    const bool *remove_sections;
    bool strip_all;
    bool strip_debug;
    const char *const *keep_global_symbols;
    size_t keep_global_count;
    /* Objcopy's RISC-V PI relocation/section namespace transformation. */
    const char *prefix_symbols;
    const char *prefix_alloc_sections;
} MiniElfRewriteOptions;

bool minielf_rewrite(const MiniElfView *view,
                     const MiniElfRewriteOptions *options,
                     unsigned char **image_out,
                     size_t *size_out,
                     MiniElfRewriteError *error_out);
const char *minielf_rewrite_error_string(MiniElfRewriteError error);

#endif
