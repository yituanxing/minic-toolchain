#include "minias_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The encoder's temporary result is an MC fragment proposal.  It does not
 * own a final ELF symbol index, Section cursor or global Fixup array slot.
 * Target failures discard the entire proposal before Common has modified the
 * object. A Common commit failure aborts assembly without writing an ELF.
 */
static MiniAsEncodeResult *active_result(MiniAs *as, const char *operation) {
    if (as == NULL || !as->emitting_fragment ||
        !as->capture_instruction_bytes || as->active_encode_result == NULL ||
        as->emitting_fragment_id >= as->stmt_count) {
        if (as != NULL)
            minias_set_error(as, "encode-result-without-fragment:%s",
                             operation);
        return NULL;
    }
    return as->active_encode_result;
}

bool minias_encode_add_fixup(MiniAs *as, int section, uint64_t offset,
                             MiniAsFixupKind kind, unsigned width,
                             const char *symbol_name, int64_t addend,
                             bool prefer_section_symbol) {
    MiniAsEncodeResult *result = active_result(as, "fixup");
    const MiniAsStmt *owner;
    MiniAsEncodeFixupRequest *request;
    uint64_t relative;
    char *owned_name;
    if (result == NULL) return false;
    owner = &as->stmts[as->emitting_fragment_id];
    if (owner->kind != MINIAS_STMT_INSN ||
        symbol_name == NULL || *symbol_name == '\0' ||
        section != owner->section || section < 0 ||
        (size_t)section >= as->section_count ||
        as->sections[(size_t)section].type == MINIAS_SHT_NOBITS ||
        offset < owner->offset ||
        (width != 2U && width != 4U && width != 8U)) {
        minias_set_error(as, "invalid-encoded-fixup:line=%zu", owner->line);
        return false;
    }
    relative = offset - owner->offset;
    if (relative > UINT32_MAX || relative > owner->size ||
        width > owner->size - relative) {
        minias_set_error(as, "encoded-fixup-outside-fragment:line=%zu",
                         owner->line);
        return false;
    }
    owned_name = minias_strdup(symbol_name);
    if (owned_name == NULL) {
        minias_set_error(as, "out-of-memory:encode-fixup-name");
        return false;
    }
    if (result->fixup_count == SIZE_MAX ||
        !minias_grow_array((void **)&result->fixups,
                           &result->fixup_capacity,
                           sizeof(*result->fixups),
                           result->fixup_count + 1U)) {
        free(owned_name);
        minias_set_error(as, "out-of-memory:encode-fixup");
        return false;
    }
    request = &result->fixups[result->fixup_count++];
    request->section = section;
    request->fragment_offset = (uint32_t)relative;
    request->kind = kind;
    request->width = width;
    request->symbol_name = owned_name;
    request->addend = addend;
    request->prefer_section_symbol = prefer_section_symbol;
    return true;
}

bool minias_encode_request_anchor(MiniAs *as, const MiniAsStmt *stmt,
                                  const char *prefix, char *out,
                                  size_t capacity) {
    MiniAsEncodeResult *result = active_result(as, "anchor");
    MiniAsEncodeAnchorRequest *request;
    char *owned_name;
    size_t ordinal;
    if (result == NULL) return false;
    if (stmt == NULL || stmt != &as->stmts[as->emitting_fragment_id] ||
        stmt->kind != MINIAS_STMT_INSN ||
        stmt->section < 0 || (size_t)stmt->section >= as->section_count ||
        result->anchor_count >= SIZE_MAX - as->synthetic_anchor_counter) {
        minias_set_error(as, "invalid-encoded-anchor-request");
        return false;
    }
    ordinal = as->synthetic_anchor_counter + result->anchor_count + 1U;
    if (!minias_make_private_symbol_name(out, capacity, prefix, ordinal)) {
        minias_set_error(as, "encoded-anchor-name-overflow");
        return false;
    }
    /* Private, non-source-addressable names are stable across retries. */
    if (minias_get_symbol(as, out, false) != NULL) {
        minias_set_error(as, "duplicate-encoded-anchor");
        return false;
    }
    owned_name = minias_strdup(out);
    if (owned_name == NULL) {
        minias_set_error(as, "out-of-memory:encode-anchor-name");
        return false;
    }
    if (result->anchor_count == SIZE_MAX ||
        !minias_grow_array((void **)&result->anchors,
                           &result->anchor_capacity,
                           sizeof(*result->anchors),
                           result->anchor_count + 1U)) {
        free(owned_name);
        minias_set_error(as, "out-of-memory:encode-anchor");
        return false;
    }
    request = &result->anchors[result->anchor_count++];
    request->name = owned_name;
    request->section = stmt->section;
    request->subsection = stmt->subsection;
    request->offset = stmt->offset;
    return true;
}

bool minias_encode_add_elf_flags(MiniAs *as, uint32_t flags) {
    MiniAsEncodeResult *result = active_result(as, "elf-flags");
    if (result == NULL) return false;
    result->elf_flags_or |= flags;
    return true;
}

void minias_encode_result_destroy(MiniAsEncodeResult *result) {
    size_t i;
    if (result == NULL) return;
    for (i = 0U; i < result->fixup_count; ++i)
        free(result->fixups[i].symbol_name);
    for (i = 0U; i < result->anchor_count; ++i)
        free(result->anchors[i].name);
    free(result->bytes);
    free(result->fixups);
    free(result->anchors);
    memset(result, 0, sizeof(*result));
}
