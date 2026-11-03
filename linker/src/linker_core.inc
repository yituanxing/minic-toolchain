#include "minild.h"
#include "linker_script.h"
#include "riscv_elf_abi.h"
#include "minielf.h"

#include <elf.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
    /* Source name remains immutable when a linker script chooses the
     * final output name. The names have different owners and lifetimes. */
    const char *input_name;
    /* Non-owning path of the exact input fragment, NULL for synthesized or
     * legacy merged sections.  InputFile owns this string until teardown. */
    const char *input_path;
    const char *output_name;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    uint64_t entsize;
    unsigned char *data;
    size_t size;
    size_t capacity;
    bool has_group;
    uint32_t group_flags;
    size_t group_signature_symbol;
    bool has_link_order_target;
    size_t link_order_target;
    bool gc_discarded;
    bool script_selected;
    bool script_keep;
    bool script_discard;
    /* Only canonical merged/synthetic sections participate in name lookup.
     * Distinct input fragments may share a spelling but never own that name. */
    bool indexable;
} MiniLdSection;

typedef struct MiniLdResolvedSymbol {
    char *name;
    int section;
    uint64_t value;
    uint64_t size;
    unsigned char info;
    unsigned char other;
    /* Canonical resolution/relocation index stays stable. This selects the
     * winning immutable input definition, or SIZE_MAX for a synthetic/script
     * definition. The current section/value above remain mutable. */
    size_t source_symbol;
    bool group_signature_section;
} MiniLdResolvedSymbol;

/* An original ET_REL symbol has its own identity even when it loses a
 * weak/strong resolution or a COMDAT selection. Never edit this record as
 * relaxation, GC, COMMON allocation or script definitions transform the
 * canonical resolved symbol. */
typedef struct MiniLdInputSymbol {
    char *name;
    size_t file;
    size_t elf_index;
    uint32_t elf_section;
    size_t input_section;
    uint64_t value;
    uint64_t size;
    unsigned char info;
    unsigned char other;
    /* Source -> canonical reference, SIZE_MAX for discarded local inputs.
     * This is not a claim that this source currently wins resolution. */
    size_t canonical_symbol;
} MiniLdInputSymbol;

typedef struct MiniLdReloc {
    size_t section;
    uint64_t offset;
    uint32_t type;
    size_t symbol;
    int64_t addend;
    size_t input_section;
    uint64_t input_offset;
} MiniLdReloc;

/* Current relocation targets and offsets are mutable working state. The
 * original input-section ID and original offset remain stable in each entry.
 * CSR section lookup is created only after all work-section permutations and
 * relocation filtering have completed. */
typedef struct MiniLdRelocationWorkspace {
    MiniLdReloc *entries;
    size_t count;
    size_t capacity;
    size_t *section_starts;
    size_t *section_refs;
    size_t index_sections;
} MiniLdRelocationWorkspace;

/* Input identity survives name-based output concatenation.  The input owner
 * retains its exact bytes and section-header metadata so GC and future
 * COMDAT/link-order selection never have to infer object boundaries from
 * STT_SECTION symbols or padding in the merged buffer. */
typedef struct MiniLdInputFile {
    char *path;
    size_t first_section;
    size_t section_count;
    /* ET_REL symbols are appended contiguously for each input object,
     * including archive members and otherwise discarded COMDAT candidates. */
    size_t first_symbol;
    size_t symbol_count;
    /* Original ELF shndx -> stable input-section ID (or SIZE_MAX for metadata). */
    size_t *section_ids;
    size_t section_slot_count;
    uint32_t elf_flags;
} MiniLdInputFile;

typedef struct MiniLdInputSection {
    size_t file;
    size_t elf_index;
    char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    uint64_t entsize;
    uint32_t link;
    uint32_t info;
    unsigned char *data;
    size_t size;
    size_t link_order_input;
    size_t group_input;
} MiniLdInputSection;

/* References provided by ET_DYN inputs are source facts, not synthesized
 * dynamic output sections. Their order is the original admission order. */
typedef struct MiniLdDsoCatalog {
    char **needed_names;
    size_t needed_count;
    size_t needed_capacity;
    char **symbol_names;
    size_t symbol_count;
    size_t symbol_capacity;
} MiniLdDsoCatalog;

/* Stable input identities and their lifetime belong to one graph.  A file's
 * section/symbol ranges are append-only, and ELF shndx maps to a stable input
 * section ID.  Placement and script/GC selection are never stored here. */
typedef struct MiniLdInputGraph {
    MiniLdInputFile *files;
    size_t file_count;
    size_t file_capacity;
    MiniLdInputSection *sections;
    size_t section_count;
    size_t section_capacity;
    MiniLdInputSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    /* One first-wins COMDAT signature registry across every admitted object,
     * including lazily extracted archive members. */
    char **comdat_signatures;
    size_t comdat_signature_count;
    size_t comdat_signature_capacity;
    MiniLdDsoCatalog dso;
} MiniLdInputGraph;

typedef struct MiniLdIndexSlot {
    uint64_t hash;
    size_t index_plus_one;
} MiniLdIndexSlot;

/* Parallel to InputGraph.sections, indexed by stable input-section ID.
 * The initial coalesced section is used only during materialization; the
 * current work section is remapped whenever work sections are reordered. */
typedef struct MiniLdInputPlacement {
    size_t merged_section;
    size_t current_section;
} MiniLdInputPlacement;

typedef struct MiniLdSectionWorkspace {
    MiniLdSection *sections;
    size_t count;
    size_t capacity;
    MiniLdIndexSlot *index;
    size_t index_capacity;
    MiniLdInputPlacement *placements;
    size_t placement_count;
    size_t placement_capacity;
} MiniLdSectionWorkspace;

/* Owns the stable canonical symbol IDs and their global-name index.  Input
 * symbols remain immutable in the input owner; layout, GC and scripts may
 * transform canonical entries without changing their IDs. */
typedef struct MiniLdSymbolResolver {
    MiniLdResolvedSymbol *entries;
    size_t count;
    size_t capacity;
    MiniLdIndexSlot *global_slots;
    size_t global_capacity;
    size_t global_count;
} MiniLdSymbolResolver;

static void symbol_resolver_destroy(MiniLdSymbolResolver *resolver);
static void work_sections_destroy(MiniLdSectionWorkspace *work);

typedef struct MiniLdState {
    MiniLdInputGraph inputs;
    MiniLdSectionWorkspace work;
    MiniLdSymbolResolver resolver;
    MiniLdRelocationWorkspace relocations;
    uint32_t elf_flags;
    size_t processed_object_count;
    bool have_input;
    bool allow_dso_inputs;
    bool relocatable_output;
    FILE *diagnostics;
} MiniLdState;

/* Implemented by the final-link input-section owner in linker.c, after this
 * shared core is included.  The EH header indexer scans materialized CFI
 * inputs independently, so shared scripts retain full source identity. */
static bool minild_materialize_input_sections(
    MiniLdState *state, bool include_nonalloc);
static bool minild_restore_input_order(MiniLdState *state);
static bool minild_shared_prune_script_inputs(MiniLdState *state,
                                              const MiniLdScript *script);

typedef struct MiniLdSectionMap {
    size_t output_section;
    uint64_t base;
    size_t input_section;
    bool mapped;
    bool discarded;
} MiniLdSectionMap;

typedef struct MiniLdParsedGroup {
    size_t signature_symbol;
    uint32_t flags;
    bool selected;
} MiniLdParsedGroup;

typedef enum MiniLdArchiveKind {
    MINILD_ARCHIVE_THIN = 1,
    MINILD_ARCHIVE_REGULAR = 2
} MiniLdArchiveKind;

typedef struct MiniLdArchiveMember {
    char *name;
    /* Thin members own an external path; regular members borrow archive data. */
    char *path;
    size_t data_offset;
    size_t data_size;
    bool selected;
} MiniLdArchiveMember;

typedef struct MiniLdArchive {
    MiniLdArchiveKind kind;
    unsigned char *data;
    size_t size;
    MiniLdArchiveMember *members;
    size_t count;
    size_t capacity;
} MiniLdArchive;

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
    if (size != 0U) {
        memset(buffer->data + buffer->size, 0, size);
    }
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

static bool string_set_append(char ***items,
                              size_t *count,
                              size_t *capacity,
                              const char *name);

#include "linker_input.inc"

#include "linker_relocations.inc"

static void state_destroy(MiniLdState *state) {
    work_sections_destroy(&state->work);
    symbol_resolver_destroy(&state->resolver);
    input_graph_destroy(&state->inputs);
    relocations_destroy(&state->relocations);
}

static bool string_set_append(char ***items,
                              size_t *count,
                              size_t *capacity,
                              const char *text) {
    size_t i;
    size_t next_capacity;
    char **next;
    char *copy;

    for (i = 0U; i < *count; ++i) {
        if (strcmp((*items)[i], text) == 0) {
            return true;
        }
    }
    if (*count == *capacity) {
        next_capacity = *capacity == 0U ? 16U : *capacity * 2U;
        if (next_capacity < *capacity ||
            next_capacity > SIZE_MAX / sizeof(**items)) {
            return false;
        }
        next = realloc(*items, next_capacity * sizeof(**items));
        if (next == NULL) {
            return false;
        }
        *items = next;
        *capacity = next_capacity;
    }
    copy = minild_strdup(text);
    if (copy == NULL) {
        return false;
    }
    (*items)[(*count)++] = copy;
    return true;
}

static bool state_add_dso_needed(MiniLdState *state, const char *name) {
    if (!input_graph_add_dso_needed(&state->inputs, name)) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:dso-needed\n");
        return false;
    }
    return true;
}

static bool state_add_dso_symbol(MiniLdState *state, const char *name) {
    if (!input_graph_add_dso_symbol(&state->inputs, name)) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:dso-symbol\n");
        return false;
    }
    return true;
}

static bool state_dso_defines(const MiniLdState *state, const char *name) {
    return input_graph_dso_defines(&state->inputs, name);
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

/* WorkSection owns the mutable section array, byte buffers and name index.
 * Its index rebuild is also used by the final-link permutation transaction. */
#include "linker_work_sections.inc"

static bool ensure_section_capacity(MiniLdState *state) {
    return work_sections_ensure_capacity(&state->work);
}

static bool rebuild_section_index(MiniLdState *state, size_t capacity) {
    return work_sections_rebuild_index(&state->work, capacity);
}

static size_t find_section_index(const MiniLdState *state,
                                 const char *name,
                                 uint64_t hash) {
    return work_sections_find_index(&state->work, name, hash);
}

/* Symbol resolution has its own owner; input, archive, script and output
 * stages consume its narrow API without managing its name index. */
#include "linker_symbol.inc"

static bool find_or_add_section_with_identity(MiniLdState *state,
                                const char *name,
                                uint32_t type,
                                uint64_t flags,
                                uint64_t align,
                                uint64_t entsize,
                                bool distinct,
                                size_t *section_out) {
    return work_sections_find_or_add(&state->work, state->diagnostics,
                                     name, type, flags, align, entsize,
                                     distinct, section_out);
}

static bool find_or_add_section(MiniLdState *state,
                                const char *name,
                                uint32_t type,
                                uint64_t flags,
                                uint64_t align,
                                uint64_t entsize,
                                size_t *section_out) {
    return find_or_add_section_with_identity(state, name, type, flags,
                                             align, entsize, false,
                                             section_out);
}

static bool section_supported_for_merge(const MiniElfSection *section) {
    if (section->type == SHT_SYMTAB_SHNDX ||
        section->type == SHT_REL) {
        return false;
    }
    return true;
}

static uint32_t input_group_u32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

/* Groups are selected before *any* member bytes or global definitions are
 * admitted.  In particular a discarded COMDAT must not introduce a duplicate
 * strong symbol or an undefined relocation into the link. */
static bool select_input_groups(MiniLdState *state,
                                const MiniElfView *elf,
                                size_t symtab_index,
                                size_t strtab_index,
                                size_t symbol_count,
                                const char *path,
                                MiniLdParsedGroup *groups,
                                size_t *group_for_section) {
    size_t i;

    for (i = 0U; i < elf->section_count; ++i) {
        group_for_section[i] = SIZE_MAX;
    }
    for (i = 1U; i < elf->section_count; ++i) {
        MiniElfSection group;
        const unsigned char *bytes;
        size_t length;
        MiniElfSymbol signature;
        const char *signature_name;
        uint32_t flags;
        size_t j;

        if (!minielf_section(elf, i, &group)) {
            return false;
        }
        if (group.type != SHT_GROUP) {
            continue;
        }
        if (group.link != symtab_index || group.info == 0U ||
            group.info >= symbol_count || group.size < 8U ||
            (group.size & 3U) != 0U ||
            !minielf_symbol(elf, symtab_index, group.info, &signature) ||
            !minielf_string(elf, strtab_index, signature.name,
                            &signature_name) ||
            !minielf_section_data(elf, i, &bytes, &length)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-section-group:%s:%zu\n", path, i);
            return false;
        }
        if (signature_name[0] == '\0' &&
            minielf_symbol_type(signature.info) == STT_SECTION &&
            signature.section_index < elf->section_count &&
            !minielf_section_name(elf, signature.section_index,
                                  &signature_name)) {
            return false;
        }
        flags = input_group_u32(bytes);
        if (signature_name[0] == '\0' ||
            (flags & ~(uint32_t)GRP_COMDAT) != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-section-group:%s:%zu\n", path, i);
            return false;
        }
        groups[i].signature_symbol = group.info;
        groups[i].flags = flags;
        groups[i].selected = true;
        if ((flags & GRP_COMDAT) != 0U) {
            if (!input_graph_select_comdat(&state->inputs,
                                           signature_name,
                                           &groups[i].selected)) {
                fprintf(state->diagnostics,
                        "minic-ld: out-of-memory:comdat-signature\n");
                return false;
            }
        }
        for (j = 4U; j < length; j += 4U) {
            size_t member = input_group_u32(bytes + j);
            MiniElfSection member_header;

            if (member == 0U || member >= elf->section_count ||
                group_for_section[member] != SIZE_MAX ||
                !minielf_section(elf, member, &member_header) ||
                member_header.type == SHT_GROUP ||
                (member_header.flags & SHF_GROUP) == 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-group-member:%s:%zu\n", path, member);
                return false;
            }
            group_for_section[member] = i;
        }
    }
    for (i = 1U; i < elf->section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(elf, i, &section)) {
            return false;
        }
        if ((section.flags & SHF_GROUP) != 0U &&
            group_for_section[i] == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: orphan-group-member:%s:%zu\n", path, i);
            return false;
        }
    }
    return true;
}

/* ELF processor-specific RISC-V attributes are a single 'A' format
 * marker followed by length-delimited vendor subsections. Ordinary
 * SHF_MERGE-style concatenation of complete input sections creates
 * 'A vendor A vendor', which readelf cannot parse. Preserve each input
 * vendor subsection (including unknown vendors) but emit 'A' only once.
 * Validate all bounds before accepting a foreign .o or archive member.
 * This is metadata, not executable data and not an ABI override. */
#define MINILD_SHT_RISCV_ATTRIBUTES UINT32_C(0x70000003)
static bool riscv_attributes_valid(const unsigned char *bytes, size_t size) {
    size_t pos = 1U;

    if (size < 7U || bytes[0] != (unsigned char)'A') {
        return false;
    }
    while (pos < size) {
        size_t vendor_end;
        uint32_t length;
        if (!range_ok(pos, 4U, size)) {
            return false;
        }
        length = (uint32_t)bytes[pos] |
                 ((uint32_t)bytes[pos + 1U] << 8U) |
                 ((uint32_t)bytes[pos + 2U] << 16U) |
                 ((uint32_t)bytes[pos + 3U] << 24U);
        if (length < 6U || !range_ok(pos, (size_t)length, size)) {
            return false;
        }
        vendor_end = pos + 4U;
        while (vendor_end < pos + (size_t)length &&
               bytes[vendor_end] != 0U) {
            ++vendor_end;
        }
        if (vendor_end == pos + (size_t)length ||
            vendor_end == pos + 4U) {
            return false;
        }
        pos += (size_t)length;
    }
    return pos == size;
}

/* Admit one stable input-section record and its separately owned initial
 * working-section placement. Both tables grow in the same admission order;
 * neither a later GC pass nor an output sort can renumber the input ID. */
static bool admit_input_section(MiniLdState *state,
                                size_t file,
                                size_t elf_index,
                                const char *name,
                                const MiniElfSection *header,
                                const unsigned char *bytes,
                                size_t data_size,
                                size_t merged_section,
                                size_t *input_id_out) {
    size_t placement_before = state->work.placement_count;

    if (placement_before != state->inputs.section_count ||
        !work_placement_append(&state->work, merged_section)) {
        return false;
    }
    if (!input_graph_record_section(&state->inputs, file, elf_index,
                                    name, header, bytes, data_size,
                                    input_id_out)) {
        state->work.placement_count = placement_before;
        return false;
    }
    return *input_id_out == placement_before;
}

static bool process_input_data(MiniLdState *state,
                               const unsigned char *data,
                               size_t size,
                               const char *path) {
    MiniElfView elf;
    MiniElfSection shstrtab;
    MiniElfSection symtab;
    MiniElfSection strtab;
    size_t symtab_index = SIZE_MAX;
    size_t strtab_index = SIZE_MAX;
    size_t file_index = SIZE_MAX;
    MiniLdSectionMap *section_map = NULL;
    MiniLdParsedGroup *groups = NULL;
    size_t *group_for_section = NULL;
    bool *link_order_targets = NULL;
    size_t *symbol_map = NULL;
    size_t symbol_count = 0U;
    size_t i;
    bool incoming_has_code = false;
    bool ok = false;

    if (!minielf_open(&elf, data, size) ||
        elf.elf_class != ELFCLASS64 ||
        elf.data_encoding != ELFDATA2LSB ||
        elf.type != ET_REL ||
        elf.machine != EM_RISCV ||
        elf.version != EV_CURRENT ||
        elf.section_count == 0U ||
        elf.section_name_table_index == SHN_UNDEF) {
        fprintf(state->diagnostics, "minic-ld: unsupported-elf:%s\n", path);
        goto done;
    }
    if (!minielf_section(&elf, elf.section_name_table_index, &shstrtab) ||
        shstrtab.type != SHT_STRTAB) {
        fprintf(state->diagnostics, "minic-ld: invalid-shstrtab:%s\n", path);
        goto done;
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(&elf, i, &section)) {
            fprintf(state->diagnostics, "minic-ld: invalid-section-table:%s\n", path);
            goto done;
        }
        if (section.type == SHT_PROGBITS && section.size != 0U &&
            (section.flags & (SHF_ALLOC | SHF_EXECINSTR)) ==
                (SHF_ALLOC | SHF_EXECINSTR)) {
            incoming_has_code = true;
        }
        if (section.type == SHT_SYMTAB) {
            if (symtab_index != SIZE_MAX) {
                fprintf(state->diagnostics, "minic-ld: multiple-symtabs:%s\n", path);
                goto done;
            }
            symtab = section;
            symtab_index = i;
        }
    }
    if (symtab_index == SIZE_MAX ||
        symtab.link >= elf.section_count ||
        symtab.entry_size < sizeof(Elf64_Sym)) {
        fprintf(state->diagnostics, "minic-ld: missing-symtab:%s\n", path);
        goto done;
    }
    strtab_index = symtab.link;
    if (!minielf_section(&elf, strtab_index, &strtab) ||
        strtab.type != SHT_STRTAB) {
        fprintf(state->diagnostics, "minic-ld: invalid-symtab:%s\n", path);
        goto done;
    }
    symbol_count = minielf_symbol_count(&elf, &symtab);
    if (symbol_count == 0U) {
        fprintf(state->diagnostics, "minic-ld: invalid-symtab:%s\n", path);
        goto done;
    }
    groups = calloc(elf.section_count, sizeof(*groups));
    group_for_section = malloc(elf.section_count * sizeof(*group_for_section));
    link_order_targets = calloc(elf.section_count, sizeof(*link_order_targets));
    if (groups == NULL || group_for_section == NULL ||
        link_order_targets == NULL ||
        !select_input_groups(state, &elf, symtab_index, strtab_index,
                             symbol_count, path, groups, group_for_section)) {
        if (groups == NULL || group_for_section == NULL ||
            link_order_targets == NULL) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:input-groups\n");
        }
        goto done;
    }
    /* An -r output must preserve the target's identity just as carefully as
     * the LINK_ORDER metadata identity. Otherwise two same-named targets
     * merge and both metadata sections point at that single merged target. */
    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection header;

        if (!minielf_section(&elf, i, &header)) {
            goto done;
        }
        if ((header.flags & SHF_LINK_ORDER) != 0U && header.link != 0U) {
            if (header.link >= elf.section_count) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-link-order:%s:%zu\n", path, i);
                goto done;
            }
            link_order_targets[header.link] = true;
        }
    }
    if (!input_graph_record_file(&state->inputs, path, elf.flags,
                           elf.section_count, &file_index)) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:input-file\n");
        goto done;
    }

    section_map = calloc(elf.section_count, sizeof(*section_map));
    if (section_map == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:section-map\n");
        goto done;
    }

    /* Keep selected group headers as first-class *input* nodes.  The ELF
     * writer synthesizes output SHT_GROUP headers from their live members. */
    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection header;
        const char *name;
        const unsigned char *bytes;
        size_t length;

        if (!minielf_section(&elf, i, &header) ||
            header.type != SHT_GROUP || !groups[i].selected) {
            continue;
        }
        if (!minielf_section_name(&elf, i, &name) ||
            !minielf_section_data(&elf, i, &bytes, &length) ||
            !admit_input_section(state, file_index, i,
                                  name, &header,
                                  bytes, length, SIZE_MAX,
                                  &section_map[i].input_section)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-input-group-header:%s:%zu\n", path, i);
            goto done;
        }
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection input;
        const char *name;
        size_t output_index;
        MiniLdSection *output;
        size_t base;
        uint64_t alignment;
        const unsigned char *section_data;
        size_t section_size;
        size_t group_index;
        uint64_t output_flags;
        bool distinct;

        if (!minielf_section(&elf, i, &input)) {
            fprintf(state->diagnostics, "minic-ld: invalid-section-table:%s\n", path);
            goto done;
        }
        if (i == symtab_index || i == strtab_index ||
            i == elf.section_name_table_index ||
            input.type == SHT_RELA || input.type == SHT_GROUP) {
            continue;
        }
        group_index = group_for_section[i];
        if (group_index != SIZE_MAX && !groups[group_index].selected) {
            section_map[i].discarded = true;
            continue;
        }
        if (!section_supported_for_merge(&input)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-section-type:%s:index=%zu:type=%u\n",
                    path,
                    i,
                    input.type);
            goto done;
        }
        if (!minielf_section_name(&elf, i, &name)) {
            fprintf(state->diagnostics, "minic-ld: invalid-section-name:%s\n", path);
            goto done;
        }
        alignment = input.alignment == 0U ? 1U : input.alignment;
        output_flags = input.flags;
        if (!state->relocatable_output) {
            output_flags &= ~(uint64_t)SHF_GROUP;
        }
        distinct = ((input.flags & SHF_LINK_ORDER) != 0U) ||
                   (state->relocatable_output &&
                    (group_index != SIZE_MAX || link_order_targets[i]));
        if (!find_or_add_section_with_identity(state,
                                 name,
                                 input.type,
                                 output_flags,
                                 alignment,
                                 input.entry_size,
                                 distinct,
                                 &output_index)) {
            goto done;
        }
        output = &state->work.sections[output_index];
        if (input.type == MINILD_SHT_RISCV_ATTRIBUTES) {
            /* Each input has its own format marker; the output must not.
             * Keep the complete vendor records instead of dropping ISA
             * facts, fabricating a target triple or using GNU output. */
            if (strcmp(name, ".riscv.attributes") != 0 ||
                input.flags != 0U ||
                !minielf_section_data(&elf, i, &section_data,
                                      &section_size) ||
                !riscv_attributes_valid(section_data, section_size) ||
                !section_append_data(
                    output,
                    section_data + (output->size == 0U ? 0U : 1U),
                    section_size - (output->size == 0U ? 0U : 1U))) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-riscv-attributes:%s\n", path);
                goto done;
            }
            section_map[i].output_section = output_index;
            section_map[i].base = 0U;
            if (!admit_input_section(state, file_index, i,
                                      name, &input,
                                      section_data, section_size,
                                      output_index,
                                      &section_map[i].input_section)) {
                fprintf(state->diagnostics,
                        "minic-ld: out-of-memory:input-section\n");
                goto done;
            }
            section_map[i].mapped = true;
            continue;
        }
        if (!align_up_size(output->size, alignment, &base) ||
            !section_append_zero(output, base - output->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: cannot-align-section:%s\n",
                    name);
            goto done;
        }
        if (input.size > SIZE_MAX) {
            fprintf(state->diagnostics, "minic-ld: section-too-large:%s\n", name);
            goto done;
        }
        if (input.type == SHT_NOBITS) {
            if (!section_append_zero(output, (size_t)input.size)) {
                fprintf(state->diagnostics, "minic-ld: section-overflow:%s\n", name);
                goto done;
            }
        } else {
            if (!minielf_section_data(&elf, i, &section_data, &section_size) ||
                !section_append_data(output, section_data, section_size)) {
                fprintf(state->diagnostics, "minic-ld: invalid-section-data:%s\n", name);
                goto done;
            }
        }
        if (!admit_input_section(state, file_index, i,
                                  name, &input,
                                  input.type == SHT_NOBITS ? NULL : section_data,
                                  input.type == SHT_NOBITS ? 0U : section_size,
                                  output_index,
                                  &section_map[i].input_section)) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:input-section\n");
            goto done;
        }
        if (group_index != SIZE_MAX) {
            MiniLdInputSection *source =
                &state->inputs.sections[section_map[i].input_section];
            source->group_input = section_map[group_index].input_section;
            if (state->relocatable_output) {
                output->has_group = true;
                output->group_flags = groups[group_index].flags;
            }
        }
        section_map[i].output_section = output_index;
        section_map[i].base = (uint64_t)base;
        section_map[i].mapped = true;
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection input;
        MiniLdInputSection *source;
        size_t target;

        if (!section_map[i].mapped ||
            !minielf_section(&elf, i, &input) ||
            (input.flags & SHF_LINK_ORDER) == 0U || input.link == 0U) {
            continue;
        }
        if (input.link >= elf.section_count ||
            !section_map[input.link].mapped) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-link-order:%s:%zu\n", path, i);
            goto done;
        }
        source = &state->inputs.sections[section_map[i].input_section];
        source->link_order_input = section_map[input.link].input_section;
        target = section_map[input.link].output_section;
        state->work.sections[section_map[i].output_section].has_link_order_target = true;
        state->work.sections[section_map[i].output_section].link_order_target = target;
    }

    symbol_map = malloc(symbol_count * sizeof(*symbol_map));
    if (symbol_map == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:symbol-map\n");
        goto done;
    }
    for (i = 0U; i < symbol_count; ++i) {
        symbol_map[i] = SIZE_MAX;
    }

    for (i = 1U; i < symbol_count; ++i) {
        MiniElfSymbol input_symbol;
        const char *name;
        int output_section = MINILD_SECTION_UNDEF;
        uint64_t value;
        size_t input_section = SIZE_MAX;
        size_t source_symbol;
        bool group_signature_section = false;
        size_t output_symbol;
        unsigned bind;

        if (!minielf_symbol(&elf, symtab_index, i, &input_symbol)) {
            fprintf(state->diagnostics, "minic-ld: truncated-symbol:%s\n", path);
            goto done;
        }
        if (!minielf_string(&elf, strtab_index, input_symbol.name, &name)) {
            fprintf(state->diagnostics, "minic-ld: invalid-symbol-name:%s\n", path);
            goto done;
        }
        /* Record every input candidate, including discarded COMDAT locals
         * and globals which will lose resolution. This table is never
         * rewritten by GC, scripts, or address assignment. */
        if (!input_graph_record_symbol(&state->inputs, state->diagnostics,
                                 file_index, i, name, &input_symbol,
                                 &source_symbol)) {
            goto done;
        }
        value = input_symbol.value;
        if (input_symbol.section_index == SHN_UNDEF) {
            output_section = MINILD_SECTION_UNDEF;
        } else if (input_symbol.section_index == SHN_ABS) {
            output_section = MINILD_SECTION_ABS;
        } else if (input_symbol.section_index == SHN_COMMON) {
            output_section = MINILD_SECTION_COMMON;
        } else if (input_symbol.section_index < elf.section_count &&
                   groups[input_symbol.section_index].signature_symbol == i &&
                   minielf_symbol_type(input_symbol.info) == STT_SECTION) {
            output_section = MINILD_SECTION_ABS;
            group_signature_section = true;
        } else if (input_symbol.section_index < elf.section_count &&
                   section_map[input_symbol.section_index].discarded) {
            /* A discarded COMDAT definition is an ordinary reference to
             * the selected definition, not a second strong definition. */
            output_section = MINILD_SECTION_UNDEF;
            if (minielf_symbol_bind(input_symbol.info) == STB_LOCAL) {
                continue;
            }
        } else if (input_symbol.section_index >= elf.section_count ||
                   !section_map[input_symbol.section_index].mapped) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-symbol-section:%s:%s\n",
                    path,
                    name);
            goto done;
        } else {
            output_section =
                (int)section_map[input_symbol.section_index].output_section;
            value += section_map[input_symbol.section_index].base;
            input_section = section_map[input_symbol.section_index].input_section;
        }
        state->inputs.symbols[source_symbol].input_section = input_section;

        bind = minielf_symbol_bind(input_symbol.info);
        if (bind == STB_LOCAL) {
            if (!symbol_resolver_add_local(&state->resolver,
                                  state->diagnostics,
                                  name,
                                  output_section,
                                  value,
                                  input_symbol.size,
                                  input_symbol.info,
                                  input_symbol.other,
                                  source_symbol,
                                  &output_symbol)) {
                goto done;
            }
        } else if (bind == STB_GLOBAL || bind == STB_WEAK) {
            if (!symbol_resolver_merge_global(&state->resolver,
                                            state->diagnostics,
                                            name,
                                            output_section,
                                            value,
                                            input_symbol.size,
                                            input_symbol.info,
                                            input_symbol.other,
                                            source_symbol,
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
        state->inputs.symbols[source_symbol].canonical_symbol = output_symbol;
        if (group_signature_section) {
            state->resolver.entries[output_symbol].group_signature_section = true;
        }
    }

    for (i = 1U; i < elf.section_count; ++i) {
        size_t group_index = group_for_section[i];
        size_t signature_id;

        if (!state->relocatable_output || !section_map[i].mapped ||
            group_index == SIZE_MAX ||
            group_index == i /* SHT_GROUP itself is never mapped */) {
            continue;
        }
        signature_id = symbol_map[groups[group_index].signature_symbol];
        if (signature_id == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-group-signature:%s:%zu\n", path, i);
            goto done;
        }
        state->work.sections[section_map[i].output_section].group_signature_symbol =
            signature_id;
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection rela_section;
        size_t count;
        size_t j;
        MiniLdSectionMap target_map;

        if (!minielf_section(&elf, i, &rela_section)) {
            fprintf(state->diagnostics, "minic-ld: invalid-section-table:%s\n", path);
            goto done;
        }
        if (rela_section.type != SHT_RELA) {
            continue;
        }
        if (group_for_section[i] != SIZE_MAX &&
            !groups[group_for_section[i]].selected) {
            continue;
        }
        if (rela_section.link != symtab_index ||
            rela_section.info >= elf.section_count ||
            !section_map[rela_section.info].mapped ||
            rela_section.entry_size < sizeof(Elf64_Rela)) {
            fprintf(state->diagnostics, "minic-ld: invalid-rela:%s\n", path);
            goto done;
        }
        target_map = section_map[rela_section.info];
        count = minielf_rela_count(&elf, &rela_section);
        if (count == 0U && rela_section.size != 0U) {
            fprintf(state->diagnostics, "minic-ld: invalid-rela:%s\n", path);
            goto done;
        }
        for (j = 0U; j < count; ++j) {
            MiniElfRela rela;
            size_t input_symbol_index;
            size_t output_symbol = SIZE_MAX;

            if (!minielf_rela(&elf, i, j, &rela)) {
                fprintf(state->diagnostics, "minic-ld: truncated-rela:%s\n", path);
                goto done;
            }
            input_symbol_index = minielf_rela_symbol(&elf, rela.info);
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
            if (!relocations_append(&state->relocations, state->diagnostics,
                                target_map.output_section,
                                rela.offset + target_map.base,
                                minielf_rela_type(&elf, rela.info),
                                output_symbol,
                                rela.addend,
                                target_map.input_section,
                                rela.offset)) {
                goto done;
            }
        }
    }

    {
        uint32_t merged_flags = 0U;
        const char *abi_reason = NULL;

        if (!minild_riscv_elf_abi_merge(
                state->elf_flags,
                elf.flags,
                state->processed_object_count != 0U,
                incoming_has_code,
                &merged_flags,
                &abi_reason)) {
            fprintf(state->diagnostics,
                    "minic-ld: incompatible-riscv-elf-flags:%s:%s:"
                    "current=0x%08x:incoming=0x%08x\n",
                    path,
                    abi_reason == NULL ? "unknown" : abi_reason,
                    (unsigned)state->elf_flags,
                    (unsigned)elf.flags);
            goto done;
        }
        state->elf_flags = merged_flags;
    }
    ++state->processed_object_count;
    state->have_input = true;
    ok = true;

done:
    free(symbol_map);
    free(section_map);
    free(group_for_section);
    free(link_order_targets);
    free(groups);
    return ok;
}

static bool process_dso_input_data(MiniLdState *state,
                                   const unsigned char *data,
                                   size_t size,
                                   const char *path) {
    MiniElfView elf;
    MiniElfSection dynsym = {0};
    MiniElfSection dynamic = {0};
    size_t dynsym_index = SIZE_MAX;
    size_t dynamic_index = SIZE_MAX;
    const char *needed_name = NULL;
    size_t i;

    if (!minielf_open(&elf, data, size) ||
        elf.elf_class != ELFCLASS64 ||
        elf.data_encoding != ELFDATA2LSB ||
        elf.type != ET_DYN ||
        elf.machine != EM_RISCV ||
        elf.version != EV_CURRENT) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-shared-input:%s\n",
                path);
        return false;
    }
    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(&elf, i, &section)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-input-section:%s\n",
                    path);
            return false;
        }
        if (section.type == SHT_DYNSYM) {
            if (dynsym_index != SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: multiple-dynsym:%s\n",
                        path);
                return false;
            }
            dynsym = section;
            dynsym_index = i;
        } else if (section.type == SHT_DYNAMIC &&
                   dynamic_index == SIZE_MAX) {
            dynamic = section;
            dynamic_index = i;
        }
    }
    if (dynsym_index == SIZE_MAX ||
        dynsym.link >= elf.section_count ||
        dynsym.entry_size < sizeof(Elf64_Sym)) {
        fprintf(state->diagnostics,
                "minic-ld: missing-dynsym:%s\n",
                path);
        return false;
    }

    if (dynamic_index != SIZE_MAX) {
        const unsigned char *dynamic_data;
        size_t dynamic_size;
        size_t entry_size =
            dynamic.entry_size == 0U ? sizeof(Elf64_Dyn)
                                     : (size_t)dynamic.entry_size;

        if (dynamic.link >= elf.section_count ||
            entry_size < sizeof(Elf64_Dyn) ||
            !minielf_section_data(&elf,
                                  dynamic_index,
                                  &dynamic_data,
                                  &dynamic_size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-dynamic:%s\n",
                    path);
            return false;
        }
        for (i = 0U; i + sizeof(Elf64_Dyn) <= dynamic_size;
             i += entry_size) {
            Elf64_Dyn entry;

            if (entry_size > dynamic_size - i) {
                break;
            }
            memcpy(&entry, dynamic_data + i, sizeof(entry));
            if (entry.d_tag == DT_NULL) {
                break;
            }
            if (entry.d_tag == DT_SONAME) {
                if (entry.d_un.d_val > UINT32_MAX ||
                    !minielf_string(&elf,
                                    dynamic.link,
                                    (uint32_t)entry.d_un.d_val,
                                    &needed_name)) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-shared-soname:%s\n",
                            path);
                    return false;
                }
                break;
            }
        }
    }
    if (needed_name == NULL || needed_name[0] == '\0') {
        const char *slash = strrchr(path, '/');
        needed_name = slash == NULL ? path : slash + 1;
    }
    if (needed_name[0] == '\0' ||
        !state_add_dso_needed(state, needed_name)) {
        return false;
    }

    {
        size_t count = minielf_symbol_count(&elf, &dynsym);

        if (count == 0U && dynsym.size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-dynsym:%s\n",
                    path);
            return false;
        }
        for (i = 1U; i < count; ++i) {
            MiniElfSymbol symbol;
            const char *name;
            unsigned bind;
            unsigned visibility;

            if (!minielf_symbol(&elf, dynsym_index, i, &symbol)) {
                fprintf(state->diagnostics,
                        "minic-ld: truncated-dynsym:%s\n",
                        path);
                return false;
            }
            bind = minielf_symbol_bind(symbol.info);
            visibility = ELF64_ST_VISIBILITY(symbol.other);
            if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
                symbol.section_index == SHN_UNDEF ||
                symbol.name == 0U ||
                (visibility != STV_DEFAULT &&
                 visibility != STV_PROTECTED)) {
                continue;
            }
            if (!minielf_string(&elf, dynsym.link, symbol.name, &name)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-dynsym-name:%s\n",
                        path);
                return false;
            }
            if (!state_add_dso_symbol(state, name)) {
                return false;
            }
        }
    }
    return true;
}

static bool process_input(MiniLdState *state, const char *path) {
    unsigned char *data = NULL;
    size_t size = 0U;
    MiniElfView elf;
    bool ok;

    if (!read_file(path, &data, &size, state->diagnostics)) {
        return false;
    }
    if (minielf_open(&elf, data, size) && elf.type == ET_DYN) {
        if (!state->allow_dso_inputs) {
            fprintf(state->diagnostics,
                    "minic-ld: shared-input-requires-dynamic-link:%s\n",
                    path);
            ok = false;
        } else {
            ok = process_dso_input_data(state, data, size, path);
        }
    } else {
        ok = process_input_data(state, data, size, path);
    }
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

static void archive_destroy(MiniLdArchive *archive) {
    size_t i;

    for (i = 0U; i < archive->count; ++i) {
        free(archive->members[i].name);
        free(archive->members[i].path);
    }
    free(archive->members);
    free(archive->data);
    memset(archive, 0, sizeof(*archive));
}

static bool archive_member_append(MiniLdArchive *archive,
                                  char *name,
                                  char *path,
                                  size_t offset,
                                  size_t size) {
    MiniLdArchiveMember *next;
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
    archive->members[archive->count++] = (MiniLdArchiveMember){
        .name = name,
        .path = path,
        .data_offset = offset,
        .data_size = size,
        .selected = false
    };
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
                                      MiniLdArchive *archive,
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
            if (path == NULL ||
                !archive_member_append(archive, name, path, 0U, 0U)) {
                free(name);
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
    return symbol_resolver_needs_archive(&state->resolver, name);
}

static bool object_data_defines_needed_symbol(MiniLdState *state,
                                              const unsigned char *data,
                                              size_t size,
                                              const char *path,
                                              bool *needed_out) {
    MiniElfView elf;
    MiniElfSection symtab;
    MiniElfSection strtab;
    size_t symtab_index = SIZE_MAX;
    size_t i;
    bool ok = false;

    *needed_out = false;
    if (!minielf_open(&elf, data, size) ||
        elf.elf_class != ELFCLASS64 ||
        elf.data_encoding != ELFDATA2LSB ||
        elf.type != ET_REL ||
        elf.machine != EM_RISCV) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-archive-object:%s\n",
                path);
        goto done;
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(&elf, i, &section)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-archive-object:%s\n",
                    path);
            goto done;
        }
        if (section.type == SHT_SYMTAB) {
            symtab = section;
            symtab_index = i;
            break;
        }
    }
    if (symtab_index == SIZE_MAX) {
        /*
         * A regular archive may contain ET_REL members with no symbol table.
         * Such a member cannot satisfy a named unresolved global, so lazy
         * archive selection must simply leave it unselected.
         */
        ok = true;
        goto done;
    }
    if (symtab.link >= elf.section_count ||
        symtab.entry_size != sizeof(Elf64_Sym) ||
        !minielf_section(&elf, symtab.link, &strtab) ||
        strtab.type != SHT_STRTAB) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-archive-object-symtab:%s\n",
                path);
        goto done;
    }

    {
        size_t count = minielf_symbol_count(&elf, &symtab);

        if (count == 0U && symtab.size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-archive-object-symtab:%s\n",
                    path);
            goto done;
        }
        for (i = 1U; i < count; ++i) {
            MiniElfSymbol symbol;
            const char *name;
            unsigned bind;

            if (!minielf_symbol(&elf, symtab_index, i, &symbol)) {
                fprintf(state->diagnostics,
                        "minic-ld: truncated-archive-object-symbol:%s\n",
                        path);
                goto done;
            }
            bind = minielf_symbol_bind(symbol.info);
            if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
                symbol.section_index == SHN_UNDEF || symbol.name == 0U) {
                continue;
            }
            if (!minielf_string(&elf, symtab.link, symbol.name, &name)) {
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

static bool read_regular_archive(const char *archive_path,
                                 MiniLdArchive *archive,
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
            if (!archive_member_append(archive,
                                       name,
                                       NULL,
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

/* Archive owns regular-member bytes and thin-member paths.  The extraction
 * policy never needs to know how a member's bytes were obtained. */
typedef struct MiniLdArchiveMemberView {
    const unsigned char *bytes;
    size_t size;
    unsigned char *owned_bytes;
} MiniLdArchiveMemberView;

static bool archive_open_kind(MiniLdArchive *archive,
                              const char *path,
                              int kind,
                              FILE *diagnostics) {
    bool ok;

    if (kind != MINILD_ARCHIVE_THIN && kind != MINILD_ARCHIVE_REGULAR) {
        fprintf(diagnostics, "minic-ld: expected-archive:%s\n", path);
        return false;
    }
    archive->kind = (MiniLdArchiveKind)kind;
    if (archive->kind == MINILD_ARCHIVE_THIN) {
        ok = read_thin_archive_members(path, archive, diagnostics);
    } else {
        ok = read_regular_archive(path, archive, diagnostics);
    }
    if (!ok) {
        archive_destroy(archive);
    }
    return ok;
}

static bool archive_open(MiniLdArchive *archive,
                         const char *path,
                         FILE *diagnostics) {
    int kind = archive_file_kind(path, diagnostics);

    return kind >= 0 && archive_open_kind(archive, path, kind, diagnostics);
}

static const char *archive_member_path(const MiniLdArchiveMember *member) {
    return member->path == NULL ? member->name : member->path;
}

static bool archive_member_view(MiniLdState *state,
                                const MiniLdArchive *archive,
                                const MiniLdArchiveMember *member,
                                MiniLdArchiveMemberView *view) {
    memset(view, 0, sizeof(*view));
    if (archive->kind == MINILD_ARCHIVE_REGULAR) {
        view->bytes = archive->data + member->data_offset;
        view->size = member->data_size;
        return true;
    }
    if (!read_file(member->path, &view->owned_bytes, &view->size,
                   state->diagnostics)) {
        return false;
    }
    view->bytes = view->owned_bytes;
    return true;
}

static bool archive_member_process(MiniLdState *state,
                                   const MiniLdArchive *archive,
                                   const MiniLdArchiveMember *member,
                                   const MiniLdArchiveMemberView *view) {
    const char *path = archive_member_path(member);
    MiniElfView elf;

    /* Preserve the existing distinction: only thin whole-archive members
     * were allowed to dispatch ET_DYN through process_input(). */
    if (archive->kind == MINILD_ARCHIVE_THIN &&
        minielf_open(&elf, view->bytes, view->size) && elf.type == ET_DYN) {
        if (!state->allow_dso_inputs) {
            fprintf(state->diagnostics,
                    "minic-ld: shared-input-requires-dynamic-link:%s\n",
                    path);
            return false;
        }
        return process_dso_input_data(state, view->bytes, view->size, path);
    }
    return process_input_data(state, view->bytes, view->size, path);
}

/* One fixed-point member selector for both archive encodings.  An extracted
 * member may introduce a new undefined symbol satisfied by an earlier member.
 * selected persists across outer --start-group passes to avoid reingestion. */
static bool archive_extract(MiniLdState *state,
                            MiniLdArchive *archive,
                            bool whole,
                            size_t *extracted_out) {
    bool changed;
    size_t extracted = 0U;
    size_t i;

    do {
        changed = false;
        for (i = 0U; i < archive->count; ++i) {
            MiniLdArchiveMember *member = &archive->members[i];
            MiniLdArchiveMemberView view;
            bool needed = whole;
            bool ok;

            if (member->selected) {
                continue;
            }
            if (!archive_member_view(state, archive, member, &view)) {
                return false;
            }
            if (!whole) {
                ok = object_data_defines_needed_symbol(
                    state, view.bytes, view.size,
                    archive_member_path(member), &needed);
                if (!ok) {
                    free(view.owned_bytes);
                    return false;
                }
            }
            ok = !needed || archive_member_process(state, archive, member, &view);
            free(view.owned_bytes);
            if (!ok) {
                return false;
            }
            if (!needed) {
                continue;
            }
            member->selected = true;
            ++extracted;
            changed = true;
        }
    } while (!whole && changed);
    *extracted_out = extracted;
    return true;
}

static bool process_whole_archive(MiniLdState *state, const char *path) {
    MiniLdArchive archive = {0};
    size_t extracted;
    bool ok = archive_open(&archive, path, state->diagnostics) &&
              archive_extract(state, &archive, true, &extracted);

    archive_destroy(&archive);
    return ok;
}

static bool process_group_archive(MiniLdState *state, const char *path) {
    MiniLdArchive archive = {0};
    size_t extracted;
    bool ok = archive_open(&archive, path, state->diagnostics) &&
              archive_extract(state, &archive, false, &extracted);

    archive_destroy(&archive);
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
    typedef struct MiniLdGroupInput {
        MiniLdArchive archive;
        bool is_archive;
    } MiniLdGroupInput;
    MiniLdGroupInput *entries;
    bool first_pass = true;
    bool changed;
    size_t i;
    bool ok = false;

    entries = calloc(input_count == 0U ? 1U : input_count, sizeof(*entries));
    if (entries == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:group-inputs\n");
        return false;
    }
    for (i = 0U; i < input_count; ++i) {
        int kind = archive_file_kind(inputs[i].path, state->diagnostics);

        if (kind < 0) {
            goto done;
        }
        entries[i].is_archive = kind == MINILD_ARCHIVE_THIN ||
                                kind == MINILD_ARCHIVE_REGULAR;
        if (entries[i].is_archive &&
            !archive_open_kind(&entries[i].archive, inputs[i].path, kind,
                               state->diagnostics)) {
            goto done;
        }
    }

    do {
        changed = false;

        for (i = 0U; i < input_count; ++i) {
            if (entries[i].is_archive) {
                size_t extracted = 0U;

                if (!archive_extract(state, &entries[i].archive,
                                     false, &extracted)) {
                    goto done;
                }
                changed = changed || extracted != 0U;
            } else if (first_pass) {
                if (!process_input(state, inputs[i].path)) {
                    goto done;
                }
                /* A later plain object can create a need for an earlier
                 * archive even if no archive member was selected this pass. */
                changed = true;
            }
        }
        first_pass = false;
    } while (changed);

    ok = true;

done:
    for (i = 0U; i < input_count; ++i) {
        archive_destroy(&entries[i].archive);
    }
    free(entries);
    return ok;
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

static bool write_output(MiniLdState *state, const char *path) {
    MiniElfWriteSection *sections = NULL;
    MiniElfWriteSymbol *symbols = NULL;
    MiniElfWriteRela *relocations = NULL;
    MiniElfRelocatableSpec spec;
    MiniElfWriteError write_error = MINIELF_WRITE_OK;
    unsigned char *image = NULL;
    size_t image_size = 0U;
    size_t i;
    FILE *file = NULL;
    bool ok = false;

    sections = calloc(state->work.count == 0U ? 1U : state->work.count,
                      sizeof(*sections));
    symbols = calloc(state->resolver.count == 0U ? 1U : state->resolver.count,
                     sizeof(*symbols));
    relocations = calloc(state->relocations.count == 0U ? 1U : state->relocations.count,
                         sizeof(*relocations));
    if (sections == NULL || symbols == NULL || relocations == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:output\n");
        goto done;
    }

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *input = &state->work.sections[i];
        MiniElfWriteSection *output = &sections[i];

        output->name = input->name;
        output->type = input->type;
        output->flags = input->flags;
        output->alignment = input->align == 0U ? 1U : input->align;
        output->entry_size = input->entsize;
        output->data = input->data;
        output->size = input->size;
        output->has_group = input->has_group;
        output->group_flags = input->group_flags;
        output->group_signature_symbol = input->group_signature_symbol;
        output->has_link_order_target = input->has_link_order_target;
        output->linked_target_section = input->link_order_target;
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *input = &state->resolver.entries[i];
        MiniElfWriteSymbol *output = &symbols[i];

        output->name = input->name;
        output->info = input->info;
        output->other = input->other;
        output->value = input->value;
        output->size = input->size;
        output->group_signature_section = input->group_signature_section;
        if (input->section == MINILD_SECTION_UNDEF) {
            output->section_index = SHN_UNDEF;
        } else if (input->section == MINILD_SECTION_ABS) {
            output->section_index = SHN_ABS;
        } else if (input->section == MINILD_SECTION_COMMON) {
            output->section_index = SHN_COMMON;
        } else if (input->section < 0 ||
                   (size_t)input->section >= state->work.count ||
                   (size_t)input->section + 1U > UINT16_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-output-symbol-section:%s\n",
                    input->name);
            goto done;
        } else {
            output->section_index =
                (uint16_t)((size_t)input->section + 1U);
        }
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *input = &state->relocations.entries[i];
        MiniElfWriteRela *output = &relocations[i];

        if (input->section >= state->work.count ||
            (input->symbol != SIZE_MAX &&
             input->symbol >= state->resolver.count)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-output-relocation\n");
            goto done;
        }
        output->target_section = input->section;
        output->offset = input->offset;
        output->symbol_index = input->symbol;
        output->type = input->type;
        output->addend = input->addend;
    }

    memset(&spec, 0, sizeof(spec));
    spec.elf_class = ELFCLASS64;
    spec.data_encoding = ELFDATA2LSB;
    spec.machine = EM_RISCV;
    spec.flags = state->elf_flags;
    spec.sections = sections;
    spec.section_count = state->work.count;
    spec.symbols = symbols;
    spec.symbol_count = state->resolver.count;
    spec.relocations = relocations;
    spec.relocation_count = state->relocations.count;
    spec.emit_section_symbols = false;

    if (!minielf_build_relocatable(&spec,
                                   &image,
                                   &image_size,
                                   &write_error)) {
        fprintf(state->diagnostics,
                "minic-ld: cannot-build-output:%s\n",
                minielf_write_error_string(write_error));
        goto done;
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
    if (!ok) {
        (void)remove(path);
    }
    free(image);
    free(relocations);
    free(symbols);
    free(sections);
    return ok;
}

/* Output-phase implementation order is intentional: layout/relocation
 * primitives, output-section projection, final symbols/section tables,
 * common program-header contracts, then distinct static and shared ELF
 * writers. These remain one C translation
 * unit; moving an owner here does not introduce an extra state copy. */
#include "linker_layout.inc"

#include "linker_output_plan.inc"

#include "linker_final_symbols.inc"

#include "linker_program_plan.inc"

#include "linker_static_output.inc"

#include "linker_shared_output.inc"

/*
 * CPU-time profile for costly whole-Linux static links. Disabled unless
 * MINILD_PROFILE_PHASES=1; diagnostics go to stderr only, never an ELF output
 * or Kbuild's stdout. No changes to the linker data flow or order.
 */
static void minild_perf_phase(const char *phase,
                             clock_t *last,
                             const MiniLdState *state,
                             FILE *diagnostics) {
    clock_t now = clock();
    const char *enabled = getenv("MINILD_PROFILE_PHASES");
    if (enabled != NULL && strcmp(enabled, "1") == 0 &&
        *last != (clock_t)-1 && now != (clock_t)-1) {
        double milliseconds =
            1000.0 * (double)(now - *last) / (double)CLOCKS_PER_SEC;
        fprintf(diagnostics,
                "MINILD_PHASE=%s cpu_ms=%.3f objects=%zu sections=%zu symbols=%zu relocs=%zu\n",
                phase, milliseconds, state->processed_object_count,
                state->work.count, state->resolver.count, state->relocations.count);
    }
    *last = now;
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
    state.relocatable_output = true;

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
