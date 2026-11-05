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
/* A source-defined symbol is resolved for archive selection before its
 * eventual WorkSection exists. Projection must eliminate this marker. */
#define MINILD_SECTION_PENDING (-4)

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
    /* Shared -T records a future script definition in this canonical slot
     * before dynamic-symbol and relocation sizing.  A newly reserved name
     * stays truly undefined until the script evaluator assigns its value;
     * it must not count as an original undefined reference for PROVIDE. */
    bool script_planned_defined;
    /* Known SHN_ABS before shared dynamic relocation reservation; false
     * is conservative for dot/section-relative script expressions. */
    bool script_planned_absolute;
    bool script_placeholder;
    /* A linker-script dot assignment may precede the first real fragment
     * of its output section.  Its section-relative offset is then a small
     * negative delta, unlike an input ELF symbol's unsigned offset. */
    bool script_relative_negative;
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
    /* Source section is the target of an SHF_LINK_ORDER reference in its
     * original object, even if that metadata later loses COMDAT selection. */
    bool link_order_target;
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
 * It is allocated only during post-admission projection. Later work-section
 * permutations remap this single mutable owner without touching sources. */
typedef struct MiniLdInputPlacement {
    size_t work_section;
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

typedef struct MiniLdTargetProfile MiniLdTargetProfile;

typedef struct MiniLdScriptForwardGuess {
    const char *name; /* borrowed from the lifetime of parsed script AST */
    uint64_t value;
} MiniLdScriptForwardGuess;

static void symbol_resolver_destroy(MiniLdSymbolResolver *resolver);
static void work_sections_destroy(MiniLdSectionWorkspace *work);

typedef struct MiniLdState {
    const MiniLdTargetProfile *target;
    MiniLdInputGraph inputs;
    MiniLdSectionWorkspace work;
    MiniLdSymbolResolver resolver;
    MiniLdRelocationWorkspace relocations;
    /* Immutable source work-section prefix at the layout checkpoint.
     * Script-generated BYTE/NOBITS tails may change between Target rounds. */
    size_t static_canonical_work_count;
    uint32_t elf_flags;
    size_t processed_object_count;
    bool have_input;
    bool allow_dso_inputs;
    bool relocatable_output;
    bool nmagic;
    bool static_pie_requested;
    /* Target-owned ET_DYN kernel image: linker emits rebasing RELA records
     * without installing their absolute values into the image bytes. */
    bool kernel_shared_image;
    /* Script layout may contain a bounded forward dependency, such as an
     * image-end expression in the calculation of page-table allocation.
     * Only placement expressions consume estimates; completed assignments
     * are re-evaluated and checkpointed until they stop changing. */
    bool script_forward_allow;
    bool script_forward_changed;
    MiniLdScriptForwardGuess *script_forward_guesses;
    size_t script_forward_guess_count;
    size_t script_forward_guess_capacity;
    bool fix_cortex_a53_843419;
    uint64_t max_page_size;
    bool separate_code;
    bool orphan_handling_error;
    bool build_id_sha1;
    bool discard_locals;
    FILE *diagnostics;
} MiniLdState;

/* Implemented by the input-projection owner included by linker.c after this
 * core. The EH indexer scans projected source CFI fragments, so shared
 * scripts retain original input identity. */
typedef enum MiniLdInputProjection {
    MINILD_PROJECTION_RELOCATABLE,
    MINILD_PROJECTION_MERGED,
    MINILD_PROJECTION_GC,
    MINILD_PROJECTION_SCRIPT
} MiniLdInputProjection;

static bool minild_project_admitted_inputs(MiniLdState *state,
                                           MiniLdInputProjection projection);
static bool minild_restore_input_order(MiniLdState *state);
static bool minild_shared_prune_script_inputs(MiniLdState *state,
                                              const MiniLdScript *script);

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

/* ELF class selects the on-disk ABI record widths once. Layout, script
 * header reservations and final serialization must use the same contract;
 * no output phase may silently assume the host's or another target's ELF64
 * record sizes. This is file geometry, not an ISA or relocation decision. */
typedef struct MiniLdElfAbiSizes {
    size_t ehdr;
    size_t phdr;
    size_t shdr;
    size_t sym;
    size_t word_alignment;
} MiniLdElfAbiSizes;

static MiniLdElfAbiSizes minild_elf_abi_sizes(unsigned char elf_class) {
    if (elf_class == ELFCLASS32) {
        return (MiniLdElfAbiSizes){sizeof(Elf32_Ehdr), sizeof(Elf32_Phdr),
                                   sizeof(Elf32_Shdr), sizeof(Elf32_Sym), 4U};
    }
    if (elf_class == ELFCLASS64) {
        return (MiniLdElfAbiSizes){sizeof(Elf64_Ehdr), sizeof(Elf64_Phdr),
                                   sizeof(Elf64_Shdr), sizeof(Elf64_Sym), 8U};
    }
    return (MiniLdElfAbiSizes){0};
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

static size_t find_section_index(const MiniLdState *state,
                                 const char *name,
                                 uint64_t hash) {
    return work_sections_find_index(&state->work, name, hash);
}

/* Symbol resolution has its own owner; input, archive, script and output
 * stages consume its narrow API without managing its name index. */
#include "linker_symbol.inc"

static bool find_or_add_section(MiniLdState *state,
                                const char *name,
                                uint32_t type,
                                uint64_t flags,
                                uint64_t align,
                                uint64_t entsize,
                                size_t *section_out) {
    return work_sections_find_or_add(&state->work, state->diagnostics,
                                     name, type, flags, align, entsize,
                                     false, section_out);
}

#include "linker_common_allocation.inc"

/* ELF/psABI identity is a Target decision shared by every input and output
 * mode; it is available before archive admission and ET_REL writing. */
#include "linker_target_elf.inc"

#include "linker_admission.inc"
#include "linker_relocatable_output.inc"

/* Output-phase implementation order is intentional: layout/relocation
 * primitives, output-section projection, final symbols/section tables,
 * common program-header contracts, then distinct static and shared ELF
 * writers. These remain one C translation
 * unit; moving an owner here does not introduce an extra state copy. */
#include "linker_layout.inc"

#include "linker_output_plan.inc"

#include "linker_final_symbols.inc"

#include "linker_program_plan.inc"

#include "linker_final_image_plan.inc"

#include "linker_final_elf_image.inc"

#include "linker_static_output.inc"

/* One layout checkpoint/rollback owner serves both static and shared image
 * paths; Target relaxation must not grow a separate shared-only algorithm. */
#include "linker_static_layout_fixpoint.inc"

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

int minild_link_relocatable_inputs_target(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *emulation,
                                          FILE *diagnostics) {
    MiniLdState state;
    const MiniLdTargetProfile *target = minild_target_find(emulation);
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    if (target == NULL || !target->supports_relocatable) {
        fprintf(diagnostics, "minic-ld: unsupported-target-relocatable:%s\n",
                emulation == NULL ? "(null)" : emulation);
        return 2;
    }
    memset(&state, 0, sizeof(state));
    state.target = target;
    state.diagnostics = diagnostics;
    state.relocatable_output = true;

    if (!process_input_sequence(&state, inputs, input_count)) {
        goto done;
    }

    if (!state.have_input ||
        !minild_project_admitted_inputs(&state,
                                        MINILD_PROJECTION_RELOCATABLE) ||
        !minild_write_relocatable_output(&state, output_path)) {
        goto done;
    }
    ok = true;

done:
    state_destroy(&state);
    return ok ? 0 : 1;
}

int minild_link_relocatable_elf64_riscv_inputs(const char *output_path,
                                               const MiniLdInput *inputs,
                                               size_t input_count,
                                               FILE *diagnostics) {
    return minild_link_relocatable_inputs_target(
        output_path, inputs, input_count, "elf64lriscv", diagnostics);
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
