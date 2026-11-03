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

typedef struct MiniLdPcrelSlot {
    size_t section_plus_one;
    uint64_t offset;
    int64_t delta;
} MiniLdPcrelSlot;

typedef struct MiniLdScriptBytePatch {
    size_t command;
    uint64_t address;
    unsigned char value;
} MiniLdScriptBytePatch;

typedef struct MiniLdStaticLayout {
    uint64_t *section_vaddr;
    uint64_t *section_laddr;
    size_t *section_file_offset;
    bool *section_discarded;
    /* Script-command identity survives layout: final ELF output sections
     * own working fragments, not the other way around. SIZE_MAX = none. */
    size_t *section_owner;
    unsigned char *script_fill;
    bool *script_have_fill;
    size_t script_command_count;
    uint64_t *script_output_start;
    uint64_t *script_output_end;
    size_t *script_output_file_base;
    MiniLdScriptBytePatch *script_bytes;
    size_t script_byte_count;
    size_t script_byte_capacity;
    size_t rx_file_offset;
    uint64_t rx_vaddr;
    size_t rx_file_size;
    size_t rw_file_offset;
    uint64_t rw_vaddr;
    size_t rw_file_size;
    size_t rw_mem_size;
    bool have_rx;
    bool have_rw;
    bool scripted;
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

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
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
        /* An input-backed .bss may leave an empty GC merger placeholder.
         * COMMON allocation turns that holder into a real synthetic output
         * section again, independently of the original input fragments. */
        state->work.sections[bss_index].gc_discarded = false;
        alignment = symbol->value == 0U ? 1U : symbol->value;
        if (!align_up_size(state->work.sections[bss_index].size,
                           alignment,
                           &aligned) ||
            !section_append_zero(&state->work.sections[bss_index],
                                 aligned - state->work.sections[bss_index].size) ||
            !section_append_zero(&state->work.sections[bss_index],
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
    got->symbol_count = state->resolver.count;
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

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
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
            /* As with COMMON, a generated GOT can reuse an emptied source
             * merger. It is live synthetic output, never a GC tombstone. */
            state->work.sections[got->section].gc_discarded = false;
            got->present = true;
        }

        section = &state->work.sections[got->section];
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
    MiniLdSection *section = &state->work.sections[section_index];
    MiniLdAlignSite *sites = NULL;
    MiniLdRelaxEvent *events = NULL;
    unsigned char *output = NULL;
    size_t site_count = 0U;
    size_t event_count = 0U;
    size_t output_size = 0U;
    size_t cursor = 0U;
    size_t i;
    bool ok = false;

    size_t relocation_begin;
    size_t relocation_end;

    if (section->type == SHT_NOBITS || section->size == 0U) {
        return true;
    }
    if (!relocations_build_section_index(&state->relocations,
                                         state->work.count,
                                         state->diagnostics)) {
        return false;
    }
    /* Synthetic script-only sections appended later have no relocations. */
    if (section_index >= state->relocations.index_sections) {
        return true;
    }
    relocation_begin = state->relocations.section_starts[section_index];
    relocation_end = state->relocations.section_starts[section_index + 1U];
    for (i = relocation_begin; i < relocation_end; ++i) {
        if (state->relocations.entries[state->relocations.section_refs[i]].type == R_RISCV_ALIGN) {
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
    for (i = relocation_begin; i < relocation_end; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[state->relocations.section_refs[i]];
        if (reloc->type != R_RISCV_ALIGN) {
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

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];

        if (symbol->section == (int)section_index &&
            symbol->value <= SIZE_MAX) {
            symbol->value = (uint64_t)static_map_relaxed_offset(
                (size_t)symbol->value,
                events,
                event_count);
        }
    }

    for (i = relocation_begin; i < relocation_end; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[state->relocations.section_refs[i]];

        if (reloc->offset <= SIZE_MAX) {
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

static bool static_build_layout_at(MiniLdState *state,
                                   MiniLdStaticLayout *layout,
                                   uint64_t base_vaddr) {
    const size_t page = 4096U;
    const size_t header_page = 4096U;
    size_t rx_cursor = 0U;
    size_t rw_file_cursor = 0U;
    size_t rw_mem_cursor = 0U;
    size_t i;

    memset(layout, 0, sizeof(*layout));
    layout->section_vaddr =
        calloc(state->work.count == 0U ? 1U : state->work.count,
               sizeof(*layout->section_vaddr));
    layout->section_laddr =
        calloc(state->work.count == 0U ? 1U : state->work.count,
               sizeof(*layout->section_laddr));
    layout->section_file_offset =
        malloc((state->work.count == 0U ? 1U : state->work.count) *
               sizeof(*layout->section_file_offset));
    layout->section_discarded =
        calloc(state->work.count == 0U ? 1U : state->work.count,
               sizeof(*layout->section_discarded));
    if (layout->section_vaddr == NULL || layout->section_laddr == NULL ||
        layout->section_file_offset == NULL ||
        layout->section_discarded == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-layout\n");
        return false;
    }
    for (i = 0U; i < state->work.count; ++i) {
        layout->section_file_offset[i] = SIZE_MAX;
    }

    layout->rx_file_offset = header_page;
    layout->rx_vaddr = base_vaddr;

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
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
        layout->section_laddr[i] = layout->section_vaddr[i];
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

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
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
        layout->section_laddr[i] = layout->section_vaddr[i];
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

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
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
        layout->section_laddr[i] = layout->section_vaddr[i];
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

static bool static_build_layout(MiniLdState *state,
                                MiniLdStaticLayout *layout) {
    return static_build_layout_at(state, layout, UINT64_C(0x10000));
}

static bool shared_build_layout(MiniLdState *state,
                                MiniLdStaticLayout *layout) {
    /*
     * ET_DYN load bias is defined relative to virtual address zero.
     * Keep the ELF/program headers at vaddr 0 and place the first
     * allocatable section one page later so e_phoff is addressable as
     * base + e_phoff during the dynamic linker's stage-1 bootstrap.
     */
    return static_build_layout_at(state, layout, UINT64_C(0x1000));
}


typedef struct MiniLdScriptLayoutState {
    MiniLdState *state;
    MiniLdStaticLayout *layout;
    const MiniLdScript *script;
    bool *section_placed;
    size_t *section_owner;
    uint64_t *output_vaddr;
    bool *output_defined;
    uint64_t dot;
    uint64_t load_bias;
    bool have_load_bias;
    uint64_t headers_size;
    bool have_headers_size;
} MiniLdScriptLayoutState;

static bool static_align_up_u64(uint64_t value,
                                uint64_t alignment,
                                uint64_t *out) {
    uint64_t mask;

    if (alignment <= 1U) {
        *out = value;
        return true;
    }
    if ((alignment & (alignment - 1U)) != 0U) {
        return false;
    }
    mask = alignment - 1U;
    if (value > UINT64_MAX - mask) {
        return false;
    }
    *out = (value + mask) & ~mask;
    return true;
}

static bool script_layout_symbol_value(void *context,
                                       const char *name,
                                       uint64_t *value_out) {
    MiniLdScriptLayoutState *script_state = context;
    size_t index = symbol_resolver_lookup(&script_state->state->resolver, name);
    MiniLdResolvedSymbol *symbol;

    if (index == SIZE_MAX) {
        return false;
    }
    symbol = &script_state->state->resolver.entries[index];
    if (symbol->section == MINILD_SECTION_ABS) {
        *value_out = symbol->value;
        return true;
    }
    if (symbol->section < 0 ||
        (size_t)symbol->section >= script_state->state->work.count ||
        !script_state->section_placed[symbol->section] ||
        script_state->layout->section_discarded[symbol->section]) {
        return false;
    }
    *value_out = script_state->layout->section_vaddr[symbol->section] +
                 symbol->value;
    return true;
}

static bool script_layout_section_value(void *context,
                                        const char *name,
                                        uint64_t *value_out) {
    MiniLdScriptLayoutState *script_state = context;
    size_t i;

    for (i = 0U; i < script_state->script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script_state->script->commands[i];

        if (command->kind == MINILD_SCRIPT_OUTPUT_SECTION &&
            script_state->output_defined[i] &&
            strcmp(command->value.section.name, name) == 0) {
            *value_out = script_state->output_vaddr[i];
            return true;
        }
    }
    return false;
}

static bool script_layout_evaluate(MiniLdScriptLayoutState *script_state,
                                   MiniLdScriptExprId expression,
                                   uint64_t *value_out) {
    MiniLdScriptEvalContext context;

    memset(&context, 0, sizeof(context));
    context.dot = script_state->dot;
    context.headers_size = script_state->headers_size;
    context.have_headers_size = script_state->have_headers_size;
    context.resolve_symbol = script_layout_symbol_value;
    context.resolve_section = script_layout_section_value;
    context.user = script_state;
    return minild_script_evaluate(script_state->script,
                                  expression,
                                  &context,
                                  value_out,
                                  script_state->state->diagnostics);
}

static bool script_layout_define_symbol(MiniLdScriptLayoutState *script_state,
                                        const char *name,
                                        uint64_t value) {
    size_t index = symbol_resolver_lookup(&script_state->state->resolver, name);

    if (index != SIZE_MAX) {
        uint64_t current;

        if (script_layout_symbol_value(script_state, name, &current) &&
            current == value) {
            return true;
        }
    }
    return symbol_resolver_assign_absolute(&script_state->state->resolver,
                                            script_state->state->diagnostics,
                                            name, value);
}

static bool script_layout_place_section(MiniLdScriptLayoutState *script_state,
                                        size_t section_index,
                                        size_t command_index) {
    MiniLdSection *section = &script_state->state->work.sections[section_index];
    uint64_t aligned;

    if (!static_align_up_u64(script_state->dot,
                             section->align == 0U ? 1U : section->align,
                             &aligned)) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: linker-script-section-alignment-overflow:%s\n",
                section->name);
        return false;
    }
    script_state->dot = aligned;
    script_state->layout->section_vaddr[section_index] = script_state->dot;
    script_state->section_placed[section_index] = true;
    script_state->section_owner[section_index] = command_index;
    if (script_state->script->commands[command_index].kind ==
            MINILD_SCRIPT_OUTPUT_SECTION) {
        section->output_name =
            script_state->script->commands[command_index].value.section.name;
    }

    if ((section->flags & SHF_ALLOC) != 0U &&
        !static_relax_align_section(script_state->state,
                                    section_index,
                                    script_state->dot)) {
        return false;
    }
    if ((uint64_t)section->size > UINT64_MAX - script_state->dot) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: linker-script-section-size-overflow:%s\n",
                section->name);
        return false;
    }
    script_state->dot += (uint64_t)section->size;
    return true;
}

/*
 * GNU SORT_BY_NAME must be stable for equal input section names.
 * The previous insertion sort took O(n*n) strcmp calls for large Linux
 * .initcall and .text linker-script patterns. Sort by (name, input index)
 * once instead, preserving the exact old tie order.
 */
typedef struct MiniLdNamedMatch {
    size_t index;
    const char *name;
} MiniLdNamedMatch;

static int minild_compare_named_match(const void *lhs, const void *rhs) {
    const MiniLdNamedMatch *a = lhs;
    const MiniLdNamedMatch *b = rhs;
    int c = strcmp(a->name, b->name);
    if (c != 0) {
        return c;
    }
    if (a->index != b->index) {
        return a->index < b->index ? -1 : 1;
    }
    return 0;
}

static const char *script_section_source_name(const MiniLdSection *section) {
    return section->input_name != NULL ? section->input_name : section->name;
}

static bool script_layout_place_pattern(MiniLdScriptLayoutState *script_state,
                                        const MiniLdScriptPattern *pattern,
                                        size_t command_index,
                                        bool discard) {
    size_t *matches;
    size_t count = 0U;
    size_t i;

    if (pattern->common) {
        return true;
    }

    matches = malloc((script_state->state->work.count == 0U
                          ? 1U
                          : script_state->state->work.count) *
                     sizeof(*matches));
    if (matches == NULL) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: out-of-memory:linker-script-pattern\n");
        return false;
    }

    for (i = 0U; i < script_state->state->work.count; ++i) {
        if (script_state->section_placed[i] ||
            script_state->state->work.sections[i].gc_discarded ||
            script_state->layout->section_discarded[i]) {
            continue;
        }
        if (minild_script_pattern_matches_input(
                pattern,
                script_section_source_name(&script_state->state->work.sections[i]),
                script_state->state->work.sections[i].input_path)) {
            matches[count++] = i;
        }
    }

    if (pattern->sort && count > 1U) {
        MiniLdNamedMatch *sorted;
        if (count > SIZE_MAX / sizeof(*sorted)) {
            free(matches);
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-pattern-sort-overflow\n");
            return false;
        }
        sorted = malloc(count * sizeof(*sorted));
        if (sorted == NULL) {
            free(matches);
            fprintf(script_state->state->diagnostics,
                    "minic-ld: out-of-memory:linker-script-pattern-sort\n");
            return false;
        }
        for (i = 0U; i < count; ++i) {
            sorted[i].index = matches[i];
            sorted[i].name =
                script_section_source_name(
                    &script_state->state->work.sections[matches[i]]);
        }
        qsort(sorted, count, sizeof(*sorted), minild_compare_named_match);
        for (i = 0U; i < count; ++i) {
            matches[i] = sorted[i].index;
        }
        free(sorted);
    }

    for (i = 0U; i < count; ++i) {
        size_t section_index = matches[i];

        if (discard) {
            script_state->layout->section_discarded[section_index] = true;
            script_state->section_placed[section_index] = true;
            continue;
        }
        if (!script_layout_place_section(script_state,
                                         section_index,
                                         command_index)) {
            free(matches);
            return false;
        }
    }

    free(matches);
    return true;
}

static bool script_layout_bind_output_symbols(
    MiniLdScriptLayoutState *script_state,
    const MiniLdScriptOutputSection *output,
    size_t command_index) {
    size_t anchor = SIZE_MAX;
    uint64_t anchor_vaddr;
    size_t i;

    for (i = 0U; i < script_state->state->work.count; ++i) {
        MiniLdSection *section = &script_state->state->work.sections[i];

        if (script_state->section_owner[i] == command_index &&
            !script_state->layout->section_discarded[i] &&
            (section->flags & SHF_ALLOC) != 0U &&
            (anchor == SIZE_MAX ||
             script_state->layout->section_vaddr[i] <
                 script_state->layout->section_vaddr[anchor])) {
            anchor = i;
        }
    }
    if (anchor == SIZE_MAX) {
        return true;
    }
    anchor_vaddr = script_state->layout->section_vaddr[anchor];

    for (i = 0U; i < output->item_count; ++i) {
        const MiniLdScriptSectionItem *item = &output->items[i];
        MiniLdResolvedSymbol *symbol;
        size_t symbol_index;

        if (item->kind != MINILD_SCRIPT_SECTION_DEFINE_SYMBOL) {
            continue;
        }
        if (item->value.symbol.expression >=
                script_state->script->expression_count ||
            script_state->script->expressions[
                item->value.symbol.expression].kind ==
                MINILD_SCRIPT_EXPR_ABSOLUTE) {
            continue;
        }
        symbol_index = symbol_resolver_lookup(&script_state->state->resolver,
                                          item->value.symbol.name);
        if (symbol_index == SIZE_MAX) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-missing-defined-symbol:%s\n",
                    item->value.symbol.name);
            return false;
        }
        symbol = &script_state->state->resolver.entries[symbol_index];
        if (symbol->section != MINILD_SECTION_ABS ||
            symbol->value < anchor_vaddr) {
            continue;
        }
        symbol->section = (int)anchor;
        symbol->value -= anchor_vaddr;
    }
    return true;
}

static bool script_layout_process_output(MiniLdScriptLayoutState *script_state,
                                         const MiniLdScriptOutputSection *output,
                                         size_t command_index) {
    uint64_t output_start;
    uint64_t lma_start;
    size_t byte_start = script_state->layout->script_byte_count;
    size_t i;

    if (output->fill != MINILD_SCRIPT_EXPR_NONE) {
        uint64_t fill;

        if (!script_layout_evaluate(script_state, output->fill, &fill)) {
            return false;
        }
        if (fill > UINT8_MAX) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: unsupported-script-fill-pattern:%s\n",
                    output->name);
            return false;
        }
        script_state->layout->script_fill[command_index] =
            (unsigned char)fill;
        script_state->layout->script_have_fill[command_index] = true;
    }

    if (output->discard) {
        for (i = 0U; i < output->item_count; ++i) {
            const MiniLdScriptSectionItem *item = &output->items[i];

            if (item->kind == MINILD_SCRIPT_SECTION_PATTERN &&
                !script_layout_place_pattern(script_state,
                                             &item->value.pattern,
                                             command_index,
                                             true)) {
                return false;
            }
        }
        return true;
    }

    if (output->address != MINILD_SCRIPT_EXPR_NONE) {
        if (!script_layout_evaluate(script_state,
                                    output->address,
                                    &script_state->dot)) {
            return false;
        }
    }
    if (output->align != MINILD_SCRIPT_EXPR_NONE) {
        uint64_t alignment;

        if (!script_layout_evaluate(script_state,
                                    output->align,
                                    &alignment) ||
            !static_align_up_u64(script_state->dot,
                                 alignment == 0U ? 1U : alignment,
                                 &script_state->dot)) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-output-alignment:%s\n",
                    output->name);
            return false;
        }
    }

    /*
     * GNU ld aligns an output section to the strictest participating
     * input-section alignment BEFORE evaluating leading symbol
     * assignments. Without this, __start___param may precede the first
     * aligned __param input by two bytes, causing parse_args() to read
     * a shifted pointer and page-fault during Linux boot.
     *
     * Respect an explicit output VMA; no implicit VMA alignment may
     * override the address selected by the linker script.
     */
    if (output->address == MINILD_SCRIPT_EXPR_NONE) {
        uint64_t input_alignment = 1U;
        size_t item_index;

        for (item_index = 0U; item_index < output->item_count;
             ++item_index) {
            const MiniLdScriptSectionItem *item = &output->items[item_index];
            size_t section_index;

            if (item->kind != MINILD_SCRIPT_SECTION_PATTERN ||
                item->value.pattern.common) {
                continue;
            }
            for (section_index = 0U;
                 section_index < script_state->state->work.count;
                 ++section_index) {
                const MiniLdSection *section =
                    &script_state->state->work.sections[section_index];
                uint64_t align = section->align == 0U ? 1U : section->align;

                if (script_state->section_placed[section_index] ||
                    section->gc_discarded ||
                    script_state->layout->section_discarded[section_index] ||
                    (section->flags & SHF_ALLOC) == 0U ||
                    !minild_script_pattern_matches_input(
                        &item->value.pattern,
                        script_section_source_name(section),
                        section->input_path)) {
                    continue;
                }
                if (align > input_alignment) {
                    input_alignment = align;
                }
            }
        }
        if (!static_align_up_u64(script_state->dot, input_alignment,
                                 &script_state->dot)) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-input-alignment:%s\n",
                    output->name);
            return false;
        }
    }

    output_start = script_state->dot;
    script_state->output_vaddr[command_index] = output_start;
    script_state->output_defined[command_index] = true;
    script_state->layout->script_output_start[command_index] = output_start;

    for (i = 0U; i < output->item_count; ++i) {
        const MiniLdScriptSectionItem *item = &output->items[i];
        uint64_t value;

        switch (item->kind) {
        case MINILD_SCRIPT_SECTION_PATTERN:
            if (!script_layout_place_pattern(script_state,
                                             &item->value.pattern,
                                             command_index,
                                             false)) {
                return false;
            }
            break;
        case MINILD_SCRIPT_SECTION_DEFINE_SYMBOL:
            if (!script_layout_evaluate(script_state,
                                        item->value.symbol.expression,
                                        &value) ||
                !script_layout_define_symbol(script_state,
                                             item->value.symbol.name,
                                             value)) {
                return false;
            }
            break;
        case MINILD_SCRIPT_SECTION_SET_DOT:
            if (!script_layout_evaluate(script_state,
                                        item->value.expression,
                                        &value)) {
                return false;
            }
            if (value < script_state->dot) {
                fprintf(script_state->state->diagnostics,
                        "minic-ld: linker-script-dot-moved-backward:%s\n",
                        output->name);
                return false;
            }
            script_state->dot = value;
            break;
        case MINILD_SCRIPT_SECTION_BYTE:
            if (!script_layout_evaluate(script_state,
                                        item->value.expression,
                                        &value)) {
                return false;
            }
            if (script_state->dot == UINT64_MAX) {
                fprintf(script_state->state->diagnostics,
                        "minic-ld: linker-script-BYTE-overflow:%s\n",
                        output->name);
                return false;
            }
            {
                MiniLdStaticLayout *layout = script_state->layout;
                if (layout->script_byte_count == layout->script_byte_capacity) {
                    size_t capacity = layout->script_byte_capacity == 0U
                                          ? 16U : layout->script_byte_capacity * 2U;
                    MiniLdScriptBytePatch *patches;
                    if (capacity < layout->script_byte_capacity ||
                        capacity > SIZE_MAX / sizeof(*patches)) {
                        fprintf(script_state->state->diagnostics,
                                "minic-ld: script-BYTE-count-overflow\n");
                        return false;
                    }
                    patches = realloc(layout->script_bytes,
                                      capacity * sizeof(*patches));
                    if (patches == NULL) {
                        fprintf(script_state->state->diagnostics,
                                "minic-ld: out-of-memory:script-BYTE\n");
                        return false;
                    }
                    layout->script_bytes = patches;
                    layout->script_byte_capacity = capacity;
                }
                layout->script_bytes[layout->script_byte_count].command =
                    command_index;
                layout->script_bytes[layout->script_byte_count].address =
                    script_state->dot;
                layout->script_bytes[layout->script_byte_count].value =
                    (unsigned char)value;
                ++layout->script_byte_count;
            }
            ++script_state->dot;
            break;
        case MINILD_SCRIPT_SECTION_CONSTRUCTORS:
            break;
        }
    }

    script_state->layout->script_output_end[command_index] =
        script_state->dot;

    /* A pure BYTE output needs a working section to anchor its layout.
     * Mixed input/BYTE outputs instead keep their real work fragments and
     * materialize byte members at their output-relative file addresses. */
    if (script_state->layout->script_byte_count != byte_start) {
        bool has_input = false;

        for (i = 0U; i < script_state->state->work.count; ++i) {
            if (script_state->section_owner[i] == command_index &&
                !script_state->layout->section_discarded[i]) {
                has_input = true;
                break;
            }
        }
        if (!has_input) {
            MiniLdState *state = script_state->state;
            MiniLdSection *section;
            size_t synthetic_index;
            size_t length;

            if (script_state->dot < output_start ||
                script_state->dot - output_start > SIZE_MAX ||
                find_section_index(state, output->name,
                                   hash_text(output->name)) != SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-generated-section:%s\n",
                        output->name);
                return false;
            }
            length = (size_t)(script_state->dot - output_start);
            if (!find_or_add_section(state, output->name,
                                     SHT_PROGBITS, SHF_ALLOC, 1U, 0U,
                                     &synthetic_index)) {
                return false;
            }
            section = &state->work.sections[synthetic_index];
            if (!section_append_zero(section, length)) {
                fprintf(state->diagnostics,
                        "minic-ld: script-generated-section-overflow:%s\n",
                        output->name);
                return false;
            }
            for (i = byte_start;
                 i < script_state->layout->script_byte_count; ++i) {
                const MiniLdScriptBytePatch *patch =
                    &script_state->layout->script_bytes[i];
                uint64_t off = patch->address - output_start;

                if (off >= length) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-script-BYTE-address:%s\n",
                            output->name);
                    return false;
                }
                section->data[(size_t)off] = patch->value;
            }
            script_state->layout->section_vaddr[synthetic_index] =
                output_start;
            script_state->section_owner[synthetic_index] = command_index;
            script_state->section_placed[synthetic_index] = true;
        }
    }

    if (output->at != MINILD_SCRIPT_EXPR_NONE) {
        if (!script_layout_evaluate(script_state,
                                    output->at,
                                    &lma_start)) {
            return false;
        }
        script_state->load_bias = output_start - lma_start;
        script_state->have_load_bias = true;
    } else if (script_state->have_load_bias) {
        lma_start = output_start - script_state->load_bias;
    } else {
        lma_start = output_start;
    }

    for (i = 0U; i < script_state->state->work.count; ++i) {
        if (script_state->section_owner[i] == command_index) {
            script_state->layout->section_laddr[i] =
                lma_start +
                (script_state->layout->section_vaddr[i] - output_start);
        }
    }
    return script_layout_bind_output_symbols(script_state,
                                             output,
                                             command_index);
}

/*
 * The old layout scanned all sections to find the next lowest LMA on EVERY
 * iteration (quadratic in alloc section count). Linux emits many section
 * fragments. Sorting stable keys once preserves the original first-minimum
 * selection exactly while reducing this operation to O(S log S).
 */
typedef struct MiniLdFileOrder {
    uint64_t laddr;
    uint64_t vaddr;
    size_t index;
} MiniLdFileOrder;

static int minild_compare_file_order(const void *lhs, const void *rhs) {
    const MiniLdFileOrder *a = lhs;
    const MiniLdFileOrder *b = rhs;
    if (a->laddr != b->laddr) {
        return a->laddr < b->laddr ? -1 : 1;
    }
    if (a->vaddr != b->vaddr) {
        return a->vaddr < b->vaddr ? -1 : 1;
    }
    if (a->index != b->index) {
        return a->index < b->index ? -1 : 1;
    }
    return 0;
}

static bool static_build_script_layout(MiniLdState *state,
                                       const MiniLdScript *script,
                                       MiniLdStaticLayout *layout,
                                       size_t shared_headers_size) {
    MiniLdScriptLayoutState script_state;
    MiniLdFileOrder *file_order = NULL;
    bool *command_deferred = NULL;
    size_t i;
    size_t alloc_count = 0U;
    size_t *owner_file_base = NULL;
    uint64_t *owner_vaddr_base = NULL;
    uint64_t *owner_max_align = NULL;
    size_t *owner_remaining = NULL;
    size_t storage_count;
    size_t file_cursor;
    size_t header_size;
    bool ok = false;

    memset(layout, 0, sizeof(*layout));
    memset(&script_state, 0, sizeof(script_state));
    /* Reserve one additional generated section per script command. */
    if (script->command_count > SIZE_MAX - state->work.count) {
        fprintf(state->diagnostics,
                "minic-ld: script-section-count-overflow\n");
        return false;
    }
    storage_count = state->work.count + script->command_count;
    if (storage_count == 0U) {
        storage_count = 1U;
    }

    layout->section_vaddr =
        calloc(storage_count,
               sizeof(*layout->section_vaddr));
    layout->section_laddr =
        calloc(storage_count,
               sizeof(*layout->section_laddr));
    layout->section_file_offset =
        malloc((storage_count) *
               sizeof(*layout->section_file_offset));
    layout->section_discarded =
        calloc(storage_count,
               sizeof(*layout->section_discarded));
    script_state.section_placed =
        calloc(storage_count,
               sizeof(*script_state.section_placed));
    script_state.section_owner =
        malloc((storage_count) *
               sizeof(*script_state.section_owner));
    layout->script_fill = calloc(script->command_count == 0U ? 1U :
                                 script->command_count, sizeof(*layout->script_fill));
    layout->script_have_fill = calloc(script->command_count == 0U ? 1U :
                                      script->command_count,
                                      sizeof(*layout->script_have_fill));
    layout->script_output_start = calloc(script->command_count == 0U ? 1U :
                                         script->command_count,
                                         sizeof(*layout->script_output_start));
    layout->script_output_end = calloc(script->command_count == 0U ? 1U :
                                       script->command_count,
                                       sizeof(*layout->script_output_end));
    layout->script_output_file_base = malloc((script->command_count == 0U ? 1U :
                                               script->command_count) *
                                              sizeof(*layout->script_output_file_base));
    script_state.output_vaddr =
        calloc(script->command_count == 0U ? 1U : script->command_count,
               sizeof(*script_state.output_vaddr));
    script_state.output_defined =
        calloc(script->command_count == 0U ? 1U : script->command_count,
               sizeof(*script_state.output_defined));
    command_deferred =
        calloc(script->command_count == 0U ? 1U : script->command_count,
               sizeof(*command_deferred));
    owner_file_base = malloc((script->command_count == 0U ? 1U :
                              script->command_count) * sizeof(*owner_file_base));
    owner_vaddr_base = calloc(script->command_count == 0U ? 1U :
                              script->command_count, sizeof(*owner_vaddr_base));
    owner_max_align = calloc(script->command_count == 0U ? 1U :
                              script->command_count, sizeof(*owner_max_align));
    owner_remaining = calloc(script->command_count == 0U ? 1U :
                              script->command_count, sizeof(*owner_remaining));

    if (layout->section_vaddr == NULL || layout->section_laddr == NULL ||
        layout->section_file_offset == NULL ||
        layout->section_discarded == NULL ||
        script_state.section_placed == NULL ||
        script_state.section_owner == NULL ||
        layout->script_fill == NULL || layout->script_have_fill == NULL ||
        layout->script_output_start == NULL ||
        layout->script_output_end == NULL ||
        layout->script_output_file_base == NULL ||
        script_state.output_vaddr == NULL ||
        script_state.output_defined == NULL ||
        command_deferred == NULL || owner_file_base == NULL ||
        owner_vaddr_base == NULL || owner_max_align == NULL ||
        owner_remaining == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:linker-script-layout\n");
        goto done;
    }

    for (i = 0U; i < state->work.count; ++i) {
        layout->section_file_offset[i] = SIZE_MAX;
        script_state.section_owner[i] = SIZE_MAX;
    }
    for (i = 0U; i < script->command_count; ++i) {
        owner_file_base[i] = SIZE_MAX;
        layout->script_output_file_base[i] = SIZE_MAX;
    }
    script_state.state = state;
    script_state.layout = layout;
    script_state.script = script;
    script_state.have_headers_size = shared_headers_size != 0U;
    script_state.headers_size = shared_headers_size;

    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        uint64_t value;

        switch (command->kind) {
        case MINILD_SCRIPT_SET_DOT:
            if (!script_layout_evaluate(&script_state,
                                        command->value.expression,
                                        &value)) {
                goto done;
            }
            if (value < script_state.dot) {
                fprintf(state->diagnostics,
                        "minic-ld: linker-script-top-level-dot-moved-backward\n");
                goto done;
            }
            script_state.dot = value;
            break;
        case MINILD_SCRIPT_DEFINE_SYMBOL:
            if (command->value.symbol.provide) {
                size_t sym = symbol_resolver_lookup(&state->resolver,
                                                command->value.symbol.name);
                if (sym == SIZE_MAX ||
                    state->resolver.entries[sym].section != MINILD_SECTION_UNDEF) {
                    break;
                }
            }
            if (script->expressions[command->value.symbol.expression].kind ==
                    MINILD_SCRIPT_EXPR_SYMBOL &&
                !script_layout_symbol_value(
                    &script_state,
                    script->expressions[command->value.symbol.expression].name,
                    &value)) {
                command_deferred[i] = true;
                break;
            }
            if (!script_layout_evaluate(&script_state,
                                        command->value.symbol.expression,
                                        &value) ||
                !script_layout_define_symbol(&script_state,
                                             command->value.symbol.name,
                                             value)) {
                goto done;
            }
            break;
        case MINILD_SCRIPT_OUTPUT_SECTION:
            if (!script_layout_process_output(&script_state,
                                             &command->value.section,
                                             i)) {
                goto done;
            }
            break;
        }
    }

    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        uint64_t value;

        if (command->kind != MINILD_SCRIPT_DEFINE_SYMBOL ||
            !command_deferred[i]) {
            continue;
        }
        if (command->value.symbol.provide) {
            size_t sym = symbol_resolver_lookup(&state->resolver,
                                            command->value.symbol.name);
            if (sym == SIZE_MAX ||
                state->resolver.entries[sym].section != MINILD_SECTION_UNDEF) {
                continue;
            }
        }
        if (!script_layout_evaluate(&script_state,
                                    command->value.symbol.expression,
                                    &value) ||
            !script_layout_define_symbol(&script_state,
                                         command->value.symbol.name,
                                         value)) {
            goto done;
        }
    }

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];

        if (script_state.section_placed[i] ||
            layout->section_discarded[i]) {
            continue;
        }
        if ((section->flags & SHF_ALLOC) != 0U && section->size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-unplaced-alloc-section:%s:size=%zu\n",
                    section->name,
                    section->size);
            goto done;
        }
        layout->section_discarded[i] = true;
        script_state.section_placed[i] = true;
    }

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        size_t owner = script_state.section_owner[i];

        if (layout->section_discarded[i] ||
            (section->flags & SHF_ALLOC) == 0U ||
            section->size == 0U) {
            continue;
        }
        if (owner != SIZE_MAX && owner < script->command_count &&
            section->align > owner_max_align[owner]) {
            owner_max_align[owner] = section->align;
        }
        if (owner != SIZE_MAX && owner < script->command_count) {
            ++owner_remaining[owner];
        }
        ++alloc_count;
        if ((section->flags & SHF_WRITE) != 0U) {
            layout->have_rw = true;
        } else {
            layout->have_rx = true;
        }
    }

    if (alloc_count > (SIZE_MAX - sizeof(Elf64_Ehdr)) / sizeof(Elf64_Phdr) ||
        !align_up_size(sizeof(Elf64_Ehdr) +
                           alloc_count * sizeof(Elf64_Phdr),
                       4096U,
                       &header_size)) {
        fprintf(state->diagnostics,
                "minic-ld: linker-script-header-overflow\n");
        goto done;
    }
    file_cursor = header_size;
    if (alloc_count > SIZE_MAX / sizeof(*file_order)) {
        fprintf(state->diagnostics,
                "minic-ld: linker-script-file-order-overflow\n");
        goto done;
    }
    file_order = malloc((alloc_count == 0U ? 1U : alloc_count) *
                        sizeof(*file_order));
    if (file_order == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:linker-script-file-order\n");
        goto done;
    }
    {
        size_t count = 0U;
        for (i = 0U; i < state->work.count; ++i) {
            const MiniLdSection *section = &state->work.sections[i];
            if (layout->section_discarded[i] ||
                (section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
                continue;
            }
            file_order[count].laddr = layout->section_laddr[i];
            file_order[count].vaddr = layout->section_vaddr[i];
            file_order[count].index = i;
            ++count;
        }
        if (count != alloc_count) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-order-count-mismatch\n");
            goto done;
        }
    }
    qsort(file_order, alloc_count, sizeof(*file_order),
          minild_compare_file_order);
    for (i = 0U; i < alloc_count; ++i) {
        size_t best = file_order[i].index;
        const MiniLdSection *section = &state->work.sections[best];
        size_t owner = script_state.section_owner[best];
        size_t aligned;

        if (!align_up_size(file_cursor,
                           section->align == 0U ? 1U : section->align,
                           &aligned)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-layout-overflow:%s\n",
                    section->name);
            goto done;
        }
        if (owner != SIZE_MAX && owner < script->command_count &&
            owner_file_base[owner] != SIZE_MAX) {
            uint64_t delta;
            size_t expected;

            if (layout->section_vaddr[best] < owner_vaddr_base[owner] ||
                layout->section_vaddr[best] - owner_vaddr_base[owner] >
                    SIZE_MAX - owner_file_base[owner]) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-output-order:%s\n",
                        section->name);
                goto done;
            }
            delta = layout->section_vaddr[best] - owner_vaddr_base[owner];
            expected = owner_file_base[owner] + (size_t)delta;
            /* An ELF output section has one sh_offset and one sh_addr.
             * Every member therefore needs the SAME file/VMA displacement.
             * Preserve script dot holes as real zero-filled file gaps. */
            if (expected < aligned ||
                expected % (section->align == 0U ? 1U : section->align) != 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-output-file-order:%s\n",
                        section->name);
                goto done;
            }
            file_cursor = expected;
        } else {
            file_cursor = aligned;
            if (owner != SIZE_MAX && owner < script->command_count) {
                /* Later members may be more strictly aligned than the
                 * first. Match the owner's maximum alignment modulo its
                 * VMA now, so later VMA gaps also have correct file gaps. */
                size_t alignment = (size_t)(owner_max_align[owner] == 0U
                                               ? 1U : owner_max_align[owner]);
                uint64_t leading = layout->section_vaddr[best] -
                                   layout->script_output_start[owner];
                size_t wanted = (size_t)(layout->section_vaddr[best] % alignment);
                size_t have;
                size_t minimum;
                if (leading > SIZE_MAX - file_cursor) {
                    fprintf(state->diagnostics,
                            "minic-ld: script-output-prefix-overflow:%s\n",
                            section->name);
                    goto done;
                }
                minimum = file_cursor + (size_t)leading;
                if (minimum > aligned) {
                    file_cursor = minimum;
                }
                have = file_cursor % alignment;
                size_t padding = wanted >= have ? wanted - have
                                                : alignment - (have - wanted);

                if (!add_size(file_cursor, padding, &file_cursor)) {
                    fprintf(state->diagnostics,
                            "minic-ld: script-output-alignment-overflow:%s\n",
                            section->name);
                    goto done;
                }
                owner_file_base[owner] = file_cursor - (size_t)leading;
                owner_vaddr_base[owner] = layout->script_output_start[owner];
                layout->script_output_file_base[owner] = owner_file_base[owner];
            }
        }
        layout->section_file_offset[best] = file_cursor;
        if (section->type != SHT_NOBITS &&
            !add_size(file_cursor, section->size, &file_cursor)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-layout-overflow:%s\n",
                    section->name);
            goto done;
        }
        if (owner != SIZE_MAX && owner < script->command_count &&
            --owner_remaining[owner] == 0U && section->type != SHT_NOBITS) {
            uint64_t span = layout->script_output_end[owner] -
                            layout->script_output_start[owner];
            size_t end;
            if (span > SIZE_MAX - owner_file_base[owner]) {
                fprintf(state->diagnostics,
                        "minic-ld: script-output-tail-overflow:%s\n",
                        section->name);
                goto done;
            }
            end = owner_file_base[owner] + (size_t)span;
            if (end > file_cursor) {
                file_cursor = end;
            }
        }
    }

    layout->scripted = true;
    layout->script_command_count = script->command_count;
    layout->section_owner = script_state.section_owner;
    script_state.section_owner = NULL;
    ok = true;

done:
    free(file_order);
    free(owner_file_base);
    free(owner_vaddr_base);
    free(owner_max_align);
    free(owner_remaining);
    free(command_deferred);
    free(script_state.output_defined);
    free(script_state.output_vaddr);
    free(script_state.section_owner);
    free(script_state.section_placed);
    if (!ok) {
        free(layout->script_bytes);
        free(layout->script_output_file_base);
        free(layout->script_output_end);
        free(layout->script_output_start);
        free(layout->script_have_fill);
        free(layout->script_fill);
        free(layout->section_discarded);
        free(layout->section_file_offset);
        free(layout->section_laddr);
        free(layout->section_vaddr);
        memset(layout, 0, sizeof(*layout));
    }
    return ok;
}

/* Shared -T uses the proven static output-section and PROVIDE evaluator.
 * In Linux's four-header single-PT_LOAD layout, the PT_LOAD starts at
 * file offset 0 and VMA 0, and every alloc section must lie after the
 * ELF and PHDR tables with file offset identical to its VMA.
 */
static bool shared_build_vdso_script_layout(MiniLdState *state,
                                            const MiniLdScript *script,
                                            MiniLdStaticLayout *layout) {
    size_t headers_size;
    size_t i;
    size_t end;

    if (script->phdr_count != 4U ||
        strcmp(script->phdrs[0].name, "text") != 0 ||
        strcmp(script->phdrs[1].name, "dynamic") != 0 ||
        strcmp(script->phdrs[2].name, "note") != 0 ||
        strcmp(script->phdrs[3].name, "eh_frame_hdr") != 0 ||
        script->phdrs[1].flags != 4U ||
        !script->phdrs[1].have_flags ||
        script->phdrs[2].flags != 4U ||
        !script->phdrs[2].have_flags ||
        strcmp(script->phdrs[0].type_name, "PT_LOAD") != 0 ||
        !script->phdrs[0].filehdr || !script->phdrs[0].phdrs ||
        !script->phdrs[0].have_flags || script->phdrs[0].flags != 5U ||
        strcmp(script->phdrs[1].type_name, "PT_DYNAMIC") != 0 ||
        strcmp(script->phdrs[2].type_name, "PT_NOTE") != 0 ||
        strcmp(script->phdrs[3].type_name, "PT_GNU_EH_FRAME") != 0) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-shared-script-PHDRS-layout\n");
        return false;
    }
    headers_size = sizeof(Elf64_Ehdr) +
                   script->phdr_count * sizeof(Elf64_Phdr);
    if (!static_build_script_layout(state, script, layout, headers_size)) {
        return false;
    }
    end = headers_size;
    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        uint64_t address;
        if (layout->section_discarded[i] ||
            (section->flags & SHF_ALLOC) == 0U ||
            section->size == 0U) {
            continue;
        }
        address = layout->section_vaddr[i];
        if (address < headers_size ||
            address > SIZE_MAX ||
            section->size > SIZE_MAX - (size_t)address) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-vdso-script-file-placement:%s\n",
                    section->name);
            return false;
        }
        layout->section_file_offset[i] = (size_t)address;
        if (section->type != SHT_NOBITS &&
            end < (size_t)address + section->size) {
            end = (size_t)address + section->size;
        }
    }
    /* vDSO PT_LOAD includes FILEHDR/PHDRS: its file and virtual bases are
     * both zero. The script allocator's temporary packed file bases are
     * replaced above, so the output owner bases must follow suit. */
    for (i = 0U; i < layout->script_command_count; ++i) {
        if (layout->script_output_file_base[i] != SIZE_MAX) {
            if (layout->script_output_start[i] > SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: vdso-output-base-overflow\n");
                return false;
            }
            layout->script_output_file_base[i] =
                (size_t)layout->script_output_start[i];
        }
    }
    layout->have_rx = true;
    layout->have_rw = false;
    layout->rx_file_offset = 0U;
    layout->rx_vaddr = 0U;
    layout->rx_file_size = end;
    return true;
}

static bool static_section_has_prefix(const char *name, const char *prefix) {
    size_t length = strlen(prefix);

    return strcmp(name, prefix) == 0 ||
           (strncmp(name, prefix, length) == 0 && name[length] == '.');
}

static bool static_define_absolute_if_undefined(MiniLdState *state,
                                                const char *name,
                                                uint64_t value) {
    return symbol_resolver_provide_absolute(&state->resolver,
                                             state->diagnostics,
                                             name, value);
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

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        uint64_t section_start;
        uint64_t section_end;

        if ((section->flags & SHF_ALLOC) == 0U ||
            (layout->section_discarded != NULL &&
             layout->section_discarded[i]) ||
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
         * are represented by a zero-length range at the end of the load image.
         */
        if (layout->scripted) {
            start = 0U;
            for (i = 0U; i < state->work.count; ++i) {
                MiniLdSection *section = &state->work.sections[i];
                uint64_t candidate;

                if ((section->flags & SHF_ALLOC) == 0U ||
                    layout->section_discarded[i]) {
                    continue;
                }
                candidate = layout->section_vaddr[i] +
                            (uint64_t)section->size;
                if (candidate > start) {
                    start = candidate;
                }
            }
        } else {
            start = layout->rw_vaddr + (uint64_t)layout->rw_mem_size;
        }
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
    if (got->section >= state->work.count) {
        fprintf(state->diagnostics, "minic-ld: invalid-static-got-section\n");
        return false;
    }
    section = &state->work.sections[got->section];

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

/*
 * GNU's unscripted RISC-V ELF64 default linker script PROVIDEs data/BSS
 * range markers and __global_pointer$. These are NOT Linux-specific names.
 * Derive the values from MiniLD's own placed alloc sections; never reuse
 * GNU's absolute addresses, since relaxation changes our section layout.
 * Explicit-script vmlinux links retain the old behavior unchanged.
 */
static bool static_synthesize_gnu_default_boundaries(
    MiniLdState *state,
    const MiniLdStaticLayout *layout) {
    uint64_t end = layout->have_rw
                       ? layout->rw_vaddr + (uint64_t)layout->rw_mem_size
                       : layout->rx_vaddr + (uint64_t)layout->rx_file_size;
    uint64_t data_begin = UINT64_MAX;
    uint64_t sdata_begin = UINT64_MAX;
    uint64_t bss_begin = UINT64_MAX;
    uint64_t bss_end = 0U;
    uint64_t global_pointer;
    size_t i;

    if (layout->scripted) {
        return true;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        uint64_t address;
        uint64_t section_end;

        if ((section->flags & SHF_ALLOC) == 0U ||
            (layout->section_discarded != NULL &&
             layout->section_discarded[i])) {
            continue;
        }
        address = layout->section_vaddr[i];
        if ((uint64_t)section->size > UINT64_MAX - address) {
            fprintf(state->diagnostics,
                    "minic-ld: default-symbol-address-overflow\n");
            return false;
        }
        section_end = address + (uint64_t)section->size;
        if ((section->flags & SHF_WRITE) != 0U &&
            address < data_begin) {
            data_begin = address;
        }
        if ((static_section_has_prefix(section->name, ".sdata") ||
             static_section_has_prefix(section->name, ".srodata")) &&
            address < sdata_begin) {
            sdata_begin = address;
        }
        if (section->type == SHT_NOBITS &&
            (section->flags & SHF_WRITE) != 0U) {
            if (address < bss_begin) {
                bss_begin = address;
            }
            if (section_end > bss_end) {
                bss_end = section_end;
            }
        }
        if (section_end > end) {
            end = section_end;
        }
    }
    if (data_begin == UINT64_MAX) {
        data_begin = end;
    }
    if (sdata_begin == UINT64_MAX) {
        sdata_begin = data_begin;
    }
    if (bss_begin == UINT64_MAX) {
        bss_begin = end;
        bss_end = end;
    }
    if (sdata_begin > UINT64_MAX - UINT64_C(0x800) ||
        data_begin > UINT64_MAX - UINT64_C(0x800)) {
        fprintf(state->diagnostics,
                "minic-ld: default-global-pointer-overflow\n");
        return false;
    }
    /* GNU RISC-V default linker script:
     * MIN(__SDATA_BEGIN__ + 0x800,
     *     MAX(__DATA_BEGIN__ + 0x800, __BSS_END__ - 0x800)).
     * Saturate subtraction when the image is unusually small. */
    global_pointer = data_begin + UINT64_C(0x800);
    if (bss_end >= UINT64_C(0x800) &&
        bss_end - UINT64_C(0x800) > global_pointer) {
        global_pointer = bss_end - UINT64_C(0x800);
    }
    if (sdata_begin + UINT64_C(0x800) < global_pointer) {
        global_pointer = sdata_begin + UINT64_C(0x800);
    }
    return static_define_absolute_if_undefined(
               state, "__DATA_BEGIN__", data_begin) &&
           static_define_absolute_if_undefined(
               state, "__SDATA_BEGIN__", sdata_begin) &&
           static_define_absolute_if_undefined(
               state, "__BSS_END__", bss_end) &&
           static_define_absolute_if_undefined(
               state, "__bss_start", bss_begin) &&
           static_define_absolute_if_undefined(
               state, "__global_pointer$", global_pointer) &&
           static_define_absolute_if_undefined(
               state, "_edata", bss_begin) &&
           static_define_absolute_if_undefined(
               state, "_end", end);
}

static bool static_need_array_bound_pair(
    const MiniLdState *state,
    const MiniLdStaticLayout *layout,
    const char *section_prefix,
    const char *start_name,
    const char *end_name) {
    size_t i;

    /* Preserve the already certified scripted Linux path exactly. */
    if (layout->scripted ||
        symbol_resolver_lookup(&state->resolver, start_name) != SIZE_MAX ||
        symbol_resolver_lookup(&state->resolver, end_name) != SIZE_MAX) {
        return true;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        if ((section->flags & SHF_ALLOC) != 0U &&
            (layout->section_discarded == NULL ||
             !layout->section_discarded[i]) &&
            static_section_has_prefix(section->name, section_prefix)) {
            return true;
        }
    }
    /* GNU does not export unused, absent array markers in a plain ET_EXEC. */
    return false;
}

static bool static_synthesize_runtime_boundaries(
    MiniLdState *state,
    const MiniLdStaticLayout *layout) {
    return static_synthesize_gnu_default_boundaries(state, layout) &&
           (!static_need_array_bound_pair(state, layout, ".preinit_array",
                                          "__preinit_array_start",
                                          "__preinit_array_end") ||
            static_synthesize_array_bound_pair(state, layout,
                                                ".preinit_array",
                                                "__preinit_array_start",
                                                "__preinit_array_end")) &&
           (!static_need_array_bound_pair(state, layout, ".init_array",
                                          "__init_array_start",
                                          "__init_array_end") ||
            static_synthesize_array_bound_pair(state, layout,
                                                ".init_array",
                                                "__init_array_start",
                                                "__init_array_end")) &&
           (!static_need_array_bound_pair(state, layout, ".fini_array",
                                          "__fini_array_start",
                                          "__fini_array_end") ||
            static_synthesize_array_bound_pair(state, layout,
                                                ".fini_array",
                                                "__fini_array_start",
                                                "__fini_array_end"));
}

static void static_layout_destroy(MiniLdStaticLayout *layout) {
    free(layout->section_vaddr);
    free(layout->section_laddr);
    free(layout->section_file_offset);
    free(layout->section_discarded);
    free(layout->section_owner);
    free(layout->script_fill);
    free(layout->script_have_fill);
    free(layout->script_bytes);
    free(layout->script_output_file_base);
    free(layout->script_output_end);
    free(layout->script_output_start);
    layout->section_vaddr = NULL;
    layout->section_laddr = NULL;
    layout->section_file_offset = NULL;
    layout->section_discarded = NULL;
    layout->section_owner = NULL;
    layout->script_fill = NULL;
    layout->script_have_fill = NULL;
    layout->script_bytes = NULL;
    layout->script_output_file_base = NULL;
    layout->script_output_end = NULL;
    layout->script_output_start = NULL;
    layout->script_command_count = 0U;
}

static bool static_resolve_symbol(MiniLdState *state,
                                  const MiniLdStaticLayout *layout,
                                  size_t symbol_index,
                                  uint64_t *value_out) {
    MiniLdResolvedSymbol *symbol;

    if (symbol_index >= state->resolver.count) {
        fprintf(state->diagnostics, "minic-ld: relocation-symbol-out-of-range\n");
        return false;
    }
    symbol = &state->resolver.entries[symbol_index];
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
        (size_t)symbol->section >= state->work.count ||
        (state->work.sections[symbol->section].flags & SHF_ALLOC) == 0U ||
        (layout->section_discarded != NULL &&
         layout->section_discarded[symbol->section])) {
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
    size_t capacity = pcrel_capacity_for(state->relocations.count);
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

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section = &state->work.sections[reloc->section];
        uint64_t symbol_value = 0U;

        if (layout->scripted &&
            ((section->flags & SHF_ALLOC) == 0U ||
             layout->section_discarded[reloc->section])) {
            continue;
        }
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
                     reloc->symbol < state->resolver.count)
                        ? state->resolver.entries[reloc->symbol].name
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

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        MiniLdResolvedSymbol *label;
        int64_t delta;

        if (reloc->type != R_RISCV_PCREL_LO12_I &&
            reloc->type != R_RISCV_PCREL_LO12_S) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-pcrel-lo-symbol\n");
            free(pcrel);
            return false;
        }
        label = &state->resolver.entries[reloc->symbol];
        if (label->section < 0 ||
            (size_t)label->section >= state->work.count ||
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
        section = &state->work.sections[reloc->section];
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
    size_t index = symbol_resolver_lookup(&state->resolver, entry_symbol);

    if (index == SIZE_MAX) {
        fprintf(state->diagnostics,
                "minic-ld: undefined-entry:%s\n",
                entry_symbol);
        return false;
    }
    return static_resolve_symbol(state, layout, index, entry_out);
}

/* Coalesce adjacent scripted PT_LOADs without moving sections or relocations.
 * A vmlinux linker script can place >10,000 individual input sections. */
typedef struct MiniLdStaticLoadSegment {
    uint64_t file_offset, vaddr, laddr, file_size, mem_size;
    uint32_t flags;
} MiniLdStaticLoadSegment;

static int static_load_segment_compare(const void *lhs, const void *rhs) {
    const MiniLdStaticLoadSegment *a = lhs, *b = rhs;

    if (a->laddr != b->laddr) {
        return a->laddr < b->laddr ? -1 : 1;
    }
    if (a->vaddr != b->vaddr) {
        return a->vaddr < b->vaddr ? -1 : 1;
    }
    if (a->file_offset != b->file_offset) {
        return a->file_offset < b->file_offset ? -1 : 1;
    }
    return 0;
}

/* A working section is the unit of relocation and relaxation, but a linker
 * script output section owns an ordered collection of those fragments. The
 * ELF section table must describe the latter. Keep this projection separate
 * from the working sections so symbol and LINK_ORDER references can share a
 * single, stable work-section -> final-shndx mapping. */
typedef struct MiniLdOutputSection {
    const char *name;
    size_t command;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    uint64_t entsize;
    uint64_t vaddr;
    uint64_t laddr;
    uint64_t size;
    size_t file_offset;
    size_t *members;
    size_t member_count;
    size_t member_capacity;
    unsigned char fill;
    bool have_fill;
    bool have_content;
} MiniLdOutputSection;

typedef struct MiniLdOutputPlan {
    MiniLdOutputSection *sections;
    size_t count;
    /* Zero means discarded, otherwise final ELF section header index. */
    size_t *work_shndx;
    /* Final physical offsets of surviving work fragments, including
     * non-ALLOC members that acquire file storage after LOAD layout. */
    size_t *work_file_offsets;
    size_t *command_shndx;
} MiniLdOutputPlan;

static void minild_output_plan_destroy(MiniLdOutputPlan *plan) {
    size_t i;

    for (i = 0U; i < plan->count; ++i) {
        free(plan->sections[i].members);
    }
    free(plan->sections);
    free(plan->work_shndx);
    free(plan->work_file_offsets);
    free(plan->command_shndx);
    memset(plan, 0, sizeof(*plan));
}

static size_t minild_output_shndx(const MiniLdOutputPlan *plan,
                                  const MiniLdState *state,
                                  size_t work) {
    return work < state->work.count ? plan->work_shndx[work] : 0U;
}

static int minild_output_section_compare(const void *left,
                                         const void *right) {
    const MiniLdOutputSection *a = left;
    const MiniLdOutputSection *b = right;
    if (a->command != b->command) {
        return a->command < b->command ? -1 : 1;
    }
    return a->members[0] < b->members[0] ? -1 :
           a->members[0] > b->members[0] ? 1 : 0;
}

static bool minild_output_plan_build(const MiniLdState *state,
                                     const MiniLdStaticLayout *layout,
                                     const size_t *file_offsets,
                                     MiniLdOutputPlan *plan) {
    size_t *owner_to_output = NULL;
    size_t i;
    bool ok = false;

    memset(plan, 0, sizeof(*plan));
    plan->sections = calloc(state->work.count == 0U ? 1U :
                            state->work.count, sizeof(*plan->sections));
    plan->work_shndx = calloc(state->work.count == 0U ? 1U :
                              state->work.count, sizeof(*plan->work_shndx));
    plan->work_file_offsets = calloc(state->work.count == 0U ? 1U :
                                     state->work.count,
                                     sizeof(*plan->work_file_offsets));
    if (plan->sections == NULL || plan->work_shndx == NULL ||
        plan->work_file_offsets == NULL) {
        goto done;
    }
    for (i = 0U; i < state->work.count; ++i) {
        plan->work_file_offsets[i] = file_offsets[i];
    }
    if (layout->scripted) {
        size_t count = layout->script_command_count;
        if (count > SIZE_MAX / sizeof(*owner_to_output)) {
            goto done;
        }
        owner_to_output = malloc((count == 0U ? 1U : count) *
                                 sizeof(*owner_to_output));
        plan->command_shndx = calloc(count == 0U ? 1U : count,
                                    sizeof(*plan->command_shndx));
        if (owner_to_output == NULL || plan->command_shndx == NULL) {
            goto done;
        }
        for (i = 0U; i < count; ++i) {
            owner_to_output[i] = SIZE_MAX;
        }
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *part = &state->work.sections[i];
        size_t command = layout->scripted && layout->section_owner != NULL
                             ? layout->section_owner[i] : SIZE_MAX;
        MiniLdOutputSection *output = NULL;

        if (part->gc_discarded ||
            (layout->scripted && layout->section_discarded[i])) {
            continue;
        }
        if (command != SIZE_MAX && command < layout->script_command_count &&
            owner_to_output[command] != SIZE_MAX) {
            output = &plan->sections[owner_to_output[command]];
        }
        if (output == NULL) {
            output = &plan->sections[plan->count++];
            output->name = part->output_name != NULL
                               ? part->output_name : part->name;
            output->command = command;
            output->file_offset = SIZE_MAX;
            if (command != SIZE_MAX && command < layout->script_command_count) {
                owner_to_output[command] = plan->count - 1U;
            }
            if (command != SIZE_MAX && layout->script_have_fill != NULL &&
                layout->script_have_fill[command]) {
                output->have_fill = true;
                output->fill = layout->script_fill[command];
            }
        }
        if (output->member_count == output->member_capacity) {
            size_t capacity = output->member_capacity == 0U
                                  ? 4U : output->member_capacity * 2U;
            size_t *members;
            if (capacity < output->member_capacity ||
                capacity > SIZE_MAX / sizeof(*members)) {
                goto done;
            }
            members = realloc(output->members, capacity * sizeof(*members));
            if (members == NULL) {
                goto done;
            }
            output->members = members;
            output->member_capacity = capacity;
        }
        output->members[output->member_count++] = i;
        if (part->align > output->align) {
            output->align = part->align;
        }
        if (part->size == 0U && output->have_content) {
            continue;
        }
        if (!output->have_content || (output->size == 0U && part->size != 0U)) {
            output->type = part->type;
            output->flags = part->flags;
            output->entsize = part->entsize;
            output->vaddr = layout->section_vaddr[i];
            output->laddr = layout->section_laddr[i];
            output->size = part->size;
            output->file_offset = file_offsets[i];
            output->have_content = true;
        } else if (part->size != 0U) {
            uint64_t low = output->vaddr;
            uint64_t high;
            uint64_t end;
            if (part->type != output->type || part->flags != output->flags ||
                part->entsize != output->entsize ||
                part->size > UINT64_MAX - layout->section_vaddr[i] ||
                output->size > UINT64_MAX - output->vaddr) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-output-type:%s\n",
                        output->name);
                goto done;
            }
            high = output->vaddr + output->size;
            end = layout->section_vaddr[i] + part->size;
            if (layout->section_vaddr[i] < low) {
                low = layout->section_vaddr[i];
                output->laddr = layout->section_laddr[i];
            }
            if (end > high) {
                high = end;
            }
            output->vaddr = low;
            output->size = high - low;
            if (part->type != SHT_NOBITS &&
                file_offsets[i] < output->file_offset) {
                output->file_offset = file_offsets[i];
            }
        }
    }
    if (layout->scripted) {
        qsort(plan->sections, plan->count, sizeof(*plan->sections),
              minild_output_section_compare);
    }
    for (i = 0U; i < plan->count; ++i) {
        MiniLdOutputSection *output = &plan->sections[i];
        size_t j;
        if (layout->scripted && output->command != SIZE_MAX &&
            output->command < layout->script_command_count) {
            size_t command = output->command;
            uint64_t start = layout->script_output_start[command];
            uint64_t end = layout->script_output_end[command];
            size_t file_base = layout->script_output_file_base[command];

            plan->command_shndx[command] = i + 1U;
            if (end < start || (file_base == SIZE_MAX &&
                                 output->size != 0U &&
                                 (output->flags & SHF_ALLOC) != 0U)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-script-output-span:%s\n",
                        output->name);
                goto done;
            }
            if ((output->flags & SHF_ALLOC) != 0U &&
                start <= output->vaddr &&
                end >= output->vaddr &&
                output->size <= end - output->vaddr) {
                uint64_t prefix = output->vaddr - start;
                if (output->laddr < prefix) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-script-output-lma:%s\n",
                            output->name);
                    goto done;
                }
                output->vaddr = start;
                output->laddr -= prefix;
                output->size = end - start;
                if (file_base != SIZE_MAX) {
                    output->file_offset = file_base;
                }
            }
        }
        for (j = 0U; j < output->member_count; ++j) {
            plan->work_shndx[output->members[j]] = i + 1U;
        }
    }
    for (i = 0U; i < plan->count; ++i) {
        const MiniLdOutputSection *output = &plan->sections[i];
        size_t j;
        size_t target_index = 0U;
        for (j = 0U; j < output->member_count; ++j) {
            size_t work = output->members[j];
            const MiniLdSection *part = &state->work.sections[work];
            if (part->has_link_order_target) {
                size_t target = part->link_order_target;
                if (target >= state->work.count ||
                    plan->work_shndx[target] == 0U ||
                    (target_index != 0U &&
                     target_index != plan->work_shndx[target])) {
                    fprintf(state->diagnostics,
                            "minic-ld: incompatible-output-link-order:%s\n",
                            output->name);
                    goto done;
                }
                target_index = plan->work_shndx[target];
            }
            if (!layout->scripted || part->size == 0U ||
                part->type == SHT_NOBITS ||
                (part->flags & SHF_ALLOC) == 0U) {
                continue;
            }
            /* A single shdr cannot describe several independent file/VMA
             * displacements; reject any inconsistent span rather than
             * producing an apparently valid but wrong section table. */
            if (layout->section_vaddr[work] < output->vaddr ||
                layout->section_laddr[work] < output->laddr ||
                file_offsets[work] < output->file_offset ||
                layout->section_vaddr[work] - output->vaddr !=
                    file_offsets[work] - output->file_offset ||
                layout->section_laddr[work] - output->laddr !=
                    layout->section_vaddr[work] - output->vaddr) {
                fprintf(state->diagnostics,
                        "minic-ld: incompatible-script-output-offset:%s\n",
                        output->name);
                goto done;
            }
        }
    }
    ok = true;

done:
    free(owner_to_output);
    if (!ok) {
        if (plan->sections == NULL || plan->work_shndx == NULL ||
            plan->work_file_offsets == NULL) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:output-section-plan\n");
        }
        minild_output_plan_destroy(plan);
    }
    return ok;
}

/* The layout phase fixes file offsets for SHF_ALLOC fragments only. Final
 * file-backed non-ALLOC sections belong to the output-section projection:
 * script members can share one output section, and a single sh_offset must
 * describe the same span as its sh_addr/sh_size. Keep these decisions out of
 * both ELF serializers. The plan owns both the working-fragment offsets
 * and each final output section's file offset. */
static bool minild_output_plan_place_nonalloc(
    const MiniLdState *state,
    const MiniLdStaticLayout *layout,
    MiniLdOutputPlan *plan,
    size_t first_offset,
    size_t *end_offset) {
    size_t *file_offsets = plan->work_file_offsets;
    size_t cursor = first_offset;
    size_t i;

    for (i = 0U; i < plan->count; ++i) {
        MiniLdOutputSection *output = &plan->sections[i];
        size_t base, end;
        size_t j;

        if ((output->flags & SHF_ALLOC) != 0U) {
            continue;
        }
        if (output->type == SHT_NOBITS || output->size == 0U) {
            output->file_offset = 0U;
            for (j = 0U; j < output->member_count; ++j) {
                file_offsets[output->members[j]] = 0U;
            }
            continue;
        }
        if (output->size > SIZE_MAX ||
            !align_up_size(cursor, output->align == 0U ? 1U :
                           (size_t)output->align, &base) ||
            !add_size(base, (size_t)output->size, &end)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-nonalloc-output-size:%s\n",
                    output->name);
            return false;
        }
        output->file_offset = base;
        for (j = 0U; j < output->member_count; ++j) {
            size_t work = output->members[j];
            const MiniLdSection *part = &state->work.sections[work];
            uint64_t delta;
            size_t offset;

            if (part->type == SHT_NOBITS || part->size == 0U) {
                file_offsets[work] = 0U;
                continue;
            }
            if (layout->section_vaddr[work] < output->vaddr ||
                (delta = layout->section_vaddr[work] - output->vaddr) >
                    SIZE_MAX - base ||
                !add_size(base + (size_t)delta, part->size, &offset) ||
                offset > end) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-nonalloc-member-span:%s\n",
                        output->name);
                return false;
            }
            file_offsets[work] = base + (size_t)delta;
        }
        cursor = end;
    }
    *end_offset = cursor;
    return true;
}

/* An output section's ELF identity and LINK_ORDER relationship have one
 * owner regardless of whether the image is ET_EXEC or ET_DYN. Dynamic
 * metadata may refine sh_link/sh_info after this common projection. */
static bool minild_output_plan_section_headers(
    const MiniLdState *state,
    const MiniLdOutputPlan *plan,
    const uint32_t *name_offsets,
    Elf64_Shdr *headers) {
    size_t i;

    for (i = 0U; i < plan->count; ++i) {
        const MiniLdOutputSection *section = &plan->sections[i];
        Elf64_Shdr *header = &headers[i + 1U];
        size_t j;

        if (section->type != SHT_NOBITS && section->size != 0U &&
            section->file_offset == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-output-file-offset:%s\n",
                    section->name);
            return false;
        }
        header->sh_name = name_offsets[i];
        header->sh_type = section->type;
        header->sh_flags = section->flags;
        header->sh_addr = (section->flags & SHF_ALLOC) != 0U
                              ? section->vaddr : 0U;
        header->sh_offset = section->type == SHT_NOBITS ||
                                    section->size == 0U
                                ? 0U : section->file_offset;
        header->sh_size = section->size;
        header->sh_addralign = section->align == 0U ? 1U : section->align;
        header->sh_entsize = section->entsize;
        for (j = 0U; j < section->member_count; ++j) {
            const MiniLdSection *part = &state->work.sections[section->members[j]];
            if (part->has_link_order_target) {
                header->sh_link = (Elf64_Word)
                    plan->work_shndx[part->link_order_target];
                break;
            }
        }
    }
    return true;
}

static bool minild_output_write_script_bytes(const MiniLdState *state,
                                              const MiniLdStaticLayout *layout,
                                              const MiniLdOutputPlan *plan,
                                              unsigned char *image,
                                              size_t image_size) {
    size_t i;

    for (i = 0U; i < layout->script_byte_count; ++i) {
        const MiniLdScriptBytePatch *patch = &layout->script_bytes[i];
        const MiniLdOutputSection *output;
        uint64_t delta;
        size_t offset;
        size_t shndx;

        if (patch->command >= layout->script_command_count ||
            plan->command_shndx == NULL ||
            (shndx = plan->command_shndx[patch->command]) == 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-script-BYTE-output-owner\n");
            return false;
        }
        output = &plan->sections[shndx - 1U];
        if (output->type == SHT_NOBITS ||
            patch->address < output->vaddr ||
            (delta = patch->address - output->vaddr) >= output->size ||
            output->file_offset == SIZE_MAX ||
            delta > SIZE_MAX - output->file_offset ||
            (offset = output->file_offset + (size_t)delta) >= image_size) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-script-BYTE-file-offset:%s\n",
                    output->name);
            return false;
        }
        image[offset] = patch->value;
    }
    return true;
}

/* Final emission follows the OutputPlan's single surviving-section map.
 * Writers do not independently decide which work fragments survive, which
 * script gaps receive fill, or where final bytes belong. */
static bool minild_output_plan_emit_sections(
    const MiniLdState *state,
    const MiniLdStaticLayout *layout,
    const MiniLdOutputPlan *plan,
    unsigned char *image,
    size_t image_size) {
    size_t i;

    for (i = 0U; i < plan->count; ++i) {
        const MiniLdOutputSection *output = &plan->sections[i];
        if (output->have_fill && output->fill != 0U &&
            output->type != SHT_NOBITS && output->size != 0U) {
            if (output->file_offset == SIZE_MAX ||
                output->size > SIZE_MAX ||
                !range_ok(output->file_offset, (size_t)output->size,
                          image_size)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-output-fill-range:%s\n",
                        output->name);
                return false;
            }
            memset(image + output->file_offset, output->fill,
                   (size_t)output->size);
        }
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *part = &state->work.sections[i];
        size_t offset = plan->work_file_offsets[i];

        if (plan->work_shndx[i] == 0U || part->type == SHT_NOBITS ||
            part->size == 0U) {
            continue;
        }
        if (offset == SIZE_MAX ||
            !range_ok(offset, part->size, image_size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-final-section-bytes:%s\n",
                    part->name);
            return false;
        }
        memcpy(image + offset, part->data, part->size);
    }
    return minild_output_write_script_bytes(state, layout, plan,
                                             image, image_size);
}

/* One final symbol-table owner for ET_EXEC and ET_DYN.  Input symbols and
 * their resolver indices are never reordered or annotated by the writer.
 * The final view owns the local-before-global order, SHN assignments,
 * relocated values and ELF string table; ET_REL keeps its source policy. */
typedef struct MiniLdFinalSymbols {
    MiniLdBuffer names;
    Elf64_Sym *entries;
    size_t local_count;
} MiniLdFinalSymbols;

static void minild_final_symbols_destroy(MiniLdFinalSymbols *final) {
    free(final->names.data);
    free(final->entries);
    memset(final, 0, sizeof(*final));
}

static bool minild_final_tls_base_address(const MiniLdState *state,
                                          const MiniLdStaticLayout *layout,
                                          const MiniLdOutputPlan *plan,
                                          uint64_t *base_out,
                                          bool *present_out) {
    uint64_t base = UINT64_MAX;
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        if ((section->flags & (SHF_ALLOC | SHF_TLS)) !=
                (SHF_ALLOC | SHF_TLS) ||
            plan->work_shndx[i] == 0U) {
            continue;
        }
        if (layout->section_vaddr[i] < base) {
            base = layout->section_vaddr[i];
        }
    }
    *present_out = base != UINT64_MAX;
    *base_out = base == UINT64_MAX ? 0U : base;
    return true;
}

static bool minild_final_symbols_build(const MiniLdState *state,
                                       const MiniLdStaticLayout *layout,
                                       const MiniLdOutputPlan *plan,
                                       MiniLdFinalSymbols *final) {
    uint64_t tls_base = 0U;
    bool have_tls = false;
    size_t next = 1U;
    size_t pass, i;

    if (state->resolver.count >= SIZE_MAX / sizeof(Elf64_Sym) - 1U ||
        !minild_final_tls_base_address(state, layout, plan,
                                       &tls_base, &have_tls)) {
        return false;
    }
    final->entries = calloc(state->resolver.count + 1U,
                            sizeof(*final->entries));
    if (final->entries == NULL || !buffer_append_zero(&final->names, 1U)) {
        return false;
    }
    for (pass = 0U; pass < 2U; ++pass) {
        for (i = 0U; i < state->resolver.count; ++i) {
            const MiniLdResolvedSymbol *source = &state->resolver.entries[i];
            bool local = ELF64_ST_BIND(source->info) == STB_LOCAL;
            Elf64_Sym *output;

            if (local != (pass == 0U)) {
                continue;
            }
            output = &final->entries[next++];
            if (source->name[0] != '\0' &&
                !buffer_append_string(&final->names, source->name,
                                      &output->st_name)) {
                return false;
            }
            output->st_info = source->info;
            output->st_other = source->other;
            output->st_size = source->size;
            if (source->section == MINILD_SECTION_UNDEF) {
                output->st_shndx = SHN_UNDEF;
            } else if (source->section == MINILD_SECTION_ABS) {
                output->st_shndx = SHN_ABS;
                output->st_value = source->value;
            } else if (source->section == MINILD_SECTION_COMMON) {
                output->st_shndx = SHN_COMMON;
                output->st_value = source->value;
            } else if (source->section >= 0 &&
                       (size_t)source->section < state->work.count) {
                size_t section = (size_t)source->section;
                if (plan->work_shndx[section] == 0U) {
                    output->st_shndx = SHN_UNDEF;
                } else {
                    output->st_shndx =
                        (Elf64_Half)plan->work_shndx[section];
                    if (ELF64_ST_TYPE(source->info) == STT_TLS) {
                        /* A final TLS symbol is relative to the TLS block,
                         * not the section VMA (ET_EXEC and ET_DYN alike). */
                        if (!have_tls ||
                            (state->work.sections[section].flags & SHF_TLS) == 0U ||
                            layout->section_vaddr[section] < tls_base ||
                            source->value > UINT64_MAX -
                                (layout->section_vaddr[section] - tls_base)) {
                            fprintf(state->diagnostics,
                                    "minic-ld: invalid-final-tls-symbol:%s\n",
                                    source->name);
                            return false;
                        }
                        output->st_value =
                            layout->section_vaddr[section] - tls_base +
                            source->value;
                    } else if ((state->work.sections[section].flags &
                                SHF_ALLOC) != 0U) {
                        output->st_value =
                            layout->section_vaddr[section] + source->value;
                    } else {
                        /* sh_addr is zero for non-ALLOC output sections.
                         * An input symbol must still include the fragment's
                         * displacement in its merged output section. */
                        const MiniLdOutputSection *owner =
                            &plan->sections[plan->work_shndx[section] - 1U];
                        uint64_t start = layout->section_vaddr[section];
                        if (start < owner->vaddr ||
                            source->value > UINT64_MAX -
                                (start - owner->vaddr)) {
                            fprintf(state->diagnostics,
                                    "minic-ld: invalid-nonalloc-symbol:%s\n",
                                    source->name);
                            return false;
                        }
                        output->st_value = start - owner->vaddr +
                                           source->value;
                    }
                }
            } else {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-final-symbol-section:%s\n",
                        source->name);
                return false;
            }
        }
        if (pass == 0U) {
            final->local_count = next;
        }
    }
    return next == state->resolver.count + 1U;
}

typedef struct MiniLdProgramPlan {
    Elf64_Phdr *entries;
    size_t count;
} MiniLdProgramPlan;

static void minild_program_plan_destroy(MiniLdProgramPlan *plan) {
    free(plan->entries);
    memset(plan, 0, sizeof(*plan));
}

static bool minild_program_plan_validate(const MiniLdState *state,
                                         const MiniLdProgramPlan *plan,
                                         size_t image_size) {
    size_t i;
    for (i = 0U; i < plan->count; ++i) {
        const Elf64_Phdr *ph = &plan->entries[i];
        uint64_t align = ph->p_align;
        if (ph->p_filesz > ph->p_memsz ||
            ph->p_offset > image_size ||
            ph->p_filesz > image_size - (size_t)ph->p_offset ||
            ph->p_vaddr > UINT64_MAX - ph->p_memsz ||
            (align > 1U &&
             (((align & (align - 1U)) != 0U) ||
              ph->p_offset % align != ph->p_vaddr % align))) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-final-program-range:%zu:type=%u\n",
                    i, ph->p_type);
            return false;
        }
    }
    return true;
}

/* The static segment policy is deliberately distinct from ET_DYN: default
 * executables leave the ELF header outside PT_LOAD, while scripted images
 * preserve LMA and merge only three-way-contiguous sections. */
static bool minild_static_program_plan_build(
    const MiniLdState *state,
    const MiniLdStaticLayout *layout,
    const MiniLdOutputPlan *output_plan,
    MiniLdProgramPlan *plan) {
    MiniLdStaticLoadSegment *loads = NULL;
    size_t count = 0U;
    size_t i;

    memset(plan, 0, sizeof(*plan));
    if (layout->scripted) {
        size_t load_count = 0U;
        loads = calloc(output_plan->count == 0U ? 1U : output_plan->count,
                       sizeof(*loads));
        if (loads == NULL) {
            goto oom;
        }
        for (i = 0U; i < output_plan->count; ++i) {
            const MiniLdOutputSection *section = &output_plan->sections[i];
            MiniLdStaticLoadSegment *load;

            if ((section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
                continue;
            }
            if (section->file_offset == SIZE_MAX ||
                section->size > UINT64_MAX - section->vaddr ||
                section->size > UINT64_MAX - section->laddr) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-script-load-range:%s\n",
                        section->name);
                goto failed;
            }
            load = &loads[load_count++];
            load->file_offset = section->file_offset;
            load->vaddr = section->vaddr;
            load->laddr = section->laddr;
            load->file_size = section->type == SHT_NOBITS ? 0U :
                              section->size;
            load->mem_size = section->size;
            load->flags = PF_R;
            if ((section->flags & SHF_WRITE) != 0U) {
                load->flags |= PF_W;
            }
            if ((section->flags & SHF_EXECINSTR) != 0U) {
                load->flags |= PF_X;
            }
        }
        qsort(loads, load_count, sizeof(*loads), static_load_segment_compare);
        for (i = 0U; i < load_count; ++i) {
            const MiniLdStaticLoadSegment *next = &loads[i];

            if (count != 0U) {
                MiniLdStaticLoadSegment *prev = &loads[count - 1U];
                if (prev->flags == next->flags &&
                    prev->file_size == prev->mem_size &&
                    prev->laddr <= UINT64_MAX - prev->mem_size &&
                    prev->vaddr <= UINT64_MAX - prev->mem_size &&
                    prev->file_offset <= UINT64_MAX - prev->file_size &&
                    prev->laddr + prev->mem_size == next->laddr &&
                    prev->vaddr + prev->mem_size == next->vaddr &&
                    prev->file_offset + prev->file_size == next->file_offset) {
                    prev->mem_size += next->mem_size;
                    prev->file_size += next->file_size;
                    continue;
                }
            }
            loads[count++] = *next;
        }
    } else {
        count = (layout->have_rx ? 1U : 0U) +
                (layout->have_rw ? 1U : 0U);
    }
    if (count > UINT16_MAX ||
        count > (SIZE_MAX - sizeof(Elf64_Ehdr)) / sizeof(Elf64_Phdr)) {
        fprintf(state->diagnostics, "minic-ld: too-many-static-segments\n");
        goto failed;
    }
    plan->entries = calloc(count == 0U ? 1U : count,
                           sizeof(*plan->entries));
    if (plan->entries == NULL) {
        goto oom;
    }
    plan->count = count;
    if (layout->scripted) {
        for (i = 0U; i < count; ++i) {
            const MiniLdStaticLoadSegment *load = &loads[i];
            Elf64_Phdr *ph = &plan->entries[i];
            ph->p_type = PT_LOAD;
            ph->p_flags = load->flags;
            ph->p_offset = load->file_offset;
            ph->p_vaddr = load->vaddr;
            ph->p_paddr = load->laddr;
            ph->p_filesz = load->file_size;
            ph->p_memsz = load->mem_size;
            ph->p_align = 1U;
        }
    } else {
        size_t index = 0U;
        if (layout->have_rx) {
            Elf64_Phdr *ph = &plan->entries[index++];
            ph->p_type = PT_LOAD;
            ph->p_flags = PF_R | PF_X;
            ph->p_offset = layout->rx_file_offset;
            ph->p_vaddr = ph->p_paddr = layout->rx_vaddr;
            ph->p_filesz = ph->p_memsz = layout->rx_file_size;
            ph->p_align = 4096U;
        }
        if (layout->have_rw) {
            Elf64_Phdr *ph = &plan->entries[index++];
            ph->p_type = PT_LOAD;
            ph->p_flags = PF_R | PF_W;
            ph->p_offset = layout->rw_file_offset;
            ph->p_vaddr = ph->p_paddr = layout->rw_vaddr;
            ph->p_filesz = layout->rw_file_size;
            ph->p_memsz = layout->rw_mem_size;
            ph->p_align = 4096U;
        }
    }
    free(loads);
    return true;

oom:
    fprintf(state->diagnostics, "minic-ld: out-of-memory:static-segments\n");
failed:
    free(loads);
    minild_program_plan_destroy(plan);
    return false;
}

static bool static_write_executable(MiniLdState *state,
                                    const MiniLdStaticLayout *layout,
                                    const char *path,
                                    uint64_t entry) {
    MiniLdOutputPlan output_plan = {0};
    MiniLdProgramPlan program_plan = {0};
    size_t load_image_size;
    size_t output_section_count;
    size_t symtab_index;
    size_t strtab_index;
    size_t shstrtab_index;
    size_t cursor;
    size_t section_header_offset;
    size_t total_size;
    uint32_t *section_name_offsets = NULL;
    MiniLdFinalSymbols final_symbols = {0};
    MiniLdBuffer shstrtab = {NULL, 0U, 0U};
    Elf64_Shdr *headers = NULL;
    unsigned char *image = NULL;
    Elf64_Ehdr header;
    size_t i;
    FILE *file = NULL;
    bool ok = false;

    if (layout->scripted &&
        !minild_output_plan_build(state, layout,
                                  layout->section_file_offset,
                                  &output_plan)) {
        goto done;
    }
    if (!minild_static_program_plan_build(state, layout, &output_plan,
                                          &program_plan)) {
        goto done;
    }
    load_image_size = sizeof(Elf64_Ehdr) +
                      program_plan.count * sizeof(Elf64_Phdr);

    if (layout->scripted) {
        for (i = 0U; i < output_plan.count; ++i) {
            const MiniLdOutputSection *section = &output_plan.sections[i];
            size_t end;

            if ((section->flags & SHF_ALLOC) == 0U ||
                section->type == SHT_NOBITS || section->size == 0U) {
                continue;
            }
            if (section->file_offset == SIZE_MAX ||
                section->size > SIZE_MAX ||
                !add_size(section->file_offset, (size_t)section->size, &end)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-script-section-file-offset:%s\n",
                        section->name);
                goto done;
            }
            if (end > load_image_size) {
                load_image_size = end;
            }
        }
    } else {
        if (layout->have_rx &&
            layout->rx_file_offset + layout->rx_file_size > load_image_size) {
            load_image_size = layout->rx_file_offset + layout->rx_file_size;
        }
        if (layout->have_rw &&
            layout->rw_file_offset + layout->rw_file_size > load_image_size) {
            load_image_size = layout->rw_file_offset + layout->rw_file_size;
        }
    }

    if (!layout->scripted &&
        !minild_output_plan_build(state, layout,
                                  layout->section_file_offset,
                                  &output_plan)) {
        goto done;
    }
    if (output_plan.count > UINT16_MAX - 4U) {
        fprintf(state->diagnostics,
                "minic-ld: too-many-static-output-sections\n");
        goto done;
    }
    output_section_count = output_plan.count + 3U;

    section_name_offsets =
        calloc(output_plan.count == 0U ? 1U : output_plan.count,
               sizeof(*section_name_offsets));
    headers = calloc(output_section_count + 1U, sizeof(*headers));
    if (section_name_offsets == NULL ||
        headers == NULL ||
        !buffer_append_zero(&shstrtab, 1U)) {
        goto oom;
    }

    if (!minild_output_plan_place_nonalloc(state, layout, &output_plan,
                                           load_image_size, &cursor)) {
        goto done;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        if (output_plan.work_shndx[i] != 0U &&
            (section->flags & SHF_ALLOC) != 0U &&
            section->type != SHT_NOBITS && section->size != 0U &&
            (output_plan.work_file_offsets[i] == SIZE_MAX ||
             !range_ok(output_plan.work_file_offsets[i], section->size,
                       load_image_size))) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-section-layout:%s\n",
                    section->name);
            goto done;
        }
    }
    for (i = 0U; i < output_plan.count; ++i) {
        if (!buffer_append_string(&shstrtab,
                                  output_plan.sections[i].name,
                                  &section_name_offsets[i])) {
            goto oom;
        }
    }

    if (!minild_final_symbols_build(state, layout, &output_plan,
                                    &final_symbols)) {
        goto oom;
    }

    symtab_index = 1U + output_plan.count;
    strtab_index = symtab_index + 1U;
    shstrtab_index = symtab_index + 2U;
    {
        uint32_t symtab_name;
        uint32_t strtab_name;
        uint32_t shstrtab_name;

        if (!buffer_append_string(&shstrtab,
                                  ".symtab",
                                  &symtab_name) ||
            !buffer_append_string(&shstrtab,
                                  ".strtab",
                                  &strtab_name) ||
            !buffer_append_string(&shstrtab,
                                  ".shstrtab",
                                  &shstrtab_name)) {
            goto oom;
        }
        headers[symtab_index].sh_name = symtab_name;
        headers[strtab_index].sh_name = strtab_name;
        headers[shstrtab_index].sh_name = shstrtab_name;
    }

    if (!minild_output_plan_section_headers(state, &output_plan,
                                             section_name_offsets, headers)) {
        goto done;
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
        (state->resolver.count + 1U) * sizeof(Elf64_Sym);
    headers[symtab_index].sh_link = (Elf64_Word)strtab_index;
    headers[symtab_index].sh_info =
        (Elf64_Word)final_symbols.local_count;
    headers[symtab_index].sh_addralign = 8U;
    headers[symtab_index].sh_entsize = sizeof(Elf64_Sym);
    if (!add_size(cursor,
                  headers[symtab_index].sh_size,
                  &cursor)) {
        goto oom;
    }

    headers[strtab_index].sh_type = SHT_STRTAB;
    headers[strtab_index].sh_offset = cursor;
    headers[strtab_index].sh_size = final_symbols.names.size;
    headers[strtab_index].sh_addralign = 1U;
    if (!add_size(cursor, final_symbols.names.size, &cursor)) {
        goto oom;
    }

    headers[shstrtab_index].sh_type = SHT_STRTAB;
    headers[shstrtab_index].sh_offset = cursor;
    headers[shstrtab_index].sh_size = shstrtab.size;
    headers[shstrtab_index].sh_addralign = 1U;
    if (!add_size(cursor, shstrtab.size, &cursor) ||
        !align_up_size(cursor, 8U, &section_header_offset) ||
        output_section_count + 1U >
            (SIZE_MAX - section_header_offset) /
                sizeof(Elf64_Shdr)) {
        goto oom;
    }

    total_size =
        section_header_offset +
        (output_section_count + 1U) * sizeof(Elf64_Shdr);
    if (!minild_program_plan_validate(state, &program_plan, total_size)) {
        goto done;
    }
    image = calloc(total_size == 0U ? 1U : total_size, 1U);
    if (image == NULL) {
        goto oom;
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
    header.e_shoff = section_header_offset;
    header.e_flags = state->elf_flags;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = (Elf64_Half)program_plan.count;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shnum =
        (Elf64_Half)(output_section_count + 1U);
    header.e_shstrndx = (Elf64_Half)shstrtab_index;
    memcpy(image, &header, sizeof(header));

    memcpy(image + sizeof(Elf64_Ehdr), program_plan.entries,
           program_plan.count * sizeof(Elf64_Phdr));

    if (!minild_output_plan_emit_sections(state, layout, &output_plan,
                                          image, total_size)) {
        goto done;
    }

    memcpy(image + headers[symtab_index].sh_offset,
           final_symbols.entries,
           headers[symtab_index].sh_size);
    memcpy(image + headers[strtab_index].sh_offset,
           final_symbols.names.data,
           final_symbols.names.size);
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
    if (fwrite(image, 1U, total_size, file) != total_size ||
        fflush(file) != 0) {
        fprintf(state->diagnostics,
                "minic-ld: write-error:%s\n",
                path);
        goto done;
    }
    ok = true;
    goto done;

oom:
    fprintf(state->diagnostics,
            "minic-ld: out-of-memory:static-output\n");

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
    free(headers);
    minild_program_plan_destroy(&program_plan);
    minild_output_plan_destroy(&output_plan);
    free(section_name_offsets);
    minild_final_symbols_destroy(&final_symbols);
    free(shstrtab.data);
    return ok;
}

typedef struct MiniLdSharedImage {
    size_t dynstr_section;
    size_t dynsym_section;
    size_t hash_section;
    size_t gnu_hash_section;
    size_t versym_section;
    size_t verdef_section;
    uint32_t version_name_offset;
    const MiniLdScript *version_script;
    const MiniLdScript *layout_script;
    size_t eh_frame_hdr_section;
    size_t eh_frame_fde_count;
    size_t rela_section;
    size_t got_section;
    size_t plt_section;
    size_t gotplt_section;
    size_t rela_plt_section;
    size_t dynamic_section;
    size_t interp_section;
    uint32_t soname_offset;
    uint32_t *needed_offsets;
    size_t needed_count;
    size_t *dynsym_index;
    uint32_t *dynstr_name_offset;
    size_t *plt_index;
    size_t *got_slot_offset;
    size_t *tls_gd_offset;
    bool *preemptible;
    unsigned char *dynamic_list_data;
    size_t dynamic_list_size;
    size_t dynsym_count;
    size_t symbol_map_capacity;
    size_t rela_count;
    size_t plt_count;
    size_t got_count;
    size_t tls_gd_count;
    bool have_soname;
    bool have_gnu_hash;
    bool have_eh_frame_hdr;
    bool have_interp;
    bool pie;
    bool have_dynamic_list;
} MiniLdSharedImage;

static void shared_image_destroy(MiniLdSharedImage *shared) {
    free(shared->dynsym_index);
    free(shared->dynstr_name_offset);
    free(shared->plt_index);
    free(shared->got_slot_offset);
    free(shared->tls_gd_offset);
    free(shared->preemptible);
    free(shared->needed_offsets);
    free(shared->dynamic_list_data);
    shared->dynsym_index = NULL;
    shared->dynstr_name_offset = NULL;
    shared->plt_index = NULL;
    shared->got_slot_offset = NULL;
    shared->tls_gd_offset = NULL;
    shared->preemptible = NULL;
    shared->needed_offsets = NULL;
    shared->needed_count = 0U;
    shared->dynamic_list_data = NULL;
    shared->dynamic_list_size = 0U;
    shared->dynsym_count = 0U;
    shared->rela_count = 0U;
    shared->plt_count = 0U;
    shared->got_count = 0U;
    shared->tls_gd_count = 0U;
    shared->have_soname = false;
    shared->have_gnu_hash = false;
    shared->have_interp = false;
    shared->pie = false;
    shared->have_dynamic_list = false;
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

static uint32_t shared_gnu_symbol_hash(const char *name) {
    uint32_t hash = UINT32_C(5381);
    while (*name != '\0') {
        hash = hash * UINT32_C(33) + (unsigned char)*name++;
    }
    return hash;
}

static bool shared_version_exports(const MiniLdScript *script,
                                   const char *name) {
    size_t i;
    if (script == NULL) {
        return true;
    }
    for (i = 0U; i < script->version_global_count; ++i) {
        if (strcmp(script->version_globals[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool shared_symbol_eligible(const MiniLdState *state, size_t index,
                                   const MiniLdScript *version) {
    const MiniLdResolvedSymbol *symbol = &state->resolver.entries[index];
    unsigned bind = ELF64_ST_BIND(symbol->info);
    unsigned visibility = ELF64_ST_VISIBILITY(symbol->other);

    if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
        symbol->name[0] == '\0' ||
        (visibility != STV_DEFAULT && visibility != STV_PROTECTED)) {
        return false;
    }
    if (version != NULL && symbol->section != MINILD_SECTION_UNDEF &&
        !shared_version_exports(version, symbol->name)) {
        return false;
    }
    if (symbol->section >= 0) {
        size_t section = (size_t)symbol->section;
        if (section >= state->work.count ||
            (state->work.sections[section].flags & SHF_ALLOC) == 0U) {
            return false;
        }
    }
    return true;
}

static bool shared_dynamic_list_name_char(unsigned char ch) {
    return (ch >= (unsigned char)'a' && ch <= (unsigned char)'z') ||
           (ch >= (unsigned char)'A' && ch <= (unsigned char)'Z') ||
           (ch >= (unsigned char)'0' && ch <= (unsigned char)'9') ||
           ch == (unsigned char)'_' ||
           ch == (unsigned char)'.' ||
           ch == (unsigned char)'$' ||
           ch == (unsigned char)'@';
}

static bool shared_dynamic_list_contains(const MiniLdSharedImage *shared,
                                         const char *name) {
    size_t name_size = strlen(name);
    size_t i = 0U;

    if (!shared->have_dynamic_list) {
        return true;
    }
    while (i < shared->dynamic_list_size) {
        size_t start;
        size_t size;

        while (i < shared->dynamic_list_size &&
               !shared_dynamic_list_name_char(shared->dynamic_list_data[i])) {
            ++i;
        }
        start = i;
        while (i < shared->dynamic_list_size &&
               shared_dynamic_list_name_char(shared->dynamic_list_data[i])) {
            ++i;
        }
        size = i - start;
        if (size == name_size &&
            size != 0U &&
            memcmp(shared->dynamic_list_data + start, name, size) == 0) {
            return true;
        }
    }
    return false;
}

static bool shared_load_dynamic_list(MiniLdState *state,
                                     MiniLdSharedImage *shared,
                                     const char *path) {
    if (path == NULL) {
        return true;
    }
    if (path[0] == '\0') {
        fprintf(state->diagnostics,
                "minic-ld: empty-dynamic-list\n");
        return false;
    }
    if (!read_file(path,
                   &shared->dynamic_list_data,
                   &shared->dynamic_list_size,
                   state->diagnostics)) {
        return false;
    }
    shared->have_dynamic_list = true;
    return true;
}

static bool shared_symbol_preemptible(const MiniLdState *state,
                                      const MiniLdSharedImage *shared,
                                      size_t index) {
    const MiniLdResolvedSymbol *symbol;
    unsigned bind;
    unsigned visibility;

    if (index >= state->resolver.count) {
        return false;
    }
    symbol = &state->resolver.entries[index];
    if (symbol->section == MINILD_SECTION_UNDEF) {
        return true;
    }
    bind = ELF64_ST_BIND(symbol->info);
    visibility = ELF64_ST_VISIBILITY(symbol->other);
    if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
        visibility != STV_DEFAULT) {
        return false;
    }
    return shared_dynamic_list_contains(shared, symbol->name);
}

static bool shared_symbol_needs_plt(const MiniLdState *state,
                                    const MiniLdSharedImage *shared,
                                    size_t index) {
    if (index >= state->resolver.count) {
        return false;
    }
    if (state->resolver.entries[index].section == MINILD_SECTION_UNDEF) {
        return true;
    }
    return shared->preemptible[index];
}


static bool shared_relocation_is_arithmetic(uint32_t type) {
    switch (type) {
    case R_RISCV_ADD8:
    case R_RISCV_ADD16:
    case R_RISCV_ADD32:
    case R_RISCV_ADD64:
    case R_RISCV_SUB6:
    case R_RISCV_SUB8:
    case R_RISCV_SUB16:
    case R_RISCV_SUB32:
    case R_RISCV_SUB64:
    case R_RISCV_SET6:
    case R_RISCV_SET8:
    case R_RISCV_SET16:
    case R_RISCV_SET32:
        return true;
    default:
        return false;
    }
}

static bool shared_relocation_is_final_layout(uint32_t type) {
    if (shared_relocation_is_arithmetic(type)) {
        return true;
    }
    switch (type) {
    case R_RISCV_GOT_HI20:
    case R_RISCV_PCREL_HI20:
    case R_RISCV_PCREL_LO12_I:
    case R_RISCV_PCREL_LO12_S:
    case R_RISCV_HI20:
    case R_RISCV_LO12_I:
    case R_RISCV_LO12_S:
    case R_RISCV_BRANCH:
    case R_RISCV_JAL:
    case R_RISCV_RVC_BRANCH:
    case R_RISCV_RVC_JUMP:
    case R_RISCV_32_PCREL:
    case R_RISCV_TLS_GD_HI20:
        return true;
    default:
        return false;
    }
}

static bool shared_define_linker_symbols_pre_layout(
    MiniLdState *state,
    size_t dynamic_section) {
    size_t index;
    MiniLdResolvedSymbol *symbol;

    index = symbol_resolver_lookup(&state->resolver, "_DYNAMIC");
    if (index != SIZE_MAX) {
        if (dynamic_section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-dynamic-section\n");
            return false;
        }
        symbol = &state->resolver.entries[index];
        if (symbol->section != MINILD_SECTION_UNDEF &&
            symbol->section != (int)dynamic_section) {
            fprintf(state->diagnostics,
                    "minic-ld: conflicting-linker-symbol:_DYNAMIC\n");
            return false;
        }
        symbol->section = (int)dynamic_section;
        symbol->value = 0U;
        symbol->size = 0U;
        symbol->info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
        symbol->other = STV_HIDDEN;
    }

    index = symbol_resolver_lookup(&state->resolver, "__ehdr_start");
    if (index != SIZE_MAX) {
        symbol = &state->resolver.entries[index];
        if (symbol->section != MINILD_SECTION_UNDEF &&
            symbol->section != MINILD_SECTION_ABS) {
            fprintf(state->diagnostics,
                    "minic-ld: conflicting-linker-symbol:__ehdr_start\n");
            return false;
        }
        symbol->section = MINILD_SECTION_ABS;
        symbol->value = 0U;
        symbol->size = 0U;
        symbol->info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        symbol->other = STV_HIDDEN;
    }
    return true;
}

static bool shared_finalize_ehdr_symbol(MiniLdState *state,
                                        const MiniLdStaticLayout *layout) {
    size_t index = symbol_resolver_lookup(&state->resolver, "__ehdr_start");
    uint64_t header_vaddr;

    if (index == SIZE_MAX) {
        return true;
    }
    if (!layout->have_rx ||
        (uint64_t)layout->rx_file_offset > layout->rx_vaddr) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-header-load-layout\n");
        return false;
    }
    header_vaddr =
        layout->rx_vaddr - (uint64_t)layout->rx_file_offset;
    state->resolver.entries[index].section = MINILD_SECTION_ABS;
    state->resolver.entries[index].value = header_vaddr;
    return true;
}

#include "linker_eh_frame.inc"

static bool shared_prepare_metadata(MiniLdState *state,
                                    const char *soname,
                                    const char *dynamic_list_path,
                                    const char *needed_name,
                                    const char *interpreter_path,
                                    bool pie,
                                    bool hash_style_both,
                                    bool eh_frame_hdr,
                                    const MiniLdScript *version_script,
                                    const MiniLdScript *layout_script,
                                    MiniLdSharedImage *shared) {
    MiniLdSection *dynstr;
    MiniLdSection *dynsym;
    MiniLdSection *hash;
    MiniLdSection *rela;
    MiniLdSection *got;
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
    shared->gnu_hash_section = SIZE_MAX;
    shared->versym_section = SIZE_MAX;
    shared->verdef_section = SIZE_MAX;
    shared->version_script = version_script;
    shared->eh_frame_hdr_section = SIZE_MAX;
    shared->have_gnu_hash = hash_style_both;
    shared->rela_section = SIZE_MAX;
    shared->got_section = SIZE_MAX;
    shared->plt_section = SIZE_MAX;
    shared->gotplt_section = SIZE_MAX;
    shared->rela_plt_section = SIZE_MAX;
    shared->dynamic_section = SIZE_MAX;
    shared->interp_section = SIZE_MAX;
    shared->pie = pie;
    /* Script output-section definitions can append globals after dynsym
     * sizing, so reserve their entire upper bound before allocating
     * per-symbol maps. Later local: * script symbols remain nonexports. */
    shared->symbol_map_capacity = state->resolver.count;
    if (layout_script != NULL) {
        for (i = 0U; i < layout_script->command_count; ++i) {
            const MiniLdScriptCommand *command = &layout_script->commands[i];
            size_t extra = 0U;
            size_t j;
            if (command->kind == MINILD_SCRIPT_DEFINE_SYMBOL) {
                extra = 1U;
            } else if (command->kind == MINILD_SCRIPT_OUTPUT_SECTION) {
                for (j = 0U; j < command->value.section.item_count; ++j) {
                    if (command->value.section.items[j].kind ==
                        MINILD_SCRIPT_SECTION_DEFINE_SYMBOL) {
                        ++extra;
                    }
                }
            }
            if (extra > SIZE_MAX - shared->symbol_map_capacity) {
                fprintf(state->diagnostics,
                        "minic-ld: script-symbol-map-overflow\n");
                goto fail;
            }
            shared->symbol_map_capacity += extra;
        }
    }
    if (shared->symbol_map_capacity >
        SIZE_MAX / sizeof(*shared->dynsym_index)) {
        fprintf(state->diagnostics,
                "minic-ld: script-symbol-map-overflow\n");
        goto fail;
    }
    if (shared->symbol_map_capacity == 0U) {
        shared->symbol_map_capacity = 1U;
    }

    shared->dynsym_index =
        malloc(shared->symbol_map_capacity *
               sizeof(*shared->dynsym_index));
    shared->dynstr_name_offset =
        calloc(shared->symbol_map_capacity,
               sizeof(*shared->dynstr_name_offset));
    shared->plt_index =
        malloc(shared->symbol_map_capacity *
               sizeof(*shared->plt_index));
    shared->got_slot_offset =
        malloc(shared->symbol_map_capacity *
               sizeof(*shared->got_slot_offset));
    shared->tls_gd_offset =
        malloc(shared->symbol_map_capacity *
               sizeof(*shared->tls_gd_offset));
    shared->preemptible =
        calloc(shared->symbol_map_capacity,
               sizeof(*shared->preemptible));
    {
        size_t maximum_needed = input_graph_dso_needed_count(&state->inputs) +
            (needed_name != NULL && needed_name[0] != '\0' ? 1U : 0U);

        shared->needed_offsets =
            calloc(maximum_needed == 0U ? 1U : maximum_needed,
                   sizeof(*shared->needed_offsets));
    }
    if (shared->dynsym_index == NULL ||
        shared->dynstr_name_offset == NULL ||
        shared->plt_index == NULL ||
        shared->got_slot_offset == NULL ||
        shared->tls_gd_offset == NULL ||
        shared->preemptible == NULL ||
        shared->needed_offsets == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:shared-symbol-map\n");
        goto fail;
    }
    for (i = 0U; i < shared->symbol_map_capacity; ++i) {
        shared->dynsym_index[i] = SIZE_MAX;
        shared->plt_index[i] = SIZE_MAX;
        shared->got_slot_offset[i] = SIZE_MAX;
        shared->tls_gd_offset[i] = SIZE_MAX;
    }
    if (!shared_load_dynamic_list(state, shared, dynamic_list_path)) {
        goto fail;
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
                             ".got",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_WRITE,
                             8U,
                             8U,
                             &shared->got_section) ||
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
    if (hash_style_both &&
        !find_or_add_section(state, ".gnu.hash", SHT_GNU_HASH,
                             SHF_ALLOC, 8U, 0U,
                             &shared->gnu_hash_section)) {
        goto fail;
    }
    if (version_script != NULL &&
        (!find_or_add_section(state, ".gnu.version",
                               SHT_GNU_versym, SHF_ALLOC, 2U, 2U,
                               &shared->versym_section) ||
         !find_or_add_section(state, ".gnu.version_d",
                               SHT_GNU_verdef, SHF_ALLOC, 4U, 0U,
                               &shared->verdef_section))) {
        goto fail;
    }
    if (interpreter_path != NULL && interpreter_path[0] != '\0') {
        if (!find_or_add_section(state,
                                 ".interp",
                                 SHT_PROGBITS,
                                 SHF_ALLOC,
                                 1U,
                                 0U,
                                 &shared->interp_section) ||
            !section_append_data(
                &state->work.sections[shared->interp_section],
                (const unsigned char *)interpreter_path,
                strlen(interpreter_path) + 1U)) {
            goto oom;
        }
        shared->have_interp = true;
    }

    dynstr = &state->work.sections[shared->dynstr_section];
    dynsym = &state->work.sections[shared->dynsym_section];
    hash = &state->work.sections[shared->hash_section];
    rela = &state->work.sections[shared->rela_section];
    got = &state->work.sections[shared->got_section];
    plt = &state->work.sections[shared->plt_section];
    gotplt = &state->work.sections[shared->gotplt_section];
    rela_plt = &state->work.sections[shared->rela_plt_section];
    dynamic = &state->work.sections[shared->dynamic_section];

    if (!shared_define_linker_symbols_pre_layout(
            state,
            shared->dynamic_section)) {
        goto fail;
    }
    for (i = 0U; i < state->resolver.count; ++i) {
        shared->preemptible[i] =
            shared_symbol_preemptible(state, shared, i) &&
            (version_script == NULL ||
             state->resolver.entries[i].section == MINILD_SECTION_UNDEF ||
             shared_version_exports(version_script,
                                    state->resolver.entries[i].name));
    }

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
    if (needed_name != NULL && needed_name[0] != '\0') {
        if (dynstr->size > UINT32_MAX) {
            goto oom;
        }
        shared->needed_offsets[shared->needed_count++] =
            (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)needed_name,
                                 strlen(needed_name) + 1U)) {
            goto oom;
        }
    }
    for (i = 0U; i < input_graph_dso_needed_count(&state->inputs); ++i) {
        const char *name = input_graph_dso_needed_name(&state->inputs, i);

        if (needed_name != NULL && strcmp(needed_name, name) == 0) {
            continue;
        }
        if (dynstr->size > UINT32_MAX) {
            goto oom;
        }
        shared->needed_offsets[shared->needed_count++] =
            (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)name,
                                 strlen(name) + 1U)) {
            goto oom;
        }
    }

    if (version_script != NULL) {
        if (dynstr->size > UINT32_MAX ||
            !section_append_data(dynstr,
                                  (const unsigned char *)version_script->version_name,
                                  strlen(version_script->version_name) + 1U)) {
            goto oom;
        }
        shared->version_name_offset =
            (uint32_t)(dynstr->size -
                       strlen(version_script->version_name) - 1U);
    }
    shared->dynsym_count = 1U;
    for (i = 0U; i < state->resolver.count; ++i) {
        if (!shared_symbol_eligible(state, i, version_script)) {
            continue;
        }
        if (dynstr->size > UINT32_MAX) {
            goto oom;
        }
        shared->dynstr_name_offset[i] = (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)state->resolver.entries[i].name,
                                 strlen(state->resolver.entries[i].name) + 1U)) {
            goto oom;
        }
        shared->dynsym_index[i] = shared->dynsym_count++;
    }

    if (version_script != NULL &&
        (shared->dynsym_count > SIZE_MAX / sizeof(Elf64_Half) ||
         !section_append_zero(&state->work.sections[shared->versym_section],
                              shared->dynsym_count * sizeof(Elf64_Half)) ||
         !section_append_zero(&state->work.sections[shared->verdef_section],
                              2U * (sizeof(Elf64_Verdef) +
                                    sizeof(Elf64_Verdaux))))) {
        goto oom;
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

    if (shared->have_gnu_hash) {
        size_t count = shared->dynsym_count;
        /* Header(16) + bloom(8) + bucket(4) + 4*(N-1) chain. */
        if (count > (SIZE_MAX - 24U) / 4U ||
            !section_append_zero(&state->work.sections[shared->gnu_hash_section],
                                 24U + count * 4U)) {
            goto oom;
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdResolvedSymbol *target;

        if (reloc->section >= state->work.count ||
            (state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U) {
            continue;
        }
        if (reloc->type == R_RISCV_NONE ||
            reloc->type == R_RISCV_RELAX ||
            reloc->type == R_RISCV_ALIGN) {
            continue;
        }
        if (reloc->type == R_RISCV_GOT_HI20) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                reloc->addend != 0) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-shared-got-hi20-relocation\n");
                goto fail;
            }
            if (shared->got_slot_offset[reloc->symbol] == SIZE_MAX) {
                if (shared->got_count > (SIZE_MAX / 8U) - 1U ||
                    shared->rela_count == SIZE_MAX) {
                    goto oom;
                }
                shared->got_slot_offset[reloc->symbol] =
                    shared->got_count * 8U;
                ++shared->got_count;
                ++shared->rela_count;
            }
            continue;
        }
        if (reloc->type == R_RISCV_TLS_GD_HI20) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                shared->dynsym_index[reloc->symbol] == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-shared-tls-gd-relocation\n");
                goto fail;
            }
            if (shared->tls_gd_offset[reloc->symbol] == SIZE_MAX) {
                if (shared->tls_gd_count > (SIZE_MAX / 16U) - 1U) {
                    goto oom;
                }
                shared->tls_gd_offset[reloc->symbol] =
                    shared->tls_gd_count * 16U;
                ++shared->tls_gd_count;
                if (shared->rela_count > SIZE_MAX - 2U) {
                    goto oom;
                }
                shared->rela_count += 2U;
            }
            continue;
        }
        if (shared_relocation_is_final_layout(reloc->type)) {
            continue;
        }
        if (reloc->type == R_RISCV_CALL ||
            reloc->type == R_RISCV_CALL_PLT) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-shared-call-relocation\n");
                goto fail;
            }
            target = &state->resolver.entries[reloc->symbol];
            if (shared_symbol_needs_plt(state,
                                        shared,
                                        reloc->symbol) &&
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
    if (shared->got_count > SIZE_MAX / 8U ||
        shared->tls_gd_count > SIZE_MAX / 16U ||
        shared->got_count * 8U >
            SIZE_MAX - shared->tls_gd_count * 16U) {
        goto oom;
    }
    if (shared->tls_gd_count != 0U) {
        size_t got_prefix = shared->got_count * 8U;

        for (i = 0U; i < state->resolver.count; ++i) {
            if (shared->tls_gd_offset[i] != SIZE_MAX) {
                shared->tls_gd_offset[i] += got_prefix;
            }
        }
    }
    if ((shared->got_count != 0U || shared->tls_gd_count != 0U) &&
        !section_append_zero(got,
                             shared->got_count * 8U +
                                 shared->tls_gd_count * 16U)) {
        goto oom;
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
    if (shared->have_gnu_hash) {
        ++dynamic_entries;
    }
    if (version_script != NULL) {
        dynamic_entries += 3U; /* VERSYM/VERDEF/VERDEFNUM */
    }
    if (shared->rela_count != 0U) {
        dynamic_entries += 3U; /* RELA/RELASZ/RELAENT */
    }
    if (shared->plt_count != 0U) {
        dynamic_entries += 5U; /* PLTGOT/PLTRELSZ/PLTREL/JMPREL/BIND_NOW */
    }
    if (shared->have_soname) {
        ++dynamic_entries;
    }
    if (shared->needed_count > SIZE_MAX - dynamic_entries) {
        goto oom;
    }
    dynamic_entries += shared->needed_count;
    if (shared->pie) {
        ++dynamic_entries; /* FLAGS_1 */
    }
    if (dynamic_entries > SIZE_MAX / sizeof(Elf64_Dyn) ||
        !section_append_zero(dynamic,
                             dynamic_entries * sizeof(Elf64_Dyn))) {
        goto oom;
    }

    if (eh_frame_hdr && !minild_eh_prepare(state, shared)) {
        goto fail;
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
                               const MiniLdOutputPlan *output_plan,
                               const MiniLdSharedImage *shared) {
    MiniLdSection *dynsym = &state->work.sections[shared->dynsym_section];
    size_t i;

    memset(dynsym->data, 0, dynsym->size);
    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *input = &state->resolver.entries[i];
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
                   (size_t)input->section < state->work.count) {
            if (output_plan->work_shndx[input->section] == 0U) {
                output.st_shndx = SHN_UNDEF;
                memcpy(dynsym->data + dynamic_index * sizeof(output),
                       &output, sizeof(output));
                continue;
            }
            output.st_shndx =
                (Elf64_Section)output_plan->work_shndx[input->section];
            if (ELF64_ST_TYPE(input->info) == STT_TLS) {
                uint64_t tls_base = 0U;
                bool tls_present = false;

                if (!minild_final_tls_base_address(state,
                                                    layout,
                                                    output_plan,
                                                    &tls_base,
                                                    &tls_present) ||
                    !tls_present ||
                    layout->section_vaddr[input->section] + input->value <
                        tls_base) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-shared-tls-symbol:%s\n",
                            input->name);
                    return false;
                }
                output.st_value =
                    layout->section_vaddr[input->section] +
                    input->value - tls_base;
            } else {
                output.st_value =
                    layout->section_vaddr[input->section] + input->value;
            }
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
    MiniLdSection *hash = &state->work.sections[shared->hash_section];
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

    for (i = 0U; i < state->resolver.count; ++i) {
        size_t dynamic_index = shared->dynsym_index[i];
        uint32_t bucket;
        uint32_t *slot;

        if (dynamic_index == SIZE_MAX) {
            continue;
        }
        bucket = shared_elf_hash(state->resolver.entries[i].name) % (uint32_t)nbucket;
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

/* ABI-correct one-bucket ELF64 GNU hash. The pre-existing dynsym indices
 * 1..N-1 are contiguous, allowing the complete chain without reordering. */
static bool shared_fill_gnu_hash(MiniLdState *state,
                                 const MiniLdSharedImage *shared) {
    MiniLdSection *section;
    uint64_t bloom = 0U;
    size_t i;

    if (!shared->have_gnu_hash) {
        return true;
    }
    section = &state->work.sections[shared->gnu_hash_section];
    if (shared->dynsym_count == 0U ||
        shared->dynsym_count > UINT32_MAX ||
        shared->dynsym_count > (SIZE_MAX - 24U) / 4U ||
        section->size != 24U + 4U * shared->dynsym_count) {
        fprintf(state->diagnostics, "minic-ld: invalid-gnu-hash-layout\n");
        return false;
    }
    memset(section->data, 0, section->size);
    store_u32le(section->data + 0U, 1U);
    store_u32le(section->data + 4U, 1U);
    store_u32le(section->data + 8U, 1U);
    store_u32le(section->data + 12U, 5U);
    store_u32le(section->data + 24U,
                shared->dynsym_count == 1U ? 0U : 1U);
    for (i = 0U; i < state->resolver.count; ++i) {
        size_t index = shared->dynsym_index[i];
        uint32_t hash;
        if (index == SIZE_MAX) {
            continue;
        }
        if (index == 0U || index >= shared->dynsym_count) {
            fprintf(state->diagnostics, "minic-ld: invalid-gnu-hash-symbol\n");
            return false;
        }
        hash = shared_gnu_symbol_hash(state->resolver.entries[i].name);
        bloom |= UINT64_C(1) << (hash % 64U);
        bloom |= UINT64_C(1) << ((hash >> 5U) % 64U);
        store_u32le(section->data + 28U + (index - 1U) * 4U,
                    (hash & ~UINT32_C(1)) |
                    (index + 1U == shared->dynsym_count ? 1U : 0U));
    }
    store_u64le(section->data + 16U, bloom);
    return true;
}

#include "linker_version.inc"

static bool shared_patch_arithmetic_relocations(
    MiniLdState *state,
    const MiniLdStaticLayout *layout) {
    size_t i;

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        uint64_t symbol_value = 0U;
        int64_t target;

        if (!shared_relocation_is_arithmetic(reloc->type)) {
            continue;
        }
        if (reloc->section >= state->work.count ||
            (state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U) {
            continue;
        }
        section = &state->work.sections[reloc->section];
        if (reloc->symbol != SIZE_MAX &&
            (reloc->symbol >= state->resolver.count ||
             !static_resolve_symbol(state,
                                    layout,
                                    reloc->symbol,
                                    &symbol_value))) {
            return false;
        }
        target = (int64_t)symbol_value + reloc->addend;

        switch (reloc->type) {
        case R_RISCV_ADD8:
        case R_RISCV_SUB8: {
            uint8_t current;
            uint8_t operand;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 1U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: shared-ADD_SUB8-offset-out-of-range\n");
                return false;
            }
            current = load_u8(section->data + (size_t)reloc->offset);
            operand = (uint8_t)(uint64_t)target;
            current = reloc->type == R_RISCV_ADD8
                          ? (uint8_t)(current + operand)
                          : (uint8_t)(current - operand);
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
                        "minic-ld: shared-SUB6-offset-out-of-range\n");
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
                        "minic-ld: shared-SET6-offset-out-of-range\n");
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
                        "minic-ld: shared-SET8-offset-out-of-range\n");
                return false;
            }
            store_u8(section->data + (size_t)reloc->offset,
                     (uint8_t)(uint64_t)target);
            break;
        case R_RISCV_SET16:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 2U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: shared-SET16-offset-out-of-range\n");
                return false;
            }
            store_u16le(section->data + (size_t)reloc->offset,
                        (uint16_t)(uint64_t)target);
            break;
        case R_RISCV_SET32:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: shared-SET32-offset-out-of-range\n");
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
                        "minic-ld: shared-ADD_SUB16-offset-out-of-range\n");
                return false;
            }
            current = load_u16le(section->data + (size_t)reloc->offset);
            operand = (uint16_t)(uint64_t)target;
            current = reloc->type == R_RISCV_ADD16
                          ? (uint16_t)(current + operand)
                          : (uint16_t)(current - operand);
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
                        "minic-ld: shared-ADD_SUB32-offset-out-of-range\n");
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
            uint64_t current;
            uint64_t operand = (uint64_t)target;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 8U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: shared-ADD_SUB64-offset-out-of-range\n");
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
        default:
            fprintf(state->diagnostics,
                    "minic-ld: internal-shared-arithmetic-relocation:%u\n",
                    reloc->type);
            return false;
        }
    }
    return true;
}

static bool shared_fill_relocations(MiniLdState *state,
                                    const MiniLdStaticLayout *layout,
                                    const MiniLdSharedImage *shared) {
    MiniLdSection *rela = &state->work.sections[shared->rela_section];
    size_t write_index = 0U;
    size_t i;

    memset(rela->data, 0, rela->size);

    for (i = 0U; i < state->resolver.count; ++i) {
        size_t offset = shared->got_slot_offset[i];
        MiniLdResolvedSymbol *target;
        Elf64_Rela output;

        if (offset == SIZE_MAX) {
            continue;
        }
        target = &state->resolver.entries[i];
        memset(&output, 0, sizeof(output));
        output.r_offset =
            layout->section_vaddr[shared->got_section] + (uint64_t)offset;
        if (target->section != MINILD_SECTION_UNDEF &&
            !shared->preemptible[i]) {
            uint64_t target_value;

            if (!static_resolve_symbol(state, layout, i, &target_value)) {
                return false;
            }
            output.r_info = ELF64_R_INFO(0U, R_RISCV_RELATIVE);
            output.r_addend = (Elf64_Sxword)target_value;
        } else {
            size_t dynamic_index = shared->dynsym_index[i];

            if (dynamic_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-dynsym-for-got:%s\n",
                        target->name);
                return false;
            }
            output.r_info = ELF64_R_INFO(dynamic_index, R_RISCV_64);
            output.r_addend = 0;
        }
        memcpy(rela->data + write_index * sizeof(output),
               &output,
               sizeof(output));
        ++write_index;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *input = &state->relocations.entries[i];
        MiniLdSection *source;
        MiniLdResolvedSymbol *target;
        Elf64_Rela output;
        size_t dynamic_index = 0U;

        if (input->section >= state->work.count ||
            (state->work.sections[input->section].flags & SHF_ALLOC) == 0U ||
            input->type == R_RISCV_NONE ||
            input->type == R_RISCV_RELAX ||
            input->type == R_RISCV_ALIGN ||
            input->type == R_RISCV_CALL ||
            input->type == R_RISCV_CALL_PLT ||
            shared_relocation_is_final_layout(input->type)) {
            continue;
        }
        if (input->type != R_RISCV_64 ||
            input->symbol == SIZE_MAX ||
            input->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-relocation:%u\n",
                    input->type);
            return false;
        }
        source = &state->work.sections[input->section];
        if (source->type == SHT_NOBITS ||
            input->offset > SIZE_MAX ||
            !range_ok((size_t)input->offset, 8U, source->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: shared-relocation-offset-out-of-range\n");
            return false;
        }
        target = &state->resolver.entries[input->symbol];

        memset(&output, 0, sizeof(output));
        output.r_offset =
            layout->section_vaddr[input->section] + input->offset;
        if (target->section != MINILD_SECTION_UNDEF &&
            !shared->preemptible[input->symbol]) {
            uint64_t target_value;

            if (!static_resolve_symbol(state,
                                       layout,
                                       input->symbol,
                                       &target_value)) {
                return false;
            }
            output.r_info = ELF64_R_INFO(0U, R_RISCV_RELATIVE);
            output.r_addend =
                (Elf64_Sxword)((int64_t)target_value + input->addend);
        } else {
            dynamic_index = shared->dynsym_index[input->symbol];
            if (dynamic_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-dynsym-for-relocation:%s\n",
                        target->name);
                return false;
            }
            output.r_info = ELF64_R_INFO(dynamic_index, R_RISCV_64);
            output.r_addend = input->addend;
        }
        store_u64le(source->data + (size_t)input->offset, 0U);
        memcpy(rela->data + write_index * sizeof(output),
               &output,
               sizeof(output));
        ++write_index;
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        size_t offset = shared->tls_gd_offset[i];
        size_t dynamic_index;
        uint64_t slot_address;
        Elf64_Rela output;

        if (offset == SIZE_MAX) {
            continue;
        }
        dynamic_index = shared->dynsym_index[i];
        if (dynamic_index == SIZE_MAX ||
            shared->got_section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-tls-gd-dynsym\n");
            return false;
        }
        slot_address =
            layout->section_vaddr[shared->got_section] + (uint64_t)offset;

        memset(&output, 0, sizeof(output));
        output.r_offset = slot_address;
        output.r_info =
            ELF64_R_INFO(dynamic_index, R_RISCV_TLS_DTPMOD64);
        memcpy(rela->data + write_index * sizeof(output),
               &output,
               sizeof(output));
        ++write_index;

        memset(&output, 0, sizeof(output));
        output.r_offset = slot_address + 8U;
        output.r_info =
            ELF64_R_INFO(dynamic_index, R_RISCV_TLS_DTPREL64);
        memcpy(rela->data + write_index * sizeof(output),
               &output,
               sizeof(output));
        ++write_index;
    }

    if (write_index != shared->rela_count) {
        fprintf(state->diagnostics,
                "minic-ld: shared-relocation-count-mismatch:%zu:%zu\n",
                write_index,
                shared->rela_count);
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

static bool shared_patch_final_layout_relocations(
    MiniLdState *state,
    const MiniLdStaticLayout *layout,
    const MiniLdSharedImage *shared) {
    size_t capacity = pcrel_capacity_for(state->relocations.count);
    MiniLdPcrelSlot *pcrel;
    size_t i;

    if (!shared_patch_arithmetic_relocations(state, layout)) {
        return false;
    }
    if (capacity == 0U) {
        fprintf(state->diagnostics,
                "minic-ld: shared-pcrel-index-overflow\n");
        return false;
    }
    pcrel = calloc(capacity, sizeof(*pcrel));
    if (pcrel == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:shared-pcrel-index\n");
        return false;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *source;
        uint64_t symbol_value = 0U;
        uint64_t place;
        int64_t target;
        int64_t delta;

        if (reloc->section >= state->work.count ||
            (state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U ||
            reloc->type == R_RISCV_NONE ||
            reloc->type == R_RISCV_RELAX ||
            reloc->type == R_RISCV_ALIGN ||
            reloc->type == R_RISCV_64 ||
            reloc->type == R_RISCV_CALL ||
            reloc->type == R_RISCV_CALL_PLT ||
            reloc->type == R_RISCV_PCREL_LO12_I ||
            reloc->type == R_RISCV_PCREL_LO12_S ||
            shared_relocation_is_arithmetic(reloc->type)) {
            continue;
        }
        if (!shared_relocation_is_final_layout(reloc->type)) {
            continue;
        }
        if (reloc->symbol != SIZE_MAX &&
            reloc->type != R_RISCV_GOT_HI20 &&
            reloc->type != R_RISCV_TLS_GD_HI20 &&
            (reloc->symbol >= state->resolver.count ||
             !static_resolve_symbol(state,
                                    layout,
                                    reloc->symbol,
                                    &symbol_value))) {
            free(pcrel);
            return false;
        }

        source = &state->work.sections[reloc->section];
        place = layout->section_vaddr[reloc->section] + reloc->offset;
        target = (int64_t)symbol_value + reloc->addend;

        switch (reloc->type) {
        case R_RISCV_GOT_HI20: {
            size_t offset;
            uint64_t got_address;

            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                shared->got_slot_offset[reloc->symbol] == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-shared-got-slot\n");
                free(pcrel);
                return false;
            }
            offset = shared->got_slot_offset[reloc->symbol];
            got_address =
                layout->section_vaddr[shared->got_section] + (uint64_t)offset;
            delta = (int64_t)got_address - (int64_t)place;
            if (!static_patch_utype(source,
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
        case R_RISCV_TLS_GD_HI20: {
            uint64_t descriptor;

            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                shared->tls_gd_offset[reloc->symbol] == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-shared-tls-gd-slot\n");
                free(pcrel);
                return false;
            }
            descriptor =
                layout->section_vaddr[shared->got_section] +
                (uint64_t)shared->tls_gd_offset[reloc->symbol];
            delta = (int64_t)descriptor + reloc->addend - (int64_t)place;
            if (!static_patch_utype(source,
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
            if (!static_patch_utype(source,
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
            if (!static_patch_utype(source,
                                    reloc->offset,
                                    riscv_hi20(target),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_LO12_I:
            if (!static_patch_itype(source,
                                    reloc->offset,
                                    riscv_lo12(target),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_LO12_S:
            if (!static_patch_stype(source,
                                    reloc->offset,
                                    riscv_lo12(target),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_BRANCH:
            delta = target - (int64_t)place;
            if (!static_patch_branch(source,
                                     reloc->offset,
                                     delta,
                                     state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_JAL:
            delta = target - (int64_t)place;
            if (!static_patch_jal(source,
                                  reloc->offset,
                                  delta,
                                  state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_RVC_BRANCH:
            delta = target - (int64_t)place;
            if (!static_patch_rvc_branch(source,
                                         reloc->offset,
                                         delta,
                                         state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_RVC_JUMP:
            delta = target - (int64_t)place;
            if (!static_patch_rvc_jump(source,
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
                source->type == SHT_NOBITS ||
                reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, source->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: shared-R_RISCV_32_PCREL-range\n");
                free(pcrel);
                return false;
            }
            store_u32le(source->data + (size_t)reloc->offset,
                        (uint32_t)(int32_t)delta);
            break;
        default:
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-final-layout:%u\n",
                    reloc->type);
            free(pcrel);
            return false;
        }
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdResolvedSymbol *anchor;
        MiniLdSection *source;
        int64_t anchor_offset;
        int64_t delta;

        if (reloc->type != R_RISCV_PCREL_LO12_I &&
            reloc->type != R_RISCV_PCREL_LO12_S) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-pcrel-lo-symbol\n");
            free(pcrel);
            return false;
        }
        anchor = &state->resolver.entries[reloc->symbol];
        if (anchor->section < 0 ||
            (size_t)anchor->section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-pcrel-lo-anchor:%s\n",
                    anchor->name);
            free(pcrel);
            return false;
        }
        anchor_offset = (int64_t)anchor->value + reloc->addend;
        if (anchor_offset < 0 ||
            !pcrel_find(pcrel,
                        capacity,
                        (size_t)anchor->section,
                        (uint64_t)anchor_offset,
                        &delta)) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-shared-pcrel-hi20:%s\n",
                    anchor->name);
            free(pcrel);
            return false;
        }
        source = &state->work.sections[reloc->section];
        if ((reloc->type == R_RISCV_PCREL_LO12_I &&
             !static_patch_itype(source,
                                 reloc->offset,
                                 riscv_lo12(delta),
                                 state->diagnostics)) ||
            (reloc->type == R_RISCV_PCREL_LO12_S &&
             !static_patch_stype(source,
                                 reloc->offset,
                                 riscv_lo12(delta),
                                 state->diagnostics))) {
            free(pcrel);
            return false;
        }
    }

    free(pcrel);
    return true;
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

    plt = &state->work.sections[shared->plt_section];
    gotplt = &state->work.sections[shared->gotplt_section];
    rela_plt = &state->work.sections[shared->rela_plt_section];
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

    for (i = 0U; i < state->resolver.count; ++i) {
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
                    state->resolver.entries[i].name);
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

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *source;
        MiniLdResolvedSymbol *target;
        uint64_t destination;

        if (reloc->type != R_RISCV_CALL &&
            reloc->type != R_RISCV_CALL_PLT) {
            continue;
        }
        if (reloc->section >= state->work.count ||
            (state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-call-symbol\n");
            return false;
        }

        source = &state->work.sections[reloc->section];
        target = &state->resolver.entries[reloc->symbol];
        if (shared_symbol_needs_plt(state, shared, reloc->symbol)) {
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
    MiniLdSection *dynamic = &state->work.sections[shared->dynamic_section];
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
    if (shared->have_gnu_hash) {
        MINILD_DYN(DT_GNU_HASH, layout->section_vaddr[shared->gnu_hash_section]);
    }
    if (shared->version_script != NULL) {
        MINILD_DYN(DT_VERSYM, layout->section_vaddr[shared->versym_section]);
        MINILD_DYN(DT_VERDEF, layout->section_vaddr[shared->verdef_section]);
        MINILD_DYN(DT_VERDEFNUM, 2U);
    }
    MINILD_DYN(DT_STRTAB, layout->section_vaddr[shared->dynstr_section]);
    MINILD_DYN(DT_SYMTAB, layout->section_vaddr[shared->dynsym_section]);
    MINILD_DYN(DT_STRSZ, state->work.sections[shared->dynstr_section].size);
    MINILD_DYN(DT_SYMENT, sizeof(Elf64_Sym));
    if (shared->rela_count != 0U) {
        MINILD_DYN(DT_RELA, layout->section_vaddr[shared->rela_section]);
        MINILD_DYN(DT_RELASZ, state->work.sections[shared->rela_section].size);
        MINILD_DYN(DT_RELAENT, sizeof(Elf64_Rela));
    }
    if (shared->plt_count != 0U) {
        MINILD_DYN(DT_PLTGOT, layout->section_vaddr[shared->gotplt_section]);
        MINILD_DYN(DT_PLTRELSZ, state->work.sections[shared->rela_plt_section].size);
        MINILD_DYN(DT_PLTREL, DT_RELA);
        MINILD_DYN(DT_JMPREL, layout->section_vaddr[shared->rela_plt_section]);
        MINILD_DYN(DT_BIND_NOW, 0U);
    }
    {
        size_t needed_index;

        for (needed_index = 0U;
             needed_index < shared->needed_count;
             ++needed_index) {
            MINILD_DYN(DT_NEEDED, shared->needed_offsets[needed_index]);
        }
    }
    if (shared->have_soname) {
        MINILD_DYN(DT_SONAME, shared->soname_offset);
    }
    if (shared->pie) {
        MINILD_DYN(DT_FLAGS_1, UINT64_C(0x08000000)); /* DF_1_PIE */
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


typedef struct MiniLdSharedTlsSegment {
    bool present;
    size_t file_offset;
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} MiniLdSharedTlsSegment;

static bool shared_tls_segment(const MiniLdState *state,
                               const MiniLdStaticLayout *layout,
                               MiniLdSharedTlsSegment *tls) {
    uint64_t base = UINT64_MAX;
    uint64_t memory_end = 0U;
    uint64_t file_end = 0U;
    uint64_t alignment = 1U;
    size_t i;
    bool have_file = false;

    memset(tls, 0, sizeof(*tls));
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        uint64_t start;
        uint64_t end;

        if ((section->flags & (SHF_ALLOC | SHF_TLS)) !=
            (SHF_ALLOC | SHF_TLS)) {
            continue;
        }
        start = layout->section_vaddr[i];
        if (section->size > UINT64_MAX - start) {
            return false;
        }
        end = start + (uint64_t)section->size;
        if (!tls->present || start < base) {
            base = start;
        }
        if (!tls->present || end > memory_end) {
            memory_end = end;
        }
        if (section->align > alignment) {
            alignment = section->align;
        }
        if (section->type != SHT_NOBITS && section->size != 0U) {
            if (layout->section_file_offset[i] == SIZE_MAX) {
                return false;
            }
            if (!have_file || end > file_end) {
                file_end = end;
            }
            have_file = true;
        }
        tls->present = true;
    }

    if (!tls->present) {
        return true;
    }
    if (!layout->have_rw || base < layout->rw_vaddr ||
        base - layout->rw_vaddr > SIZE_MAX ||
        layout->rw_file_offset >
            SIZE_MAX - (size_t)(base - layout->rw_vaddr)) {
        return false;
    }

    tls->vaddr = base;
    tls->file_offset =
        layout->rw_file_offset + (size_t)(base - layout->rw_vaddr);
    tls->filesz = have_file ? file_end - base : 0U;
    tls->memsz = memory_end - base;
    tls->align = alignment == 0U ? 1U : alignment;
    return true;
}

/* The ET_DYN segment policy owns the program headers before serialization.
 * PT_LOAD includes ELF headers; PIE, TLS and vDSO introduce distinct PHDRs. */
static bool minild_shared_program_plan_build(
    const MiniLdState *state,
    const MiniLdStaticLayout *layout,
    const MiniLdSharedImage *shared,
    MiniLdProgramPlan *plan) {
    MiniLdSharedTlsSegment tls;
    size_t phnum;
    size_t program_index = 0U;
    size_t i;
    Elf64_Phdr *programs;

    memset(plan, 0, sizeof(*plan));
    if (!shared_tls_segment(state, layout, &tls)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-tls-segment-layout\n");
        return false;
    }
    phnum = (shared->have_eh_frame_hdr ||
             shared->layout_script != NULL ? 1U : 0U) +
            (shared->layout_script != NULL ? 1U : 0U) +
            (shared->pie ? 1U : 0U) +
            (shared->have_interp ? 1U : 0U) +
            (layout->have_rx ? 1U : 0U) +
            (layout->have_rw ? 1U : 0U) +
            (tls.present ? 1U : 0U) + 1U;
    if (phnum > UINT16_MAX ||
        phnum > (SIZE_MAX - sizeof(Elf64_Ehdr)) / sizeof(Elf64_Phdr)) {
        fprintf(state->diagnostics, "minic-ld: too-many-shared-segments\n");
        return false;
    }
    plan->entries = calloc(phnum, sizeof(*plan->entries));
    if (plan->entries == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:shared-segments\n");
        return false;
    }
    plan->count = phnum;
    programs = plan->entries;

    if (shared->pie) {
        Elf64_Phdr *ph = &programs[program_index++];
        uint64_t header_vaddr;

        if (!layout->have_rx ||
            (uint64_t)layout->rx_file_offset > layout->rx_vaddr) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-pie-phdr-layout\n");
            goto failed;
        }
        header_vaddr =
            layout->rx_vaddr - (uint64_t)layout->rx_file_offset;
        ph->p_type = PT_PHDR;
        ph->p_flags = PF_R;
        ph->p_offset = sizeof(Elf64_Ehdr);
        ph->p_vaddr = header_vaddr + sizeof(Elf64_Ehdr);
        ph->p_paddr = ph->p_vaddr;
        ph->p_filesz = phnum * sizeof(Elf64_Phdr);
        ph->p_memsz = ph->p_filesz;
        ph->p_align = sizeof(Elf64_Addr);
    }
    if (shared->have_interp) {
        Elf64_Phdr *ph = &programs[program_index++];
        const MiniLdSection *interp;

        if (shared->interp_section >= state->work.count ||
            layout->section_file_offset[shared->interp_section] == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-pie-interpreter-layout\n");
            goto failed;
        }
        interp = &state->work.sections[shared->interp_section];
        ph->p_type = PT_INTERP;
        ph->p_flags = PF_R;
        ph->p_offset = layout->section_file_offset[shared->interp_section];
        ph->p_vaddr = layout->section_vaddr[shared->interp_section];
        ph->p_paddr = ph->p_vaddr;
        ph->p_filesz = interp->size;
        ph->p_memsz = interp->size;
        ph->p_align = 1U;
    }
    if (layout->have_rx) {
        Elf64_Phdr *ph = &programs[program_index++];
        uint64_t header_vaddr;

        if ((uint64_t)layout->rx_file_offset > layout->rx_vaddr ||
            layout->rx_file_offset >
                SIZE_MAX - layout->rx_file_size) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-header-load-layout\n");
            goto failed;
        }
        header_vaddr =
            layout->rx_vaddr - (uint64_t)layout->rx_file_offset;
        ph->p_type = PT_LOAD;
        ph->p_flags = PF_R | PF_X;
        ph->p_offset = 0U;
        ph->p_vaddr = header_vaddr;
        ph->p_paddr = header_vaddr;
        ph->p_filesz =
            (uint64_t)(layout->rx_file_offset + layout->rx_file_size);
        ph->p_memsz = ph->p_filesz;
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
    if (tls.present) {
        Elf64_Phdr *ph = &programs[program_index++];
        ph->p_type = PT_TLS;
        ph->p_flags = PF_R;
        ph->p_offset = tls.file_offset;
        ph->p_vaddr = tls.vaddr;
        ph->p_paddr = tls.vaddr;
        ph->p_filesz = tls.filesz;
        ph->p_memsz = tls.memsz;
        ph->p_align = tls.align;
    }
    {
        Elf64_Phdr *ph = &programs[program_index++];
        MiniLdSection *dynamic = &state->work.sections[shared->dynamic_section];
        size_t offset = layout->section_file_offset[shared->dynamic_section];

        if (offset == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-shared-dynamic-file-offset\n");
            goto failed;
        }
        ph->p_type = PT_DYNAMIC;
        ph->p_flags = shared->layout_script != NULL ? PF_R : PF_R | PF_W;
        ph->p_offset = offset;
        ph->p_vaddr = layout->section_vaddr[shared->dynamic_section];
        ph->p_paddr = ph->p_vaddr;
        ph->p_filesz = dynamic->size;
        ph->p_memsz = dynamic->size;
        ph->p_align = 8U;
    }

    if (shared->layout_script != NULL) {
        /* Emit only the actual GNU vDSO mapped PT_NOTE section(s).
         * A missing or out-of-load note is a hard failure. */
        size_t note_offset = SIZE_MAX;
        uint64_t note_addr = UINT64_MAX;
        uint64_t note_end = 0U;
        for (i = 0U; i < state->work.count; ++i) {
            const MiniLdSection *section = &state->work.sections[i];
            size_t offset = layout->section_file_offset[i];
            uint64_t address = layout->section_vaddr[i];
            if (section->type != SHT_NOTE ||
                (section->flags & SHF_ALLOC) == 0U ||
                layout->section_discarded[i] || section->size == 0U) {
                continue;
            }
            if (offset == SIZE_MAX ||
                address > UINT64_MAX - section->size ||
                offset > SIZE_MAX - section->size) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-vdso-script-note-placement\n");
                goto failed;
            }
            if (offset < note_offset) {
                note_offset = offset;
            }
            if (address < note_addr) {
                note_addr = address;
            }
            if (address + section->size > note_end) {
                note_end = address + section->size;
            }
        }
        if (note_offset == SIZE_MAX || note_addr == UINT64_MAX ||
            note_end <= note_addr ||
            note_end - note_addr > layout->rx_file_size) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-vdso-script-PT_NOTE\n");
            goto failed;
        }
        {
            Elf64_Phdr *ph = &programs[program_index++];
            ph->p_type = PT_NOTE;
            ph->p_flags = PF_R;
            ph->p_offset = note_offset;
            ph->p_vaddr = note_addr;
            ph->p_paddr = note_addr;
            ph->p_filesz = note_end - note_addr;
            ph->p_memsz = ph->p_filesz;
            ph->p_align = 4U;
        }
    }
    if (shared->have_eh_frame_hdr ||
        shared->layout_script != NULL) {
        Elf64_Phdr *ph = &programs[program_index++];
        memset(ph, 0, sizeof(*ph));
        ph->p_type = PT_GNU_EH_FRAME;
        if (shared->have_eh_frame_hdr) {
            size_t h = shared->eh_frame_hdr_section;
            if (h >= state->work.count ||
                layout->section_file_offset[h] == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-eh-frame-hdr-file-offset\n");
                goto failed;
            }
            ph->p_flags = PF_R;
            ph->p_offset = layout->section_file_offset[h];
            ph->p_vaddr = layout->section_vaddr[h];
            ph->p_paddr = ph->p_vaddr;
            ph->p_filesz = state->work.sections[h].size;
            ph->p_memsz = ph->p_filesz;
            ph->p_align = 4U;
        } else {
            /* GNU's explicit-script header when all vDSO CFI FDEs
             * disappear: 0-byte PT_GNU_EH_FRAME, no invented section. */
            ph->p_align = 8U;
        }
    }
    if (program_index != phnum) {
        fprintf(state->diagnostics, "minic-ld: shared-phdr-count-mismatch\n");
        goto failed;
    }
    return true;

failed:
    minild_program_plan_destroy(plan);
    return false;
}

/* Only ET_DYN synthetic sections add dynamic sh_link/sh_info policy.
 * All generic section identity and LINK_ORDER links are already finalized. */
static void minild_shared_output_section_links(
    const MiniLdState *state,
    const MiniLdOutputPlan *output_plan,
    const MiniLdSharedImage *shared,
    Elf64_Shdr *section_headers) {
    size_t i;
    for (i = 0U; i < output_plan->count; ++i) {
        Elf64_Shdr *sh = &section_headers[i + 1U];

        if (i + 1U == minild_output_shndx(output_plan, state,
                                           shared->dynsym_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, state, shared->dynstr_section);
            sh->sh_info = 1U;
        } else if (shared->version_script != NULL &&
                   i + 1U == minild_output_shndx(output_plan, state,
                                                  shared->versym_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, state, shared->dynsym_section);
        } else if (shared->version_script != NULL &&
                   i + 1U == minild_output_shndx(output_plan, state,
                                                  shared->verdef_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, state, shared->dynstr_section);
            sh->sh_info = 2U;
        } else if (i + 1U == minild_output_shndx(output_plan, state,
                                                  shared->hash_section) ||
                   (shared->have_gnu_hash &&
                    i + 1U == minild_output_shndx(output_plan, state,
                                                   shared->gnu_hash_section)) ||
                   i + 1U == minild_output_shndx(output_plan, state,
                                                  shared->rela_section) ||
                   i + 1U == minild_output_shndx(output_plan, state,
                                                  shared->rela_plt_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, state, shared->dynsym_section);
        } else if (i + 1U == minild_output_shndx(output_plan, state,
                                                  shared->dynamic_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, state, shared->dynstr_section);
        }
    }
}

static bool shared_write_object(MiniLdState *state,
                                const MiniLdStaticLayout *layout,
                                MiniLdOutputPlan *output_plan,
                                const MiniLdSharedImage *shared,
                                const char *path,
                                uint64_t entry) {
    MiniLdProgramPlan program_plan = {0};
    size_t image_size;
    MiniLdBuffer shstrtab = {NULL, 0U, 0U};
    MiniLdFinalSymbols final_symbols = {0};
    uint32_t symtab_name = 0U;
    uint32_t strtab_name = 0U;
    uint32_t shstrtab_name = 0U;
    uint32_t *name_offsets = NULL;
    Elf64_Shdr *section_headers = NULL;
    unsigned char *image = NULL;
    size_t section_count = output_plan->count + 4U; /* null + symtab + strtab + shstrtab */
    size_t symtab_index = output_plan->count + 1U;
    size_t strtab_index = output_plan->count + 2U;
    size_t shstrtab_index = output_plan->count + 3U;
    size_t symtab_offset;
    size_t strtab_offset;
    size_t shstrtab_offset;
    size_t section_header_offset;
    size_t total_size;
    size_t i;
    Elf64_Ehdr header;
    FILE *file = NULL;
    bool ok = false;

    if (!minild_shared_program_plan_build(state, layout, shared,
                                          &program_plan)) {
        goto done;
    }
    image_size = sizeof(Elf64_Ehdr) +
                 program_plan.count * sizeof(Elf64_Phdr);

    if (section_count > UINT16_MAX) {
        fprintf(state->diagnostics,
                "minic-ld: too-many-shared-sections\n");
        goto done;
    }
    /* GNU nm uses the non-ALLOC .symtab in Linux's vDSO build, not only
     * .dynsym.  Both executable kinds consume the same final symbol view. */
    if (!minild_final_symbols_build(state, layout, output_plan,
                                    &final_symbols)) {
        fprintf(state->diagnostics,
                "minic-ld: cannot-build-shared-static-symbols\n");
        goto oom;
    }
    if (layout->have_rx &&
        layout->rx_file_offset + layout->rx_file_size > image_size) {
        image_size = layout->rx_file_offset + layout->rx_file_size;
    }
    if (layout->have_rw &&
        layout->rw_file_offset + layout->rw_file_size > image_size) {
        image_size = layout->rw_file_offset + layout->rw_file_size;
    }
    /* Synthetic non-ALLOC symbol/name tables are after the LOAD image;
     * no change to program headers or script-derived section positions.
     */
    if (!minild_output_plan_place_nonalloc(state, layout, output_plan,
                                           image_size,
                                           &strtab_offset)) {
        goto done;
    }
    if (!add_size(strtab_offset, final_symbols.names.size, &symtab_offset) ||
        !align_up_size(symtab_offset, 8U, &symtab_offset) ||
        state->resolver.count + 1U > SIZE_MAX / sizeof(Elf64_Sym) ||
        !add_size(symtab_offset,
                  (state->resolver.count + 1U) * sizeof(Elf64_Sym),
                  &shstrtab_offset)) {
        goto oom;
    }

    name_offsets =
        calloc(output_plan->count == 0U ? 1U : output_plan->count,
               sizeof(*name_offsets));
    if (name_offsets == NULL ||
        !buffer_append_zero(&shstrtab, 1U)) {
        goto oom;
    }
    for (i = 0U; i < output_plan->count; ++i) {
        if (!buffer_append_string(&shstrtab,
                                  output_plan->sections[i].name,
                                  &name_offsets[i])) {
            goto oom;
        }
    }
    {
        if (!buffer_append_string(&shstrtab, ".symtab",
                                  &symtab_name) ||
            !buffer_append_string(&shstrtab, ".strtab",
                                  &strtab_name) ||
            !buffer_append_string(&shstrtab, ".shstrtab",
                                  &shstrtab_name)) {
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

    if (!minild_program_plan_validate(state, &program_plan, total_size)) {
        goto done;
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
    header.e_entry = entry;
    header.e_phoff = sizeof(Elf64_Ehdr);
    header.e_shoff = section_header_offset;
    header.e_flags = state->elf_flags;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = (Elf64_Half)program_plan.count;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shnum = (Elf64_Half)section_count;
    header.e_shstrndx = (Elf64_Half)shstrtab_index;
    memcpy(image, &header, sizeof(header));

    memcpy(image + sizeof(Elf64_Ehdr), program_plan.entries,
           program_plan.count * sizeof(Elf64_Phdr));
    if (!minild_output_plan_section_headers(state, output_plan,
                                             name_offsets, section_headers)) {
        goto done;
    }
    minild_shared_output_section_links(state, output_plan, shared,
                                        section_headers);
    if (!minild_output_plan_emit_sections(state, layout, output_plan,
                                          image, total_size)) {
        goto done;
    }

    {
        Elf64_Shdr *sh = &section_headers[symtab_index];
        sh->sh_name = symtab_name;
        sh->sh_type = SHT_SYMTAB;
        sh->sh_offset = symtab_offset;
        sh->sh_size = (state->resolver.count + 1U) * sizeof(Elf64_Sym);
        sh->sh_link = (Elf64_Word)strtab_index;
        sh->sh_info = (Elf64_Word)final_symbols.local_count;
        sh->sh_addralign = 8U;
        sh->sh_entsize = sizeof(Elf64_Sym);
    }
    {
        Elf64_Shdr *sh = &section_headers[strtab_index];
        sh->sh_name = strtab_name;
        sh->sh_type = SHT_STRTAB;
        sh->sh_offset = strtab_offset;
        sh->sh_size = final_symbols.names.size;
        sh->sh_addralign = 1U;
    }
    {
        Elf64_Shdr *sh = &section_headers[shstrtab_index];
        sh->sh_name = shstrtab_name;
        sh->sh_type = SHT_STRTAB;
        sh->sh_offset = shstrtab_offset;
        sh->sh_size = shstrtab.size;
        sh->sh_addralign = 1U;
    }
    memcpy(image + strtab_offset, final_symbols.names.data,
           final_symbols.names.size);
    memcpy(image + symtab_offset, final_symbols.entries,
           (state->resolver.count + 1U) * sizeof(Elf64_Sym));
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
    minild_program_plan_destroy(&program_plan);
    free(section_headers);
    free(image);
    minild_final_symbols_destroy(&final_symbols);
    free(shstrtab.data);
    return ok;

oom:
    fprintf(state->diagnostics,
            "minic-ld: out-of-memory:shared-writer\n");
    goto done;
}

static bool shared_validate_pie_dso_symbols(
    const MiniLdState *state) {
    size_t i;

    if (input_graph_dso_needed_count(&state->inputs) == 0U) {
        return true;
    }
    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        unsigned bind = ELF64_ST_BIND(symbol->info);

        if (symbol->section != MINILD_SECTION_UNDEF ||
            bind == STB_WEAK ||
            bind == STB_LOCAL ||
            symbol->name[0] == '\0') {
            continue;
        }
        if (!state_dso_defines(state, symbol->name)) {
            fprintf(state->diagnostics,
                    "minic-ld: unresolved-pie-symbol:%s\n",
                    symbol->name);
            return false;
        }
    }
    return true;
}

/* -S on an already debug-free ELF shared input is a semantic no-op.
 * Reject unimplemented debug removal rather than silently emit a DSO
 * still containing non-ALLOC debugging sections. .eh_frame is runtime
 * unwind data and is intentionally not classified as debug information.
 */
static bool shared_strip_debug_preflight(const MiniLdState *state) {
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        const char *name = section->name;

        if (section->size == 0U) {
            continue;
        }
        if (strncmp(name, ".debug", 6U) == 0 ||
            strncmp(name, ".zdebug", 7U) == 0 ||
            strncmp(name, ".stab", 5U) == 0 ||
            strcmp(name, ".gdb_index") == 0 ||
            strcmp(name, ".gnu_debuglink") == 0 ||
            strcmp(name, ".gnu_debugaltlink") == 0) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-strip-debug-section:%s\n",
                    name);
            return false;
        }
    }
    return true;
}

/* Shared metadata/dynamic relocation slots are prepared before the vDSO
 * script places its inputs.  Until these builders consume the same liveness
 * map as the final OutputPlan, silently discarding an allocated source could
 * leave a dynamic relocation or exported symbol pointing into dead bytes.
 * Reject that exact unsupported case instead of emitting an invalid DSO. */
static bool shared_script_validate_discarded_alloc(
    const MiniLdState *state, const MiniLdStaticLayout *layout) {
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];

        if (layout->section_discarded[i] &&
            (section->flags & SHF_ALLOC) != 0U &&
            section->size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-script-discard-alloc:%s\n",
                    script_section_source_name(section));
            return false;
        }
    }
    return true;
}

int minild_link_shared_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdSharedOptions *options,
    FILE *diagnostics) {
    const char *soname = options != NULL ? options->soname : NULL;
    const char *entry_symbol =
        options != NULL ? options->entry_symbol : NULL;
    const char *dynamic_list_path =
        options != NULL ? options->dynamic_list_path : NULL;
    const char *version_script_path =
        options != NULL ? options->version_script_path : NULL;
    const char *script_path =
        options != NULL ? options->script_path : NULL;
    const char *needed_name =
        options != NULL ? options->needed_name : NULL;
    const bool pie = options != NULL && options->pie;
    const bool strip_debug = options != NULL && options->strip_debug;
    const bool hash_style_both = options != NULL && options->hash_style_both;
    const bool eh_frame_hdr = options != NULL && options->eh_frame_hdr;
    const char *interpreter_path =
        options != NULL && options->interpreter_path != NULL
            ? options->interpreter_path
            : pie ? "/lib/ld-musl-riscv64.so.1" : NULL;
    MiniLdState state;
    MiniLdStaticLayout layout;
    MiniLdSharedImage shared;
    MiniLdOutputPlan output_plan = {0};
    MiniLdScript version_script;
    MiniLdScript layout_script;
    const MiniLdScript *version = NULL;
    uint64_t entry = 0U;
    bool layout_ready = false;
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    memset(&layout, 0, sizeof(layout));
    memset(&shared, 0, sizeof(shared));
    minild_script_initialize(&version_script);
    minild_script_initialize(&layout_script);
    state.diagnostics = diagnostics;
    state.allow_dso_inputs = true;
    if (version_script_path != NULL) {
        if (!minild_script_parse_file(version_script_path,
                                      &version_script, diagnostics) ||
            version_script.version_name == NULL ||
            !version_script.version_local_all ||
            version_script.command_count != 0U ||
            version_script.phdr_count != 0U ||
            version_script.version_global_count == 0U ||
            soname == NULL || soname[0] == '\0') {
            fprintf(diagnostics,
                    "minic-ld: unsupported-shared-version-script\n");
            goto done;
        }
        version = &version_script;
    }
    if (script_path != NULL) {
        if (version != NULL ||
            !minild_script_parse_file(script_path,
                                      &layout_script, diagnostics) ||
            layout_script.version_name == NULL ||
            layout_script.version_global_count == 0U ||
            !layout_script.version_local_all ||
            layout_script.output_arch == NULL ||
            strcmp(layout_script.output_arch, "riscv") != 0 ||
            soname == NULL || soname[0] == '\0') {
            fprintf(diagnostics,
                    "minic-ld: unsupported-shared-script-contract:%s\n",
                    script_path);
            goto done;
        }
        version = &layout_script;
    }

    if (!process_input_sequence(&state, inputs, input_count) ||
        !state.have_input ||
        (script_path != NULL &&
         (!minild_materialize_input_sections(&state, true) ||
          !minild_restore_input_order(&state))) ||
        (strip_debug && !shared_strip_debug_preflight(&state)) ||
        !static_allocate_common(&state) ||
        (script_path != NULL &&
         !minild_shared_prune_script_inputs(&state, &layout_script)) ||
        !shared_prepare_metadata(&state,
                                 soname,
                                 dynamic_list_path,
                                 needed_name,
                                 interpreter_path,
                                 pie,
                                 hash_style_both,
                                 eh_frame_hdr,
                                 version,
                                 script_path != NULL ? &layout_script : NULL,
                                 &shared) ||
        (pie && !shared_validate_pie_dso_symbols(&state)) ||
        !(script_path != NULL
              ? shared_build_vdso_script_layout(&state, &layout_script,
                                                &layout)
              : shared_build_layout(&state, &layout))) {
        goto done;
    }
    layout_ready = true;
    if (script_path != NULL &&
        !shared_script_validate_discarded_alloc(&state, &layout)) {
        goto done;
    }
    shared.layout_script = script_path != NULL ? &layout_script : NULL;
    if (state.resolver.count > shared.symbol_map_capacity) {
        fprintf(diagnostics,
                "minic-ld: script-symbol-map-exceeded:%zu:%zu\n",
                state.resolver.count, shared.symbol_map_capacity);
        goto done;
    }

    if (!minild_output_plan_build(&state, &layout,
                                  layout.section_file_offset, &output_plan) ||
        !shared_finalize_ehdr_symbol(&state, &layout) ||
        (entry_symbol != NULL &&
         !static_entry_address(&state, &layout, entry_symbol, &entry)) ||
        !shared_fill_dynsym(&state, &layout, &output_plan, &shared) ||
        !shared_fill_hash(&state, &shared) ||
        !shared_fill_gnu_hash(&state, &shared) ||
        !shared_fill_versions(&state, &shared) ||
        !shared_fill_relocations(&state, &layout, &shared) ||
        !shared_patch_final_layout_relocations(&state, &layout, &shared) ||
        !minild_eh_fill(&state, &layout, &shared) ||
        !shared_fill_plt(&state, &layout, &shared) ||
        !shared_fill_dynamic(&state, &layout, &shared) ||
        !shared_write_object(&state,
                             &layout,
                             &output_plan,
                             &shared,
                             output_path,
                             entry)) {
        goto done;
    }
    ok = true;

done:
    minild_output_plan_destroy(&output_plan);
    if (layout_ready || layout.section_vaddr != NULL) {
        static_layout_destroy(&layout);
    }
    shared_image_destroy(&shared);
    state_destroy(&state);
    minild_script_destroy(&version_script);
    minild_script_destroy(&layout_script);
    return ok ? 0 : 1;
}

int minild_link_shared_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *soname,
                                          const char *entry_symbol,
                                          FILE *diagnostics) {
    MiniLdSharedOptions options;

    options.soname = soname;
    options.entry_symbol = entry_symbol;
    options.dynamic_list_path = NULL;
    options.version_script_path = NULL;
    options.script_path = NULL;
    options.needed_name = NULL;
    options.interpreter_path = NULL;
    options.strip_debug = false;
    options.hash_style_both = false;
    options.eh_frame_hdr = false;
    options.pie = false;
    return minild_link_shared_elf64_riscv_inputs_options(output_path,
                                                          inputs,
                                                          input_count,
                                                          &options,
                                                          diagnostics);
}

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
