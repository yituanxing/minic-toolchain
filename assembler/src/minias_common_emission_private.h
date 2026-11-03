#ifndef MINIAS_COMMON_EMISSION_PRIVATE_H
#define MINIAS_COMMON_EMISSION_PRIVATE_H

#include "minias_common_emission.h"

/* Private transactional write requests. Other Common domains borrow only a
 * const Fixup span or the current MCData cursor, never these vectors. */
typedef struct MiniAsEncodeFixupRequest {
    uint32_t fragment_offset;
    MiniAsFixupKind kind;
    unsigned width;
    MiniAsSymbolRef symbol_ref;
    /* MCData may retain an existing parse-site SymbolID instead of a name. */
    bool has_symbol_index;
    size_t symbol_index;
    bool absolute_zero_symbol; /* ELF STN_UNDEF=0 linker marker */
    bool prefer_section_symbol;
} MiniAsEncodeFixupRequest;

typedef struct MiniAsEncodeAnchorRequest {
    MiniAsSymbolNameId name_id;
} MiniAsEncodeAnchorRequest;

typedef enum MiniAsFragmentWriteKind {
    MINIAS_WRITE_COPIED_BYTES,
    MINIAS_WRITE_BORROWED_BYTES,
    MINIAS_WRITE_PATTERN
} MiniAsFragmentWriteKind;

/* Ordered output writes. Borrowed .incbin bytes and compact fills avoid
 * copying large source snapshots into a contiguous intermediate buffer. */
typedef struct MiniAsFragmentWrite {
    MiniAsFragmentWriteKind kind;
    size_t size;
    size_t copied_offset;
    const unsigned char *borrowed_bytes;
    unsigned char pattern[8];
    size_t repeat_count;
    unsigned repeat_width;
    bool ignore_nobits_fill;
} MiniAsFragmentWrite;

typedef struct MiniAsEncodeResult {
    unsigned char *bytes;
    size_t byte_count; /* logical size, including compact writes */
    size_t byte_storage_size; /* only physically copied bytes */
    size_t byte_capacity;
    MiniAsFragmentWrite *writes;
    size_t write_count;
    size_t write_capacity;
    MiniAsEncodeFixupRequest *fixups;
    size_t fixup_count;
    size_t fixup_capacity;
    MiniAsEncodeAnchorRequest *anchors;
    size_t anchor_count;
    size_t anchor_capacity;
    uint32_t target_output_features_or;
} MiniAsEncodeResult;

/* Bytes are not Section metadata. The Emission owner retains the sole
 * materialized payload for each Context SectionID, including NOBITS size. */
typedef struct MiniAsEmittedSection {
    unsigned char *data;
    size_t size;
    size_t capacity;
} MiniAsEmittedSection;

/* The Emission owner allocates/releases this whole graph. Only the output
 * transaction and its shared MCData/Target request encoder include this
 * definition. No generic parser, layout or ELF writer can mutate it. */
struct MiniAsEmissionState {
    MiniAsEmittedSection *section_bytes;
    size_t section_byte_count;
    size_t section_byte_capacity;
    MiniAsFixup *fixups;
    size_t fixup_count;
    size_t fixup_capacity;
    bool emitting_fragment;
    bool committing_fragment;
    size_t emitting_fragment_id;
    /* Final emission-order predecessor, not the previous source ordinal.
     * SIZE_MAX before the first successfully committed Fragment. */
    size_t last_emitted_fragment_id;
    MiniAsEncodeResult *active_encode_result;
    size_t synthetic_anchor_counter;
};

void minias_encode_result_destroy(MiniAsEncodeResult *);

#endif
