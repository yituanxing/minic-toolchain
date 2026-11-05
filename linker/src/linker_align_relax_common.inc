/* Generic section-offset projection after target-owned alignment shrinking.
 * The architecture decides which bytes may be removed and their encoding;
 * Common maps the resulting source offsets for symbols and relocations. */
typedef struct MiniLdRelaxEvent {
    size_t raw_end;
    size_t cumulative_deleted;
} MiniLdRelaxEvent;

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
