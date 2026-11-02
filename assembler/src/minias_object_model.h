#ifndef MINIAS_OBJECT_MODEL_H
#define MINIAS_OBJECT_MODEL_H

#include "minias_internal.h"

/* The object-format-neutral terminal borrow. SectionID, SymbolID and Fixup
 * records stay in their existing Context/Emission owners. This reader has no
 * parser, layout mutation, encoder or output-file capability. */
typedef struct MiniAsObjectFragmentLocation {
    int section;
    uint64_t offset;
    size_t size;
    size_t line;
} MiniAsObjectFragmentLocation;

typedef struct MiniAsObjectModelView {
    const MiniAsSection *sections;
    size_t section_count;
    const MiniAsSymbol *symbols;
    size_t symbol_count;
    const MiniAsFixup *fixups;
    size_t fixup_count;
    size_t cu_file_count;
    const void *reader;
    const char *(*cu_file)(const void *,size_t);
    /* Context resolves a name to a checked stable SymbolID. Never derive an
     * ID by subtracting unrelated or potentially reallocated pointers. */
    bool (*find_symbol_id)(const void *,const char *,size_t *);
    int (*find_section)(const void *,const char *);
    bool (*numeric_reference)(const char *);
    bool (*fragment_location)(const void *,size_t,MiniAsObjectFragmentLocation *);
    bool (*section_bytes)(const void *,int,const unsigned char **,size_t *);
} MiniAsObjectModelView;

/* A single short-lived resolution of logical references and final emitted
 * extents. These are *indices and borrows*, not copies of Section/Symbol/
 * Fixup entities. Formats translate them to their own indices and records. */
typedef struct MiniAsObjectSectionPlan {
    const unsigned char *bytes;
    size_t size;
    size_t signature_symbol; /* SIZE_MAX if no COMDAT/group signature */
    size_t linked_section; /* SIZE_MAX if no logical association */
} MiniAsObjectSectionPlan;

typedef struct MiniAsObjectFixupPlan {
    uint64_t final_offset;
} MiniAsObjectFixupPlan;

typedef struct MiniAsObjectPlan {
    MiniAsObjectSectionPlan *sections;
    MiniAsObjectFixupPlan *fixups;
} MiniAsObjectPlan;

bool minias_object_plan(const MiniAsObjectModelView *,MiniAsObjectPlan *,
                        char *error,size_t error_capacity);
void minias_object_dispose_plan(MiniAsObjectPlan *);

/* Atomic publication belongs to the output boundary, not any one object
 * format. Non-regular destinations retain stream semantics. */
bool minias_object_publish_image(const char *path,const unsigned char *image,
                                 size_t size,char *error,size_t error_capacity);

#endif
